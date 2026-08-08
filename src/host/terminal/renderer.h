#pragma once
//
// TerminalRenderer -- paints a TerminalScreen into a Display (DESIGN.md 7.4).
//
// The emulation-independent painter shared by every built-in terminal. It walks the
// grid, rasterizes each cell from an injected TerminalFont, applies the per-cell
// attributes (reverse, blink, half intensity) and the whole-screen polarity, and draws
// a block cursor -- then hands the Surface to the Display. It owns the frame economy
// the VDM-1 and VDB share: frameChanged() answers whether a repaint could differ, so
// the host walks a hundred thousand pixels only when the picture actually moved.
//
// It was lifted verbatim from the VDB-8024 board's render(); the palette (green
// phosphor) is the default and the caller may set its own.

#include "host/display.h"  // Color, Surface, Display

#include <cstdint>

namespace altair {

class TerminalFont;
class TerminalScreen;

class TerminalRenderer {
public:
    // The Indexed8 palette: index 0 = background, 1 = full-intensity foreground,
    // 2 = half (dim) foreground. Defaults to a green phosphor terminal.
    TerminalRenderer() = default;

    void setFont(const TerminalFont* f) { font_ = f; }
    void setPalette(Color bg, Color fg, Color dim) { bg_ = bg; fg_ = fg; dim_ = dim; }

    // Whole-screen video polarity (light-on-dark vs its inverse) and the cursor style
    // (0 = off, 1 = blink, 2 = steady) -- the board's switches, passed through.
    void setReverse(bool on) { reverse_ = on; }
    void setCursorMode(uint8_t m) { cursorMode_ = m; }

    // Would a repaint now differ from the last one? True if the screen is dirty, or a
    // blinking cursor / blinking cell has flipped on its own ~1 Hz oscillator (wall
    // time from the Display -- the one clock a terminal's blink may use, DESIGN.md 7.5).
    bool frameChanged(const TerminalScreen& scr, Display* d) const;

    // Paint the frame and present it. Clears the screen's dirty flag. `windowWidth` is
    // the board's window-width choice (0 = auto), handed to the Display before drawing.
    void render(Display& d, TerminalScreen& scr, int windowWidth);

private:
    bool blinkOn(Display* d) const;

    const TerminalFont* font_ = nullptr;

    Color bg_  = {0x00, 0x00, 0x00, 0xFF};  // black
    Color fg_  = {0x33, 0xFF, 0x66, 0xFF};  // full green
    Color dim_ = {0x1A, 0x80, 0x33, 0xFF};  // half green

    bool    reverse_    = false;
    uint8_t cursorMode_ = 1;  // blink, the usual terminal default

    // Render change-detection state (same shape as the VDM-1's).
    bool lastBlinkOn_  = true;
    bool hasBlinkCell_ = false;
};

} // namespace altair
