#pragma once
//
// NEC µPD765A -- a floppy disk controller CHIP, NOT A CARD. Intel second-sourced it as
// the 8272. The CompuPro Disk 1A carries one; the Disk 1 and the next board with a 765
// or an 8272 get this for free.
//
// Modeled from the NEC data sheet (Rev/3, 1982), distilled in
// reference/NEC uPD765A FDC.md -- the section numbers below are that file's. NOT from the
// one boot ROM that drives it, and not from any other emulator's 765.
//
// It knows nothing about S-100, ports, DMA addresses or the board's select latch. It has
// A0 (status or data), the DRQ/DACK/TC data path, the INT pin, and US0/US1/HD going out to
// four drives. What is on the far end of those is the board's: see Wiring.
//
// ---------------------------------------------------------------------------
// WHAT THIS CHIP DOES NOT MODEL, AND WHY EACH IS SAFE TO LEAVE OUT.
//
//   - ROTATION. A sector is found at once; there is no wait for it to come round. The
//     only rotation-shaped facts the data sheet gives are "the index hole twice" (an
//     error path, reached here at once) and Read ID's "first ID field" (a cursor that
//     moves one sector per call, so successive Read IDs walk the track).
//   - BYTE TIMING, AND SO OVERRUN. Data moves when the board or the guest asks for it. In
//     DMA mode the board's DMA always keeps up on the real card too; in non-DMA mode a
//     slow guest would get ST1 OR on silicon and does not here. Same reasoning as the
//     wait-synced WD chip (wd17xx.h): nothing that runs correctly on the hardware can tell.
//   - READY CHANGING DURING A COMMAND (ST0 IC = 11 from inside a read or write). A drive
//     here goes not-ready only when the host ejects it.
//   - DELETED DATA MARKS ON AN IMAGE. A raw image records none (host/disk.h). Write
//     Deleted Data writes the data, and the mark is lost; Read Deleted Data then finds a
//     normal mark. The chip itself handles both marks: a drive that reports `deleted`
//     gets ST2 CM and SK exactly as §7 says.
//
// What IS timed: step pulses (SRT, per drive, so seeks overlap), head load (HLT) and
// head unload (HUT), all from Specify and scaled by the chip's clock (§4: at 4 MHz every
// interval doubles).
// ---------------------------------------------------------------------------

#include "chips/floppy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

class Clock;
class StateWriter;
class StateReader;

class Upd765 {
public:
    explicit Upd765(std::string name);

    const std::string& name() const { return name_; }

    // ---- WHAT US0/US1/HD REACH (the board's) ----
    //
    // The chip drives the unit-select and head-select pins; the board decides what they
    // reach. The Disk 1A puts a Drive Select Register between them (it can override the
    // unit and force the Two Side line), which is exactly why this is a callback and not
    // an array of drives.
    //
    //   drive(us)        the drive on the cable when US1/US0 = us, or nullptr (none: NOT
    //                    READY, as an empty connector reads).
    //   head(us, hd)     the HD pin, with `us` selected. The board passes it to the drive
    //                    (DiskImageDrive::setSide).
    //   twoSided(us)     the TS line. Optional: without it, the drive's own twoSided().
    struct Wiring {
        std::function<FloppyDrive*(int us)>     drive;
        std::function<void(int us, int hd)>     head;
        std::function<bool(int us)>             twoSided;
    } wiring;

    // The chip's own time source. Without one every timer is zero: a seek ends at once.
    void attachClock(const Clock* c) { clk_ = c; }

    // CLK, pin 19. 8 MHz for an 8" drive. At 4 MHz (a minifloppy) every Specify interval
    // doubles (§4). The Disk 1A switches it from its Drive Select Register.
    long long fdcClockHz = 8000000;

    // ---- THE TWO REGISTERS (A0) -- §2 ----
    uint8_t readMsr();              // A0 = 0, read
    uint8_t readData();             // A0 = 1, read
    void    writeData(uint8_t v);   // A0 = 1, write (A0 = 0 write is illegal: the board drops it)

