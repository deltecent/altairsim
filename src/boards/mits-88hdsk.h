#pragma once
//
// MITS 88-HDSK -- the "Datakeeper" hard disk controller (reference/88-HDSK.md,
// docs/boards/mits-88hdsk.md).
//
// NOT A HARD-SECTOR CARD. The two floppy controllers (88-DCDD, 88-MDS) are
// HardSectorFdc: an unusually raw card where software steps the head, watches the
// sector counter turn, and shifts 137-byte slots through a data port in real time.
// The Datakeeper is the opposite -- a "controller does the work" card, like a
// Tarbell. It is an outboard box with its own 8X300 processor that presents a
// COMMAND/HANDSHAKE PORT BLOCK to the Altair through an 88-4PIO, decodes 8 ports at
// A0h..A7h, and moves whole 256-byte sectors for you between the disk and one of
// four internal buffers. So this inherits Board directly and reuses only the image
// layer (DiskImage/MediaFile) and the [[board.drive]] machinery -- there is no
// register model to share with the floppy cards.
//
// THE PROTOCOL (reference/88-HDSK.md sections 4-6; roms/HDBL/HDBL.ASM drives it):
//
//   A0 CREADY  IN  bit7 = controller ready for a new command
//   A1 CSTAT   IN  error/status flags; READING resets CREADY low
//   A2 ACSTA   IN  bit7 = command acknowledged
//   A3 ACMD    OUT command HIGH byte -- writing it INITIATES the command
//              IN  resets the command-ack
//   A4 CDSTA   IN  bit7 = a read byte is ready at CDATA
//   A5 CDATA   IN  read-buffer / read-status data
//   A6 ADSTA   IN  bit7 = the write port (ADATA) will take a byte
//   A7 ADATA   OUT command LOW byte / write-buffer data
//
// A command is a 16-bit word: the low byte is written to A7, then the high byte to
// A3 (which starts it). The opcode is bits 15:12. Seven commands (section 5):
// Seek(0), Write Sector(2), Read Sector(3), Write Buffer(4), Read Buffer(5),
// Read Status(6), Set Byte(8). Read/Write Sector move a disk sector to/from one of
// four 256-byte BUFFERS; Read/Write Buffer stream a buffer to/from the Altair
// through A5/A7. The boot is: Seek cyl 0 -> Read Sector into buffer 0 -> Read Buffer
// out to memory, repeated for each page named by the disk's descriptor page.
//
// NO REAL LATENCY IS MODELLED. A command runs synchronously the moment its high byte
// is written, and CREADY is asserted immediately -- which is period-faithful: the
// errata says the firmware sets ready ~1-4.5 us after the strobe, "fast enough that
// a driver need not spin-wait" (reference/88-HDSK.md, section 4). So every guest
// poll loop terminates in one pass, and the data-ready bits (A4/A6) pace the byte
// streams.
//
// THE IMAGE is a linear .DSK of one PLATTER: 406 cylinders x 2 sides x 24 sectors x
// 256 bytes = 4,988,928 bytes, laid out offset = (cyl*48 + side*24 + sector)*256.
// That is exactly a DiskImage with init(406, 2, interleaved=true) + a single 24-
// sector 256-byte format, so (cylinder,side,sector) maps 1:1 onto readSector(t,h,s).
// A logical drive is one platter (reference/88-HDSK.md section 3); a physical unit
// carries up to two, so slot = unit*2 + platter. The bootable CP/M image is one
// platter in slot 0.

#include "core/board.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class HdskBoard : public Board {
public:
    HdskBoard();

    std::string type() const override { return "hdsk"; }

    // ---- bus: 8 ports A0..A7 ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

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
    // Geometry of one Pertec platter (reference/88-HDSK.md section 2).
    static constexpr int      kCylinders   = 406;
    static constexpr int      kSides       = 2;   // head surfaces per platter
    static constexpr int      kSectors     = 24;
    static constexpr int      kSectorBytes = 256;
    static constexpr uint64_t kPlatterBytes =
        (uint64_t)kCylinders * kSides * kSectors * kSectorBytes;  // 4,988,928
    static constexpr int kPlattersPerUnit = 2;   // removable + fixed
    static constexpr int kMaxDrives       = 8;   // 4 units x 2 platters
    static constexpr int kBuffers         = 4;   // the controller's internal page buffers

    // ---- config / straps (rebuilt from TOML, never serialized) ----
    uint16_t  port_   = 0xA0;
    int       drives_ = 1;
    IrqJumper irq_    = IrqJumper::None;  // decoded, not wired -- HDBL never interrupts

    // ---- mounted media, one DiskImage per (unit,platter) slot ----
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string path;
    };
    std::vector<Drive> drive_;  // size == drives_; slotFor(unit,platter) indexes it

    // ---- the four internal 256-byte controller buffers ----
    uint8_t buffers_[kBuffers][kSectorBytes]{};

    // ---- per-unit current cylinder, set by Seek ----
    int cyl_[4]{0, 0, 0, 0};

    // ---- command assembly (A7-low-then-A3-high) ----
    uint8_t  pendingLow_ = 0;  // last byte staged to A7 while Idle
    uint16_t command_    = 0;  // last assembled 16-bit word (diagnostic / snapshot)

    // ---- streaming (Read/Write Buffer, Read Status, Set Byte) ----
    enum class Phase : uint8_t { Idle, ReadStream, WriteStream, StatusStream, SetByte };
    Phase   phase_     = Phase::Idle;
    int     xferBuf_   = 0;  // buffer bound to a Read/Write Buffer stream
    int     xferPos_   = 0;  // next byte index in the stream
    int     xferCount_ = 0;  // total bytes to move (a low byte of 0 means 256)
    uint8_t ivAddr_    = 0;  // IV-byte address latched for Read Status / Set Byte
    uint8_t statusByte_ = 0; // the single byte a Read Status stream returns

    // ---- error/status byte (A1) and handshake ready bits (each is bit7 of its port) ----
    uint8_t status_  = 0xFF;  // section 6: all error bits read 1 on first read after power-on
    bool    cready_  = true;  // A0 CREADY
    bool    ack_     = false; // A2 ACSTA
    bool    cdReady_ = false; // A4 CDSTA (read data at A5)
    bool    adReady_ = true;  // A6 ADSTA (A7 will take a byte)

    // ---- helpers ----
    int  slotFor(int unit, int platter) const;
    void dispatch(uint8_t high);
    void doSeek(uint16_t w);
    void doSector(uint16_t w, bool write);
    void beginBufferStream(uint16_t w, bool write);
    bool sectorAccess(int unit, int platter, int side, int cyl, int sector, uint8_t* buf,
                      bool write);
    void say(std::string s) { log_.push_back(std::move(s)); }

    std::vector<std::string> log_;
};

} // namespace altair
