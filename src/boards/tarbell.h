#pragma once
//
// Tarbell Electronics floppy interfaces -- single-density #1011 (July 1977) and
// double-density #2022 (1979-80). The S-100 disk controller that booted CP/M on
// most first-generation Altair/IMSAI machines. See
// reference/Tarbell_Floppy_Disk_Interface_Manual.md, docs/boards/tarbell-sd.md and
// docs/boards/tarbelldd.md.
//
// ONE CARD, TWO HALVES. Like the SBC-100/200 (boards/sd-sbc.h), a Tarbell board
// carries BOTH a disk controller (a WD FD1771/FD1791 + a drive-select latch over an
// 8-port block, default F8) AND a 32-byte boot PROM that shadows the bottom of
// memory over the S-100 PHANTOM* line until the boot code jumps past it. board.h
// names this very card as the canonical example of a board that is more than one
// kind of thing.
//
// TWO GENERATIONS, ONE SHARED BASE. `TarbellBoardBase` is the common card -- the
// FD177x register decode, the boot PROM / PHANTOM* half, the drives, MOUNT and the
// snapshot -- and is abstract (it never decides which chip it is). The two shipping
// cards are SIBLINGS off it, neither built on the other: the single-density #1011
// (FD1771) is `TarbellBoard`, the double-density #2022 (FD1791/93) is
// `TarbellDdBoard`. Each supplies its own chip, its OUT-FC control decode, the port-FD
// extra registers, and its disk geometry; the DD card adds the on-card 8257 DMA block.
// The base holds none of either card's specifics, so a change to one generation cannot
// reach the other.
//
// PORTS (8-port window, base F8; COMMAND AT OFFSET 0, unlike the VersaFloppy):
//   F8  command (write) / status (read)      F9  track      FA  sector
//   FB  data (wait-synced)                   FC  control (write) / WAIT (read: bit7 = DRQ/INTRQ)
//   FD  SD: unused / DD: ext-addr (write), DMA-busy (read)      FE-FF  unused
//
// THE BOOT IS FULLY AUTOMATIC. RESET arms the PROM at 0000 and drives the FDC's MR
// (an auto-Restore that homes the head); the PROM reads track 0 sector 1 into 0000
// and jumps to 07D -- an address with A5 set, which combinationally releases the
// shadow and drops into the cold loader it just read. No monitor, no auto-baud, no
// typed 'C'.

#include "boards/floppy-drive.h"
#include "chips/i8257.h"
#include "chips/wd17xx.h"
#include "core/board.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

// The shared Tarbell card: the FD177x register decode, the boot PROM / PHANTOM* half,
// the drives and their MOUNT plumbing, and the snapshot. ABSTRACT -- it never picks a
// chip, a control-port decode, or a disk geometry; those are the pure virtual seams
// each generation fills. Not a registered board; it cannot be instantiated on its own.
class TarbellBoardBase : public Board {
public:
    ~TarbellBoardBase() override;

    // ---- the bus: the 8-port I/O block AND the boot PROM's memory reads ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // ---- the boot PROM shadows low RAM over PHANTOM* until A5 releases it ----
    bool assertsPhantom(const BusCycle& c) const override;
    void snoop(const BusCycle& c) override;
    bool decodeIsPageUniform() const override { return false; }  // A5-gated inside page 0
    bool wantsSnoop() const override { return true; }            // watches for the A5 release

    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // Bad sectors on a mounted disk (#966): FAULT, the machine file's `faults`, and a `fault`
    // debug flag that writes one line each time one is used.
    bool        takesFaults(const std::string& unit) const override;
    DiskFaults* faults(const std::string& unit) override;
    std::vector<std::string> debugFlags() const override { return {"fault"}; }

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For tests, without going through the bus.
    Wd17xx& chip() { return *chip_; }
    bool    promArmed() const { return armed_; }

protected:
    TarbellBoardBase();   // wires up the drive vector; a concrete card calls buildChip()

    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

    // One physical drive on the daisy chain: the mounted image (the board OWNS it) and
    // the adapter that presents it to the chip as a FloppyDrive, carrying the head
    // position (runtime state the board serializes). Same shape as the VersaFloppy.
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string    path;   // as WRITTEN (round-trips through SAVE)
        DiskImageDrive drv;
    };

    // A run of tracks sharing one on-disk format. The SD card describes one range; the
    // DD card two -- an SD track 0 and DD tracks 1-76 -- which is why this is a LIST and
    // not the VersaFloppy's single uniform descriptor.
    struct FmtRange {
        int     trackLo, trackHi, headLo, headHi;
        Density density;
        int     sectors, sectorSize, startSector;
    };

    // ---- the seams each generation fills; the base holds neither card's answer ----
    virtual void    buildChip() = 0;                 // SD: Wd1771; DD: Wd1791
    virtual void    writeControl(uint8_t v) = 0;     // SD: function decoder; DD: bitmap latch
    virtual uint8_t readExtra(uint8_t off) const = 0;        // DD: port FD in; SD: none
    virtual void    writeExtra(uint8_t off, uint8_t v) = 0;  // DD: port FD out; SD: none
    // Probe an image's byte count into a geometry. SD: 256,256; DD: a 499,456 / 256,256 / blank
    // superset (the DD controller reads SD media too).
    virtual bool    describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                                     std::vector<FmtRange>& ranges, std::string& err) const = 0;

    void   applySelection();   // point the chip at drive_[sel_], set side + data rate
    void   refresh();          // advance the chip, re-drive the wires, re-arm the wake
    Clock& clk() const;
    void   loadProm();

    std::unique_ptr<Wd17xx> chip_;
    std::vector<Drive>      drive_;

    uint8_t   port_     = 0xF8;   // decodes port_ .. port_+7
    int       drives_   = 4;
    IrqJumper irq_      = IrqJumper::None;
    int       sel_      = 0;   // default drive 0 -- the strapped single-drive card never writes FC
    int       side_     = 0;
    long long dataRate_ = 250000;
    bool      timingReal_ = false;  // `timing = real`: the wait port holds READY (#637)

    // ---- the boot PROM / PHANTOM* half ----
    uint8_t prom_[32]  = {};
    bool    armed_     = true;   // the PROM flip-flop (runtime; travels in a snapshot)
    bool    bootstrap_ = true;   // the DIP that enables the boot PROM at all (a strap)

    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;
};

