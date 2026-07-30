#pragma once
//
// DiskImageDrive -- a FloppyDrive (chips/wd17xx.h) backed by a DiskImage (host/disk.h).
//
// THE MISSING HALF OF THE WD-CHIP SEAM. The FD177x is a chip with pins -- STEP, TR00,
// READY, and a data path that moves whole sectors -- and `FloppyDrive` is the abstract
// drive on the far end of them. Wd17xx ships with the interface but no implementation,
// because the only card that ever wired the chip up (the Tarbell) was dropped before it
// landed. This is that implementation, and it is deliberately GENERIC: it knows the chip's
// pins and a DiskImage, and nothing about which card is holding either. The VersaFloppy is
// the first to use it; a Tarbell SD/DD card would use the very same adapter.
//
// WHERE THE CHIP/DRIVE SPLIT LANDS (see wd17xx.h at length):
//
//   - NO DRIVE SELECT. The chip talks to ONE drive; the CARD's select latch decides which,
//     by pointing the chip at one of these with Wd17xx::attach(). So there is one adapter
//     per physical drive, and the board attaches the selected one.
//   - THE HEAD POSITION LIVES HERE, not in the chip's Track Register. `head_` is where the
//     head physically is; the chip's track register is software's bookkeeping, and the two
//     are allowed to disagree (that is what a verify catches). step() moves the head; the
//     chip never writes it.
//   - SIDE IS THE CARD'S. The 179x has a side pin but the VersaFloppy drives it from its own
//     control latch, so the board sets side_ (it is the DiskImage head index too).
//
// A raw .DSK holds SECTOR PAYLOADS ONLY -- no gaps, no address marks, no CRCs (host/disk.h).
// So the ID fields are SYNTHESIZED from the mounted image's declared geometry (sectorIdAt),
// the CRCs always check (images do not carry the rot that makes a bad CRC), and the whole-
// track calls stay false -- a format has nowhere to put gaps that were never in the file,
// and the chip already says so out loud (WRITE FAULT) rather than inventing them.

#include "chips/wd17xx.h"

#include <cstddef>
#include <cstdint>

namespace altair {

class DiskImage;

class DiskImageDrive : public FloppyDrive {
public:
    // The board owns the DiskImage and points the drive at it on mount (nullptr = an empty
    // drive, which reads NOT READY -- exactly what a bare drive with no diskette does). It
    // stays valid until the board unmounts or replaces it.
    void mount(DiskImage* img, bool writeProtect) { img_ = img; wp_ = writeProtect; }
    void eject() { img_ = nullptr; wp_ = false; }
    bool loaded() const { return img_ != nullptr; }

    // The card's drive-select latch chose a side; the DiskImage head index follows it.
    void setSide(int s) { side_ = s; }
    int  side() const { return side_; }

    // FORMAT CAPABILITY -- a pure yes/no, NOT a byte budget or a density. FALSE (the default)
    // means this drive CANNOT be formatted: Write Track faults with WRITE FAULT, the wd17xx.h
    // base contract, which is what a raw .DSK on a card that does not implement formatting must
    // do (the VersaFloppy, today). A card that DOES format (either Tarbell generation) sets it
    // true on mount. Nothing about the RATE lives here: the revolution byte budget and the
    // recorded density are both derived per call from the data rate the chip hands in
    // (trackImageBytes/writeTrackImage), keeping the chip (Wd17xx::dataRateBits) the single
    // source of truth. See docs/devguide/soft-sector-floppy.md.
    void setFormatting(bool on) { canFormat_ = on; }

    // Head position -- runtime state the board serializes and restores (the disk itself is
    // host-backed and does not travel).
    int  headTrackRaw() const { return head_; }
    void setHeadTrack(int t) { head_ = t < 0 ? 0 : t; }

    // ---- the FloppyDrive pins and seams ----
    bool ready() const override;
    bool writeProtected() const override;
    bool trackZero() const override { return head_ == 0; }
    bool index() const override { return false; }  // no angular model; nothing here polls it
    int  headTrack() const override { return head_; }

    void step(bool inward) override;

    int  sectorCount() const override;
    bool sectorIdAt(int index, SectorId& out) const override;
    bool readData(const SectorId& id, uint8_t* buf, size_t* n) override;
    bool writeData(const SectorId& id, const uint8_t* buf, size_t n) override;

    // ---- FORMAT: the Write Track command lands here (see setFormatting) ----
    // trackImageBytes(rate) is 0 (WRITE FAULT) unless a disk is loaded AND the card enabled
    // formatting; otherwise it is one revolution at `rate`. writeTrackImage(in, rate) parses the
    // raw track the guest streamed and (re)establishes the addressed track's geometry as it fills
    // it, recording the density `rate` implies. floppy-drive.cpp.
    int  trackImageBytes(long long rate) const override;
    bool writeTrackImage(const std::vector<uint8_t>& in, long long rate) override;

private:
    // The chip searches ID fields by (track, sector). We hand it the current track's
    // geometry off the DiskImage; `trackFmt` fetches it, false if this drive has no such
    // track formatted (an empty drive, or a track range the image never declared).
    bool trackFmt(struct TrackFormat& out) const;

    DiskImage* img_  = nullptr;  // NON-OWNING: the board owns it
    int        head_ = 0;        // where the head physically is
    int        side_ = 0;        // the selected side / DiskImage head index
    bool       wp_   = false;    // the drive's write-protect notch (board-forced)

    // FORMAT strap (setFormatting): can this card format at all? false = cannot (WRITE FAULT).
    // Set by the CARD, not serialized (a strap, re-applied on mount). The byte budget and the
    // recorded density are NOT here -- they come from the chip's data rate, passed per call.
    bool canFormat_ = false;
};

} // namespace altair
