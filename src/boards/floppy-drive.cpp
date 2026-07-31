#include "boards/floppy-drive.h"

#include "core/clock.h"
#include "host/disk.h"

namespace altair {

// IP (pin 35) -- the index hole under the sensor RIGHT NOW. A pulse, not a level: true for a
// sliver of each revolution and false the rest. It is a question about ROTATION, so it needs
// a clock and a spin rate; a drive handed neither (the default) cannot answer and reads false,
// exactly as before this model existed. With a disk loaded and a clock injected, the disk is
// turning at revsPerSec_ -- period = hz / revsPerSec_ T-states -- and the hole passes the
// sensor once per turn. The pulse is a small fraction of the revolution (a real 8"/5.25" IP is
// ~1-4 ms of a ~166/200 ms turn); ~1/32 of a revolution gives one clean rising edge per turn,
// which is all the two consumers -- Type I status bit S1 and Force-Interrupt-on-index -- need.
bool DiskImageDrive::index() const {
    if (!clk_ || !img_) return false;             // no clock or no disk -> not spinning
    const long long rps    = revsPerSec_ > 0 ? revsPerSec_ : 6;
    const uint64_t  period = (uint64_t)(clk_->hz() / rps);  // T-states per revolution
    if (period == 0) return false;
    return (clk_->now() % period) < (period / 32);  // the hole is under the sensor
}

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

// ---------------------------------------------------------------------------
// FORMAT -- the Write Track command (host/disk.h "geometry in real time").
// ---------------------------------------------------------------------------

// The raw bytes one revolution holds at data rate `rate`: bytes/s = rate/8, and the drive turns
// at `rps` rev/s, so one revolution is rate/8/rps bytes. An 8" drive spins at 360 RPM = 6 rev/s:
// 250,000 -> 5208 (8" SD), 500,000 -> 10416 (8" DD). A 5.25" mini spins at 300 RPM = 5 rev/s, a
// longer revolution: 250,000 -> 6250 (5" SD), 500,000 -> 12500 (5" DD). Derived, never a magic
// constant: the chip hands us the rate it is configured at, and the card hands us the drive's RPM
// (setRevsPerSecond), and the geometry follows. The default rps 6 keeps the Tarbell, which never
// sets it, byte-for-byte identical.
static int revolutionBytes(long long rate, int rps) { return (int)(rate / (8 * rps)); }

// The raw one-revolution byte budget, or 0 (WRITE FAULT) if this drive is empty or the card
// does not implement formatting. The chip stalls the wait-synced Write Track until exactly this
// many bytes have arrived, so a nonzero value is BOTH "yes, you may format" and "here is the
// length" -- FORMAT.COM pads its structured track out to it with gap bytes (see the ENDTRK
// loop in pd2/FORMAT.ASM). The budget is the CHIP's data rate expressed as a revolution length,
// so an SD track (250k) and a DD track (500k) get the right budget from the same card. See
// setFormatting (floppy-drive.h) and Risk 1 in the plan: too large hangs the command, too small
// truncates the last sectors.
int DiskImageDrive::trackImageBytes(long long rate) const {
    return (img_ && canFormat_) ? revolutionBytes(rate, revsPerSec_) : 0;
}

// Parse the raw track the guest streamed into `in` and (re)establish the addressed track's
// geometry as we fill it. The stream is IBM 3740: gap bytes, an index mark (0xFC), then per
// sector an ID field (0xFE track side sector N, then 0xF7) and a data field (0xFB, `sectorSize`
// data bytes, then 0xF7). 0xF7 is the CRC-generate byte and can never appear as literal track
// data, so accumulate-until-0xF7 is unambiguous; 0xE5 fill is ordinary data. See the format FSM
// in docs/devguide/soft-sector-floppy.md, modelled on simh.mdsk/.../wd_17xx.c.
bool DiskImageDrive::writeTrackImage(const std::vector<uint8_t>& in, long long rate) {
    if (!img_) return false;

    // Each parsed data field, as an (offset, length) window into `in` -- we copy nothing, and
    // write straight from the buffer the chip already collected. The per-sector number and size
    // are only meaningful for the FIRST sector (a track is uniform), so they are captured once
    // as locals rather than stored per sector.
    struct Field { size_t off, len; };
    std::vector<Field> fields;
    int startSector = 0;
    int sectorSize  = 0;

    const size_t n = in.size();
    size_t       i = 0;
    while (i < n) {
        if (in[i] != 0xFE) { ++i; continue; }  // gap / index mark -- scan to the next ID field
        ++i;                                    // consume the 0xFE ID address mark
        if (i + 4 > n) break;                   // a truncated header ends the track

        // ID field: track, side, sector, length code N. We TRUST THE HEAD POSITION for the
        // physical track (sectorIdAt synthesizes track = head_), so the recorded track/side
        // are not used; the sector number and N are. sectorSize = 128 << N (IBM 3740).
        const int sec  = in[i + 2];
        const int lenN = in[i + 3] & 0x03;
        i += 4;

        // Skip the ID CRC (0xF7) and the gap up to the data address mark. Bail to the outer
        // scan if the next ID field arrives first -- an ID with no data field is not a sector.
        while (i < n && in[i] != 0xFB && in[i] != 0xFE) ++i;
        if (i >= n || in[i] != 0xFB) continue;
        ++i;  // consume the 0xFB data address mark

        // The data field is EXACTLY the bytes the software streamed, up to the 0xF7 CRC-generate
        // terminator (which can never be literal data). We reference it in place -- no fabricated
        // or padded fill: the sector gets what the formatter wrote (0xE5 for CP/M, but that is
        // the guest's choice). A data field shorter than the recorded sectorSize is malformed and
        // writeSector rejects it below -> WRITE FAULT, which is honest.
        const size_t off = i;
        while (i < n && in[i] != 0xF7) ++i;
        if (fields.empty()) { startSector = sec; sectorSize = 128 << lenN; }
        fields.push_back({off, i - off});
        if (i < n) ++i;  // consume the 0xF7
    }

    if (fields.empty()) return false;  // nothing legible -> WRITE FAULT (the chip sets S5)

    // The track's geometry, derived from the stream: sector count from what we found, sector
    // size and startSector from the first sector. Density comes from the CHIP's data rate, the
    // single source of truth (Wd17xx::dataRateBits, handed in as `rate`): an 8" DD track streams
    // at 500 kbit/s, everything slower is single density. This is what lets one DD card format a
    // mixed disk -- SD track 0, DD tracks 1-76 -- from the guest's per-track OUT-FC density bit.
    // setTrackFormat re-runs rebuild(), so the following tracks' offsets and the growth cap follow
    // (ascending-order invariant, disk.h).
    TrackFormat tf;
    tf.density     = rate >= 500000 ? Density::DD : Density::SD;
    tf.sectors     = (int)fields.size();
    tf.sectorSize  = sectorSize;
    tf.startSector = startSector;
    img_->setTrackFormat(head_, side_, tf);

    // Write each sector's fill by a SEQUENTIAL counter from startSector, ignoring the header's
    // possibly-skewed sector number, so the fill stays contiguous and the file grows in order.
    for (size_t s = 0; s < fields.size(); ++s) {
        size_t len = fields[s].len;
        if (!img_->writeSector(head_, side_, startSector + (int)s, in.data() + fields[s].off, &len))
            return false;  // a write that will not land -> WRITE FAULT
    }
    return true;
}

} // namespace altair
