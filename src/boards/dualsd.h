#pragma once
//
// S100Computers "Dual SD" card board (reference/dual-sd-card.md).
//
// A PROGRAMMED-I/O COMMAND/HANDSHAKE CONTROLLER, the same architectural shape as the
// 88-HDSK and the iCOM FD3712: the CPU sees two I/O ports and shifts a whole 512-byte
// sector through the data port a byte at a time, under a two-bit handshake. An onboard
// ESP32-S3 is the "firmware" that a real card runs; here the mounted medium stands in
// for the microSD card and the engine below stands in for the ESP32.
//
//   OUT STATUS (80H)  the command byte. Every command is TWO bytes on this port: a lead
//                     33H (a safety sync), then one of the eight command codes.
//   IN  STATUS (80H)  the status byte: bit7 (DI7) = a data byte is waiting to be read;
//                     bit0 = the last written byte has not been taken yet.
//   OUT DATA   (81H)  a byte a command consumes -- a SET_TRK_SEC address byte, or a
//                     WRITE_SECTOR buffer byte.
//   IN  DATA   (81H)  the next byte of a READ_SECTOR transfer (clears DI7; the engine
//                     presents the following byte).
//
// The eight commands (reference section 2): 80/81 INIT drive A/B, 82/83 SELECT drive
// A/B, 84 SET_TRK_SEC, 85 READ_SECTOR, 86 WRITE_SECTOR, 87 FORMAT_SECTOR (fills the
// current sector with E5), 88 RESET.
//
// THE MEDIUM IS ADDRESSED DIRECTLY BY BYTE OFFSET -- readAt/writeAt(lba*512, ...) on a
// raw MediaFile -- NOT through DiskImage. A DirectoryMedia card already owns its own
// geometry (its card.geometry descriptor), so DiskImage's CHS-probe machinery would
// only fight it. Any MediaFile works, though: a plain image, or a MemoryMedia in a test.
//
// The SET_TRK_SEC (84H) argument bytes and the (track,sector)->card-LBA formula were the
// one part of the protocol no fetched page pinned; they are now CONFIRMED from SD_CARD.Z80.
// See the addressing seam (decodeAddr, below).

#include "core/board.h"
#include "host/media.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class DualSdBoard : public Board {
public:
    DualSdBoard();

    std::string type() const override { return "dualsd"; }

    // ---- bus: two I/O ports (STATUS/command + DATA) ----
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

    // ---- units / [[board.drive]] (the two SD sockets) ----
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
    static constexpr int     kDrives     = 2;      // two microSD sockets: drive A:, drive B:
    static constexpr size_t  kSectorSize = 512;    // reference section 4
    static constexpr uint8_t kLead       = 0x33;   // the safety-sync byte before every command
    static constexpr uint8_t kFormatFill = 0xE5;   // FORMAT_SECTOR (87H) writes E5 (reference section 2)
    static constexpr uint8_t kErasedFill = 0xFF;   // a never-written CF/SD sector reads FF (open item 2)

    // The eight command codes (reference section 2).
    enum Cmd : uint8_t {
        cInitA     = 0x80, cInitB  = 0x81,
        cSelA      = 0x82, cSelB   = 0x83,
        cSetTrkSec = 0x84,
        cRead      = 0x85, cWrite  = 0x86,
        cFormat    = 0x87, cReset  = 0x88,
    };

    // What an OUT to the DATA port currently means, and whether an IN has bytes to give.
    enum class Phase { Idle, CollectAddr, ReadXfer, WriteXfer };

    // ---- config / straps (rebuilt from TOML, never serialized) ----
    uint16_t port_ = 0x80;   // STATUS/command; DATA is port_+1

    // ---- the two SD sockets ----
    struct Drive {
        std::unique_ptr<MediaFile> media;   // the microSD card standing in (a DirectoryMedia, image, ...)
        std::string                path;
    };
    std::vector<Drive> drive_;   // size == kDrives
    int                curDrive_ = 0;

    // ---- the command/handshake engine (reference sections 2-3) ----
    bool     armed_    = false;   // a 33H lead was seen; the next STATUS-port byte is the command
    Phase    phase_    = Phase::Idle;
    uint32_t lba_      = 0;       // current sector LBA (set by SET_TRK_SEC -- see decodeAddr)
    uint8_t  buf_[kSectorSize]{}; // the 512-byte sector buffer
    size_t   xferPtr_  = 0;       // next byte a DATA read/write moves

    // SET_TRK_SEC argument collection: two bytes, track then sector (confirmed, see decodeAddr).
    static constexpr int kAddrBytes = 2;
    uint8_t addrBuf_[4]{};
    int     addrPtr_ = 0;

    // ---- engine helpers ----
    void       outCmd(uint8_t v);
    void       outData(uint8_t v);
    uint8_t    inData();
    uint8_t    statusByte() const;
    void       dispatch(uint8_t cmd);
    void       doRead();
    void       doWrite();
    void       doFormat();
    void       resetEngine();
    MediaFile* curMedia() const;

    // THE ADDRESSING SEAM -- reference/dual-sd-card.md §3 (CONFIRMED from SD_CARD.Z80).
    //
    // SET_TRK_SEC (84H) was the ONE part of the protocol no fetched page pinned. The board's
    // own SD_CARD.Z80 test driver settles it: SET_SECTOR sends the CURRENT_TRACK byte first,
    // then CURRENT_SECTOR -- so kAddrBytes = 2, track then sector. The card LBA is
    // track*256 + sector: the driver's "next sector" step loads H=track, L=sector and does a
    // single INC HL, treating the pair as one big-endian 16-bit number that rolls sector into
    // track at 256 (256 sectors per track). The byte offset is LBA * 512. This stays
    // localized in decodeAddr on purpose -- the READ/WRITE/FORMAT mechanics and the handshake
    // never depended on which reading was correct.
    uint32_t decodeAddr() const;

    void say(std::string s) { log_.push_back(std::move(s)); }
    std::vector<std::string> log_;
};

} // namespace altair
