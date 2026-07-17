#pragma once
//
// DiskImage -- the generic mountable disk (DESIGN.md 7.3).
//
// Every disk board (88-DCDD, Tarbell, 88-HDSK, Disk 1A, North Star, whatever you
// build next) sees only this. It does offsets and I/O, and NOTHING ELSE: what is
// inside a sector -- the sync byte, the header, where the checksum sits -- is the
// controller's business and never this file's.
//
// THE INTERFACE IS CHS, NOT LBA, AND THE FORMAT IS PER-TRACK-RANGE. Both are
// forced by disks that exist, and the reasoning is in DESIGN.md 7.3 -- it is not
// re-litigated here. In one line each:
//
//   - CHS, because every controller in the catalog addresses track/head/sector,
//     and an LBA interface would force each board to invent a flattening the
//     hardware never had and then invert it.
//   - Per track RANGE, because sector size and density vary WITHIN one image: a
//     double-density controller keeps track 0 single-density so the boot PROM can
//     read it. One geometry for the whole disk cannot say that; two initFormat()
//     calls can.
//   - startSector, because the 88-DCDD numbers sectors from 0 and most soft-sector
//     controllers number from 1. That is the off-by-one that silently corrupts a
//     disk, so it is a parameter, not a convention.
//
// Hard-sector vs soft-sector needs no flag: it falls out of sectorSize. A DCDD
// sector is 137 bytes because the image holds the WHOLE SLOT -- sync, header,
// payload, checksum, trailer. A Tarbell sector is 128 bytes because the header sat
// in the inter-sector gap on real media and never made it into the image.
//
// GEOMETRY PROBING IS THE BOARD'S JOB, NOT THIS FILE'S (DESIGN.md 7.3). 337,568
// bytes means a 77-track 8" floppy only because it is a DCDD; the same count on
// another controller means something else. The board probes, picks among the
// formats IT knows, and calls init()/initFormat(). See sizeMatches(), below --
// that much of the probe IS shared, because the trap is in the file format and not
// in the controller.

#include "host/media.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

enum class Density { SD, DD };

struct TrackFormat {
    Density density    = Density::SD;
    int     sectors    = 0;
    int     sectorSize = 0;
    int     startSector = 0;  // the number of the FIRST sector: 0 on a DCDD, 1 on a Tarbell
};

// ---------------------------------------------------------------------------
// THE 337,664-BYTE TRAP.
//
// Both 8" DCDD images in the tree are 337,664 bytes, not the 337,568 that
// 77 x 32 x 137 predicts. The slop is XMODEM: it pads to 128-byte blocks, and
// 337,568 rounded up to the next block is exactly 337,664.
//
// So A STRICT `size == exact` PROBE REJECTS BOTH OF THE ONLY 8" DISKS WE HAVE.
// Every format match is `exact <= size < exact + 128`. The pad is never data, and
// a write never reaches it -- DiskImage bounds every access against the declared
// geometry, which stops at `exact`.
// ---------------------------------------------------------------------------
inline bool sizeMatches(uint64_t got, uint64_t exact) {
    return got >= exact && got < exact + 128;
}

// ---------------------------------------------------------------------------
// ONE CLASS. NOT A BASE CLASS.
//
// DESIGN.md 7.3 used to name four implementations -- RawImageFile, ReadOnlyImage,
// MemoryDisk, and later ImdImage/Td0Image -- and every one of them has now
// dissolved:
//
//   - read-only was never a different IMAGE. It is a medium that says no.
//   - an in-memory disk was never a different image either. It is a MemoryMedia.
//   - and IMD/TD0 ARE NEVER COMING (Patrick, 2026-07-12): raw disk images are the only
//     kind this program will ever read, and an IMD file that has to be used here is one
//     that gets converted to raw BEFOREHAND, outside it.
//
// That last one is what removes the last reason for readSector()/writeSector() to be
// virtual -- they were virtual FOR a container format that carries its own per-track
// sector map, and there is not going to be one. So the image is sector-linear,
// always, and this class is the whole of it. A virtual left in place for a
// possibility the owner has ruled out is not extensibility; it is a hook that will
// never be pulled, and the next reader has to work out why it is there.
// ---------------------------------------------------------------------------
class DiskImage {
public:
    explicit DiskImage(std::unique_ptr<MediaFile> m) : media_(std::move(m)) {}

    // The BOARD describes the medium: overall shape, then one or more TRACK RANGES.
    // init() clears any previous format -- re-probing an image is not a merge.
    void init(int tracks, int heads, bool interleaved);
    void initFormat(int trackLo, int trackHi, int headLo, int headHi, Density d, int sectors,
                    int sectorSize, int startSector);

    int  tracks() const { return tracks_; }
    int  heads() const { return heads_; }
    bool interleaved() const { return interleaved_; }

    // What is on this track? False if the board never formatted it -- which is an
    // error, not an empty track: the controller asked for a track the medium it
    // itself described does not have.
    bool trackFormat(int t, int h, TrackFormat& out) const;

    // The bytes the declared geometry accounts for. The medium may be LONGER (the
    // XMODEM pad); it may never be shorter.
    uint64_t geometryBytes() const { return geometryBytes_; }

    // *n is IN/OUT: on entry the capacity of buf, on exit the bytes moved. A sector
    // bigger than the buffer is a failure, never a truncation -- a short sector is
    // not a smaller sector, it is a corrupt one.
    bool readSector(int t, int h, int s, uint8_t* buf, size_t* n);
    bool writeSector(int t, int h, int s, const uint8_t* buf, size_t* n);

    bool     readOnly() const { return media_->readOnly(); }
    bool     readOnlyForced() const { return media_->readOnlyForced(); }
    void     sync() { media_->sync(); }
    uint64_t size() const { return media_->size(); }
    const std::string& describe() const { return media_->describe(); }

private:
    // Where the sector IS, and how big. The whole of the CHS arithmetic, in one
    // place, so no board ever does it again.
    bool locate(int t, int h, int s, uint64_t& off, size_t& len) const;
    void rebuild();

    struct Slot {
        TrackFormat fmt;
        uint64_t    offset = 0;
        bool        valid  = false;
    };
    std::unique_ptr<MediaFile> media_;
    int               tracks_        = 0;
    int               heads_         = 0;
    bool              interleaved_   = false;
    uint64_t          geometryBytes_ = 0;
    std::vector<Slot> slots_;  // one per (track, head), in IMAGE order
};

} // namespace altair
