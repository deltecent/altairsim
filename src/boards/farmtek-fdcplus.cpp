#include "boards/farmtek-fdcplus.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/debuglog.h"
#include "core/statefile.h"
#include "boards/floppy-drive.h"
#include "core/value.h"
#include "host/endpoint.h"
#include "host/media.h"
#include "host/stream.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <thread>

namespace altair {

namespace {

FdcPlusBoard::EndpointResolver g_resolver;

uint64_t steadyNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Every message, both ways, is ten bytes: four ASCII, two little-endian words, and a
// checksum that is the 16-bit sum of the eight bytes before it.
constexpr size_t kMsgLen = 10;

uint16_t sum16(const uint8_t* p, size_t n) {
    uint16_t s = 0;
    for (size_t i = 0; i < n; ++i) s = (uint16_t)(s + p[i]);
    return s;
}

// ---- The drive status bits (asserted LOW), from serialDrive.s ----
constexpr uint8_t dsENWD   = 0x01;
constexpr uint8_t dsMOVE   = 0x02;  // MOVE HEAD ok
constexpr uint8_t dsHEAD   = 0x04;  // head loaded and settled
constexpr uint8_t dsTRACK0 = 0x40;
constexpr uint8_t dsNRDA   = 0x80;
// "move head asserted, b4, b3 cleared" -- b3 is the firmware's own DRIVE READY flag.
constexpr uint8_t dsINIT = 0xE5;

// ---- The card's timers ----
constexpr uint64_t kStepTicks8 = 10500 / 4;  // STEP_TIMER8: 10.5 ms at 250 kHz
constexpr uint64_t kStepTicks5 = 50000 / 4;  // STEP_TIMER5: 50 ms
constexpr uint64_t kSectorUs8  = 5208;       // SECTOR_TIME8: 5.208 ms
constexpr uint64_t kSectorUs5  = 12500;      // SECTOR_TIME5: 12.5 ms
constexpr uint64_t kIdle8      = 256;        // mtEIGHT13: 1.3 s, 8" head unloaded
constexpr uint64_t kIdle5      = 128;        // mtMINI16: 1.6 s
constexpr uint64_t kSleep5     = 512;        // mtMINI64: 6.4 s, the Minidisk turns off

constexpr size_t kTrackBuf = 32 * FdcPlusBoard::kSlot;  // trackBuf: TRACK_LENGTH8

// THE SERIAL DRIVE'S BAUD RATES. The card's v1.8 monitor offered three -- 403.2K (preferred),
// 460.8K and 230.4K; later firmware and the server (v1.4: 9600, 19.2K, 38.4K, 76.8K; v1.41:
// 57.6K) added the slow ones, and the two ends must agree on one. Nothing else is a setting
// either end has.
constexpr long long kBauds[] = {9600, 19200, 38400, 57600, 76800, 230400, 403200, 460800};

} // namespace

void FdcPlusBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

FdcPlusBoard::FdcPlusBoard() : stream_(std::make_unique<NullStream>()), buf_(kTrackBuf, 0) {
    latch();
    // A bad spot used on a type-5 disk: one line, only while `fault` is on (#969).
    hdf_.setFaultHook([this](int drive, int cyl, int side, const DiskFault& f) {
        auto* dc = debugChannel();
        if (!dc || !dc->on(FAULT)) return;
        dbg::line(*dc) << "fault drive" << drive << " " << cyl << "/" << side << "/0 "
                       << faultKindName(f.kind) << " hit " << f.hits;
        if (!f.hard()) dbg::out() << ", " << f.left << " left";
        dbg::out() << "\n";
    });
}

FdcPlusBoard::~FdcPlusBoard() { drain(); }

uint64_t FdcPlusBoard::now() const { return clock_ ? clock_->now() : 0; }
uint64_t FdcPlusBoard::hostNs() const { return hostNs_ ? hostNs_() : steadyNs(); }

// The card's timers are its own crystal, so they are measured in MICROSECONDS of emulated
// time and turned into T-states of whatever the CPU runs at.
static uint64_t tFromUs(const Clock* c, uint64_t us) {
    if (!c) return 1;
    uint64_t t = (uint64_t)c->hz() * us / 1000000;
    return t ? t : 1;
}

uint64_t FdcPlusBoard::sectorT() const { return tFromUs(clock_, mini() ? kSectorUs5 : kSectorUs8); }

// ---------------------------------------------------------------------------
// POWER-ON. "The drive type is latched at power-on." The firmware then starts with every
// track at zero, no drive ready until the server says so, and an empty track buffer.
// ---------------------------------------------------------------------------
void FdcPlusBoard::latch() {
    type_       = typeSwitch_;
    status_     = 0xFF;
    sectorPos_  = 0xFF;
    readData_   = 0x00;
    ready_      = 0;
    cur_        = 0;
    std::fill(std::begin(track_), std::end(track_), 0);
    headLoaded_ = false;
    intsOn_     = false;
    byteCount_  = 0;
    pSector_    = 0;
    key_        = 0x8000;
    secsBuf_    = 0;
    dirty_      = false;
    want_       = false;
    flushNow_   = false;
    stepping_   = false;
    settleAt_   = 0;
    fast_       = false;
    tickSeen_   = now() / sectorT();
    idleT_      = 0;
    idleMark_   = now();
    idleNs_     = 0;
    touched_    = true;
    statAt_     = 0;  // ask the server at once
}

void FdcPlusBoard::power() {
    drain();
    latch();
    hdf_.setClock(clock_);
    hdf_.power();
}

// ---------------------------------------------------------------------------
// The card's own timers, caught up to NOW. The step timer (OC1) and the activity timer
// (motorTimer) run on the card whether or not the 8080 is looking; we just read them late.
// ---------------------------------------------------------------------------
void FdcPlusBoard::sync() {
    const uint64_t t = now();
    if (stepping_ && t >= settleAt_) {  // headMoveInt
        stepping_ = false;
        if (driveReady()) status_ &= (uint8_t)~dsMOVE;
        if (headLoaded_) status_ &= (uint8_t)~dsHEAD;
    }
    // motorTimer counts sector interrupts, and sectorInt counts only while a drive is ready.
    if (driveReady() && t > idleMark_) idleT_ += t - idleMark_;
    idleMark_ = t;
}

// ---------------------------------------------------------------------------
// The bus. FOUR ports: status/select, sector/command, data, and a fourth the manual calls
// Reserved, which the firmware clears at start-up and never writes again -- it reads 00.
// ---------------------------------------------------------------------------
bool FdcPlusBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::IoRead && c.type != Cycle::IoWrite) return false;
    const uint8_t p = c.port();
    return p >= port_ && p < port_ + 4;
}

