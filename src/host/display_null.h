#pragma once
//
// NullDisplay -- a Display that draws into memory and shows nothing (DESIGN.md 7.4:
// "A headless build must still pass every acceptance test").
//
// This is the display half of NullStream. It is what a graphics board is given
// when there is no window: in a headless CI build (no SDL, ALTAIRSIM_ENABLE_SDL
// off), and in EVERY test, where tests/main.cpp injects it so a VDM-1 or Dazzler
// runs identically to the shipping binary and a test asserts on the pixels it
// paints -- with no window, no OS, and nothing to skip.
//
// It keeps the Surface it acquire()d (reusing it while the dimensions hold) so a
// test can read the last frame back after pumping the board. present() and
// setPalette() record just enough to prove the board CALLED them -- present is a
// counter, the palette is retained -- and do no host work.

#include "host/display.h"

#include <memory>

namespace altair {

class NullDisplay : public Display {
public:
    Surface* acquire(int w, int h, PixelFormat fmt) override {
        if (!surface_ || surface_->width() != w || surface_->height() != h ||
            surface_->format() != fmt) {
            surface_ = std::make_unique<Surface>(w, h, fmt);
        }
        return surface_.get();
    }

    void present(Surface*) override { ++frames_; }

    void setPalette(std::span<const Color> colors) override {
        palette_.assign(colors.begin(), colors.end());
    }

    // ---- For tests: look at what the board drew, without a window. ----
    const Surface* surface() const { return surface_.get(); }
    uint64_t frames() const { return frames_; }
    const std::vector<Color>& palette() const { return palette_; }

private:
    std::unique_ptr<Surface> surface_;
    std::vector<Color> palette_;
    uint64_t frames_ = 0;
};

} // namespace altair
