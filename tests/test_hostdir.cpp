#include "test.h"

#include "host/hostdir.h"

#include <filesystem>
#include <fstream>

// THE SANDBOX TESTS, AND THESE ARE THE IMPORTANT ONES.
//
// Everything else about the Host Bridge is a state machine, and a state machine that
// is wrong is a transfer that fails loudly. THIS is the part that is wrong quietly:
// a guest program that can name a file outside `hostdir` can read your ssh key or
// overwrite your shell profile, and nothing about the transfer it performs looks any
// different from a working one.
//
// SO THIS TEST TOUCHES A REAL FILESYSTEM, deliberately, and it is the only test in
// the tree that does so on purpose rather than for want of a fake. A symlink escape
// CANNOT be tested against a MemHostDir, because a fake filesystem has no symlinks to
// escape through -- and the symlink is the escape that the text-level name checks
// cannot see, because `evil.txt` is a perfectly legal filename right up until the
// moment the filesystem tells you where it points.

namespace fs = std::filesystem;
using namespace altair;

namespace {

// A directory to be a sandbox, and a file OUTSIDE it to try to reach.
struct Sandbox {
    fs::path root;
    fs::path outside;      // the directory above -- what an escape lands in
    fs::path secret;       // the file an escape is after

    Sandbox() {
        std::error_code ec;
        outside = fs::temp_directory_path(ec) / fs::path("altairsim-hb-test");
        fs::remove_all(outside, ec);
        fs::create_directories(outside, ec);

        root = outside / "sandbox";
        fs::create_directories(root, ec);

        secret = outside / "SECRET.TXT";
        write(secret, "the host's private business");
    }

    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(outside, ec);
    }

    static void write(const fs::path& p, const std::string& s) {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(s.data(), (std::streamsize)s.size());
    }

    static std::string read(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    bool exists(const fs::path& p) const {
        std::error_code ec;
        return fs::exists(p, ec);
    }
};

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }
std::string          str(const std::vector<uint8_t>& v) { return {v.begin(), v.end()}; }

// Every escape must fail the SAME way: Outside (0x03), and nothing touched.
void refuses(RealHostDir& d, const Sandbox& sb, const std::string& name, const char* what) {
    HbFail err;
    std::vector<uint8_t> bytes;

    bool ok = d.read(name, 0, bytes, err);
    CHECK(!ok, what);
    CHECK(err.code == HbError::Outside, "an escape is refused as Outside (0x03), not as NotFound");

    // ...and a WRITE through the same name must not create anything out there either.
    // A sandbox that stops reads and not writes is not a sandbox.
    err = {};
    ok  = d.write(name, {'x'}, err);
    CHECK(!ok, "the same escape is refused on write");
    CHECK(err.code == HbError::Outside, "a write escape is Outside too");

    CHECK(sb.read(sb.secret) == "the host's private business", "the secret is untouched");
}

} // namespace

