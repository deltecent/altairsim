#include "host/hostdir.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

// std::filesystem, and no #ifdef (DESIGN.md 2.1). Directory iteration, symlink
// resolution and path canonicalization are all portable C++17, so this file has no
// business knowing which OS it is on -- and the platform lint agrees.
namespace fs = std::filesystem;

namespace altair {

const char* hbErrorText(HbError e) {
    switch (e) {
    case HbError::None:       return "no error";
    case HbError::NotFound:   return "no such file";
    case HbError::Permission: return "permission denied";
    case HbError::Outside:    return "outside the host directory";
    case HbError::Io:         return "host I/O error";
    case HbError::NoFile:     return "no file open";
    case HbError::TooLarge:   return "file too large";
    case HbError::BadName:    return "bad file name";
    case HbError::Ambiguous:  return "ambiguous name -- several host files differ only in case";
    }
    return "unknown error";
}

// ASCII upper-case fold. Deliberately not locale-aware: these are filenames on a
// wire between an 8080 and a host, not text, and a Turkish locale must not change
// which file `R README.TXT` opens.
static char fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

// ---------------------------------------------------------------------------
// The name gate. Text only -- nothing here touches the disk.
//
// A GUEST NAME IS A RELATIVE PATH, AND BOTH SEPARATORS ARE ACCEPTED EVERYWHERE.
//
// `SRC/FOO.ASM` and `SRC\FOO.ASM` mean the same thing on every host, and this is the
// only place that is true -- below here it is a native fs::path like any other. That
// is deliberate: it means ONE R.COM assembled once works against a Mac, a Linux box
// and a Windows box without being told which it is talking to, and it is why this
// card has no "what is your path separator?" command. (AltairZ80's pseudo-device has
// one -- getHostOSPathSeparator, command 28 -- and both its utilities must ask, then
// rewrite their own usage text with the answer. The guest should not have to care.)
//
// WHAT IS STILL REFUSED IS EVERY WAY OUT:
//   - an ABSOLUTE path (a leading separator) -- that is not a name in our root
//   - a DRIVE LETTER (`:`) -- likewise, and it is absolute on the one OS it means
//     anything to
//   - a `..` COMPONENT -- the classic
//   - control characters
//
// Note `..` is rejected as a COMPONENT, not as a substring: `a..b` is a perfectly
// ordinary filename and there is no reason to refuse it. The substring test would be
// cheaper and would be wrong.
//
// And none of the above is the real defence. It is the cheap, early one. The real
// defence is resolve(), which canonicalizes what all this text produces and proves it
// still lives under the root -- because that is the only thing that can see a symlink.
// ---------------------------------------------------------------------------
bool splitHostName(const std::string& name, std::vector<std::string>& parts, HbFail& err) {
    auto no = [&](const std::string& why) {
        err.code = HbError::Outside;
        err.msg  = why;
        return false;
    };

    parts.clear();

    if (name.empty()) {
        err.code = HbError::BadName;
        err.msg  = "empty file name";
        return false;
    }

    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) {
            err.code = HbError::BadName;
            err.msg  = "control character in file name";
            return false;
        }
        if (c == ':') return no("'" + name + "': a drive letter cannot name a file in the sandbox");
    }

    if (name.front() == '/' || name.front() == '\\')
        return no("'" + name + "': an absolute path is outside the host directory");

    // Split on EITHER separator. This is the line that makes one .COM work on three
    // operating systems.
    std::string cur;
    for (char c : name) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);

    if (parts.empty()) return no("'" + name + "': names no file");

    for (const std::string& p : parts) {
        if (p == "..") return no("'" + name + "': '..' is outside the host directory");
        // A bare `.` component is noise, not an attack. Drop it rather than refuse --
        // `./FOO.ASM` is what a shell-trained human types.
    }
    parts.erase(std::remove(parts.begin(), parts.end(), std::string(".")), parts.end());

    if (parts.empty()) return no("'" + name + "': names no file");
    return true;
}

bool validateHostName(const std::string& name, HbFail& err) {
    std::vector<std::string> parts;
    return splitHostName(name, parts, err);
}

