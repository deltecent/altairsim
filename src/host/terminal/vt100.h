#pragma once
//
// Vt100Emulator -- the VT100 / ANSI dialect, driving a TerminalScreen (issue #244).
//
// The one every modern host emulator already speaks, so a `terminal:` line set to it
// works with anything that talks ANSI -- but here the SIMULATOR owns it, so it is the
// SAME VT100 on every platform and it answers the period reports (ESC[6n) that an
// external xterm answers with its own geometry, not the guest's.
//
// WHAT IT UNDERSTANDS. The C0 controls (BS, HT, LF/VT/FF, CR); the CSI cursor moves
// (CUU/CUD/CUF/CUB A/B/C/D, CUP H and HVP f); erase (ED J, EL K, all three modes);
// SGR m (reset, reverse, blink, faint -- the attributes the grid carries); the cursor
// report DSR n (ESC[6n -> ESC[r;cR, and ESC[5n -> ESC[0n); save/restore cursor (ESC 7 /
// ESC 8 and CSI s / u); index/reverse-index/next-line (ESC D / M / E); RIS (ESC c); and
// DECCKM (ESC[?1h/l), which flips the arrow keys between ESC[A and ESC O A. Charset
// designators (ESC ( x) are consumed and ignored -- we render one font. Unknown escapes
// are abandoned cleanly rather than leaking their bytes onto the screen.
//
// This is deliberately a WORKING SUBSET, not a conformant VT100: no scroll region, no
// double-width lines, no smooth scroll, no 132-column switch by escape. It is what a CP/M
// full-screen editor (VEDIT, WordStar in ANSI mode, TURBO) actually drives.

#include "host/terminal/emulator.h"

#include <cstdint>
#include <vector>

namespace altair {

class TerminalScreen;

class Vt100Emulator : public TerminalEmulator {
public:
    void feed(uint8_t b, TerminalScreen& scr) override;
    void reset() override;

    // The arrows and Home, encoded the VT100 way -- ESC[A in normal cursor mode, ESC O A
    // when the guest has set DECCKM (application cursor keys). See keySpecial's body.
    void keySpecial(Key k) override;

    // DECANM (ESC[?2h/l): true in ANSI mode (the default), false once the guest selects
    // VT52 mode. A standalone VT100 has no VT52 parser, so it just records the request;
    // the H19 emulator, which drives a VT100 in its ANSI mode, polls this to fall back to
    // its native Heath (VT52) parser.
    bool ansiMode() const { return ansi_; }

private:
    void c0(uint8_t b, TerminalScreen& scr);       // a C0 control (b < 0x20)
    void esc(uint8_t b, TerminalScreen& scr);      // the byte after ESC
    void csi(uint8_t b, TerminalScreen& scr);      // a byte inside a CSI sequence
    void dispatchCsi(uint8_t final, TerminalScreen& scr);
    void sgr(TerminalScreen& scr);                 // ESC[ ... m -- select graphic rendition

    void saveCursor(const TerminalScreen& scr);
    void restoreCursor(TerminalScreen& scr);
    void ris(TerminalScreen& scr);                 // ESC c -- reset to initial state

    // A CSI parameter with a default: an absent or zero value means `def` for the moves
    // and CUP (ESC[A is up one, ESC[H is home). SGR and the erases read params_ directly,
    // because there a zero is a real selector (ESC[0m, ESC[0J), not "use the default".
    int arg(size_t i, int def) const {
        return (i < params_.size() && params_[i] != 0) ? params_[i] : def;
    }
    int argRaw(size_t i) const { return i < params_.size() ? params_[i] : 0; }

    enum class State : uint8_t { Ground, Esc, Csi, EscInter };
    State state_ = State::Ground;

    // CSI accumulation.
    std::vector<int> params_;
    int              cur_        = 0;      // the digits seen so far for the current param
    bool             haveDigits_ = false;  // ...any at all (to tell ESC[H from ESC[0H)
    bool             priv_       = false;  // a '?' lead-in (DECCKM et al.)

    // Saved cursor (ESC 7 / CSI s).
    int     savedRow_  = 0, savedCol_ = 0;
    uint8_t savedAttr_ = 0;
    bool    saved_     = false;

    // DECCKM: application cursor keys. Off -> arrows are ESC[A; on -> ESC O A (vi, less).
    bool appCursor_ = false;

    // DECANM: ANSI vs VT52 mode. True (ANSI) unless the guest sends ESC[?2l.
    bool ansi_ = true;
};

} // namespace altair