void test_hostdir() {
    SECTION("hostdir: the name gate -- text only, before the disk is touched");
    {
        HbFail err;
        CHECK(!validateHostName("", err), "an empty name is refused");
        CHECK(err.code == HbError::BadName, "...as BadName -- it is not an escape, it is nothing");

        // EVERY escape-shaped name is Outside, not BadName. The distinction matters: a
        // guest told BadName would reasonably try a different SPELLING of the same
        // escape, and we would rather it learned the truth the first time.
        for (const char* bad : {"../SECRET.TXT", "..", "/etc/passwd", "\\etc\\passwd",
                                "C:\\windows", "SRC/../../x", "SRC/.."}) {
            HbFail e;
            CHECK(!validateHostName(bad, e), std::string("refused: ").append(bad).c_str());
            CHECK(e.code == HbError::Outside, "an escape-shaped name is Outside");
        }

        HbFail ok;
        CHECK(validateHostName("FOO.ASM", ok), "an ordinary 8.3 name is fine");
        CHECK(validateHostName("read.me", ok), "so is a lower-case one");
        CHECK(validateHostName("a-file_1.txt", ok), "so are dashes and underscores");

        // A RELATIVE PATH IS A NAME, and BOTH separators work on EVERY host. That one
        // line is what lets a single R.COM, assembled once, work against a Mac, a Linux
        // box and a Windows box -- and it is why this card needs no "what is your path
        // separator?" command, which AltairZ80's pseudo-device does have.
        CHECK(validateHostName("SRC/FOO.ASM", ok), "a subdirectory is a legal name");
        CHECK(validateHostName("SRC\\FOO.ASM", ok), "...spelled with a backslash too");
        CHECK(validateHostName("./FOO.ASM", ok), "a bare '.' is dropped, not refused");

        // `..` is refused as a COMPONENT, never as a SUBSTRING. `A..B.TXT` and `..hidden`
        // are ordinary filenames; the cheap substring test would refuse both, and that is
        // the kind of over-strict check people quietly work around later.
        CHECK(validateHostName("A..B.TXT", ok), "'..' inside a name is not a '..' component");
        CHECK(validateHostName("..hidden", ok), "nor is a leading '..' in a longer name");

        std::vector<std::string> parts;
        CHECK(splitHostName("SRC\\SUB/X.TXT", parts, ok), "mixed separators split");
        CHECK(parts.size() == 3 && parts[0] == "SRC" && parts[1] == "SUB" && parts[2] == "X.TXT",
              "...into the components the host will actually be asked for");
    }

    SECTION("hostdir: the glob -- * and ?, case folded");
    {
        CHECK(hostGlobMatch("*.ASM", "FOO.ASM"), "*.ASM matches FOO.ASM");
        // CASE FOLDED, and this is the one that matters: CP/M has no lower case, so a
        // guest types *.ASM in capitals. A case-SENSITIVE match would make `R *.asm`
        // silently find nothing on a Linux host, which reads as "the card is broken".
        CHECK(hostGlobMatch("*.ASM", "foo.asm"), "*.ASM matches foo.asm -- the fold is the point");
        CHECK(hostGlobMatch("*", "anything"), "* matches everything");
        CHECK(hostGlobMatch("", "anything"), "an empty glob matches everything");
        CHECK(hostGlobMatch("R.?SM", "R.ASM"), "? matches one character");
        CHECK(!hostGlobMatch("*.ASM", "FOO.COM"), "*.ASM does not match FOO.COM");
        CHECK(!hostGlobMatch("R.ASM", "RW.ASM"), "a literal does not match a longer name");
        CHECK(hostGlobMatch("*O*.A*", "FOO.ASM"), "several stars backtrack correctly");
    }

    SECTION("hostdir: an ordinary round trip");
    {
        Sandbox sb;
        RealHostDir d(sb.root.string());
        HbFail err;

        std::vector<uint8_t> bytes{'h', 'e', 'l', 'l', 'o'};
        CHECK(d.write("HELLO.TXT", bytes, err), "write lands");
        CHECK(sb.exists(sb.root / "HELLO.TXT"), "...as a real file, in the root");

        std::vector<uint8_t> back;
        CHECK(d.read("HELLO.TXT", 0, back, err), "read comes back");
        CHECK(back == bytes, "byte for byte");

        // Binary, and NUL-clean. The bridge carries a .COM file, so a transport that
        // stopped at a zero byte would corrupt every one of them.
        std::vector<uint8_t> bin{0x00, 0xFF, 0x1A, 0x00, 0x80};
        CHECK(d.write("BIN.COM", bin, err), "a binary file with NULs and a ^Z writes");
        CHECK(d.read("BIN.COM", 0, back, err) && back == bin, "...and reads back identical");

        std::vector<std::string> names;
        CHECK(d.list("", names, err), "list works");
        CHECK(names.size() == 2, "two files");
        CHECK(names[0] == "BIN.COM" && names[1] == "HELLO.TXT", "sorted");

        CHECK(d.list("*.TXT", names, err) && names.size() == 1 && names[0] == "HELLO.TXT",
              "the glob filters");

        CHECK(d.remove("HELLO.TXT", err), "delete works");
        CHECK(!sb.exists(sb.root / "HELLO.TXT"), "...and the file is gone");
        CHECK(!d.remove("HELLO.TXT", err) && err.code == HbError::NotFound,
              "deleting it twice is NotFound");

        CHECK(!d.read("NOPE.TXT", 0, back, err), "a missing file fails");
        CHECK(err.code == HbError::NotFound, "...as NotFound");
        CHECK(!err.msg.empty(), "and it says something a human can read");
    }

    SECTION("hostdir: THE CASE FOLD -- because CP/M's CCP destroys the case of the name");
    {
        // The guest cannot type lower case, and that is not its fault: CP/M's CCP folds
        // the whole command tail to upper case at 0x0080 before the program ever runs.
        // So `R readme.txt` arrives at R.COM as `README.TXT`, and on a case-sensitive
        // host an exact-match-only card could never open a lower-case file at all.
        //
        // (AltairZ80 answered this with an `L` switch on READ, putting the burden on the
        // human to remember which of their files are which case. We answer it in the
        // card, which is the thing that knows what is actually on the disk.)
        Sandbox sb;
        RealHostDir d(sb.root.string());
        HbFail err;
        std::vector<uint8_t> back;

        Sandbox::write(sb.root / "readme.txt", "lower case on the host");

        // THIS MUST HOLD ON EVERY HOST, and it does so for two different reasons -- which
        // is the whole reason it is worth asserting rather than assuming. On Linux the
        // exact name misses and OUR fold finds the file. On a Mac (APFS, case-insensitive
        // by default) the exact name HITS, because the filesystem folded it for us and our
        // fallback never runs. Same answer, different machinery, and the guest cannot tell.
        CHECK(d.read("README.TXT", 0, back, err),
              "an UPPER-CASE guest name finds a lower-case host file");
        CHECK(str(back) == "lower case on the host", "...and gets the right bytes");

        // A WRITE of a name nobody has creates it EXACTLY as asked -- the fold is a way to
        // FIND an existing file, never a way to rename a new one.
        err = {};
        CHECK(d.write("NEW.TXT", bytes("fresh"), err), "a new file writes");
        CHECK(sb.exists(sb.root / "NEW.TXT"), "...under the name the guest gave, exactly");

        // ...and a write to an existing lower-case file OVERWRITES it in place, rather
        // than leaving a second file beside it differing only in case.
        err = {};
        CHECK(d.write("README.TXT", bytes("rewritten"), err),
              "a write folds onto the existing file");
        CHECK(sb.read(sb.root / "readme.txt") == "rewritten", "...and overwrites it in place");

        // ---- The rest of this section needs TWO files differing only in case, and a
        // case-INSENSITIVE host cannot hold such a pair. Ask, rather than assume: on
        // macOS these assertions are not merely untestable, they are MEANINGLESS, and a
        // test that quietly passed by testing nothing would be worse than one that says
        // so out loud.
        Sandbox::write(sb.root / "Two.dat", "a");
        Sandbox::write(sb.root / "two.dat", "b");
        const bool caseSensitiveHost = sb.read(sb.root / "Two.dat") == "a";

        if (!caseSensitiveHost) {
            std::printf("  SKIP  the host filesystem is case-INSENSITIVE; it folds names "
                        "itself, so the card's fallback cannot arise here\n");
        } else {
            // An EXACT match always wins, so a host that has BOTH spellings is never
            // surprised by the fold.
            Sandbox::write(sb.root / "BOTH.TXT", "exact");
            Sandbox::write(sb.root / "both.txt", "folded");
            err = {};
            CHECK(d.read("BOTH.TXT", 0, back, err), "an exact match still resolves");
            CHECK(str(back) == "exact", "...and it WINS over the folded one");

            // ...but a fold that lands on two files is REFUSED. There is no defensible
            // way to choose, and choosing wrong is a transfer that succeeds and moves the
            // wrong file -- the worst outcome available.
            err = {};
            CHECK(!d.read("TWO.DAT", 0, back, err),
                  "a name matching two host files by fold is refused");
            CHECK(err.code == HbError::Ambiguous, "...as Ambiguous (0x08), never by guessing");
        }
    }

    SECTION("hostdir: subdirectories -- and BOTH separators, on every host");
    {
        // One R.COM, assembled once, works against a Mac, a Linux box and a Windows box.
        // That is what accepting both separators buys, and it is why this card needs no
        // "what is your path separator?" command (AltairZ80's has one, and both its
        // utilities have to ask before they can print their own usage text).
        Sandbox sb;
        std::error_code ec;
        fs::create_directories(sb.root / "SRC", ec);
        Sandbox::write(sb.root / "SRC" / "FOO.ASM", "in a subdirectory");

        RealHostDir d(sb.root.string());
        HbFail err;
        std::vector<uint8_t> back;

        CHECK(d.read("SRC/FOO.ASM", 0, back, err), "a forward slash reaches a subdirectory");
        CHECK(str(back) == "in a subdirectory", "...and gets the bytes");

        err = {};
        back.clear();
        CHECK(d.read("SRC\\FOO.ASM", 0, back, err), "and a BACKSLASH means the same thing here");
        CHECK(str(back) == "in a subdirectory", "...on Linux and macOS too, not just Windows");

        // `./FOO` is what a shell-trained human types. It is noise, not an attack.
        Sandbox::write(sb.root / "TOP.TXT", "top");
        err = {};
        CHECK(d.read("./TOP.TXT", 0, back, err), "a bare '.' component is dropped, not refused");

        // `a..b` is an ordinary filename. Only a `..` COMPONENT is an escape -- rejecting
        // the substring would be cheaper and would be wrong.
        Sandbox::write(sb.root / "A..B.TXT", "dots");
        err = {};
        CHECK(d.read("A..B.TXT", 0, back, err), "'..' inside a name is not a '..' component");

        // A glob may carry a directory, and the names come back with it attached -- so
        // the guest can feed one straight back into OPEN_READ without reassembling a path.
        std::vector<std::string> names;
        err = {};
        CHECK(d.list("SRC/*.ASM", names, err), "a glob may name a directory");
        CHECK(names.size() == 1 && names[0] == "SRC/FOO.ASM",
              "...and the name comes back ready to hand straight back to OPEN_READ");

        // A directory is marked, so HDIR can show it and R's wildcard loop can skip it.
        err = {};
        CHECK(d.list("", names, err), "the root lists");
        bool sawDir = false;
        for (const std::string& n : names)
            if (n == "SRC/") sawDir = true;
        CHECK(sawDir, "a subdirectory is listed with a trailing '/', so it can be told apart");
    }

    SECTION("hostdir: subdirectories did NOT weaken the sandbox");
    {
        // The text gate got one line more permissive. The canonicalize-and-compare gate
        // did not move at all, and it is the one doing the work -- so a symlinked
        // DIRECTORY pointing out is exactly as refused as a symlinked file.
        Sandbox sb;
        std::error_code ec;
        RealHostDir d(sb.root.string());

        refuses(d, sb, "SRC/../../SECRET.TXT", "a '..' component is still refused, mid-path");
        refuses(d, sb, "/etc/passwd", "an absolute path is still refused");
        refuses(d, sb, "\\etc\\passwd", "...spelled the Windows way too");

        fs::create_symlink(sb.outside, sb.root / "OUT", ec);
        if (!ec) {
            // `OUT/SECRET.TXT` is legal TEXT -- no `..`, no absolute path, nothing a
            // string check can see. Only the filesystem knows OUT leaves the sandbox.
            refuses(d, sb, "OUT/SECRET.TXT", "a symlinked DIRECTORY is not a way out either");

            std::vector<std::string> names;
            HbFail err;
            CHECK(d.list("", names, err), "list survives a bad directory symlink");
            for (const std::string& n : names)
                CHECK(n != "OUT/", "...and does not advertise it");
        }
    }

    SECTION("hostdir: the size cap is asked BEFORE anything is allocated");
    {
        Sandbox sb;
        RealHostDir d(sb.root.string());
        HbFail err;

        CHECK(d.write("BIG.BIN", std::vector<uint8_t>(1000, 'x'), err), "a 1000-byte file");

        std::vector<uint8_t> back;
        CHECK(!d.read("BIG.BIN", 100, back, err), "reading it with a 100-byte cap fails");
        CHECK(err.code == HbError::TooLarge, "...as TooLarge");
        CHECK(back.empty(), "and nothing was allocated");
        CHECK(d.read("BIG.BIN", 1000, back, err), "exactly at the cap is fine");
        CHECK(d.read("BIG.BIN", 0, back, err), "and 0 means no cap");
    }

    SECTION("hostdir: SANDBOX ESCAPES -- the tests this file exists for");
    {
        Sandbox sb;
        RealHostDir d(sb.root.string());

        // 1. The classic.
        refuses(d, sb, "../SECRET.TXT", "../SECRET.TXT is refused");

        // 2. Deeper, in case one level of `..` was special-cased.
        refuses(d, sb, "../../etc/passwd", "../../etc/passwd is refused");

        // 3. An absolute path -- a guest that gives up on relative escapes.
        refuses(d, sb, "/etc/passwd", "an absolute path is refused");

        // 4. A Windows drive letter, refused on every host. It is inert on Unix, but a
        //    guest that sends one is trying for something, and there is no reason to
        //    let it through on the platform where it happens not to work.
        refuses(d, sb, "C:\\windows\\system32", "a drive letter is refused");

        // 5. And the Windows spelling of the classic, on a Unix host. A backslash is
        //    inert here, but a guest that sends one is still trying to leave.
        refuses(d, sb, "..\\SECRET.TXT", "..\\SECRET.TXT is refused");
    }

    SECTION("hostdir: THE SYMLINK ESCAPE -- the one the name check cannot see");
    {
        Sandbox sb;
        std::error_code ec;

        // A symlink INSIDE the sandbox, pointing at the secret OUTSIDE it. Its name --
        // `INNOCENT.TXT` -- passes every text check there is. Only the filesystem knows
        // where it goes, so only the filesystem can be asked, and canonicalizing the
        // resolved path is the asking.
        fs::create_symlink(sb.secret, sb.root / "INNOCENT.TXT", ec);
        if (ec) {
            // A host that will not let us make one (Windows without the privilege).
            // Skipping is honest; pretending to have tested it is not.
            std::printf("  SKIP  symlinks not permitted on this host (%s)\n",
                        ec.message().c_str());
        } else {
            RealHostDir d(sb.root.string());
            HbFail err;
            std::vector<uint8_t> bytes;

            CHECK(!d.read("INNOCENT.TXT", 0, bytes, err),
                  "a symlink pointing out of the sandbox is refused");
            CHECK(err.code == HbError::Outside, "...as Outside (0x03), not followed");
            CHECK(bytes.empty(), "and nothing came back");

            // And it must not be WRITABLE through either -- following it on write would
            // overwrite the target, which is worse than reading it.
            err = {};
            CHECK(!d.write("INNOCENT.TXT", {'x'}, err), "...and cannot be written through");
            CHECK(err.code == HbError::Outside, "...also Outside");
            CHECK(sb.read(sb.secret) == "the host's private business", "the target is untouched");

            // HDIR must not ADVERTISE it either. A name that list() shows but read()
            // refuses is a worse experience than a name that is simply not there.
            std::vector<std::string> names;
            CHECK(d.list("", names, err), "list still works with a bad symlink present");
            CHECK(names.empty(), "and it does not list a symlink that leaves the sandbox");
        }
    }

    SECTION("hostdir: a symlink that stays INSIDE is a perfectly ordinary file");
    {
        Sandbox sb;
        std::error_code ec;

        Sandbox::write(sb.root / "REAL.TXT", "inside, and fine");
        fs::create_symlink(sb.root / "REAL.TXT", sb.root / "LINK.TXT", ec);

        if (!ec) {
            RealHostDir d(sb.root.string());
            HbFail err;
            std::vector<uint8_t> bytes;

            // The check is "does it resolve inside the root", NOT "is it a symlink".
            // Refusing all symlinks would be easy and would be wrong: a symlink within
            // the sandbox never leaves it, and a user who arranged one meant it.
            CHECK(d.read("LINK.TXT", 0, bytes, err),
                  "a symlink that stays inside the sandbox is readable");
            CHECK(std::string(bytes.begin(), bytes.end()) == "inside, and fine",
                  "...and gives the right bytes");
        }
    }

    SECTION("hostdir: an empty root is the working directory, and a bad root says so");
    {
        // Empty = the shell's CWD. Not a special case -- it is the same rule
        // Board::resolvePath() follows with an empty configDir_ (core/paths.h).
        RealHostDir cwd("");
        HbFail err;
        std::vector<std::string> names;
        CHECK(cwd.list("", names, err), "an empty root lists the working directory");

        // A `hostdir` pointing at a typo must NOT report every file in it as merely
        // missing -- that sends the user hunting for the file instead of the config.
        RealHostDir bad("/no/such/directory/anywhere");
        std::vector<uint8_t> bytes;
        CHECK(!bad.read("X.TXT", 0, bytes, err), "a nonexistent root fails");
        CHECK(err.code == HbError::Outside, "...as Outside");
        CHECK(err.msg.find("does not exist") != std::string::npos,
              "...and the message blames the DIRECTORY, not the file");
    }
}
