#pragma once
//
// Cromemco 4FDC / 16FDC / 64FDC floppy-disk controllers -- one S-100 board family that
// each combine a Western Digital FD177x floppy controller, an onboard TMS 5501 (a UART +
// timers + interrupt controller, wired here as a single console channel), and an onboard
// RDOS boot PROM. See reference/Cromemco 4FDC 16FDC 64FDC Floppy Controllers.md,
// docs/boards/cromemco-16fdc.md and docs/boards/cromemco-64fdc.md.
//
// ONE CARD, THREE THINGS AT ONCE -- the same "more than one kind of thing" as the Tarbell
// (boards/tarbell.h) and the SBC (boards/sd-sbc.h), and this board is built from both:
//
//   - THE TARBELL half: a WD FD177x + a drive-select latch + a boot PROM the card owns
//     outright. The floppy stack (chips/wd17xx.h, boards/floppy-drive.h) is reused
//     UNCHANGED, exactly as the Tarbell and VersaFloppy reuse it.
//   - THE SBC half: one UART embedded DIRECTLY as a member, the card owning
//     refresh()/nextEdge()/wake_ and the endpoint resolver -- but the UART is a TMS 5501,
//     not an 8251 or a 6850.
//
// THE FAMILY IS ONE BASE + THIN LEAVES (the Tarbell pattern). The three boards differ
// across a handful of independent axes -- which WD part, ROM size, whether OUT 40H can
// bank the ROM out, the port-34/port-04 bit tables, single vs double density -- so a base
// class with a per-leaf answer for each beats a variant-enum switch sprinkled everywhere.
//
//   CromemcoFdcBoard : Board          the shared card: ports 00-09 / 30-34 / 40, the ROM
//     |                               window at C000, refresh()/applySelection()
//     |-- Fdc16Board                  FD1793, 4K RDOS 2.52, OUT 40H, mixed-density probe
//     `-- Fdc64Board                  FD1793, 4K RDOS 3.12, OUT 40H, mixed-density probe
//     (later) Fdc4Board               FD1771, 1K RDOS, no bank-select, single density
//
// The 16FDC and 64FDC are near-identical in Phase 1 (both FD1793, both 4K, both OUT 40H,
// both double density); the base carries all of it and the two leaves override only the
// board name and which RDOS ROM they carry. The 4FDC (single density, 1K no-disable ROM)
// is a thin follow-up on the same base -- which is why buildFdc()/romBytes()/hasBankSelect()
// /describeGeometry() are virtual even though the two shipping leaves do not touch them.
//
// PHASE 1 IS A POLLED BOOT. The TMS 5501's five timers and eight-source interrupt
// controller are inert (chips/tms5501.h), so assertsInt() is false and no disk interrupt
// reaches the backplane; a polled RDOS/CDOS console boot needs none of it. The disk-side
// interrupt routing (RS7/DRQ/RTC through the 5501) is a later effort -- said out loud, not
// overlooked. The port-04 aux register IS modeled (side-select + seek status -- readAux/
// writeAux), because RDOS's boot-sector read polls it; only the PerSci mechanical bits
// (eject / fast-seek) are no-ops, an emulated drive having no such mechanism to drive.

