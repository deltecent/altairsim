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
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace altair {

class SdlDisplay : public Display {
public:
    SdlDisplay() = default;
    ~SdlDisplay() override;

    SdlDisplay(const SdlDisplay&) = delete;
    SdlDisplay& operator=(const SdlDisplay&) = delete;

    Surface* acquire(int w, int h, PixelFormat fmt) override;
    void     present(Surface* s) override;
    void     setPalette(std::span<const Color> colors) override;
    void     pollEvents() override;
    void     setTitle(const std::string& name) override;
    void     setRunning(bool running) override;
    void     yieldFocus() override;

    // The close box, remembered by pollEvents() and handed to the run loop
    // (host/display.h). Consuming: while the guest RUNS the window itself stays open and
    // responsive -- closing it stops the GUEST and hands you the monitor, and the machine
    // is still there, so RUN resumes into the same window. The STOPPED-prompt consumer
    // (the monitor's idle hook) instead calls closeWindow() below and the window goes.
    bool takeQuitRequest() override {
        bool q = quit_;
        quit_ = false;
        return q;
    }

    void closeWindow() override;

private:
    bool ensureWindow(int w, int h);  // lazy: no SDL work until the first frame

    // Fit the logical presentation (and the bezel, and the aspect lock) to a frame of
    // w x h in the CURRENT window. Called once when the window is built, and again
    // whenever a board changes its frame resolution (a Dazzler switching video mode) --
    // without it the presentation stays frozen at the first frame's size and a larger
    // picture is clipped to the top-left (see ensureWindow / the header note on picW_).
    void applyPresentation(int w, int h);

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    int texW_ = 0, texH_ = 0;

    // The frame dimensions the logical presentation is currently fit to. The window is
    // built lazily on the first frame and ensureWindow() then short-circuits, so a
    // resolution change afterwards (the Dazzler's 32/64/128 modes) would otherwise never
    // re-fit the presentation. acquire() compares the incoming w,h against these and
    // calls applyPresentation() when they move; 0,0 until the first frame.
    int picW_ = 0, picH_ = 0;

    // The bezel, in LOGICAL pixels, folded into the logical presentation size on every side
    // (ensureWindow). present() draws the picture into a sub-rect inset by this much, and the
    // letterbox fills the rest black. Because it lives in logical space -- not device pixels --
    // it stays an even band on all four sides as the aspect-locked window is dragged. See
    // ensureWindow() for how it is chosen (about kBorder device pixels at the opening size).
    int border_ = 0;

    // The padded-frame aspect the window is currently locked to (logW/logH). Stored so a
    // resolution change re-locks only when the ratio actually moves -- for the Dazzler's
    // square modes it is always 1:1, and re-asserting an unchanged ratio would nudge a
    // window the operator has sized. 0 until the first applyPresentation().
    float aspect_ = 0.0f;

    std::unique_ptr<Surface> surface_;   // the board draws here (indexed)
    std::vector<Color>       palette_;   // index -> Color, set by the board
    std::vector<uint8_t>     rgba_;      // scratch: indexed -> RGBA for upload

    // The title bar. Held rather than pushed straight at SDL, because the run loop names
    // the machine long before a board draws the first frame and opens the window. title_
    // is the composed string currently on the window (cached so a redundant retitle is
    // skipped); machineName_ and running_ are the parts it is built from -- see
    // applyTitle(), which appends " -- simulator stopped" while the guest is halted.
    std::string title_ = "AltairSim";
    std::string machineName_;
    bool        running_ = false;
    void        applyTitle();

    bool inited_ = false;
    bool quit_   = false;
};

} // namespace altair