// ---------------------------------------------------------------------------
// Glob. `*` and `?`, folded case.
// ---------------------------------------------------------------------------
bool hostGlobMatch(const std::string& glob, const std::string& name) {
    if (glob.empty()) return true;  // no pattern means everything

    // The classic two-pointer backtracking matcher: linear in practice, no
    // recursion, and it cannot blow the stack on a pathological pattern.
    size_t g = 0, n = 0;
    size_t star = std::string::npos, mark = 0;

    while (n < name.size()) {
        if (g < glob.size() && (glob[g] == '?' || fold(glob[g]) == fold(name[n]))) {
            ++g;
            ++n;
        } else if (g < glob.size() && glob[g] == '*') {
            star = g++;
            mark = n;
        } else if (star != std::string::npos) {
            g = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (g < glob.size() && glob[g] == '*') ++g;
    return g == glob.size();
}

// ---------------------------------------------------------------------------
// RealHostDir
// ---------------------------------------------------------------------------

// Turn a std::error_code into something the guest can act on AND a human can read.
static void fromErrno(const std::error_code& ec, HbFail& err) {
    if (ec == std::errc::no_such_file_or_directory) {
        err.code = HbError::NotFound;
    } else if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system) {
        err.code = HbError::Permission;
    } else {
        err.code = HbError::Io;
    }
    err.msg = ec.message();  // the host's own words -- strerror, verbatim
}

// The case-folded fallback. See the long note in hostdir.h -- the short version is
// that CP/M's CCP destroys the case of the name before the guest program runs, so an
// exact-match-only card could never open `readme.txt` from a CP/M prompt.
bool RealHostDir::findCaseInsensitive(const std::string& canonRoot, const std::string& name,
                                      std::string& out, HbFail& err) const {
    std::error_code ec;

    auto foldeq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (fold(a[i]) != fold(b[i])) return false;
        return true;
    };

    std::vector<std::string> hits;
    for (fs::directory_iterator it(canonRoot, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!fs::is_regular_file(it->path(), ec2) || ec2) continue;
        std::string have = it->path().filename().string();
        if (foldeq(have, name)) hits.push_back(have);
    }

    if (hits.empty()) {
        err.code = HbError::NotFound;
        err.msg  = name + ": no such file";
        return false;
    }
    if (hits.size() > 1) {
        // Refuse rather than guess. Picking one would be a transfer that succeeds and
        // moves the wrong file, which is the worst outcome available.
        err.code = HbError::Ambiguous;
        err.msg  = name + ": matches several host files, differing only in case";
        return false;
    }

    out = (fs::path(canonRoot) / hits[0]).string();
    return true;
}

// Is `canon` the root itself, or something under it?
//
// COMPONENT-WISE, not a string prefix. A string compare would say that
// `/tmp/sandbox-evil` is inside `/tmp/sandbox`, which is a sandbox escape that a
// `starts_with` would wave straight through -- and it is the classic way this check
// is got wrong.
static bool within(const fs::path& canonRoot, const fs::path& canon) {
    auto r = canonRoot.begin(), rend = canonRoot.end();
    auto c = canon.begin(), cend = canon.end();
    for (; r != rend; ++r, ++c) {
        if (c == cend || *c != *r) return false;
    }
    return c != cend;  // equal to the root is not a FILE in the root
}

