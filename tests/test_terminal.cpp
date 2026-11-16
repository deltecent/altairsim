#include "test.h"

#include "host/display_null.h"
#include "host/endpoint.h"
#include "host/stream.h"
#include "host/terminal/adm3a.h"
#include "host/terminal/h19.h"
#include "host/terminal/screen.h"
#include "host/terminal/font.h"
#include "host/terminal/stream.h"
#include "host/terminal/vt100.h"
#include "host/terminal/vt52.h"

#include <cstring>
#include <memory>
#include <optional>
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

// And the H19 -- a VT52 superset in Heath mode, with an ANSI (VT100) mode after ESC <.
struct H19 {
    TerminalScreen scr{24, 80};
    H19Emulator    emu;

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

    // ---- The [terminal] transform chain (issue #244 follow-up) ----
    SECTION("terminal stream -- the [terminal] transforms fold at the right seams");
    {
        // settings() is one static (a global section, like [console]); reset it around the
        // block so it neither inherits nor leaks state across sections.
        auto& s = TerminalStream::settings();
        s = TerminalStream::Settings{};

        // strip7out: an even-parity monitor (PS II) sends CR as 0x8D. Off, it is >= 0x20 and
        // prints as a glyph, so the column advances; on, bit 7 is masked and the 0x0D homes
        // the cursor -- exactly the "LF without CR" the reporter hit.
        {
            TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
            const uint8_t ab[] = {'A', 'B', 0x8D};
            ts.write(ab, 3);
            CHECK(ts.screen().cursorCol() == 3,
                  "without strip7out an even-parity CR (0x8D) prints as a glyph");
            s.strip7out = true;
            ts.write(ab, 3);  // A,B advance from col 3, then 0x8D -> 0x0D homes the column
            CHECK(ts.screen().cursorCol() == 0, "with strip7out 0x8D becomes a carriage return");
            s.strip7out = false;
        }

        // crlf: a guest that emits a bare CR and no LF. Off, CR only homes the column;
        // on, an LF follows and the row advances.
        {
            TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
            const uint8_t cr[] = {'X', 0x0D};
            ts.write(cr, 2);
            CHECK(ts.screen().cursorRow() == 0, "crlf=off: a bare CR stays on the same row");
            s.crlf = true;
            ts.write(cr, 2);
            CHECK(ts.screen().cursorRow() == 1, "crlf=on: an LF follows the CR, the row advances");
            s.crlf = false;
        }

        // Issue #768: one name on the console and the terminal. `crlf` is set through the
        // same path SET TERMINAL and [terminal] use; the old `cr` key is refused with the
        // line to write instead, in any case.
        {
            std::string err;
            CHECK(TerminalStream::set("crlf", "on", err) && s.crlf, "SET TERMINAL crlf=on sets it");
            CHECK(TerminalStream::set("CRLF", "off", err) && !s.crlf, "...and crlf=off clears it");
            err.clear();
            CHECK(!TerminalStream::set("cr", "crlf", err) && !s.crlf,
                  "the old key `cr` is refused and changes nothing");
            CHECK(err.find("'cr' is now 'crlf'") != std::string::npos &&
                      err.find("crlf=on") != std::string::npos,
                  "...and the refusal gives the new key and value");
            err.clear();
            CHECK(!TerminalStream::set("CR", "cr", err) && !err.empty(),
                  "...in any case, as every key is");
        }

        // upper / strip7in / bsdel fold KEYSTROKES, in keyAscii(), on the way to the guest.
        {
            TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
            uint8_t buf[8];
            s.upper = true;
            ts.keyAscii('a');
            CHECK(ts.read(buf, sizeof buf) == 1 && buf[0] == 'A', "upper folds a typed key");
            s.upper = false;

            s.strip7in = true;
            ts.keyAscii(0xE1);
            CHECK(ts.read(buf, sizeof buf) == 1 && buf[0] == 0x61, "strip7in masks bit 7 of a key");
            s.strip7in = false;

            s.bsdel = BsMap::Bs;
            ts.keyAscii(0x7F);
            CHECK(ts.read(buf, sizeof buf) == 1 && buf[0] == 0x08, "bsdel=bs folds DEL to BS");
            s.bsdel = BsMap::Off;
        }

        // echo: the transformed key is painted locally, for a half-duplex guest that won't.
        {
            TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
            s.echo  = true;
            s.upper = true;
            ts.keyAscii('z');
            CHECK(ts.screen().charAt(0, 0) == 'Z', "echo paints the transformed key on the screen");
            uint8_t buf[8];
            CHECK(ts.read(buf, sizeof buf) == 1 && buf[0] == 'Z', "and the guest still receives it");
            s.echo  = false;
            s.upper = false;
        }

        // THE guard for folding in keyAscii() and NOT read(): status replies share the reply
        // FIFO with keystrokes. An H19 ESC n report carries a lowercase coordinate byte here
        // (col 65 -> 0x20+65 = 0x61 'a'); a fold in read() would corrupt it. read() must not fold.
        {
            TerminalStream ts("terminal?emulation=h19", 24, 80, std::make_unique<H19Emulator>());
            s.upper    = true;
            s.strip7in = true;
            const uint8_t addr[] = {0x1b, 'Y', 0x25, 0x61};  // ESC Y row=5 col=65 (byte 'a')
            ts.write(addr, 4);
            const uint8_t report[] = {0x1b, 'n'};
            ts.write(report, 2);
            uint8_t buf[8];
            size_t  n = ts.read(buf, sizeof buf);
            CHECK(std::string((char*)buf, n) == std::string("\x1b" "Y\x25\x61"),
                  "read() drains a report UNFOLDED -- the lowercase coordinate survives upper=on");
            s.upper    = false;
            s.strip7in = false;
        }

        s = TerminalStream::Settings{};  // leave it as the next section expects
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

    SECTION("terminal stream -- its window is a keyboard even with keyboard=none (#767)");
    {
        // `[display] keyboard = none` makes a VIDEO window display-only (a Dazzler's). The
        // terminal's own window still sends its keys: pump() claims the keyboard for it, and
        // the back end asks keysToGuest() per window as each key arrives.
        Display* d = TerminalStream::display();
        CHECK(d != nullptr, "tests/main.cpp injected a display");
        int videoBoard = 0;  // any other drawer's owner handle
        Display::setKeyboardToConsole(false);
        // An optional keeps the storage in scope after reset(), so the address checked below
        // still names live memory (GCC's -Wdangling-pointer flags a block-scoped local).
        std::optional<TerminalStream> ts;
        ts.emplace("terminal", 24, 80, std::make_unique<Vt100Emulator>());
        const void* owner = &*ts;
        ts->pump();
        CHECK(d->keysToGuest(owner), "keyboard=none: the terminal window still sends its keys");
        CHECK(!d->keysToGuest(&videoBoard), "keyboard=none: a video window sends nothing");
        ts.reset();
        CHECK(!d->keysToGuest(owner), "a destroyed terminal leaves no claim behind");
        Display::setKeyboardToConsole(true);
        CHECK(d->keysToGuest(&videoBoard), "keyboard=console: every window sends its keys");
    }

    // ---- The host keyboard -> the line, encoded by the dialect ----
    SECTION("terminal stream -- the host keyboard reaches the line, encoded by the dialect");
    {
        TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());

        uint8_t buf[16];
        ts.keyAscii('k');
        size_t n = ts.read(buf, sizeof buf);
        CHECK(std::string((char*)buf, n) == "k", "an ASCII key reaches the guest unencoded");

        // The point of the symbolic special-key path: each dialect spells an arrow its own
        // way, which the display's single byte table could never do.
        ts.keySpecial((int)TerminalEmulator::Key::Up);
        n = ts.read(buf, sizeof buf);
        CHECK(std::string((char*)buf, n) == std::string("\x1b[A"),
              "a VT100 line encodes Up as ESC[A");

        {
            TerminalStream adm("terminal?emulation=adm3a", 24, 80,
                               std::make_unique<Adm3aEmulator>());
            adm.keySpecial((int)TerminalEmulator::Key::Up);
            n = adm.read(buf, sizeof buf);
            CHECK(std::string((char*)buf, n) == std::string("\x0b"),
                  "an ADM-3A line encodes the SAME key as ^K -- the dialect decides");
        }
    }

