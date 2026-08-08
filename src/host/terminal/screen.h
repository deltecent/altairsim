#pragma once
//
// TerminalScreen -- an attributed character grid with a cursor and the primitive
// operations every glass terminal performs on it (DESIGN.md 7.4).
//
// This is the emulation-INDEPENDENT half of a terminal. It knows nothing about VT100
// escape sequences or the SD Systems control codes -- it only knows how to put a glyph
// at the cursor, move the cursor, feed a line, scroll, and erase. A TerminalEmulator
// (host/terminal/emulator.h) turns a byte stream in one dialect into calls on this;
// a TerminalRenderer (host/terminal/renderer.h) paints it into a Display.
//
// It was lifted verbatim from the SD Systems VDB-8024 board (src/boards/sd-vdb8024.cpp),
// which was already a built-in terminal; the semantics below (wrap via newline, clear
// homes the cursor, a scroll blanks the new bottom row) are that board's, unchanged.
//
// EVERY MUTATION SETS dirty_. That is the render economy the VDM-1 and VDB share: the
// host repaints only when the picture could have changed. dirty() is consumed by the
// renderer; markDirty()/clearDirty() bracket a frame.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace altair {

class TerminalScreen {
public:
    // Per-cell attribute bits, carried in a parallel plane. A terminal that has no
    // notion of one (a bare TTY) simply never sets it.
    static constexpr uint8_t kAttrReverse = 0x01;
    static constexpr uint8_t kAttrBlink   = 0x02;
    static constexpr uint8_t kAttrHalf    = 0x04;  // half (dim) intensity

    TerminalScreen(int rows, int cols);

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // ---- the cursor ----
    int     cursorRow() const { return curRow_; }
    int     cursorCol() const { return curCol_; }
    uint8_t currentAttr() const { return curAttr_; }
    void    setCurrentAttr(uint8_t a) { curAttr_ = a; }

    // ---- primitive operations (the terminal's verbs) ----
    void putGlyph(uint8_t code);   // a printable char at the cursor, in curAttr_; advance
    void lineFeed();               // cursor down; scroll at the bottom
    void newline();                // line feed + carriage return
    void carriageReturn();         // cursor to column 0
    void backspace();              // non-destructive cursor left, stops at the left margin
    void tab();                    // to the next multiple of 8, not past the last column
    void cursorUp();               // cursor up, stops at the top
    void cursorForward();          // cursor right, wrapping to the next line
    void home();                   // cursor to (0,0)
    void scrollUp();               // shift rows up one, blank the new bottom row
    void clearScreen();            // blank the page, cursor home (does NOT touch curAttr_)
    void eraseToEol();             // blank cursor..end of line
    void eraseToEos();             // blank cursor..end of screen
    void place(int row, int col);  // move the cursor, clamped to the page

    // ---- reads ----
    uint8_t cell(int r, int c) const { return cells_[idx(r, c)]; }  // raw (bit 7 = attr flag)
    uint8_t attr(int r, int c) const { return attr_[idx(r, c)]; }
    uint8_t charAt(int row, int col) const;    // masked glyph, bounds-safe (off-grid -> space)
    std::string screenText() const;            // rows joined by '\n'

    // ---- render economy ----
    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }

    // ---- snapshot support: raw planes for StateWriter::raw (see the board) ----
    uint8_t*       cellData() { return cells_.data(); }
    const uint8_t* cellData() const { return cells_.data(); }
    uint8_t*       attrData() { return attr_.data(); }
    const uint8_t* attrData() const { return attr_.data(); }
    size_t         planeBytes() const { return cells_.size(); }
    void           setCursor(int row, int col) { curRow_ = row; curCol_ = col; }

private:
    size_t idx(int r, int c) const { return (size_t)r * (size_t)cols_ + (size_t)c; }
    uint8_t& cellRef(int r, int c) { return cells_[idx(r, c)]; }
    uint8_t& attrRef(int r, int c) { return attr_[idx(r, c)]; }

    int rows_, cols_;
    std::vector<uint8_t> cells_;  // a glyph code per cell (bit 7 = attribute flag)
    std::vector<uint8_t> attr_;   // a parallel attribute plane (kAttr* bits)

    int     curRow_ = 0, curCol_ = 0;
    uint8_t curAttr_ = 0;  // enhancement applied to chars written now (kAttr* bits)

    bool dirty_ = true;  // a fresh screen owes the host one frame
};

} // namespace altair
