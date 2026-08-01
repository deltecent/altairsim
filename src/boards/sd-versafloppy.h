#pragma once
//
// SD Systems VersaFloppy I & II -- an S-100 soft-sector floppy controller built around a
// Western Digital FD177x. See reference/SD Systems VersaFloppy.md.
//
// ONE BOARD, TWO GENERATIONS. The VersaFloppy I (FD1771, single density) and the
// VersaFloppy II (FD1791, single AND double density) share the same 8-port block, the same
// board-level control/status latch at the same address, and the same driver family (the
// DDBIOS and the SD/MS monitors). They differ only in the FDC part and a few bits of the
// control register -- so they are one board with a `variant` property, VF-II the default
// (it is the one that boots SDOS's double-density 256-byte disks). The chip is the matching
// part of the wd17xx family (Wd1771 or Wd1791); the disk on the far end is a DiskImageDrive
// (boards/floppy-drive.h) over a mounted DiskImage.
//
// PRDY, NOT DRQ. The VersaFloppy does not let the guest poll DRQ: the FDC's DRQ drives a
// wait-state generator (S-100 PRDY) that stalls the CPU until the byte -- and the whole
// command -- is ready. A sector transfer is a bare `IN A,(67H)` loop with no software
// polling. We cannot inject wait states, so the chip runs wait-synced (Wd17xx::setWaitSynced):
// every register access resolves the command as if the CPU had waited on PRDY. See the .cpp
// and wd17xx.h.
//
// PORTS (standard window, base 60H; the board decodes A0-A7 only):
//   60H  controller reset strobe (VF-II; a no-op on VF-I)
//   61H, 62H  unused
//   63H  board control latch (write) / status readback (read) -- bit layout differs VF-I/VF-II
//   64H  FD177x Command (write) / Status (read)
//   65H  FD177x Track          66H  FD177x Sector          67H  FD177x Data (wait-synced)

#include "boards/floppy-drive.h"
#include "chips/wd17xx.h"
#include "core/board.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class VersaFloppyBoard : public Board {
public:
    VersaFloppyBoard();
    ~VersaFloppyBoard() override;

    std::string type() const override { return "versafloppy"; }

    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // No disksInterleaved() override: the VersaFloppy lays disks cylinder-major (head-minor),
    // which is Board's default, so a converted IMD is emitted that way too (host/imd.h). The
    // order is fixed by the tool that dumps these disks (vf.z80, cylinder-major over XMODEM) and
    // matched by the DDBIOS format itself, which writes both sides of a cylinder before stepping
    // (DDB200.ASM NXTRK) -- see mount().

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For tests, without going through the bus.
    Wd17xx& chip() { return *chip_; }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    enum class Variant { Vf1, Vf2 };

    // One physical drive on the daisy chain: the mounted image (the board OWNS it) and the
    // adapter that presents it to the chip as a FloppyDrive. The adapter carries the head
    // position, which is runtime state the board serializes.
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string    path;    // as WRITTEN (round-trips through SAVE); see mount()
        std::string    forced;  // the `media` property: "" means "probe it"
        bool           roSaid = false;  // "mounted write-protected" -- said once, not per access
        DiskImageDrive drv;
    };

    // The controller and the guest's control latch.
    void     buildChip();       // (re)construct chip_ for variant_, wait-synced
    void     selectFromControl();  // decode 63H: drive select, side, density -> attach + straps
    void     refresh();         // advance the chip, re-drive pin 73, re-arm any wake
    Drive*   selected();
    Clock&   clk() const;

    Variant  variant_ = Variant::Vf2;
    uint8_t  port_    = 0x60;   // decodes port_ .. port_+7
    int      drives_  = 4;
    IrqJumper irq_    = IrqJumper::None;

    std::unique_ptr<Wd17xx> chip_;
    std::vector<Drive>      drive_;

    uint8_t control_ = 0;   // the last byte written to 63H (drive/side/density/wait/int)
    int     sel_     = -1;  // which drive the one-hot select picked, or -1 for none

    // What the CARD (as opposed to the chip) has to say -- e.g. a host-forced write-protect.
    // drainLog() merges this with the chip's.
    std::vector<std::string> log_;

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace altair
