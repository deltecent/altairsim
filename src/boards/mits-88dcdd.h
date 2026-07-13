#pragma once
//
// MITS 88-DCDD -- the Altair floppy disk controller (docs/boards/mits-dcdd.md).
//
// AN UNUSUALLY RAW CONTROLLER. It does not read or write sectors for you. Software
// steps the head, watches the sector counter go by, and shifts bytes one at a time
// through a data port in real time -- so nearly all the disk logic lives in the
// BIOS, and what this card owes the guest is mostly TIMING that is honest.
//
// There is no controller chip to model here (contrast the Tarbell, which wires up an
// FD1771): the 88-DCDD is TTL, and its "registers" are three ports over a handful of
// flip-flops. So this file is the whole card.
//
// THE THREE THINGS THAT ARE EASY TO GET WRONG, all of them from the doc's quirks
// table, and all of them pinned by tests:
//
//   1. STATUS READS INVERTED. Flags are kept true-sense in `Status` and complemented
//      on the way out. Get this backwards and nothing works, immediately.
//
//   2. THE DISK TURNS ON ITS OWN. The sector under the head is a reading taken off
//      the Clock (Spindle, DESIGN.md 7.5.1) and reading port 0x09 does not advance
//      it. The SIMH module this card's doc was once written from bumps a counter
//      every second read of that port, which makes the platter spin at the speed of
//      whatever loop is polling it. See docs/boards/mits-dcdd.md.
//
//   3. SECTORS ARE NUMBERED FROM ZERO (startSector = 0). The Tarbell numbers from
//      one. DESIGN.md 7.3 calls that the off-by-one that silently corrupts a disk,
//      and the two cards sit in the same machine with both conventions live.
//
// THE SLOT IS 137 BYTES AND THE IMAGE HOLDS ALL OF IT. This is a HARD-SECTOR card:
// sync byte, header, 128-byte payload, checksum, stop byte, trailer -- the lot is on
// the medium and the lot is in the file. (A soft-sector image holds the payload
// only, because those headers lived in the inter-sector gap and never reached the
// file.) What is INSIDE the slot is the BIOS's business, not ours: this card moves
// 137 bytes and never looks at one of them.

#include "core/board.h"
#include "core/spindle.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// The slot, on every format. Not a payload size -- see above.
inline constexpr int kDcddSectorBytes = 137;

// ---------------------------------------------------------------------------
// THE MEDIA THIS CARD KNOWS.
//
// Geometry probing belongs to the BOARD (DESIGN.md 7.3): 337,568 bytes means a
// 77-track 8" floppy *because it is a DCDD*, and the same byte count on another
// controller means something else. Only this card knows which formats are even
// candidates, so only this card can probe.
//
// `dataTrack` is where the sector layout changes from the system format to the data
// format, and it is NOT the same on every medium -- the minidisk's is 4 where the
// 8" is 6. This card does not use it (what is inside the slot is the BIOS's affair)
// but the doc records it, and a formatter written against this card would need it.
// ---------------------------------------------------------------------------
struct DcddFormat {
    const char* name;
    int         tracks;
    int         sectors;
    int         dataTrack;
    uint64_t    bytes;  // tracks * sectors * 137
};

const std::vector<DcddFormat>& dcddFormats();

class DcddBoard : public Board {
public:
    DcddBoard();

    std::string type() const override { return "dcdd"; }

    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    void reset(Reset) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;
    std::vector<SubUnit> subUnits() const override;

    std::vector<std::string> drainLog() override;

private:
    // ---- The card's flags, TRUE-SENSE. The inversion happens once, on the way out.
    struct Status {
        bool enwd    = false;  // 0x01 write circuit wants another byte
        bool moveok  = false;  // 0x02 head movement allowed
        bool hdstat  = false;  // 0x04 head loaded
        bool dsken   = false;  // 0x08 a drive is selected and enabled
        bool inten   = false;  // 0x20 interrupts enabled (decoded, not wired)
        bool track0  = false;  // 0x40 head on track 0
        bool nrda    = false;  // 0x80 new read data available
    };

    // One Pertec FD-400 and whatever is in it. The Spindle is PER DRIVE because the
    // media are not all the same shape: a minidisk turns 16 sectors past the head
    // where an 8" turns 32, so "which sector is under the head" is a different
    // question on each, and a single board-wide spindle could not answer both.
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string path;
        std::string forced;   // `media` property: "", "8in", "minidisk", "fdc8mb"
        DcddFormat  fmt{};
        bool        loaded = false;
        int         track  = 0;
        Spindle     spindle;
    };

    Drive* selected();
    const Drive* selected() const;

    // Rotation, in the only place it is computed. Everything the guest can observe
    // about time -- the sector number, sector-true, NRDA -- comes out of here.
    struct Position {
        bool spinning   = false;
        int  sector     = 0;
        bool sectorTrue = false;  // the 30 us one-shot: positioned, write NOW
        int  byteIndex  = -1;     // which of the 137 bytes the read head has passed
        int  writeIndex = -1;     // which byte the write circuit is asking for
    };
    Position where() const;

    // Load the slot now under the head, if it is not the one we are holding. Called
    // from EVERY port read: the read head clocks off the medium continuously and does
    // not wait to be asked. (It used to hang off the sector port alone, which worked
    // only because the CP/M BIOS happens to poll that port first.)
    void syncSector(const Position&);

    // The card's one-shots are RC networks, so they are measured in MICROSECONDS and
    // know nothing about the CPU. A 4 MHz machine gets twice as many instructions
    // inside the same 30 us sector-true window; the window does not grow.
    uint64_t tFromUs(uint64_t us) const;
    uint64_t tPerByte() const;  // 32 us: 250,000 bits/sec, 64 T at 2 MHz

    void selectDrive(int n);
    void command(uint8_t v);
    void flushWrite();
    void invalidatePosition();
    bool probe(Drive&, std::string& err);
    void say(std::string s) { log_.push_back(std::move(s)); }

    // ---- Properties (jumpers and straps) ----
    uint16_t port_    = 0x08;  // three ports: base, base+1, base+2
    int      drives_  = 4;     // 1..16 daisy-chained; four was typical
    IrqJumper irq_    = IrqJumper::None;

    std::vector<Drive> drive_;
    int                sel_ = -1;  // -1 = none selected. Status then reads 0xFF.
    Status             st_;

    // The read path. `bufSector_` is which sector is in the buffer, or -1; `readPos_`
    // is how many of its bytes the guest has taken. The guest cannot read a byte the
    // disk has not turned past yet -- that is what NRDA is for.
    uint8_t buf_[kDcddSectorBytes]{};
    int     bufSector_ = -1;
    int     readPos_   = 0;

    // The write path. cWRTEN arms it; the card then consumes EXACTLY 137 bytes and
    // commits. A partial write that is interrupted by a step or a select must still
    // be flushed -- the system sectors are only 133 bytes long and never reach 137,
    // so a card that waited for the 137th byte would lose every one of them.
    bool    writing_  = false;
    uint8_t wbuf_[kDcddSectorBytes]{};
    int     writePos_ = 0;
    int     wSector_  = -1;
    int     wTrack_   = -1;

    std::vector<std::string> log_;
};

} // namespace altair
