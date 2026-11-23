#pragma once
//
// FarmTek FDC+ -- the Altair floppy controller that can run with NO DRIVE AT ALL
// (docs/boards/farmtek-fdcplus.md).
//
// The FDC+ is a modern, one-board replacement for the two-board 88-DCDD and 88-MDS. A switch
// bank (S3, "Drive Type", latched at power-on) picks what it drives. THIS BOARD DOES THREE:
//
//   type 5  the 1.5 MB floppy -- a whole 10,240-byte track per read, from a disk image in
//           drive0..drive3. A different machine again, so it is its own engine, FdcPlusHdf
//           (farmtek-fdcplus-hdf.h); everything below this paragraph is the serial drive.
//   types 6 and 7, the SERIAL DRIVE: no rotating media at all. The card's high-speed serial port talks to
// a DRIVE SERVER on a PC that holds the disk images, and the card fetches and writes back ONE
// WHOLE TRACK at a time:
//
//   type 7  serial drive as an Altair 8" drive -- 77 tracks, or the 8 MB drive's 2048
//   type 6  serial drive as an Altair Minidisk -- 35 tracks, 16 sectors
//
// WHY THIS IS NOT A HardSectorFdc. The 88-DCDD and the 88-MDS are TTL around a spinning disk:
// the sector under the head is a reading off the clock, and bytes come off the medium one
// every 32 (or 64) us whether anyone is looking or not. The serial drive has no medium. Its
// registers are driven by a PIC24 (firmware serialDrive.s), and they behave the way that
// firmware makes them behave:
//
//   - the sector number moves on only when the 8080 READS the sector port, a sector time has
//     passed, AND that sector has arrived from the server. Sector true lasts for ONE read.
//   - NRDA is true the moment the 8080 sees sector true, and a byte is ready every time the
//     8080 takes one -- no byte clock. ENWD is true from WRITE ENABLE to the next sector read.
//   - a sector-port read on a track that is not in the buffer asks the server for it, and the
//     port reads FF until the track starts to arrive.
//
// Every 88-DCDD/MDS driver works against this -- that is the card's whole point -- but it is
// a different machine, so it is its own board, modeled on the firmware and nothing else.
//
// THE ORACLE is the FDC+ firmware v1.8 source, serialDrive.s (M. Douglas), distilled in
// reference/FDC+ Serial Drive Firmware.md; the wire protocol is
// reference/FDC_Serial_Drive_Protocol.md.
//
// THE LINK NEVER RUNS IN A BUS CYCLE. A port access only sets flags -- "this track is wanted",
// "the buffer is dirty" -- and pump() talks to the server a message at a time without waiting,
// exactly the split the firmware makes between its port ISR and its idle loop.

#include "boards/farmtek-fdcplus-hdf.h"
#include "core/board.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class FdcPlusBoard : public Board {
public:
    FdcPlusBoard();
    ~FdcPlusBoard() override;  // out of line: it owns a unique_ptr<ByteStream>

    std::string type() const override { return "fdcplus"; }

    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // The PIC restarts at POWER, and only then: the firmware has no bus-RESET handler, so a
    // front-panel RESET leaves the card exactly as it was.
    void reset(Reset) override {}
    void power() override;
    void pump() override;

    std::vector<Property> properties() override;
    std::vector<MapEntry> ioMap() const override;

    // `error` is the link's faults; `fault` is a bad spot used on a type-5 disk (#969).
    std::vector<std::string> debugFlags() const override {
        return {"seek", "link", "error", "fault"};
    }
    enum DebugFlag { SEEK = 0, LINK = 1, LINKERR = 2, FAULT = 3 };  // not ERROR: a Windows macro

    // Bad spots, on the type-5 drives only. The serial drive has no medium: its tracks come
    // from a server's file, so `line` takes no faults.
    bool        takesFaults(const std::string& unit) const override;
    DiskFaults* faults(const std::string& unit) override;

    // The serial unit, the card's J2 connector, for types 6 and 7 -- the server mounts those
    // images -- and the four drives of type 5, drive0..drive3, which MOUNT fills.
    std::vector<UnitDef> units() const override;
    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property> subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>  subUnits() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;
    uint64_t    rxBytes() const override { return rxBytes_; }  // track data only -- see step()

    std::vector<std::string> drainLog() override;

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

public:
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // The HOST clock the link's timers read (the 0.1 s STAT interval, the 1 s timeout), in
    // nanoseconds. Defaults to steady_clock; a test drives it.
    void setHostNs(std::function<uint64_t()> f) { hostNs_ = std::move(f); }

    // ---- for tests ----
    int      driveType() const { return type_; }
    uint16_t readyMask() const { return ready_; }
    bool     dirty() const { return dirty_; }
    bool     linkIdle() const { return link_ == Link::Idle; }
    int      track(int drive) const { return track_[(size_t)(drive & 7)]; }
    const FdcPlusHdf& hdf() const { return hdf_; }

    static constexpr uint64_t kStatNs     = 100'000'000;    // STAT_RATE: 0.1 s
    static constexpr uint64_t kTimeoutNs  = 1'000'000'000;  // RESPONSE_TIMEOUT: 1 s
    static constexpr int      kWriteTries = 3;              // WRITE_TRIES
    static constexpr int      kSlot       = 137;            // BYTES_PER_SECTOR

