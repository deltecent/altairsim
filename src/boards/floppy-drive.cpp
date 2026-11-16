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

const std::vector<DiskImageDrive::RecordedId>* DiskImageDrive::recordedIds() const {
    TrackFormat tf;
    if (!trackFmt(tf)) return nullptr;
    const auto it = ids_.find({head_, side_});
    if (it == ids_.end() || (int)it->second.size() != tf.sectors) return nullptr;
    return &it->second;
}

bool DiskImageDrive::sectorIdAt(int index, SectorId& out) const {
    TrackFormat tf;
    if (!trackFmt(tf)) return false;
    if (index < 0 || index >= tf.sectors) return false;

    out               = {};
    out.size          = tf.sectorSize;
    out.doubleDensity = tf.density == Density::DD;  // FM or MFM, as the track was recorded
    out.idCrcOk       = true;    // ...and no rot: images do not carry bad CRCs
    out.dataCrcOk     = true;

    // The ID field a format wrote this session, in the order it goes round the track.
    if (const auto* ids = recordedIds()) {
        const RecordedId& r = (*ids)[(size_t)index];
        out.track      = r.c;
        out.head       = r.h;
        out.sector     = r.r;
        out.lengthCode = r.n;
        out.deleted    = r.deleted;
        return true;
    }

    // Otherwise the ID field is SYNTHESIZED from the declared geometry -- a raw image carries
    // none. The recorded track is the physical track; the side byte is not known (head -1);
    // the sector number counts from the format's startSector (1 on a soft-sector card).
    out.track      = head_;
    out.sector     = tf.startSector + index;
    out.lengthCode = lengthCodeFor(tf.sectorSize);
    out.deleted    = false;   // a raw image has no data address marks to be deleted
    return true;
}

bool DiskImageDrive::readData(const SectorId& id, uint8_t* buf, size_t* n) {
    if (!img_) return false;
    return img_->readSector(head_, side_, id.sector, buf, n);
}