uint8_t FdcPlusBoard::read(const BusCycle& c) {
    if (hd()) {
        hdf_.setClock(clock_);
        return hdf_.read((uint8_t)(c.port() - port_));
    }
    sync();
    switch ((uint8_t)(c.port() - port_)) {
    case 0: return status_;
    case 1: {
        // The 8080 reads the register FIRST; the firmware works out its next value AFTER
        // ("this routine is entered AFTER the sector position is read").
        const uint8_t v = sectorPos_;
        sectorRead();
        return v;
    }
    case 2: {
        const uint8_t v = readData_;
        dataRead();
        return v;
    }
    default: return 0x00;
    }
}

void FdcPlusBoard::write(const BusCycle& c) {
    if (hd()) {
        hdf_.setClock(clock_);
        return hdf_.write((uint8_t)(c.port() - port_), c.data);
    }
    sync();
    switch ((uint8_t)(c.port() - port_)) {
    case 0: select(c.data); break;
    case 1: command(c.data); break;
    case 2: dataWrite(c.data); break;
    default: break;
    }
}

std::vector<MapEntry> FdcPlusBoard::ioMap() const {
    if (hd()) {
        return {
            {port_,      port_,      "select/status",  "drive select / status (INVERTED)"},
            {port_ + 1u, port_ + 1u, "command/sector", "step/head/read/write / sector 0 or 15"},
            {port_ + 2u, port_ + 2u, "data",           "a whole 10,240-byte track, no handshake"},
            {port_ + 3u, port_ + 3u, "track/iostatus", "track number / I/O status"},
        };
    }
    return {
        {port_,      port_,      "select/status",  "drive select / status (INVERTED)"},
        {port_ + 1u, port_ + 1u, "command/sector", "step/head/write / sector position"},
        {port_ + 2u, port_ + 2u, "data",           "137-byte sector slots, from the track buffer"},
        {port_ + 3u, port_ + 3u, "reserved",       "reads 00"},
    };
}

// ---------------------------------------------------------------------------
// DRIVE SELECT (ioPortInt). Any select -- or deselect -- drops the head and turns the card's
// interrupts off. A drive the server has no image for is not ready: status and sector
// position stay FF, as if nothing were there.
// ---------------------------------------------------------------------------
void FdcPlusBoard::select(uint8_t v) {
    status_     = 0xFF;
    sectorPos_  = 0xFF;
    intsOn_     = false;
    headLoaded_ = false;
    if (v & 0x80) return;  // deselect

    cur_ = v & 0x07;  // scDRIVE_MASK: three bits, eight drives
    if (!((ready_ >> cur_) & 1)) return;

    status_ = dsINIT;
    if (track_[cur_] == 0) status_ &= (uint8_t)~dsTRACK0;
    touch();
    if (mini()) {  // "otherwise, minidisk has head loaded"
        status_ &= (uint8_t)~dsHEAD;
        headLoaded_ = true;
    }
}

