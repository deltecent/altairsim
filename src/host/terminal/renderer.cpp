#include "host/terminal/renderer.h"

#include "host/terminal/font.h"
#include "host/terminal/screen.h"

namespace altair {
namespace {

// THE CURSOR BLINK IS NOT ON THE CPU'S CRYSTAL -- it is the terminal's own oscillator,
// asynchronous to the host and unreadable from the S-100 side, exactly like the VDM-1's.
// So it is measured in SECONDS OF WALL TIME, taken from the Display, which is the one
// duration on this card that may be (DESIGN.md 7.5).
constexpr double kBlinkHalfPeriod = 0.5;  // seconds -- ~1 Hz blink

} // namespace

bool TerminalRenderer::blinkOn(Display* d) const {
    if (!d) return true;
    return ((uint64_t)(d->hostSeconds() / kBlinkHalfPeriod) & 1) == 0;
}

bool TerminalRenderer::frameChanged(const TerminalScreen& scr, Display* d) const {
    if (scr.dirty()) return true;
    // A blinking cursor (present unless off) or any blinking cell repaints on its own clock.
    if (d && (cursorMode_ == 1 || hasBlinkCell_)) {
        if (blinkOn(d) != lastBlinkOn_) return true;
    }
    return false;
}

void TerminalRenderer::render(Display& d, TerminalScreen& scr, int windowWidth) {
    if (!font_) return;
    const int cw = font_->cellCols(), ch = font_->cellRows();
    const int rows = scr.rows(), cols = scr.cols();

    d.setWindowWidth(windowWidth);  // this board's window-width choice
    Surface* s = d.acquire(cols * cw, rows * ch, PixelFormat::Indexed8);
    if (!s) return;

    const Color pal[3] = {bg_, fg_, dim_};
    d.setPalette(pal);
    s->clear(0);

    const bool lit = blinkOn(&d);
    const bool cursorShown = cursorMode_ == 2 || (cursorMode_ == 1 && lit);
    const int curRow = scr.cursorRow(), curCol = scr.cursorCol();
    bool sawBlink = false;

    for (int dr = 0; dr < rows; ++dr) {
        for (int c = 0; c < cols; ++c) {
            uint8_t code = (uint8_t)(scr.cell(dr, c) & 0x7F);
            uint8_t a    = scr.attr(dr, c);
            bool blink   = (a & TerminalScreen::kAttrBlink) != 0;
            if (blink) sawBlink = true;

            // Whole-screen polarity (a switch) XORs the per-cell reverse; the cursor
            // then inverts its own cell on top of that (a block cursor).
            bool reverse = ((a & TerminalScreen::kAttrReverse) != 0) ^ reverse_;
            bool isCursor = (dr == curRow && c == curCol && cursorShown);
            if (isCursor) reverse = !reverse;

            bool hideGlyph = blink && !lit;                                 // dark half-cycle
            uint8_t fgColor = (a & TerminalScreen::kAttrHalf) ? 2 : 1;      // dim vs full
            uint8_t bgIdx = reverse ? fgColor : 0;
            uint8_t fgIdx = reverse ? 0 : fgColor;

            const int px = c * cw, py = dr * ch;
            if (bgIdx != 0)
                for (int y = 0; y < ch; ++y)
                    for (int x = 0; x < cw; ++x) s->put(px + x, py + y, bgIdx);
            if (!hideGlyph)
                for (int ry = 0; ry < ch; ++ry) {
                    uint8_t bits = font_->glyphRow(code, ry);  // bit 7 = leftmost
                    for (int rx = 0; rx < cw; ++rx)
                        if (bits & (0x80 >> rx)) s->put(px + rx, py + ry, fgIdx);
                }
        }
    }

    scr.clearDirty();
    lastBlinkOn_  = lit;
    hasBlinkCell_ = sawBlink;
    d.present(s);
}

} // namespace altair
