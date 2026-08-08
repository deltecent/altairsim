#pragma once
//
// SD Systems VDB-8024 -- an S-100 video display board that is, to the host, a
// two-I/O-port intelligent terminal. See reference/SD Systems VDB-8024.md and
// docs/boards/sd-vdb8024.md.
//
// NOT MEMORY-MAPPED, DESPITE THE NAME. The real card carries its own Z80, an SMC
// CRT-5027, a private screen RAM and a character-generator PROM -- an entire terminal
// on one board -- and NONE of it is on the S-100 address bus. To the host it answers
// exactly two ports and behaves like a serial terminal with a parallel handshake, which
// is the counterpart to the SBC's 8251 console: the SD build of the boot monitor
// (builtin:sdmonv21, "SD monitor v2.10 for SBC-100 and VDM Console") polls THIS board
// where the MS build (msmonr21) polls the 8251. So we model the host-visible terminal
// behavior -- an 80x24 attributed character grid driven by control codes -- and never
// the 5027 or the onboard Z80 (the reference is explicit about this).
//
//   PORT base+0 (default 00H)  IN  = status. D1 (0x02) = a keyboard byte is waiting;
//                                    D2 (0x04) = the display will accept a byte. Both
//                                    active-HIGH; other bits undefined. OUT ignored.
//   PORT base+1 (default 01H)  IN  = keyboard data (7-bit ASCII); reading clears D1.
//                              OUT = display data -- a printable character or a control
//                                    word fed to the terminal state machine.
//
// TWO HOST SERVICES, one each way, exactly like the Sol-PC's video+keyboard:
//   - KEYBOARD IN is a ByteStream connected to an endpoint (CONNECT vdb0:keyboard
//     console), latched in pump() and surfaced at D1 / IN base+1 -- the SolBoard model.
//   - DISPLAY OUT renders the internal grid into an injected Display every pump(), the
//     VdmBoard model, so the board never touches SDL and a headless build tests it
//     against a NullDisplay.
//
// THE TERMINAL ITSELF IS THE SHARED ENGINE (host/terminal/): a TerminalScreen holds the
// attributed grid, a TerminalRenderer paints it, and the SD Systems VDB v1.6 dialect
// lives in SdVdb16Emulator below. The generic built-in terminal (issue #244) is the same
// three pieces with a different emulator and a serial byte stream in place of these ports.
//
// THE CONTROL-CODE MAP is the SD Systems VDB v1.6 firmware's (examples/sdsys/VDB16.LST),
// the firmware sdmonv21's console was written against: CR=0D, LF=0A, BS=08, TAB=09,
// cursor up=0B / right=0C, clear=1A, home=1E, new-line=1F, and ESC (1B) sequences
// = (position), * / : (clear), T (erase-EOL), Y (erase-EOS), G (attribute), & / ' (half/
// full intensity). E/R (insert/delete line) are no-ops in v1.6, as in the firmware.

#include "core/board.h"
#include "host/stream.h"
#include "host/terminal/emulator.h"
#include "host/terminal/renderer.h"
#include "host/terminal/screen.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Display;  // host/display.h -- injected; the board never learns it is SDL

// Turning an endpoint string into a stream is the monitor's job, not the board's
// (DESIGN.md 7.7). The composition root installs the resolver; the board hands a name
// down it. Declared identically to the chip headers' -- an alias to the same type is
// legal to repeat at namespace scope, so including this beside a UART board is fine.
using EndpointResolver =
    std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;

// The SD Systems VDB v1.6 firmware's control-code dialect, driving a TerminalScreen.
// This is the one board-specific half of the terminal; the screen and renderer are the
// shared engine. See the control-code map in the file header.
class SdVdb16Emulator : public TerminalEmulator {
public:
    void feed(uint8_t b, TerminalScreen& scr) override;
    void reset() override { parse_ = Parse::Normal; }

    // Snapshot: the parser is two bytes of state (the phase, and the row held between
    // the two bytes of ESC = row col).
    uint8_t parseByte() const { return (uint8_t)parse_; }
    uint8_t pendRow() const { return pendRow_; }
    void    setParseByte(uint8_t v) { parse_ = (Parse)v; }
    void    setPendRow(uint8_t v) { pendRow_ = v; }

private:
    void control(uint8_t b, TerminalScreen& scr);  // a C0 control code (b < 0x20)
    void escByte(uint8_t b, TerminalScreen& scr);   // the byte after ESC