// ---------------------------------------------------------------------------
// THE DRIVE COMMAND (chkDriveCmd), in the firmware's order: a step (out wins over in), then
// unload, then load; then write enable; then interrupts, where disable wins.
// ---------------------------------------------------------------------------
void FdcPlusBoard::command(uint8_t v) {
    if (!driveReady()) return;  // "do we show the drive ready? no, ignore the command"

    if (v & 0x0F) {
        // "immediately disable the sector position register (0xff). This is done because
        // 8080 software may quickly read the status registers after writing this command."
        if (!mini()) sectorPos_ = 0xFF;
        if (v & 0x03) {
            sectorPos_ = 0xFF;
            status_ |= dsMOVE | dsHEAD;
            touch();
            int&      t   = track_[cur_];
            const int was = t;
            if (v & 0x02) {  // step out
                if (t > 0 && --t == 0) status_ &= (uint8_t)~dsTRACK0;
            } else {         // step in -- "maximum track validation was removed" (8 MB drive)
                status_ |= dsTRACK0;
                if (t < 0x0FFF) ++t;  // the wire carries twelve bits
            }
            // The 8 MB drive steps fast: once STAT has reported a track past 76, the firmware
            // clears the high byte of the step time and leaves it cleared.
            uint64_t ticks = mini() ? kStepTicks5 : kStepTicks8;
            if (fast_) ticks &= 0xFF;
            settleAt_ = now() + tFromUs(clock_, ticks * 4);
            stepping_ = true;
            if (auto* dc = debugChannel(); dc && dc->on(SEEK))
                dbg::line(*dc) << "seek drive=" << cur_ << " track=" << was << " -> " << t << "\n";
        }
        if ((v & 0x08) && !mini()) {  // head unload; the Minidisk has no head to unload
            headLoaded_ = false;
            status_ |= dsHEAD;
        }
        if (v & 0x04) {  // head load -- on the Minidisk, only the timer reset it always was
            touch();
            if (!mini()) {
                headLoaded_ = true;
                status_ &= (uint8_t)~dsHEAD;
            }
        }
    }

    if (v & 0x80) {  // write enable: the sector was already found by reading sector true
        dirty_ = true;
        status_ &= (uint8_t)~dsENWD;
    }

    if (v & 0x20)      intsOn_ = false;
    else if (v & 0x10) intsOn_ = true;
}

// ---------------------------------------------------------------------------
// THE SECTOR PORT (chkSector) -- where the whole drive happens.
//
// If the value the 8080 just read had sector true, point at that sector in the track buffer
// and put its first byte out with NRDA true. Then decide the NEXT value: a track that is not
// in the buffer is requested and the port goes to FF; otherwise the sector moves on only if
// a sector time has passed AND the next sector has arrived. Sector true lasts one read.
// ---------------------------------------------------------------------------
void FdcPlusBoard::sectorRead() {
    const uint8_t sp   = sectorPos_;
    const int     wrap = sectors() - 1;

    status_ |= dsENWD;  // "make sure new write data flag is off"
    if (!mini()) touch();  // on the 8" drive, this access counts as activity

    if (!(sp & 0x01)) {
        const int sector = (sp >> 1) & wrap;
        byteCount_       = 0;
        pSector_         = sector * kSlot;
        readData_        = buf_[(size_t)pSector_];
        status_ &= (uint8_t)~dsNRDA;
    }

    if (key_ != curKey()) {  // nextSPValue: not in the buffer -- ask for it
        want_      = true;
        sectorPos_ = 0xFF;
        return;
    }

    const uint64_t tick = now() / sectorT();
    if (tick != tickSeen_) {  // SECTOR_START
        const int next = ((sectorPos_ >> 1) + 1) & wrap;
        if (next < secsBuf_) {
            sectorPos_ = (uint8_t)((next << 1) | 0xC0);  // sector true asserted (bit 0 low)
            tickSeen_  = tick;
            return;
        }
    }
    sectorPos_ |= 0x01;  // sameSector: sector true off
}

// chkReadData: the byte the 8080 just took is replaced by the next one. NRDA goes false
// after the 137th, but the register keeps moving.
void FdcPlusBoard::dataRead() {
    if (++byteCount_ >= kSlot) status_ |= dsNRDA;
    ++pSector_;
    readData_ = (pSector_ >= 0 && (size_t)pSector_ < buf_.size()) ? buf_[(size_t)pSector_] : 0x00;
}

// chkWriteData: the first 137 bytes land in the buffer; the rest are taken and dropped.
void FdcPlusBoard::dataWrite(uint8_t v) {
    if (++byteCount_ > kSlot) return;
    if (pSector_ >= 0 && (size_t)pSector_ < buf_.size()) buf_[(size_t)pSector_] = v;
    ++pSector_;
}

