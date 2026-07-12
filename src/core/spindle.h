#pragma once
//
// Spindle -- the rotating disk, as a pure function of time (DESIGN.md 7.5.1).
//
// A floppy turns whether or not anyone is looking at it. So the sector under the
// head is not STATE that a controller advances; it is a READING taken off the
// clock, and this class is the whole of that arithmetic:
//
//     sector = (now / tPerSector) % sectors
//
// THERE IS NO HIDDEN COUNTER AND NO ADVANCE-ON-READ. That matters more than it
// looks. A counter that ticks when the guest reads the sector-position port makes
// the disk's rotation depend on how often the guest polls -- so a tight polling
// loop spins the disk faster than a slow one, the drive runs at the speed of the
// software watching it, and a recorded session stops replaying identically. Derive
// it from the clock and every one of those goes away for free.
//
// TWO CARDS NEED THIS, FOR DIFFERENT REASONS, AND THAT IS WHY IT IS HERE AND NOT ON
// EITHER OF THEM:
//
//   - the 88-DCDD hands the sector number straight to the guest at `IN 0x09`;
//   - the Tarbell never exposes it, but its FD1771's Read Address command (0xC4) has
//     to answer "which sector is under the head right now" so the buffered CP/M BIOS
//     can start a track read where the head already is. A static answer spins that
//     BIOS forever.
//
// Same arithmetic, so: written once. The Spindle knows about Clock and NOTHING else
// -- not a board, not a MediaFile, not a sector's contents. It hands back an INDEX,
// 0-based; a controller that numbers its sectors from 1 (the Tarbell does; the DCDD
// does not) adds its own startSector. That off-by-one is the one that silently
// corrupts a disk, so it stays where the board can see it and is deliberately NOT
// buried in here.

#include "core/clock.h"

#include <cstdint>

namespace altair {

class Spindle {
public:
    // The drive's mechanics: 360 RPM and 32 sectors is an 8" DCDD track. A Spindle
    // that has not been told them is STOPPED -- it reads sector 0 and schedules
    // nothing -- which is the honest model of a drive with no disk in it.
    void configure(int rpm, int sectorsPerTrack);
    void stop() { rpm_ = 0; sectors_ = 0; }

    bool spinning() const { return rpm_ > 0 && sectors_ > 0; }
    int  rpm() const { return rpm_; }
    int  sectors() const { return sectors_; }

    // T-states for one revolution, and for one sector to pass the head. Zero when
    // stopped. Derived from Clock::hz(), so a machine running at other than 2 MHz
    // still turns its disks at 360 RPM -- the crystal moves, the motor does not.
    uint64_t tPerRev(const Clock& c) const;
    uint64_t tPerSector(const Clock& c) const;

    // WHICH SECTOR IS UNDER THE HEAD. A 0-based index; see the note above about
    // startSector. Zero when stopped.
    int sectorAt(const Clock& c) const;

    // How far into that sector the head is, in T-states. The DCDD's byte-position
    // counter is built on this.
    uint64_t intoSector(const Clock& c) const;

    // When the NEXT sector arrives under the head -- the deadline a board arms with
    // Clock::at() to wake a guest that is polling for one.
    //
    // STRICTLY FUTURE, always, by construction: now - (now % tPerSector) + tPerSector
    // is greater than now for any tPerSector >= 1. That is the same rule the UART's
    // nextEdge() enforces by hand, and for the same reason -- a deadline armed for
    // now() fires inside the drain loop that is running us, re-arms, and never stops.
    //
    // ZERO MEANS NEVER, matching nextEdge(): a stopped spindle has no next anything.
    uint64_t nextBoundary(const Clock& c) const;

private:
    int rpm_     = 0;
    int sectors_ = 0;
};

} // namespace altair
