#pragma once
//
// Vector Graphic Dual-Mode Disk Controller -- four I/O ports over a 512-byte buffer RAM, as a
// floppy controller. See reference/HD-FD documentation.md. The hard disk half is not built:
// the board is modeled with the floppy-only jumper out, so all four drives are floppies.
//
//   base+0  OUT  drive 0-1, side 2, step 5, direction 6.  Retriggers the 4 s motor timer.
//           IN   status 0: write protect 0, track 0 2, and 5-7 read 1.
//   base+1  OUT  sector 0-3, read/write 5 (1 = read), ECC enable 6.
//           IN   status 1: floppy selected 0, busy 1 (always 0, see below), motor on 2.
//   base+2  IN/OUT one byte of RAM. The address counter goes up and wraps at 512.
//   base+3  IN   clears the counter and reads FF, because nothing drives the bus.
//           OUT  START: read or write one whole sector, between RAM and the disk.
//
// THE BOARD HAS NO COMMAND SET. It does not seek and it does not check an ID. The guest loads
// every byte of the sector into RAM, and the guest checks every header byte. The only compare
// in hardware is the sector number against the sector counter.
//
// BUSY IS NOT A STATUS BIT. While busy, BUSY* gates the address decode, so the board answers no
// port: each IN floats to FF and each OUT is lost. Status 1 bit 1 is wired to ground. A driver
// reads bit 1 = 1 only because a floating bus reads FF (reference §4, Q2).
//
// decodes() CANNOT SHOW THIS, because the bus caches its answer and busy ends with no bus cycle
// and no event. read() and write() do it, and the guest sees the same floating bus.
//
// THE TRANSFER HAPPENS AT START, AND ONLY THE WAIT IS LAZY. The guest cannot see RAM or the
// disk until busy ends, so the bytes can move at once. That leaves no deadline in the Clock
// queue, so an idle board lets the machine stand down, and a write reaches the host image even
// if the guest stops inside the wait. busyEnd_ and motorEnd_ are absolute T-states.
//
// THE SLOT IS 275 BYTES AND THE IMAGE HOLDS ALL OF IT: sync, track, sector, 10 zeros, 256 data,
// checksum, 4 ECC, valid. That is the sector as it reads back into RAM, from the sync byte on.

#include "core/board.h"
#include "core/spindle.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class DualModeBoard : public Board {
public:
    std::string type() const override { return "dualmode"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    void reset(Reset r) override;
    void power() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The latches, the RAM and its counter, and the head of
    // each drive. busyEnd_ and motorEnd_ are absolute T-states, which stay valid because the
    // Clock's time travels too. The disk images do not travel, and the rotation is a pure
    // function of the Clock.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // `SET dm0 DEBUG=seek` traces head steps. `DEBUG=sector` traces each transfer.
    std::vector<std::string> debugFlags() const override { return {"sector", "seek"}; }
    enum DebugFlag { SECTOR = 0, SEEK = 1 };

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    // Both sides of the image are head-major: all of side 0, then all of side 1.
    bool disksInterleaved(uint64_t) const override { return false; }

    std::vector<std::string> drainLog() override;

    // The ECC of `n` bytes, from the schematic: g(x) = x^32 + x^24 + x^18 + x^15 + x^14 + x^11 +
    // x^8 + x^7 + 1, MSB first, starting at 0. The top byte is the first on the disk.
    static uint32_t ecc(const uint8_t* p, size_t n);

    // ---- For tests. ----
    int driveTrack(int n) const { return drive_[(size_t)n].track; }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    static constexpr int      kDrives   = 4;
    static constexpr int      kTracks   = 77;
    static constexpr int      kSectors  = 16;
    static constexpr int      kRpm      = 300;  // System B Overview 1-3, Micropolis 1115 Table 1-1
    static constexpr size_t   kSlot     = 275;
    static constexpr size_t   kEccAt    = 270;  // in the slot
    static constexpr size_t   kEccRam   = 310;  // in RAM on a write: U51 times it off the address
    static constexpr uint16_t kRamBytes = 512;
    static constexpr int      kMotorSec = 4;

    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string path;
        int         sides  = 0;
        int         track  = 0;
        bool        roSaid = false;  // "write-protected, discarding" -- said once
        Spindle     spindle;         // stopped when empty, so no sector pulses arrive
    };

    uint64_t now() const { return clock_ ? clock_->now() : 0; }
    bool     busy() const { return now() < busyEnd_; }
    bool     motorOn() const { return now() < motorEnd_; }
    bool     isReadCmd() const { return (c1_ & 0x20) != 0; }
    bool     eccEnabled() const { return (c1_ & 0x40) != 0; }

    // The drive the select lines reach, or null. The timer's output enables the select
    // decoder, so no drive stays selected once it ends.
    Drive*       selected();
    const Drive* selected() const;

    void probe(Drive& d);
    void start();
    void readSlot(Drive& d, int sector);
    void writeSlot(Drive& d, int sector);
    void say(std::string s) { log_.push_back(std::move(s)); }

    uint16_t port_ = 0xC0;  // jumper block D, A7-A4

    uint8_t  c0_ = 0, c1_ = 0;      // U9, U18. POC* clears both
    uint8_t  ram_[kRamBytes]{};     // two 2114s, A9 grounded
    uint16_t counter_ = 0;          // U59
    uint64_t motorEnd_ = 0;         // U60, the 4-second timer
    uint64_t busyEnd_  = 0;

    std::vector<Drive> drive_ = std::vector<Drive>(kDrives);

    std::vector<std::string> log_;
};

} // namespace altair