// ---------------------------------------------------------------------------
// ACTIVITY restarts the firmware's motorTimer: a select, a step, a head load, and on the 8"
// drive a sector-port read.
//
// THE TIMER DOES TWO JOBS, AND THEY RUN ON TWO CLOCKS. The rule (DESIGN.md, and the VDM-1's
// blink): a duration the guest can READ BACK is emulated time; one it cannot is the card's
// own crystal, and that is wall time.
//
//   - The Minidisk's 6.4 s turn-off IS read back -- status goes to FF -- so it counts
//     emulated time (idleT_), in the bus cycle, like the 88-MDS's disable timer.
//   - The 1.3 s / 1.6 s write-back-and-forget is NOT. It only decides when the LINK talks,
//     and the link is wall time. On emulated time it fired in the middle of a track transfer
//     whenever the machine ran flat out -- 1.3 emulated seconds pass long before a 1.17 s
//     track at 38,400 baud arrives -- and threw the track away. So it counts wall time
//     (idleNs_), in pump(), and a bus cycle only leaves a mark (touched_) for pump() to see.
// ---------------------------------------------------------------------------
void FdcPlusBoard::touch() {
    idleT_   = 0;
    touched_ = true;
}

void FdcPlusBoard::hostIdle() {
    const uint64_t h = hostNs();
    if (touched_) {
        idleNs_  = 0;
        touched_ = false;
    } else if (driveReady() && h > idleNsMark_) {
        idleNs_ += h - idleNsMark_;
    }
    idleNsMark_ = h;
}

// The idle loop's housekeeping:
//
//   8"        head NOT loaded for 256 sector times (1.3 s): forget the buffer, and write
//             it back if dirty.
//   Minidisk  128 sector times (1.6 s): the same. 512 (6.4 s): the drive turns off.
//
// Forgetting makes the next access fetch the track again, which is how a disk swapped on
// the server is noticed.
void FdcPlusBoard::idleChecks() {
    auto letGo = [this] {
        key_ |= 0x8000;
        if (dirty_) flushNow_ = true;
    };
    if (!mini()) {
        if (!headLoaded_ && idleNs_ >= kIdle8 * kSectorUs8 * 1000) {
            idleNs_ = 0;
            letGo();
        }
        return;
    }
    if (idleT_ >= kSleep5 * sectorT()) {
        idleT_      = 0;
        status_     = 0xFF;
        sectorPos_  = 0xFF;
        intsOn_     = false;
        headLoaded_ = false;
    }
    if (idleNs_ >= kIdle5 * kSectorUs5 * 1000) {
        idleNs_ = 0;
        letGo();
    }
}

void FdcPlusBoard::pump() {
    // At type 5 the serial-drive code is not running: the line stays plugged in, and quiet.
    if (hd()) return;
    stream_->pump();
    sync();
    hostIdle();
    idleChecks();
    if (connected()) step();
    stream_->flush();
}

// ---------------------------------------------------------------------------
// THE LINK. One non-blocking turn: move what bytes can move, then act on the state.
//
// The timeout runs from the LAST BYTE MOVED, not from the start of the transfer. The
// firmware times the whole receive from its start, which is right at 403.2K (a track takes
// 0.11 s) and wrong at a host rate the card never had: a track at 38,400 baud takes 1.14 s,
// and a whole-transfer timeout would throw every one of them away. The protocol's own words
// are "one second after the last byte", and that is what this does.
// ---------------------------------------------------------------------------
void FdcPlusBoard::transmit() {
    while (!tx_.empty() && stream_->writable()) {
        const size_t n = stream_->write(tx_.data(), tx_.size());
        if (n == 0) break;
        tx_.erase(tx_.begin(), tx_.begin() + (std::ptrdiff_t)n);
        deadline_ = hostNs() + kTimeoutNs;
    }
}

