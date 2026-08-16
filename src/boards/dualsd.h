#pragma once
//
// S100Computers "Dual SD" card board (reference/dual-sd-card.md).
//
// A PROGRAMMED-I/O COMMAND/HANDSHAKE CONTROLLER whose behavior is a faithful port of the
// board's own ESP32-S3 firmware (S100_ESP32_Firmware_v1.5). An onboard ESP32 runs that
// firmware; here the engine below stands in for the ESP32 and the mounted medium stands in
// for the microSD card.
//
// THE PORT MODEL (confirmed against the firmware and both Z80 drivers, MASTER.Z80 /
// SD_CARD.Z80): the CPU sees two I/O ports, but EVERY host->board byte -- the 33H lead, the
// command code, AND any argument/write bytes -- goes OUT the DATA port. The STATUS port is
// read-only status; a write to it just flushes any pending read data ("tell the ESP32 no
// data is waiting"). This is exactly the firmware's single input path (loop(): 33H ->
// command -> per-command payload), NOT a separate "command port".
//
//   OUT DATA   (BASE+1)  one byte into the ESP32's input stream: the 33H lead, then a
//                        command code, then that command's argument/write bytes.
//   IN  DATA   (BASE+1)  the next byte the ESP32 is returning: read-sector data, a report
//                        command's payload, then always a trailing STATUS byte.
//   IN  STATUS (BASE+0)  bit7 (DI7) = a byte is waiting to be read; bit2/bit1 = the two sockets'
//                        card-detect lines (bit1 = SD card 1 / drive C:, bit2 = SD card 2 /
//                        drive D:), which the CP/M 3 BIOS reads to decide a drive is present;
//                        bit0 = the last written byte has not been consumed yet (always 0 here
//                        -- we take it at once). A floating bus reads 0xFF = no board at all.
//   OUT STATUS (BASE+0)  flush any pending read byte (the driver's init housekeeping).
//
// EVERY in-range command (80H..97H) ends by returning a STATUS byte (00 = OK, 1A = ERR) that
// the driver reads back -- firmware `SendData(runCmd(cmd))`. RESET (88H) reboots the ESP32
// and returns nothing. The nine core disk commands (reference section 2) plus the firmware's
// report/utility set are modeled:
//   80/81 INIT drive 1/2, 82/83 SELECT drive 1/2, 84 SET_TRK_SEC (track,sector),
//   85 READ (512 data + status), 86 WRITE (512 data -> status), 87 FORMAT (16-bit sector
//   count, skips sector 0, E5 fill), 88 RESET, 90 FWVER, 91 SETLBA (32-bit), 92 TYPE,
//   93 CAP, 94 CID, 95 CSD, 96 DISP, 97 ECHO.
//
// ADDRESSING (firmware getTrkSec): SET_TRK_SEC sends the TRACK byte first, then the SECTOR
// byte, and the current sector is track*256 + sector (track high, sector low). SETLBA sends a
// full 32-bit LBA, MS byte first. The medium is addressed DIRECTLY by byte offset --
// readAt/writeAt(lba*512, ...) on a raw MediaFile -- NOT through DiskImage. A CardImage
// card already owns its own geometry; any MediaFile works (a plain image, or a MemoryMedia in
// a test).

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

    // ---- bus: two I/O ports (STATUS + DATA) ----
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
    static constexpr int     kDrives     = 2;      // two microSD sockets: drive 1 (C:), drive 2 (D:)
    static constexpr size_t  kSectorSize = 512;    // reference section 4
    static constexpr size_t  kBufMax     = 1024;   // firmware bufData size -- caps WRITE (512) and ECHO
    static constexpr uint8_t kLead       = 0x33;   // the safety-sync byte before every command
    static constexpr uint8_t kFormatFill = 0xE5;   // FORMAT (87H) writes E5 (reference section 2)
    static constexpr uint8_t kStatOk     = 0x00;   // firmware STAT_OK
    static constexpr uint8_t kStatErr    = 0x1A;   // firmware STAT_ERR

    // Firmware identity reported by FWVER (S100_ESP32_Firmware_v1.5, board id 2 = Dual SD).
    static constexpr uint8_t kBoardId  = 2;
    static constexpr uint8_t kFwMajor  = 1;
    static constexpr uint8_t kFwMinor  = 5;
    static constexpr uint8_t kCardType = 0;   // no physical SD card is modeled (TYPE reports 0)

    // The command codes (firmware CMD_*; reference section 2 for the disk subset).
    enum Cmd : uint8_t {
        cInit1 = 0x80, cInit2 = 0x81,
        cSel1  = 0x82, cSel2  = 0x83,
        cSetTrkSec = 0x84,
        cRead  = 0x85, cWrite = 0x86,
        cFormat = 0x87, cReset = 0x88,
        cFwVer = 0x90, cSetLba = 0x91,
        cType = 0x92, cCap = 0x93, cCid = 0x94, cCsd = 0x95,
        cDisp = 0x96, cEcho = 0x97,
    };

    // The ESP32's input state machine (firmware loop() + runCmd()). A command may pull a fixed
    // count of following bytes (Collect), a NUL-terminated string (DISP), or a length-prefixed
    // blob (ECHO), before it runs and queues its reply.
    enum class In { Idle, Cmd, Collect, CollectDisp, CollectEchoLen, CollectEchoData };

    // ---- config / straps (rebuilt from TOML, never serialized) ----
    uint16_t port_ = 0x80;   // STATUS/command base; DATA is port_+1

    // ---- the two SD sockets ----
    struct Drive {
        std::unique_ptr<MediaFile> media;   // the microSD card standing in (a CardImage, image, ...)
        std::string                path;
    };
    std::vector<Drive> drive_;   // size == kDrives
    int                curDrive_ = 0;

    // ---- the command/handshake engine ----
    In       in_      = In::Idle;   // where we are in the host->board byte stream
    uint8_t  cmd_     = 0;          // the command currently collecting its argument bytes
    uint32_t lba_     = 0;          // current sector (SET_TRK_SEC 16-bit, or SETLBA 32-bit)
    uint32_t need_    = 0;          // bytes still to collect for the current command
    uint32_t got_     = 0;          // bytes collected so far
    uint32_t echoLen_ = 0;          // ECHO payload length
    uint8_t  inBuf_[kBufMax]{};     // collected argument / WRITE / ECHO bytes

    // ESP32 -> host output FIFO (read-sector data / report payloads, then a trailing STATUS).
    uint8_t  out_[kBufMax + 1]{};
    uint32_t outLen_ = 0;
    uint32_t outPtr_ = 0;

    // The SENDACT (DI7) flip-flop is a PER-BYTE handshake, not a "data remains" level. The
    // firmware's SendData() presents ONE byte, then blocks until the CPU has read it before
    // presenting the next (`while (SENDACT == HIGH);`). The hardware sets SENDACT when the ESP32
    // writes a byte and clears it the instant the CPU reads the DATA port -- so DI7 drops to 0
    // between every byte and only rises again for the next. A strict driver (CP/M 3's CPMLDR)
    // reads a byte then spins on DI7 GOING LOW before it reads the next; if DI7 never dropped it
    // would hang forever. `readGap_` models that low interval: the first STATUS read after a DATA
    // read returns DI7=0 (the ESP32 has not yet presented the next byte), then it reflects
    // "bytes remain" again. The laxer MASTER monitor never waits for the drop, so both drivers
    // work. (reference/dual-sd-card.md section 3; S100_ESP32_Firmware_v1.5 SendData/readData.)
    bool     readGap_ = false;

    // ---- engine helpers ----
    void       feedInput(uint8_t v);      // one OUT-DATA byte into the input stream
    void       startCommand(uint8_t cmd); // dispatch on a freshly received command code
    void       completeCollect();         // a fixed-count Collect finished -> run the command
    uint8_t    inData();                  // one IN-DATA byte from the output FIFO
    uint8_t    statusByte();              // the STATUS-port byte (DI7 = a byte is presented now)
    void       beginReply();              // clear the output FIFO before queueing a reply
    void       queueByte(uint8_t b);      // append a byte to the output FIFO
    void       reply(uint8_t status);     // no-payload command: just queue STATUS, go Idle
    uint8_t    doInit(int drive);         // INIT: select a drive, report OK/ERR
    uint8_t    doRead();                  // READ: queue 512 data bytes, return STATUS
    uint8_t    doWrite();                 // WRITE: commit inBuf_ to the medium, return STATUS
    uint8_t    doFormat(uint32_t count);  // FORMAT: E5-fill `count` sectors from lba_, return STATUS
    void       resetEngine();
    MediaFile* curMedia() const;

    void say(std::string s) { log_.push_back(std::move(s)); }
    std::vector<std::string> log_;
};

} // namespace altair
