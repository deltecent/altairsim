#include "boards/floppy-drive.h"

#include "host/disk.h"

namespace altair {

// THE HEAD IS THE ONLY THING THAT MOVES, and a step outward at track 0 goes nowhere -- the
// head is against its stop and the real drive just buzzes. The upper bound is the image's
// track count: a step past the last track also goes nowhere (there is no track there).
void DiskImageDrive::step(bool inward) {
    if (!img_) { if (inward) ++head_; else if (head_ > 0) --head_; return; }
    if (inward) {
        if (head_ < img_->tracks() - 1) ++head_;
    } else if (head_ > 0) {
        --head_;
    }
}

bool DiskImageDrive::ready() const {
    // A disk is in the drive and its geometry is known. NOT READY otherwise -- which is what
    // a Type II command samples and refuses on, and what S7 reports.
    return img_ != nullptr && img_->tracks() > 0;
}

bool DiskImageDrive::writeProtected() const {
    // Either the notch (the board forced it, or the machine file said `readonly`) or the host
    // refusing the file. Both look the same to the drive: the guest writes, nothing lands.
    return wp_ || (img_ && img_->readOnly());
}

bool DiskImageDrive::trackFmt(TrackFormat& out) const {
    if (!img_) return false;
    return img_->trackFormat(head_, side_, out);
}

int DiskImageDrive::sectorCount() const {
    TrackFormat tf;
    return trackFmt(tf) ? tf.sectors : 0;
}

// The length CODE as the chip records it (IBM 3740: 128 << n). Read Address hands this raw
// byte to the guest, so it has to be right: 128->0, 256->1, 512->2, 1024->3.
static int lengthCodeFor(int sectorSize) {
    switch (sectorSize) {
        case 128:  return 0;
        case 256:  return 1;
        case 512:  return 2;
        case 1024: return 3;
        default:   return 0;
    }
}

bool DiskImageDrive::sectorIdAt(int index, SectorId& out) const {
    TrackFormat tf;
    if (!trackFmt(tf)) return false;
    if (index < 0 || index >= tf.sectors) return false;

    // The ID field, SYNTHESIZED from the declared geometry -- a raw image carries none.
    // The recorded track is the physical track (we do not model a mislabeled disk); the
    // sector number counts from the format's startSector (1 on a soft-sector card).
    out            = {};
    out.track      = head_;
    out.sector     = tf.startSector + index;
    out.size       = tf.sectorSize;
    out.lengthCode = lengthCodeFor(tf.sectorSize);
    out.deleted    = false;   // a raw image has no data address marks to be deleted
    out.idCrcOk    = true;    // ...and no rot: images do not carry bad CRCs
    out.dataCrcOk  = true;
    return true;
}

bool DiskImageDrive::readData(const SectorId& id, uint8_t* buf, size_t* n) {
    if (!img_) return false;
    return img_->readSector(head_, side_, id.sector, buf, n);
}

bool DiskImageDrive::writeData(const SectorId& id, const uint8_t* buf, size_t n) {
    if (!img_) return false;
    // writeSector wants a byte count it can bound against the geometry; a short write is a
    // failure, never a truncation (host/disk.h).
    size_t len = n;
    return img_->writeSector(head_, side_, id.sector, buf, &len);
}

} // namespace altair