    // ---- THE DMA SIDE: DRQ out, DACK and TC in -- §6 ----
    //
    // DRQ is up in DMA mode while the execution phase has a byte for memory, or wants one
    // from it. toMemory() says which way: true for a read, false for a write, Format (the
    // C/H/R/N bytes) or a Scan (the processor's comparison bytes). The board answers a DRQ
    // with one DACK cycle: dackRead() hands it the byte, dackWrite() takes one.
    //
    // TC ends a read, write or scan (§7): a read finishes the sector it is in and sends no
    // more bytes, a write fills the rest of its sector with 00. A board that has no TC
    // source never calls it, and the command runs to EOT and ends with ST1 EN.
    //
    // dackRead() is false when there was no byte after all (the command ended looking for
    // the next sector): the board then writes nothing to memory.
    bool drq() const;
    bool toMemory() const { return toMem_; }
    bool dackRead(uint8_t& out);
    void dackWrite(uint8_t v);
    void terminalCount();

    // INT, pin 18 (§6): the result phase has started; or a byte waits in non-DMA mode; or a
    // seek, recalibrate or ready change waits for Sense Interrupt Status.
    bool intPin() const;

    // RST, pin 1 (§8): idle, FDD outputs low. SRT, HUT and HLT are kept. If drive 0 is
    // ready, the ready-change interrupt the data sheet promises "within 1.024 ms" is
    // raised at once.
    void reset();

    // Power applied: reset, and the Specify values back to zero (no Specify yet).
    void powerOn();

    // ---- TIME ----
    // Run every deadline that has come due (step pulses, the head-load wait). The board
    // calls this before any access and when nextEdge() comes round, and then re-reads
    // intPin() and drq().
    void     poll();
    uint64_t nextEdge() const;  // the next deadline, in T-states, or 0 for none

    // For the board's SHOW and for tests.
    int  pcn(int us) const { return pcn_[(size_t)(us & 3)]; }
    bool nonDma() const { return nd_; }

    // US1/US0 as the last command left them. A board that reads its drive's own lines
    // (the Disk 1A's ready and index bits) needs to know which drive is on the cable.
    int  unitSelect() const { return us_; }

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

    // ---- STATUS BITS (§2, §5), public for the board and the tests ----
    static constexpr uint8_t kMsrRqm = 0x80, kMsrDio = 0x40, kMsrExm = 0x20, kMsrCb = 0x10;

    static constexpr uint8_t kSt0Nt = 0x00, kSt0At = 0x40, kSt0Ic = 0x80, kSt0Ai = 0xC0;
    static constexpr uint8_t kSt0Se = 0x20, kSt0Ec = 0x10, kSt0Nr = 0x08;

    static constexpr uint8_t kSt1En = 0x80, kSt1De = 0x20, kSt1Or = 0x10, kSt1Nd = 0x04;
    static constexpr uint8_t kSt1Nw = 0x02, kSt1Ma = 0x01;

    static constexpr uint8_t kSt2Cm = 0x40, kSt2Dd = 0x20, kSt2Wc = 0x10, kSt2Sh = 0x08;
    static constexpr uint8_t kSt2Sn = 0x04, kSt2Bc = 0x02, kSt2Md = 0x01;

    static constexpr uint8_t kSt3Ft = 0x80, kSt3Wp = 0x40, kSt3Ry = 0x20, kSt3T0 = 0x10;
    static constexpr uint8_t kSt3Ts = 0x08;

private:
    enum class Phase : uint8_t { Command, Wait, Exec, Result };

    // What the execution phase is doing with its bytes.
    enum class Xfer : uint8_t { None, Read, Write, Scan, Format };

    // One per drive (§7 Seek): the chip has four PCN registers, and seeks overlap.
    struct Seek {
        bool     active = false;
        bool     recal  = false;
        int      target = 0;
        int      hd     = 0;  // the HD bit of the command, for ST0
        int      pulses = 0;  // Recalibrate gives up after 77
        uint64_t due    = 0;
    };

    uint64_t now() const;
    uint64_t msT(int ms) const;  // a Specify interval at fdcClockHz, in T-states

    FloppyDrive* drv() const;  // the drive the command addresses (us_)
    bool         driveReady(int us) const;
    bool         driveTwoSided(int us) const;
    void         selectHead();

    void startCommand();
    void execute();     // after the last command byte
    void beginData();   // after the head-load wait: find the first sector
    void readId();
    void invalid();
    void result(int n, bool irq);     // res_[0..n) is ready; n = 7, 2 or 1 bytes
    void endData(uint8_t ic);         // the 7-byte result of a data command
    uint8_t st0(uint8_t ic) const;

