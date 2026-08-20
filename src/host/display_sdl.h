#pragma once
//
// SdlDisplay -- the SDL3-backed Display (DESIGN.md 7.4). Compiled ONLY when SDL3 is
// found (ALTAIRSIM_ENABLE_SDL); the headless build uses NullDisplay instead.
//
// THE ONLY FILE IN THE PROJECT THAT INCLUDES SDL. A board renders into a Surface
// (host/display.h) and knows nothing of this; the composition root (src/main.cpp)
// creates one of these, injects it, and the window appears. It converts the board's
// indexed frame to RGBA against the palette, uploads it to a streaming texture, and
// scales it nearest-neighbor with integer logical presentation so low-res pixels
// stay crisp on a modern panel.
//
// SINGLE-THREADED, MAIN-THREAD (DESIGN.md 7.4 #2). The emulation runs on the main
// thread; acquire()/present() are called from Board::pump() on that same thread, and
// pollEvents() from the run loop on the same one again, so SDL's window and event pump
// live on the main thread as macOS requires -- with no worker thread and no cross-thread
// queue. present() never blocks on vsync: emulated time, not the monitor's refresh, owns
// the clock. Input is drained by pollEvents(), not by present(), because a keystroke
// must not wait for a frame (host/display.h).

#include "host/display.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace altair {

// ONE WINDOW PER VIDEO BOARD (issue #234). A real Altair could drive one monitor off each
// video board, so a machine with two video boards is two pictures. This back end keeps one
// SDL window per drawing board, keyed by the board's Owner handle -- acquire()/present()/
// setPalette() route to the caller's own Window. SDL_Init and the operator-focus policy stay
// one-of (one process, one keyboard, host/display.h); everything a window owns is per-board.
class SdlDisplay : public Display {
public:
    SdlDisplay() = default;
    ~SdlDisplay() override;

    SdlDisplay(const SdlDisplay&) = delete;
    SdlDisplay& operator=(const SdlDisplay&) = delete;

    Surface* acquire(Owner owner, const std::string& label, int w, int h, PixelFormat fmt,
                     int targetWidthPx) override;
    void     present(Owner owner, Surface* s) override;
    void     setPalette(Owner owner, std::span<const Color> colors) override;
    bool     isWindowed() const override { return true; }  // there is a real window
    void     pollEvents() override;
    void     setTitle(const std::string& name) override;
    void     setRunning(bool running) override;
    void     yieldFocus() override;

    // The close box, remembered by pollEvents() and handed to the run loop
    // (host/display.h). Consuming: while the guest RUNS a window stays open and responsive --
    // closing it stops the GUEST and hands you the monitor, and the machine is still there, so
    // RUN resumes into the same windows. The STOPPED-prompt consumer (the monitor's idle hook)
    // instead calls closeWindow() below, and only the window whose close box was clicked goes.
    bool takeQuitRequest() override {
        bool q = quitRequested_;
        quitRequested_ = false;
        return q;
    }

    void closeWindow() override;      // close only the windows the operator clicked shut
    void closeAllWindows() override;  // close every window (CONFIG LOAD replaces the machine)

private:
    // Everything one board's window owns. Held by value in windows_ (a std::map, so the node
    // -- and thus the raw SDL pointers below -- never moves), keyed by the drawing board's
    // Owner handle. Non-copyable via the unique_ptr; never copied.
    struct Window {
        SDL_Window*   window   = nullptr;
        SDL_Renderer* renderer = nullptr;
        SDL_Texture*  texture  = nullptr;
        int texW = 0, texH = 0;

        // The frame dimensions the logical presentation is currently fit to. The window is
        // built lazily on the first frame and ensureWindow() then short-circuits, so a
        // resolution change afterwards (the Dazzler's 32/64/128 modes) would otherwise never
        // re-fit the presentation. acquire() compares the incoming w,h against these and
        // calls applyPresentation() when they move; 0,0 until the first frame.
        int picW = 0, picH = 0;