    // ---- Each window types on its own line ----
    SECTION("terminal stream -- a key goes to the terminal whose window it was typed in");
    {
        int  videoBoard = 0;  // any other window's owner
        auto readAll    = [](TerminalStream& t) {
            uint8_t     buf[64];
            std::string s;
            for (size_t n; (n = t.read(buf, sizeof buf)) > 0;) s.append((char*)buf, n);
            return s;
        };
        auto tty1 = std::make_unique<TerminalStream>("terminal", 24, 80,
                                                     std::make_unique<Vt100Emulator>());
        auto tty2 = std::make_unique<TerminalStream>("terminal?emulation=vt52", 24, 80,
                                                     std::make_unique<Vt52Emulator>());
        CHECK(TerminalStream::forOwner(tty1.get()) == tty1.get(), "tty1's window is tty1's");
        CHECK(TerminalStream::forOwner(tty2.get()) == tty2.get(), "tty2's window is tty2's");
        CHECK(TerminalStream::forOwner(&videoBoard) == nullptr, "a video window is no terminal's");
        CHECK(TerminalStream::forOwner(nullptr) == nullptr, "no window is no terminal's");

        const uint8_t one[] = {'d', 'i', 'r', '\r'};
        const uint8_t two[] = {'w', 'h', 'o'};
        CHECK(TerminalStream::routeKeys(tty1.get(), one, sizeof one), "tty1 takes its keys");
        CHECK(TerminalStream::routeKeys(tty2.get(), two, sizeof two), "tty2 takes its keys");
        CHECK(!TerminalStream::routeKeys(&videoBoard, two, sizeof two),
              "keys from a video window are refused: the caller gives them to the Console");
        CHECK(readAll(*tty1) == "dir\r", "tty1's line carries only tty1's keys");
        CHECK(readAll(*tty2) == "who", "tty2's line carries only tty2's keys");

        // An arrow is encoded by the dialect of the window it was pressed in.
        CHECK(TerminalStream::routeSpecial(tty1.get(), (int)TerminalEmulator::Key::Up), "Up, tty1");
        CHECK(TerminalStream::routeSpecial(tty2.get(), (int)TerminalEmulator::Key::Up), "Up, tty2");
        CHECK(readAll(*tty1) == "\x1b[A", "the VT100 window sends ESC[A");
        CHECK(readAll(*tty2) == "\x1b" "A", "the VT52 window sends ESC A");
        CHECK(!TerminalStream::routeSpecial(&videoBoard, (int)TerminalEmulator::Key::Up),
              "an arrow from a video window is not a terminal's");

        // The newest terminal no longer takes every key: with tty2 open, tty1 still types.
        auto tty3 = std::make_unique<TerminalStream>("terminal", 24, 80,
                                                     std::make_unique<Vt100Emulator>());
        CHECK(TerminalStream::routeKeys(tty1.get(), one, 1) && readAll(*tty1) == "d",
              "opening another terminal does not take tty1's keyboard");

        // CONFIG LOAD builds the replacement line before the old one goes: both are live for
        // that moment, and destroying the old one leaves the others routed.
        const void* gone = tty2.get();
        tty2.reset();
        CHECK(TerminalStream::forOwner(gone) == nullptr, "a closed terminal's window types nowhere");
        CHECK(TerminalStream::forOwner(tty1.get()) == tty1.get() &&
                  TerminalStream::forOwner(tty3.get()) == tty3.get(),
              "closing one terminal leaves the others their keyboards");
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

    // ---- The H19 dialect: VT52 superset in Heath mode + an ANSI (VT100) mode ----
    SECTION("terminal H19 -- Heath-mode cursor moves and ESC Y addressing");
    {
        H19 g;
        g.feed("HI");
        CHECK(g.at(0, 0) == 'H' && g.at(0, 1) == 'I', "text lands at the cursor");
        g.feed("\x1b" "Y\x24\x29");  // ESC Y row=4 col=9
        CHECK(g.scr.cursorRow() == 4 && g.scr.cursorCol() == 9, "ESC Y addresses (4,9)");
        g.feed("\x1b" "A");
        CHECK(g.scr.cursorRow() == 3, "ESC A moves up");
        g.feed("\x1b" "H");
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0, "ESC H homes");
    }

