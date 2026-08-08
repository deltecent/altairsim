#include "test.h"

#include "host/display_null.h"
#include "host/endpoint.h"
#include "host/stream.h"
#include "host/terminal/adm3a.h"
#include "host/terminal/screen.h"
#include "host/terminal/stream.h"
#include "host/terminal/vt100.h"
#include "host/terminal/vt52.h"

#include <memory>
#include <string>

using namespace altair;

namespace {

// Drive the VT100 engine with no window and no endpoint: an emulator over a screen, the
// way a test proves any terminal -- feed it the bytes a guest would send and read the grid
// back. This is the emulation-independent way to test a dialect (test_vdb8024 does the same
// for the SD terminal), and it runs on every platform because it touches no display.
struct Eng {
    TerminalScreen scr{24, 80};
    Vt100Emulator  emu;

    void feed(const std::string& s) {
        for (char c : s) emu.feed((uint8_t)c, scr);
    }
    // Drain the bytes the terminal owes the guest (reports, encoded keys).
    std::string reply() {
        std::string out;
        uint8_t     b;
        while (emu.hasReply() && emu.takeReply(&b, 1) == 1) out.push_back((char)b);
        return out;
    }
    char at(int r, int c) { return (char)scr.charAt(r, c); }
};

// The same jig for the ADM-3A dialect -- a dumb terminal, so no reports come back, but the
// arrow keys DO encode (to ^H/^J/^K/^L), which reply() reads.
struct Adm {
    TerminalScreen scr{24, 80};
    Adm3aEmulator  emu;

    void feed(const std::string& s) {
        for (char c : s) emu.feed((uint8_t)c, scr);
    }
    std::string reply() {
        std::string out;
        uint8_t     b;
        while (emu.hasReply() && emu.takeReply(&b, 1) == 1) out.push_back((char)b);
        return out;
    }
    char at(int r, int c) { return (char)scr.charAt(r, c); }
};

// And for the VT52 -- ESC-letter sequences, ESC Y addressing, an ESC Z identity reply.
struct V52 {
    TerminalScreen scr{24, 80};
    Vt52Emulator   emu;

    void feed(const std::string& s) {
        for (char c : s) emu.feed((uint8_t)c, scr);
    }
    std::string reply() {
        std::string out;
        uint8_t     b;
        while (emu.hasReply() && emu.takeReply(&b, 1) == 1) out.push_back((char)b);
        return out;
    }
    char at(int r, int c) { return (char)scr.charAt(r, c); }
};

} // namespace

