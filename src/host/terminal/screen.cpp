#include "host/terminal/screen.h"

#include <cstring>

namespace altair {

TerminalScreen::TerminalScreen(int rows, int cols)
    : rows_(rows), cols_(cols),
      cells_((size_t)rows * (size_t)cols, 0x20),  // a fresh page is spaces
      attr_((size_t)rows * (size_t)cols, 0x00) {}

void TerminalScreen::putGlyph(uint8_t code) {
    cellRef(curRow_, curCol_) = code;
    attrRef(curRow_, curCol_) = curAttr_;
    dirty_ = true;
    if (++curCol_ >= cols_) newline();  // end of line -> LF + CR (scrolls at the bottom)
}

void TerminalScreen::lineFeed() {
    if (curRow_ < rows_ - 1) ++curRow_;
    else                     scrollUp();
    dirty_ = true;
}

void TerminalScreen::newline() {
    lineFeed();
    curCol_ = 0;
}

void TerminalScreen::carriageReturn() {
    curCol_ = 0;
    dirty_  = true;
}

void TerminalScreen::backspace() {
    if (curCol_ > 0) { --curCol_; dirty_ = true; }
}

void TerminalScreen::tab() {
    int n = (curCol_ & ~7) + 8;
    if (n < cols_) { curCol_ = n; dirty_ = true; }
}

void TerminalScreen::cursorUp() {
    if (curRow_ > 0) { --curRow_; dirty_ = true; }
}

void TerminalScreen::cursorForward() {
    if (++curCol_ >= cols_) newline();
    dirty_ = true;
}

void TerminalScreen::home() {
    curRow_ = 0;
    curCol_ = 0;
    dirty_  = true;
}

void TerminalScreen::scrollUp() {
    std::memmove(cells_.data(), cells_.data() + cols_, (size_t)(rows_ - 1) * cols_);
    std::memmove(attr_.data(),  attr_.data() + cols_,  (size_t)(rows_ - 1) * cols_);
    for (int c = 0; c < cols_; ++c) { cellRef(rows_ - 1, c) = 0x20; attrRef(rows_ - 1, c) = 0; }
    dirty_ = true;
}

void TerminalScreen::clearScreen() {
    for (auto& b : cells_) b = 0x20;
    for (auto& a : attr_) a = 0x00;
    curRow_ = curCol_ = 0;  // CLEAR homes the cursor
    dirty_  = true;
}

void TerminalScreen::eraseToEol() {
    for (int c = curCol_; c < cols_; ++c) { cellRef(curRow_, c) = 0x20; attrRef(curRow_, c) = 0; }
    dirty_ = true;
}

void TerminalScreen::eraseToEos() {
    eraseToEol();
    for (int r = curRow_ + 1; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) { cellRef(r, c) = 0x20; attrRef(r, c) = 0; }
    dirty_ = true;
}

// The other three quadrants of erase, which the VT100's ED/EL want (ANSI J/K with a
// parameter of 1 or 2). eraseAll leaves the cursor where it is -- ESC[2J clears the
// page without homing, unlike the SD terminal's clear-and-home clearScreen().
void TerminalScreen::eraseFromBol() {
    for (int c = 0; c <= curCol_ && c < cols_; ++c) {
        cellRef(curRow_, c) = 0x20;
        attrRef(curRow_, c) = 0;
    }
    dirty_ = true;
}

void TerminalScreen::eraseLine() {
    for (int c = 0; c < cols_; ++c) { cellRef(curRow_, c) = 0x20; attrRef(curRow_, c) = 0; }
    dirty_ = true;
}

void TerminalScreen::eraseFromTop() {
    for (int r = 0; r < curRow_; ++r)
        for (int c = 0; c < cols_; ++c) { cellRef(r, c) = 0x20; attrRef(r, c) = 0; }
    eraseFromBol();
    dirty_ = true;
}

void TerminalScreen::eraseAll() {
    for (auto& b : cells_) b = 0x20;
    for (auto& a : attr_) a = 0x00;
    dirty_ = true;  // the cursor is deliberately left where it is
}

void TerminalScreen::place(int row, int col) {
    curRow_ = row < 0 ? 0 : (row >= rows_ ? rows_ - 1 : row);
    curCol_ = col < 0 ? 0 : (col >= cols_ ? cols_ - 1 : col);
    dirty_  = true;
}

uint8_t TerminalScreen::charAt(int row, int col) const {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return 0x20;
    return (uint8_t)(cell(row, col) & 0x7F);
}

std::string TerminalScreen::screenText() const {
    std::string out;
    out.reserve((size_t)rows_ * (cols_ + 1));
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) out.push_back((char)(cell(r, c) & 0x7F));
        if (r != rows_ - 1) out.push_back('\n');
    }
    return out;
}

} // namespace altair
