#include "test.h"

#include "boards/mits-frontpanel.h"
#include "boards/proctech-sol.h"
#include "boards/proctech-vdm1.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/roms.h"
#include "host/display_null.h"
#include "host/media.h"
#include "host/stream.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace altair;

namespace {

// The bytes the guest's recording actually put on the host file -- which is the only
// question that matters about a write, and the only one the guest cannot answer. No
// filesystem is touched: MemoryMedia stands in for the file (host/media.h).
MemoryMedia* g_media = nullptr;

void withTape(const std::string& contents, bool ro = false) {
    setMediaResolver([contents, ro](const std::string& path, bool wantRo, std::string&) {
        auto m = std::make_unique<MemoryMedia>(
            path, std::vector<uint8_t>(contents.begin(), contents.end()), ro || wantRo);
        g_media = m.get();
        return m;
    });
}

// The Sol-PC I/O card on a bus, with a crystal (an 8080 supplies Machine::clock) and,
// optionally, the VDM-1 it forwards its scroll writes to. Built by hand rather than
// from a .toml so a config bug cannot make these go red. The ports are driven straight
// over the bus; the CPU is here only for its clock (the serial UART's deadlines).
struct Rig {
    Machine     m;
    NullDisplay disp;
    SolBoard*   sol = nullptr;
    VdmBoard*   vdm = nullptr;

    explicit Rig(bool withVdm = false) {
        std::string err;
        m.bus.setVerify(true);
        if (withVdm) {
            vdm = dynamic_cast<VdmBoard*>(m.add("vdm1", "vdm0", err));
            VdmBoard::setDisplay(&disp);
        }
        sol = dynamic_cast<SolBoard*>(m.add("sol", "sol0", err));
        m.add("8080", "cpu0", err);
        m.power();
    }

    // CONNECT a unit to a fresh scripted terminal, through the real resolver path, and
    // hand back the pointer so a test can feed()/read out() of it.
    ScriptedStream* script(const std::string& unit) {
        std::string err;
        sol->connect(unit, "scripted", err);
        return dynamic_cast<ScriptedStream*>(sol->unitStream(unit));
    }

    uint8_t in(uint8_t p) { return m.bus.ioRead(p); }
    void    out(uint8_t p, uint8_t v) { m.bus.ioWrite(p, v); }
    void    lineTime() { m.clock.advance(5000); }  // one 9600-baud char, comfortably

    // ...and a CASSETTE character is a different order of magnitude: 11 bit times
    // (8N2) at 1200 baud is 9.2 ms, or ~18,300 T-states at 2 MHz. A test that waited
    // a serial character would be testing nothing but the receiver's cold start.
    void tapeTime() {
        m.clock.advance(20000);
        sol->pump();
    }
};

// The Sol-PC ports, by SOLOS's names (reference/Sol-20.md).
constexpr uint8_t SERST = 0xF8, SDATA = 0xF9, STAPT = 0xFA;
constexpr uint8_t KDATA = 0xFC, DSTAT = 0xFE;

// FA (general status) bit masks.
constexpr uint8_t KDR = 0x01;   // keyboard data ready -- ACTIVE LOW
constexpr uint8_t TTBE = 0x80;  // tape transmitter empty -- active high
constexpr uint8_t TDR = 0x40;   // tape data ready -- active high
// FA (OUT) -- the transport controls.
constexpr uint8_t MOTOR1 = 0x80, MOTOR2 = 0x40, SLOW = 0x20;
constexpr uint8_t TDATA = 0xFB;  // the CUTS data register
// F8 (serial status) bit masks.
constexpr uint8_t SDR = 0x40;   // serial data ready -- active high
constexpr uint8_t STBE = 0x80;  // serial transmitter empty -- active high

// A machine that actually boots SOLOS: RAM under the ROM, SOLOS at C000, the VDM at
// CC00, the fp and the sol card. Runs the 8080 until the '>' prompt appears in the
// VDM's screen RAM, or a generous step budget is spent.
struct BootRig {
    Machine     m;
    NullDisplay disp;
    SolBoard*   sol = nullptr;

