#pragma once
//
// TerminalFont -- the character generator a TerminalRenderer rasterizes from.
//
// A terminal's glyphs are the one board-specific thing a generic renderer cannot
// assume: the VDB-8024 carries an authentic SD Systems CGEN PROM (7x10 with a
// descender flag), a VT100 has its own ROM, a plain build ships a bundled font. So
// the renderer draws against this interface and each terminal supplies the metal.
//
// glyphRow() returns one scan line of a glyph with bit 15 = the LEFTMOST dot; a set bit
// paints the foreground. The value is 16 bits so a cell may be wider than 8 dots -- the
// authentic VT220 face is 10 dots wide. An 8-dot font (the VDM-1, the VDB) MSB-aligns its
// byte into the top of the word, so bit 15 stays the leftmost dot for every font.

#include <cstdint>

namespace altair {

class TerminalFont {
public:
    virtual ~TerminalFont() = default;

    virtual int cellCols() const = 0;  // dot columns per cell (the glyph's box width)
    virtual int cellRows() const = 0;  // scan lines per cell (the glyph's box height)

    // One scan line (0..cellRows()-1) of glyph `code`, bit 15 = leftmost dot.
    virtual uint16_t glyphRow(uint8_t code, int row) const = 0;
};

} // namespace altair
