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
// EACH WINDOW TYPES ON ITS OWN LINE. The window is keyed by the stream's `this` (the
// Display Owner), so a key typed in it carries that owner, and routeKeys() sends it to this
// line's emulator: an MP/M machine with three terminal windows has three keyboards, and the
// one you click on is the one you type on. A key from any other window (a VDM-1, a Sol)
// is not a terminal's, and the composition root gives it to the Console (src/main.cpp).

#include "host/filter.h"  // BsMap -- the terminal reuses the console's rubout mapping
#include "host/stream.h"
#include "host/terminal/renderer.h"
#include "host/terminal/screen.h"

#include <cstdint>
#include <memory>
#include <set>
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

    std::string doDescribe() const override { return spec_; }

    // The name of the unit this terminal is on (Machine::pump, issue #940). It names the
    // window in the host title bar and, with `[terminal] titlebar`, on a bar at the top.
    void setTitle(const std::string& title) override;
    const std::string& title() const { return title_; }

    size_t read(uint8_t* buf, size_t n) override;   // guest <- reports + keystrokes
    size_t doWrite(const uint8_t* buf, size_t n) override;  // guest -> screen
    bool   readable() const override;
    bool   writable() const override { return true; }  // the terminal never stalls the guest
    size_t rxPending() const override;  // keys and reports the guest has not read

    void pump() override;  // paint the frame, once per slice, on the main thread

    // Host keyboard -> this line (what routeKeys/routeSpecial drive, and what a test
    // types with). Encoded by the emulator and enqueued toward the guest.
    void keyAscii(uint8_t b);
    void keySpecial(int key);  // a TerminalEmulator::Key value

    // Opening window width in device pixels, handed to the Display's acquire() (0 = auto,
    // ~half the screen). The `width=` connect-string option sets it, mirroring the video
    // boards' width property; a larger value opens a larger terminal window.
    void setWindowWidth(int px) { videoWidth_ = px; }

    // The phosphor colour of the tube. Green is the default (the renderer's built-in
    // palette); amber is the other common period look. Set by the `phosphor=` connect
    // option; applies the toned-down palette to this terminal's renderer.
    enum class Phosphor { Green, Amber };
    void setPhosphor(Phosphor p);

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
        bool  crlf      = false;  // add LF after each CR the guest prints
        bool  titlebar  = true;   // the unit's title on a bar at the top of the window
    };
    static Settings&             settings() { return s_settings; }
    static std::vector<Property> properties();
    // SET TERMINAL and a [terminal] table both come through here, so the two cannot
    // disagree about a key. It refuses the old `cr` key (issue #768) with the new line.
    static bool set(const std::string& key, const std::string& value, std::string& err);

    // ---- composition root: the borrowed host video service and bundled font ----
    static void setDisplay(Display* d) { s_display = d; }
    static Display* display() { return s_display; }
    static void setFont(const TerminalFont* f) { s_font = f; }
    static const TerminalFont* font() { return s_font; }

    // Is a `terminal:` usable in this build/run? True only when a real window is behind the
    // injected display -- resolveEndpoint refuses the endpoint otherwise.
    static bool hasWindow();

    // THE TERMINAL BEHIND A WINDOW, or null when the window is not a live terminal's (a
    // video board's, or one whose terminal has gone). The owner is only compared, never
    // dereferenced, until it is found among the live terminals.
    static TerminalStream* forOwner(const void* owner);

    // Keys typed in the window of `owner`. True when that window is a terminal's: the keys
    // went down its line. False: not a terminal window, and the caller gives the keys to
    // the Console as before. `key` in routeSpecial is a TerminalEmulator::Key value.
    static bool routeKeys(const void* owner, const uint8_t* p, size_t n);
    static bool routeSpecial(const void* owner, int key);

    // SELECT AND COPY (Display::Pointer, Display::emitCopy). The left button down starts a
    // selection at a cell, a drag extends it, up ends it; the selected cells draw reversed.
    // Copy puts the selected text on the host clipboard -- the whole screen when nothing is
    // selected. A key typed in the window clears the selection. `kind` is
    // Display::Pointer::Kind as an int (0 down, 1 drag, 2 up); x, y are picture pixels.
    void        pointer(int kind, int x, int y);
    bool        hasSelection() const { return selOn_; }
    std::string copyText() const;
    static bool routePointer(const void* owner, int kind, int x, int y);
    static bool routeCopy(const void* owner);

    // ---- tests: read the grid back without a window ----
    const TerminalScreen& screen() const { return screen_; }
    TerminalScreen&       screen() { return screen_; }

private:
    std::string                       spec_;
    std::string                       title_;               // the unit's name (setTitle)
    bool                              titleDirty_ = false;  // repaint for a new title
    bool                              barShown_   = false;  // titlebar at the last paint
    TerminalScreen                    screen_;
    std::unique_ptr<TerminalEmulator> emu_;
    TerminalRenderer                  renderer_;
    int                               videoWidth_ = 0;  // window width px, 0 = auto
    bool selOn_ = false;     // a selection is shown (more than a click)
    bool dragging_ = false;  // the left button is down in this window
    int  selAR_ = 0, selAC_ = 0, selHR_ = 0, selHC_ = 0;  // anchor and head cells
    void clearSelection();
    void cellAt(int x, int y, int& row, int& col) const;

    static Display*                  s_display;
    static const TerminalFont*       s_font;
    static std::set<TerminalStream*> s_live;  // every terminal now open: forOwner()
    static Settings                  s_settings;
};

} // namespace altair