    SECTION("terminal H19 -- the extra Heath escapes: erase, reverse video, save/restore");
    {
        H19 g;
        g.feed("\x1b" "Y\x20\x20" "ABCDE");   // ABCDE from home
        g.feed("\x1b" "Y\x20\x22");           // (0,2), on 'C'
        g.feed("\x1b" "o");                    // ESC o -- erase to start of line
        CHECK(g.at(0, 0) == ' ' && g.at(0, 2) == ' ' && g.at(0, 3) == 'D',
              "ESC o clears from BOL to the cursor");
        g.feed("\x1b" "j");                    // save cursor at (0,2)
        g.feed("\x1b" "Y\x25\x25");           // move away to (5,5)
        g.feed("\x1b" "k");                    // restore
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 2, "ESC j/k save and restore the cursor");
        g.feed("\x1b" "pR");                   // reverse on, then a char
        CHECK((g.scr.attr(0, 2) & TerminalScreen::kAttrReverse) != 0, "ESC p sets reverse video");
        g.feed("\x1b" "qN");                   // reverse off, then a char
        CHECK((g.scr.attr(0, 3) & TerminalScreen::kAttrReverse) == 0, "ESC q clears reverse video");
        g.feed("\x1b" "E");                    // clear display + home
        CHECK(g.at(0, 3) == ' ' && g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0,
              "ESC E clears the display and homes");
    }

    SECTION("terminal H19 -- ESC n reports the cursor as ESC Y r c");
    {
        H19 g;
        g.feed("\x1b" "Y\x2A\x30");  // (10,16)
        g.feed("\x1b" "n");
        CHECK(g.reply() == std::string("\x1b" "Y\x2A\x30"),
              "ESC n answers ESC Y (row+0x20)(col+0x20)");
    }

    SECTION("terminal H19 -- arrows are ESC-letter in Heath mode");
    {
        H19 g;
        g.emu.keySpecial(TerminalEmulator::Key::Up);
        CHECK(g.reply() == std::string("\x1b" "A"), "Up is ESC A in Heath mode");
        g.emu.keySpecial(TerminalEmulator::Key::Right);
        CHECK(g.reply() == std::string("\x1b" "C"), "Right is ESC C");
    }

    SECTION("terminal H19 -- ESC < enters ANSI mode; ESC[?2l returns to Heath");
    {
        H19 g;
        g.feed("\x1b<");                 // enter ANSI (VT100) mode
        g.feed("\x1b[5;10H");            // now a VT100 CSI works
        CHECK(g.scr.cursorRow() == 4 && g.scr.cursorCol() == 9,
              "in ANSI mode the H19 honors VT100 CSI cursor addressing");
        g.emu.keySpecial(TerminalEmulator::Key::Up);
        CHECK(g.reply() == std::string("\x1b[A"), "and the arrows encode the VT100 way");
        g.feed("\x1b[6n");               // VT100 DSR is forwarded from the inner engine
        CHECK(g.reply() == std::string("\x1b[5;10R"), "the VT100 DSR report is forwarded");
        g.feed("\x1b[?2l");              // DECANM reset -> back to Heath mode
        g.feed("\x1b" "H");             // a Heath ESC H works again
        CHECK(g.scr.cursorRow() == 0 && g.scr.cursorCol() == 0,
              "ESC[?2l drops back to Heath mode, where ESC H homes");
    }

    SECTION("terminal endpoint -- h19 is an accepted emulation name");
    {
        std::string err;
        CHECK(!resolveEndpoint("terminal?emulation=h19", err),
              "h19 parses, then refuses for want of a window");
        CHECK(err.find("window") != std::string::npos, "the failure is the window, not the name");
    }
    SECTION("paste -- clipboard text becomes the keys a terminal sends (pasteKeys)");
    {
        CHECK(pasteKeys("DIR\n") == "DIR\r", "LF is the Return key: CR");
        CHECK(pasteKeys("A\r\nB\r\n") == "A\rB\r", "CR LF is one CR, not two");
        CHECK(pasteKeys("A\rB") == "A\rB", "a lone CR stays");
        CHECK(pasteKeys("10 PRINT\t\"HI\"") == "10 PRINT\t\"HI\"", "printable ASCII and TAB stay");
        CHECK(pasteKeys("x\x01\x7Fy") == "xy", "other control bytes and DEL are dropped");
        CHECK(pasteKeys("\xE2\x80\x9Cq\xE2\x80\x9D") == "?q?", "each non-ASCII character is one ?");
        CHECK(pasteKeys("").empty(), "an empty clipboard sends nothing");
    }
    SECTION("copy -- a mouse selection in reading order; the whole screen with none");
    {
        TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
        const char* msg = "HELLO\r\nWORLD  \r\nTHIRD";
        ts.write((const uint8_t*)msg, std::strlen(msg));
        const TerminalFont* f = TerminalStream::font();
        const int cw = f ? f->cellCols() : 8, ch = f ? f->cellRows() : 16;
        auto at = [&](int kind, int row, int col) { ts.pointer(kind, col * cw + 1, row * ch + 1); };

        CHECK(ts.copyText() == "HELLO\nWORLD\nTHIRD",
              "no selection: the whole screen, trailing blanks and blank rows cut");
        at(0, 0, 1);
        CHECK(!ts.hasSelection(), "a click alone selects nothing");
        at(1, 1, 2);
        at(2, 1, 2);
        CHECK(ts.hasSelection(), "a drag selects");
        CHECK(ts.copyText() == "ELLO\nWOR", "from the anchor cell to the head cell, both included");
        at(0, 2, 2);
        at(2, 0, 3);
        CHECK(ts.copyText() == "LO\nWORLD\nTHI", "a drag upward selects the same as downward");
        at(0, 0, 0);
        at(2, 0, 200);
        CHECK(ts.copyText() == "HELLO", "a drag past the right edge stops at the edge");
        ts.keyAscii('x');
        CHECK(!ts.hasSelection(), "a typed key clears the selection");
    }
    SECTION("copy -- the window's copy reaches the host clipboard; the selection draws reversed");
    {
        // A display that keeps what was put on its clipboard. The terminal's display is a
        // static, so it is swapped in for this section and the test display put back after.
        struct ClipDisplay : NullDisplay {
            std::string clip;
            int         sets = 0;
            void        setClipboardText(const std::string& t) override { clip = t; ++sets; }
        } cd;
        Display* const was = TerminalStream::display();
        TerminalStream::setDisplay(&cd);
        {
            int  videoBoard = 0;
            TerminalStream ts("terminal", 24, 80, std::make_unique<Vt100Emulator>());
            ts.write((const uint8_t*)"A", 1);
            CHECK(!TerminalStream::routeCopy(&videoBoard) && cd.sets == 0,
                  "a copy from a video window is not a terminal's: the clipboard is untouched");
            CHECK(TerminalStream::routeCopy(&ts) && cd.sets == 1 && cd.clip == "A",
                  "a copy from the terminal's window puts its text on the clipboard");

            const TerminalFont* f = TerminalStream::font();
            const int cw = f ? f->cellCols() : 8, ch = f ? f->cellRows() : 16;
            CHECK(TerminalStream::routePointer(&ts, 0, 0 * cw + 1, 1) &&
                      TerminalStream::routePointer(&ts, 1, 3 * cw + 1, 1) &&
                      TerminalStream::routePointer(&ts, 2, 3 * cw + 1, 1),
                  "the mouse in the terminal's window reaches the terminal");
            CHECK(!TerminalStream::routePointer(&videoBoard, 0, 0, 0),
                  "the mouse in a video window is not a terminal's");
            ts.pump();
            const Surface* sf = cd.surface(&ts);
            CHECK(sf != nullptr, "the terminal painted a frame");
            if (sf) {
                // Cell 3 is a blank in the selection; cell 5 is a blank outside it. The cursor
                // is on cell 1, so neither is the cursor's cell.
                auto litIn = [&](int col) {
                    int lit = 0;
                    for (int y = 0; y < ch; ++y)
                        for (int x = col * cw; x < (col + 1) * cw; ++x)
                            lit += sf->pixels()[(size_t)y * sf->width() + x] != 0;
                    return lit;
                };
                CHECK(litIn(3) == cw * ch, "a selected blank cell is drawn reversed: all lit");
                CHECK(litIn(5) == 0, "a blank cell outside the selection stays dark");
            }
            CHECK(TerminalStream::routeCopy(&ts) && cd.clip == "A",
                  "the copy is the selection (the blanks at its end are cut)");
        }
        TerminalStream::setDisplay(was);
    }
}
