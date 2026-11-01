#pragma once
//
// Vector Graphic Micropolis Disk Controller Board -- a 1K memory-mapped block, with a boot PROM
// and four registers, for 77-track 16-hard-sector 5.25" drives.
// See reference/Vector Graphic Micropolis Controller.md.
//
//   base+000-0FF  read  boot PROM
//   base+100-1FF  read  no chip. FF here, because no source gives the value (reference Q4)
//   base+200, +4  read  sector register   write  command
//   base+201, +4  read  status register   write  command
//   base+202-203  read  data              write  data
//
// THE GUEST MOVES EVERY BYTE, AND PRDY PACES IT. The board has no buffer and no DMA. Each access
// of the data register calls holdReady() up to the time the byte passes the head, so the guest
// runs at the speed of the disk. The position comes from the Spindle, which is a pure function
// of the Clock, so nothing here counts bytes on a read.
//
// THE 4-SECOND TIMER IS LEVEL, NOT A PULSE. D1-5 going low holds RST/ low (manual 1-11), so with
// the timer out a select is lost until a read of the board starts the timer again. That is also
// the state at power on. timerEnd_ is an absolute T-state, read at each access, and it needs no
// queued event.
//
// ONE CLOCK EVENT, ONLY WHEN IT HAS WORK. A sector boundary ends WRT, which stores the sector, and
// it sets INT. tick() runs at the boundary only while a write is on or interrupts are enabled, so
// an idle board lets the machine stand down.
//
// THE CHECKSUM IS THE GUEST'S. The board neither makes it nor checks it.

#include "core/board.h"
#include "core/spindle.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class MicropolisBoard : public Board {
public:
    MicropolisBoard();
    ~MicropolisBoard() override;

    std::string type() const override { return "micropolis"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    bool    assertsInt() const override { return int_ && irq_ == IrqJumper::Int; }
    uint8_t assertsVi() const override { return int_ ? viBit(irq_) : 0; }

    void reset(Reset r) override;
    void power() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The latches, the write buffer and the head of each drive.
    // timerEnd_ is an absolute T-state, which stays valid because the Clock's time travels too.
    // The disk images do not travel, and the rotation is a pure function of the Clock.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    // `SET mp0 DEBUG=seek` traces head steps. `DEBUG=sector` traces each write.
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

    // ---- For tests. ----
    int driveTrack(int n) const { return drive_[(size_t)n].track; }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    static constexpr int    kDrives  = 4;
    static constexpr int    kTracks  = 77;
    static constexpr int    kSectors = 16;
    static constexpr int    kRpm     = 300;
    static constexpr size_t kSlot    = 275;
    static constexpr size_t kProm    = 256;
    static constexpr int    kTimerSec = 4;

    static constexpr uint64_t kSfUs      = 30;    // Fig 1-7
    static constexpr uint64_t kReadTfUs  = 1268;  // one byte before the sync byte
    static constexpr uint64_t kReadSyncUs = 1300;  // Fig 1-12
    static constexpr uint64_t kWriteTfUs = 1232;  // WOS ends at 1200, then 8 bits (manual 1-18)
    static constexpr uint64_t kByteUs    = 32;

    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string path;
        int         sides  = 0;
        int         track  = 0;
        bool        roSaid = false;  // "write-protected, discarding" -- said once
        Spindle     spindle;         // stopped when empty, so no sector pulses arrive
    };

    uint64_t now() const { return clock_ ? clock_->now() : 0; }
    uint64_t tFromUs(uint64_t us) const { return us * (uint64_t)clock_->hz() / 1000000; }

    void   loadProm();
    void   settle();
    void   controllerReset();
    void   command(uint8_t v);
    void   step(bool in);
    Drive* selected();
    const Drive* selected() const;
    bool   ready() const;
    bool   transferFlag();
    uint8_t sectorRegister() const;
    uint8_t statusRegister();
    uint8_t readData();
    void   writeData(uint8_t v);
    const uint8_t* slotUnderHead();
    void   endWrite();
    void   rearm();
    void   tick();
    void   say(std::string s) { log_.push_back(std::move(s)); }
    void   probe(Drive& d);

    // ---- configuration ----
    uint16_t  base_      = 0xF800;       // W1-W4
    bool      speed2Mhz_ = false;        // W9 out
    bool      readyDrive_ = false;       // area B of Rev. 1 not at 2-3
    IrqJumper irq_       = IrqJumper::None;
    uint8_t   prom_[kProm] = {};

    // ---- runtime state (travels in a snapshot) ----
    bool     selected_ = false;          // US
    uint8_t  unit_     = 0;              // B3
    uint8_t  head_     = 0;
    bool     intEnable_ = false;         // D2-5
    bool     int_      = false;          // D2-9
    bool     timedOut_ = true;           // D1-5 low holds RST/ low. See the header
    uint64_t timerEnd_ = 0;
    bool     wrt_      = false;
    int      wrSector_ = 0;
    size_t   wrPos_    = 0;
    uint8_t  wrSlot_[kSlot] = {};

    std::vector<Drive> drive_ = std::vector<Drive>(kDrives);

    // A cache of one slot. It holds no state of the machine, so it does not travel.
    int     slotKey_ = -1;
    uint8_t slot_[kSlot] = {};

    Clock::Handle wake_ = Clock::kNone;
    std::vector<std::string> log_;
};

} // namespace altair