    BootRig() {
        std::string err;

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0x0000;
        r.size = 0xCC00;  // 0000-CBFF: RAM under the VDM (which owns CC00-CFFF)
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        m.add("vdm1", "vdm0", err);
        VdmBoard::setDisplay(&disp);
        sol = dynamic_cast<SolBoard*>(m.add("sol", "sol0", err));
        auto* fp = dynamic_cast<FrontPanelBoard*>(m.add("fp", "fp0", err));
        setProperty(*fp, "sense", "0", err);
        m.add("8080", "cpu0", err);
        m.power();

        // Load SOLOS where the ROM would be and start the CPU at its cold-start entry.
        const BuiltinRom* rom = findRom("solos");
        Image img;
        std::string rerr;
        decodeRom(*rom, 0, img, rerr);
        auto flat = img.flat();
        for (size_t i = 0; i < flat.size(); ++i)
            m.bus.memWrite((uint16_t)(img.lo() + i), flat[i]);
        m.cpu()->setPc(0xC000);
    }

    // Is character `ch` anywhere in the 1 KB VDM screen (ignoring the D7 cursor bit)?
    bool screenHas(uint8_t ch) {
        for (uint16_t off = 0; off < 1024; ++off)
            if ((m.bus.memRead(0xCC00 + off) & 0x7F) == ch) return true;
        return false;
    }

    bool runToPrompt(uint64_t budget) {
        for (uint64_t i = 0; i < budget; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
            if ((i & 0x3FFF) == 0 && screenHas('>')) return true;
        }
        return screenHas('>');
    }

    // Bind the keyboard to a scripted stream = "typing" at SOLOS with no terminal.
    ScriptedStream* connectKeyboard() {
        std::string err;
        sol->connect("keyboard", "scripted", err);
        return dynamic_cast<ScriptedStream*>(sol->unitStream("keyboard"));
    }

    // Run the CPU, pumping the host side (so keystrokes cross into the guest) the way
    // the real run loop does -- once per slice, never inside a bus cycle.
    void steps(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
            if ((i & 0x1F) == 0) m.pump();
        }
    }
};

}  // namespace

