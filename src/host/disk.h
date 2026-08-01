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
//   - and IMD/TD0 ARE STILL NEVER READ HERE: a raw sector-linear image is the only kind
//     THIS CLASS reads. An IMD that has to be used is converted to a raw sibling `.DSK`
//     FIRST -- at the MOUNT layer (src/cli/monitor.cpp + src/host/imd.cpp), not inside
//     the image -- and only the `.DSK` is ever wrapped here. (The convert used to happen
//     entirely outside the program; Patrick, 2026-08-01, moved it in-line at MOUNT, which
//     is the same bargain the WAV codec strikes: decode above MediaFile, so what DiskImage
//     sees is unchanged. TD0 is still nowhere.)
//
// So the image is sector-linear, ALWAYS, and this class is the whole of it -- which is
// what keeps readSector()/writeSector() non-virtual. They were virtual FOR a container
// format that carries its own per-track sector map, and DiskImage never gets one: the
// container is unpacked before it, so a virtual here would be a hook that is never pulled,
// and the next reader would have to work out why it is there.
// ---------------------------------------------------------------------------
class DiskImage {
public:
    explicit DiskImage(std::unique_ptr<MediaFile> m) : media_(std::move(m)) {}

    // The BOARD describes the medium: overall shape, then one or more TRACK RANGES.
    // init() clears any previous format -- re-probing an image is not a merge.
    void init(int tracks, int heads, bool interleaved);
    void initFormat(int trackLo, int trackHi, int headLo, int headHi, Density d, int sectors,
                    int sectorSize, int startSector);

    // GEOMETRY IN REAL TIME -- one track's format, established or changed at RUNTIME.
    //
    // A soft-sector controller's FORMAT (the WD177x Write Track command) is the ONLY thing
    // that establishes a track's geometry, and it does so one track at a time as the guest
    // streams it -- density from the controller, sector size and count and startSector from
    // the stream (boards/floppy-drive.cpp). So this is initFormat for exactly one (t,h): set
    // the slot's format, mark it valid (or not -- sectors==0 clears it), and re-run rebuild()
    // so the following tracks' offsets and the growth cap follow. It is safe to call after
    // init()/initFormat and after mount.
    //
    // CORRECTNESS RESTS ON THE ASCENDING-SLOT-ORDER INVARIANT. FORMAT writes slots 0->N in IMAGE
    // order (slotIndex), so when a track is (re)formatted to a LARGER geometry the slots after it
    // have either not been written yet or are about to be overwritten -- rebuild() moving their
    // offsets clobbers nothing valid. This is why the format program's write order must agree with
    // the image layout: a head-major image wants all of side 0 then side 1, an interleaved
    // (cylinder-major) image wants both sides of a cylinder before the next -- and a real
    // controller's FORMAT does whichever its dumped images use (the VersaFloppy, cylinder-major).
    // An out-of-order or cross-density reformat of an already populated disk would need the tail
    // shifted first; that is a deferred, separate feature.
    void setTrackFormat(int t, int h, const TrackFormat& fmt);

    int  tracks() const { return tracks_; }
    int  heads() const { return heads_; }
    bool interleaved() const { return interleaved_; }

    // EXTEND-ON-WRITE -- an UNFORMATTED-MEDIA concession, off by default (DESIGN.md 7.3).
    //
    // A blank or short image is not a truncated disk, it is an UNFORMATTED one -- the
    // guest's own FORMAT program fills it in, and until it does a read of an absent slot
    // fails and the controller's sync/checksum (or, on a soft-sector card, an unformatted
    // track) rejects it, which is correct.
    //
    // With this on, writeSector() lets the backing file GROW as sectors are written, still
    // capped at geometryBytes_ (the controller's whole reach). Two cards turn it on for two
    // shapes of the same idea:
    //
    //   - HARD-SECTOR (88-DCDD/88-MDS): the image carries no geometry at all -- fixed
    //     137-byte slots addressed linearly -- and geometryBytes_ is the board's largest
    //     format, FIXED at mount. FORMAT grows the file one slot at a time up to it.
    //
    //   - SOFT-SECTOR (Tarbell): the geometry arrives one track at a time from the Write
    //     Track command (setTrackFormat), so geometryBytes_ is DYNAMIC -- it grows as each
    //     track is formatted, and the cap rises with it under the ascending-order invariant.
    //     A recognized full disk never grows (its writes stay in bounds); a blank one grows
    //     as it formats.
    //
    // A card that neither pre-declares its whole geometry nor formats leaves this OFF, and a
    // short image there really is a truncated one.
    void setExtendsOnWrite(bool g) { extendsOnWrite_ = g; }
    bool extendsOnWrite() const { return extendsOnWrite_; }

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
    // The slot's index in IMAGE order -- the one place the interleave mapping lives, so
    // initFormat/setTrackFormat/trackFormat/locate all agree. Interleaved stores T0H0, T0H1,
    // T1H0...; otherwise all of head 0 then all of head 1. No bounds check: every caller does
    // its own (initFormat continues, the others return false).
    size_t slotIndex(int t, int h) const {
        return interleaved_ ? (size_t)t * (size_t)heads_ + (size_t)h
                            : (size_t)h * (size_t)tracks_ + (size_t)t;
    }

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
    bool              interleaved_    = false;
    bool              extendsOnWrite_ = false;  // hard-sector: grow the file as it formats
    uint64_t          geometryBytes_  = 0;
    std::vector<Slot> slots_;  // one per (track, head), in IMAGE order
};

} // namespace altair
