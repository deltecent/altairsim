#pragma once
//
// Adm3aEmulator -- the Lear Siegler ADM-3A dialect, driving a TerminalScreen (issue #244).
//
// The archetypal CP/M "dumb" terminal: no ANSI, no status reports, a handful of control
// codes and one cursor-addressing escape. It is the terminal WordStar, Turbo Pascal and a
// hundred other 8-bit programs were configured for out of the box, and the one whose arrow
// keys -- ^H ^J ^K ^L on the H/J/K/L keys -- became vi's motion keys. No modern host
// emulator speaks it, which is exactly why it belongs built in.
//
// WHAT IT UNDERSTANDS (this is the whole ADM-3A, not a subset):
//   ^G BEL  -- ignored (no audible bell here)
//   ^H BS   -- cursor left, stops at the left margin
//   ^J LF   -- cursor down, scroll at the bottom
//   ^K VT   -- cursor up
//   ^L FF   -- cursor right
//   ^M CR   -- carriage return
//   ^Z SUB  -- clear screen and home the cursor
//   ^^ RS   -- home the cursor (no clear)
//   ESC = r c -- load cursor: r and c are BIASED BY 0x20 (a space is row/col 0)
// Everything else printable lands at the cursor. There is no erase-to-EOL, no attribute,
// no report -- the ADM-3A had none, and inventing one would be a lie about the hardware.
//
// KEYS. The four arrows send ^K/^J/^H/^L (up/down/left/right) and Home sends ^^ -- the
// exact codes the ADM-3A keyboard produced, so a guest that reads them back sees its own
// terminal.

#include "host/terminal/emulator.h"

#include <cstdint>

namespace altair {

class TerminalScreen;

class Adm3aEmulator : public TerminalEmulator {
public:
    void feed(uint8_t b, TerminalScreen& scr) override;
    void reset() override;

    // The arrows and Home as the ADM-3A keyboard sent them: bare control codes, no ESC.
    void keySpecial(Key k) override;

private:
    // The cursor-load escape (ESC = row col) is the only multi-byte sequence.
    enum class State : uint8_t { Ground, Esc, LoadRow, LoadCol };
    State   state_ = State::Ground;
    uint8_t row_   = 0;  // the row byte, held while the col byte arrives
};

} // namespace altair