void test_sol() {
    SECTION("Sol I/O -- decodes the seven Sol-PC ports F8..FE, and nothing else");
    {
        Rig g;
        for (uint16_t p = 0xF8; p <= 0xFE; ++p) {
            BusCycle c;
            c.type = Cycle::IoRead;
            c.addr = p;
            CHECK(g.sol->decodes(c), "decodes an F8..FE port");
        }
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0xF7;
        CHECK(!g.sol->decodes(c), "not F7 (below the block)");
        c.addr = 0xFF;
        CHECK(!g.sol->decodes(c), "not FF (the sense switches -- that is the fp board)");
        c.type = Cycle::MemRead;
        c.addr = 0xF8;
        CHECK(!g.sol->decodes(c), "and it is an I/O card -- it decodes no memory");
    }

    SECTION("Sol I/O -- the keyboard: a key waiting reads KDR active-low, FC consumes it");
    {
        Rig g;
        ScriptedStream* kbd = g.script("keyboard");

        CHECK((g.in(STAPT) & KDR) != 0, "idle: KDR is 1 (no key -- active low)");
        CHECK(!g.sol->keyboardReady(), "and the holding register is empty");

        kbd->feed("A");
        g.sol->pump();  // the host turn latches one character
        CHECK((g.in(STAPT) & KDR) == 0, "a key waiting: KDR falls to 0");
        CHECK(g.sol->keyboardReady(), "the holding register is full");

        CHECK(g.in(KDATA) == 'A', "IN 0FCH returns the keystroke");
        CHECK((g.in(STAPT) & KDR) != 0, "and reading it clears KDR back to 1");

        // Two keys, one per host turn (one strobe at a time, like the hardware).
        kbd->feed("BC");
        g.sol->pump();
        CHECK(g.in(KDATA) == 'B', "first of two");
        g.sol->pump();
        CHECK(g.in(KDATA) == 'C', "then the second");
    }

    SECTION("Sol I/O -- the serial port round-trips on F8/F9, active-high status");
    {
        Rig g;
        ScriptedStream* ser = g.script("serial");

        CHECK((g.in(SERST) & STBE) != 0, "idle: the transmitter is empty (STBE=1)");
        CHECK((g.in(SERST) & SDR) == 0, "and no receive data yet (SDR=0)");

        ser->feed("Z");
        g.lineTime();     // let the character finish arriving on the line
        g.sol->pump();    // the receiver runs on the host turn
        CHECK((g.in(SERST) & SDR) != 0, "a byte arrived: SDR=1");
        CHECK(g.in(SDATA) == 'Z', "IN 0F9H returns it");
        CHECK((g.in(SERST) & SDR) == 0, "and reading it clears SDR");

        g.out(SDATA, 'Q');  // OUT 0F9H -- the character goes out the line
        CHECK(ser->out() == "Q", "the transmitted byte reached the endpoint");
    }

    SECTION("Sol I/O -- FA idle bits: no parallel input, no tape running");
    {
        Rig g;
        uint8_t s = g.in(STAPT);
        CHECK((s & 0x02) != 0, "PDR=1: no parallel input source (active low)");
        CHECK((s & TTBE) != 0, "TTBE=1: the tape transmitter is empty");
        CHECK((s & TDR) == 0, "TDR=0: no deck is turning, so no tape byte is waiting");
    }

    SECTION("Sol I/O -- OUT 0FEH forwards the display scroll to the VDM-1");
    {
        Rig g(/*withVdm=*/true);
        CHECK(g.vdm->scroll() == 0, "the VDM starts unscrolled");
        g.out(DSTAT, 0x05);
        CHECK(g.vdm->scroll() == 5, "OUT 0FEH sets the VDM's scroll (low nibble)");
        g.out(DSTAT, 0x1A);
        CHECK(g.vdm->scroll() == 0x0A, "only the low four bits are the row");
    }

    SECTION("Sol I/O -- five purpose-named units: three you CONNECT, two you MOUNT");
    {
        Rig g;
        auto u = g.sol->units();
        CHECK(u.size() == 5, "serial, printer, keyboard, tape1, tape2");
        bool haveKbd = false, haveSer = false, havePrn = false;
        int  decks = 0;
        for (auto& d : u) {
            haveKbd |= d.name == "keyboard";
            haveSer |= d.name == "serial";
            havePrn |= d.name == "printer";
            if (d.kind == UnitKind::Tape) ++decks;
        }
        CHECK(haveKbd && haveSer && havePrn, "the three connectable lines are named");
        CHECK(decks == 2, "...and the two cassette decks are TAPE units, so they MOUNT");

        std::string err;
        CHECK(g.sol->connect("printer", "null", err), "CONNECT printer null");
        CHECK(!g.sol->connect("floppy", "null", err), "an unknown unit is refused");

        // The `connect` unit-property round-trips (this is what CONFIG SAVE writes).
        bool sawConnect = false;
        for (Property& p : g.sol->unitProperties("keyboard"))
            if (p.name == "connect") sawConnect = true;
        CHECK(sawConnect, "keyboard exposes a `connect` property");
        CHECK(g.sol->unitProperties("serial").size() >= 2, "serial also exposes baud/data_bits");
    }

    // ---- The cassette decks -------------------------------------------------

    SECTION("Sol cassette -- a mounted tape plays only while its motor is turning");
    {
        withTape("HI");
        Rig         g;
        std::string err;
        CHECK(g.sol->mount("tape1", "trk80.tap", false, err), "MOUNT sol0:tape1");

        // THE MOTOR IS THE POINT. A cassette in a stopped transport is silent, so the
        // card holds no line at all and TDR never rises -- however long you wait.
        g.tapeTime();
        CHECK((g.in(STAPT) & TDR) == 0, "motor off: nothing comes off the tape");

        g.out(STAPT, MOTOR1);  // OUT 0FAh -- GET TAPE MOVING
        g.tapeTime();
        CHECK((g.in(STAPT) & TDR) != 0, "motor on: TDR rises, a byte is waiting");
        CHECK(g.in(TDATA) == 'H', "IN 0FBH returns it");
        CHECK((g.in(STAPT) & TDR) == 0, "...and the read strobe clears TDR");

        g.tapeTime();
        CHECK(g.in(TDATA) == 'I', "the next byte follows it off the tape");
        CHECK(g.sol->tape(1)->pos() == 2, "the head has moved two bytes down the tape");
    }

    SECTION("Sol cassette -- two decks, and the motor bits pick which one is on the line");
    {
        withTape("21");  // both decks get the same media here; position tells them apart
        Rig         g;
        std::string err;
        CHECK(g.sol->mount("tape1", "one.tap", false, err), "MOUNT sol0:tape1");
        CHECK(g.sol->mount("tape2", "two.tap", false, err), "MOUNT sol0:tape2");

        g.out(STAPT, MOTOR2);  // deck 2 only
        g.tapeTime();
        (void)g.in(TDATA);
        CHECK(g.sol->tape(2)->pos() == 1, "deck 2 turned: ITS head moved");
        CHECK(g.sol->tape(1)->pos() == 0, "...and deck 1's did not");

        g.out(STAPT, MOTOR1);  // ...and now the other one
        g.tapeTime();
        (void)g.in(TDATA);
        CHECK(g.sol->tape(1)->pos() == 1, "deck 1 turned");
        CHECK(g.sol->tape(2)->pos() == 1, "...and deck 2 stayed where it stopped");
    }

    SECTION("Sol cassette -- RECORD writes through to the host file");
    {
        withTape("");
        Rig         g;
        std::string err;
        CHECK(g.sol->mount("tape1", "out.tap", false, err), "MOUNT sol0:tape1");
        CHECK(setUnitProperty(*g.sol, "tape1", "mode", "record", err), "press RECORD");

        g.out(STAPT, MOTOR1);
        g.out(TDATA, 'A');
        g.tapeTime();
        g.out(TDATA, 'B');
        g.tapeTime();

        // No UNMOUNT here, and that is not laziness: unmounting destroys the media
        // and g_media would dangle. The recording is already on the host file --
        // TapeStream::flush() syncs on every byte -- which is the thing being checked.
        const std::vector<uint8_t> want{'A', 'B'};
        CHECK(g_media && g_media->bytes() == want,
              "the two bytes reached the host file, in order");
    }

    SECTION("Sol cassette -- a deck is MOUNTed, never CONNECTed, and it REWINDs");
    {
        withTape("XY");
        Rig         g;
        std::string err;

        // `CONNECT sol0:tape file:...` used to open the file WRITE-ONLY AND TRUNCATING
        // -- the documented way to play a tape destroyed it. It is refused now, and
        // the refusal has to say what to type instead.
        CHECK(!g.sol->connect("tape1", "null", err), "CONNECT is refused on a deck");
        CHECK(err.find("MOUNT") != std::string::npos, "...and it points at MOUNT");

        CHECK(g.sol->mount("tape", "trk80.tap", false, err),
              "`tape` still names deck 1, so an old config still resolves");
        g.out(STAPT, MOTOR1);
        g.tapeTime();
        (void)g.in(TDATA);
        CHECK(g.sol->tape(1)->pos() == 1, "the head is one byte in");

        std::ostringstream out;
        CHECK(g.sol->runCommand("REWIND", {"REWIND", "sol0:tape1"}, out, err),
              "REW sol0:tape1");
        CHECK(g.sol->tape(1)->pos() == 0, "...and the head is back at the beginning");

        // With two decks there is no sensible default, so a bare REW must ask rather
        // than rewind the tape the operator was in the middle of writing.
        std::ostringstream out2;
        CHECK(!g.sol->runCommand("REWIND", {"REWIND", "sol0"}, out2, err),
              "a bare REWIND is refused");
        CHECK(err.find("tape1") != std::string::npos, "...naming a deck to type");
    }

    SECTION("Sol cassette -- OUT 0FAh D5 picks the speed, and the guest can feel it");
    {
        withTape("AB");
        Rig         g;
        std::string err;
        CHECK(g.sol->mount("tape1", "t.tap", false, err), "MOUNT sol0:tape1");

        // 1200 baud (D5 clear) is the default. A character is 11 bit times -- 8N2 --
        // so at 2 MHz it occupies ~18,300 T-states, and at 300 baud four times that.
        //
        // THE FIRST BYTE OFF A TAPE IS FREE, on this UART and on the 88-ACR's: the
        // receiver has no character in flight when the transport starts, so it takes
        // one at once and only then begins pacing. So the speed is measured on the
        // SECOND byte, which is the first one the line actually clocked.
        g.out(STAPT, MOTOR1);
        g.sol->pump();
        CHECK(g.in(TDATA) == 'A', "the first byte comes off as the motor starts");

        g.m.clock.advance(12000);  // less than one character at 1200 baud
        g.sol->pump();
        CHECK((g.in(STAPT) & TDR) == 0, "1200 baud: too early for the second byte");
        g.m.clock.advance(12000);
        g.sol->pump();
        CHECK((g.in(STAPT) & TDR) != 0, "...and by ~18,300 T-states it has arrived");
    }

    SECTION("Sol cassette -- ...and 0FAh D5 set is four times slower, not a no-op");
    {
        withTape("AB");
        Rig         g;
        std::string err;
        CHECK(g.sol->mount("tape1", "t.tap", false, err), "MOUNT sol0:tape1");

        g.out(STAPT, MOTOR1 | SLOW);  // 300 baud from the start, so the pacing is 300's
        g.sol->pump();
        CHECK(g.in(TDATA) == 'A', "the first byte is still free");

        g.m.clock.advance(24000);  // ample at 1200; a third of a character at 300
        g.sol->pump();
        CHECK((g.in(STAPT) & TDR) == 0, "300 baud: the wait that sufficed at 1200 does not");
        g.m.clock.advance(60000);
        g.sol->pump();
        CHECK((g.in(STAPT) & TDR) != 0, "...and by ~73,300 T-states the byte is there");
    }

    SECTION("Sol-20 -- SOLOS cold-starts, paints its '>' prompt, and rests on the keyboard");
    {
        BootRig b;
        CHECK(!b.screenHas('>'), "before running, the screen is blank");
        CHECK(b.runToPrompt(4'000'000), "SOLOS reaches its prompt and writes '>' to the VDM");

        // The keyboard is a NullStream here (never ready), so SOLOS must SETTLE at the
        // prompt polling for a key -- not loop re-erasing. Run on and confirm the '>'
        // is still there: a resting prompt, not a flicker caught mid-redraw.
        for (int i = 0; i < 500'000; ++i) {
            StepResult s = b.m.master()->step(b.m.bus);
            b.m.clock.advance(s.tStates);
        }
        CHECK(b.screenHas('>'), "the prompt is still there -- SOLOS is resting, not looping");
    }

    SECTION("Sol-20 -- SOLOS SAVEs a file to the cassette and GETs it back");
    {
        // THE ACCEPTANCE TEST FOR THE WHOLE PHASE, and it is a round trip because that
        // is the only check that cannot pass by accident: SOLOS's own tape driver
        // writes the leader, the header and the data through 0FBh, and SOLOS's own
        // reader has to find them again. Nothing here knows the file layout -- if the
        // motor bits, TDR/TTBE, the head position or REWIND were wrong, the bytes
        // would not come home.
        withTape("");
        BootRig     b;
        std::string err;
        CHECK(b.sol->mount("tape1", "round.tap", false, err), "MOUNT sol0:tape1");
        CHECK(b.runToPrompt(4'000'000), "SOLOS is at its prompt");

        // Something distinctive to save: sixteen bytes at 0100.
        for (uint16_t i = 0; i < 16; ++i)
            b.m.bus.memWrite((uint16_t)(0x0100 + i), (uint8_t)(0xA0 + i));

        ScriptedStream* kbd = b.connectKeyboard();
        CHECK(setUnitProperty(*b.sol, "tape1", "mode", "record", err), "press RECORD");
        kbd->feed("SA FOO 0100 010F\r");
        b.steps(20'000'000);  // the leader alone is a great many 1200-baud characters
        CHECK(b.sol->tape(1)->size() > 16,
              "SOLOS wrote a leader and a header, not just the sixteen data bytes");

        // Now play it back into a DIFFERENT place, so a test that never read the tape
        // cannot pass by finding the original still sitting where it was.
        CHECK(setUnitProperty(*b.sol, "tape1", "mode", "play", err), "press STOP, then PLAY");
        std::ostringstream out;
        CHECK(b.sol->runCommand("REWIND", {"REWIND", "sol0:tape1"}, out, err),
              "REW sol0:tape1 -- nothing in the guest can wind a Sol transport back");

        kbd->feed("GE FOO 0200\r");
        b.steps(20'000'000);

        bool same = true;
        for (uint16_t i = 0; i < 16; ++i)
            same &= b.m.bus.memRead((uint16_t)(0x0200 + i)) == (uint8_t)(0xA0 + i);
        CHECK(same, "GET found the file on the tape and loaded it back, byte for byte");
    }

    SECTION("Sol-20 -- an .ENT program loads through SOLOS's EN command, then EX runs it");
    {
        // This is the deramp `.ENT` load path (as used for e.g. trk80.ent) proven with
        // OUR OWN six-byte program instead of copyrighted game data: a `.ENT` file is a
        // script of SOLOS console input -- `ENTER addr` selects the load address, each
        // `AAAA:` line seats the address and lays hex bytes, and `/` ends the entry.
        // There is no tape involved. When EX'd, the program writes a sentinel glyph into
        // the VDM and RETs to SOLOS (whose stack is primed for exactly that return).
        //
        //   0100: MVI A,'Z'  ;  STA 0CE00H (VDM screen RAM)  ;  RET
        const uint8_t prog[] = {0x3E, 0x5A, 0x32, 0x00, 0xCE, 0xC9};

        BootRig b;
        CHECK(b.runToPrompt(4'000'000), "SOLOS is at its prompt, ready for a command");
        CHECK(!b.screenHas('Z'), "the sentinel glyph is not on the screen yet");

        ScriptedStream* kbd = b.connectKeyboard();

        // Type the .ENT text exactly as the file's bytes would arrive (CR line ends).
        kbd->feed("ENTER 0100\r0100: 3E 5A 32 00 CE C9/\r");
        b.steps(2'000'000);  // let SOLOS's EN parse the line and poke memory

        bool loaded = true;
        for (size_t i = 0; i < sizeof(prog); ++i)
            loaded &= (b.m.bus.memRead((uint16_t)(0x0100 + i)) == prog[i]);
        CHECK(loaded, "SOLOS EN laid the .ENT bytes down at 0100 (console load, not tape)");

        // Now run it -- EX begins execution at the address the .ENT loaded to.
        kbd->feed("EX 0100\r");
        b.steps(2'000'000);
        CHECK(b.screenHas('Z'), "EX 0100 executed the loaded program (it painted the VDM)");
    }
}
