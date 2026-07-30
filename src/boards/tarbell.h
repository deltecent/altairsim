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
// TWO GENERATIONS, SD IS THE BASE. The single-density #1011 (FD1771) is
// `TarbellBoard`; the double-density #2022 (FD1791/93) is `TarbellDdBoard`, which
// inherits it and overrides only the chip, the OUT-FC control decode, the port-FD
// DMA/extended-address registers, and the disk geometry (its track 0 is single
// density, the rest double).
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
#include "chips/wd17xx.h"
#include "core/board.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class TarbellBoard : public Board {
public:
    TarbellBoard();
    ~TarbellBoard() override;

    std::string type() const override { return "tarbell"; }

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

    // ---- the four things the double-density card changes ----
    virtual void    buildChip();                     // SD: Wd1771; DD: Wd1791
    virtual void    writeControl(uint8_t v);         // SD: function decoder; DD: bitmap latch
    virtual uint8_t readExtra(uint8_t off) const { (void)off; return 0xFF; }    // DD: port FD in
    virtual void    writeExtra(uint8_t off, uint8_t v) { (void)off; (void)v; }  // DD: port FD out
    // Probe an image's byte count into a geometry. SD: 256,256; DD: 499,456.
    virtual bool    describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                                     std::vector<FmtRange>& ranges, std::string& err) const;
    // The card's FORMAT budget in raw track bytes, granted to each drive on mount. Nonzero =
    // "this card can be formatted" AND the wait-synced Write Track byte budget (floppy-drive.h).
    // SD is one 8" SD revolution (~5208). The DD card's rate-doubled / mixed-density budget is
    // DEFERRED, so it returns 0 -- Write Track keeps faulting with WRITE FAULT, unchanged.
    virtual int     formatTrackBytes() const;

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

    // ---- the boot PROM / PHANTOM* half ----
    uint8_t prom_[32]  = {};
    bool    armed_     = true;   // the PROM flip-flop (runtime; travels in a snapshot)
    bool    bootstrap_ = true;   // the DIP that enables the boot PROM at all (a strap)

    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;
};

// The double-density #2022: a WD1791/93, a bitmap OUT-FC latch (density + side +
// drive), a port-FD DMA-busy/extended-address register, and mixed-density media
// (single-density track 0, double-density tracks 1-76).
class TarbellDdBoard : public TarbellBoard {
public:
    // The base constructor already ran buildChip() -- but virtual dispatch during base
    // construction reaches TarbellBoard::buildChip (a Wd1771), not ours. Rebuild here, now
    // that the object is fully a TarbellDdBoard, so the chip is the Wd1791 this card needs.
    TarbellDdBoard() { buildChip(); }

    std::string type() const override { return "tarbelldd"; }

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    void    buildChip() override;
    void    writeControl(uint8_t v) override;
    uint8_t readExtra(uint8_t off) const override;
    void    writeExtra(uint8_t off, uint8_t v) override;
    bool    describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                             std::vector<FmtRange>& ranges, std::string& err) const override;
    // DD blank format is DEFERRED (a rate-doubled budget + a mixed-density fresh disk needs a
    // size/format argument to CREATE). Return 0 so Write Track keeps faulting, as it does today.
    int     formatTrackBytes() const override { return 0; }

private:
    uint8_t extAddr_ = 0;   // the A16-A23 extended-address latch (a no-op store in our PIO model)
};

} // namespace altair
