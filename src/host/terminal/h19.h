#pragma once
//
// H19Emulator -- the Heath/Zenith H19 (and the H89's built-in terminal), driving a
// TerminalScreen (issue #244).
//
// The H19 is a VT52 SUPERSET in its native "Heath" mode, with extra erase, reverse-video
// and save-cursor escapes -- and it also has an ANSI mode (ESC <) in which it behaves like
// a VT100. So this emulator IS a VT52-family parser for Heath mode, and it DELEGATES to a
// Vt100Emulator once the guest sends ESC <; the guest returns to Heath mode with the
// VT100's own DECANM reset (ESC[?2l), which the inner VT100 records and we poll.
//
// HEATH MODE (the default):
//   C0: BS, HT, LF, CR
//   ESC A/B/C/D -- cursor up/down/right/left    ESC H -- home    ESC I -- reverse index
//   ESC J -- erase to end of screen             ESC K -- erase to end of line
//   ESC b -- erase to start of screen           ESC o -- erase to start of line
//   ESC l -- erase entire line                  ESC E -- clear display and home
//   ESC Y r c -- direct cursor address (r, c biased by 0x20)
//   ESC n -- report cursor position -> ESC Y r c (same bias)
//   ESC j -- save cursor position               ESC k -- restore cursor position
//   ESC p -- enter reverse video                ESC q -- exit reverse video
//   ESC F / ESC G -- enter/exit graphics charset (accepted, ignored: one font)
//   ESC = / ESC > -- enter/exit keypad-shifted mode (accepted, no visible effect)
//   ESC z -- reset to power-up configuration     ESC < -- enter ANSI (VT100) mode
// Insert/delete line and character (ESC L/M/N, ESC @/O) are NOT modeled -- the shared
// TerminalScreen has no insert/delete primitive; they are abandoned cleanly, as the VT100
// subset abandons what it does not implement.
//
// KEYS. In Heath mode the arrows send ESC A/B/C/D and Home ESC H (the VT52 codes); in ANSI
// mode the inner VT100 encodes them (ESC[A, or ESC O A under DECCKM).

#include "host/terminal/emulator.h"
#include "host/terminal/vt100.h"

#include <cstdint>

namespace altair {

class TerminalScreen;

class H19Emulator : public TerminalEmulator {
public:
    void feed(uint8_t b, TerminalScreen& scr) override;
    void reset() override;

    void keyAscii(uint8_t b) override;
    void keySpecial(Key k) override;

private:
    void esc(uint8_t b, TerminalScreen& scr);  // the byte after ESC, in Heath mode
    void drainInner();                         // move the inner VT100's replies into ours
    void reverseVideo(TerminalScreen& scr, bool on);

    // ESC Y row col is the one multi-byte Heath escape.
    enum class State : uint8_t { Ground, Esc, AddrRow, AddrCol };
    State   state_ = State::Ground;
    uint8_t row_   = 0;  // held between the row and col bytes of ESC Y

    int  savedRow_ = 0, savedCol_ = 0;  // ESC j / ESC k
    bool saved_    = false;

    // ANSI mode: once ESC < is seen, feed()/keys route to this VT100 until it sees ESC[?2l.
    bool           ansiMode_ = false;
    Vt100Emulator  vt100_;
};

} // namespace altair
