#pragma once
//
// S100Computers "IDE-AB CF+ESP32" board -- the IDE/CompactFlash half (reference/dual-ide-card.md).
//
// The physical board is TWO interfaces in one: an 8255-based IDE/CompactFlash controller at
// ports 30H-34H (CP/M drives A:/B:) and the byte-identical Dual-SD ESP32 engine at 80H-81H
// (drives C:/D:). This board models ONLY the IDE/CF half; the SD half is `dualsd`, and the two
// compose into the full combination card (examples/dualidesd). The medium is the SAME CardImage
// `.img`/`.geo` card `dualsd` uses -- the CP/M 3 BIOS builds an identical LBA and byte order for
// both halves (reference/dual-ide-card.md section 1), so a card is portable between them.
//
// THE PORT MODEL (confirmed against HIDE3.ASM, the combination CP/M 3 BIOS): the CPU drives an
// 8255 PPI whose three ports fan out to a minimal ATA/IDE register file. Every access is a
// programmed-I/O strobe the BIOS builds by hand on port C's control lines -- there is no DMA and
// no memory window (the CPU board's MASTER monitor loads CP/M; reference section 4).
//
//   +0 portA    (30H)  8255 A -- IDE data LOW byte out, and register read-back in.
//   +1 portB    (31H)  8255 B -- IDE data HIGH byte (16-bit sector transfers only).
//   +2 portC    (32H)  8255 C -- the IDE control lines, driven as a group (see below).
//   +3 portCtrl (33H)  8255 mode config (READcfg8255=0x92 / WRITEcfg8255=0x80). Accepted and
//                      noted; the A/B direction it sets is functionally inert here (portA/B in
//                      and out are separate latches).
//   +4 drivePort(34H)  bit0 = drive select: 0 -> A: (socket 0), 1 -> B: (socket 1).
//
// Control-line bits on port C (HIDE3.ASM): A0=01 A1=02 A2=04 CS0=08 CS1=10 WR=20 RD=40 RST=80.
// With CS0 asserted the register address is `portC & 0x0F`: REGdata=08 REGerr=09 REGseccnt=0A
// REGsector=0B REGcylLSB=0C REGcylMSB=0D REGshd=0E REGcmd/REGstatus=0F.
//
// THE ENGINE is edge-triggered on writes to port C (the BIOS pulses WR/RD high then low):
//   - RST newly asserted (0x80)        -> reset the transfer state.
//   - WR  newly asserted (0x20) & CS0  -> write the register `portC & 0x0F`. REGdata pushes the
//                                         16-bit word {portB, portA} into the sector buffer; on
//                                         the 256th word the buffer commits to the medium. Other
//                                         registers store the byte (sector/cyl/shd build the LBA;
//                                         REGcmd dispatches a command).
//   - RD  newly asserted (0x40) & CS0  -> read the register into the A/B input latches. REGdata
//                                         presents the next buffer word (lo->A, hi->B) and
//                                         advances; REGstatus presents the status byte.
// IN portA / IN portB just return whatever the last RD strobe latched.
//
// LBA = REGsector | REGcylLSB<<8 | REGcylMSB<<16 | (REGshd & 0x0F)<<24; the byte offset is
// LBA*512 on a raw MediaFile (reference section 1). BSY is modeled as always 0 (an instant
// controller), RDY always 1 while a card is present, DRQ set during a transfer phase, ERR in
// bit0 -- exactly what IDEwaitnotbusy `(st&0xC0)==0x40` and IDEwaitdrq `(st&0x88)==0x08` poll.
// An EMPTY socket floats the IDE bus: every register (status included) reads 0xFF, so the BIOS
// init spins on BSY, times out and reports the drive "not Present".

