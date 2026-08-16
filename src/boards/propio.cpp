#include "boards/propio.h"

namespace altair {

// reference/Console IO Board.md: the board's monitor-default ports are 00H (status) / 01H
// (data); the worked example (its shipped SD Systems 8024 monitor) reads keyboard-ready in
// status bit 1 and output-ready in status bit 2, both active high (`AND 02H` / `AND 04H`, and
// a 0 in the TX bit means the display is busy). The real board jumpers all of this (P74-P77,
// SW2/SW3), so these are PRESETS the operator can still override -- not fixed silicon.
UsioProfile propioProfile() {
    return UsioProfile{/*status*/ 0x00, /*data*/ 0x01,
                       /*rdr*/ 1, /*tdre*/ 2,
                       /*rdrActiveLow*/ false, /*tdreActiveLow*/ false};
}

PropIoBoard::PropIoBoard() { applyProfile(propioProfile()); }

} // namespace altair