void FdcPlusBoard::step() {
    transmit();
    uint8_t in[512];
    while (stream_->readable()) {
        const size_t n = stream_->read(in, sizeof in);
        if (n == 0) break;
        rx_.insert(rx_.end(), in, in + n);
        // ONLY A TRACK COUNTS as bytes arriving for the guest (Board::rxBytes). The run loop
        // reads any arrival as "the guest is receiving, this is not a prompt" -- so counting the
        // STAT reply that comes ten times a second kept a machine at A> from ever resting,
        // and it burned a whole core. STAT, WRIT and WSTA are the card talking to the server;
        // the 8080 never sees a byte of them.
        if (link_ == Link::Read) rxBytes_ += n;
        deadline_ = hostNs() + kTimeoutNs;
    }
    const bool timedOut = tx_.empty() && hostNs() >= deadline_;
    auto*      dc       = debugChannel();
    const bool trace    = dc && dc->on(LINK);

    uint16_t code = 0, data = 0;
    switch (link_) {
    case Link::Idle:
        if (want_) {
            if (dirty_) {
                readAfter_ = true;  // "see if the existing track buffer must be written first"
                startWrite();
            } else {
                startRead();
            }
        } else if (flushNow_) {
            flushNow_ = false;
            if (dirty_) {
                readAfter_ = false;
                startWrite();
            }
        } else if (hostNs() >= statAt_) {
            // STAT: the selected drive in the low byte (FF = none) and FF in the high byte if
            // its head is loaded; the current drive's track in the second word.
            uint16_t p1 = 0x00FF;
            if (driveReady()) p1 = (uint16_t)(cur_ | (headLoaded_ ? 0xFF00 : 0));
            const uint16_t trk = (uint16_t)track_[cur_];
            if (trk >= 77) fast_ = true;
            send("STAT", p1, trk);
            link_ = Link::Stat;
        }
        break;

    case Link::Stat:
        if (rx_.size() >= kMsgLen) {
            if (const char* why = takeReply("STAT", code, data)) {
                if (faultsOn()) dbg::line(*dc) << "STAT reply: " << why << "\n";
            } else {
                if (serverUp_ == 0 && faultsOn()) dbg::line(*dc) << "server answering again\n";
                serverUp_ = 1;
                if (trace && data != ready_) dbg::line(*dc) << "server drives=" << data << "\n";
                ready_ = data;
                // "Verify the server still has the currently selected drive ready. If not, set
                // drive status and sector position registers to 0xff."
                if (driveReady() && !((ready_ >> cur_) & 1)) {
                    status_    = 0xFF;
                    sectorPos_ = 0xFF;
                }
            }
            link_   = Link::Idle;
            statAt_ = hostNs() + kStatNs;
        } else if (timedOut) {
            // Once, on the change: STAT goes ten times a second, and a silent server would
            // otherwise say so ten times a second.
            if (serverUp_ != 0 && faultsOn())
                dbg::line(*dc) << "server not answering (no STAT reply in 1 s)\n";
            serverUp_ = 0;
            link_     = Link::Idle;
            statAt_   = hostNs() + kStatNs;
        }
        break;

    case Link::Read: {
        const size_t len = (size_t)trackLen();
        const size_t got = std::min(rx_.size(), len);
        std::memcpy(buf_.data(), rx_.data(), got);
        secsBuf_ = (int)(got / kSlot);  // each sector is readable as soon as it is here
        bool done = false;
        if (rx_.size() >= len + 2) {
            const uint16_t ck = (uint16_t)(rx_[len] | (rx_[len + 1] << 8));
            const bool bad = sum16(rx_.data(), len) != ck;
            if (bad) key_ |= 0x8000;
            if (trace || (bad && faultsOn()))
                dbg::line(*dc) << "read drive=" << ((key_ >> 12) & 7) << " track="
                               << (key_ & 0x0FFF) << (bad ? " BAD CHECKSUM" : "") << "\n";
            done = true;
        } else if (timedOut) {
            key_ |= 0x8000;  // "force drive:track to not match"
            if (faultsOn())
                dbg::line(*dc) << "read drive=" << ((key_ >> 12) & 7) << " track="
                               << (key_ & 0x0FFF) << " timed out after " << rx_.size() << " of "
                               << len + 2 << " bytes\n";
            done = true;
        }
        if (done) {
            want_ = false;  // "read request completed" -- the next sector read asks again
            rx_.clear();
            link_   = Link::Idle;
            statAt_ = hostNs() + kStatNs;
        }
        break;
    }

    case Link::WritAsk:
        if (rx_.size() >= kMsgLen) {
            const char* why = takeReply("WRIT", code, data);
            if (!why && code == 0) {
                sendTrack();
                link_ = Link::WritSta;
            } else {
                writeFault("WRIT", why ? why : code == 1 ? "drive not ready" : "refused");
                finishWrite(false);
            }
        } else if (timedOut) {
            writeFault("WRIT", "no reply in 1 s");
            finishWrite(false);
        }
        break;

    case Link::WritSta:
        if (rx_.size() >= kMsgLen) {
            const char* why = takeReply("WSTA", code, data);
            if (why || code != 0) writeFault("WSTA", why ? why : "track not written");
            finishWrite(!why && code == 0);
        } else if (timedOut) {
            writeFault("WSTA", "no reply in 1 s");
            finishWrite(false);
        }
        break;
    }
    transmit();  // whatever this turn decided to say goes out now, not a slice later
}

// The firmware empties its receive FIFO before it waits for any answer, so a late reply to an
// earlier command can never be taken for this one.
void FdcPlusBoard::flushRx() {
    uint8_t in[512];
    while (stream_->readable())
        if (stream_->read(in, sizeof in) == 0) break;
    rx_.clear();
}

void FdcPlusBoard::send(const char* cmd, uint16_t p1, uint16_t p2) {
    flushRx();
    uint8_t m[kMsgLen];
    std::memcpy(m, cmd, 4);
    m[4]             = (uint8_t)p1;
    m[5]             = (uint8_t)(p1 >> 8);
    m[6]             = (uint8_t)p2;
    m[7]             = (uint8_t)(p2 >> 8);
    const uint16_t s = sum16(m, 8);
    m[8]             = (uint8_t)s;
    m[9]             = (uint8_t)(s >> 8);
    tx_.insert(tx_.end(), m, m + kMsgLen);
    deadline_ = hostNs() + kTimeoutNs;
    if (std::strcmp(cmd, "STAT") != 0)  // STAT goes ten times a second; the rest is news
        if (auto* dc = debugChannel(); dc && dc->on(LINK))
            dbg::line(*dc) << "send " << cmd << " drive=" << ((p1 >> 12) & 7)
                           << " track=" << (p1 & 0x0FFF) << "\n";
}