        // The bezel, in LOGICAL pixels, folded into the logical presentation on every side.
        // present() insets the picture by this much and the letterbox fills the rest black;
        // living in logical space it stays an even band on all four sides as the aspect-locked
        // window is dragged. See ensureWindow() for how it is chosen.
        int border = 0;

        // The padded-frame aspect the window is currently locked to (logW/logH). Stored so a
        // resolution change re-locks only when the ratio actually moves. 0 until first fit.
        float aspect = 0.0f;

        // Which look the current logical fit was built for -- Display::crt() at the last
        // applyPresentation(). crt is a session-wide static with no handle on a window, so
        // each window compares it against this and re-fits when the operator flips it.
        bool crtFit = false;

        std::unique_ptr<Surface> surface;   // the board draws here (indexed)
        std::vector<Color>       palette;   // index -> Color, set by the board
        std::vector<uint8_t>     rgba;      // scratch: indexed -> RGBA for upload

        // This board's own name (its id -- "vdm0"), so its window is titled apart from the
        // other video board's. Set by acquire() before the window is built; empty until then.
        std::string label;

        // The composed string currently on this window's title bar (cached so a redundant
        // retitle is not pushed at SDL). Built from the shared machineName_/running_ and this
        // window's label by applyTitle(), which appends " -- simulator stopped" while halted.
        std::string title = "AltairSim";

        // The operator clicked this window's close box while the machine was STOPPED, so the
        // monitor's idle hook should tear it down. closeWindow() acts on the flagged windows.
        bool pendingClose = false;
    };

    bool ensureWindow(Window& win, int w, int h, int targetWidthPx);  // lazy: no SDL until 1st frame

    // Fit the logical presentation (and the bezel, and the aspect lock) to a frame of
    // w x h in this window as it stands now. Called once when the window is built, and again
    // whenever a board changes its frame resolution (a Dazzler switching video mode) --
    // without it the presentation stays frozen at the first frame's size and a larger
    // picture is clipped to the top-left (see ensureWindow / the note on Window::picW).
    void applyPresentation(Window& win, int w, int h);

    // Re-fit (and resize) a window when the crt look has flipped since its last fit; returns
    // true if it did, so pollEvents() knows to repaint. See the definitions for why the crt
    // toggle resizes the window where a resolution change does not.
    bool refitForCrt(Window& win);

    // Paint a window's current texture -- bezel inset, the crt stretch, the scan lines. Split
    // out of present() so refitForCrt() can repaint the LAST frame with no new one in hand
    // (the board only calls present() when the guest's framebuffer changed).
    void drawLastFrame(Window& win);

    // Compose+cache this window's title from the shared machineName_/running_ and win.label.
    void applyTitle(Window& win);

    // "AltairSim -- <machine> -- <board>" plus " -- simulator stopped" while halted.
    std::string composedTitle(const Window& win) const;

    // Destroy a window's SDL resources (texture, renderer, window); leaves the struct reusable
    // (renderer null, so the next acquire re-opens it). SDL itself stays initialized.
    void destroyWindow(Window& win);

    // The window an event names, by SDL_WindowID (0 if none matches -- e.g. an app-level QUIT).
    Window* windowById(uint32_t id);

    std::map<Owner, Window> windows_;

    // The title parts, shared by every window (all belong to the one machine). The run loop
    // names the machine and its run/stop state long before a board opens a window, so these
    // are recorded here and each Window picks them up via applyTitle().
    std::string machineName_;
    bool        running_ = false;

    bool inited_        = false;  // SDL_Init done -- once per PROCESS, not per window
    bool quitRequested_ = false;  // operator asked to stop the guest; see takeQuitRequest()

    // Where the next window opens, so a machine's second video board does not land on top of
    // the first (issue #234). Tiled left-to-right across the usable desktop, wrapping to a new
    // row; desktop coordinates, advanced by ensureWindow() as each window opens.
    bool tilePlaced_ = false;
    int  tileX_ = 0, tileY_ = 0, tileRowH_ = 0;
};

} // namespace altair