// EVERY WRITE GOES TO THE HOST FILE NOW. The file is the diskette: a write the guest was told
// succeeded must survive a killed simulator, so it is synced before the call returns -- as every
// other disk board does. The cards that hold this drive sync only on unmount, which a SIGTERM or
// a SIGKILL never reaches (#900). A failed write changed nothing, so it has nothing to sync.
bool DiskImageDrive::writeData(const SectorId& id, const uint8_t* buf, size_t n) {
    if (!img_) return false;
    // writeSector wants a byte count it can bound against the geometry; a short write is a
    // failure, never a truncation (host/disk.h).
    size_t len = n;
    if (!img_->writeSector(head_, side_, id.sector, buf, &len)) return false;
    img_->sync();

    // A WRITE ALSO WRITES THE DATA MARK: the chip says which (the 179x's a0 bit, the 765's
    // Write Deleted Data), and the next read of the sector reports it. The file has no place
    // for it, so it goes in the ID list. A deleted mark on a track with no list makes the
    // list from the IDs sectorIdAt makes up, so the other sectors read as they did.
    const auto* ids = recordedIds();
    if (!ids && id.deleted) {
        std::vector<RecordedId> made;
        SectorId s;
        for (int i = 0; sectorIdAt(i, s); ++i)
            made.push_back({s.track, s.head, s.sector, s.lengthCode, s.deleted});
        ids_[{head_, side_}] = std::move(made);
        ids = recordedIds();
    }
    if (ids) {
        for (RecordedId& r : ids_[{head_, side_}])
            if (r.r == id.sector) r.deleted = id.deleted;
    }
    return true;
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

// ---------------------------------------------------------------------------
// READ TRACK -- the track as the head would read it, index pulse to index pulse.
//
// A raw image keeps payloads only, so the track is BUILT: the ID fields from sectorIdAt (the
// ones a format wrote this session, or ones made from the geometry), the data from the image,
// both CRCs computed (fieldCrc, chips/floppy.h), and the gaps at the IBM standard lengths:
//
//              FM (IBM 3740)              MFM (IBM System 34)
//   gap 4a     40 x FF                    80 x 4E
//   index      6 x 00, FC                 12 x 00, C2 C2 C2 FC
//   gap 1      26 x FF                    50 x 4E
//   per sector:
//   ID         6 x 00, FE C H R N CRC     12 x 00, A1 A1 A1 FE C H R N CRC
//   gap 2      11 x FF                    22 x 4E
//   data       6 x 00, FB/F8 data CRC     12 x 00, A1 A1 A1 FB/F8 data CRC
//   gap 3      27 x FF                    54 x 4E
//   gap 4b     FF to the index            4E to the index
//
// The length is one revolution at the chip's data rate. Gap 3 is made shorter when the track
// does not fit at the standard length, and gap 4b takes what is left. A track that does not
// fit even with no gap 3 is cut at the index pulse, as the chip's command ends there.
//
// THE GAPS ARE THE ONE THING MADE UP. A guest that formatted with its own gap lengths reads
// back the standard ones; every ID byte, data mark and data byte is what the guest wrote.
// ---------------------------------------------------------------------------
bool DiskImageDrive::readTrackImage(std::vector<uint8_t>& out, long long rate) {
    TrackFormat tf;
    if (!trackFmt(tf) || tf.sectors <= 0 || tf.sectorSize <= 0) return false;
    const int rev = revolutionBytes(rate, revsPerSec_);
    if (rev <= 0) return false;

    const bool    mfm   = tf.density == Density::DD;
    const uint8_t gap   = mfm ? 0x4E : 0xFF;
    const int     gap4a = mfm ? 80 : 40, gap1 = mfm ? 50 : 26, gap2 = mfm ? 22 : 11;
    const int     zeros = mfm ? 12 : 6;  // the sync run in front of every mark
    const int     syncs = mfm ? 3 : 0;   // A1 A1 A1 (C2 C2 C2 for the index mark)

    // The bytes of everything but gap 3: the index preamble, then each sector's two fields.
    const long long pre  = gap4a + zeros + syncs + 1 + gap1;
    const long long per  = (zeros + syncs + 1 + 4 + 2) + gap2 + (zeros + syncs + 1 + tf.sectorSize + 2);
    const long long room = rev - pre - per * tf.sectors;
    int gap3 = mfm ? 54 : 27;
    if (room < (long long)gap3 * tf.sectors) gap3 = room > 0 ? (int)(room / tf.sectors) : 0;

    // Sized up front to one revolution and filled by index; gap 4b is the fill already there.
    out.assign((size_t)rev, gap);
    size_t p   = 0;
    auto   put = [&](uint8_t b, int count) {
        for (int k = 0; k < count && p < out.size(); ++k) out[p++] = b;
    };
    auto putBytes = [&](const uint8_t* b, size_t count) {
        for (size_t k = 0; k < count && p < out.size(); ++k) out[p++] = b[k];
    };
    auto putCrc = [&](uint16_t crc) {
        put((uint8_t)(crc >> 8), 1);
        put((uint8_t)(crc & 0xFF), 1);
    };

    put(gap, gap4a);
    put(0x00, zeros);
    put(0xC2, syncs);
    put(0xFC, 1);
    put(gap, gap1);

    std::vector<uint8_t> data((size_t)tf.sectorSize, 0);
    for (int i = 0; i < tf.sectors; ++i) {
        SectorId id;
        if (!sectorIdAt(i, id)) { out.clear(); return false; }
        size_t n = data.size();
        if (!readData(id, data.data(), &n) || n != data.size()) { out.clear(); return false; }

        // The ID field. H is the side byte as written, or the selected side for a made-up ID.
        const uint8_t f[5] = {0xFE, (uint8_t)id.track, (uint8_t)(id.head >= 0 ? id.head : side_),
                              (uint8_t)id.sector, (uint8_t)id.lengthCode};
        put(0x00, zeros);
        put(0xA1, syncs);
        putBytes(f, sizeof f);
        putCrc(fieldCrc(mfm, f, sizeof f));
        put(gap, gap2);

        // The data field: the mark, the data, and a CRC over both.
        const uint8_t mark = id.deleted ? 0xF8 : 0xFB;
        put(0x00, zeros);
        put(0xA1, syncs);
        put(mark, 1);
        putBytes(data.data(), data.size());
        putCrc(crcCcitt(fieldCrc(mfm, &mark, 1), data.data(), data.size()));
        put(gap, gap3);
    }
    return true;
}

// Parse the raw track the guest streamed into `in` and (re)establish the addressed track's
// geometry as we fill it. The stream is IBM 3740: gap bytes, an index mark (0xFC), then per
// sector an ID field (0xFE track side sector N, then 0xF7) and a data field (a data address
// mark 0xF8-0xFB, `sectorSize` data bytes, then 0xF7). 0xF7 is the CRC-generate byte and can
// never appear as literal track data, so accumulate-until-0xF7 is unambiguous; 0xE5 fill is
// ordinary data. See the format FSM in docs/devguide/soft-sector-floppy.md, modelled on
// simh.mdsk/.../wd_17xx.c.
//
// A raw image keeps payloads only, so what it can record is a TrackFormat: one size and sector
// numbers that run startSector..startSector+n-1. The fields may come in any order (an
// interleaved format is the common case), so the start is the lowest sector number and each
// field's data goes to the sector its own ID names -- the rules formatTrack applies to the
// µPD765A. A track that does not fit that shape (mixed sizes, a gap in the numbers, a number
// twice) is refused before the image is touched (#931).
bool DiskImageDrive::writeTrackImage(const std::vector<uint8_t>& in, bool doubleDensity) {
    if (!img_) return false;

    // Each parsed data field: its sector number and length code from the ID field, and an
    // (offset, length) window into `in` -- we copy nothing, and write straight from the buffer
    // the chip already collected.
    struct Field { int sec, lenN; size_t off, len; RecordedId id; };
    std::vector<Field> fields;

    const size_t n = in.size();
    size_t       i = 0;
    while (i < n) {
        if (in[i] != 0xFE) { ++i; continue; }  // gap / index mark -- scan to the next ID field
        ++i;                                    // consume the 0xFE ID address mark
        if (i + 4 > n) break;                   // a truncated header ends the track

        // ID field: track, side, sector, length code N, all kept as written (the ID list,
        // floppy-drive.h). The sector number places the data in the image; sectorSize =
        // 128 << N (IBM 3740).
        RecordedId rid;
        rid.c = in[i], rid.h = in[i + 1], rid.r = in[i + 2], rid.n = in[i + 3];
        const int sec  = rid.r;
        const int lenN = rid.n & 0x03;
        i += 4;

        // Skip the ID CRC (0xF7) and the gap up to the data address mark. 0xFB is a data mark,
        // 0xF8 the deleted data mark, and 0xF9/0xFA the 1771's two user marks. The data lands in
        // the image either way; the ID list keeps whether the mark was F8. Bail to the outer
        // scan if the next ID field arrives first -- an ID with no data field is not a sector.
        auto isDataMark = [](uint8_t b) { return b >= 0xF8 && b <= 0xFB; };
        while (i < n && !isDataMark(in[i]) && in[i] != 0xFE) ++i;
        if (i >= n || !isDataMark(in[i])) continue;
        rid.deleted = in[i] == 0xF8;
        ++i;  // consume the data address mark

        // The data field is EXACTLY the bytes the software streamed, up to the 0xF7 CRC-generate
        // terminator (which can never be literal data). We reference it in place -- no fabricated
        // or padded fill: the sector gets what the formatter wrote (0xE5 for CP/M, but that is
        // the guest's choice). A data field shorter than the recorded sectorSize is malformed and
        // writeSector rejects it below -> WRITE FAULT, which is honest.
        const size_t off = i;
        while (i < n && in[i] != 0xF7) ++i;
        fields.push_back({sec, lenN, off, i - off, rid});
        if (i < n) ++i;  // consume the 0xF7
    }

    if (fields.empty()) return false;  // nothing legible -> WRITE FAULT (the chip sets S5)

    // The track must have one image form: one size, and the numbers lo..lo+n-1 once each.
    // `order[k]` is the field that holds sector lo+k, so the writes below go in ascending order.
    const int lenN = fields[0].lenN;
    int       lo   = fields[0].sec;
    for (const Field& f : fields) {
        if (f.lenN != lenN) return false;
        if (f.sec < lo) lo = f.sec;
    }
    std::vector<size_t> order(fields.size(), fields.size());
    for (size_t s = 0; s < fields.size(); ++s) {
        const int k = fields[s].sec - lo;
        if (k >= (int)fields.size() || order[(size_t)k] != fields.size()) return false;
        order[(size_t)k] = s;
    }

    // Density comes from the CHIP's DDEN pin, the single source of truth (Wd17xx::doubleDensity,
    // handed in). It is not read from the data rate: a 5.25" double-density track streams at
    // 250 kbit/s, the 8" single-density rate. This is what lets one DD card format a mixed
    // disk -- SD track 0, DD tracks 1-76 -- from the guest's per-track density bit.
    // setTrackFormat re-runs rebuild(), so the following tracks' offsets and the growth cap follow,
    // and a track that changes size moves the bytes after it (disk.h).
    TrackFormat tf;
    tf.density     = doubleDensity ? Density::DD : Density::SD;
    tf.sectors     = (int)fields.size();
    tf.sectorSize  = 128 << lenN;
    tf.startSector = lo;
    img_->setTrackFormat(head_, side_, tf);

    // The ID fields as written, in the order they go round the track.
    std::vector<RecordedId>& ids = ids_[{head_, side_}];
    ids.clear();
    for (const Field& f : fields) ids.push_back(f.id);

    // Each field to the sector its own ID names, in ascending order, so a blank image grows in
    // order with no gap to fill.
    bool ok = true;
    for (size_t k = 0; ok && k < order.size(); ++k) {
        const Field& f   = fields[order[k]];
        size_t       len = f.len;
        ok = img_->writeSector(head_, side_, lo + (int)k, in.data() + f.off, &len);
    }
    // One sync for the track (see writeData). setTrackFormat has already changed the image, so a
    // track that faults part way (false -> WRITE FAULT) is synced too: the file holds what landed.
    img_->sync();
    return ok;
}

// TS: an image with two heads is a two-sided diskette.
bool DiskImageDrive::twoSided() const { return img_ != nullptr && img_->heads() > 1; }

// The µPD765A's Format a Track: the chip hands over each sector's ID field in the order it
// goes round the track, and one fill byte for every data field. A raw image records
// payloads only, so what it can keep is a TrackFormat: one size, one density, and sector
// numbers that run startSector..startSector+n-1. The ID fields may come in any order (an
// interleaved format is the common case), so the start is the lowest R. A track whose IDs
// do not fit that shape -- mixed sizes, a gap in the numbers, a number twice -- has no
// image form, and the chip is told so (false: it reports a fault) instead of having the
// image quietly record something else. The C, H, R and N the processor wrote are kept in
// the ID list (floppy-drive.h), in the order given, every data field with a normal mark.
bool DiskImageDrive::formatTrack(const std::vector<SectorId>& ids, uint8_t fill) {
    if (!img_ || !canFormat_ || ids.empty()) return false;

    const int  size = ids[0].size;
    const bool dd   = ids[0].doubleDensity;
    int        lo   = ids[0].sector;
    for (const SectorId& id : ids) {
        if (id.size != size || id.doubleDensity != dd) return false;
        if (id.sector < lo) lo = id.sector;
    }
    std::vector<bool> seen(ids.size(), false);
    for (const SectorId& id : ids) {
        const int k = id.sector - lo;
        if (k >= (int)ids.size() || seen[(size_t)k]) return false;
        seen[(size_t)k] = true;
    }

    TrackFormat tf;
    tf.density     = dd ? Density::DD : Density::SD;
    tf.sectors     = (int)ids.size();
    tf.sectorSize  = size;
    tf.startSector = lo;
    img_->setTrackFormat(head_, side_, tf);

    std::vector<RecordedId>& rec = ids_[{head_, side_}];
    rec.clear();
    for (const SectorId& id : ids)
        rec.push_back({id.track, id.head, id.sector, id.lengthCode, false});

    // In ascending order, so a blank image grows in order, with no gap to fill.
    const std::vector<uint8_t> data((size_t)size, fill);
    bool ok = true;
    for (int s = 0; ok && s < tf.sectors; ++s) {
        size_t len = data.size();
        ok = img_->writeSector(head_, side_, lo + s, data.data(), &len);
    }
    img_->sync();  // one per track, on every path past setTrackFormat (see writeTrackImage)
    return ok;
}

} // namespace altair