#include "core/board.h"
#include "host/media.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class DualIdeBoard : public Board {
public:
    DualIdeBoard();

    std::string type() const override { return "dualide"; }

    // ---- bus: five contiguous 8255 I/O ports ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // ---- units / [[board.drive]] (the two CF sockets) ----
    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;
    std::vector<UnitDef>     units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    static constexpr int    kDrives     = 2;    // two CF sockets: drive 0 (A:), drive 1 (B:)
    static constexpr size_t kSectorSize = 512;  // reference section 1
    static constexpr size_t kWords      = kSectorSize / 2;  // 256 16-bit words per sector

    // Port-C control-line bits (HIDE3.ASM IDExxxline).
    static constexpr uint8_t kCs0 = 0x08;
    static constexpr uint8_t kWr  = 0x20;
    static constexpr uint8_t kRd  = 0x40;
    static constexpr uint8_t kRst = 0x80;

    // The ATA registers the BIOS addresses (portC & 0x0F, CS0 asserted).
    enum Reg : uint8_t {
        rData   = 0x08, rErr    = 0x09, rSecCnt = 0x0A, rSector = 0x0B,
        rCylLSB = 0x0C, rCylMSB = 0x0D, rShd    = 0x0E, rCmdSt  = 0x0F,
    };

    // ATA command codes (HIDE3.ASM COMMANDxxx).
    enum Cmd : uint8_t {
        cRecal = 0x10, cRead = 0x20, cWrite = 0x30,
        cInit = 0x91, cSpinDown = 0xE0, cSpinUp = 0xE1, cIdentify = 0xEC,
    };

    // Status bits (the subset the BIOS polls).
    static constexpr uint8_t kStBsy = 0x80;   // never set here (instant controller)
    static constexpr uint8_t kStRdy = 0x40;
    static constexpr uint8_t kStDrq = 0x08;
    static constexpr uint8_t kStErr = 0x01;
    static constexpr uint8_t kFloat = 0xFF;   // an empty socket floats the IDE bus

    enum class Phase { Idle, ReadData, WriteData };

    // ---- config / straps (rebuilt from TOML, never serialized) ----
    uint16_t port_ = 0x30;   // 8255 base; the board decodes port_..port_+4

    // ---- the two CF sockets ----
    struct Drive {
        std::unique_ptr<MediaFile> media;   // the CF card standing in (a CardImage, image, ...)
        std::string                path;
    };
    std::vector<Drive> drive_;   // size == kDrives
    int                curDrive_ = 0;

    // ---- 8255 latches ----
    uint8_t portAOut_ = 0;   // last OUT to portA (data low / register byte)
    uint8_t portBOut_ = 0;   // last OUT to portB (data high)
    uint8_t portAIn_  = 0;   // what IN portA returns (latched by the last RD strobe)
    uint8_t portBIn_  = 0;   // what IN portB returns
    uint8_t ctrlCfg_  = 0;   // last 8255 mode config written to portCtrl (inert)
    uint8_t prevCtrl_ = 0;   // last value on portC (for edge detection)

    // ---- the ATA register file ----
    uint8_t  regErr_    = 0;
    uint8_t  regSecCnt_ = 0;
    uint8_t  regSector_ = 0;
    uint8_t  regCylLSB_ = 0;
    uint8_t  regCylMSB_ = 0;
    uint8_t  regShd_    = 0xE0;  // LBA mode, single drive, head 0 (COMMON$INIT default)

    // ---- the transfer engine ----
    Phase    phase_   = Phase::Idle;
    uint32_t wordIdx_ = 0;               // 16-bit word within the current sector (0..kWords)
    uint32_t lba_     = 0;               // LBA latched at command time
    bool     err_     = false;           // last command's ERR bit
    uint8_t  buf_[kSectorSize]{};        // the sector being read from / written to the medium

    // ---- engine helpers ----
    void       writeCtrl(uint8_t v);     // an OUT to portC: run the edge-triggered strobe engine
    void       writeReg8(uint8_t reg, uint8_t val);  // an 8-bit register write (IDEwr8D)
    uint8_t    readReg8(uint8_t reg);    // an 8-bit register read (IDErd8D) -- 0xFF if no card
    void       doCommand(uint8_t cmd);   // dispatch a REGcmd write
    void       pushWriteWord();          // REGdata WR strobe: buffer {portB,portA}, commit at 256
    void       presentReadWord();        // REGdata RD strobe: latch the next word into A/B
    void       commitWrite();            // 256 words gathered -> writeAt + sync
    uint8_t    statusByte() const;       // the REGstatus byte
    uint32_t   computeLba() const;
    void       resetEngine();            // full reset (power / RESET line asserted low elsewhere)
    void       resetTransfer();          // RST line: just the transfer state
    MediaFile* curMedia() const;

    void say(std::string s) { log_.push_back(std::move(s)); }
    std::vector<std::string> log_;
};

} // namespace altair