// nullptr for a good reply; otherwise what was wrong with it, for the debug channel.
const char* FdcPlusBoard::takeReply(const char* cmd, uint16_t& code, uint16_t& data) {
    const uint8_t* m   = rx_.data();
    const char*    why = std::memcmp(m, cmd, 4) != 0                            ? "wrong reply"
                         : sum16(m, 8) != (uint16_t)(m[8] | (m[9] << 8)) ? "bad checksum"
                                                                          : nullptr;
    code = (uint16_t)(m[4] | (m[5] << 8));
    data = (uint16_t)(m[6] | (m[7] << 8));
    rx_.erase(rx_.begin(), rx_.begin() + (std::ptrdiff_t)kMsgLen);
    return why;  // a bad reply is dropped, as the firmware drops it
}

bool FdcPlusBoard::faultsOn() {
    auto* dc = debugChannel();
    return dc && (dc->on(LINKERR) || dc->on(LINK));
}

// One try of a track write failed. finishWrite() retries, or gives up and says so.
void FdcPlusBoard::writeFault(const char* what, const char* why) {
    if (!faultsOn()) return;
    dbg::line(*debugChannel()) << what << " drive=" << ((key_ >> 12) & 7) << " track="
                               << (key_ & 0x0FFF) << ": " << why << " (try "
                               << kWriteTries - tries_ + 1 << " of " << kWriteTries << ")\n";
}

// readTrack: the drive:track is taken NOW, from wherever the head is, not from when the
// request was made.
void FdcPlusBoard::startRead() {
    secsBuf_ = 0;
    key_     = curKey();
    send("READ", key_, (uint16_t)trackLen());
    link_ = Link::Read;
}

// The dirty flag drops as the write STARTS, as in the firmware: a sector the guest writes
// while this one is on the wire makes the buffer dirty again, and it goes out next time.
void FdcPlusBoard::startWrite() {
    tries_ = kWriteTries;
    dirty_ = false;
    send("WRIT", (uint16_t)(key_ & 0x7FFF), (uint16_t)trackLen());
    link_ = Link::WritAsk;
}

void FdcPlusBoard::sendTrack() {
    rx_.clear();
    const size_t len = (size_t)trackLen();
    tx_.insert(tx_.end(), buf_.begin(), buf_.begin() + (std::ptrdiff_t)len);
    const uint16_t s = sum16(buf_.data(), len);
    tx_.push_back((uint8_t)s);
    tx_.push_back((uint8_t)(s >> 8));
    deadline_ = hostNs() + kTimeoutNs;
}

// Three tries, then the firmware gives up without a word -- the guest cannot be told. The
// operator can, so we say it on the host: those writes are gone.
void FdcPlusBoard::finishWrite(bool ok) {
    if (!ok && --tries_ > 0) {
        send("WRIT", (uint16_t)(key_ & 0x7FFF), (uint16_t)trackLen());
        link_ = Link::WritAsk;
        return;
    }
    if (!ok) {
        char m[160];
        std::snprintf(m, sizeof m,
                      "%s: the drive server did not take drive%d track %d after %d tries -- "
                      "that track's writes are lost",
                      id.c_str(), (key_ >> 12) & 7, key_ & 0x0FFF, kWriteTries);
        say(m);
    }
    link_   = Link::Idle;
    statAt_ = hostNs() + kStatNs;
    if (readAfter_) {
        readAfter_ = false;
        startRead();
    }
}

