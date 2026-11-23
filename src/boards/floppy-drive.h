#pragma once
//
// DiskImageDrive -- a FloppyDrive (chips/floppy.h) backed by a DiskImage (host/disk.h).
//
// THE MISSING HALF OF THE WD-CHIP SEAM. The FD177x is a chip with pins -- STEP, TR00,
// READY, and a data path that moves whole sectors -- and `FloppyDrive` is the abstract
// drive on the far end of them. Wd17xx ships with the interface but no implementation,
// because the only card that ever wired the chip up (the Tarbell) was dropped before it
// landed. This is that implementation, and it is deliberately GENERIC: it knows the chip's
// pins and a DiskImage, and nothing about which card is holding either. The VersaFloppy is
// the first to use it; a Tarbell SD/DD card would use the very same adapter.
//
// WHERE THE CHIP/DRIVE SPLIT LANDS (see chips/floppy.h and wd17xx.h at length):
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
// So the ID fields come from one of two places (sectorIdAt): the list a format wrote in this
// session (ids_ below), or, with no list, IDs made up from the mounted image's declared
// geometry. The CRCs always check (images do not carry the rot that makes a bad CRC). The
// whole-track calls work from the same parts: Write Track parses the guest's stream into a
// geometry, the ID list and the payloads, and Read Track builds a revolution back from the ID
// list, the payloads and the IBM standard gaps. Only the gap lengths are made up.

#include "chips/floppy.h"
#include "host/diskfaults.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace altair {

class DiskImage;
class Clock;
namespace dbg { class Channel; }

class DiskImageDrive : public FloppyDrive {
public:
    // The board owns the DiskImage and points the drive at it on mount (nullptr = an empty
    // drive, which reads NOT READY -- exactly what a bare drive with no diskette does). It
    // stays valid until the board unmounts or replaces it.
    // A new diskette brings no bad sectors with it: mount and eject clear the fault table.
    void mount(DiskImage* img, bool writeProtect) {
        img_ = img;
        wp_  = writeProtect;
        ids_.clear();
        faults_.clearAll();
    }
    void eject() {
        img_ = nullptr;
        wp_  = false;
        ids_.clear();
        faults_.clearAll();
    }
    bool loaded() const { return img_ != nullptr; }

    // The card's drive-select latch chose a side; the DiskImage head index follows it.
    void setSide(int s) { side_ = s; }
    int  side() const { return side_; }

    // FORMAT CAPABILITY -- a pure yes/no, NOT a byte budget or a density. FALSE (the default)
    // means this drive CANNOT be formatted: Write Track faults with WRITE FAULT, the wd17xx.h
    // base contract, which is what a raw .DSK on a card that does not implement formatting must
    // do (the VersaFloppy, today). A card that DOES format (either Tarbell generation) sets it
    // true on mount. Nothing about the RATE or the DENSITY lives here: the chip hands in its
    // data rate for the revolution byte budget (trackImageBytes) and its density for the
    // recorded encoding (writeTrackImage), keeping the chip (Wd17xx::dataRateBits,
    // Wd17xx::doubleDensity) the single source of truth. See docs/devguide/soft-sector-floppy.md.
    void setFormatting(bool on) { canFormat_ = on; }

    // DRIVE ROTATION SPEED, in revolutions per second -- the DENOMINATOR of the Write Track byte
    // budget (trackImageBytes). An 8" drive spins at 360 RPM = 6 rev/s (the default, so a card
    // that never sets it -- the Tarbell -- is byte-for-byte unchanged); a 5.25" mini spins at
    // 300 RPM = 5 rev/s, a longer revolution that holds more bytes, so its last sectors are not
    // truncated. The card sets it per mounted drive from the diskette's physical size (a drive's
    // RPM is fixed by its size, not by a control bit). See revolutionBytes (floppy-drive.cpp).
    void setRevsPerSecond(int rps) { revsPerSec_ = rps > 0 ? rps : 6; }

    // ANGULAR MODEL, OPT-IN. index() below needs a time source to know where the index hole
    // is right now; a card that wants a live IP pin (the Cromemco RDOS boot polls Force-
    // Interrupt-on-index) hands the drive the machine Clock. A card that never sets it -- the
    // VersaFloppy, the Tarbell -- keeps index() == false, byte-for-byte the prior behavior,
    // because a drive with no clock genuinely cannot know how far it has turned. Non-owning.
    void setClock(const Clock* c) { clk_ = c; }

    // Head position -- runtime state the board serializes and restores (the disk itself is
    // host-backed and does not travel).
    int  headTrackRaw() const { return head_; }
    void setHeadTrack(int t) { head_ = t < 0 ? 0 : t; }

    // THE BAD SECTORS OF THE DISKETTE IN THIS DRIVE (host/diskfaults.h, #966). An overlay: the
    // image bytes never change. sectorIdAt reports a bad ID or data CRC, or leaves a missing ID
    // field out; writeData refuses a bad write. One fault is used up (and counted) when the
    // chip reads the sector's data, writes it, or gives up looking for it (sectorNotFound).
    // The address is the physical track under the head, the selected side and the sector ID.
    DiskFaults&       faults() { return faults_; }
    const DiskFaults& faults() const { return faults_; }

    // The card's debug channel, its `fault` flag and this drive's unit name. Each use of a
    // fault writes one line -- ONLY while the operator has the flag on (SET <id> DEBUG=fault).
    // The fault works the same with the flag off; the flag decides the line and nothing else.
    // `ch` is asked at each use, because a card gets its channel when it enters a backplane,
    // after its drives are built (Board::ensureDebugChannel).
    void setDebug(std::function<const dbg::Channel*()> ch, unsigned bit, std::string unit) {
        dbg_    = std::move(ch);
        dbgBit_ = bit;
        unit_   = std::move(unit);
    }