    bool findSector(FloppyDrive::SectorId& out);  // C/H/R/N, or sets ST1/ST2 and returns false
    bool legible(const FloppyDrive::SectorId& id) const { return id.doubleDensity == mf(); }
    // The ID field's H: the byte the format wrote, or -- when the drive made the ID up from a
    // raw image's geometry and records none -- the side selected (HD).
    int idHead(const FloppyDrive::SectorId& id) const { return id.head >= 0 ? id.head : hd_; }
    bool openSector();       // the sector at R into buf_; false = the command ended
    bool openTrackSector();  // Read a Track: the next sector round the track
    bool advance();          // R to the next sector (MT to head 1, or EOT); false = ended
    bool nextByte();         // a byte is due: open the next sector if one is waiting
    void sectorDone();       // a sector's bytes have all moved
    void resultIdAfter(bool atEot);  // Table 2
    bool byteOut(uint8_t& v);
    void byteIn(uint8_t v);
    bool moving() const;     // the execution phase has a byte to move now

    void stepDrives();
    void queueSeekEnd(int us, uint8_t st0);
    void pollReady();

    bool mt() const { return (cmd_[0] & 0x80) != 0; }
    bool mf() const { return (cmd_[0] & 0x40) != 0; }
    bool sk() const { return (cmd_[0] & 0x20) != 0; }
    int  op() const { return cmd_[0] & 0x1F; }
    int  dataLen() const;  // bytes per sector the command moves (N, or DTL when N = 0)

    std::string  name_;
    const Clock* clk_ = nullptr;

    // ---- Specify (§4). Kept across reset ----
    uint8_t srt_ = 0, hut_ = 0, hlt_ = 0;
    bool    nd_        = false;
    bool    specified_ = false;  // polling for ready changes starts here (§6)

    // ---- the command in hand ----
    Phase                  phase_ = Phase::Command;
    std::array<uint8_t, 9> cmd_{};
    int                    cmdLen_ = 0;  // bytes the command takes, byte 1 included
    int                    cmdGot_ = 0;
    int     us_ = 0, hd_ = 0;            // from byte 2
    uint8_t c_ = 0, h_ = 0, r_ = 0, n_ = 0, eot_ = 0, gpl_ = 0, dtl_ = 0;
    uint64_t due_ = 0;                   // Phase::Wait: the head-load time is up

    // ---- the execution phase ----
    Xfer                 xfer_  = Xfer::None;
    bool                 toMem_ = false;
    std::vector<uint8_t> buf_;           // the sector moving (or Format's ID bytes)
    size_t               idx_   = 0;
    size_t               want_  = 0;     // bytes of buf_ that cross the data path
    bool                 tc_    = false;
    uint8_t              st1_ = 0, st2_ = 0;
    int                  tracked_ = 0;   // Read a Track: sectors read so far
    int                  idCursor_ = 0;  // Read ID / Read a Track: where the disk is
    FloppyDrive::SectorId cur_{};        // the sector in buf_
    bool                 needNext_ = false;  // a sector ended; the next opens on the next byte
    bool                 cmStop_   = false;  // a control mark: this sector, then end (§7)
    bool                 crcStop_  = false;  // a data CRC error: this sector, then end
    bool                 scanEqual_ = true, scanMet_ = true;
    uint8_t              dr_ = 0;        // the data register's last byte

    // ---- the result phase ----
    std::array<uint8_t, 7> res_{};
    int  resLen_ = 0, resIdx_ = 0;
    bool resultInt_ = false;  // INT for the result phase; the first result byte clears it

    // ---- the drives ----
    std::array<int, 4>     pcn_{};
    std::array<Seek, 4>    seek_{};
    std::array<int, 4>     pending_{};    // ST0 waiting for Sense Interrupt Status, or -1
    std::array<bool, 4>    seekEnd_{};    // ...and it is a seek or recalibrate end (§6)
    std::array<bool, 4>    lastReady_{};  // what the ready poll saw last
    std::array<bool, 4>    busy_{};       // MSR D0B..D3B

    // ---- the head (HDL): loaded on one drive until HUT runs out ----
    int      loadedUs_ = -1;
    uint64_t unloadAt_ = 0;
};

} // namespace altair
