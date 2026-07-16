// The Host Bridge -- src/boards/hostbridge.h, docs/boards/hostbridge.md.
//
// THIS SUITE IS ABOUT THE STATE MACHINE AT BA+0 AND BA+1, and it runs against a
// MemHostDir so it cannot fail because of a temp directory or a read-only checkout
// (the same argument MemoryMedia makes for disks). The SANDBOX -- the part that is
// wrong quietly rather than loudly -- is tested in tests/test_hostdir.cpp, against a
// real filesystem, because a symlink escape has no meaning against a fake one.
//
// The invariant worth breaking this suite over is at the top of command():
//
//     ANY OUT TO THE COMMAND PORT ABANDONS THE STREAM IN FLIGHT.
//
// It is what lets the guest utilities be simple, and it is the thing the SIMH
// pseudo-device does not have -- which is why ITS utilities must send a reset command
// 128 times before they dare do anything. If that invariant goes red, the fix is in
// the card, not in the test.

#include "test.h"

#include "boards/hostbridge.h"
#include "core/machine.h"
#include "host/hostdir.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

using Cmd = HostBridgeBoard::Cmd;

struct Rig {
    Machine          m;
    HostBridgeBoard* hb  = nullptr;
    MemHostDir*      dir = nullptr;

    static constexpr uint8_t kBase = 0xB0;

    Rig() {
        std::string err;

        // PARANOID MODE, as every board suite runs. It re-derives the decode and the
        // interrupt wire the slow way on every cycle and aborts the moment the card
        // disagrees with the bus's cached copy.
        m.bus.setVerify(true);

        hb = dynamic_cast<HostBridgeBoard*>(m.add("hostbridge", "hb0", err));

        auto d = std::make_unique<MemHostDir>();
        dir    = d.get();
        hb->setDir(std::move(d));

        m.power();
    }

    // ---- the wire ----
    uint8_t status() { return in(kBase); }
    uint8_t data() { return in((uint8_t)(kBase + 1)); }
    void    cmd(Cmd c) { out(kBase, (uint8_t)c); }
    void    put(uint8_t v) { out((uint8_t)(kBase + 1), v); }

    uint8_t in(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return hb->read(c);
    }
    void out(uint8_t port, uint8_t v) {
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = port;
        c.data = v;
        hb->write(c);
    }

    // ---- what a guest utility actually does ----

    // Send a NUL-terminated name to the data port. The NUL is what commits it.
    void name(const std::string& s) {
        for (char ch : s) put((uint8_t)ch);
        put(0);
    }

    // Drain a NUL-terminated string from the data port.
    std::string text() {
        std::string s;
        for (int i = 0; i < 512; ++i) {
            uint8_t b = data();
            if (b == 0) break;
            s.push_back((char)b);
        }
        return s;
    }

    // Read a whole file the way R.COM does: while not EOF, take a byte if one is there.
    std::vector<uint8_t> slurp() {
        std::vector<uint8_t> v;
        for (int i = 0; i < 100000; ++i) {
            uint8_t s = status();
            if (s & HostBridgeBoard::DAV) v.push_back(data());
            else if (s & HostBridgeBoard::EOFF) break;
            else break;
        }
        return v;
    }

    // The error code the card is holding, without disturbing anything else.
    uint8_t errCode() {
        cmd(Cmd::Error);
        return data();
    }
    std::string errText() {
        cmd(Cmd::Error);
        data();  // the code
        return text();
    }

    bool err() { return (status() & HostBridgeBoard::ERR) != 0; }
};

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }
std::string          str(const std::vector<uint8_t>& v) { return {v.begin(), v.end()}; }

} // namespace