void test_terminal() {
    SECTION("terminal VT100 -- printable text and the C0 controls");
    {
        Eng g;
        g.feed("HI");
        CHECK(g.at(0, 0) == 'H' && g.at(0, 1) == 'I', "text lands at the cursor");
        CHECK(g.scr.cursorCol() == 2, "and advances it");
        g.feed("\r");
        CHECK(g.scr.cursorCol() == 0, "CR homes the column");
        g.feed("\n");
        CHECK(g.scr.cursorRow() == 1 && g.scr.cursorCol() == 0,
              "LF drops a line WITHOUT returning (VT100 LF is index only)");
        g.feed("\bX");  // BS at col 0 does nothing; X at col 0
        CHECK(g.at(1, 0) == 'X', "backspace stops at the left margin");
    }

    SECTION("terminal VT100 -- CUP, and cursor moves with defaults");
    {
        Eng g;
        g.feed("\x1b[5;10H");
        CHECK(g.scr.cursorRow() == 4 && g.scr.cursorCol() == 9,
              "ESC[5;10H is 1-based -> (4,9) zero-based");
        g.feed("\x1b[A");  // up one (default)
        CHECK(g.scr.cursorRow() == 3, "ESC[A moves up one");
        g.feed("\x1b[3B");
        CHECK(g.scr.cursorRow() == 6, "ESC[3B moves down three");
        g.feed("\x1b[2D");
        CHECK(g.scr.cursorCol() == 7, "ESC[2D moves left two");
        g.feed("\x1b[H");
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "ESC[H homes with no params");
        g.feed("\x1b[A\x1b[D");  // clamp at the top-left corner
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "moves clamp at the edges");
    }

    SECTION("terminal VT100 -- ED and EL (all three modes)");
    {
        Eng g;
        g.feed("\x1b[2;1HLINE-TWO");   // put text on row 2 (index 1)
        g.feed("\x1b[1;1HLINE-ONE");   // and row 1
        g.feed("\x1b[1;4H");           // cursor to (0,3), on the 'E' of LINE-ONE
        g.feed("\x1b[K");              // EL 0: erase to end of line
        CHECK(g.at(0, 2) == 'N' && g.at(0, 3) == ' ', "ESC[K clears from the cursor to EOL");
        g.feed("\x1b[1;4H\x1b[1K");    // EL 1: erase from BOL to cursor inclusive
        CHECK(g.at(0, 0) == ' ' && g.at(0, 3) == ' ', "ESC[1K clears from BOL to the cursor");

        g.feed("\x1b[2;1H\x1b[2J");    // ED 2: erase the whole page, cursor unmoved
        CHECK(g.at(1, 0) == ' ' && g.at(0, 0) == ' ', "ESC[2J blanks the whole screen");
        CHECK(g.scr.cursorRow() == 1 && g.scr.cursorCol() == 0,
              "...and ESC[2J does NOT move the cursor");
    }

    SECTION("terminal VT100 -- SGR attributes ride the grid's attribute plane");
    {
        Eng g;
        g.feed("\x1b[7mR");  // reverse on, then a char
        CHECK((g.scr.attr(0, 0) & TerminalScreen::kAttrReverse) != 0, "ESC[7m sets reverse");
        g.feed("\x1b[0mN");  // reset, then a char
        CHECK((g.scr.attr(0, 1) & TerminalScreen::kAttrReverse) == 0, "ESC[0m clears it");
        g.feed("\x1b[5;2mB");  // blink + faint together
        uint8_t a = g.scr.attr(0, 2);
        CHECK((a & TerminalScreen::kAttrBlink) && (a & TerminalScreen::kAttrHalf),
              "ESC[5;2m sets blink and half-intensity together");
    }

    SECTION("terminal VT100 -- DSR: ESC[6n reports the cursor position");
    {
        Eng g;
        g.feed("\x1b[6n");
        CHECK(g.reply() == std::string("\x1b[1;1R"), "at home the report is ESC[1;1R");
        g.feed("\x1b[10;20H\x1b[6n");
        CHECK(g.reply() == std::string("\x1b[10;20R"), "and it tracks the cursor (1-based)");
        g.feed("\x1b[5n");
        CHECK(g.reply() == std::string("\x1b[0n"), "ESC[5n answers 'terminal OK'");
    }

    SECTION("terminal VT100 -- arrow keys encode ESC[ , and ESC O under DECCKM");
    {
        Eng g;
        g.emu.keySpecial(TerminalEmulator::Key::Up);
        CHECK(g.reply() == std::string("\x1b[A"), "Up is ESC[A in normal cursor mode");
        g.emu.keySpecial(TerminalEmulator::Key::Left);
        CHECK(g.reply() == std::string("\x1b[D"), "Left is ESC[D");
        g.feed("\x1b[?1h");  // DECCKM on -- application cursor keys
        g.emu.keySpecial(TerminalEmulator::Key::Up);
        CHECK(g.reply() == std::string("\x1bOA"), "under DECCKM Up becomes ESC O A");
        g.feed("\x1b[?1l");  // back off
        g.emu.keySpecial(TerminalEmulator::Key::Right);
        CHECK(g.reply() == std::string("\x1b[C"), "and normal mode returns");
        // A plain ASCII key passes straight through.
        g.emu.keyAscii('q');
        CHECK(g.reply() == std::string("q"), "an ASCII key passes through unencoded");
    }

    SECTION("terminal VT100 -- save/restore cursor (ESC 7 / ESC 8)");
    {
        Eng g;
        g.feed("\x1b[3;4H\x1b" "7");   // go somewhere, save
        g.feed("\x1b[20;40H");          // move away
        g.feed("\x1b" "8");             // restore
        CHECK(g.scr.cursorRow() == 2 && g.scr.cursorCol() == 3, "ESC 8 restores the ESC 7 cursor");
    }

    SECTION("terminal VT100 -- a line wraps at the right margin");
    {
        Eng g;
        for (int i = 0; i < 80; ++i) g.feed("*");
        CHECK(g.scr.cursorRow() == 1 && g.scr.cursorCol() == 0, "the 80th column wraps");
        CHECK(g.at(0, 79) == '*', "the last column of row 0 holds the 80th char");
    }

    // ---- The TerminalStream: the engine wearing a ByteStream face ----
    SECTION("terminal stream -- write() paints the grid, read() drains the report");
    {
        // Construct one directly (no endpoint gate): the guest's OUT is write(), its IN is
        // read(). This is exactly what a UART does to the line.
        TerminalStream ts("terminal?emulation=vt100&size=80x24", 24, 80,
                          std::make_unique<Vt100Emulator>());
        CHECK(ts.describe() == "terminal?emulation=vt100&size=80x24", "describe() round-trips the spec");
        CHECK(ts.writable(), "a terminal is always writable -- it never stalls the guest");

        const char* msg = "AB";
        CHECK(ts.write((const uint8_t*)msg, 2) == 2, "write() consumes every byte");
        CHECK(ts.screen().charAt(0, 0) == 'A' && ts.screen().charAt(0, 1) == 'B',
              "the bytes landed on the grid");

        CHECK(!ts.readable(), "nothing to read until the guest asks a question");
        const char* dsr = "\x1b[6n";
        ts.write((const uint8_t*)dsr, 4);
        CHECK(ts.readable(), "ESC[6n leaves a report waiting");
        uint8_t buf[16];
        size_t  n = ts.read(buf, sizeof buf);
        CHECK(std::string((char*)buf, n) == std::string("\x1b[1;3R"),
              "the report reads back the cursor after 'AB' (row 1, col 3, 1-based)");
        CHECK(!ts.readable(), "and the report is consumed once");
    }

    SECTION("terminal stream -- pump() paints into the injected display");
    {
        // tests/main.cpp injected a NullDisplay and the bundled font, so a pump() renders a
        // frame into memory -- the headless proof that the render path is whole.
        TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
        const char* msg = "HELLO";
        ts.write((const uint8_t*)msg, 5);
        ts.pump();  // should paint one frame (the screen is dirty) without a window
        CHECK(!TerminalStream::hasWindow(), "the test display is not windowed");
    }

    // ---- The `terminal:` endpoint grammar, through the real resolver ----
    SECTION("terminal endpoint -- the grammar is validated before the window check");
    {
        std::string err;

        // A bad emulation and a bad size are the operator's typo -- caught on EVERY build,
        // even this headless one, because grammar is checked before capability.
        CHECK(!resolveEndpoint("terminal?emulation=zork", err), "unknown emulation is refused");
        CHECK(err.find("zork") != std::string::npos, "and the message names it");

        err.clear();
        CHECK(!resolveEndpoint("terminal?size=9x9", err), "an out-of-range size is refused");
        CHECK(err.find("size") != std::string::npos, "and the message says so");

        err.clear();
        CHECK(!resolveEndpoint("terminal?size=eighty", err), "a non-numeric size is refused");

        err.clear();
        CHECK(!resolveEndpoint("terminal?bogus=1", err), "an unknown option is refused");
        CHECK(err.find("bogus") != std::string::npos, "and the message names it");

        err.clear();
        CHECK(!resolveEndpoint("terminal!", err), "junk after 'terminal' is refused");
    }

    SECTION("terminal endpoint -- a well-formed spec refuses cleanly with no window");
    {
        std::string err;
        // Grammar is fine here; it fails ONLY because the test display is a NullDisplay.
        CHECK(!resolveEndpoint("terminal", err), "a headless build has no terminal window");
        CHECK(err.find("window") != std::string::npos, "and the message explains why");

        err.clear();
        CHECK(!resolveEndpoint("terminal:?emulation=ansi&size=132x24", err),
              "the leading-colon form parses, then refuses for want of a window");
        CHECK(err.find("window") != std::string::npos,
              "the failure is the window, not the grammar (ansi and 132x24 are valid)");
    }

    // ---- The ADM-3A dialect: the dumb CP/M terminal ----
    SECTION("terminal ADM-3A -- printable text and the cursor-move control codes");
    {
        Adm g;
        g.feed("HI");
        CHECK(g.at(0, 0) == 'H' && g.at(0, 1) == 'I', "text lands at the cursor");
        CHECK(g.scr.cursorCol() == 2, "and advances it");
        g.feed("\r");
        CHECK(g.scr.cursorCol() == 0, "CR homes the column");
        g.feed("\n");
        CHECK(g.scr.cursorRow() == 1, "LF drops a line");
        g.feed("\x0b");  // ^K -- cursor up
        CHECK(g.scr.cursorRow() == 0, "^K moves the cursor up");
        g.feed("\x0c");  // ^L -- cursor right
        CHECK(g.scr.cursorCol() == 1, "^L moves the cursor right");
        g.feed("\b");    // ^H -- cursor left
        CHECK(g.scr.cursorCol() == 0, "^H (BS) moves the cursor left");
    }

    SECTION("terminal ADM-3A -- ESC = loads the cursor with a 0x20 bias");
    {
        Adm g;
        // ESC = <row+0x20> <col+0x20>. Row 4, col 9 -> 0x24, 0x29.
        g.feed("\x1b=\x24\x29");
        CHECK(g.scr.cursorRow() == 4 && g.scr.cursorCol() == 9,
              "ESC = (space+4)(space+9) addresses (4,9)");
        // Space bias means a literal space is the origin.
        g.feed("\x1b=  ");  // ESC = <space> <space>
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "ESC = (space)(space) is home");
    }

    SECTION("terminal ADM-3A -- ^Z clears and homes, ^^ homes without clearing");
    {
        Adm g;
        g.feed("\x1b=\x24\x29TEXT");        // put TEXT at (4,9)
        g.feed("\x1e");                     // ^^ -- home, no clear
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "^^ homes the cursor");
        CHECK(g.at(4, 9) == 'T', "...and leaves the screen intact");
        g.feed("\x1a");                     // ^Z -- clear + home
        CHECK(g.at(4, 9) == ' ', "^Z blanks the screen");
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "...and homes the cursor");
    }

    SECTION("terminal ADM-3A -- arrow keys are the vi motion codes, no reports");
    {
        Adm g;
        g.emu.keySpecial(TerminalEmulator::Key::Up);
        CHECK(g.reply() == std::string("\x0b"), "Up is ^K");
        g.emu.keySpecial(TerminalEmulator::Key::Down);
        CHECK(g.reply() == std::string("\x0a"), "Down is ^J");
        g.emu.keySpecial(TerminalEmulator::Key::Left);
        CHECK(g.reply() == std::string("\x08"), "Left is ^H");
        g.emu.keySpecial(TerminalEmulator::Key::Right);
        CHECK(g.reply() == std::string("\x0c"), "Right is ^L");
        g.emu.keySpecial(TerminalEmulator::Key::Home);
        CHECK(g.reply() == std::string("\x1e"), "Home is ^^");
        // A dumb terminal answers no status query -- ESC[6n is just bytes, not a report.
        g.feed("\x1b[6n");
        CHECK(g.reply().empty(), "the ADM-3A never reports its cursor");
    }

    SECTION("terminal endpoint -- adm3a is an accepted emulation name");
    {
        std::string err;
        // Grammar (the name) is valid; it fails only for the headless window, like vt100.
        CHECK(!resolveEndpoint("terminal?emulation=adm3a", err),
              "adm3a parses, then refuses for want of a window");
        CHECK(err.find("window") != std::string::npos, "the failure is the window, not the name");
    }

    // ---- The VT52 dialect: ESC-letter sequences, no CSI ----
    SECTION("terminal VT52 -- printable text and ESC-letter cursor moves");
    {
        V52 g;
        g.feed("HI");
        CHECK(g.at(0, 0) == 'H' && g.at(0, 1) == 'I', "text lands at the cursor");
        g.feed("\r\n");
        CHECK(g.scr.cursorRow() == 1 && g.scr.cursorCol() == 0, "CR/LF drop to the next line");
        g.feed("\x1b" "C");  // ESC C -- cursor right
        CHECK(g.scr.cursorCol() == 1, "ESC C moves right");
        g.feed("\x1b" "A");  // ESC A -- cursor up
        CHECK(g.scr.cursorRow() == 0, "ESC A moves up");
        g.feed("\x1b" "B");  // ESC B -- cursor down
        CHECK(g.scr.cursorRow() == 1, "ESC B moves down");
        g.feed("\x1b" "D");  // ESC D -- cursor left
        CHECK(g.scr.cursorCol() == 0, "ESC D moves left");
        g.feed("\x1b" "H");  // ESC H -- home
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "ESC H homes");
    }

    SECTION("terminal VT52 -- ESC Y addresses the cursor (0x20 bias)");
    {
        V52 g;
        // ESC Y <row+0x20> <col+0x20>. Row 4, col 9 -> 0x24, 0x29.
        g.feed("\x1b" "Y\x24\x29");
        CHECK(g.scr.cursorRow() == 4 && g.scr.cursorCol() == 9,
              "ESC Y (space+4)(space+9) addresses (4,9)");
        g.feed("\x1b" "Y  ");  // ESC Y <space><space> -> home
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "ESC Y (space)(space) is home");
    }

    SECTION("terminal VT52 -- ESC J and ESC K erase, ESC Z identifies");
    {
        V52 g;
        g.feed("\x1b" "Y\x20\x20" "ABCDE");   // ABCDE from home
        g.feed("\x1b" "Y\x20\x22");           // back to (0,2), on 'C'
        g.feed("\x1b" "K");                    // erase to end of line
        CHECK(g.at(0, 1) == 'B' && g.at(0, 2) == ' ', "ESC K clears from the cursor to EOL");
        g.feed("\x1b" "Z");                    // identify
        CHECK(g.reply() == std::string("\x1b/K"), "ESC Z answers ESC / K (a VT52)");
    }

    SECTION("terminal VT52 -- arrow keys are ESC-letter, no CSI");
    {
        V52 g;
        g.emu.keySpecial(TerminalEmulator::Key::Up);
        CHECK(g.reply() == std::string("\x1b" "A"), "Up is ESC A");
        g.emu.keySpecial(TerminalEmulator::Key::Left);
        CHECK(g.reply() == std::string("\x1b" "D"), "Left is ESC D");
        g.emu.keySpecial(TerminalEmulator::Key::Home);
        CHECK(g.reply() == std::string("\x1b" "H"), "Home is ESC H");
        g.emu.keyAscii('q');
        CHECK(g.reply() == std::string("q"), "an ASCII key passes through unencoded");
    }

    SECTION("terminal endpoint -- vt52 is an accepted emulation name");
    {
        std::string err;
        CHECK(!resolveEndpoint("terminal?emulation=vt52", err),
              "vt52 parses, then refuses for want of a window");
        CHECK(err.find("window") != std::string::npos, "the failure is the window, not the name");
    }
}
