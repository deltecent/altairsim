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
// ONE SURFACE PER OWNER, exactly as the SDL back end keeps one window per owner
// (issue #234): two video boards drawing into the same NullDisplay each get their
// own Surface, so a test can prove they no longer share one. It keeps each Surface
// it acquire()d (reusing it while the dimensions hold) so a test can read a board's
// last frame back after pumping it. present() and setPalette() record just enough
// to prove the board CALLED them -- present is a per-owner counter, the palette is
// retained per owner -- and do no host work.

#include "host/display.h"

#include <map>
#include <memory>

namespace altair {

class NullDisplay : public Display {
public:
    Surface* acquire(Owner owner, const std::string& /*label*/, int w, int h, PixelFormat fmt,
                     int /*targetWidthPx*/) override {
        Win& win = wins_[owner];
        if (!win.surface || win.surface->width() != w || win.surface->height() != h ||
            win.surface->format() != fmt) {
            win.surface = std::make_unique<Surface>(w, h, fmt);
        }
        last_ = owner;
        return win.surface.get();
    }

    void present(Owner owner, Surface*) override { ++wins_[owner].frames; last_ = owner; }

    void setPalette(Owner owner, std::span<const Color> colors) override {
        wins_[owner].palette.assign(colors.begin(), colors.end());
        last_ = owner;
    }

    // WALL TIME A TEST CAN SET. The base Display reads steady_clock, which is right
    // for a window and useless for an assertion -- a test that wanted to watch a
    // cursor blink would have to sleep half a second to see one phase. Here the
    // clock only moves when a test moves it, so a blink is exact and instant, and a
    // headless run is as deterministic as it was before boards had a wall clock at
    // all.
    double hostSeconds() override { return hostSeconds_; }
    void   setHostSeconds(double s) { hostSeconds_ = s; }
    void   advanceHostSeconds(double s) { hostSeconds_ += s; }

    // ---- For tests: look at what a board drew, without a window. ----
    //
    // The per-owner overloads are the honest ones now that a NullDisplay may serve
    // several boards; the zero-arg ones answer for the board that drew most recently,
    // which is exactly the sole board in a single-board test.
    const Surface* surface(Owner owner) const {
        auto it = wins_.find(owner);
        return it == wins_.end() ? nullptr : it->second.surface.get();
    }
    uint64_t frames(Owner owner) const {
        auto it = wins_.find(owner);
        return it == wins_.end() ? 0 : it->second.frames;
    }
    const std::vector<Color>& palette(Owner owner) const {
        static const std::vector<Color> empty;
        auto it = wins_.find(owner);
        return it == wins_.end() ? empty : it->second.palette;
    }

    const Surface*            surface() const { return surface(last_); }
    uint64_t                  frames() const { return frames(last_); }
    const std::vector<Color>& palette() const { return palette(last_); }

private:
    struct Win {
        std::unique_ptr<Surface> surface;
        std::vector<Color>       palette;
        uint64_t                 frames = 0;
    };
    std::map<Owner, Win> wins_;
    Owner  last_        = nullptr;  // the board that most recently drew (the zero-arg answer)
    double hostSeconds_ = 0.0;
};

} // namespace altair
