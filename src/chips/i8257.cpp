#include "chips/i8257.h"

#include "core/statefile.h"

namespace altair {

// ---------------------------------------------------------------------------
// THE PORTS. off is A3..A0: 2n / 2n+1 are channel n's address / count registers,
// 8 is the Mode Set (write) / Status (read) register. The sixteen-bit registers are
// read and written one byte at a time, low then high, through the single first/last
// flip-flop -- so each access to one of them TOGGLES flipHigh_, and a write to the
// Mode Set register RESETS it (which is what the BIOS's `XRA A ; OUT CMND` is for).
// ---------------------------------------------------------------------------
void I8257::writePort(uint8_t off, uint8_t v) {
    if (off == 8) {
        mode_    = v;
        flipHigh_ = false;  // "next byte is the low byte" -- the flip-flop is reset here
        return;
    }
    if (off >= 8) return;  // only registers 0..8 exist on the 8257

    const int  n       = off >> 1;
    const bool isCount  = (off & 1) != 0;
    uint16_t&  reg      = isCount ? ch_[n].count : ch_[n].addr;
    if (!flipHigh_) reg = (uint16_t)((reg & 0xFF00) | v);
    else            reg = (uint16_t)((reg & 0x00FF) | ((uint16_t)v << 8));
    flipHigh_ = !flipHigh_;
}

uint8_t I8257::readPort(uint8_t off) {
    if (off == 8) {
        // Reading the Status register clears its terminal-count bits (real-chip behavior).
        const uint8_t s = status_;
        status_ &= (uint8_t)~0x0Fu;
        return s;
    }
    if (off >= 8) return 0xFF;

    const int      n      = off >> 1;
    const bool     isCount = (off & 1) != 0;
    const uint16_t reg     = isCount ? ch_[n].count : ch_[n].addr;
    const uint8_t  v       = !flipHigh_ ? (uint8_t)(reg & 0xFF) : (uint8_t)(reg >> 8);
    flipHigh_ = !flipHigh_;
    return v;
}

// ---------------------------------------------------------------------------
// THE TRANSFER BOOKKEEPING the owning card drives.
//
// FIXED PRIORITY, channel 0 highest -- the 8257's power-up default and what the Tarbell
// BIOS uses (it drives channel 0 only). Rotating priority (Mode Set bit 4) is not
// modeled because nothing here selects it; a card that needs it starts at this hook.
// ---------------------------------------------------------------------------
int I8257::activeChannel() const {
    for (int n = 0; n < 4; ++n)
        if (mode_ & (1u << n)) return n;
    return -1;
}

uint16_t I8257::curAddr() const {
    const int n = activeChannel();
    return n < 0 ? 0 : ch_[n].addr;
}

bool I8257::writeToMemory() const {
    const int n = activeChannel();
    if (n < 0) return false;
    return ((ch_[n].count >> 14) & 3u) == 1u;  // 01 = write into memory (a disk read)
}

void I8257::advance() {
    lastTc_ = false;
    const int n = activeChannel();
    if (n < 0) return;

    Channel&       c    = ch_[n];
    const uint16_t mode = c.count & 0xC000;      // the R/W mode bits ride along untouched
    const uint16_t rem  = c.count & 0x3FFF;      // the 14-bit down-counter

    c.addr = (uint16_t)(c.addr + 1);
    if (rem == 0) {
        // Terminal count: the counter underflows and wraps (real chip), the TC status bit
        // latches, and under TC-STOP the channel disables itself -- which is how the burst
        // ends (channelEnabled() goes false and the card drops pHOLD).
        lastTc_ = true;
        status_ |= (uint8_t)(1u << n);
        if (mode_ & 0x40) mode_ &= (uint8_t)~(1u << n);
        c.count = (uint16_t)(mode | 0x3FFF);
    } else {
        c.count = (uint16_t)(mode | (uint16_t)(rem - 1));
    }
}

void I8257::reset() {
    for (auto& c : ch_) { c.addr = 0; c.count = 0; }
    mode_    = 0;
    status_  = 0;
    flipHigh_ = false;
    lastTc_  = false;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13). The whole register file, the Mode Set / Status
// registers, and the two latches (the first/last flip-flop and the last-TC flag). No
// host handle and no clock live here, so there is nothing that cannot travel.
// ---------------------------------------------------------------------------
void I8257::serialize(StateWriter& w) const {
    for (const auto& c : ch_) { w.u16(c.addr); w.u16(c.count); }
    w.u8(mode_);
    w.u8(status_);
    w.boolean(flipHigh_);
    w.boolean(lastTc_);
}

void I8257::deserialize(StateReader& r) {
    for (auto& c : ch_) { c.addr = r.u16(); c.count = r.u16(); }
    mode_    = r.u8();
    status_  = r.u8();
    flipHigh_ = r.boolean();
    lastTc_  = r.boolean();
}

} // namespace altair
