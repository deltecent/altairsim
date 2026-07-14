#pragma once
//
// HostDir -- the sandboxed host directory a guest program can reach (DESIGN.md
// 12.1, docs/boards/hostbridge.md).
//
// THE HOST BRIDGE IS THE ONLY CARD THAT LETS THE GUEST NAME A HOST FILE, and that
// makes it the only card that can be talked into opening the wrong one. So the
// naming, the rooting and the escape checks do NOT live on the board: they live
// here, in the host layer, where a board never sees a path and where the tests
// that matter can be written against the service alone.
//
// Same rule as MediaFile (host/media.h): a board asks for bytes and gets bytes. It
// does not hold a file handle, it does not know what a symlink is, and it cannot
// reach around this to std::filesystem.
//
// THE NAMESPACE IS FLAT, ON PURPOSE. No subdirectories, so a guest name is a
// FILENAME and never a path -- which deletes a whole class of escape bug rather
// than defending against it. `docs/boards/hostbridge.md` says so in Limitations,
// and validateHostName() below is where that claim is actually enforced.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace altair {

// The error codes the card reports to the guest. These ARE the wire values -- the
// guest's ERROR command hands one of these straight to an 8080 -- so the numbers
// are part of the card's published interface and must not be renumbered.
enum class HbError : uint8_t {
    None       = 0x00,
    NotFound   = 0x01,
    Permission = 0x02,
    Outside    = 0x03,  // the sandbox said no. The one that must never be a false negative.
    Io         = 0x04,
    NoFile     = 0x05,  // no file open -- the guest's mistake, not the host's
    TooLarge   = 0x06,
    BadName    = 0x07,
    Ambiguous  = 0x08,  // the case-folded name matches more than one host file
};

// What went wrong, and what to TELL THE HUMAN.
//
// The message is the host's own (`std::error_code::message()`, i.e. strerror), and
// the guest prints it verbatim. That is the whole reason the ERROR command yields
// text after the code: "R: FOO.ASM: No such file or directory" is an answer, and
// "R: error 01" is a lookup table the user does not have.
struct HbFail {
    HbError     code = HbError::None;
    std::string msg;
};

// A one-word fallback for each code, for when there is no host errno to quote.
const char* hbErrorText(HbError e);

// A GUEST NAME IS A RELATIVE PATH, AND BOTH SEPARATORS WORK EVERYWHERE.
//
// `SRC/FOO.ASM` and `SRC\FOO.ASM` mean the same thing on a Mac, on Linux and on
// Windows, and this is the only place that is true -- below here it is a native
// fs::path like any other. That is what lets ONE R.COM, assembled once, work against
// all three without being told which it is talking to, and it is why this card has
// no "what is your path separator?" command. (AltairZ80's pseudo-device has one --
// command 28 -- and both its utilities must ask, then rewrite their own help text
// with the answer. The guest should not have to care.)
//
// Splits the name into components, having refused every way OUT of the sandbox: an
// absolute path, a drive letter, a `..` component, a control character. A bare `.`
// component is dropped rather than refused -- `./FOO.ASM` is what a shell-trained
// human types, and it is not an attack.
//
// `..` is refused as a COMPONENT, not as a substring: `a..b` is an ordinary filename.
//
// NONE OF THIS IS THE REAL DEFENCE. It is the cheap, early one. The real defence is
// RealHostDir::resolve(), which canonicalizes what this produces and proves it still
// lives under the root -- because that is the only thing that can see a symlink.
bool splitHostName(const std::string& name, std::vector<std::string>& parts, HbFail& err);

// The same gates, when the components are not wanted.
bool validateHostName(const std::string& name, HbFail& err);

// Does `name` match `glob`? `*` and `?` only, and CASE-INSENSITIVELY.
//
// Case-insensitive because the guest is CP/M: it types `*.ASM` in capitals because
// its own filesystem has no lower case, and a case-sensitive match would make
// `R *.ASM` silently miss every file on a Linux host. The host names themselves are
// reported exactly as they are on disk (see list()) -- we fold the COMPARISON, never
// the name.
bool hostGlobMatch(const std::string& glob, const std::string& name);

// ---------------------------------------------------------------------------
// The service.
//
// Whole-file, all-or-nothing. There is no seek, no partial read, and no open file
// handle held across calls -- the card slurps at OPEN and spills at CLOSE, and that
// is what lets it promise the guest that an unclosed write is not committed.
// ---------------------------------------------------------------------------
class HostDir {
public:
    virtual ~HostDir() = default;

    // `maxBytes` = 0 means no limit. Over it -> TooLarge, and nothing is allocated.
    virtual bool read(const std::string& name, size_t maxBytes, std::vector<uint8_t>& out,
                      HbFail& err) = 0;
    virtual bool write(const std::string& name, const std::vector<uint8_t>& bytes,
                       HbFail& err) = 0;
    virtual bool remove(const std::string& name, HbFail& err) = 0;

