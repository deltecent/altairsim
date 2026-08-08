#pragma once
//
// The bundled character generator for the generic built-in terminal (issue #244).
//
// The terminal engine (host/terminal/) draws against an abstract TerminalFont and never
// picks one -- the choice is a composition-root policy, injected with TerminalStream::
// setFont(), the same way the Display and the endpoint resolver are. This is that choice:
// the VDM-1's 128-glyph ASCII set (proctech-vdm1-font.h), a full period font with real
// lowercase and descenders, reused as the default terminal face.
//
// It lives in the BOARD layer on purpose. host/terminal/ must not reach up into boards/,
// so the adapter that bridges a board font to the terminal seam belongs here, where a
// board font is a peer -- and the composition root (main.cpp / tests/main.cpp), which
// already includes board headers, is where it is handed to the terminal.

#include "boards/proctech-vdm1-font.h"
#include "host/terminal/font.h"

#include <cstdint>

namespace altair {

class BundledTerminalFont : public TerminalFont {
public:
    int cellCols() const override { return vdm1font::kCols; }
    int cellRows() const override { return vdm1font::kRows; }
    uint8_t glyphRow(uint8_t code, int row) const override {
        return vdm1font::glyphRow(code, row);
    }
};

// One shared, stateless instance -- the terminal only reads it.
inline const TerminalFont& bundledTerminalFont() {
    static const BundledTerminalFont f;
    return f;
}

} // namespace altair