void test_hostbridge() {
    SECTION("hostbridge: it decodes two ports, and nothing else");
    {
        Rig r;
        auto decodes = [&](Cycle t, uint8_t port) {
            BusCycle c;
            c.type = t;
            c.addr = port;
            return r.hb->decodes(c);
        };
        CHECK(decodes(Cycle::IoRead, 0xB0), "reads BA+0");
        CHECK(decodes(Cycle::IoWrite, 0xB0), "writes BA+0");
        CHECK(decodes(Cycle::IoRead, 0xB1), "reads BA+1");
        CHECK(decodes(Cycle::IoWrite, 0xB1), "writes BA+1");
        CHECK(!decodes(Cycle::IoRead, 0xB2), "and not BA+2");
        CHECK(!decodes(Cycle::IoRead, 0xAF), "nor BA-1");
        CHECK(!decodes(Cycle::MemRead, 0xB0), "it has no memory decode at all");

        // 0x30 was the first choice for the default and it was WRONG: the WD179X floppy
        // controller lives there (AltairZ80/wd179x.c), and so does the Cromemco 64FDC
        // that wraps it. Pinning the default here means moving it back is a red test,
        // not a quiet regression.
        CHECK(!decodes(Cycle::IoRead, 0x30), "and it is NOT at 0x30 -- that is a floppy controller");
    }

    SECTION("hostbridge: bits 5-7 of the status register read ZERO");
    {
        // Load-bearing. An IN from a port no card decodes floats to 0xFF, so a guest
        // that reads 0xFF here has found an EMPTY SLOT, not a card with every flag lit.
        // If these bits ever start reading back, that distinction is gone.
        Rig r;
        CHECK((r.status() & 0xE0) == 0, "the reserved bits are clear at rest");
        r.cmd(Cmd::OpenRead);
        CHECK((r.status() & 0xE0) == 0, "...and while a command is in flight");
        r.name("NOPE.TXT");
        CHECK((r.status() & 0xE0) == 0, "...and with the error latch set");
    }

    SECTION("hostbridge: IDENT -- the probe, and why it exists");
    {
        Rig r;
        r.cmd(Cmd::Ident);
        CHECK(r.text() == HostBridgeBoard::kIdent, "IDENT hands back the signature");

        // Ask twice. A guest that probes, then probes again after an error, must get the
        // same answer both times -- there is no one-shot state to exhaust.
        r.cmd(Cmd::Ident);
        CHECK(r.text() == HostBridgeBoard::kIdent, "and again");
    }

    SECTION("hostbridge: OPEN_READ -- a whole file, byte for byte");
    {
        Rig r;
        r.dir->put("HELLO.TXT", bytes("hello, host"));

        r.cmd(Cmd::OpenRead);
        CHECK((r.status() & HostBridgeBoard::TBE) != 0, "it wants a name");
        r.name("HELLO.TXT");

        CHECK(!r.err(), "the open succeeded");
        CHECK((r.status() & HostBridgeBoard::DAV) != 0, "a byte is waiting");
        CHECK((r.status() & HostBridgeBoard::EOFF) == 0, "and we are not at EOF yet");

        CHECK(str(r.slurp()) == "hello, host", "the file comes across intact");
        CHECK((r.status() & HostBridgeBoard::EOFF) != 0, "EOF is up when it runs out");
        CHECK((r.status() & HostBridgeBoard::DAV) == 0, "and DAV is down");

        // Reading past the end gives NUL, not 0xFF. No board may ever manufacture 0xFF
        // -- that is the bus's word for "nobody answered" (DESIGN.md 4.6.1), and
        // tests/test_boundary.cpp enforces it across every card.
        CHECK(r.data() == 0x00, "reading past EOF gives 0x00, never 0xFF");
    }

    SECTION("hostbridge: BINARY IS BINARY -- NULs, high bits, and a ^Z all survive");
    {
        // A .COM file goes through here. A transport that stopped at a zero byte, or
        // masked bit 7, or took a 0x1A for end-of-file would corrupt every one of them
        // -- and would do it silently, which is the strip7out scar all over again.
        Rig r;
        std::vector<uint8_t> bin{0x00, 0xFF, 0x1A, 0x80, 0x00, 0x7F, 0xC3};
        r.dir->put("PROG.COM", bin);

        r.cmd(Cmd::OpenRead);
        r.name("PROG.COM");
        CHECK(r.slurp() == bin, "every byte survives, including NUL, 0x1A and bit 7");
    }

    SECTION("hostbridge: an EMPTY file is EOF at once, and is not an error");
    {
        Rig r;
        r.dir->put("EMPTY.TXT", {});

        r.cmd(Cmd::OpenRead);
        r.name("EMPTY.TXT");
        CHECK(!r.err(), "an empty file opens cleanly");
        CHECK((r.status() & HostBridgeBoard::EOFF) != 0, "and is immediately at EOF");
        CHECK(r.slurp().empty(), "with nothing in it");
    }

    SECTION("hostbridge: OPEN_WRITE + CLOSE commits; WITHOUT close, nothing happens");
    {
        Rig r;

        r.cmd(Cmd::OpenWrite);
        r.name("OUT.TXT");
        CHECK((r.status() & HostBridgeBoard::TBE) != 0, "it will take data");
        for (uint8_t b : bytes("written")) r.put(b);

        // NOT YET. This is the promise the buffer exists to keep: the file does not
        // exist on the host until CLOSE, so an abandoned transfer leaves nothing behind
        // -- not even an empty file.
        CHECK(!r.dir->has("OUT.TXT"), "the file does not exist before CLOSE");

        r.cmd(Cmd::Close);
        CHECK(!r.err(), "CLOSE succeeds");
        CHECK(r.dir->has("OUT.TXT"), "and NOW the file exists");
        CHECK(str(r.dir->get("OUT.TXT")) == "written", "with the right bytes");
    }

    SECTION("hostbridge: an ABANDONED write is discarded -- the rule, in one test");
    {
        Rig r;

        r.cmd(Cmd::OpenWrite);
        r.name("GONE.TXT");
        for (uint8_t b : bytes("half a file")) r.put(b);

        // The guest changes its mind. ANY command abandons the stream in flight -- so
        // the half-written file is not merely uncommitted, it is gone, and the card is
        // ready for the next thing with no reset dance in between. THIS is what SIMH's
        // pseudo-device cannot do, and the reason its utilities open with 128 resets.
        r.cmd(Cmd::Ident);
        CHECK(r.text() == HostBridgeBoard::kIdent, "the card answers the new command normally");
        CHECK(!r.dir->has("GONE.TXT"), "and the abandoned write left NOTHING on the host");

        // And a CLOSE now is not a stray commit of the old data -- there is no file open.
        r.cmd(Cmd::Close);
        CHECK(r.err(), "a CLOSE with nothing open is an error");
        CHECK(r.errCode() == (uint8_t)HbError::NoFile, "...NoFile (0x05)");
        CHECK(!r.dir->has("GONE.TXT"), "and it still committed nothing");
    }

    SECTION("hostbridge: BOTH resets discard an uncommitted write");
    {
        // The guest hit STOP and RESET halfway through a transfer. The half of a file
        // that made it across is not a file, and because the write buffer is the only
        // place those bytes ever lived, dropping it IS the whole job -- there is no
        // partial file on the host to clean up, because one was never created.
        for (Reset kind : {Reset::PowerOn, Reset::Bus}) {
            Rig r;
            r.cmd(Cmd::OpenWrite);
            r.name("PARTIAL.TXT");
            for (uint8_t b : bytes("half")) r.put(b);

            r.hb->reset(kind);

            CHECK(!r.dir->has("PARTIAL.TXT"), "a reset leaves no partial file on the host");
            CHECK(!r.err(), "and clears the error latch");
            CHECK((r.status() & HostBridgeBoard::TBE) == 0, "and nothing is open");
        }
    }

    SECTION("hostbridge: `hostdir` survives a reset -- it is a jumper, not state");
    {
        Rig r;
        std::string err;
        CHECK(setProperty(*r.hb, "hostdir", "/tmp/somewhere", err), "hostdir sets");

        r.hb->reset(Reset::PowerOn);

        // A reset does not move a jumper (DESIGN.md 6). Configuration -- the port, the
        // sandbox root, the write-protect -- is the cable and the straps, and a reset
        // button does not reach them.
        for (Property& p : r.hb->properties())
            if (p.name == "hostdir")
                CHECK(p.get().s() == "/tmp/somewhere", "hostdir survives a power-on reset");
    }

    // ---- WHERE THE FENCE ACTUALLY LANDS ------------------------------------------
    //
    // These two are the same rule from both ends, and the bug they pin was found by
    // trying to DOCUMENT it: `hostdir = "xfer"` in a machine file resolved beside that
    // file if the guest ran R before you hit ^E, and beside your SHELL if you hit ^E
    // first. Same file, same session, two different sandboxes -- because dir() is built
    // lazily and it read configDir(), which runStartup() had already cleared.
    //
    // A Rig is no good here: it injects a MemHostDir, and an injected sandbox outranks
    // the property by design. The bare card is the thing under test.

    SECTION("hostbridge: the sandbox is pinned to the file that WROTE hostdir, not to when it is asked");
    {
        namespace fs = std::filesystem;
        HostBridgeBoard hb;
        std::string     err;

        const fs::path cfg = fs::temp_directory_path() / "altairsim-cfgdir";

        // The loader stands at the machine file while it applies the key (toml.cpp)...
        hb.setConfigDir(cfg.generic_string());
        CHECK(setProperty(hb, "hostdir", "xfer", err), "a machine file sets hostdir");

        // ...and then the file stops talking. This is the exact teardown runStartup()
        // does, and every guest R/W in the session happens AFTER it.
        hb.setConfigDir("");

        const std::string got  = fs::path(hb.sandboxRoot()).lexically_normal().generic_string();
        const std::string want = (cfg / "xfer").lexically_normal().generic_string();
        CHECK(got == want, "the fence is the machine file's xfer, whenever it is asked");

        // ...and the written value is untouched: SHOW prints it and CONFIG SAVE writes
        // it back, so a machine file that saved out a resolved path would not reload.
        for (Property& p : hb.properties())
            if (p.name == "hostdir")
                CHECK(p.get().s() == "xfer", "...and hostdir still reads back AS WRITTEN");
    }

    SECTION("hostbridge: a TYPED hostdir is the SHELL's -- the pin must not outlive the file");
    {
        namespace fs = std::filesystem;
        HostBridgeBoard hb;
        std::string     err;

        hb.setConfigDir((fs::temp_directory_path() / "altairsim-cfgdir").generic_string());
        CHECK(setProperty(hb, "hostdir", "xfer", err), "the machine file sets one...");
        hb.setConfigDir("");

        // ...and now the operator types their own. configDir() is empty, which IS the
        // rule: what a human types is relative to the shell they typed it in.
        CHECK(setProperty(hb, "hostdir", "typed-xfer", err), "...the operator replaces it");

        const std::string got = fs::path(hb.sandboxRoot()).lexically_normal().generic_string();
        CHECK(got == fs::absolute("typed-xfer").lexically_normal().generic_string(),
              "a typed hostdir is the shell's cwd, as a typed path always is");

        // THE HALF THAT WOULD ROT SILENTLY. Pinning is only correct if the pin is
        // REPLACED with the value; a stale base kept from the old file would re-base
        // something a human typed, which is the original bug wearing the other hat.
        CHECK(got.find("altairsim-cfgdir") == std::string::npos,
              "...and the machine file's base did not follow it");
    }

    SECTION("hostbridge: DELETE");
    {
        Rig r;
        r.dir->put("DOOMED.TXT", bytes("x"));

        r.cmd(Cmd::Delete);
        r.name("DOOMED.TXT");
        CHECK(!r.err(), "delete succeeds");
        CHECK(!r.dir->has("DOOMED.TXT"), "and the file is gone");

        r.cmd(Cmd::Delete);
        r.name("DOOMED.TXT");
        CHECK(r.err(), "deleting it again fails");
        CHECK(r.errCode() == (uint8_t)HbError::NotFound, "...NotFound (0x01)");
    }

    SECTION("hostbridge: the directory enumerator");
    {
        Rig r;
        r.dir->put("A.ASM", bytes("a"));
        r.dir->put("B.ASM", bytes("b"));
        r.dir->put("C.COM", bytes("c"));

        // The glob is a NUL-terminated string like any other name -- and it may be
        // EMPTY, which means everything. The NUL is still required, so a guest never has
        // to remember which commands take a name and which do not.
        r.cmd(Cmd::DirFirst);
        r.name("");
        CHECK(r.text() == "A.ASM", "the first name");
        r.cmd(Cmd::DirNext);
        CHECK(r.text() == "B.ASM", "the second");
        r.cmd(Cmd::DirNext);
        CHECK(r.text() == "C.COM", "the third");
        CHECK((r.status() & HostBridgeBoard::EOFF) == 0, "not EOF while a name is in hand");
        r.cmd(Cmd::DirNext);
        CHECK((r.status() & HostBridgeBoard::EOFF) != 0, "EOF when the list runs out");

        // Filtered.
        r.cmd(Cmd::DirFirst);
        r.name("*.ASM");
        CHECK(r.text() == "A.ASM", "the glob filters: first match");
        r.cmd(Cmd::DirNext);
        CHECK(r.text() == "B.ASM", "second match");
        r.cmd(Cmd::DirNext);
        CHECK((r.status() & HostBridgeBoard::EOFF) != 0, "and C.COM is not in it");
    }

    SECTION("hostbridge: THE ENUMERATOR SURVIVES A TRANSFER -- this is what `R *.ASM` is");
    {
        // The enumerator is NOT a stream, and the "a command abandons the stream" rule
        // does not reach it. It has to work this way: walking a wildcard means
        // interleaving DIR_NEXT with a whole OPEN_READ/read/CLOSE for each match, and if
        // the OPEN_READ threw the listing away, the guest would have to buffer every
        // matching name up front -- in 8080, inside a CP/M TPA, with a 128-byte DMA
        // buffer to find room around. This test is the reason dirOpen_ exists.
        Rig r;
        r.dir->put("A.ASM", bytes("first"));
        r.dir->put("B.ASM", bytes("second"));
        r.dir->put("C.COM", bytes("not me"));

        std::vector<std::string> got;

        r.cmd(Cmd::DirFirst);
        r.name("*.ASM");

        for (int guard = 0; guard < 10; ++guard) {
            if (r.status() & HostBridgeBoard::EOFF) break;

            std::string hostName = r.text();

            // ...and now do a COMPLETE transfer, exactly as R.COM does, in the middle of
            // the enumeration.
            r.cmd(Cmd::OpenRead);
            r.name(hostName);
            CHECK(!r.err(), "the file opens mid-enumeration");
            std::string body = str(r.slurp());
            got.push_back(hostName + "=" + body);

            // The listing must still be there.
            r.cmd(Cmd::DirNext);
        }

        CHECK(got.size() == 2, "both matches were walked");
        CHECK(got[0] == "A.ASM=first", "the first file, read in full");
        CHECK(got[1] == "B.ASM=second", "the second file -- the enumerator survived the read");
    }

    SECTION("hostbridge: an EMPTY directory is EOF at once -- the same state as 'run out'");
    {
        // This is why HDIR.COM is ten lines of 8080 and not twenty: "no matches at all"
        // and "no matches left" are the SAME state to the guest, so there is one loop
        // and no special case.
        Rig r;
        r.cmd(Cmd::DirFirst);
        r.name("*.NONE");
        CHECK((r.status() & HostBridgeBoard::EOFF) != 0, "no matches means EOF immediately");
        CHECK((r.status() & HostBridgeBoard::DAV) == 0, "and there is nothing to read");
        CHECK(!r.err(), "and it is not an ERROR -- an empty directory is a fine answer");
    }

    SECTION("hostbridge: DIR_NEXT without DIR_FIRST is an error, not a crash");
    {
        Rig r;
        r.cmd(Cmd::DirNext);
        CHECK(r.err(), "DIR_NEXT with no enumeration going fails");
        CHECK(r.errCode() == (uint8_t)HbError::NoFile, "...NoFile (0x05)");
    }

    SECTION("hostbridge: ERROR yields the code, THEN the host's own words");
    {
        Rig r;
        r.cmd(Cmd::OpenRead);
        r.name("MISSING.TXT");

        CHECK(r.err(), "the ERR bit is up");
        CHECK(r.errCode() == (uint8_t)HbError::NotFound, "the code is NotFound (0x01)");

        // The text is the point. "R: MISSING.TXT: no such file" is an answer; "R: error
        // 01" is a lookup table the user does not have. A guest that only wants the byte
        // takes one and issues its next command -- safe, because a command abandons the
        // stream.
        std::string msg = r.errText();
        CHECK(!msg.empty(), "and a message follows it");
        CHECK(msg.find("MISSING.TXT") != std::string::npos, "which names the file");

        // ERROR does NOT clear the latch -- if it did, a guest could never read it. An
        // OPERATION clears it.
        CHECK(r.err(), "reading the error does not clear it");
        r.cmd(Cmd::Reset);
        CHECK(!r.err(), "RESET does");
    }

    SECTION("hostbridge: a failed OPEN leaves NOTHING half-open");
    {
        Rig r;
        r.cmd(Cmd::OpenRead);
        r.name("MISSING.TXT");

        CHECK((r.status() & HostBridgeBoard::DAV) == 0, "no data is waiting after a failed open");
        CHECK((r.status() & HostBridgeBoard::TBE) == 0, "and it is not still taking a name");

        // The failure must be recoverable in one command, with no reset dance.
        r.dir->put("THERE.TXT", bytes("ok"));
        r.cmd(Cmd::OpenRead);
        r.name("THERE.TXT");
        CHECK(!r.err(), "the very next open works");
        CHECK(str(r.slurp()) == "ok", "and reads correctly");
    }

    SECTION("hostbridge: `readonly` refuses OPEN_WRITE and DELETE");
    {
        Rig r;
        r.dir->put("KEEP.TXT", bytes("safe"));

        std::string err;
        CHECK(setProperty(*r.hb, "readonly", "on", err), "readonly sets");

        r.cmd(Cmd::OpenWrite);
        CHECK(r.err(), "OPEN_WRITE is refused at once -- before it even asks for a name");
        CHECK(r.errCode() == (uint8_t)HbError::Permission, "...Permission (0x02)");
        CHECK((r.status() & HostBridgeBoard::TBE) == 0, "and it is not taking a name");

        r.cmd(Cmd::Delete);
        r.name("KEEP.TXT");
        CHECK(r.err(), "DELETE is refused");
        CHECK(r.errCode() == (uint8_t)HbError::Permission, "...Permission (0x02)");
        CHECK(r.dir->has("KEEP.TXT"), "and the file is still there");

        // Reading still works. `readonly` is a write-protect tab, not an off switch.
        r.cmd(Cmd::OpenRead);
        r.name("KEEP.TXT");
        CHECK(!r.err(), "reads still work");
        CHECK(str(r.slurp()) == "safe", "...and give the right bytes");
    }

    SECTION("hostbridge: the sandbox is enforced THROUGH the card, not just under it");
    {
        // test_hostdir.cpp proves the sandbox itself. This proves the CARD does not
        // route around it: the escape must come back to the guest as 0x03, on the wire,
        // as an error code an 8080 can branch on.
        Rig r;
        r.cmd(Cmd::OpenRead);
        r.name("../../etc/passwd");
        CHECK(r.err(), "an escape through the card fails");
        CHECK(r.errCode() == (uint8_t)HbError::Outside, "...as Outside (0x03), on the wire");
    }

    SECTION("hostbridge: an unknown command says so");
    {
        Rig r;
        r.out(Rig::kBase, 0x7F);  // there is no command 0x7F
        CHECK(r.err(), "an unknown command is an error, not a silent no-op");
    }

    SECTION("hostbridge: a full round trip -- write it, read it back, compare");
    {
        // What the acceptance test does with real CP/M and a real disk, in three lines.
        // If this passes and that one does not, the bug is in the 8080, not the card.
        Rig r;
        std::vector<uint8_t> payload;
        for (int i = 0; i < 300; ++i) payload.push_back((uint8_t)(i & 0xFF));

        r.cmd(Cmd::OpenWrite);
        r.name("ROUND.BIN");
        for (uint8_t b : payload) r.put(b);
        r.cmd(Cmd::Close);
        CHECK(!r.err(), "written");

        r.cmd(Cmd::OpenRead);
        r.name("ROUND.BIN");
        CHECK(r.slurp() == payload, "and it comes back byte for byte, all 300 of them");
    }
}
