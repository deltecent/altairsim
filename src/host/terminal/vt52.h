#pragma once
//
// Vt52Emulator -- the DEC VT52 dialect, driving a TerminalScreen (issue #244).
//
// The VT100's small predecessor and the terminal a lot of period software (and CP/M-68K,
// and the Atari ST later) targeted: no CSI, no parameters -- just ESC followed by a single
// letter, and one cursor-address escape (ESC Y row col) biased by 0x20 the same way the
// ADM-3A biases ESC =. Where the VT100 answers ESC[6n, the VT52 answers ESC Z with its
// identity (ESC / K).
//
// WHAT IT UNDERSTANDS:
//   C0: BS, HT, LF, CR (as everywhere)
//   ESC A  -- cursor up            ESC B  -- cursor down
//   ESC C  -- cursor right         ESC D  -- cursor left
//   ESC H  -- cursor home          ESC I  -- reverse line feed
//   ESC J  -- erase to end of screen   ESC K -- erase to end of line
//   ESC Y r c -- direct cursor address (r, c biased by 0x20)
//   ESC Z  -- identify -> replies ESC / K  (a VT52 without copier)
//   ESC =  -- alternate keypad mode    ESC > -- exit alternate keypad (both accepted, no-op)
//   ESC F / ESC G -- enter/exit graphics charset (accepted, ignored; we render one font)
// There is no attribute (SGR) -- the VT52 had none -- so nothing rides the attribute plane.
//
// KEYS. The four arrows send ESC A/B/C/D and Home sends ESC H -- the VT52's own codes, the
// direct ancestor of the VT100's ESC[A that a guest reads straight back.

#include "host/terminal/emulator.h"

#include <cstdint>

namespace altair {

class TerminalScreen;

class Vt52Emulator : public TerminalEmulator {
public:
    void feed(uint8_t b, TerminalScreen& scr) override;
    void reset() override;

    // The arrows and Home as ESC-letter codes -- ESC A/B/C/D and ESC H.
    void keySpecial(Key k) override;

private:
    void esc(uint8_t b, TerminalScreen& scr);  // the byte after ESC

    // ESC Y row col is the one multi-byte escape.
    enum class State : uint8_t { Ground, Esc, AddrRow, AddrCol };
    State   state_ = State::Ground;
    uint8_t row_   = 0;  // held between the row and col bytes of ESC Y
};

} // namespace altair