// ---------------------------------------------------------------------------
// THE DIRTY TRACK GOES HOME before the line goes away -- on DISCONNECT, POWER, and when the
// simulator quits. The card itself would lose it if you switched the Altair off inside the
// 1.3 s idle window; an operator closing a window should not. This is host-side lifecycle,
// never a bus cycle, so it may wait -- bounded by the link's own 1 s x 3 limits.
// ---------------------------------------------------------------------------
void FdcPlusBoard::drain() {
    if (!connected()) return;
    want_      = false;
    readAfter_ = false;
    if (dirty_) flushNow_ = true;
    if (!flushNow_ && link_ == Link::Idle) return;

    const uint64_t saveStat = statAt_;
    statAt_                 = UINT64_MAX;  // no STAT in the middle of going home
    const auto give         = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((dirty_ || flushNow_ || link_ != Link::Idle) && std::chrono::steady_clock::now() < give) {
        stream_->pump();
        step();
        stream_->flush();
        if (link_ != Link::Idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    statAt_ = saveStat;
}

void FdcPlusBoard::resetLink() {
    link_ = Link::Idle;
    tx_.clear();
    rx_.clear();
    ready_    = 0;  // nothing is ready until a server says so
    serverUp_ = -1;
    key_ |= 0x8000;
    secsBuf_ = 0;
    statAt_  = 0;
}

std::vector<std::string> FdcPlusBoard::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Properties, units, and the connector.
// ---------------------------------------------------------------------------
std::vector<Property> FdcPlusBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "Base address: 08, or 80 with the address jumpers moved. Four ports, BASE+0 .. BASE+3";
        x.kind  = Kind::Int;
        x.radix = 16;  // ON THE WIRE -> HEX (DESIGN.md 10.0.1)
        x.min   = 0;
        x.max   = 0xFC;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (v.i() != 0x08 && v.i() != 0x80) {
                err = "the FDC+ answers at 08-0B, or at 80-83 with the address jumpers moved -- "
                      "nowhere else";
                return false;
            }
            port_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drivetype";
        x.help  = "Drive Type switches (S3), read at power-on: 5 = 1.5 MB floppy, 6 = serial "
                  "drive as a Minidisk, 7 = serial drive as an 8\" drive";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 5;
        x.max   = 7;
        x.get   = [this] { return Value::ofInt(typeSwitch_); };
        x.set   = [this](const Value& v, std::string&) {
            typeSwitch_ = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "baud";
        x.help  = "Serial drive baud rate: 9600, 19200, 38400, 57600, 76800, 230400, 403200 "
                  "(preferred) or 460800";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = kBauds[0];
        x.max   = kBauds[std::size(kBauds) - 1];
        x.get   = [this] { return Value::ofInt(baud_); };
        x.set   = [this](const Value& v, std::string& err) {
            if (std::find(std::begin(kBauds), std::end(kBauds), v.i()) == std::end(kBauds)) {
                err = "the FDC+ and its drive server run at 9600, 19200, 38400, 57600, 76800, "
                      "230400, 403200 or 460800 -- nothing else";
                return false;
            }
            const long long was = baud_;
            baud_               = v.i();
            if (!connected()) return true;
            LineParams lp;
            lp.baud = baud_;
            if (stream_->setParams(lp, err)) return true;
            baud_ = was;
            return false;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "connect";
        x.help = "The drive server on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(connectSpec_); };
        x.set  = [this](const Value& v, std::string& err) { return applyEndpoint(v.s(), err); };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<UnitDef> FdcPlusBoard::units() const {
    std::vector<UnitDef> u{{"line", UnitKind::Serial, connectSpec_}};
    for (int i = 0; i < FdcPlusHdf::kDrives; ++i) {
        UnitDef x;
        x.name  = "drive" + std::to_string(i);
        x.kind  = UnitKind::Disk;
        x.state = hdf_.mounted(i) ? hdf_.path(i) : "(empty)";
        x.readOnly       = hdf_.readOnly(i);
        x.readOnlyForced = hdf_.readOnlyForced(i);
        u.push_back(std::move(x));
    }
    return u;
}

// "drive3" -> 3, or -1.
static int hdDrive(const std::string& unit) {
    if (unit.size() != 6 || unit.rfind("drive", 0) != 0) return -1;
    const int i = unit[5] - '0';
    return (i >= 0 && i < FdcPlusHdf::kDrives) ? i : -1;
}

ByteStream* FdcPlusBoard::unitStream(const std::string& unit) {
    return unit == "line" ? stream_.get() : nullptr;
}

// THE DRIVES ARE TYPE 5's. They are cabled whatever the switches say -- a disk can go in
// before the card is set to 5 and powered up -- but only type 5 reads them. The serial drive's
// images are on its server, so `line` takes no MOUNT.
bool FdcPlusBoard::mount(const std::string& unit, const std::string& path, bool ro,
                         std::string& err) {
    const int i = hdDrive(unit);
    if (i < 0) {
        err = unit == "line"
                  ? id + ":line is the serial drive's line -- its server mounts the images. " +
                        "CONNECT " + id + ":line to the server, or MOUNT a 1.5 MB image in " +
                        id + ":drive0..drive3"
                  : "no unit `" + unit + "` on " + id + " (it has line, and drive0..drive3)";
        return false;
    }
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) {
        err += pathNote(path);
        return false;
    }
    const bool forced = media->readOnlyForced();
    if (!hdf_.mount(i, std::move(media), path, err)) return false;
    if (forced)
        say(id + ": drive" + std::to_string(i) +
            " mounted WRITE-PROTECTED -- the host will not let us write " + path);
    return true;
}

bool FdcPlusBoard::unmount(const std::string& unit, std::string& err) {
    const int i = hdDrive(unit);
    if (i < 0) {
        err = "no disk unit `" + unit + "` on " + id + " (it has drive0..drive3)";
        return false;
    }
    return hdf_.unmount(i, err);
}

// [[board.drive]]: unit, mount, readonly -- as on the other floppy boards.
std::vector<Property> FdcPlusBoard::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive: 0-3. Drive type 5 only";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = FdcPlusHdf::kDrives - 1;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "The 1.5 MB disk image to put in it. Relative to THIS FILE.";
        x.kind = Kind::Str;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "readonly";
        x.help    = "Write-protect the disk. The drive tells the card, and the card tells the "
                    "8080: status bit 4";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};
        p.push_back(std::move(x));
    }
    p.push_back(driveFaultsKey());
    return p;
}

