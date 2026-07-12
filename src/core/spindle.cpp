#include "core/spindle.h"

namespace altair {

void Spindle::configure(int rpm, int sectorsPerTrack) {
    rpm_     = rpm > 0 ? rpm : 0;
    sectors_ = sectorsPerTrack > 0 ? sectorsPerTrack : 0;
}

// hz T-states per second, rpm/60 revolutions per second, so hz / (rpm/60) T-states
// per revolution -- and the multiply comes FIRST so the only division is the last
// one. Dividing by (rpm/60) instead would floor 360/60 to 6 and lose the remainder
// of every rpm that is not a multiple of 60.
uint64_t Spindle::tPerRev(const Clock& c) const {
    if (!spinning()) return 0;
    return (uint64_t)(c.hz() * 60 / rpm_);
}

// THE SECTOR IS THE UNIT, NOT THE REVOLUTION. Everything below is derived from this
// one number, which means a revolution is exactly `sectors * tPerSector` T-states --
// up to `sectors - 1` T-states short of tPerRev(), because this division truncates.
// That is ~0.01% at 8" geometry and it is the RIGHT trade: taking the sector as the
// unit keeps sectorAt() and nextBoundary() in exact agreement, so the sector under
// the head never disagrees with the deadline that announced it. Deriving both from
// tPerRev independently would let them round apart, and a board would occasionally
// wake to find the sector it was promised has not arrived yet.
uint64_t Spindle::tPerSector(const Clock& c) const {
    if (!spinning()) return 0;
    uint64_t rev = tPerRev(c);
    uint64_t t   = rev / (uint64_t)sectors_;
    return t ? t : 1;  // a disk that spins faster than the clock ticks still moves
}

int Spindle::sectorAt(const Clock& c) const {
    if (!spinning()) return 0;
    return (int)((c.now() / tPerSector(c)) % (uint64_t)sectors_);
}

uint64_t Spindle::intoSector(const Clock& c) const {
    if (!spinning()) return 0;
    return c.now() % tPerSector(c);
}

uint64_t Spindle::nextBoundary(const Clock& c) const {
    if (!spinning()) return 0;  // zero means never
    uint64_t t = tPerSector(c);
    return c.now() - (c.now() % t) + t;  // strictly future for any t >= 1
}

} // namespace altair
