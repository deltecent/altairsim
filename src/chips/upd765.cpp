#include "chips/upd765.h"

#include "core/clock.h"
#include "core/statefile.h"

#include <algorithm>

namespace altair {

namespace {

// The command table (§3): the opcode with its flag bits clear, the flag bits it may
// carry, and how many bytes it takes, byte 1 included. "Any other code" is invalid,
// and so is a known code with a flag bit the table does not give it.
struct Op {
    uint8_t code, flags, len;
};
constexpr Op kOps[] = {
    {0x06, 0xE0, 9},  // Read Data            MT MF SK
    {0x0C, 0xE0, 9},  // Read Deleted Data    MT MF SK
    {0x05, 0xC0, 9},  // Write Data           MT MF
    {0x09, 0xC0, 9},  // Write Deleted Data   MT MF
    {0x02, 0x60, 9},  // Read a Track            MF SK
    {0x0A, 0x40, 2},  // Read ID                 MF
    {0x0D, 0x40, 6},  // Format a Track          MF
    {0x11, 0xE0, 9},  // Scan Equal           MT MF SK
    {0x19, 0xE0, 9},  // Scan Low or Equal    MT MF SK
    {0x1D, 0xE0, 9},  // Scan High or Equal   MT MF SK
    {0x07, 0x00, 2},  // Recalibrate
    {0x08, 0x00, 1},  // Sense Interrupt Status
    {0x03, 0x00, 3},  // Specify
    {0x04, 0x00, 2},  // Sense Drive Status
    {0x0F, 0x00, 3},  // Seek
};

const Op* lookup(uint8_t b) {
    for (const Op& o : kOps)
        if ((b & 0x1F) == o.code && (b & 0xE0 & ~o.flags) == 0) return &o;
    return nullptr;
}

bool isScan(int op) { return op == 0x11 || op == 0x19 || op == 0x1D; }

} // namespace

Upd765::Upd765(std::string name) : name_(std::move(name)) { powerOn(); }

// ---------------------------------------------------------------------------
// Time. Every Specify interval is in milliseconds at 8 MHz, and doubles at 4 MHz (§4).
// ---------------------------------------------------------------------------
uint64_t Upd765::now() const { return clk_ ? clk_->now() : 0; }

uint64_t Upd765::msT(int ms) const {
    if (!clk_) return 0;
    const long long fdc = fdcClockHz > 0 ? fdcClockHz : 8000000;
    return (uint64_t)(clk_->hz() * (long long)ms / 1000 * 8000000 / fdc);
}

// SRT: 16 - n ms (F = 1 ms). HUT: n x 16 ms. HLT: n x 2 ms. The data sheet gives no
// meaning to 0 for HUT or HLT (§4 ⚠); 0 is taken as one past the largest code, as a
// 4-bit or 7-bit down-counter loaded with 0 would run (256 ms for both). SRT 0 is 16 ms
// by its own formula.
static int srtMs(uint8_t srt) { return 16 - srt; }
static int hutMs(uint8_t hut) { return (hut ? hut : 16) * 16; }
static int hltMs(uint8_t hlt) { return (hlt ? hlt : 128) * 2; }

// ---------------------------------------------------------------------------
// The drives, through the board's wiring.
// ---------------------------------------------------------------------------
FloppyDrive* Upd765::drv() const { return wiring.drive ? wiring.drive(us_) : nullptr; }

bool Upd765::driveReady(int us) const {
    FloppyDrive* d = wiring.drive ? wiring.drive(us) : nullptr;
    return d && d->ready();
}

bool Upd765::driveTwoSided(int us) const {
    if (wiring.twoSided) return wiring.twoSided(us);
    FloppyDrive* d = wiring.drive ? wiring.drive(us) : nullptr;
    return d && d->twoSided();
}

void Upd765::selectHead() {
    if (wiring.head) wiring.head(us_, hd_);
}

// ---------------------------------------------------------------------------
// Reset and power.
// ---------------------------------------------------------------------------
void Upd765::reset() {
    phase_  = Phase::Command;
    cmdGot_ = cmdLen_ = 0;
    xfer_   = Xfer::None;
    toMem_  = false;
    buf_.clear();
    idx_ = want_ = 0;
    tc_ = needNext_ = cmStop_ = crcStop_ = false;
    resLen_ = resIdx_ = 0;
    resultInt_ = false;
    loadedUs_  = -1;
    unloadAt_  = 0;
    for (int u = 0; u < 4; ++u) {
        seek_[(size_t)u]      = Seek{};
        pending_[(size_t)u]   = -1;
        seekEnd_[(size_t)u]   = false;
        busy_[(size_t)u]      = false;
        lastReady_[(size_t)u] = driveReady(u);
    }
    // "If RDY pin is held high during Reset, FDC will generate an interrupt within 1.024
    // ms" (§6). The unit-select pins are low in reset, so RDY is drive 0's.
    if (lastReady_[0]) pending_[0] = kSt0Ai;
}

void Upd765::powerOn() {
    srt_ = hut_ = hlt_ = 0;
    nd_        = false;
    specified_ = false;
    pcn_       = {};
    dr_        = 0;
    idCursor_  = 0;
    reset();
}

// ---------------------------------------------------------------------------
// The Main Status Register (§2).
// ---------------------------------------------------------------------------
bool Upd765::moving() const {
    return phase_ == Phase::Exec && xfer_ != Xfer::None && (needNext_ || idx_ < want_);
}

uint8_t Upd765::readMsr() {
    poll();
    uint8_t v = 0;
    for (int u = 0; u < 4; ++u)
        if (busy_[(size_t)u]) v |= (uint8_t)(1u << u);
    switch (phase_) {
        case Phase::Command:
            v |= kMsrRqm;
            if (cmdGot_ > 0) v |= kMsrCb;
            break;
        case Phase::Wait:
            v |= kMsrCb;
            if (nd_) v |= kMsrExm;
            break;
        case Phase::Exec:
            v |= kMsrCb;
            if (nd_) {
                v |= kMsrExm;
                if (moving()) v |= (uint8_t)(kMsrRqm | (toMem_ ? kMsrDio : 0));
            }
            break;
        case Phase::Result:
            v |= kMsrRqm | kMsrDio | kMsrCb;
            break;
    }
    return v;
}

bool Upd765::drq() const { return !nd_ && moving(); }

bool Upd765::intPin() const {
    if (resultInt_) return true;
    if (nd_ && moving()) return true;
    for (int p : pending_)
        if (p >= 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// The data register.
// ---------------------------------------------------------------------------
uint8_t Upd765::readData() {
    poll();
    if (phase_ == Phase::Result) {
        dr_        = res_[(size_t)resIdx_++];
        resultInt_ = false;  // "Reading the first result byte clears INT" (§6)
        if (resIdx_ >= resLen_) {
            phase_  = Phase::Command;
            cmdGot_ = 0;
        }
        return dr_;
    }
    if (nd_ && phase_ == Phase::Exec && toMem_) {
        uint8_t v = 0;
        if (byteOut(v)) dr_ = v;
    }
    return dr_;
}

void Upd765::writeData(uint8_t v) {
    poll();
    if (phase_ == Phase::Command) {
        dr_ = v;
        if (cmdGot_ == 0) {
            cmd_[0] = v;
            cmdGot_ = 1;
            startCommand();
            return;
        }
        cmd_[(size_t)cmdGot_++] = v;
        if (cmdGot_ >= cmdLen_) execute();
        return;
    }
    if (nd_ && phase_ == Phase::Exec && !toMem_ && moving()) {
        dr_ = v;
        byteIn(v);
    }
}

// ---------------------------------------------------------------------------
// DACK and TC (§6).
// ---------------------------------------------------------------------------
bool Upd765::dackRead(uint8_t& out) {
    poll();
    if (!drq() || !toMem_) return false;
    if (!byteOut(out)) return false;
    dr_ = out;
    return true;
}

void Upd765::dackWrite(uint8_t v) {
    poll();
    if (!drq() || toMem_) return;
    dr_ = v;
    byteIn(v);
}

void Upd765::terminalCount() {
    if (phase_ != Phase::Exec) return;
    if (xfer_ != Xfer::Read && xfer_ != Xfer::Write && xfer_ != Xfer::Scan) return;
    tc_ = true;
    if (needNext_) {
        // TC on the sector boundary: the sector just moved was the last.
        needNext_ = false;
        resultIdAfter(r_ == eot_);
        endData(kSt0Nt);
        return;
    }
    if (xfer_ == Xfer::Scan) {
        // "The FDC completes the comparison of the byte in process, then ends" (§7).
        endData(kSt0Nt);
        return;
    }
    // A read sends no more bytes but finishes the sector; a write fills the rest with
    // 00 (buf_ was zeroed when the sector opened). Either way the sector is done now.
    sectorDone();
}

// ---------------------------------------------------------------------------
// The command phase.
// ---------------------------------------------------------------------------
void Upd765::startCommand() {
    const Op* o = lookup(cmd_[0]);
    if (!o) {
        invalid();
        return;
    }
    // "A Sense Interrupt Status must follow a Seek or Recalibrate interrupt. If it does
    // not, the FDC treats the next command as invalid" (§6).
    if (o->code != 0x08)
        for (bool e : seekEnd_)
            if (e) {
                invalid();
                return;
            }
    cmdLen_ = o->len;
    if (cmdGot_ >= cmdLen_) execute();
}

void Upd765::invalid() {
    // ST0 = 80H, one result byte, and no interrupt (§7 Invalid).
    res_[0] = kSt0Ic;
    result(1, false);
}

void Upd765::result(int n, bool irq) {
    phase_     = Phase::Result;
    resLen_    = n;
    resIdx_    = 0;
    resultInt_ = irq;
}

uint8_t Upd765::st0(uint8_t ic) const { return (uint8_t)(ic | (hd_ << 2) | us_); }

void Upd765::execute() {
    const int o = op();
    if (cmdLen_ >= 2) {
        us_ = cmd_[1] & 0x03;
        hd_ = (o == 0x07) ? 0 : (cmd_[1] >> 2) & 1;
    }
    switch (o) {
        case 0x03:  // Specify
            srt_       = (uint8_t)(cmd_[1] >> 4);
            hut_       = (uint8_t)(cmd_[1] & 0x0F);
            hlt_       = (uint8_t)(cmd_[2] >> 1);
            nd_        = (cmd_[2] & 1) != 0;
            specified_ = true;
            phase_     = Phase::Command;
            cmdGot_    = 0;
            return;

        case 0x04: {  // Sense Drive Status: ST3, no interrupt
            selectHead();
            FloppyDrive* d  = drv();
            uint8_t      s3 = (uint8_t)((hd_ << 2) | us_);
            if (d) {
                if (d->writeFault()) s3 |= kSt3Ft;
                if (d->writeProtected()) s3 |= kSt3Wp;
                if (d->ready()) s3 |= kSt3Ry;
                if (d->trackZero()) s3 |= kSt3T0;
            }
            if (driveTwoSided(us_)) s3 |= kSt3Ts;
            res_[0] = s3;
            result(1, false);
            return;
        }

        case 0x08: {  // Sense Interrupt Status
            for (int u = 0; u < 4; ++u) {
                if (pending_[(size_t)u] < 0) continue;
                res_[0]               = (uint8_t)pending_[(size_t)u];
                res_[1]               = (uint8_t)pcn_[(size_t)u];
                pending_[(size_t)u]   = -1;
                seekEnd_[(size_t)u]   = false;
                busy_[(size_t)u]      = false;
                result(2, false);
                return;
            }
            invalid();  // "with no interrupt pending is an invalid command" (§6)
            return;
        }

        case 0x07:    // Recalibrate
        case 0x0F: {  // Seek
            Seek& s  = seek_[(size_t)us_];
            s        = Seek{};
            s.active = true;
            s.recal  = (o == 0x07);
            s.target = (o == 0x0F) ? cmd_[2] : 0;
            s.hd     = hd_;
            s.due    = now();
            busy_[(size_t)us_]    = true;
            pending_[(size_t)us_] = -1;  // see pollReady(): one status per drive
            selectHead();
            // The execution phase is NOT BUSY: another drive's Seek may come now (§7).
            phase_  = Phase::Command;
            cmdGot_ = 0;
            stepDrives();
            return;
        }
        default: break;
    }

    // ---- the commands with an execution phase and a 7-byte result ----
    c_ = cmd_[2], h_ = cmd_[3], r_ = cmd_[4], n_ = cmd_[5];
    eot_ = cmd_[6], gpl_ = cmd_[7], dtl_ = cmd_[8];
    if (o == 0x0A) c_ = h_ = r_ = n_ = 0;
    if (o == 0x0D) {  // Format: HD/US, N, SC, GPL, D
        n_ = cmd_[2], eot_ = cmd_[3], gpl_ = cmd_[4], dtl_ = cmd_[5];
        c_ = h_ = r_ = 0;
    }
    st1_ = st2_ = 0;
    tc_ = needNext_ = cmStop_ = crcStop_ = false;
    xfer_ = Xfer::None;

    // "If any of D0B-D3B is set, the FDC will not accept a read or write command" (§2).
    for (bool b : busy_)
        if (b) {
            invalid();
            return;
        }

    selectHead();
    // Not ready, or side 1 of a single-sided drive: ST0 NR (§5).
    if (!driveReady(us_) || (hd_ && !driveTwoSided(us_))) {
        endData(kSt0At | kSt0Nr);
        return;
    }
    if ((o == 0x05 || o == 0x09 || o == 0x0D) && drv()->writeProtected()) {
        st1_ = kSt1Nw;
        endData(kSt0At);
        return;
    }

    // The head: loaded already (a new command inside HUT skips the load wait), or HLT.
    if (loadedUs_ == us_ && now() < unloadAt_) {
        phase_ = Phase::Exec;
        beginData();
        return;
    }
    loadedUs_ = us_;
    phase_    = Phase::Wait;
    due_      = now() + msT(hltMs(hlt_));
    poll();
}

// ---------------------------------------------------------------------------
// The execution phase of the data commands (§7).
// ---------------------------------------------------------------------------
int Upd765::dataLen() const {
    if (n_ == 0 && !isScan(op()) && dtl_ > 0 && dtl_ < 128) return dtl_;
    return 128 << std::min<int>(n_, 7);
}

void Upd765::beginData() {
    phase_    = Phase::Exec;
    unloadAt_ = ~0ull;  // the head stays loaded while the command runs
    switch (op()) {
        case 0x0A: readId(); return;
        case 0x0D:
            xfer_  = Xfer::Format;
            toMem_ = false;
            buf_.assign((size_t)eot_ * 4, 0);
            idx_  = 0;
            want_ = buf_.size();
            if (want_ == 0) sectorDone();
            return;
        case 0x05:
        case 0x09:
            xfer_  = Xfer::Write;
            toMem_ = false;
            break;
        case 0x02:
            xfer_     = Xfer::Read;
            toMem_    = true;
            tracked_  = 0;
            idCursor_ = 0;  // "after the index hole"
            break;
        default:
            xfer_  = isScan(op()) ? Xfer::Scan : Xfer::Read;
            toMem_ = (xfer_ == Xfer::Read);
            break;
    }
    openSector();
}

void Upd765::readId() {
    FloppyDrive* d = drv();
    const int    n = d->sectorCount();
    for (int k = 0; k < n; ++k) {
        FloppyDrive::SectorId id;
        const int at = (idCursor_ + k) % n;
        if (!d->sectorIdAt(at, id) || !legible(id) || !id.idCrcOk) continue;
        idCursor_ = (at + 1) % n;  // the disk has turned past it
        c_ = (uint8_t)id.track, h_ = (uint8_t)idHead(id), r_ = (uint8_t)id.sector;
        n_ = (uint8_t)id.lengthCode;
        endData(kSt0Nt);
        return;
    }
    st1_ = kSt1Ma | kSt1Nd;
    endData(kSt0At);
}

bool Upd765::findSector(FloppyDrive::SectorId& out) {
    FloppyDrive* d       = drv();
    const int    n       = d->sectorCount();
    bool         seen    = false;  // any ID field this density can read
    bool         wrongC  = false;
    bool         badC    = false;
    for (int i = 0; i < n; ++i) {
        FloppyDrive::SectorId id;
        if (!d->sectorIdAt(i, id) || !legible(id)) continue;
        seen = true;
        // The ID field's H: what the format wrote, or the side selected when the drive made
        // the ID up. A command whose H is not the ID's finds nothing.
        if (id.sector != r_ || h_ != idHead(id)) continue;
        if (id.track != c_) {
            wrongC = true;
            if (id.track == 0xFF) badC = true;
            continue;
        }
        if (id.lengthCode != n_) continue;
        if (!id.idCrcOk) {
            st1_ |= kSt1De;
            return false;
        }
        out = id;
        return true;
    }
    if (!seen) {
        // No address mark at this density: the index hole twice, and nothing (§5 MA).
        st1_ |= kSt1Ma;
        return false;
    }
    st1_ |= kSt1Nd;
    if (wrongC) st2_ |= kSt2Wc;
    if (badC) st2_ |= kSt2Bc;
    return false;
}

bool Upd765::openSector() {
    if (op() == 0x02) return openTrackSector();
    for (;;) {
        FloppyDrive::SectorId id;
        if (!findSector(id)) {
            endData(kSt0At);
            return false;
        }
        cur_ = id;
        idx_ = 0;
        if (xfer_ == Xfer::Write) {
            buf_.assign((size_t)id.size, 0);
            want_ = std::min<size_t>((size_t)dataLen(), buf_.size());
            return true;
        }
        // Read Data wants the data mark, Read Deleted Data the deleted one. The other is
        // a control mark (§7): with SK it is skipped, without it the sector is read and the
        // command ends. A Scan treats it as the last sector unless SK is set.
        const bool wantDeleted = (op() == 0x0C);
        if (id.deleted != wantDeleted) {
            if (xfer_ == Xfer::Scan) {
                st2_ |= kSt2Cm;
                if (!sk()) {
                    endData(kSt0Nt);
                    return false;
                }
                if (!advance()) return false;
                continue;
            }
            if (sk()) {
                if (!advance()) return false;
                continue;
            }
            st2_ |= kSt2Cm;
            cmStop_ = true;
        }
        buf_.assign((size_t)id.size, 0);
        size_t got = buf_.size();
        if (!drv()->readData(id, buf_.data(), &got) || got != buf_.size()) {
            st1_ |= kSt1De;
            st2_ |= kSt2Dd;
            endData(kSt0At);
            return false;
        }
        if (!id.dataCrcOk) {
            st1_ |= kSt1De;
            st2_ |= kSt2Dd;
            crcStop_ = true;
        }
        want_      = std::min<size_t>((size_t)dataLen(), buf_.size());
        scanEqual_ = scanMet_ = true;
        return true;
    }
}

// Read a Track: every sector in the order it comes round, from the index hole. An ID that
// is not the IDR's sets ND and the read goes on (§7).
bool Upd765::openTrackSector() {
    FloppyDrive* d = drv();
    const int    n = d->sectorCount();
    for (int k = 0; k < n; ++k) {
        FloppyDrive::SectorId id;
        const int at = (idCursor_ + k) % n;
        if (!d->sectorIdAt(at, id) || !legible(id)) continue;
        idCursor_ = (at + 1) % n;
        if (id.track != c_ || h_ != idHead(id) || id.sector != r_ || id.lengthCode != n_)
            st1_ |= kSt1Nd;
        if (!id.idCrcOk || !id.dataCrcOk) {
            st1_ |= kSt1De;
            if (!id.dataCrcOk) st2_ |= kSt2Dd;
        }
        cur_ = id;
        idx_ = 0;
        buf_.assign((size_t)id.size, 0);
        size_t got = buf_.size();
        if (!d->readData(id, buf_.data(), &got) || got != buf_.size()) {
            st1_ |= kSt1De;
            st2_ |= kSt2Dd;
            endData(kSt0At);
            return false;
        }
        want_ = std::min<size_t>((size_t)dataLen(), buf_.size());
        return true;
    }
    st1_ |= kSt1Ma;
    endData(kSt0At);
    return false;
}

// R moves on. At EOT on head 0 with MT the read goes on at sector 1 of head 1, on the
// same cylinder (Table 1). At EOT otherwise there is no next sector: ST1 EN (§5).
bool Upd765::advance() {
    if (r_ == eot_) {
        if (mt() && hd_ == 0) {
            hd_ = 1;
            h_ ^= 1;
            r_ = 1;
            selectHead();
            if (!driveTwoSided(us_)) {
                endData(kSt0At | kSt0Nr);
                return false;
            }
            return true;
        }
        resultIdAfter(true);
        st1_ |= kSt1En;
        endData(kSt0At);
        return false;
    }
    r_ = (uint8_t)(r_ + (isScan(op()) ? (dtl_ ? dtl_ : 1) : 1));
    return true;
}

bool Upd765::nextByte() {
    if (!needNext_) return true;
    needNext_ = false;
    if (op() == 0x02) {
        ++r_;
        return openTrackSector();
    }
    if (!advance()) return false;
    return openSector();
}

bool Upd765::byteOut(uint8_t& v) {
    if (!moving() || !toMem_) return false;
    if (!nextByte()) return false;
    v = buf_[idx_++];
    if (idx_ >= want_) sectorDone();
    return true;
}

void Upd765::byteIn(uint8_t v) {
    if (!moving() || toMem_) return;
    if (!nextByte()) return;
    if (xfer_ == Xfer::Scan) {
        // FF from the processor matches anything. "Ones complement arithmetic": FF is
        // the largest number, 00 the smallest -- plain unsigned order (§7 Scan).
        const uint8_t disk = buf_[idx_];
        if (v != 0xFF) {
            if (disk != v) scanEqual_ = false;
            const bool ok = op() == 0x11 ? disk == v : op() == 0x19 ? disk <= v : disk >= v;
            if (!ok) scanMet_ = false;
        }
        ++idx_;
    } else {
        buf_[idx_++] = v;
    }
    if (idx_ >= want_) sectorDone();
}

// Table 2: the ID the result phase reports after the last sector moved.
void Upd765::resultIdAfter(bool atEot) {
    if (!atEot) {
        r_ = (uint8_t)(r_ + 1);
        return;
    }
    r_ = 1;
    if (mt()) {
        if (hd_ == 1) c_ = (uint8_t)(c_ + 1);
        h_ ^= 1;
    } else {
        c_ = (uint8_t)(c_ + 1);
    }
}

void Upd765::sectorDone() {
    switch (xfer_) {
        case Xfer::Format: {
            // Each sector's C, H, R, N came through the data path, four bytes a sector (§7).
            std::vector<FloppyDrive::SectorId> ids;
            for (size_t i = 0; i + 3 < buf_.size(); i += 4) {
                FloppyDrive::SectorId id;
                id.track         = buf_[i];
                id.head          = buf_[i + 1];
                id.sector        = buf_[i + 2];
                id.lengthCode    = buf_[i + 3];
                id.size          = 128 << std::min<int>(buf_[i + 3], 7);
                id.doubleDensity = mf();
                ids.push_back(id);
            }
            if (!ids.empty()) {
                c_ = (uint8_t)ids.back().track;
                h_ = buf_[buf_.size() - 3];
                r_ = (uint8_t)(ids.back().sector + 1);  // "the last sector number + 1"
                n_ = (uint8_t)ids.back().lengthCode;
            }
            if (ids.empty() || !drv()->formatTrack(ids, dtl_)) {
                endData(kSt0At | kSt0Ec);  // the drive could not record it: a fault
                return;
            }
            endData(kSt0Nt);
            return;
        }
        case Xfer::Write:
            // Write Data writes a data mark and Write Deleted Data a deleted one, whatever
            // mark the sector had before (§7). The drive keeps the mark it is handed.
            cur_.deleted = (op() == 0x09);
            if (!drv()->writeData(cur_, buf_.data(), buf_.size())) {
                endData(kSt0At | kSt0Ec);
                return;
            }
            break;
        case Xfer::Scan:
            if (scanMet_) {
                if (scanEqual_) st2_ |= kSt2Sh;
                endData(kSt0Nt);
                return;
            }
            if (tc_) {
                endData(kSt0Nt);
                return;
            }
            if (r_ == eot_ && !(mt() && hd_ == 0)) {
                st2_ |= kSt2Sn;  // no sector up to EOT met the condition
                endData(kSt0Nt);
                return;
            }
            needNext_ = true;
            return;
        default: break;
    }

    if (crcStop_) {
        endData(kSt0At);
        return;
    }
    if (op() == 0x02) {
        if (tc_) {
            ++r_;
            endData(st1_ ? kSt0At : kSt0Nt);
            return;
        }
        if (++tracked_ >= eot_) {  // "ends when the number of sectors read equals EOT"
            ++r_;
            endData(st1_ ? kSt0At : kSt0Nt);
            return;
        }
        needNext_ = true;
        return;
    }
    if (tc_ || cmStop_) {
        resultIdAfter(r_ == eot_);
        endData(kSt0Nt);
        return;
    }
    // No TC. At EOT with nowhere to go the command ends here, with no DRQ for a byte that
    // will never come.
    if (r_ == eot_ && !(mt() && hd_ == 0)) {
        resultIdAfter(true);
        st1_ |= kSt1En;
        endData(kSt0At);
        return;
    }
    needNext_ = true;
}

void Upd765::endData(uint8_t ic) {
    xfer_     = Xfer::None;
    needNext_ = false;
    idx_ = want_ = 0;
    res_[0] = st0(ic);
    res_[1] = st1_;
    res_[2] = st2_;
    res_[3] = c_;
    res_[4] = h_;
    res_[5] = r_;
    res_[6] = n_;
    result(7, true);
    if (loadedUs_ == us_) unloadAt_ = now() + msT(hutMs(hut_));
}

// ---------------------------------------------------------------------------
// Seek and Recalibrate: one step pulse per SRT, per drive (§7).
// ---------------------------------------------------------------------------
void Upd765::queueSeekEnd(int us, uint8_t st0v) {
    seek_[(size_t)us].active = false;
    pending_[(size_t)us]     = st0v;
    seekEnd_[(size_t)us]     = true;
}

void Upd765::stepDrives() {
    const uint64_t t = now();
    for (int u = 0; u < 4; ++u) {
        Seek& s = seek_[(size_t)u];
        while (s.active && s.due <= t) {
            const uint8_t ids = (uint8_t)((s.hd << 2) | u);
            FloppyDrive*  d   = wiring.drive ? wiring.drive(u) : nullptr;
            if (!d || !d->ready()) {
                queueSeekEnd(u, (uint8_t)(kSt0At | kSt0Se | kSt0Nr | ids));
                break;
            }
            if (s.recal) {
                if (d->trackZero()) {
                    pcn_[(size_t)u] = 0;
                    queueSeekEnd(u, (uint8_t)(kSt0Se | ids));
                    break;
                }
                if (s.pulses >= 77) {  // "Track 0 still low after 77 step pulses"
                    pcn_[(size_t)u] = 0;
                    queueSeekEnd(u, (uint8_t)(kSt0At | kSt0Se | kSt0Ec | ids));
                    break;
                }
                d->step(false);
                ++s.pulses;
            } else {
                int& p = pcn_[(size_t)u];
                if (p == s.target) {
                    queueSeekEnd(u, (uint8_t)(kSt0Se | ids));
                    break;
                }
                const bool in = p < s.target;
                d->step(in);
                p += in ? 1 : -1;
            }
            const uint64_t dt = msT(srtMs(srt_));
            s.due += dt ? dt : 1;
            if (!clk_) s.due = t;  // no clock: every pulse is now
        }
    }
}

// The ready poll (§6): after Specify, each drive between commands. A change raises an
// interrupt for Sense Interrupt Status: IC = 11, and NR if the drive is now not ready.
//
// ONE PENDING STATUS PER DRIVE. The data sheet does not say how two interrupts for one
// drive combine. Here a Seek or Recalibrate clears the drive's pending status when it
// starts, and any later status replaces an earlier one. CompuPro's boot ROM (ROM1.ASM)
// needs exactly that: it gives no Sense Interrupt Status between reset (which raises
// drive 0's ready interrupt, in reset()) and its Recalibrate of drive 0, then polls INT
// to see the Recalibrate end, and requires ST0 = 20H from its first Sense Interrupt Status.
void Upd765::pollReady() {
    if (!specified_ || phase_ == Phase::Wait || phase_ == Phase::Exec) return;
    for (int u = 0; u < 4; ++u) {
        const bool r = driveReady(u);
        if (r == lastReady_[(size_t)u]) continue;
        lastReady_[(size_t)u] = r;
        pending_[(size_t)u]   = kSt0Ai | (r ? 0 : kSt0Nr) | u;
        seekEnd_[(size_t)u]   = false;
    }
}

void Upd765::poll() {
    stepDrives();
    if (phase_ == Phase::Wait && now() >= due_) beginData();
    pollReady();
}

uint64_t Upd765::nextEdge() const {
    uint64_t e = 0;
    auto take  = [&e](uint64_t t) {
        if (e == 0 || t < e) e = t;
    };
    for (const Seek& s : seek_)
        if (s.active) take(s.due ? s.due : 1);
    if (phase_ == Phase::Wait) take(due_ ? due_ : 1);
    return e;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE. The wiring and the clock are the board's, and are not saved.
// ---------------------------------------------------------------------------
void Upd765::serialize(StateWriter& w) const {
    w.u8(srt_), w.u8(hut_), w.u8(hlt_);
    w.boolean(nd_), w.boolean(specified_);
    w.u8((uint8_t)phase_);
    w.raw(cmd_.data(), cmd_.size());
    w.u8((uint8_t)cmdLen_), w.u8((uint8_t)cmdGot_);
    w.u8((uint8_t)us_), w.u8((uint8_t)hd_);
    w.u8(c_), w.u8(h_), w.u8(r_), w.u8(n_), w.u8(eot_), w.u8(gpl_), w.u8(dtl_);
    w.u64(due_);
    w.u8((uint8_t)xfer_);
    w.boolean(toMem_);
    w.blob(buf_);
    w.u32((uint32_t)idx_), w.u32((uint32_t)want_);
    w.boolean(tc_), w.boolean(needNext_), w.boolean(cmStop_), w.boolean(crcStop_);
    w.u8(st1_), w.u8(st2_);
    w.u32((uint32_t)tracked_), w.u32((uint32_t)idCursor_);
    w.u32((uint32_t)cur_.track), w.u32((uint32_t)cur_.sector), w.u32((uint32_t)cur_.size);
    w.u32((uint32_t)cur_.lengthCode);
    w.boolean(cur_.deleted), w.boolean(cur_.doubleDensity);
    w.boolean(cur_.idCrcOk), w.boolean(cur_.dataCrcOk);
    w.boolean(scanEqual_), w.boolean(scanMet_);
    w.u8(dr_);
    w.raw(res_.data(), res_.size());
    w.u8((uint8_t)resLen_), w.u8((uint8_t)resIdx_);
    w.boolean(resultInt_);
    for (int u = 0; u < 4; ++u) {
        const Seek& s = seek_[(size_t)u];
        w.u32((uint32_t)pcn_[(size_t)u]);
        w.boolean(s.active), w.boolean(s.recal);
        w.u32((uint32_t)s.target), w.u8((uint8_t)s.hd), w.u32((uint32_t)s.pulses);
        w.u64(s.due);
        w.u32((uint32_t)pending_[(size_t)u]);
        w.boolean(seekEnd_[(size_t)u]), w.boolean(lastReady_[(size_t)u]);
        w.boolean(busy_[(size_t)u]);
    }
    w.u32((uint32_t)loadedUs_);
    w.u64(unloadAt_);
}

void Upd765::deserialize(StateReader& r) {
    srt_ = r.u8(), hut_ = r.u8(), hlt_ = r.u8();
    nd_ = r.boolean(), specified_ = r.boolean();
    phase_ = (Phase)r.u8();
    r.raw(cmd_.data(), cmd_.size());
    cmdLen_ = r.u8(), cmdGot_ = r.u8();
    us_ = r.u8(), hd_ = r.u8();
    c_ = r.u8(), h_ = r.u8(), r_ = r.u8(), n_ = r.u8(), eot_ = r.u8(), gpl_ = r.u8();
    dtl_ = r.u8();
    due_   = r.u64();
    xfer_  = (Xfer)r.u8();
    toMem_ = r.boolean();
    buf_   = r.blob();
    idx_ = r.u32(), want_ = r.u32();
    tc_ = r.boolean(), needNext_ = r.boolean(), cmStop_ = r.boolean(), crcStop_ = r.boolean();
    st1_ = r.u8(), st2_ = r.u8();
    tracked_ = (int)r.u32(), idCursor_ = (int)r.u32();
    cur_.track = (int)r.u32(), cur_.sector = (int)r.u32(), cur_.size = (int)r.u32();
    cur_.lengthCode = (int)r.u32();
    cur_.deleted = r.boolean(), cur_.doubleDensity = r.boolean();
    cur_.idCrcOk = r.boolean(), cur_.dataCrcOk = r.boolean();
    scanEqual_ = r.boolean(), scanMet_ = r.boolean();
    dr_ = r.u8();
    r.raw(res_.data(), res_.size());
    resLen_ = r.u8(), resIdx_ = r.u8();
    resultInt_ = r.boolean();
    for (int u = 0; u < 4; ++u) {
        Seek& s         = seek_[(size_t)u];
        pcn_[(size_t)u] = (int)r.u32();
        s.active = r.boolean(), s.recal = r.boolean();
        s.target = (int)r.u32(), s.hd = r.u8(), s.pulses = (int)r.u32();
        s.due    = r.u64();
        pending_[(size_t)u]   = (int)(int32_t)r.u32();
        seekEnd_[(size_t)u]   = r.boolean();
        lastReady_[(size_t)u] = r.boolean();
        busy_[(size_t)u]      = r.boolean();
    }
    loadedUs_ = (int)(int32_t)r.u32();
    unloadAt_ = r.u64();
    // A buffer that does not cover its indices is a corrupt file; idle the chip rather
    // than index past the end.
    if (idx_ > buf_.size() || want_ > buf_.size() || resIdx_ > 7 || resLen_ > 7 ||
        cmdGot_ > 9 || cmdLen_ > 9) {
        reset();
    }
}

} // namespace altair
