#include "test.h"

#include "boards/s100-memory.h"
#include "boards/sd-sbc.h"
#include "boards/sd-vdb8024.h"
#include "boards/sd-versafloppy.h"
#include "core/machine.h"
#include "host/display_null.h"
#include "host/media.h"
#include "host/stream.h"

#include <string>

using namespace altair;

namespace {

// A bare VDB-8024 on a NullDisplay, its keyboard bound to a scripted terminal through
// the real resolver path -- the SAME injection main() does, one backend down. No memory
// board, so nothing else decodes ports 00/01; setVerify(true) would flag a clash if it
// did. The board's screen is read back as text (screenText), which is how a headless CI
// build proves a video terminal whose output is characters, not a serial line.
struct Rig {
    Machine        m;
    NullDisplay    disp;
    Vdb8024Board*  vdb = nullptr;
    ScriptedStream* kbd = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        vdb = dynamic_cast<Vdb8024Board*>(m.add("vdb8024", "vdb0", err));
        Vdb8024Board::setDisplay(&disp);
        vdb->connect("keyboard", "scripted", err);
        kbd = dynamic_cast<ScriptedStream*>(vdb->unitStream("keyboard"));
        m.power();
    }

    // Send a display byte the way OUT 01H does, but straight to the terminal engine.
    void put(const std::string& s) { for (char c : s) vdb->putByte((uint8_t)c); }
    void put(uint8_t b) { vdb->putByte(b); }
    uint8_t status() { return m.bus.ioRead(0x00); }
};

} // namespace

