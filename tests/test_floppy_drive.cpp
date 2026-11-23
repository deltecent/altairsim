// DiskImageDrive -- the ID fields a format wrote, kept for the session (#937).
//
// A raw image holds payloads only. The drive keeps the ID fields that a Write Track (or the
// 765's Format a Track) laid down -- their order round the track, C/H/R/N as written, and the
// data mark of each sector -- so the chip reads back what the guest formatted.

#include "boards/floppy-drive.h"
#include "chips/wd17xx.h"
#include "core/clock.h"
#include "core/debuglog.h"
#include "host/disk.h"
#include "host/media.h"
#include "test.h"
#include "trackdecode.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <memory>
#include <vector>

using namespace altair;

namespace {

struct Rec {
    int  c, h, r, n;
    bool deleted;
};

// A raw FM Write Track stream (IBM 3740 bytes as the guest streams them: F7 = write the CRC).
// Each sector's data is its own sector number, so a test sees where each field landed. Sized
// up front and filled by index: GCC 15 -Warray-bounds misfires on a grown byte vector.
std::vector<uint8_t> fmStream(const std::vector<Rec>& ids, size_t dataLen) {
    const size_t         per = 6 + 11 + 1 + dataLen + 1 + 27;  // ID, gap, mark, data, F7, gap
    std::vector<uint8_t> t(41 + ids.size() * per, 0xFF);       // FF gap everywhere
    t[40] = 0xFC;                                              // index mark
    for (size_t s = 0; s < ids.size(); ++s) {
        const Rec&    d      = ids[s];
        const size_t  b      = 41 + s * per;
        const uint8_t hdr[6] = {0xFE, (uint8_t)d.c, (uint8_t)d.h, (uint8_t)d.r, (uint8_t)d.n, 0xF7};
        for (size_t k = 0; k < 6; ++k) t[b + k] = hdr[k];
        t[b + 17] = d.deleted ? 0xF8 : 0xFB;
        for (size_t k = 0; k < dataLen; ++k) t[b + 18 + k] = (uint8_t)d.r;
        t[b + 18 + dataLen] = 0xF7;
    }
    return t;
}

// 26 x 128, interleave 2 (1, 14, 2, 15, ...), every ID's C = `c` and H = `h`, and sector
// `del` with a deleted data mark.
std::vector<Rec> skewed(int c, int h, int del, int n = 0) {
    std::vector<Rec> v;
    for (int k = 0; k < 26; ++k) {
        const int r = (k % 2) ? 14 + k / 2 : 1 + k / 2;
        v.push_back({c, h, r, n, r == del});
    }
    return v;
}

// The IDs a raw track with no list gets: C the physical track, H the side, R ascending from 1.
std::vector<Rec> madeUp(int c, int h, int n) {
    std::vector<Rec> v;
    for (int r = 1; r <= 26; ++r) v.push_back({c, h, r, n, false});
    return v;
}

// An 8" single-sided image, 3 tracks declared 26 x `size` at density `d`, all zero.
struct Disk {
    DiskImage      img;
    DiskImageDrive drv;
    explicit Disk(Density d = Density::SD, int size = 128)
        : img(std::make_unique<MemoryMedia>("t.dsk", std::vector<uint8_t>((size_t)(3 * 26 * size), 0))) {
        img.init(3, 1, false);
        img.initFormat(0, 2, 0, 0, d, 26, size, 1);
        drv.mount(&img, false);
        drv.setFormatting(true);
    }
};

// Check a decoded track against the IDs it should hold: one index mark first, then for each ID
// in `want` (in order) an ID field and a data field whose bytes are all `fill(r)`. Every CRC good.
template <class Fill>
bool trackMatches(const std::vector<TrackField>& f, const std::vector<Rec>& want, size_t dataLen, Fill fill) {
    if (f.size() != 1 + 2 * want.size() || f[0].mark != 0xFC) return false;
    for (size_t s = 0; s < want.size(); ++s) {
        const TrackField& id = f[1 + 2 * s];
        const TrackField& dt = f[2 + 2 * s];
        const Rec&   w  = want[s];
        if (id.mark != 0xFE || !id.crcOk) return false;
        if (id.body[0] != w.c || id.body[1] != w.h || id.body[2] != w.r || id.body[3] != w.n) return false;
        if (dt.mark != (w.deleted ? 0xF8 : 0xFB) || !dt.crcOk || dt.body.size() != dataLen) return false;
        for (uint8_t b : dt.body)
            if (b != fill(w.r)) return false;
    }
    return true;
}

template <class F>
void spin(Wd17xx& f, Clock& clk, F svc) {
    const uint64_t t0 = clk.now();
    while (f.busy() && clk.now() - t0 < 60000000) {
        clk.advance(8);
        f.poll(clk);
        svc(f);
    }
}

// Read Sector `r` with the track register at `tr`; returns the status and the bytes.
uint8_t readSector(Wd17xx& f, Clock& clk, int tr, int r, std::vector<uint8_t>& got) {
    got.clear();
    f.writeTrackReg((uint8_t)tr);
    f.writeSectorReg((uint8_t)r);
    f.writeCommand(0x80, clk);
    spin(f, clk, [&](Wd17xx& c) {
        if (c.drq()) got.push_back(c.readData(clk));
    });
    return f.readStatus(clk);
}

// Write Sector `r` of 128 bytes of `v`; `cmd` is 0xA0, or 0xA1 for the 179x deleted mark.
uint8_t writeSector(Wd17xx& f, Clock& clk, uint8_t cmd, int tr, int r, uint8_t v) {
    f.writeTrackReg((uint8_t)tr);
    f.writeSectorReg((uint8_t)r);
    f.writeCommand(cmd, clk);
    spin(f, clk, [&](Wd17xx& c) {
        if (c.drq()) c.writeData(v, clk);
    });
    return f.readStatus(clk);
}

// AN IMAGE TO PUT BAD SECTORS ON (#966): 8" SSSD, 3 tracks of 26 x 128, every byte of sector
// r on track t is (t * 32 + r). The MemoryMedia stays reachable, so a test can prove that a
// fault never touched the image.
struct FaultDisk {
    MemoryMedia*   m = nullptr;
    DiskImage      img;
    DiskImageDrive drv;
    static std::vector<uint8_t> bytes() {
        std::vector<uint8_t> b((size_t)(3 * 26 * 128), 0);
        for (size_t i = 0; i < b.size(); ++i) b[i] = (uint8_t)((i / 128 / 26) * 32 + (i / 128 % 26) + 1);
        return b;
    }
    FaultDisk() : img(take()) {
        img.init(3, 1, false);
        img.initFormat(0, 2, 0, 0, Density::SD, 26, 128, 1);
        drv.mount(&img, false);
    }
    std::unique_ptr<MediaFile> take() {
        auto owned = std::make_unique<MemoryMedia>("f.dsk", bytes());
        m          = owned.get();
        return owned;
    }
    void fault(const char* spec) {
        std::string err;
        drv.faults().add(spec, err);
    }
    const DiskFault& at(size_t i) const { return drv.faults().list()[i]; }
};

// Route the debug sink to a scratch file while `f` runs, and return what reached it.
std::string captureDebug(const std::function<void()>& f) {
    namespace fs     = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "altair_fault_debug.log";
    std::error_code ec;
    fs::remove(p, ec);
    std::string err;
    dbg::setSink(dbg::Sink::File, p.string(), err);
    f();
    dbg::setSink(dbg::Sink::Stderr, "", err);
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    fs::remove(p, ec);
    return ss.str();
}

} // namespace