bool RealHostDir::resolve(const std::string& name, bool mustExist, std::string& out,
                          HbFail& err) const {
    std::vector<std::string> parts;
    if (!splitHostName(name, parts, err)) return false;

    std::error_code ec;

    // An empty root is the shell's working directory (core/paths.h's rule, and the
    // reason `hostdir` can have a useful default at all).
    fs::path rootp = root_.empty() ? fs::current_path(ec) : fs::path(root_);
    if (ec) {
        fromErrno(ec, err);
        return false;
    }

    // The root must EXIST and be a directory. A `hostdir` pointing at a typo is a
    // configuration error, and it should say so rather than reporting every file in
    // it as missing.
    fs::path canonRoot = fs::canonical(rootp, ec);
    if (ec) {
        err.code = HbError::Outside;
        err.msg  = "host directory '" + (root_.empty() ? std::string(".") : root_) +
                  "' does not exist";
        return false;
    }
    if (!fs::is_directory(canonRoot, ec)) {
        err.code = HbError::Outside;
        err.msg  = "host directory '" + canonRoot.string() + "' is not a directory";
        return false;
    }

    // Build the path a component at a time. `parts` has already been proved free of
    // `..`, of absolutes and of drive letters, so this can only ever go DOWN.
    fs::path target = canonRoot;
    for (const std::string& p : parts) target /= p;

    // THE CASE-FOLDED FALLBACK (hostdir.h). An exact match always wins; only when
    // there is none do we fold and look again -- so a host that HAS `README.TXT` is
    // never surprised, and a host that only has `readme.txt` is still reachable from a
    // CP/M prompt that cannot type it.
    //
    // The fold applies to the FINAL component, in whatever directory the earlier
    // components landed in. That is enough for the case CP/M actually creates (the CCP
    // upper-cased the name the user typed); folding intermediate directories too would
    // multiply the directory scans for a case nobody has hit.
    if (!fs::exists(target, ec)) {
        std::string found;
        HbFail      f;
        if (findCaseInsensitive(target.parent_path().string(), parts.back(), found, f)) {
            target = fs::path(found);
        } else if (f.code == HbError::Ambiguous) {
            err = f;  // never guess between two files
            return false;
        } else if (mustExist) {
            err = f;  // NotFound, and it names the file
            return false;
        }
        // else: a WRITE of a name that does not exist in any case. That is a new file,
        // and it gets the name the guest asked for, exactly as asked.
    }

    // THE SYMLINK GATE, AND IT IS THE ONLY THING THAT CATCHES ONE.
    //
    // Everything above was TEXT. Text cannot see that `SRC` is a symlink to `/`, or
    // that `NOTES.TXT` points at your ssh key -- those are perfectly legal names, and
    // only the filesystem knows where they go. So we ask it: resolve the path for real
    // (weakly, because a file we are about to CREATE does not exist yet) and prove what
    // comes back still lives under the root.
    //
    // This is why subdirectories cost us nothing in safety. The text gate got one line
    // more permissive; THIS gate did not move at all, and it is the one doing the work.
    fs::path canon = fs::weakly_canonical(target, ec);
    if (ec) {
        fromErrno(ec, err);
        return false;
    }

    if (!within(canonRoot, canon)) {
        err.code = HbError::Outside;
        err.msg  = "'" + name + "' resolves outside the host directory";
        return false;
    }

    out = canon.string();
    return true;
}

bool RealHostDir::read(const std::string& name, size_t maxBytes, std::vector<uint8_t>& out,
                       HbFail& err) {
    std::string path;
    if (!resolve(name, true, path, err)) return false;

    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        err.code = HbError::NotFound;
        err.msg  = name + ": no such file";
        return false;
    }

    // Ask BEFORE allocating. The whole point of the cap is not to be talked into a
    // multi-gigabyte vector by a guest that named the wrong thing.
    auto sz = fs::file_size(path, ec);
    if (ec) {
        fromErrno(ec, err);
        return false;
    }
    if (maxBytes != 0 && sz > maxBytes) {
        err.code = HbError::TooLarge;
        err.msg  = name + ": too large for the host bridge";
        return false;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err.code = HbError::Permission;
        err.msg  = name + ": cannot open for reading";
        return false;
    }

    out.resize((size_t)sz);
    if (sz > 0 && !f.read((char*)out.data(), (std::streamsize)sz)) {
        err.code = HbError::Io;
        err.msg  = name + ": read failed";
        out.clear();
        return false;
    }
    return true;
}

bool RealHostDir::write(const std::string& name, const std::vector<uint8_t>& bytes, HbFail& err) {
    // mustExist = false: writing a name nobody has is how a new file is made.
    std::string path;
    if (!resolve(name, false, path, err)) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err.code = HbError::Permission;
        err.msg  = name + ": cannot open for writing";
        return false;
    }
    if (!bytes.empty()) f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    f.flush();
    if (!f) {
        err.code = HbError::Io;
        err.msg  = name + ": write failed";
        return false;
    }
    return true;
}

bool RealHostDir::remove(const std::string& name, HbFail& err) {
    std::string path;
    if (!resolve(name, true, path, err)) return false;

    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        err.code = HbError::NotFound;
        err.msg  = name + ": no such file";
        return false;
    }
    if (!fs::remove(path, ec) || ec) {
        fromErrno(ec, err);
        return false;
    }
    return true;
}