#include "boards/floppy-drive.h"
#include "chips/tms5501.h"
#include "chips/wd17xx.h"
#include "core/board.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class CromemcoFdcBoard : public Board {
public:
    CromemcoFdcBoard();
    ~CromemcoFdcBoard() override;

    // ---- the bus: the I/O blocks (00-09 serial, 30-34 disk, 40 bank-select) AND the
    // RDOS PROM's memory reads while it is armed ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // Phase 1 raises no interrupt: the 5501's interrupt controller is inert and no disk
    // interrupt is wired to the backplane yet (see the class note). One place to make real.
    bool    assertsInt() const override { return false; }
    uint8_t assertsVi() const override { return 0; }

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    // ---- the console serial unit ('tty'), delegated to the embedded TMS 5501 ----
    std::vector<UnitDef>  units() const override;
    std::vector<Property> unitProperties(const std::string& unit) override;
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "tty" ? &uart_.stream() : nullptr;
    }
    uint64_t rxBytes() const override { return uart_.rxBytes(); }

    // ---- the disk drives, MOUNT/UNMOUNT, and the [[board.drive]] sub-unit table ----
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The monitor resolves an endpoint string to a stream; the board (not the chip) holds
    // the resolver, because the chip is not allowed to know the grammar (DESIGN.md 7.7).
    // Installed in main.cpp and tests/main.cpp. One static covers every leaf.
    static void setResolver(EndpointResolver r);

    // ---- for tests, without going through the bus ----
    Wd17xx&  chip() { return *chip_; }
    Tms5501& uart() { return uart_; }
    bool     romArmed() const { return armed_; }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

    // One physical drive on the daisy chain: the mounted image (the board OWNS it) and the
    // adapter that presents it to the chip as a FloppyDrive, carrying the head position
    // (runtime state the board serializes). Same shape as the Tarbell/VersaFloppy.
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string    path;   // as WRITTEN (round-trips through SAVE)
        DiskImageDrive drv;
    };

    // A run of tracks sharing one on-disk format. A CDOS disk is one MIXED format -- a DD
    // track 0 side 0, an SD boot side, and DD everywhere else -- expressed as a LIST, the
    // same way TarbellDdBoard describes its SD-track-0 / DD-rest media. The headLo/headHi
    // pair is what the per-SIDE mixed density needs.
    struct FmtRange {
        int     trackLo, trackHi, headLo, headHi;
        Density density;
        int     sectors, sectorSize, startSector;
    };

    // ---- the axes the leaves answer for themselves (see the class note) ----
    virtual void        buildFdc();                       // 16/64: FD1793. 4FDC (later): FD1771
    virtual bool        hasBankSelect() const { return true; }   // OUT 40H banks the ROM out
    virtual int         romBytes() const { return 4096; }        // 16FDC: 4K at C000-CFFF (64FDC overrides -> 8K)
    virtual std::string romName() const = 0;                     // which RDOS ROM
    // Probe an image's byte count into a geometry + the drive's RPM. 16/64: the mixed 8"
    // CDOS disk, a plain SD disk, or a formattable blank. 4FDC (later): single density only.
    virtual bool describeGeometry(uint64_t bytes, int& tracks, int& heads, bool& interleaved,
                                  int& revsPerSec, std::vector<FmtRange>& ranges,
                                  std::string& err) const;

    // Port 34: disk flags (IN) and disk control (OUT). The 16/64 layout lives here; the
    // 4FDC (no density bit, different flag bits) overrides both.
    virtual uint8_t readPort34();          // DRQ / ¬BOOT / EOJ, with the Auto-Wait collapse
    virtual void    writePort34(uint8_t v);// AUTO WAIT / DENSITY / MOTOR / MAXI / drive select

    // Port 04: the auxiliary disk register (the TMS 5501's parallel pins, wired to disk). The
    // 16/64 layout lives here; the 4FDC (dual eject, no side-select) overrides both.
    virtual uint8_t readAux();             // seek-in-progress / sense switches 5-8
    virtual void    writeAux(uint8_t v);   // ¬SIDE SELECT + PerSci mechanical controls

    void   clockAttached() override;  // hand every drive the machine Clock (index() angular model)
    void   wireClocks();       // (re)point each drive's angular model at clock_
    void   applySelection();   // point the chip at drive_[sel_], set side + data rate
    void   refresh();          // advance both chips, re-drive the wires, re-arm the wake
    uint64_t nextEdge() const; // the next autonomous edge of either chip
    Clock& clk() const;
    void   loadRom();

    // ---- the disk half ----
    std::unique_ptr<Wd17xx> chip_;
    std::vector<Drive>      drive_;

    int       drives_   = 4;      // A-D
    int       sel_      = 0;      // one-hot DS4-DS1 -> a drive index; boot runs against 0
    int       side_     = 0;      // the selected side (driven by port-04 D1 ¬SIDE SELECT)
    bool      maxi_     = true;   // MAXI: true = 8", false = 5.25" (port-34 D4)
    long long dataRate_ = 250000; // the media bit rate (MAXI x DDEN -- see writePort34)
    uint8_t   control_  = 0;      // the port-34 OUT latch (Auto-Wait arm is D7)
    uint8_t   aux_      = 0xFF;   // the port-04 OUT latch (active-low; idle high = no-op)

    // ---- the console UART half (embedded directly, like the SBC's 8251) ----
    Tms5501 uart_{"tms0"};

    // ---- the RDOS boot PROM (host-backed config: re-read on power, never serialized) ----
    std::vector<uint8_t> rom_;   // romBytes() bytes at C000

    // ---- runtime latches (these DO travel in a snapshot) ----
    bool armed_     = true;   // the ROM is mapped until OUT 40H (16/64) banks it out
    bool bootstrap_ = true;   // the BOOT/MON strap: on = boot the disk, and ¬BOOT reads low

    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;

private:
    static constexpr uint16_t kRomBase = 0xC000;
    bool inRomWindow(uint16_t a) const {
        return armed_ && bootstrap_ && a >= kRomBase &&
               (uint32_t)a < (uint32_t)kRomBase + (uint32_t)rom_.size();
    }
};

} // namespace altair
