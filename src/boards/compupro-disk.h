#pragma once
//
// CompuPro Disk 1 / Disk 1A -- what the two floppy controller boards share. See
// reference/CompuPro Disk 1 & 1A Floppy Controllers.md; the section numbers below are that
// file's.
//
// THE FAMILY IS ONE BASE + TWO LEAVES (the Cromemco FDC pattern, boards/cromemco-fdc.h):
//
//   CompuProDiskBoard : Board         the 765, the DMA engine, the boot ROM window, the
//     |                               four drives and the size probe; ports 0-2
//     |-- CompuProDisk1Board          1981: a 2716 of 256-byte routines; port 3 is a
//     |                               bit-banged serial line (boards/compupro-disk1.h)
//     `-- CompuProDisk1aBoard         1984: a 2764 of 512-byte routines; the Drive Select
//                                     and Motor Control Registers (boards/compupro-disk1a.h)
//
// WHAT THE BASE OWNS (§1: the same design on both boards):
//   - FOUR PORTS at a base that is a multiple of four (default C0), §2. The base answers
//     port 0 IN (the 765's main status), port 1 (the 765's data) and port 2 OUT (the DMA
//     address, 3 bytes, MSB first). Port 0 OUT, port 2 IN and port 3 are the leaf's.
//   - A DMA ENGINE. The 765's DRQ asks for the bus; the board takes it and moves one byte
//     per granted cycle to or from the address in a 24-bit counter. There is no byte count
//     and no TC: a read runs to the EOT the guest gave, which is why the CompuPro ROMs take
//     ST0 = 40H with ST1 = EN as a good read.
//   - A BOOT ROM. One routine of the EPROM, picked by switches (`routine`), answers memory
//     reads at 0000 over PHANTOM* until the guest writes D0 = 0 to port 3. Any reset turns
//     it on again (§3).
//
// WHAT IS NOT MODELED, AND WHY EACH IS SAFE TO LEAVE OUT:
//   - A16-A23. The counter counts all 24 bits, but this is a 64 K bus (DESIGN.md 4.0), so
//     only A0-A15 reach memory.
//   - WAIT STATES AND DMA PRIORITY. One bus master and no slow memory: nothing to wait for
//     and nobody to arbitrate with. A DMA byte costs one 3-state bus cycle.
//   - THE DATA RATE AGAINST THE MEDIA. A 5.25" disk read at the 8" rate fails on the board;
//     here the rate sets only the 765's Specify timing.

#include "boards/floppy-drive.h"
#include "chips/upd765.h"
#include "core/board.h"
#include "host/disk.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class CompuProDiskBoard : public Board {
public:
    // What differs between the two boards that the base needs to know.
    struct Traits {
        const char* name;        // "Disk 1A", for messages
        uint32_t    romBytes;    // the whole EPROM
        uint32_t    romWindow;   // one routine, at the Z80/8080 reset address
        const char* romDefault;  // "builtin:..."
        const char* romPart;     // "a 2764 (8 KB, 16 routines of 512 bytes)", for an error
        const char* romHelp;
        const char* baseSwitch;  // "S3-2..7", for the help text
        const char* routineHelp;
        const char* bootHelp;
        int         routineDefault;  // the first routine for an 8080/Z80
    };

    explicit CompuProDiskBoard(const Traits& t);
    ~CompuProDiskBoard() override;

    // ---- the bus: the four ports, and the boot ROM's memory reads ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;
    bool    assertsPhantom(const BusCycle& c) const override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    bool    assertsInt() const override;
    uint8_t assertsVi() const override;

    // ---- pHOLD: the 765's DRQ, in DMA mode (HAS-A Mover, as the Tarbell DD) ----
    bool       requestsBus() const override;
    BusMaster* busMaster() override { return &mover_; }

    void reset(Reset r) override;
    void power() override;
    void pump() override;
    void configChanged() override;
    void clockAttached() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // Bad sectors on a mounted disk (#966): FAULT, the machine file's `faults`, and a `fault`
    // debug flag that writes one line each time one is used.
    bool        takesFaults(const std::string& unit) const override;
    DiskFaults* faults(const std::string& unit) override;
    std::vector<std::string> debugFlags() const override { return {"fault"}; }
    std::vector<Property> mountKeys(const std::string& unit) const override;
    bool mountWith(const std::string& unit, const std::string& path, bool ro, const KeyValues& keys,
                   std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    // For tests, without going through the bus.
    Upd765&  chip() { return chip_; }
    bool     romEnabled() const { return romOn_; }
    uint32_t dmaAddress() const { return dma_; }

    static constexpr int kDrives = 4;

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;
    Property mediaKey() const;  // `media`: a [[board.drive]] key and a MOUNT option

    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string    path;           // as WRITTEN (round-trips through SAVE)
        bool           eightInch = true;
        std::string    forced;         // `media`, or empty: the size decides
        DiskImageDrive drv;
    };

    // ---- the leaf's half of the board ----
    // Does the board drive the bus for this port offset (2 or 3) in this direction?
    virtual bool    decodesPort(uint8_t off, bool write) const = 0;
    // An IN from port 2 or 3.
    virtual uint8_t readPort(uint8_t off) = 0;
    // An OUT to port 0, or to port 3 (after the base has handled D0, the ROM disable).
    virtual void    writePort(uint8_t off, uint8_t v) = 0;
    // The unit the drive cable carries for the 765's US1/US0.
    virtual int     cableUnit(int us) const { return us & 0x03; }
    // The 765's TS input held high, whatever the disk says.
    virtual bool    forceTwoSided() const { return false; }
    // The 765's clock: 8 MHz for 8" drives, 4 MHz for 5.25".
    virtual long long fdcClockHz() const = 0;
    // Which disk sizes MOUNT takes.
    virtual bool    takes8() const { return true; }
    virtual bool    takes525() const { return true; }
    // A byte the board answered on any port.
    virtual void    touched() {}
    // The leaf's registers, at every reset, before the 765 is reset.
    virtual void    resetRegisters() = 0;
    // The leaf's properties, between `boot` and `interrupt`.
    virtual void    boardProperties(std::vector<Property>& p) = 0;

    void applyDataRate() { chip_.fdcClockHz = fdcClockHz(); }
    void refresh();  // the chip up to now, the wires re-driven, the next deadline armed
    bool loadRom(const std::string& spec, std::string& err);

    // SNAPSHOT/RESTORE helpers for the leaf's serialize(): the drives' heads.
    void serializeHeads(StateWriter& w) const;
    void deserializeHeads(StateReader& r);

    StepResult transferOne(Bus& bus);

    // HAS-A, NOT IS-A: Machine::master()'s dynamic_cast must not take the board for a CPU.
    struct Mover : public BusMaster {
        CompuProDiskBoard* d = nullptr;
        StepResult step(Bus& bus) override { return d->transferOne(bus); }
    } mover_;

    const Traits traits_;

    Upd765 chip_{"fdc"};
    std::array<Drive, kDrives> drive_;

    // ---- straps ----
    uint8_t     port_    = 0xC0;
    std::string romSpec_;
    int         routine_ = 0;
    bool        boot_    = true;
    IrqJumper   irq_     = IrqJumper::None;

    // ---- runtime (snapshot) ----
    uint32_t dma_   = 0;      // the 24-bit DMA address counter
    bool     romOn_ = true;   // the boot ROM flip-flop

    std::vector<uint8_t>     rom_;  // the whole EPROM; `routine` picks a page
    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;

private:
    bool probe(DiskImage& img, const std::string& forced, bool& eightInch,
               std::string& err) const;
};

} // namespace altair