    // ESC and the cursor-position sequence are multi-byte.
    enum class Parse : uint8_t { Normal, Esc, PosRow, PosCol, Attr };
    Parse   parse_   = Parse::Normal;
    uint8_t pendRow_ = 0;  // row held between the two bytes of ESC = row col
};

class Vdb8024Board : public Board {
public:
    Vdb8024Board();

    std::string type() const override { return "vdb8024"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // The board never pulls pin 73 directly -- its optional keyboard interrupt is a
    // VECTORED one. When strapped (reference 6: E17 -> one of E13-E16 = VI0-VI3), the
    // keyboard strobe raises an S-100 VI line, which an interrupt controller elsewhere
    // (the SBC-200's Z80-CTC) turns into a Z80 mode-2 vector. The SD video CBIOS runs
    // its keyboard on exactly this path; polled is the default (`interrupt = none`).
    bool    assertsInt() const override { return false; }
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    std::vector<UnitDef>  units() const override;
    std::vector<MapEntry> ioMap() const override;

    // A keystroke arriving is live traffic, so the idle policy stands the machine back
    // up for it (Board::rxBytes, [[idle policy]]) -- the same signal SolBoard gives.
    uint64_t rxBytes() const override { return kbRx_; }

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "keyboard" ? kb_.get() : nullptr;
    }

    // The host video service, wired once in main.cpp / tests/main.cpp -- an SdlDisplay
    // in the shipping binary, a NullDisplay headless. Borrowed; the root owns it.
    static void setDisplay(Display* d);

    // The monitor resolves an endpoint string to a stream; the board is not allowed to
    // know what a socket is (DESIGN.md 7.7). Installed in main.cpp and tests/main.cpp.
    static void setResolver(EndpointResolver r);

    // ---- For tests, without a window: the terminal's state read straight out. ----
    uint8_t statusByte() const;
    std::string screenText() const { return screen_.screenText(); }  // 24 lines, '\n'-joined
    uint8_t charAt(int row, int col) const { return screen_.charAt(row, col); }
    int cursorRow() const { return screen_.cursorRow(); }
    int cursorCol() const { return screen_.cursorCol(); }
    // Feed one display byte to the terminal state machine directly (what OUT base+1
    // does), so a test can drive the screen without a CPU.
    void putByte(uint8_t b) { emu_.feed(b, screen_); }

    static constexpr int kCols = 80;
    static constexpr int kRows = 24;

private:
    // ---- the keyboard line ----
    void latchKeyboard();  // take one byte off the line, from pump() only

    // Which board this is strapped at. The card is fixed at 00/01 (no host base-address
    // jumper on the real board); the property exists for tests and for symmetry.
    uint8_t base_ = 0x00;

    // ---- the terminal engine (host/terminal/) ----
    TerminalScreen   screen_{kRows, kCols};
    SdVdb16Emulator  emu_;
    TerminalRenderer renderer_;

    // ---- switches (SW/jumpers, reference 6) ----
    bool    reverse_    = false;  // whole-screen video polarity
    uint8_t cursorMode_ = 1;      // 0 = off, 1 = blink (the board default), 2 = steady
    int     videoWidth_ = 0;      // host window width in px, 0 = auto (~half the screen)

    // The keyboard-interrupt strap (E17 -> E13-E16). `none` = polled (the board default);
    // a VI line = the keyboard strobe raises that S-100 vectored-interrupt line while a
    // byte is waiting, for the SBC-200's CTC to vector on. `int` (raw pin 73) is not a
    // real option on this board -- the keyboard interrupt is vectored -- but viBit() maps
    // it to nothing, so a stray strap to it simply does not interrupt.
    IrqJumper kbIrq_ = IrqJumper::None;

    // ---- the keyboard holding register + strobe (status D1) ----
    std::unique_ptr<ByteStream> kb_;  // never null -- a NullStream stands in
    uint8_t  kbData_ = 0;
    bool     kbHave_ = false;
    uint64_t kbRx_   = 0;  // keystrokes handed to the guest -- the idle-traffic signal
};

} // namespace altair
