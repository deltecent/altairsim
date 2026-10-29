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
            // The IMAGE order, not the geometric one -- a property of the tool that WROTE
            // the file, which is why it is a parameter of init() and not a rule (slotIndex).
            size_t i = slotIndex(t, h);
            slots_[i].fmt   = TrackFormat{d, sectors, sectorSize, startSector};
            slots_[i].valid = sectors > 0 && sectorSize > 0;
        }
    }
    rebuild();
}

// One track's geometry, at runtime. The same slot-index arithmetic as initFormat, for a
// single (t,h): the WD177x Write Track command lands here once per track as a soft-sector
// FORMAT streams. rebuild() re-runs so the following tracks' offsets and geometryBytes_
// (the growth cap) follow. When the track changes size, the bytes after it move with it
// (moveTail), so every other track keeps its data whatever order the guest formats in.
void DiskImage::setTrackFormat(int t, int h, const TrackFormat& fmt) {
    if (t < 0 || t >= tracks_ || h < 0 || h >= heads_) return;
    size_t i = slotIndex(t, h);
    if (i >= slots_.size()) return;
    const uint64_t off    = slots_[i].offset;
    const uint64_t before = slotBytes(slots_[i]);
    slots_[i].fmt   = fmt;
    slots_[i].valid = fmt.sectors > 0 && fmt.sectorSize > 0;
    rebuild();
    const uint64_t after = slotBytes(slots_[i]);
    if (after != before) moveTail(off + before, off + after);
}

// THE TAIL MOVES WITH THE TRACK. A raw image is the tracks end to end, so a track that
// changes size moves the start of every track after it (rebuild). Their bytes must move
// the same distance, or a guest that reformats one track -- Jade's FORMAT `*` does tracks
// 0-1 only -- reads every later track at the wrong place, and a whole-disk reformat to a
// smaller layout leaves the file at the old size, which the next mount reads as the old
// layout. Only a medium that follows the format (extendsOnWrite) moves; one that cannot
// resize() keeps its bytes where they are.
void DiskImage::moveTail(uint64_t oldEnd, uint64_t newEnd) {
    if (!extendsOnWrite_ || media_->readOnly()) return;
    const uint64_t size = media_->size();
    if (size <= oldEnd) {
        // No tail: the file ends inside this track or before it. A smaller track only
        // cuts what is past its new end; a larger one grows as the guest writes it.
        if (size > newEnd) media_->resize(newEnd);
        return;
    }
    std::vector<uint8_t> tail(size_t(size - oldEnd));
    if (!media_->readAt(oldEnd, tail.data(), tail.size())) return;
    if (!media_->resize(newEnd + tail.size())) return;
    media_->writeAt(newEnd, tail.data(), tail.size());
}

// The offsets, recomputed from scratch after every initFormat. A track's offset is
// the sum of every track BEFORE it in image order -- and tracks are not all the same
// size, which is the whole reason the format is declared over ranges.
void DiskImage::rebuild() {
    uint64_t off = 0;
    for (auto& s : slots_) {
        s.offset = off;
        off += slotBytes(s);
    }
    geometryBytes_ = off;
}

bool DiskImage::trackFormat(int t, int h, TrackFormat& out) const {
    if (t < 0 || t >= tracks_ || h < 0 || h >= heads_) return false;
    size_t i = slotIndex(t, h);
    if (i >= slots_.size() || !slots_[i].valid) return false;
    out = slots_[i].fmt;
    return true;
}

bool DiskImage::locate(int t, int h, int s, uint64_t& off, size_t& len) const {
    if (t < 0 || t >= tracks_ || h < 0 || h >= heads_) return false;
    size_t i = slotIndex(t, h);
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

    // A SOFT-SECTOR DISK NEVER GROWS. writeAt() would happily extend the medium -- a
    // tape needs that -- but a floppy whose image is SHORT is a truncated image, and
    // appending to it would manufacture the missing tracks out of zeroes rather than
    // say so.
    //
    // A HARD-SECTOR disk is the exception (extendsOnWrite_): its image carries no
    // geometry, so a short image is UNFORMATTED, not truncated, and the guest's FORMAT
    // program grows it one slot at a time. The write still cannot escape the
    // controller's reach -- locate() already bounded `off` against geometryBytes_ (the
    // largest format the board declared), so the file can never exceed it.
    if (off + len > media_->size() && !extendsOnWrite_) return false;

    if (!media_->writeAt(off, buf, len)) return false;
    *n = len;
    return true;
}

} // namespace altair
