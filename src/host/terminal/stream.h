#pragma once
//
// TerminalStream -- the generic built-in terminal as a ByteStream endpoint (issue #244).
//
// This is what `connect = "terminal"` resolves to. It is the terminal engine
// (host/terminal/) wearing a ByteStream face: the guest's output (write()) is fed to a
// TerminalEmulator that drives a TerminalScreen, which a TerminalRenderer paints into the
// host Display every pump(); the guest's input (read()) is drained from the emulator's
// reply FIFO -- the cursor reports it owes and the host keystrokes routed to this line.
//
// So every serial board gets a windowed terminal for free, exactly as printing did: no
// board learns what a terminal is, only that it has a line (DESIGN.md 7.1, 7.7). The
// window is the SIMULATOR's, so the emulation is the same on every platform and answers
// the period reports (ESC[6n) an external xterm would answer with its own geometry.
//
// THE DISPLAY AND FONT ARE INJECTED ONCE, at the composition root (setDisplay/setFont),
// the same way the boards' Display and the endpoint resolver are -- a stream constructed
// deep inside resolveEndpoint cannot be handed them, and they are session-lifetime host
// resources it only borrows. In a headless build (or a test) the display is a NullDisplay,
// which answers isWindowed() false; resolveEndpoint refuses `terminal:` there rather than
// opening a serial line nobody can see (see hasWindow()).
//
// KEYBOARD ROUTING IS NOT HERE YET. Today the one host window feeds the single Console
// (src/main.cpp); wiring each terminal window's keys to ITS line is the multi-window work
// (issue #244, Task 4). keyAscii()/keySpecial() are the seam that work will drive; until
// then a `terminal:` line renders the guest and answers its reports, and read() carries
// only those reports.

#include "host/filter.h"  // BsMap -- the terminal reuses the console's rubout mapping
#include "host/stream.h"
#include "host/terminal/renderer.h"
#include "host/terminal/screen.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Display;
class TerminalFont;
class TerminalEmulator;

class TerminalStream : public ByteStream {
public:
    // `spec` is echoed by describe() (round-trips through SHOW / CONFIG SAVE). The grid is
    // sized here; the emulator is the chosen dialect. The font and display are the statics.
    TerminalStream(std::string spec, int rows, int cols,
                   std::unique_ptr<TerminalEmulator> emu);
    ~TerminalStream() override;

    std::string describe() const override { return spec_; }

    size_t read(uint8_t* buf, size_t n) override;   // guest <- reports + keystrokes
    size_t write(const uint8_t* buf, size_t n) override;  // guest -> screen
    bool   readable() const override;
    bool   writable() const override { return true; }  // the terminal never stalls the guest

    void pump() override;  // paint the frame, once per slice, on the main thread

    // Host keyboard -> this line (the seam the multi-window work drives, and what a test
    // types with). Encoded by the emulator and enqueued toward the guest.
    void keyAscii(uint8_t b);
    void keySpecial(int key);  // a TerminalEmulator::Key value

    // ---- [terminal] transform chain (issue #244 follow-up) ----------------------
    // A serial LINE must stay 8-bit clean (host/filter.h): a strip7out on a socket
    // masks bit 7 of every XMODEM byte, silently. But the built-in terminal is a
    // terminal with a HUMAN on the end -- the one case that comment names where these
    // transforms are facts about a terminal. So the console's chain lives here too, as
    // the `[terminal]` config section, applied by this stream at its own byte seams.
    //
    // It exists because an even-parity monitor (PS II) writes parity into bit 7: CR
    // 0x0D goes out as 0x8D, which is >= 0x20 and prints as a glyph instead of homing
    // the cursor, so Enter line-feeds without a carriage return. `strip7out` masks it.
    //
    // Global (one section, like `[console]`) and a live-read static, so
    // `SET terminal upper=on` reaches the running window. Inbound folds run in
    // keyAscii(), NOT read(): read() drains the emulator's reply FIFO, which also
    // carries the cursor reports (ESC[6n) the guest asked for, and folding those would
    // corrupt them.
    struct Settings {
        bool  upper     = false;  // fold typed keys to uppercase
        bool  strip7in  = false;  // mask bit 7 on keystrokes
        bool  strip7out = false;  // mask bit 7 on guest output (the even-parity fix)
        bool  echo      = false;  // local echo of keystrokes, for half-duplex guests
        bool  bell      = true;   // pass 0x07 to the emulator
        BsMap bsdel     = BsMap::Off;
        enum class Cr { Cr, CrLf } cr = Cr::Cr;  // pass CR through, or add LF after each CR
    };
    static Settings&             settings() { return s_settings; }
    static std::vector<Property> properties();

    // ---- composition root: the borrowed host video service and bundled font ----
    static void setDisplay(Display* d) { s_display = d; }
    static void setFont(const TerminalFont* f) { s_font = f; }

    // Is a `terminal:` usable in this build/run? True only when a real window is behind the
    // injected display -- resolveEndpoint refuses the endpoint otherwise.
    static bool hasWindow();

    // WHICH terminal line the host keyboard drives. Today there is one host window and one
    // keyboard (src/main.cpp), so a `terminal:` line claims it on construction -- last one
    // wins -- and releases it on destruction. The composition root's key sinks route
    // through here: keystrokes reach this line's emulator instead of the monitor Console.
    // Per-window routing for two live terminals is the deferred multi-window work (#244).
    static TerminalStream* keyTarget() { return s_keyTarget; }

    // ---- tests: read the grid back without a window ----
    const TerminalScreen& screen() const { return screen_; }
    TerminalScreen&       screen() { return screen_; }

private:
    std::string                       spec_;
    TerminalScreen                    screen_;
    std::unique_ptr<TerminalEmulator> emu_;
    TerminalRenderer                  renderer_;
    int                               videoWidth_ = 0;  // window width px, 0 = auto

    static Display*            s_display;
    static const TerminalFont* s_font;
    static TerminalStream*     s_keyTarget;
    static Settings            s_settings;
};

} // namespace altair