private:
    enum class Link { Idle, Stat, Read, WritAsk, WritSta };

    bool     hd() const { return type_ == 5; }
    bool     mini() const { return type_ == 6; }
    int      sectors() const { return mini() ? 16 : 32; }
    int      trackLen() const { return sectors() * kSlot; }
    uint64_t sectorT() const;  // one sector time, in T-states
    uint64_t now() const;
    uint64_t hostNs() const;
    bool     driveReady() const { return !(status_ & 0x08); }  // dsREADY, asserted LOW
    uint16_t curKey() const { return (uint16_t)((cur_ << 12) | (track_[(size_t)cur_] & 0x0FFF)); }

    void latch();           // power-on: read the switches, clear everything
    void sync();            // let the card's own timers catch up with emulated time
    void select(uint8_t v);
    void command(uint8_t v);
    void sectorRead();      // chkSector, after the 8080 has read the sector port
    void dataRead();        // chkReadData
    void dataWrite(uint8_t v);
    void touch();           // activity: restart both idle counts
    void hostIdle();        // count wall-clock idle time, in pump()
    void idleChecks();      // the flush/invalidate and the Minidisk's 6.4 s turn-off

    // ---- the link ----
    bool connected() const { return connectSpec_ != "null"; }
    bool applyEndpoint(const std::string& endpoint, std::string& err);
    void step();            // one non-blocking turn of the link
    void transmit();        // move what queued bytes the line will take
    void drain();           // the dirty track goes home: DISCONNECT, POWER, quit
    void send(const char* cmd, uint16_t p1, uint16_t p2);
    void sendTrack();
    void startWrite();
    void startRead();
    const char* takeReply(const char* cmd, uint16_t& code, uint16_t& data);  // nullptr = good
    bool faultsOn();  // `error` or `link`: the link's faults are reported
    void writeFault(const char* what, const char* why);
    void flushRx();
    void finishWrite(bool ok);
    void resetLink();
    void say(std::string s) { log_.push_back(std::move(s)); }

    std::unique_ptr<ByteStream> stream_;
    std::string                 connectSpec_ = "null";
    std::function<uint64_t()>   hostNs_;
    long long                   baud_ = 403200;
    uint16_t                    port_ = 0x08;

    int typeSwitch_ = 7;  // the switches as set
    int type_       = 7;  // as LATCHED at power-on -- what the card is doing

    // ---- the registers, as the firmware keeps them: the RAW bytes the 8080 reads, status
    // inverted (every flag asserted LOW). ----
    uint8_t status_   = 0xFF;
    uint8_t sectorPos_ = 0xFF;
    uint8_t readData_ = 0x00;

    // ---- the card's state ----
    uint16_t ready_      = 0;      // wDrivesReady: bit n = the server has drive n mounted
    int      cur_        = 0;      // wCurDrivex2 / 2: the drive last selected
    int      track_[8]   = {};     // trackTable
    bool     headLoaded_ = false;  // fHEAD_LOADED
    bool     intsOn_     = false;  // fINTS_ENABLED (stored; the interrupt is not wired)
    int      byteCount_  = 0;
    int      pSector_    = 0;      // index into the track buffer

    // THE TRACK BUFFER -- trackBuf, driveTrack, secsBuffed, and the two flags.
    std::vector<uint8_t> buf_;
    uint16_t key_     = 0x8000;  // drive:track in it; bit 15 set = it means nothing
    int      secsBuf_ = 0;       // whole sectors arrived: readable before the rest of the track
    bool     dirty_   = false;   // fTRACK_DIRTY
    bool     want_    = false;   // fTRACK_READ
    bool     flushNow_ = false;  // the idle loop wants the dirty track written back

    // ---- emulated time: the card's own timers ----
    uint64_t settleAt_  = 0;      // OC1: the head step timer
    bool     stepping_  = false;
    bool     fast_      = false;  // STAT reported a track past 76: the fast step rate
    uint64_t tickSeen_  = 0;      // the last sector-timer tick consumed (SECTOR_START)
    uint64_t idleT_     = 0;      // motorTimer, in T-states: counted while a drive is ready
    uint64_t idleMark_  = 0;
    bool     touched_   = false;  // activity since pump() last looked: restart idleNs_

    // ---- host time: the idle write-back, and the link ----
    uint64_t idleNs_     = 0;     // motorTimer again, in WALL time -- see touch()

    uint64_t idleNsMark_ = 0;
    Link                 link_      = Link::Idle;
    uint64_t             statAt_    = 0;
    uint64_t             deadline_  = 0;
    int                  tries_     = 0;
    int                  serverUp_  = -1;  // -1 not heard yet, 0 silent, 1 answering: reported on a change
    bool                 readAfter_ = false;
    std::vector<uint8_t> rx_;
    std::vector<uint8_t> tx_;
    uint64_t             rxBytes_ = 0;

    std::vector<std::string> log_;

    FdcPlusHdf hdf_;  // type 5
};

} // namespace altair