    // Sorted. An empty glob means everything. The glob may carry a directory --
    // `SRC/*.ASM` lists SRC -- and the names handed back carry it too, so the guest can
    // feed one straight back to read(). A DIRECTORY comes back with a trailing '/', so
    // HDIR can show it and R's wildcard loop can skip it.
    virtual bool list(const std::string& glob, std::vector<std::string>& out, HbFail& err) = 0;

    // What SHOW prints. The root as the operator wrote it, not as canonicalized.
    virtual std::string root() const = 0;
};

// ---------------------------------------------------------------------------
// The real one: a directory on the host, and nothing above it.
//
// An EMPTY root means the shell's working directory, which is not a special case --
// it is the same rule Board::resolvePath() already follows with an empty configDir_
// ("a path TYPED is relative to the shell", core/paths.h).
// ---------------------------------------------------------------------------
class RealHostDir : public HostDir {
public:
    explicit RealHostDir(std::string root) : root_(std::move(root)) {}

    bool read(const std::string& name, size_t maxBytes, std::vector<uint8_t>& out,
              HbFail& err) override;
    bool write(const std::string& name, const std::vector<uint8_t>& bytes, HbFail& err) override;
    bool remove(const std::string& name, HbFail& err) override;
    bool list(const std::string& glob, std::vector<std::string>& out, HbFail& err) override;
    std::string root() const override { return root_; }

private:
    // Name -> an absolute path that is PROVABLY inside the root, or false.
    //
    // Two gates, and both are needed. validateHostName() rejects the name as TEXT,
    // which stops `../` and `/etc/passwd` before the disk is touched. Then the
    // resolved path is canonicalized and its parent compared against the
    // canonicalized root -- which is the ONLY thing that catches a SYMLINK sitting
    // inside the root and pointing out of it, because that name is perfectly legal
    // text and only the filesystem knows where it goes.
    //
    // `mustExist` also turns on the CASE-FOLDED FALLBACK. See findCaseInsensitive().
    bool resolve(const std::string& name, bool mustExist, std::string& out, HbFail& err) const;

    // THE GUEST CANNOT TYPE LOWER CASE, AND THAT IS NOT ITS FAULT.
    //
    // CP/M's CCP folds the whole command tail to upper case before the program ever
    // sees it, at 0x0080. So `R readme.txt` arrives as `README.TXT` and there is
    // nothing R.COM can do about it -- the lower case is gone before it runs. On a
    // case-sensitive host that means a guest could NEVER open a lower-case file by
    // name, which would make the card useless against most real directories.
    //
    // (AltairZ80 hit this too and answered it with an `L` switch on READ, which puts
    // the burden on the human to remember which of their files are which case. We
    // resolve it in the card instead: it is the card that knows what is actually on
    // the disk.)
    //
    // So: an EXACT match always wins. Failing that, we fold and look again. If exactly
    // one host file matches case-insensitively, that is the file. If SEVERAL do --
    // `Makefile` and `makefile` in one directory -- we refuse with Ambiguous rather
    // than silently picking one, because there is no defensible way to choose and the
    // wrong choice is a corrupted transfer that looks like a successful one.
    bool findCaseInsensitive(const std::string& canonRoot, const std::string& name,
                             std::string& out, HbFail& err) const;

    std::string root_;
};

// ---------------------------------------------------------------------------
// A directory made of nothing. The MemoryMedia of the host-file world.
//
// The board's protocol tests use this: they are about the state machine at BA+0
// and BA+1, and they should not be able to fail because of a temp directory or a
// read-only checkout. The SANDBOX tests are the exact opposite and use RealHostDir
// against a real directory -- a symlink escape cannot be tested against a fake
// filesystem, because a fake filesystem has no symlinks to escape through.
// ---------------------------------------------------------------------------
class MemHostDir : public HostDir {
public:
    bool read(const std::string& name, size_t maxBytes, std::vector<uint8_t>& out,
              HbFail& err) override;
    bool write(const std::string& name, const std::vector<uint8_t>& bytes, HbFail& err) override;
    bool remove(const std::string& name, HbFail& err) override;
    bool list(const std::string& glob, std::vector<std::string>& out, HbFail& err) override;
    std::string root() const override { return "(memory)"; }

    // What the test set up, and what the guest left behind.
    void put(const std::string& name, std::vector<uint8_t> bytes) {
        files_[name] = std::move(bytes);
    }
    bool has(const std::string& name) const { return files_.count(name) != 0; }
    const std::vector<uint8_t>& get(const std::string& name) const { return files_.at(name); }
    size_t count() const { return files_.size(); }

    // Make the next write fail, so the board's error path is reachable from a test.
    void failWrites(HbError e) { writeFails_ = e; }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
    HbError                                     writeFails_ = HbError::None;
};

} // namespace altair
