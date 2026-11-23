#include "boards/farmtek-fdcplus-hdf.h"

#include "core/statefile.h"
#include "host/media.h"

#include <algorithm>
#include <cstring>

namespace altair {

namespace {

// ---- The drive status bits (asserted LOW), from hdfloppy.s ----
constexpr uint8_t dsENWD   = 0x01;
constexpr uint8_t dsMOVE   = 0x02;
constexpr uint8_t dsHEAD   = 0x04;
constexpr uint8_t dsREADY  = 0x08;  // the firmware's own "drive selected" flag
constexpr uint8_t dsWP     = 0x10;  // WRITE PROTECT: bit 4, which type 5 gives a meaning
constexpr uint8_t dsTRACK0 = 0x40;
constexpr uint8_t dsNRDA   = 0x80;
constexpr uint8_t dsINIT   = 0xF5;  // "move head asserted, b3 cleared"

// ---- The I/O status register (IN base+3), asserted HIGH ----
constexpr uint8_t iosCKSUM_ERR = 0x01;  // only a fault sets it: an image has no bad sums
constexpr uint8_t iosTRACK_ERR = 0x02;
constexpr uint8_t iosEOS_ERR   = 0x04;
constexpr uint8_t iosDONE      = 0x80;

// ---- The card's timing, in microseconds after the index hole ----
constexpr uint64_t kRevUs        = 166668;  // INDEX_TIME: 360 rpm
constexpr uint64_t kSecTrueUs    = 32;      // SEC_TRUE_TIME
constexpr uint64_t kReadClearUs  = 400;     // READ_CLEAR_TIME: a read starts here
constexpr uint64_t kWriteClearUs = 800;     // WRITE_CLEAR_TIME: zeros until here
constexpr uint64_t kByteUs       = 16;      // one MFM byte at 500 kbit/s
constexpr uint64_t kDataUs       = kWriteClearUs + kByteUs;  // the sync byte is past
constexpr uint64_t kStepUs       = 3000;    // ptStepTHD's default, STEP_TIME3
constexpr uint64_t kSettleUs     = 18000;   // HEAD_SETTLE_TIME

constexpr size_t kBootImage = 256;  // bootImage[256], and trackBuf right after it
constexpr size_t kMem       = kBootImage + FdcPlusHdf::kTrackBytes + 256;

// THE FAKE BOOT SECTOR (hdfloppy.s, bootSector; its source is HDFBL.ASM). A standard Altair
// sector in the card's flash: 80 (sync, track 0), 0080 (the load ends at 0080), 128 bytes of
// loader, FF, and the checksum A6. A stock boot PROM reads it as sector 0 of track 0, jumps to
// 0000, and the loader reads HD track 0 to 4000h and jumps there.
constexpr uint8_t kBoot[] = {
    0x80, 0x80, 0x00, 0xf3, 0x31, 0x00, 0x3f, 0x3e, 0x80, 0xd3, 0x08, 0xaf, 0xd3, 0x08,
    0xd3, 0x0b, 0x3e, 0x04, 0xd3, 0x09, 0xdb, 0x08, 0xe6, 0x02, 0xc2, 0x11, 0x00, 0xdb,
    0x08, 0xe6, 0x40, 0xca, 0x26, 0x00, 0x3e, 0x02, 0xd3, 0x09, 0xc3, 0x11, 0x00, 0xdb,
    0x09, 0x1f, 0xda, 0x26, 0x00, 0x21, 0x00, 0x40, 0x01, 0x00, 0x14, 0x3e, 0x10, 0xd3,
    0x09, 0xdb, 0x08, 0xb7, 0xfa, 0x36, 0x00, 0xdb, 0x0a, 0xdb, 0x0a, 0x77, 0x23, 0xdb,
    0x0a, 0x77, 0x23, 0x0b, 0x78, 0xb1, 0xc2, 0x3e, 0x00, 0xdb, 0x0b, 0x32, 0xff, 0x3f,
    0xe6, 0x7f, 0xc2, 0x00, 0x00, 0x3e, 0x80, 0xd3, 0x08, 0xc3, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xa6, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof kBoot >= FdcPlusHdf::kBootLen, "the boot sector is a whole Altair sector");

} // namespace

const uint8_t* FdcPlusHdf::bootSector() { return kBoot; }

FdcPlusHdf::FdcPlusHdf() : mem_(kMem, 0), pending_((size_t)kTrackBytes, 0) {
    std::memcpy(mem_.data(), kBoot, sizeof kBoot);
}

FdcPlusHdf::~FdcPlusHdf() = default;

uint64_t FdcPlusHdf::us(uint64_t n) const {
    if (!clock_) return n ? n : 1;
    const uint64_t t = (uint64_t)clock_->hz() * n / 1000000;
    return t ? t : 1;
}

bool FdcPlusHdf::readOnly(int i) const {
    const auto& d = drive_[(size_t)i];
    return d.img && d.img->readOnly();
}

bool FdcPlusHdf::readOnlyForced(int i) const {
    const auto& d = drive_[(size_t)i];
    return d.img && d.img->readOnlyForced();
}

// THE TRACK LAYOUT (hdfloppy.s, and the BIOS it goes with). Even tracks on the bottom side,
// odd tracks on the top, a cylinder each pair, up to cylinder 71. The top side's last eight
// cylinders are left alone ("for better reliability with aging media"), so tracks 144-148
// are cylinders 72-76 on the bottom -- and 76 is the last, "for compatibility with 8" drives".
int FdcPlusHdf::imageTrack(int cyl, bool topSide) {
    if (cyl < 0) return -1;
    if (topSide) return cyl < 72 ? 2 * cyl + 1 : -1;
    if (cyl < 72) return 2 * cyl;
    return cyl <= 76 ? cyl + 72 : -1;
}

int FdcPlusHdf::headTrack() const {
    if (sel_ < 0) return -1;
    return imageTrack(drive_[(size_t)sel_].cyl, top());
}

bool FdcPlusHdf::formatted(int t) const {
    if (sel_ < 0 || t < 0) return false;
    const auto& d = drive_[(size_t)sel_];
    return d.img && (uint64_t)(t + 1) * kTrackBytes <= d.img->size();
}

// ---------------------------------------------------------------------------
// POWER-ON. The PIC starts in enterIdle: nothing selected, both registers FF.
// ---------------------------------------------------------------------------
void FdcPlusHdf::power() {
    for (auto& d : drive_) d.cyl = 0;  // the heads are wherever; a restore finds them
    idle();
    readData_   = 0x00;
    curTrack_   = 0;
    motorTimer_ = 0;
    moving_     = false;
    settling_   = false;
    rd_ = wr_ = kBootImage;
}

// enterIdle: deselect, motor off, every flag false.
void FdcPlusHdf::idle() {
    status_     = 0xFF;
    sectorPos_  = 0xFF;
    ioStatus_   = 0x00;
    sel_        = -1;
    motor_      = false;
    readFlag_   = false;
    writeFlag_  = false;
    headLoaded_ = false;
    sector_     = 0xC0;
    phase_      = Phase::Off;
    trueOn_     = false;
    skipHole_   = false;
}

// waitMotor: the motor on (the drives keep turning if it already was), and the first index
// hole thrown away -- a transfer in progress is simply abandoned.
void FdcPlusHdf::startMotor() {
    const uint64_t t   = now();
    const uint64_t rev = us(kRevUs);
    if (!motor_) {
        motor_    = true;
        spinFrom_ = t;
    }
    nextHole_ = spinFrom_ + ((t - spinFrom_) / rev + 1) * rev;
    skipHole_ = true;
    phase_    = Phase::Loop;
    trueOn_   = false;
}

// ---------------------------------------------------------------------------
// THE CARD, RUN FORWARD TO t. Every timer the firmware has, in the order they fall due:
// the step and settle timers, the end of sector true, the read-clear window, the end of a
// transfer, and the index holes of a turning disk.
// ---------------------------------------------------------------------------
void FdcPlusHdf::advance(uint64_t t) {
    const uint64_t rev = us(kRevUs);
    for (;;) {
        enum Ev { None, Move, Settle, TrueEnd, Window, XferEnd, Hole } ev = None;
        uint64_t at = UINT64_MAX;
        auto     consider = [&](bool live, uint64_t when, Ev e) {
            if (live && when <= t && when < at) {
                at = when;
                ev = e;
            }
        };
        consider(moving_, moveAt_, Move);
        consider(settling_, settleAt_, Settle);
        consider(trueOn_, trueUntil_, TrueEnd);
        consider(phase_ == Phase::Window,
                 holeAt_ + us(writeFlag_ ? kSecTrueUs : kReadClearUs), Window);
        consider(phase_ == Phase::Reading && !noSync_,
                 holeAt_ + us(kDataUs + kByteUs * (kTrackBytes + 2)), XferEnd);
        consider(phase_ == Phase::Writing,
                 holeAt_ + us(kDataUs + kByteUs * (kTrackBytes + 5)), XferEnd);
        consider(spinning(), nextHole_, Hole);
        if (ev == None) break;

        switch (ev) {
        case Move:  // headMoveInt
            moving_ = false;
            if (!(status_ & dsREADY)) {
                if (sel_ >= 0 && drive_[(size_t)sel_].cyl == 0) status_ &= (uint8_t)~dsTRACK0;
                else status_ |= dsTRACK0;
                status_ &= (uint8_t)~dsMOVE;
            }
            break;
        case Settle:  // headStatusInt
            settling_ = false;
            if (headLoaded_) status_ &= (uint8_t)~dsHEAD;
            break;
        case TrueEnd:
            trueOn_ = false;
            sectorPos_ |= 0x01;  // "de-assert sector true flag"
            break;
        case Window:  // wtReadClr: a write enable wins while it lasts; then the read, or nothing
            if (writeFlag_) startWrite(at);
            else startRead(at);
            break;
        case XferEnd:
            if (phase_ == Phase::Reading) finishRead();
            else finishWrite();
            break;
        case Hole: {
            // A LONG QUIET STRETCH IS DONE IN ONE STEP. With the head loaded and nothing
            // asked for, one turn is exactly like the last but for which of sector 0 and 15
            // it shows, so all but the last two are skipped; an hour of CP/M sitting at A>
            // is not 21,600 turns of arithmetic at the next IN.
            if (phase_ == Phase::Loop && headLoaded_ && !readFlag_ && !writeFlag_ && !skipHole_ &&
                !moving_ && !settling_) {
                const uint64_t k = (t - at) / rev;
                if (k > 2) {
                    if ((k - 1) & 1) sector_ ^= 0x1E;
                    nextHole_ = at + (k - 1) * rev;
                    break;
                }
            }
            nextHole_ = at + rev;
            hole(at);
            break;
        }
        case None: break;
        }
    }
    if (phase_ == Phase::Reading) fillTo(t);
    else if (phase_ == Phase::Writing) drainTo(t);
}

// ---------------------------------------------------------------------------
// ONE INDEX HOLE -- sectorLoop.
// ---------------------------------------------------------------------------
void FdcPlusHdf::hole(uint64_t h) {
    // A read that never found its sync byte ends here: "unexpected end of sector", done, and
    // NRDA forced true "so the 8080 is not stuck in a loop". This hole is used up.
    if (phase_ == Phase::Reading) {
        ioStatus_ |= iosEOS_ERR | iosDONE;
        status_ &= (uint8_t)~dsNRDA;
        phase_ = Phase::Loop;
        return;
    }
    if (skipHole_) {  // waitMotor's "get a first pulse"
        skipHole_ = false;
        return;
    }

    // THE FAKE BOOT SECTOR. No HD read asked for, so point the read pointer at the Altair
    // sector in bootImage, put out its first byte, and assert NRDA. A read enable set BEFORE
    // this hole is cleared here and never happens: the 8080 must ask after the hole.
    const bool wasRead = readFlag_;
    readFlag_          = false;
    if (!wasRead) {
        rd_       = 0;
        readData_ = mem_[rd_++];
        status_ &= (uint8_t)~dsNRDA;
    }

    // The motor goes off after 32 turns with the head not loaded (5.3 s).
    if (headLoaded_) motorTimer_ = 0;
    if (++motorTimer_ & 32) motor_ = false;

    // Sector 0 and sector 15 in turn -- DBL wants 0, CDBL looks for 0 and 15 -- shown only
    // when the head is loaded and settled. Sector true for 32 us.
    sector_ ^= 0x1E;
    if (!(status_ & dsHEAD)) sectorPos_ = sector_;
    trueOn_    = true;
    trueUntil_ = h + us(kSecTrueUs);
    holeAt_    = h;
    phase_     = Phase::Window;
}

// initRead, at the end of read clear -- if a read enable came in after the hole.
void FdcPlusHdf::startRead(uint64_t) {
    if (!readFlag_) {
        phase_ = Phase::Loop;
        return;
    }
    phase_     = Phase::Reading;
    rd_ = wr_  = kBootImage;
    ioStatus_  = 0x00;
    done_      = 0;
    xferTrack_ = headTrack();
    noSync_    = !formatted(xferTrack_);
    xferCyl_   = sel_ >= 0 ? drive_[(size_t)sel_].cyl : 0;
    xferSide_  = top() ? 1 : 0;
    xferFault_ = -1;
    if (noSync_) return;  // nothing on the track to spoil

    // A BAD SPOT (#969). One kind a read: missing, then id, then data -- the worst first.
    if (DiskFault* f = findFault(FaultKind::Missing)) {
        noSync_ = true;  // the firmware hunts for a sync byte that is not there
        useFault(*f);
    } else if (DiskFault* g = findFault(FaultKind::Id)) {
        xferFault_ = (int)FaultKind::Id;  // fillTo() raises the track error
        useFault(*g);
    } else if (findFault(FaultKind::Data)) {
        xferFault_ = (int)FaultKind::Data;  // used at the checksum, in finishRead()
    }
}

// The bytes that have come off the disk by t: the sync byte, then one every 16 us, into the
// track buffer. NRDA after the fifth. The sync byte carries the track the disk was written
// as; a track that is not the one the 8080 named is a TRACK error.
void FdcPlusHdf::fillTo(uint64_t t) {
    if (noSync_ || t < holeAt_) return;
    const uint64_t el = (t - holeAt_) * 1000000 / (uint64_t)clock_->hz();
    if (el < kDataUs) return;
    const int n = (int)std::min<uint64_t>(kTrackBytes, (el - kDataUs) / kByteUs);
    if (done_ == 0 && (((xferTrack_ ^ curTrack_) & 0x7F) || xferFault_ == (int)FaultKind::Id))
        ioStatus_ |= iosTRACK_ERR;
    if (n <= done_) return;
    drive_[(size_t)sel_].img->readAt((uint64_t)xferTrack_ * kTrackBytes + (uint64_t)done_,
                                     &mem_[kBootImage + (size_t)done_], (size_t)(n - done_));
    if (xferFault_ == (int)FaultKind::Data && done_ <= kFaultDataByte && kFaultDataByte < n)
        mem_[kBootImage + kFaultDataByte] ^= 0xFF;  // the bad spot: the sum cannot match
    if (done_ < 5 && n >= 5) status_ &= (uint8_t)~dsNRDA;
    done_ = n;
    wr_   = kBootImage + (size_t)n;
}

void FdcPlusHdf::finishRead() {
    fillTo(holeAt_ + us(kDataUs + kByteUs * (kTrackBytes + 2)));
    ioStatus_ |= iosDONE;
    phase_ = Phase::Loop;
    if (xferFault_ == (int)FaultKind::Data) {  // the firmware's sum does not match the disk's
        ioStatus_ |= iosCKSUM_ERR;
        if (DiskFault* f = findFault(FaultKind::Data)) useFault(*f);
    }
    xferFault_ = -1;
}

// initWrite. ENWD, and the 8080 fills the buffer while the drive writes zeros to the end of
// write clear; then the sync byte, and one byte taken from the buffer every 16 us.
void FdcPlusHdf::startWrite(uint64_t) {
    phase_     = Phase::Writing;
    rd_ = wr_  = kBootImage;
    status_ &= (uint8_t)~dsENWD;
    ioStatus_  = 0x00;
    done_      = 0;
    xferTrack_ = headTrack();
    xferCyl_   = sel_ >= 0 ? drive_[(size_t)sel_].cyl : 0;
    xferSide_  = top() ? 1 : 0;
    xferFault_ = findFault(FaultKind::Write) ? (int)FaultKind::Write : -1;
}

// The bytes the disk has taken by t, as they were in the buffer at that moment: a 8080 that
// falls behind has old bytes written in its place, as on the card.
void FdcPlusHdf::drainTo(uint64_t t) {
    if (t < holeAt_) return;
    const uint64_t el = (t - holeAt_) * 1000000 / (uint64_t)clock_->hz();
    if (el < kDataUs) return;
    const int n = (int)std::min<uint64_t>(kTrackBytes, (el - kDataUs) / kByteUs + 1);
    if (n <= done_) return;
    std::memcpy(&pending_[(size_t)done_], &mem_[kBootImage + (size_t)done_], (size_t)(n - done_));
    done_ = n;
}

// The write is done. The drive's write gate is what a write-protected disk blocks, so the
// bytes go nowhere, and a head over a place the layout has no track for writes to nothing
// the image can hold.
void FdcPlusHdf::finishWrite() {
    drainTo(holeAt_ + us(kDataUs + kByteUs * (kTrackBytes + 5)));
    writeFlag_ = false;
    ioStatus_ |= iosDONE;
    phase_ = Phase::Loop;
    if (xferFault_ == (int)FaultKind::Write) {  // the drive does not take it (#969)
        xferFault_ = -1;
        if (DiskFault* f = findFault(FaultKind::Write)) {
            useFault(*f);
            return;
        }
    }
    xferFault_ = -1;
    if (sel_ < 0 || xferTrack_ < 0) return;
    auto& d = drive_[(size_t)sel_];
    if (!d.img || d.img->readOnly()) return;
    const uint64_t end = (uint64_t)(xferTrack_ + 1) * kTrackBytes;
    if (d.img->size() < end && !d.img->resize(end)) return;
    d.img->writeAt((uint64_t)xferTrack_ * kTrackBytes, pending_.data(), pending_.size());
    d.img->sync();
}

// ---------------------------------------------------------------------------
// The bus.
// ---------------------------------------------------------------------------
uint8_t FdcPlusHdf::read(int off) {
    advance(now());
    switch (off) {
    case 0: return status_;
    case 1: return sectorPos_;
    case 2: {  // chkReadData: the next byte out, whatever is there -- no handshake
        const uint8_t v = readData_;
        readData_       = rd_ < mem_.size() ? mem_[rd_] : 0x00;
        ++rd_;
        return v;
    }
    default: return ioStatus_;
    }
}

void FdcPlusHdf::write(int off, uint8_t v) {
    advance(now());
    switch (off) {
    case 0: select(v); break;
    case 1: command(v); break;
    case 2:  // chkWriteData: into the buffer at the insert pointer, no questions asked
        if (phase_ != Phase::Reading && wr_ < mem_.size()) mem_[wr_++] = v;
        break;
    default: curTrack_ = v; break;  // the track number, which also picks the side
    }
}

// DRIVE SELECT. Bit 7, or a drive past the fourth, deselects. The drive already selected is
// left alone; a new one comes up at F5 with its own TRACK 0 and WRITE PROTECT lines.
void FdcPlusHdf::select(uint8_t v) {
    if (v & 0x80) return idle();
    const int d = v & 0x0F;
    if (d >= kDrives) return idle();
    if (d == sel_) return;

    sel_    = d;
    status_ = dsINIT;
    if (drive_[(size_t)d].cyl == 0) status_ &= (uint8_t)~dsTRACK0;
    if (readOnly(d)) status_ &= (uint8_t)~dsWP;
    sectorPos_  = 0xFF;
    motorTimer_ = 0;
    readFlag_ = writeFlag_ = headLoaded_ = false;
    startMotor();
}

// THE DRIVE COMMAND (chkDriveCmd). A step or a head load or unload, and nothing else in the
// same byte counts; otherwise read enable, and otherwise write enable.
void FdcPlusHdf::command(uint8_t v) {
    if (status_ & dsREADY) return;  // no drive selected: ignored

    if (v & 0x0F) {
        status_ |= dsHEAD;
        sectorPos_ = 0xFF;
        if (v & 0x03) {
            status_ |= dsMOVE;
            int& c = drive_[(size_t)sel_].cyl;
            if (v & 0x02) {
                if (c > 0) --c;  // no step pulse at all on track 0
            } else if (c < kCylinders - 1) {
                ++c;
            }
            const uint64_t t = now();
            moveAt_   = t + us(kStepUs);
            settleAt_ = t + us(kSettleUs);
            moving_ = settling_ = true;
            return;  // a head load in the same command is not looked at
        }
        if (v & 0x04) {
            status_ &= (uint8_t)~dsHEAD;
            headLoaded_ = true;
            if (!motor_) startMotor();  // "turn on the motor", and wait for it
        } else if (v & 0x08) {
            headLoaded_ = false;
        }
        return;
    }
    if (v & 0x10) {
        readFlag_ = true;
        status_ |= dsNRDA;
        return;
    }
    if (v & 0x80) {
        writeFlag_ = true;
        status_ |= dsENWD;
        // Inside this turn's window already, past sector true: the write starts now.
        const uint64_t t = now();
        if (phase_ == Phase::Window && t >= holeAt_ + us(kSecTrueUs)) startWrite(t);
    }
}

// ---------------------------------------------------------------------------
// The disks.
// ---------------------------------------------------------------------------
bool FdcPlusHdf::mount(int i, std::unique_ptr<MediaFile> m, const std::string& path,
                       std::string& err) {
    const uint64_t n = m->size();
    if (n % kTrackBytes || n > kImageBytes) {
        err = path + " is not a 1.5 MB FDC+ disk image: that is whole 10,240-byte tracks, " +
              "149 of them at most (1,525,760 bytes)";
        return false;
    }
    auto& d = drive_[(size_t)i];
    if (d.img) d.img->sync();
    d.img  = std::move(m);
    d.path = path;
    d.faults.clearAll();  // the faults were the old disk's
    if (sel_ == i) {  // the write-protect line is the drive's, and it changed
        if (d.img->readOnly()) status_ &= (uint8_t)~dsWP;
        else status_ |= dsWP;
    }
    return true;
}

bool FdcPlusHdf::unmount(int i, std::string& err) {
    auto& d = drive_[(size_t)i];
    if (!d.img) {
        err = "drive" + std::to_string(i) + " is empty";
        return false;
    }
    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.faults.clearAll();
    if (sel_ == i) status_ |= dsWP;
    return true;
}

// ---------------------------------------------------------------------------
// BAD SPOTS (#969) -- see the header.
// ---------------------------------------------------------------------------
DiskFaults* FdcPlusHdf::faults(int i) {
    if (i < 0 || i >= kDrives || !drive_[(size_t)i].img) return nullptr;
    return &drive_[(size_t)i].faults;
}

DiskFault* FdcPlusHdf::findFault(FaultKind k) {
    if (sel_ < 0) return nullptr;
    auto& d = drive_[(size_t)sel_];
    if (d.faults.empty()) return nullptr;
    return d.faults.find(xferCyl_, xferSide_, 0, k);
}

void FdcPlusHdf::useFault(DiskFault& f) {
    DiskFaults::use(f);
    if (faultHook_) faultHook_(sel_, xferCyl_, xferSide_, f);
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE. The registers, the buffer and the timers; the disks are their files.
// ---------------------------------------------------------------------------
void FdcPlusHdf::serialize(StateWriter& w) const {
    w.u8(status_);
    w.u8(sectorPos_);
    w.u8(readData_);
    w.u8(ioStatus_);
    w.u8(curTrack_);
    w.u8((uint8_t)(sel_ + 1));
    w.boolean(motor_);
    w.boolean(readFlag_);
    w.boolean(writeFlag_);
    w.boolean(headLoaded_);
    w.u8((uint8_t)motorTimer_);
    w.u8(sector_);
    for (const auto& d : drive_) w.u8((uint8_t)d.cyl);
    w.raw(mem_.data(), mem_.size());
    w.raw(pending_.data(), pending_.size());
    w.u32((uint32_t)rd_);
    w.u32((uint32_t)wr_);
    w.u8((uint8_t)phase_);
    w.u64(spinFrom_);
    w.u64(nextHole_);
    w.boolean(skipHole_);
    w.u64(holeAt_);
    w.u64(trueUntil_);
    w.boolean(trueOn_);
    w.u32((uint32_t)done_);
    w.boolean(noSync_);
    w.u32((uint32_t)(xferTrack_ + 1));
    w.u64(moveAt_);
    w.boolean(moving_);
    w.u64(settleAt_);
    w.boolean(settling_);
    for (const auto& d : drive_) d.faults.serialize(w);  // the bad spots and their counts (#969)
    w.u8((uint8_t)(xferFault_ + 1));
    w.u8((uint8_t)xferCyl_);
    w.u8((uint8_t)xferSide_);
}

void FdcPlusHdf::deserialize(StateReader& r) {
    status_     = r.u8();
    sectorPos_  = r.u8();
    readData_   = r.u8();
    ioStatus_   = r.u8();
    curTrack_   = r.u8();
    sel_        = std::min((int)r.u8(), kDrives) - 1;
    motor_      = r.boolean();
    readFlag_   = r.boolean();
    writeFlag_  = r.boolean();
    headLoaded_ = r.boolean();
    motorTimer_ = r.u8();
    sector_     = r.u8();
    for (auto& d : drive_) d.cyl = std::min((int)r.u8(), kCylinders - 1);
    r.raw(mem_.data(), mem_.size());
    r.raw(pending_.data(), pending_.size());
    rd_         = r.u32();
    wr_         = r.u32();
    const uint8_t p = r.u8();
    phase_      = p <= (uint8_t)Phase::Writing ? (Phase)p : Phase::Off;
    spinFrom_   = r.u64();
    nextHole_   = r.u64();
    skipHole_   = r.boolean();
    holeAt_     = r.u64();
    trueUntil_  = r.u64();
    trueOn_     = r.boolean();
    done_       = std::min((int)r.u32(), kTrackBytes);
    noSync_     = r.boolean();
    xferTrack_  = std::min((int)r.u32(), kTracks) - 1;
    moveAt_     = r.u64();
    moving_     = r.boolean();
    settleAt_   = r.u64();
    settling_   = r.boolean();
    for (auto& d : drive_) {
        DiskFaults table;
        table.deserialize(r);
        d.faults = d.img ? std::move(table) : DiskFaults{};  // a fault belongs to a disk
    }
    const int k = (int)r.u8() - 1;
    xferFault_  = k >= 0 && k <= (int)FaultKind::Write ? k : -1;
    xferCyl_    = std::min((int)r.u8(), kCylinders - 1);
    xferSide_   = r.u8() & 1;
    // A transfer from a disk that is not in the drive now cannot go on.
    if ((phase_ == Phase::Reading || phase_ == Phase::Writing) &&
        (sel_ < 0 || !drive_[(size_t)sel_].img)) {
        phase_ = Phase::Loop;
    }
    if (phase_ == Phase::Reading && !noSync_ && !formatted(xferTrack_)) noSync_ = true;
}

} // namespace altair
