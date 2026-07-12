#include "host/disk.h"

namespace altair {

void DiskImage::init(int tracks, int heads, bool interleaved) {
    tracks_      = tracks > 0 ? tracks : 0;
    heads_       = heads > 0 ? heads : 0;
    interleaved_ = interleaved;
    slots_.assign((size_t)tracks_ * (size_t)heads_, Slot{});
    geometryBytes_ = 0;
}

void DiskImage::initFormat(int trackLo, int trackHi, int headLo, int headHi, Density d, int sectors,
                           int sectorSize, int startSector) {
    for (int t = trackLo; t <= trackHi; ++t) {
        if (t < 0 || t >= tracks_) continue;
        for (int h = headLo; h <= headHi; ++h) {
            if (h < 0 || h >= heads_) continue;
            // The IMAGE order, not the geometric one. Interleaved means the image
            // stores T0H0, T0H1, T1H0...; otherwise it is all of head 0 followed by
            // all of head 1. That is a property of the tool that WROTE the file, and
            // it varies -- which is why it is a parameter of init() and not a rule.
            size_t i = interleaved_ ? (size_t)t * (size_t)heads_ + (size_t)h
                                    : (size_t)h * (size_t)tracks_ + (size_t)t;
            slots_[i].fmt   = TrackFormat{d, sectors, sectorSize, startSector};
            slots_[i].valid = sectors > 0 && sectorSize > 0;
        }
    }
    rebuild();
}

// The offsets, recomputed from scratch after every initFormat. A track's offset is
// the sum of every track BEFORE it in image order -- and tracks are not all the same
// size, which is the whole reason the format is declared over ranges.
void DiskImage::rebuild() {
    uint64_t off = 0;
    for (auto& s : slots_) {
        s.offset = off;
        if (s.valid) off += (uint64_t)s.fmt.sectors * (uint64_t)s.fmt.sectorSize;
    }
    geometryBytes_ = off;
}

bool DiskImage::trackFormat(int t, int h, TrackFormat& out) const {
    if (t < 0 || t >= tracks_ || h < 0 || h >= heads_) return false;
    size_t i = interleaved_ ? (size_t)t * (size_t)heads_ + (size_t)h
                            : (size_t)h * (size_t)tracks_ + (size_t)t;
    if (i >= slots_.size() || !slots_[i].valid) return false;
    out = slots_[i].fmt;
    return true;
}

bool DiskImage::locate(int t, int h, int s, uint64_t& off, size_t& len) const {
    if (t < 0 || t >= tracks_ || h < 0 || h >= heads_) return false;
    size_t i = interleaved_ ? (size_t)t * (size_t)heads_ + (size_t)h
                            : (size_t)h * (size_t)tracks_ + (size_t)t;
    if (i >= slots_.size() || !slots_[i].valid) return false;

    const TrackFormat& f = slots_[i].fmt;

    // startSector, doing its one job. `s` is the number the CONTROLLER uses: 0..31
    // on a DCDD, 1..26 on a Tarbell. Off-by-one here does not fail -- it reads the
    // neighbouring sector, forever, and that is what silently corrupts a disk.
    int rel = s - f.startSector;
    if (rel < 0 || rel >= f.sectors) return false;

    off = slots_[i].offset + (uint64_t)rel * (uint64_t)f.sectorSize;
    len = (size_t)f.sectorSize;
    return true;
}

bool DiskImage::readSector(int t, int h, int s, uint8_t* buf, size_t* n) {
    uint64_t off = 0;
    size_t   len = 0;
    if (!locate(t, h, s, off, len)) return false;
    if (!n || *n < len) return false;  // the caller's buffer is too small: not a short read
    if (!media_->readAt(off, buf, len)) return false;
    *n = len;
    return true;
}

bool DiskImage::writeSector(int t, int h, int s, const uint8_t* buf, size_t* n) {
    if (media_->readOnly()) return false;
    uint64_t off = 0;
    size_t   len = 0;
    if (!locate(t, h, s, off, len)) return false;
    if (!n || *n < len) return false;  // a partial sector is a corrupt one

    // A DISK NEVER GROWS. writeAt() would happily extend the medium -- a tape needs
    // that -- but a floppy whose image is SHORT is a truncated image, and appending
    // to it would manufacture the missing tracks out of zeroes rather than say so.
    if (off + len > media_->size()) return false;

    if (!media_->writeAt(off, buf, len)) return false;
    *n = len;
    return true;
}

} // namespace altair