    // ---- the FloppyDrive pins and seams ----
    bool ready() const override;
    bool writeProtected() const override;
    bool trackZero() const override { return head_ == 0; }
    bool index() const override;  // the index hole under the sensor now (angular, needs a clock)
    int  headTrack() const override { return head_; }

    void step(bool inward) override;

    int  sectorCount() const override;
    bool sectorIdAt(int index, SectorId& out) const override;
    bool readData(const SectorId& id, uint8_t* buf, size_t* n) override;
    bool writeData(const SectorId& id, const uint8_t* buf, size_t n) override;
    void sectorNotFound(int sector) override;

    // ---- FORMAT: the Write Track command lands here (see setFormatting) ----
    // trackImageBytes(rate) is 0 (WRITE FAULT) unless a disk is loaded AND the card enabled
    // formatting; otherwise it is one revolution at `rate`. writeTrackImage(in, doubleDensity)
    // parses the raw track the guest streamed and (re)establishes the addressed track's geometry
    // as it fills it, recording the density the chip was set to. floppy-drive.cpp.
    int  trackImageBytes(long long rate) const override;
    bool writeTrackImage(const std::vector<uint8_t>& in, bool doubleDensity) override;

    // ---- READ TRACK: one revolution, built from the ID list, the data and IBM gaps ----
    // False when there is no disk or the track has no format. Unlike Write Track it does
    // not need setFormatting: reading a track is not formatting one. floppy-drive.cpp.
    bool readTrackImage(std::vector<uint8_t>& out, long long rate) override;

    // ---- the µPD765A's two: the Two Side line, and Format a Track from an ID list ----
    // twoSided() is the image's head count. formatTrack() needs setFormatting(true), as
    // Write Track does; floppy-drive.cpp says what it can and cannot record.
    bool twoSided() const override;
    bool formatTrack(const std::vector<SectorId>& ids, uint8_t fill) override;

private:
    // The chip searches ID fields by (track, sector). We hand it the current track's
    // geometry off the DiskImage; `trackFmt` fetches it, false if this drive has no such
    // track formatted (an empty drive, or a track range the image never declared).
    bool trackFmt(struct TrackFormat& out) const;

    // The ID field at `index` from the image and the recorded list, with no faults on it.
    // sectorIdAt() is this plus the faults; the drive's own track work uses this.
    bool idAt(int index, SectorId& out) const;

    // THE ID FIELDS A FORMAT WROTE, kept for the session. A raw image holds payloads only, so
    // the ID fields a Write Track (or the 765's Format a Track) laid down -- their order round
    // the track, the C, H, R and N bytes as written, and which data mark each sector has --
    // have no place in the file. The drive keeps them here, per (track, side), so that the
    // chip reads back what the guest formatted: an interleave, an ID whose C is not the
    // physical track, a deleted sector. They are lost on unmount and are not saved with the
    // machine state; a track with no list gets IDs made up from its geometry (sectorIdAt).
    struct RecordedId {
        int  c = 0, h = -1, r = 0, n = 0;  // the ID bytes as written (n raw; h -1 = not known)
        bool deleted = false;             // the data field carries F8, not FB
    };
    // The list for the track under the head, or nullptr. Only a list whose length matches the
    // track's TrackFormat counts, so a list can never disagree with the image's geometry.
    const std::vector<RecordedId>* recordedIds() const;
    std::map<std::pair<int, int>, std::vector<RecordedId>> ids_;

    // Use a fault: count it, wear it down, and write the debug line if the flag is on.
    void useFault(DiskFault& f, int sector);

    DiskFaults         faults_;
    std::function<const dbg::Channel*()> dbg_;  // the card's channel, or null
    unsigned            dbgBit_ = 0;
    std::string         unit_;

    DiskImage* img_  = nullptr;  // NON-OWNING: the board owns it
    int        head_ = 0;        // where the head physically is
    int        side_ = 0;        // the selected side / DiskImage head index
    bool       wp_   = false;    // the drive's write-protect notch (board-forced)

    // FORMAT strap (setFormatting): can this card format at all? false = cannot (WRITE FAULT).
    // Set by the CARD, not serialized (a strap, re-applied on mount). The byte budget and the
    // recorded density are NOT here -- they come from the chip's data rate, passed per call.
    bool canFormat_ = false;

    // ROTATION SPEED (setRevsPerSecond): the Write Track budget's denominator. 6 = 360 RPM (8"),
    // 5 = 300 RPM (5.25" mini). A strap set by the CARD per mounted drive, not serialized.
    int revsPerSec_ = 6;

    // ANGULAR MODEL time source (setClock), non-owning. nullptr = no rotation modeled, so
    // index() reads false -- the default for every card that does not opt in.
    const Clock* clk_ = nullptr;
};

// ---- THE CARD SIDE OF THE FAULTS (#966), shared by every card that holds these drives ----

// The `faults` key of a [[board.drive]] table: the drive's fault list, entries split by ";".
struct Property;
Property driveFaultsKey();

// Apply a `faults` key to a drive the table just mounted. An empty text is no faults. A fault
// list on a drive with no disk is refused: the faults belong to a diskette.
bool applyDriveFaults(DiskImageDrive& d, const std::string& text, std::string& err);

// The debug flag every such card offers for its drives' faults: `fault`, bit 0 of the channel.
constexpr unsigned kFaultDebugBit = 0;

} // namespace altair