void test_vdb8024() {
    SECTION("VDB-8024 -- two I/O ports, and not a third");
    {
        Rig g;
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x00;
        CHECK(g.vdb->decodes(c), "decodes the status port (00)");
        c.addr = 0x01;
        CHECK(g.vdb->decodes(c), "decodes the keyboard/display port (01)");
        c.addr = 0x02;
        CHECK(!g.vdb->decodes(c), "does NOT decode 02 -- the card is exactly two ports");
        c.type = Cycle::MemRead;
        c.addr = 0x0000;
        CHECK(!g.vdb->decodes(c), "and it is NOT memory-mapped -- it decodes no memory");
    }

    SECTION("VDB-8024 -- status polarity: D2 display-ready always, D1 keyboard active-high");
    {
        Rig g;
        CHECK((g.status() & 0x04) != 0, "D2 (display ready) is always set");
        CHECK((g.status() & 0x02) == 0, "D1 (keyboard) clear -- nobody typed");

        g.kbd->feed("A");
        g.vdb->pump();  // the keyboard is latched in pump(), never in a bus cycle
        CHECK((g.status() & 0x02) != 0, "a key waiting sets D1");
        CHECK(g.m.bus.ioRead(0x01) == 'A', "IN 01 yields the keyboard byte");
        CHECK((g.status() & 0x02) == 0, "reading 01 clears D1");
        CHECK(g.vdb->rxBytes() == 1, "and the keystroke counts as live traffic (idle policy)");
    }

    SECTION("VDB-8024 -- the keyboard-interrupt strap (E17 -> VI): polled by default, VI when set");
    {
        // Reference 6: the keyboard strobe can raise an S-100 vectored-interrupt line so the
        // SBC-200's CTC can vector it (mode-2), which is how the SD video CBIOS runs its
        // keyboard. The strap is `interrupt`; the vector is not this board's business -- it
        // only pulls the line while a byte is waiting, and drops it when the byte is read.
        Rig         g;
        std::string err;

        CHECK(g.vdb->assertsVi() == 0, "unstrapped, the board pulls no VI line");
        g.kbd->feed("A");
        g.vdb->pump();
        CHECK((g.status() & 0x02) != 0, "polled: a key still shows in status D1...");
        CHECK(g.vdb->assertsVi() == 0, "...but with interrupt = none it pulls no VI line");
        CHECK(g.m.bus.ioRead(0x01) == 'A', "drain it");

        CHECK(setProperty(*g.vdb, "interrupt", "vi2", err), "strap the keyboard to VI2");
        CHECK(g.vdb->assertsVi() == 0, "no key waiting yet -> the line is not pulled");
        g.kbd->feed("B");
        g.vdb->pump();
        CHECK(g.vdb->assertsVi() == 0x04, "a key waiting pulls VI2 (bit 2)");
        CHECK((g.m.bus.viLines() & 0x04) != 0, "and the bus sees VI2 on the backplane");
        CHECK(g.m.bus.ioRead(0x01) == 'B', "the keyboard read (the ISR's IN 01H)...");
        CHECK(g.vdb->assertsVi() == 0, "...drops the strobe, so VI2 falls -- the implicit EOI");
        CHECK((g.m.bus.viLines() & 0x04) == 0, "the backplane line is released");
    }

    SECTION("VDB-8024 -- printable text lands at the cursor and advances it");
    {
        Rig g;
        g.put("HI");
        CHECK(g.vdb->charAt(0, 0) == 'H' && g.vdb->charAt(0, 1) == 'I', "'HI' at row 0");
        CHECK(g.vdb->cursorRow() == 0 && g.vdb->cursorCol() == 2, "cursor advanced to col 2");
    }

    SECTION("VDB-8024 -- CR, LF and NEW LINE (the v1.6 firmware map)");
    {
        Rig g;
        g.put("AB");
        g.put((uint8_t)0x0D);  // CR -> col 0, same row
        CHECK(g.vdb->cursorRow() == 0 && g.vdb->cursorCol() == 0, "CR homes the column");
        g.put((uint8_t)0x0A);  // LF -> next row, same col
        CHECK(g.vdb->cursorRow() == 1 && g.vdb->cursorCol() == 0, "LF drops a line");
        g.put("X");
        g.put((uint8_t)0x1F);  // NEW LINE = LF + CR
        CHECK(g.vdb->cursorRow() == 2 && g.vdb->cursorCol() == 0, "new-line drops AND returns");
        CHECK(g.vdb->charAt(0, 0) == 'A', "the first line is untouched");
    }

    SECTION("VDB-8024 -- a line wraps at column 80");
    {
        Rig g;
        for (int i = 0; i < 80; ++i) g.put('*');
        CHECK(g.vdb->cursorRow() == 1 && g.vdb->cursorCol() == 0,
              "the 80th column wraps to the next line");
        CHECK(g.vdb->charAt(0, 79) == '*', "the 80th char is on the first line");
    }

    SECTION("VDB-8024 -- backspace, tab, home, clear");
    {
        Rig g;
        g.put("ABCDE");
        g.put((uint8_t)0x08);  // backspace
        CHECK(g.vdb->cursorCol() == 4, "backspace steps the cursor left");
        g.put((uint8_t)0x09);  // tab from col 4 -> 8
        CHECK(g.vdb->cursorCol() == 8, "tab moves to the next multiple of 8");
        g.put((uint8_t)0x1E);  // home
        CHECK(g.vdb->cursorRow() == 0 && g.vdb->cursorCol() == 0, "home -> (0,0)");
        g.put("Z");
        g.put((uint8_t)0x1A);  // clear screen
        CHECK(g.vdb->charAt(0, 0) == ' ' && g.vdb->cursorRow() == 0 && g.vdb->cursorCol() == 0,
              "clear blanks the page and homes the cursor");
    }

    SECTION("VDB-8024 -- ESC = row col positions the cursor (Y then X, each + 0x20)");
    {
        Rig g;
        g.put((uint8_t)0x1B);        // ESC
        g.put('=');
        g.put((uint8_t)(0x20 + 5));  // row 5
        g.put((uint8_t)(0x20 + 10)); // col 10
        CHECK(g.vdb->cursorRow() == 5 && g.vdb->cursorCol() == 10, "ESC = 5 10 lands at (5,10)");
        g.put("Q");
        CHECK(g.vdb->charAt(5, 10) == 'Q', "and a char written there lands at (5,10)");
    }

    SECTION("VDB-8024 -- ESC T erases to end of line, ESC Y to end of screen");
    {
        Rig g;
        g.put("HELLO");
        g.put((uint8_t)0x0D);  // back to col 0
        g.put((uint8_t)0x0C);  // cursor right to col 1 (forward space)
        g.put((uint8_t)0x1B); g.put('T');  // erase to EOL from col 1
        CHECK(g.vdb->charAt(0, 0) == 'H', "the char before the cursor survives");
        CHECK(g.vdb->charAt(0, 1) == ' ' && g.vdb->charAt(0, 4) == ' ', "the rest of the line is blank");

        g.put((uint8_t)0x1B); g.put('=');            // reposition to (2,0)
        g.put((uint8_t)(0x20 + 2)); g.put((uint8_t)0x20);
        g.put("KEEP");
        g.put((uint8_t)0x1B); g.put('=');            // to (1,0)
        g.put((uint8_t)(0x20 + 1)); g.put((uint8_t)0x20);
        g.put((uint8_t)0x1B); g.put('Y');            // erase to end of screen
        CHECK(g.vdb->charAt(0, 0) == 'H', "text above the cursor survives ESC Y");
        CHECK(g.vdb->charAt(2, 0) == ' ', "text below the cursor is erased by ESC Y");
    }

    SECTION("VDB-8024 -- LF on the last line scrolls the page up");
    {
        Rig g;
        g.put((uint8_t)0x1B); g.put('=');                       // to (0,0)
        g.put((uint8_t)0x20); g.put((uint8_t)0x20);
        g.put("TOP");
        g.put((uint8_t)0x1B); g.put('=');                       // to the last row (23,0)
        g.put((uint8_t)(0x20 + 23)); g.put((uint8_t)0x20);
        g.put((uint8_t)0x0A);  // LF at the bottom -> scroll
        CHECK(g.vdb->cursorRow() == 23, "the cursor stays on the last line");
        CHECK(g.vdb->charAt(0, 0) != 'T' || g.vdb->charAt(0, 2) != 'P',
              "the top line scrolled away");
    }

    // ---- ACCEPTANCE: the SD monitor boots onto the VDB console ----
    SECTION("VDB-8024 -- SDMONV21 boots and prints its prompt on the video screen");
    {
        // The real machine (machines/sbc200v.toml), built here by hand: a 4 MHz Z80, the
        // SD build of the monitor at E000 (the VDM-console build), the DDBIOS at F000,
        // RAM elsewhere, and the VDB-8024 as the console. No CPU-visible output goes to a
        // serial line -- the monitor's '.' lands on the VDB's 80x24 grid, which the test
        // reads back with screenText().
        Machine m;
        NullDisplay disp;
        Vdb8024Board::setDisplay(&disp);
        std::string err;

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        auto region = [&](RegionKind k, uint32_t at, uint32_t size, const std::string& mount) {
            Region r; r.kind = k; r.at = at; r.size = size; r.mount = mount;
            CHECK(mem->addRegion(r, err), ("region mounts: " + err).c_str());
        };
        region(RegionKind::Ram, 0x0000, 0xE000, "");            // 56K 0000-DFFF
        region(RegionKind::Rom, 0xE000, 0,      "builtin:sdmonv21");
        region(RegionKind::Ram, 0xE800, 0x0800, "");            // 2K
        region(RegionKind::Rom, 0xF000, 0,      "builtin:ddb200");
        region(RegionKind::Ram, 0xF800, 0x0800, "");            // 2K
        setProperty(*mem, "fill", "zero", err);

        auto* vdb = dynamic_cast<Vdb8024Board*>(m.add("vdb8024", "vdb0", err));
        vdb->connect("keyboard", "scripted", err);
        auto* kbd = dynamic_cast<ScriptedStream*>(vdb->unitStream("keyboard"));

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();
        m.cpu()->setPc(0xE000);  // the reset jam-to-E000 is a later phase (as in sbc200)

        // Run until the monitor's '.' prompt lands on the screen.
        bool booted = false;
        for (int i = 0; i < 5'000'000 && !booted; ++i) {
            StepResult sr = m.master()->step(m.bus);
            m.clock.advance(sr.tStates);
            if ((i & 0x3FF) == 0 && vdb->screenText().find('.') != std::string::npos)
                booted = true;
        }
        CHECK(booted, "SDMONV21 printed its '.' prompt onto the VDB-8024 screen");

        // Now type a command AT the video console -- keystrokes reach the guest through
        // the keyboard line -- and watch its answer land on the same screen. H is hex
        // arithmetic: 'H 100 200' prints the difference of the operands, and 100-200 is
        // FF00 -- a computed value that appears on the screen only if the whole
        // keyboard -> guest -> display loop worked.
        kbd->feed("H 100 200\r");
        bool answered = false;
        for (int i = 0; i < 5'000'000 && !answered; ++i) {
            StepResult sr = m.master()->step(m.bus);
            m.clock.advance(sr.tStates);
            if ((i & 0x7F) == 0) vdb->pump();  // latch a keystroke, once the guest took the last
            if ((i & 0x7F) == 0 && vdb->screenText().find("FF00") != std::string::npos)
                answered = true;
        }
        CHECK(vdb->rxBytes() == 10, "all ten keystrokes reached the guest");
        CHECK(answered, "a command typed at the video console was answered on the screen (H -> FF00)");
    }

    SECTION("VDB-8024 -- SDOS boots on the video console and its INTERRUPT keyboard works");
    {
        // THE BUG THIS GUARDS. SDMONV21 (above) polls the keyboard, so it boots and takes
        // commands with no interrupt hardware at all. The disk operating systems do NOT: the
        // SD video build of SDOS runs its console input under a Z80 mode-2 interrupt -- the
        // VDB-8024's keyboard strobe (strapped to VI2) drives the SBC-200's CTC channel 1,
        // which vectors to the CBIOS keyboard ISR (vector 0x02). Miss that path and the
        // monitor still works but a booted OS never sees a key. This is the whole machine
        // (machines/sbc200v.toml + the VersaFloppy): boot SDOS off the disk, then type a
        // command through the interrupt and watch it echo onto the same screen.
        Machine m;
        NullDisplay disp;
        Vdb8024Board::setDisplay(&disp);
        std::string err;
        setMediaResolver(openHostFile);  // a real .DSK off disk (an earlier test may have swapped it)

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        auto region = [&](RegionKind k, uint32_t at, uint32_t size, const std::string& mount) {
            Region r; r.kind = k; r.at = at; r.size = size; r.mount = mount;
            CHECK(mem->addRegion(r, err), ("region mounts: " + err).c_str());
        };
        region(RegionKind::Ram, 0x0000, 0xE000, "");            // 56K 0000-DFFF
        region(RegionKind::Rom, 0xE000, 0,      "builtin:sdmonv21");
        region(RegionKind::Ram, 0xE800, 0x0800, "");            // 2K
        region(RegionKind::Rom, 0xF000, 0,      "builtin:ddb200");
        region(RegionKind::Ram, 0xF800, 0x0800, "");            // 2K
        setProperty(*mem, "fill", "zero", err);

        auto* vdb = dynamic_cast<Vdb8024Board*>(m.add("vdb8024", "vdb0", err));
        CHECK(setProperty(*vdb, "interrupt", "vi2", err), "the VDB keyboard straps to VI2");
        vdb->connect("keyboard", "scripted", err);
        auto* kbd = dynamic_cast<ScriptedStream*>(vdb->unitStream("keyboard"));

        // The SBC-200's CTC is what turns the VI2 strobe into the mode-2 vector; the 8251 has
        // no console here (the VDB is the console).
        m.add("sbc", "sbc0", err);

        auto* vf = dynamic_cast<VersaFloppyBoard*>(m.add("versafloppy", "vf0", err));
        CHECK(setProperty(*vf, "port", "60", err), "the VersaFloppy sits at the 60H block");
        CHECK(setProperty(*vf, "variant", "vfii", err), "an FD1791 (double-density) controller");
        std::string dsk = std::string(ALTAIR_SOURCE_DIR) + "/examples/sdsys/SDOS-18B-SSDDV-256-32K.DSK";
        CHECK(vf->loadSubUnit("drive", {{"unit", "0"}, {"mount", dsk}}, err),
              ("the SDOS video master mounts in drive A: " + err).c_str());

        m.add("z80", "cpu0", err);
        setProperty(*(dynamic_cast<Board*>(m.find("cpu0"))), "clock_hz", "4000000", err);
        m.power();
        m.cpu()->setPc(0xE000);  // the reset jam-to-E000 is a later phase (as in sbc200)

        auto run = [&](const char* needle, int budget) {
            for (int i = 0; i < budget; ++i) {
                StepResult sr = m.master()->step(m.bus);
                m.clock.advance(sr.tStates);
                if ((i & 0x7F) == 0) vdb->pump();  // latch keystrokes; paint nothing (NullDisplay)
                if ((i & 0x3FF) == 0 && vdb->screenText().find(needle) != std::string::npos)
                    return true;
            }
            return false;
        };

        CHECK(run(".", 5'000'000), "SDMONV21 printed its '.' prompt onto the VDB screen");

        // COLD-BOOT SDOS. `C` at the monitor prompt is polled (the monitor never armed an
        // interrupt), so this reaches the DDBIOS the same way a serial machine's would.
        kbd->feed("C\r");
        CHECK(run("SD-OS", 40'000'000), "SDOS cold-booted off the VersaFloppy and signed on");
        CHECK(run("[A]", 20'000'000), "...and reached its [A] command prompt");

        // NOW THE INTERRUPT PATH. SDOS is live; its console input is interrupt-only. Type a
        // line at the prompt: each character can only arrive through the VI2 -> CTC -> mode-2
        // ISR, and the CCP echoes it. If the echo lands on the screen, the whole chain works.
        uint64_t before = vdb->rxBytes();
        kbd->feed("STAT");
        CHECK(run("STAT", 20'000'000),
              "a line typed at the SDOS prompt echoed back -- the interrupt keyboard delivered it");
        CHECK(vdb->rxBytes() == before + 4, "all four keystrokes crossed into the guest");
    }
}