bool FdcPlusBoard::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }
    int         unit = -1;
    std::string path, faultText;
    bool        ro = false;
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "readonly") ro = (v == "true" || v == "1" || v == "yes" || v == "on");
        else if (k == "faults") faultText = v;
    }
    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }
    if (!path.empty() && !mount("drive" + std::to_string(unit), path, ro, err)) return false;
    DiskFaults  none;
    DiskFaults* f = hdf_.faults(unit);
    return applyDriveFaults(f ? *f : none, f != nullptr, faultText, err);
}

std::vector<Board::SubUnit> FdcPlusBoard::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < FdcPlusHdf::kDrives; ++i) {
        if (!hdf_.mounted(i)) continue;
        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});
        su.fields.push_back({"mount", hdf_.path(i), true});
        if (hdf_.readOnly(i)) su.fields.push_back({"readonly", "true", false});
        if (const DiskFaults* f = hdf_.faults(i); f && !f->empty())
            su.fields.push_back({"faults", f->toText(), true});
        out.push_back(std::move(su));
    }
    return out;
}

bool FdcPlusBoard::takesFaults(const std::string& unit) const { return hdDrive(unit) >= 0; }

DiskFaults* FdcPlusBoard::faults(const std::string& unit) {
    const int i = hdDrive(unit);
    return i >= 0 ? hdf_.faults(i) : nullptr;
}

bool FdcPlusBoard::applyEndpoint(const std::string& endpoint, std::string& err) {
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    std::vector<std::string> paths;
    auto s = openEndpoint(
        g_resolver, endpoint, [this](const std::string& p) { return resolvePath(p); }, err,
        &paths);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    LineParams lp;
    lp.baud = baud_;
    if (!s->setParams(lp, err)) return false;

    drain();  // the old line's dirty track goes home first
    stream_      = std::move(s);
    connectSpec_ = endpoint;  // as written -- what SHOW and CONFIG SAVE echo
    resetLink();
    return true;
}

bool FdcPlusBoard::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "line") {
        err = "fdcplus connects only 'line' -- '" + unit + "' is a disk drive, which MOUNT fills";
        return false;
    }
    return applyEndpoint(ep, err);
}

bool FdcPlusBoard::disconnect(const std::string& unit, std::string& err) {
    if (unit != "line") {
        err = "fdcplus connects only 'line' -- '" + unit + "' is a disk drive, which MOUNT fills";
        return false;
    }
    drain();
    stream_      = std::make_unique<NullStream>();
    connectSpec_ = "null";
    resetLink();
    dirty_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13). The registers, the track table and the track buffer are
// runtime state on the card -- a dirty buffer has not reached the server yet -- so they
// travel. The link does not: a restored board starts idle, and a track that was only half
// here is fetched again.
// ---------------------------------------------------------------------------
void FdcPlusBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8((uint8_t)type_);
    w.u8(status_);
    w.u8(sectorPos_);
    w.u8(readData_);
    w.u16(ready_);
    w.u8((uint8_t)cur_);
    for (int t : track_) w.u16((uint16_t)t);
    w.boolean(headLoaded_);
    w.boolean(intsOn_);
    w.u16((uint16_t)byteCount_);
    w.u16((uint16_t)pSector_);
    w.raw(buf_.data(), buf_.size());
    w.u16(key_);
    w.u8((uint8_t)secsBuf_);
    w.boolean(dirty_);
    w.boolean(want_);
    w.boolean(flushNow_);
    w.boolean(stepping_);
    w.u64(settleAt_);
    w.boolean(fast_);
    w.u64(tickSeen_);
    w.u64(idleT_);
    w.u64(idleMark_);
    hdf_.serialize(w);
}

void FdcPlusBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    const uint8_t ty = r.u8();
    type_      = ty == 5 || ty == 6 ? ty : 7;
    status_    = r.u8();
    sectorPos_ = r.u8();
    readData_  = r.u8();
    ready_     = r.u16();
    cur_       = r.u8() & 0x07;
    for (int& t : track_) t = r.u16() & 0x0FFF;
    headLoaded_ = r.boolean();
    intsOn_     = r.boolean();
    byteCount_  = r.u16();
    pSector_    = r.u16();
    r.raw(buf_.data(), buf_.size());
    key_      = r.u16();
    secsBuf_  = r.u8();
    dirty_    = r.boolean();
    want_     = r.boolean();
    flushNow_ = r.boolean();
    stepping_ = r.boolean();
    settleAt_ = r.u64();
    fast_     = r.boolean();
    tickSeen_ = r.u64();
    idleT_    = r.u64();
    idleMark_ = r.u64();
    hdf_.deserialize(r);
    if (secsBuf_ < sectors()) {  // it was still arriving: fetch it again
        key_ |= 0x8000;
        secsBuf_ = 0;
    }
    link_ = Link::Idle;
    tx_.clear();
    rx_.clear();
    statAt_ = 0;
}

} // namespace altair