bool RealHostDir::list(const std::string& glob, std::vector<std::string>& out, HbFail& err) {
    std::error_code ec;

    fs::path rootp = root_.empty() ? fs::current_path(ec) : fs::path(root_);
    if (ec) {
        fromErrno(ec, err);
        return false;
    }
    fs::path canonRoot = fs::canonical(rootp, ec);
    if (ec) {
        err.code = HbError::Outside;
        err.msg =
            "host directory '" + (root_.empty() ? std::string(".") : root_) + "' does not exist";
        return false;
    }

    // A GLOB MAY CARRY A DIRECTORY. `HDIR SRC/*.ASM` lists SRC, not the root -- so the
    // pattern splits at the last separator into "where to look" and "what to match".
    // The wildcard lives only in the last part; a `*` in the directory part would mean
    // walking a tree, and this card does not walk trees.
    std::string dirPart, pattern = glob;
    size_t      cut = glob.find_last_of("/\\");
    if (cut != std::string::npos) {
        dirPart = glob.substr(0, cut);
        pattern = glob.substr(cut + 1);
    }

    fs::path where = canonRoot;
    if (!dirPart.empty()) {
        // The directory goes through the SAME gates as a file would -- text, then
        // canonicalize, then prove it is under the root. A symlinked directory pointing
        // out is exactly as much of an escape as a symlinked file, and `HDIR ../` must
        // not work any better than `R ../x` does.
        std::string resolved;
        if (!resolve(dirPart, true, resolved, err)) return false;
        where = fs::path(resolved);
        if (!fs::is_directory(where, ec) || ec) {
            err.code = HbError::NotFound;
            err.msg  = dirPart + ": not a directory";
            return false;
        }
    }

    out.clear();
    for (fs::directory_iterator it(where, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;

        const bool isDir  = fs::is_directory(it->path(), ec2) && !ec2;
        const bool isFile = fs::is_regular_file(it->path(), ec2) && !ec2;
        if (!isDir && !isFile) continue;  // a socket, a device, a dangling symlink

        std::string name = it->path().filename().string();
        if (!hostGlobMatch(pattern, name)) continue;

        // THE SAME SYMLINK GATE AS resolve(). A link inside the sandbox pointing out of
        // it is NOT LISTED -- because a name HDIR advertises and R then refuses is a
        // worse experience than a name that was simply never shown.
        fs::path canon = fs::weakly_canonical(it->path(), ec2);
        if (ec2 || !within(canonRoot, canon)) continue;

        // Hand back something the guest can feed STRAIGHT BACK to OPEN_READ. So the
        // directory prefix comes along, and `/` is the separator we speak -- which the
        // guest may return to us as either, since splitHostName() takes both.
        std::string full = dirPart.empty() ? name : dirPart + "/" + name;

        // A directory is marked, so HDIR can show it and R's wildcard loop can skip it.
        // A guest that tried to OPEN_READ a directory would otherwise get a confusing
        // I/O error instead of an obvious "that is a folder".
        if (isDir) full += "/";

        out.push_back(full);
    }

    std::sort(out.begin(), out.end());
    return true;
}

// ---------------------------------------------------------------------------
// MemHostDir
// ---------------------------------------------------------------------------
bool MemHostDir::read(const std::string& name, size_t maxBytes, std::vector<uint8_t>& out,
                      HbFail& err) {
    if (!validateHostName(name, err)) return false;

    auto it = files_.find(name);
    if (it == files_.end()) {
        err.code = HbError::NotFound;
        err.msg  = name + ": no such file";
        return false;
    }
    if (maxBytes != 0 && it->second.size() > maxBytes) {
        err.code = HbError::TooLarge;
        err.msg  = name + ": too large for the host bridge";
        return false;
    }
    out = it->second;
    return true;
}

bool MemHostDir::write(const std::string& name, const std::vector<uint8_t>& bytes, HbFail& err) {
    if (!validateHostName(name, err)) return false;

    if (writeFails_ != HbError::None) {
        err.code = writeFails_;
        err.msg  = name + ": " + hbErrorText(writeFails_);
        return false;
    }
    files_[name] = bytes;
    return true;
}

bool MemHostDir::remove(const std::string& name, HbFail& err) {
    if (!validateHostName(name, err)) return false;

    if (files_.erase(name) == 0) {
        err.code = HbError::NotFound;
        err.msg  = name + ": no such file";
        return false;
    }
    return true;
}

bool MemHostDir::list(const std::string& glob, std::vector<std::string>& out, HbFail&) {
    out.clear();
    for (const auto& [name, bytes] : files_) {
        (void)bytes;
        if (hostGlobMatch(glob, name)) out.push_back(name);
    }
    std::sort(out.begin(), out.end());  // std::map is already sorted; say so anyway
    return true;
}

} // namespace altair
