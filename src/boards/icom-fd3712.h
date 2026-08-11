#pragma once
//
// iCOM FD3712 / FD3812 8" floppy interface (reference/iCOM FD3712 & FD3812 Floppy Disk
// Systems.md, docs/boards/icom-fd3712.md).
//
// A PROGRAMMED-I/O COMMAND/HANDSHAKE CONTROLLER, NOT A WD177x CARD. The iCOM "S-100
// Interface" board (aka "8800 Interface") sits between a MITS 8800 and an iCOM/Pertec
// FD3712 (single-density) or FD3812 (double-density) 8" floppy cabinet. The Altair talks
// to it through just TWO I/O ports and a boot PROM in high memory -- architecturally the
// 88-HDSK's cousin, not the Tarbell's: the controller buffers a WHOLE sector and the CPU
// shifts bytes through the data port, and the OS's disk driver lives in the PROM.
//
//   OUT C0  CMDOUT   the command word. Bit 0 is the command strobe; bit 6 selects what the
//                    next IN C0 returns.
//   IN  C0  DATAIN   read-buffer byte when the last OUT C0 had bit 6 set (cRDBUF/cSHIFT),
//                    otherwise the status byte (reference section 4).
//   OUT C1  DATAOUT  a byte a following command consumes: track (cSETTRK), unit+sector
//                    (cDRVSEC), a write-buffer byte (cWRTBUF), or the cLDCFG density latch.
//
// ONE ENGINE, TWO DENSITIES. The FD3812 is a strict superset of the FD3712 -- identical
// command word, C0/C1 handshake and status byte, plus double density (256-byte sectors,
// track 0 stays single) and a Load Configuration command. So this is ONE command engine
// with density carried by the mounted disk's per-track geometry (reference section 8).
// The three shipped boot PROMs (FD3712 CP/M at F000, FDOS at C000, FD3812 CP/M at F000)
// all drive these same two ports; a config picks one with `rom = "builtin:..."`.
//
// THE INTERFACE BOARD CARRIES ITS OWN SCRATCH RAM. A 128-byte 6810 sits at ROM base + 0x400
// (F400 for the CP/M PROMs, C400 for FDOS) and holds the PROM's BIOS jump vectors, its
// working variables and (on the FD3812) its local stack. The board provides it, so a machine
// needs only 48K of main RAM plus this card to boot -- see machines/icom.toml.
//
// The read/write BUFFER protocol differs subtly between the two PROM generations -- the
// FD3712 advances the read pointer with an explicit cSHIFT between bytes, the FD3812 reads
// bytes back-to-back after one cRDBUF -- but both are satisfied by one rule: an IN in
// read-buffer mode returns the current byte and advances the pointer; cREAD (re)fills the
// buffer and resets it; cRDBUF/cSHIFT never reset it. The write side unifies the same way.
// See the engine in icom-fd3712.cpp.

#include "core/board.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class IcomFdBoard : public Board {
public:
    IcomFdBoard();

    std::string type() const override { return "icom"; }

    // ---- bus: two I/O ports (C0/C1) + the boot PROM and scratch-RAM windows ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;
    bool    peek(uint16_t addr, uint8_t& out) const override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;
    std::vector<MapEntry> memMap() const override;

    // ---- units / [[board.drive]] ----
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
    // ---- geometry (reference section 6) ----
    static constexpr int      kTracks       = 77;
    static constexpr int      kSectors      = 26;
    static constexpr uint64_t kSdBytes      = (uint64_t)kTracks * kSectors * 128;  // 256,256
    // DD: track 0 stays single density (128), tracks 1-76 double (256).
    static constexpr uint64_t kDdBytes      = (uint64_t)kSectors * 128 +
                                              (uint64_t)(kTracks - 1) * kSectors * 256;  // 509,184
    static constexpr int      kMaxDrives    = 4;
    static constexpr size_t   kBufBytes     = 256;    // a whole DD sector; SD uses the low 128

    // ---- the boot PROM window and the 6810 scratch RAM, both derived from the ROM image ----
    static constexpr uint32_t kRomSize   = 0x400;  // a 1K PROM (F000-F3FF / C000-C3FF)
    static constexpr uint32_t kRamOffset = 0x400;  // the 6810 sits 1K above the PROM base
    static constexpr uint32_t kRamSize   = 0x100;  // the 128-byte 6810 (low half of the page)

    // ---- config / straps (rebuilt from TOML, never serialized) ----
    uint16_t    port_    = 0xC0;
    std::string romName_ = "builtin:icom-fd3712-cpm";
    int         drives_  = 2;

    // ---- the boot PROM + scratch RAM the card carries ----
    std::vector<uint8_t> rom_;          // kRomSize bytes; 0xFF where the image is unprogrammed
    uint8_t   ram_[kRamSize]{};         // 6810 scratch (BIOS vectors, variables, FD3812 stack)
    uint32_t  romBase_   = 0xF000;      // established by loadRom() from the decoded image
    uint32_t  ramBase_   = 0xF400;      // romBase_ + kRamOffset
    bool      romLoaded_ = false;

    // ---- mounted media, one DiskImage per drive ----
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string path;
    };
    std::vector<Drive> drive_;  // size == drives_

    // ---- the command/handshake engine (reference sections 2-5) ----
    uint8_t  lastCmd_     = 0;      // last byte written to C0 (bit 6 -> IN C0 returns read buffer)
    uint8_t  lastDataOut_ = 0;      // last byte written to C1
    int      track_       = 0;      // cSETTRK / cRESTOR
    int      unit_        = 0;      // cDRVSEC bits 7:6
    int      sector_      = 1;      // cDRVSEC bits 5:0 (1..26)
    uint8_t  config_      = 0;      // cLDCFG: bit4 double density, bit5 format mode

    uint8_t  readBuf_[kBufBytes]{};
    size_t   readPtr_     = 0;      // next byte IN C0 returns in read-buffer mode
    size_t   readLen_     = 0;      // bytes cREAD/cRDCRC placed in readBuf_ (128 or 256)
    uint8_t  writeBuf_[kBufBytes]{};
    size_t   writePtr_    = 0;      // next slot a cWRTBUF push fills
    bool     wbufMode_    = false;  // FD3812 streaming: OUT C1 pushes into the write buffer

    bool     crcErr_      = false;  // status bit 3, latched, cleared by cCLRERR/cRESET
    bool     ddam_        = false;  // status bit 7, latched, cleared by cCLRERR/cRESET

    // ---- engine helpers ----
    void    outCmd(uint8_t v);
    void    outData(uint8_t v);
    uint8_t inC0();
    uint8_t statusByte() const;
    void    doRead();
    void    doWrite();
    void    pushWrite(uint8_t b);
    void    resetController();
    DiskImage* currentImg() const;

    // ---- ROM/mount helpers ----
    void loadRom();
    bool inRom(uint16_t a) const { return romLoaded_ && a >= romBase_ && a < romBase_ + kRomSize; }
    bool inRam(uint16_t a) const { return romLoaded_ && a >= ramBase_ && a < ramBase_ + kRamSize; }
    void say(std::string s) { log_.push_back(std::move(s)); }

    std::vector<std::string> log_;
};

} // namespace altair