// The single-density #1011: an FD1771, the 74LS138 OUT-FC function decoder, no port-FD
// extra registers, and a single 256,256-byte 8" SD geometry.
class TarbellBoard : public TarbellBoardBase {
public:
    TarbellBoard();

    std::string type() const override { return "tarbell"; }

protected:
    void    buildChip() override;
    void    writeControl(uint8_t v) override;
    uint8_t readExtra(uint8_t /*off*/) const override { return 0xFF; }  // no port FD on the SD card
    void    writeExtra(uint8_t /*off*/, uint8_t /*v*/) override {}
    bool    describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                             std::vector<FmtRange>& ranges, std::string& err) const override;
};

// The double-density #2022: a WD1791/93, a bitmap OUT-FC latch (density + side +
// drive), a port-FD DMA-busy/extended-address register, and mixed-density media
// (single-density track 0, double-density tracks 1-76). It also READS plain SD media
// (256,256) and FORMATS a blank into a mixed image track-by-track, each track's density
// taken from the OUT-FC density bit -- see describeGeometry.
//
// AND IT IS THE FIRST SHIPPING BUS MASTER (DESIGN.md 4.5). The card carries an Intel 8257
// DMA controller (chips/i8257.h) wired to its FD1791, decoded onto a second port block
// (default 0xE0). With DMA-mode CBIOS the guest programs the 8257 (address, length, R/W
// mode), issues the WD1791 read/write, and the card STEALS BUS CYCLES to move the sector
// -- pulling pHOLD and driving the transfer through the same BusMaster::step() the CPU
// uses, so BREAK MEM and TRACE catch a DMA cycle like any other. The 8257 is a CHIP the
// board owns (HAS-A, like the WD1791); the BOARD is the bus master.
class TarbellDdBoard : public TarbellBoardBase {
public:
    // buildChip() is pure virtual, so the base constructor never runs it -- each card
    // builds its own chip here, once the object is fully its own type. Ours is the Wd1791.
    TarbellDdBoard() { mover_.d = this; buildChip(); }

    std::string type() const override { return "tarbelldd"; }

    // ---- the bus: the base F8 window PLUS the 8257's register block at dmaport_ ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // ---- pHOLD / the on-card 8257 as a bus master (HAS-A, DESIGN.md 4.5) ----
    // The card pulls pHOLD when a channel is armed AND the FD1791 has a byte pending
    // (HRQ = enable & DRQ on the real chip). busMaster() hands back the Mover the run
    // loop drives once it grants pHLDA.
    bool       requestsBus() const override;
    BusMaster* busMaster()         override { return &mover_; }

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    void    buildChip() override;
    void    writeControl(uint8_t v) override;
    uint8_t readExtra(uint8_t off) const override;
    void    writeExtra(uint8_t off, uint8_t v) override;
    bool    describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                             std::vector<FmtRange>& ranges, std::string& err) const override;

private:
    // Is a port in the 8257's 16-address register block? (A3..A0 off a /CS decode.)
    bool    inDmaWindow(uint8_t p) const { return p >= dmaport_ && (uint8_t)(p - dmaport_) < 16; }

    // ONE granted byte: two real bus cycles through the FD1791, mirroring the board's
    // own PIO data path (readData/writeData then poll), plus one 8257 advance(). Burst:
    // held under one pHLDA grant until the channel disables itself at terminal count.
    StepResult transferOne(Bus& bus);

    // HAS-A, NOT IS-A (board.h, test_dma.cpp): the board CONTAINS this and returns it
    // from busMaster(); it does NOT inherit BusMaster, or Machine::master()'s
    // dynamic_cast would mistake the disk controller for a CPU.
    struct Mover : public BusMaster {
        TarbellDdBoard* d = nullptr;
        StepResult step(Bus& bus) override { return d->transferOne(bus); }
    } mover_;

    I8257  dma_;            // the on-card DMA controller, wired to chip_ (the FD1791)
    uint8_t dmaport_ = 0xE0; // base of the 8257's register block (SD2DD straps it E0)
    uint8_t extAddr_ = 0;   // the A16-A23 extended-address latch (beyond our 64K bus; stored only)
};

} // namespace altair