void test_floppy_drive() {
    SECTION("DiskImageDrive: Write Track keeps the ID fields as written (#937)");
    {
        Disk d;
        d.drv.setHeadTrack(1);
        CHECK(d.drv.writeTrackImage(fmStream(skewed(40, 1, 5), 128), false),
              "Write Track of an interleaved track lands");

        FloppyDrive::SectorId id;
        bool order = true, ids = true;
        const auto want = skewed(40, 1, 5);
        for (int i = 0; i < 26; ++i) {
            if (!d.drv.sectorIdAt(i, id) || id.sector != want[(size_t)i].r) order = false;
            if (id.track != 40 || id.head != 1 || id.lengthCode != 0 || id.size != 128) ids = false;
            if (id.deleted != (id.sector == 5)) ids = false;
        }
        CHECK(order, "the IDs read back in the order they were written (1, 14, 2, 15, ...)");
        CHECK(ids, "...with C, H and N as written, and only sector 5 deleted");

        uint8_t buf[128];
        size_t  n = sizeof buf;
        CHECK(d.img.readSector(1, 0, 14, buf, &n) && buf[0] == 14 && buf[127] == 14,
              "each data field went to the sector its own ID names");

        d.drv.setHeadTrack(2);
        CHECK(d.drv.sectorIdAt(0, id) && id.track == 2 && id.sector == 1 && id.head == -1,
              "a track this session did not format still gets IDs made from its geometry");

        d.drv.mount(&d.img, false);
        d.drv.setHeadTrack(1);
        CHECK(d.drv.sectorIdAt(1, id) && id.track == 1 && id.sector == 2 && !id.deleted,
              "a remount forgets the list: made-up IDs again, ascending");
    }

    SECTION("DiskImageDrive: the chip reads back what the format wrote");
    {
        Disk  d;
        Clock clk;
        Wd1791 f("fdc");
        f.attach(&d.drv);
        f.powerOn(clk);
        spin(f, clk, [](Wd17xx&) {});
        d.drv.setHeadTrack(0);
        CHECK(d.drv.writeTrackImage(fmStream(skewed(40, 1, 5), 128), false), "track 0 formatted");

        f.writeCommand(0x07, clk);  // Restore, verify on
        spin(f, clk, [](Wd17xx&) {});
        CHECK(f.readStatus(clk) & 0x10, "Restore with verify: the ID says C 40, so Seek Error");

        std::vector<uint8_t> got;
        uint8_t st = readSector(f, clk, 0, 3, got);
        CHECK((st & 0x10) && got.empty(), "Read Sector with TR 0: Record Not Found");
        st = readSector(f, clk, 40, 3, got);
        CHECK(!(st & 0x30) && got.size() == 128 && got[0] == 3, "TR 40 finds sector 3, a normal mark");
        st = readSector(f, clk, 40, 5, got);
        CHECK((st & 0x20) && got.size() == 128 && got[0] == 5, "sector 5 has the deleted mark (S5)");

        got.clear();
        f.writeCommand(0xC0, clk);  // Read Address
        spin(f, clk, [&](Wd17xx& c) {
            if (c.drq()) got.push_back(c.readData(clk));
        });
        CHECK(got.size() == 6 && got[0] == 40 && got[1] == 1 && got[2] == 1,
              "Read Address: C 40, the H byte as written (1), sector 1");
    }

    SECTION("DiskImageDrive: Write Sector writes the data mark");
    {
        Disk  d;
        Clock clk;
        Wd1791 f("fdc");
        f.attach(&d.drv);
        f.powerOn(clk);
        spin(f, clk, [](Wd17xx&) {});
        d.drv.setHeadTrack(2);  // never formatted this session: made-up IDs

        uint8_t st = writeSector(f, clk, 0xA1, 2, 7, 0x77);  // a0 = 1: deleted mark
        CHECK(!(st & 0x5C), "Write Sector with a0 set lands");
        std::vector<uint8_t> got;
        st = readSector(f, clk, 2, 7, got);
        CHECK((st & 0x20) && got.size() == 128 && got[0] == 0x77, "...and reads back deleted (S5)");
        st = readSector(f, clk, 2, 8, got);
        CHECK(!(st & 0x20), "...the other sectors keep their normal mark");
        FloppyDrive::SectorId id;
        CHECK(d.drv.sectorIdAt(0, id) && id.track == 2 && id.sector == 1 && id.head == -1,
              "...and their made-up IDs");

        writeSector(f, clk, 0xA0, 2, 7, 0x78);  // a0 = 0: normal mark
        st = readSector(f, clk, 2, 7, got);
        CHECK(!(st & 0x20) && got[0] == 0x78, "a normal Write Sector clears the deleted mark");
    }

    SECTION("DiskImageDrive: the 765's Format a Track keeps its IDs");
    {
        Disk d;
        d.drv.setHeadTrack(1);
        std::vector<FloppyDrive::SectorId> ids;
        for (const Rec& r : skewed(9, 1, 0)) {
            FloppyDrive::SectorId id;
            id.track = r.c, id.head = r.h, id.sector = r.r, id.lengthCode = r.n, id.size = 128;
            ids.push_back(id);
        }
        CHECK(d.drv.formatTrack(ids, 0xE5), "Format a Track lands");
        FloppyDrive::SectorId id;
        CHECK(d.drv.sectorIdAt(1, id) && id.sector == 14 && id.track == 9 && id.head == 1 &&
                  !id.deleted,
              "the IDs read back in the order given, with C and H as written");
    }

    // ---- READ TRACK (#937) ----
    // The drive builds one revolution from the ID list, the data and the IBM standard gaps. The
    // decoder above checks the fields; the offsets check the gap lengths themselves.
    const auto own  = [](int r) { return (uint8_t)r; };   // fmStream: each sector holds its R
    const auto zero = [](int) { return (uint8_t)0; };     // a fresh Disk is all zeros

    SECTION("DiskImageDrive: Read Track builds an FM track from what Write Track wrote");
    {
        Disk d;
        d.drv.setHeadTrack(1);
        const auto want = skewed(40, 1, 5);
        CHECK(d.drv.writeTrackImage(fmStream(want, 128), false), "track 1 formatted, interleaved");

        std::vector<uint8_t> t;
        CHECK(d.drv.readTrackImage(t, 250000) && t.size() == 5208,
              "one revolution: 5208 bytes at 250 kbit/s and 6 rev/s");
        const auto f = decodeTrack(t, false, 128);
        CHECK(trackMatches(f, want, 128, own),
              "the index mark, then the 26 IDs in the written order with C 40 and H 1, sector 5 "
              "with F8, each data field its own sector's, and every CRC good");

        // IBM 3740: 40 FF, 6 x 00, FC at 46, 26 FF, then 6 x 00 and the first ID mark at 79.
        // A sector is 6+7+11+6+1+128+2 = 161 bytes and gap 3 is 27, so the next is 188 on.
        bool at = f.size() == 53 && f[0].at == 46;
        for (size_t s = 0; at && s < 26; ++s)
            at = f[1 + 2 * s].at == 79 + 188 * s && f[2 + 2 * s].at == 103 + 188 * s;
        CHECK(at, "the standard FM gaps: index mark at 46, ID marks at 79 + 188k, data marks 24 on");
        bool gaps = true;
        for (size_t k = 0; k < 40; ++k) gaps = gaps && t[k] == 0xFF;
        for (size_t k = 4934; k < t.size(); ++k) gaps = gaps && t[k] == 0xFF;
        CHECK(gaps, "gap 4a is FF, and so is everything after the last gap 3 (gap 4b)");
    }

    SECTION("DiskImageDrive: Read Track builds an MFM track, with the A1s in the CRCs");
    {
        Disk d(Density::DD, 256);
        d.drv.setHeadTrack(1);
        const auto want = skewed(7, 0, 20, 1);
        CHECK(d.drv.writeTrackImage(fmStream(want, 256), true), "track 1 formatted, double density");

        std::vector<uint8_t> t;
        CHECK(d.drv.readTrackImage(t, 500000) && t.size() == 10416,
              "one revolution: 10416 bytes at 500 kbit/s");
        const auto f = decodeTrack(t, true, 256);
        CHECK(trackMatches(f, want, 256, own),
              "the IDs in the written order, sector 20 with F8, every CRC good over A1 A1 A1");

        // System 34: 80 4E, 12 x 00, C2 C2 C2, FC at 95, 50 4E, then 12 x 00, A1 A1 A1 and the
        // first ID mark at 161. A sector is 318 bytes and gap 3 is 54, so the next is 372 on.
        bool at = f.size() == 53 && f[0].at == 95;
        for (size_t s = 0; at && s < 26; ++s)
            at = f[1 + 2 * s].at == 161 + 372 * s && f[2 + 2 * s].at == 205 + 372 * s;
        CHECK(at, "the standard MFM gaps: index mark at 95, ID marks at 161 + 372k");
        CHECK(t[0] == 0x4E && t[79] == 0x4E && t.back() == 0x4E, "the gaps are 4E");

        d.drv.setHeadTrack(2);
        CHECK(d.drv.readTrackImage(t, 500000) &&
                  trackMatches(decodeTrack(t, true, 256), madeUp(2, 0, 1), 256, zero),
              "a track not formatted this session: IDs made from the geometry (C 2, H 0, N 1)");
    }

    SECTION("DiskImageDrive: Read Track is one revolution long");
    {
        Disk d;
        d.drv.setHeadTrack(0);
        std::vector<uint8_t> t;
        d.drv.setRevsPerSecond(5);
        CHECK(d.drv.readTrackImage(t, 250000) && t.size() == 6250, "5.25\" at 250 kbit/s: 6250 bytes");
        CHECK(trackMatches(decodeTrack(t, false, 128), madeUp(0, 0, 0), 128, zero),
              "...with the whole track in it");
        CHECK(d.drv.readTrackImage(t, 500000) && t.size() == 12500, "5.25\" at 500 kbit/s: 12500 bytes");

        // 26 x 128 FM needs 73 + 26 x 161 = 4259 bytes with no gap 3. At 4500 bytes there are 241
        // to share, so gap 3 is 9 and the sectors are 170 apart.
        d.drv.setRevsPerSecond(6);
        CHECK(d.drv.readTrackImage(t, 216000) && t.size() == 4500, "a 4500-byte revolution");
        auto f = decodeTrack(t, false, 128);
        CHECK(trackMatches(f, madeUp(0, 0, 0), 128, zero) && f[3].at - f[1].at == 170,
              "...holds the whole track with gap 3 made shorter (9 bytes)");

        // At 2604 bytes the track does not fit at all: it is cut at the index pulse.
        CHECK(d.drv.readTrackImage(t, 125000) && t.size() == 2604, "a 2604-byte revolution");
        f = decodeTrack(t, false, 128);
        CHECK(f.size() > 3 && f.size() < 53 && f[3].at - f[1].at == 161,
              "...is cut at the index pulse, with no gap 3, and the sectors that fit are whole");
    }

    SECTION("DiskImageDrive: Read Track with no track to read");
    {
        std::vector<uint8_t> t;
        DiskImageDrive empty;
        CHECK(!empty.readTrackImage(t, 250000), "no disk: no track");
        Disk d;
        d.drv.setHeadTrack(5);  // the image declares tracks 0-2
        CHECK(!d.drv.readTrackImage(t, 250000), "a track with no format: no track");
    }

    SECTION("DiskImageDrive: the chip's Read Track (E4) streams the track");
    {
        Disk  d;
        Clock clk;
        Wd1791 f("fdc");
        f.attach(&d.drv);
        f.powerOn(clk);
        spin(f, clk, [](Wd17xx&) {});
        d.drv.setHeadTrack(0);
        CHECK(d.drv.writeTrackImage(fmStream(skewed(0, 0, 9), 128), false), "track 0 formatted");

        std::vector<uint8_t> want, got;
        CHECK(d.drv.readTrackImage(want, f.dataRateBits), "the drive's track");
        f.writeCommand(0xE4, clk);  // Read Track
        spin(f, clk, [&](Wd17xx& c) {
            if (c.drq()) got.push_back(c.readData(clk));
        });
        CHECK(got == want && got.size() == 5208, "E4 hands over the whole revolution, byte for byte");
        CHECK((f.readStatus(clk) & 0x7C) == 0, "...with no error bits");

        // With the density check on, a chip clocked for MFM finds nothing it can read in FM.
        f.setDensityChecked(true);
        f.doubleDensity = true;
        got.clear();
        f.drainLog();
        f.writeCommand(0xE4, clk);
        spin(f, clk, [&](Wd17xx& c) {
            if (c.drq()) got.push_back(c.readData(clk));
        });
        bool said = false;
        for (const std::string& l : f.drainLog())
            said = said || l.find("other density") != std::string::npos;
        CHECK(got.empty() && said, "a track at the other density gives no bytes, and the log says why");
    }

    // ---- BAD SECTORS (#966) ----
    // WD1791 status bits: S3 CRC Error 0x08, S4 Record Not Found 0x10, S5 Write Fault 0x20.

    auto wd = [](FaultDisk& d, Clock& clk, Wd1791& f) {
        f.attach(&d.drv);
        f.powerOn(clk);
        spin(f, clk, [](Wd17xx&) {});
    };

    SECTION("DiskImageDrive faults: a data fault is a data CRC error, and the data still arrives");
    {
        FaultDisk d;
        Clock     clk;
        Wd1791    f("fdc");
        wd(d, clk, f);
        d.fault("0/0/3 data count=2");
        const std::vector<uint8_t> before = d.m->bytes();

        std::vector<uint8_t> got;
        uint8_t st = readSector(f, clk, 0, 3, got);
        CHECK((st & 0x18) == 0x08, "first read: S3 up, S4 down (a data-field error)");
        CHECK(got.size() == 128 && got[0] == 3 && got[127] == 3, "...and the sector's data arrived");
        st = readSector(f, clk, 0, 3, got);
        CHECK((st & 0x18) == 0x08, "second read: S3 again");
        CHECK(d.at(0).hits == 2 && d.at(0).left == 0, "two hits, none left");
        st = readSector(f, clk, 0, 3, got);
        CHECK((st & 0x3C) == 0 && got.size() == 128 && got[0] == 3, "third read: clean -- the retry recovered");
        CHECK(d.at(0).hits == 2, "a clean read uses nothing");
        st = readSector(f, clk, 0, 4, got);
        CHECK((st & 0x3C) == 0 && got[0] == 4, "the sector next to it was never bad");
        CHECK(d.m->bytes() == before, "the image is unchanged");
    }

    SECTION("DiskImageDrive faults: a hard id fault is Record Not Found with S3, every time");
    {
        FaultDisk d;
        Clock     clk;
        Wd1791    f("fdc");
        wd(d, clk, f);
        d.fault("0/0/4 id");
        std::vector<uint8_t> got;
        bool                 all = true;
        for (int i = 0; i < 5; ++i) all = all && (readSector(f, clk, 0, 4, got) & 0x18) == 0x18 && got.empty();
        CHECK(all, "five reads: S4 and S3, no data");
        CHECK(d.at(0).hits == 5 && d.at(0).hard(), "five hits, and a hard fault never wears off");

        // THE S3 RULE: a search that passes the bad ID field and finds its own sector leaves S3
        // down. With S3 up and S4 down the guest would read this clean sector as a CRC error.
        const uint8_t st = readSector(f, clk, 0, 9, got);
        CHECK((st & 0x18) == 0 && got.size() == 128 && got[0] == 9,
              "sector 9, past the bad ID field of sector 4: clean, S3 down");
        CHECK(d.at(0).hits == 5, "...and the fault on sector 4 was not used");
    }

    SECTION("DiskImageDrive faults: a missing ID field is Record Not Found without S3");
    {
        FaultDisk d;
        Clock     clk;
        Wd1791    f("fdc");
        wd(d, clk, f);
        d.fault("0/0/6 missing count=1");
        std::vector<uint8_t> got;
        CHECK((readSector(f, clk, 0, 6, got) & 0x18) == 0x10, "first read: S4 only");
        CHECK((readSector(f, clk, 0, 6, got) & 0x18) == 0 && got[0] == 6, "second read: found");
        CHECK(d.at(0).hits == 1, "one hit");

        // A sector the track does not have fails for its own reason and uses no fault.
        d.fault("0/0/27 missing");
        CHECK((readSector(f, clk, 0, 27, got) & 0x10) == 0x10, "sector 27 of 26: Record Not Found");
        CHECK(d.at(1).hits == 0, "...and the fault on it is not used");
    }

    SECTION("DiskImageDrive faults: a write fault is Write Fault, and the image keeps its data");
    {
        FaultDisk d;
        Clock     clk;
        Wd1791    f("fdc");
        wd(d, clk, f);
        d.fault("0/0/7 write count=1");
        const size_t at = 6 * 128;  // track 0, sector 7
        CHECK((writeSector(f, clk, 0xA0, 0, 7, 0xAA) & 0x20) == 0x20, "first write: S5");
        CHECK(d.m->bytes()[at] == 7 && d.m->bytes()[at + 127] == 7, "...and nothing reached the image");
        CHECK((writeSector(f, clk, 0xA0, 0, 7, 0xAA) & 0x20) == 0, "second write: taken");
        CHECK(d.m->bytes()[at] == 0xAA, "...and it is in the image");
        CHECK(d.at(0).hits == 1 && d.at(0).left == 0, "one hit");
    }

    SECTION("DiskImageDrive faults: wildcards, the side, and a new diskette");
    {
        FaultDisk d;
        Clock     clk;
        Wd1791    f("fdc");
        wd(d, clk, f);
        d.fault("0/*/* data");
        std::vector<uint8_t> got;
        CHECK((readSector(f, clk, 0, 1, got) & 0x08) && (readSector(f, clk, 0, 26, got) & 0x08),
              "0/*/*: every sector on track 0");
        d.drv.faults().clearAll();
        d.fault("0/1/1 data");
        CHECK((readSector(f, clk, 0, 1, got) & 0x08) == 0, "a fault on side 1 is not on side 0");

        d.drv.mount(&d.img, false);
        CHECK(d.drv.faults().empty(), "mount: a new diskette brings no faults");
        d.fault("0/0/1 data");
        d.drv.eject();
        CHECK(d.drv.faults().empty(), "eject clears them too");
    }

    SECTION("DiskImageDrive faults: the debug line is written only while the flag is on");
    {
        FaultDisk    d;
        Clock        clk;
        Wd1791       f("fdc");
        dbg::Channel ch("faulttest", {"fault"});
        wd(d, clk, f);
        d.drv.setDebug([&ch] { return &ch; }, 0, "drive0");
        d.fault("0/0/3 data");
        std::vector<uint8_t> got;

        std::string out = captureDebug([&] { readSector(f, clk, 0, 3, got); });
        CHECK(out.empty(), "flag off: the fault fires and writes nothing");
        CHECK(d.at(0).hits == 1, "...but it was used");

        std::string err;
        CHECK(ch.enable("fault", err), "DEBUG=fault");
        out = captureDebug([&] { readSector(f, clk, 0, 3, got); });
        CHECK(out.find("faulttest: fault drive0 0/0/3 data hit 2\n") != std::string::npos &&
                  out.find('\n') == out.size() - 1,
              "flag on: exactly one line");

        d.drv.faults().clearAll();
        d.fault("0/0/5 data count=2");
        out = captureDebug([&] { readSector(f, clk, 0, 5, got); });
        CHECK(out.find("fault drive0 0/0/5 data hit 1, 1 left\n") != std::string::npos,
              "a count fault says how many are left");

        CHECK(ch.disable("fault", err), "NODEBUG=fault");
        out = captureDebug([&] { readSector(f, clk, 0, 5, got); });
        CHECK(out.empty(), "flag off again: nothing");
    }

    SECTION("DiskImageDrive faults: Read Track shows the image and uses no fault");
    {
        FaultDisk d;
        d.fault("0/0/2 missing");
        std::vector<uint8_t> t;
        CHECK(d.drv.readTrackImage(t, 250000) && !t.empty(), "the track reads");
        CHECK(d.at(0).hits == 0, "...and the fault is not used");
    }
}
