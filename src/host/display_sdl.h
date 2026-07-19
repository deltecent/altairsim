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

    // The close box, remembered by pollEvents() and handed to the run loop
    // (host/display.h). Consuming: the window itself stays open and responsive --
    // closing it stops the GUEST and hands you the monitor, and the machine is still
    // there, so RUN resumes into the same window.
    bool takeQuitRequest() override {
        bool q = quit_;
        quit_ = false;
        return q;
    }

private:
    bool ensureWindow(int w, int h);  // lazy: no SDL work until the first frame

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    int texW_ = 0, texH_ = 0;

    std::unique_ptr<Surface> surface_;   // the board draws here (indexed)
    std::vector<Color>       palette_;   // index -> Color, set by the board
    std::vector<uint8_t>     rgba_;      // scratch: indexed -> RGBA for upload
    bool inited_ = false;
    bool quit_   = false;
};

} // namespace altair
