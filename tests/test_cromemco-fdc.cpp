// Cromemco 16FDC / 64FDC floppy controllers (boards/cromemco-fdc.cpp,
// docs/boards/cromemco-16fdc.md, docs/boards/cromemco-64fdc.md).
//
// The acceptance test boots CDOS off a real image end to end. This file pins the
// board-level facts that boot cannot see, and that a wrong guess would let boot
// paper over: the one-hot DS4-DS1 drive select (the decode that differs from the
// Tarbell's bitmap and the SD function-decoder), the OUT-40H ROM bank-out / RESET
// re-arm contract, the port-34 DENSITY x MAXI data-rate decode (the one whose "DD ->
// 500k always" mistake mis-clocks every 5.25" DD disk -- the likely shape of Joe's
// double-density failures), the mixed-density geometry probe, and the blank-formats-
// and-grows Write-Track path.
//
// THE 5501 CONSOLE is proved against its data sheet in test_tms5501.cpp; here it is
// only proved to be REACHABLE through the board's ports 00/01 -- the card wiring, not
// the chip.
//
// SIDE SELECT is the port-04 D1 register; with no OUT 04 the drive stays on side 0, which
// is where these unit tests read. On a real CDOS disk side 0 of cylinder 0 is the SD boot
// track (128-byte sectors, IBM 3740) and side 1 is DD -- the full two-sided boot is an
// ACCEPTANCE-level round-trip with the real media (task 5); what a unit test pins is the
// PROBE that lays out all three ranges, and the side-0 SD read + DD format path, both below.

#include "boards/cromemco-16fdc.h"
#include "boards/cromemco-64fdc.h"
#include "boards/s100-memory.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/roms.h"
#include "host/media.h"
#include "host/stream.h"
#include "test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

// The Cromemco FDC ports (hard-decoded, reference §2).
constexpr uint8_t SER_ST = 0x00;  // TMS 5501 status (in) / baud (out)
constexpr uint8_t SER_DT = 0x01;  // TMS 5501 receive (in) / transmit (out)
constexpr uint8_t FD_CMD = 0x30;  // FD1793 status (in) / command (out)
constexpr uint8_t FD_TRK = 0x31;  // FD1793 track
constexpr uint8_t FD_SEC = 0x32;  // FD1793 sector
constexpr uint8_t FD_DAT = 0x33;  // FD1793 data (NEVER wait-synced -- reference §6)
constexpr uint8_t FD_FLG = 0x34;  // disk flags (in) / control (out)
constexpr uint8_t AUX    = 0x04;  // auxiliary disk command (out) / status (in) -- reference §5
constexpr uint8_t BANK   = 0x40;  // OUT 40H banks the RDOS ROM out until RESET

// Port-04 OUT bits (reference §5, all active-low). D3 ¬RESTORE forces the selected drive to
// track 0 on the 4FDC/16FDC (not the 64FDC); D1 ¬SIDE SELECT (1 = side 0). 0xF7 = D3 low
// (restore asserted) with D1 high (side 0).
constexpr uint8_t AUX_RESTORE = 0xF7;  // D3=0 -> home selected drive

// Port-34 OUT control bits (reference §4).
constexpr uint8_t AUTOWAIT = 0x80;  // D7: Auto Wait -- the CPU-stall (wait-sync) path
constexpr uint8_t DDEN     = 0x40;  // D6: double density
constexpr uint8_t MOTOR    = 0x20;  // D5: motor on
constexpr uint8_t MAXI     = 0x10;  // D4: 8" (set) / 5.25" (clear)
// D3-D0: one-hot drive select DS4-DS1.

// Port-34 IN flag bits. (D0 is EOJ/INTRQ; the poll loops key off DRQ falling, not EOJ, so
// it is read implicitly rather than named here.)
constexpr uint8_t F_DRQ   = 0x80;  // D7: DRQ
constexpr uint8_t F_NBOOT = 0x40;  // D6: ¬BOOT (high when the strap is OFF)

uint8_t in(CromemcoFdcBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(CromemcoFdcBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}
bool decodesMem(CromemcoFdcBoard& b, uint16_t addr) {
    BusCycle c;
    c.type = Cycle::MemRead;
    c.addr = addr;
    return b.decodes(c);
}
uint8_t readMem(CromemcoFdcBoard& b, uint16_t addr) {
    BusCycle c;
    c.type = Cycle::MemRead;
    c.addr = addr;
    return b.read(c);
}
bool phantomsMem(CromemcoFdcBoard& b, uint16_t addr, Cycle type = Cycle::MemRead) {
    BusCycle c;
    c.type = type;
    c.addr = addr;
    return b.assertsPhantom(c);
}

// A ramp disk resolver: byte i holds (i + i/128) & 0xFF, so each 128-byte block gets its
// own linear stamp and adjacent sectors never look alike. Same fixture as test_tarbell.
void withRampDisk(uint64_t bytes) {
    setMediaResolver([bytes](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d((size_t)bytes);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)((i + i / 128) & 0xFF);
        return std::make_unique<MemoryMedia>(path, std::move(d), ro);
    });
}

// A 48K RAM board (0000-BFFF). The Cromemco ROM sits in a memory HOLE at C000 -- unlike
// the Tarbell's low-RAM PHANTOM* shadow, it does not overlay RAM, so the fixture leaves
// C000 up for the card to own. The RAM only proves the ports and low memory are ordinary.
MemoryBoard* ram48k(const char* id) {
    auto* m = new MemoryBoard();
    m->id = id;
    std::string err;
    setProperty(*m, "fill", "zero", err);
    Region r;
    r.kind = RegionKind::Ram;
    r.at   = 0x0000;
    r.size = 0xC000;  // 0000-BFFF
    m->addRegion(r, err);
    m->power();
    return m;
}

// Read a sector the Cromemco way: the data port (33) is NOT wait-synced, so the driver
// arms Auto Wait (OUT 34 D7) and polls the flag port (34) for DRQ, reading a data byte
// (33) each time DRQ shows, until the command completes (INTRQ / DRQ gone). A hard cap
// keeps a stuck DRQ from hanging the test. `ctl` carries the density/MAXI/drive bits the
// caller wants latched alongside Auto Wait.
std::vector<uint8_t> pollRead(CromemcoFdcBoard& b, uint8_t ctl) {
    out(b, FD_FLG, (uint8_t)(AUTOWAIT | ctl));  // arm Auto Wait + density/drive
    std::vector<uint8_t> got;
    for (int guard = 0; guard < 8192; ++guard) {
        uint8_t f = in(b, FD_FLG);
        if (f & F_DRQ) { got.push_back(in(b, FD_DAT)); continue; }
        break;  // no DRQ: the command has completed (INTRQ) or the drive is not ready
    }
    return got;
}

// Stream a raw track the way DFORMAT does: arm Auto Wait, poll the flag port for DRQ and
// feed a byte (33) each time, padding with gap 0xFFs once the structured stream runs out,
// until the wait-synced Write Track completes. The caller must have issued Write Track (F4)
// with the density/drive already latched by the same `ctl`.
void pollWrite(CromemcoFdcBoard& b, uint8_t ctl, const std::vector<uint8_t>& stream) {
    (void)ctl;  // already latched by the caller's OUT 34 before Write Track
    size_t k = 0;
    for (int guard = 0; guard < 40000; ++guard) {
        uint8_t f = in(b, FD_FLG);
        if (f & F_DRQ) { out(b, FD_DAT, k < stream.size() ? stream[k++] : 0xFF); continue; }
        break;  // INTRQ: the track is done
    }
}

// Seek to `track`. A Type I command raises no DRQ, so Auto Wait completes it on the next
// flag read; a generous clock advance is belt-and-braces so the head really lands.
void seekTo(CromemcoFdcBoard& b, Clock& c, uint8_t ctl, int track) {
    out(b, FD_FLG, (uint8_t)(AUTOWAIT | ctl));
    out(b, FD_DAT, (uint8_t)track);  // seek target in the data register
    out(b, FD_CMD, 0x18);            // Seek, no verify
    (void)in(b, FD_FLG);             // Auto Wait runs the seek to completion
    c.advance(2000000);
}

// A well-formed Cromemco DD track: 16 sectors of 512 0xE5 bytes, MFM gap byte 0x4E, the 3x
// 0xF5 (A1 sync) run before each mark, per sector `FE trk 00 sec N=2 F7 ... FB (512xE5) F7`.
// The format parser trusts the head position for the track, reads the sector number and the
// length code N (=2 -> 512), and terminates each data field at 0xF7. ~9.4 KB of structure,
// under the ~10.4 KB 8"-DD revolution budget, so all 16 sectors stream before completion.
std::vector<uint8_t> ddTrack512(int trackNum) {
    std::vector<uint8_t> s;
    auto put = [&](uint8_t b, int n) { for (int i = 0; i < n; ++i) s.push_back(b); };
    put(0x4E, 40);                                    // lead-in gap (no index mark)
    for (int sec = 1; sec <= 16; ++sec) {
        put(0x4E, 12); put(0x00, 8); put(0xF5, 3);    // gap + sync + A1 marks
        s.push_back(0xFE);                            // ID address mark
        s.push_back((uint8_t)trackNum);               // track (ignored: head position wins)
        s.push_back(0x00);                            // side
        s.push_back((uint8_t)sec);                    // sector
        s.push_back(0x02);                            // length code N=2 -> 512 bytes
        s.push_back(0xF7);                            // ID CRC
        put(0x4E, 12); put(0x00, 8); put(0xF5, 3);    // gap + sync + A1 marks
        s.push_back(0xFB);                            // data address mark
        put(0xE5, 512);                               // the 512 payload bytes
        s.push_back(0xF7);                            // data CRC
    }
    return s;
}

// A ProbeBoard exposes the protected geometry probe and the latch state the bus cannot read
// back, so the decode can be pinned directly. It IS a 16FDC in every other respect.
struct ProbeBoard : Fdc16Board {
    struct Probe {
        bool ok;
        int  tracks, heads, rev;
        bool interleaved;
        std::string err;
        std::vector<FmtRange> ranges;
    };
    Probe probe(uint64_t bytes) {
        Probe p;
        p.ok = describeGeometry(bytes, p.tracks, p.heads, p.interleaved, p.rev, p.ranges, p.err);
        return p;
    }
    long long dataRate() const { return dataRate_; }
    int       selected() const { return sel_; }
    bool      maxiSet()  const { return maxi_; }
};

} // namespace

void test_cromemco_fdc() {
    SECTION("boards/cromemco-fdc: the Cromemco 16FDC / 64FDC floppy controllers");

    // ---- THE ONE-HOT DRIVE SELECT (OUT 34 D3-D0), by the numbers ----
    {
        Clock c;  // Clock BEFORE the board: the board's dtor cancels its wake on the Clock,
        ProbeBoard b;  // so the Clock must outlive it (Windows UAF otherwise -- see the memory)
        b.attachClock(&c);
        b.power();

        out(b, FD_FLG, 0x01);  CHECK(b.selected() == 0, "DS1 (D0) selects drive 0");
        out(b, FD_FLG, 0x02);  CHECK(b.selected() == 1, "DS2 (D1) selects drive 1");
        out(b, FD_FLG, 0x04);  CHECK(b.selected() == 2, "DS3 (D2) selects drive 2");
        out(b, FD_FLG, 0x08);  CHECK(b.selected() == 3, "DS4 (D3) selects drive 3");

        // Two bits set: the LOWEST wins (the one-hot latch's priority).
        out(b, FD_FLG, 0x0C);  CHECK(b.selected() == 2, "with D2+D3 set, the lowest bit wins");

        // No select bit set leaves the selection unchanged -- the boot loader never writes
        // port 34, so it runs against the default drive 0.
        out(b, FD_FLG, 0x02);  CHECK(b.selected() == 1, "select drive 1");
        out(b, FD_FLG, MAXI);  CHECK(b.selected() == 1, "no DS bit set leaves the drive selected");
    }

    // ---- THE DATA RATE IS MAXI x DDEN, NOT DDEN ALONE ----
    //
    // The diagnostic centrepiece. ONLY 8" double density is 500 kbit/s; 8" SD, 5.25" SD and
    // 5.25" DD are all 250 kbit/s. A naive "D6 -> 500k" mis-clocks every 5.25" DD disk.
    {
        Clock c;  // Clock BEFORE the board: the board's dtor cancels its wake on the Clock,
        ProbeBoard b;  // so the Clock must outlive it (Windows UAF otherwise -- see the memory)
        b.attachClock(&c);
        b.power();

        out(b, FD_FLG, MAXI | DDEN);  // 8" double density
        CHECK(b.dataRate() == 500000, "8\" DD is 500 kbit/s");
        CHECK(b.maxiSet(), "MAXI latched (8\")");

        out(b, FD_FLG, DDEN);         // 5.25" double density -- the trap
        CHECK(b.dataRate() == 250000, "5.25\" DD is 250 kbit/s, NOT 500 -- MAXI is clear");
        CHECK(!b.maxiSet(), "MAXI clear (5.25\")");

        out(b, FD_FLG, MAXI);         // 8" single density
        CHECK(b.dataRate() == 250000, "8\" SD is 250 kbit/s");

        out(b, FD_FLG, 0x00);         // 5.25" single density
        CHECK(b.dataRate() == 250000, "5.25\" SD is 250 kbit/s");
    }

    // ---- THE MIXED-DENSITY GEOMETRY PROBE (the 8" CDOS disk's three ranges) ----
    {
        ProbeBoard b;

        // 1,256,704 bytes: an SD boot track 0 side 0 (IBM 3740, so RDOS can read it with no
        // density known), a DD side 1 of cylinder 0, DD everywhere else -- confirmed against the
        // real CDOS 2.58 image (reference/Cromemco CDOS.md 3.1).
        auto cdos = b.probe(26ull * 128 + 16ull * 512 + 76ull * 2 * 16 * 512);
        CHECK(cdos.ok, "the 1,256,704-byte mixed CDOS image is recognized");
        CHECK(cdos.tracks == 77 && cdos.heads == 2, "77 tracks, double-sided");
        CHECK(cdos.rev == 6, "8\" / 360 RPM -> 6 rev/s");
        CHECK(cdos.interleaved, "image slot order is cylinder-major, head-minor (T0H0,T0H1,T1H0...)");
        CHECK(cdos.ranges.size() == 3, "three format ranges: SD t0h0, DD t0h1, DD the rest");
        if (cdos.ranges.size() == 3) {
            const auto& r0 = cdos.ranges[0];
            CHECK(r0.trackLo == 0 && r0.trackHi == 0 && r0.headLo == 0 && r0.headHi == 0 &&
                  r0.density == Density::SD && r0.sectors == 26 && r0.sectorSize == 128,
                  "range 0: track 0 side 0 is the SD boot track, 26x128");
            const auto& r1 = cdos.ranges[1];
            CHECK(r1.trackLo == 0 && r1.trackHi == 0 && r1.headLo == 1 && r1.headHi == 1 &&
                  r1.density == Density::DD && r1.sectors == 16 && r1.sectorSize == 512,
                  "range 1: track 0 side 1 is DD 16x512");
            const auto& r2 = cdos.ranges[2];
            CHECK(r2.trackLo == 1 && r2.trackHi == 76 && r2.headLo == 0 && r2.headHi == 1 &&
                  r2.density == Density::DD && r2.sectors == 16 && r2.sectorSize == 512,
                  "range 2: tracks 1-76 both sides are DD 16x512");
        }

        // A plain 8" single-density disk: 77 tracks, one side, one SD range.
        auto sd = b.probe(77ull * 26 * 128);  // 256,256
        CHECK(sd.ok, "the 256,256-byte plain SD image is recognized");
        CHECK(sd.tracks == 77 && sd.heads == 1, "77 tracks, single-sided");
        CHECK(sd.ranges.size() == 1 && sd.ranges[0].density == Density::SD &&
              sd.ranges[0].sectors == 26 && sd.ranges[0].sectorSize == 128,
              "one range: all tracks SD 26x128");

        // 5.25" mixed-density DSDD CDOS: 40 cyl, 2 heads, 300 RPM. cyl0/side0 SD 18x128 boot,
        // everything else DD 10x512 (the minifloppy's DD track, NOT the 8"'s 16x512). Confirmed
        // against 082DISK.IMD (406,784). Head order is cylinder-major like the 8" disk.
        auto dsdd = b.probe(18ull * 128 + 10ull * 512 + 39ull * 2 * 10 * 512);  // 406,784
        CHECK(dsdd.ok, "the 406,784-byte 5.25\" DSDD image is recognized");
        CHECK(dsdd.tracks == 40 && dsdd.heads == 2, "40 tracks, double-sided");
        CHECK(dsdd.rev == 5, "5.25\" / 300 RPM -> 5 rev/s");
        CHECK(dsdd.interleaved, "image slot order is cylinder-major, head-minor");
        CHECK(dsdd.ranges.size() == 3, "three format ranges: SD t0h0, DD t0h1, DD the rest");
        if (dsdd.ranges.size() == 3) {
            const auto& r0 = dsdd.ranges[0];
            CHECK(r0.trackLo == 0 && r0.trackHi == 0 && r0.headLo == 0 && r0.headHi == 0 &&
                  r0.density == Density::SD && r0.sectors == 18 && r0.sectorSize == 128,
                  "range 0: track 0 side 0 is the SD boot track, 18x128");
            const auto& r1 = dsdd.ranges[1];
            CHECK(r1.trackLo == 0 && r1.trackHi == 0 && r1.headLo == 1 && r1.headHi == 1 &&
                  r1.density == Density::DD && r1.sectors == 10 && r1.sectorSize == 512,
                  "range 1: track 0 side 1 is DD 10x512");
            const auto& r2 = dsdd.ranges[2];
            CHECK(r2.trackLo == 1 && r2.trackHi == 39 && r2.headLo == 0 && r2.headHi == 1 &&
                  r2.density == Density::DD && r2.sectors == 10 && r2.sectorSize == 512,
                  "range 2: tracks 1-39 both sides are DD 10x512");
        }

        // The 17-sector-boot variant (052C0253, 406,656): the SD boot count falls out of the
        // size, so the same branch handles it with a 17-sector range 0.
        auto dsdd17 = b.probe(17ull * 128 + 10ull * 512 + 39ull * 2 * 10 * 512);  // 406,656
        CHECK(dsdd17.ok && dsdd17.tracks == 40 && dsdd17.heads == 2 && dsdd17.rev == 5,
              "the 406,656-byte 5.25\" DSDD (17-sector boot) is recognized");
        CHECK(dsdd17.ranges.size() == 3 && dsdd17.ranges[0].sectors == 17 &&
              dsdd17.ranges[0].density == Density::SD && dsdd17.ranges[0].sectorSize == 128,
              "range 0 boot track is 17x128, derived from the size");

        // 5.25" plain single-density: 40 cyl, one side, all SD 18x128. One head, so moot order.
        auto ss = b.probe(40ull * 18 * 128);  // 92,160
        CHECK(ss.ok, "the 92,160-byte 5.25\" SSSD image is recognized");
        CHECK(ss.tracks == 40 && ss.heads == 1 && ss.rev == 5, "40 tracks, single-sided, 5 rev/s");
        CHECK(ss.ranges.size() == 1 && ss.ranges[0].trackLo == 0 && ss.ranges[0].trackHi == 39 &&
              ss.ranges[0].density == Density::SD && ss.ranges[0].sectors == 18 &&
              ss.ranges[0].sectorSize == 128, "one range: all 40 tracks SD 18x128");

        // A short SSSD dump (one sector lost, 92,032) stays in the blank fallback -- not claimed.
        auto ssShort = b.probe(40ull * 18 * 128 - 128);  // 92,032
        CHECK(ssShort.ok && ssShort.ranges.empty() && ssShort.heads == 2,
              "a short 5.25\" SSSD dump falls through to the blank fallback (best-effort)");

        // A blank / short image mounts UNFORMATTED (77 tracks, 2 heads, no ranges) -- not an
        // error, so MOUNT ... CREATE gives a formattable disk.
        auto blank = b.probe(0);
        CHECK(blank.ok && blank.ranges.empty(), "a 0-byte blank probes to empty geometry");
        CHECK(blank.tracks == 77 && blank.heads == 2, "...at the 8\" double-sided track count");

        // Oversized is the one refusal -- larger than the mixed CDOS disk, which a real one
        // never is.
        auto big = b.probe(16ull * 512 + 26ull * 128 + 76ull * 2 * 16 * 512 + 512);
        CHECK(!big.ok, "an oversized image is refused");
        CHECK(big.err.find("too large") != std::string::npos, "...with a reason that says why");
    }

    // ---- READING THE MIXED DISK (side 0, the DD ranges the bus can reach) ----
    {
        withRampDisk(26ull * 128 + 16ull * 512 + 76ull * 2 * 16 * 512);  // 1,256,704
        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "cdos.dsk", false, err), "the mixed CDOS image mounts");

        // Track 0 side 0 is the SD boot track (26x128), so read it single density (no DDEN).
        const uint8_t ctlSd = MAXI | MOTOR | 0x01;  // 8" SD, motor, drive 0

        // Read sector 1 and the last sector (26) of the SD boot track.
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);  // Read Sector
        CHECK(pollRead(b, ctlSd).size() == 128, "track 0 sector 1 is a 128-byte SD sector");

        out(b, FD_SEC, 26);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctlSd).size() == 128, "sector 26 reads too -- all 26 SD sectors are there");
        CHECK((in(b, FD_CMD) & 0x1C) == 0, "clean status: no RNF/CRC/Lost-Data error");

        const uint8_t ctl = MAXI | DDEN | MOTOR | 0x01;  // 8" DD, motor, drive 0

        // Seek to track 1 (still DD 16x512 on side 0) and read its sector 1.
        seekTo(b, c, ctl, 1);
        CHECK(in(b, FD_TRK) == 1, "the track register followed the head to track 1");
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctl).size() == 512, "track 1 sector 1 is a 512-byte DD sector");
    }

    // ---- PORT-04 ¬RESTORE HOMES THE HEAD (the CDOS warm-boot path) ----
    // CDOS.COM homes the head through port 04 D3 on disk selection, not a WD Restore: after the
    // cold loader leaves the head at track 2, CDOS reloads the track register to 0 (no seek) and
    // reads the directory. Without D3 that read faults Record Not Found -- the head is still at
    // track 2, so no ID field matches the register; with it, the head is home and the read finds
    // its record. This is exactly what the CDOS acceptance boot exercises (reference §5, and the
    // CDOS manual: "Disk selection also restores the disk drive head to home, track 0").
    {
        withRampDisk(26ull * 128 + 16ull * 512 + 76ull * 2 * 16 * 512);  // 1,256,704
        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        b.power();
        std::string err;
        CHECK(b.mount("drive0", "cdos.dsk", false, err), "the mixed CDOS image mounts");

        const uint8_t ctlSd = MAXI | MOTOR | 0x01;         // 8" SD, motor, drive 0
        const uint8_t ctlDd = MAXI | DDEN | MOTOR | 0x01;  // 8" DD, motor, drive 0

        // Leave the head at track 2, exactly as the cold loader does when it loads CDOS.COM.
        seekTo(b, c, ctlDd, 2);

        // Reload the track register to 0 with no seek (CDOS's warm-boot read) and read track 0
        // sector 1: the head is still at track 2, so it is Record Not Found and no bytes return.
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctlSd).empty(), "head at track 2, register 0 -> the track-0 read finds nothing");
        CHECK((in(b, FD_CMD) & 0x10) != 0, "...and the status is Record Not Found");

        // Assert port-04 ¬RESTORE (D3 low): the selected drive homes to track 0.
        out(b, AUX, AUX_RESTORE);

        // The same read now finds its record -- a full 128-byte SD sector, no RNF.
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctlSd).size() == 128, "after ¬RESTORE the head is home, so track 0 sector 1 reads");
        CHECK((in(b, FD_CMD) & 0x10) == 0, "no Record Not Found once the head is home");
    }

    // ---- THE 64FDC DROPS ¬RESTORE (reference §5: D3 not assigned) ----
    // Its simpler PerSci 299B has no restore line; drivers home with the 1793's own Restore
    // command instead. So port 04 D3 moves no head, and the same warm-boot sequence still faults.
    {
        withRampDisk(26ull * 128 + 16ull * 512 + 76ull * 2 * 16 * 512);
        Clock c;
        Fdc64Board b;
        b.attachClock(&c);
        b.power();
        std::string err;
        CHECK(b.mount("drive0", "cdos.dsk", false, err), "the mixed CDOS image mounts on the 64FDC");

        const uint8_t ctlSd = MAXI | MOTOR | 0x01;
        const uint8_t ctlDd = MAXI | DDEN | MOTOR | 0x01;

        seekTo(b, c, ctlDd, 2);
        out(b, AUX, AUX_RESTORE);  // no effect on the 64FDC -- D3 is unassigned
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctlSd).empty(), "64FDC: port-04 D3 is unassigned, so the head stays at track 2");
        CHECK((in(b, FD_CMD) & 0x10) != 0, "...and the track-0 read is Record Not Found");
    }

    // ---- ONE-HOT SELECT, BEHAVIORALLY: only the drive with a disk reads ----
    {
        withRampDisk(26ull * 128 + 16ull * 512 + 76ull * 2 * 16 * 512);
        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        b.power();
        std::string err;
        CHECK(b.mount("drive0", "cdos.dsk", false, err), "only drive 0 has a disk");

        // Select drive 1 (empty): NOT READY, nothing transfers. The drive must be latched
        // BEFORE the command -- OUT 34 selects, THEN Read Sector runs against that drive.
        uint8_t ctl1 = MAXI | MOTOR | 0x02;  // DS2 -> drive 1 (SD, the boot track's density)
        out(b, FD_FLG, (uint8_t)(AUTOWAIT | ctl1));
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctl1).empty(), "the empty selected drive 1 transfers nothing");

        // Reselect drive 0 (DS1): its SD boot track (track 0 side 0, 128-byte sectors) is found.
        uint8_t ctl0 = MAXI | MOTOR | 0x01;  // DS1 -> drive 0
        out(b, FD_FLG, (uint8_t)(AUTOWAIT | ctl0));
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctl0).size() == 128, "reselecting drive 0 finds its disk");
    }

    // ---- THE BLANK FORMATS, SIDE 0, AND THE FILE GROWS (the Write-Track path) ----
    //
    // A 0-byte disk, formatted track by track at 8" DOUBLE density through the real Auto-Wait
    // / data ports, grows to a full side-0 DD disk of 0xE5 and reads back at 16x512. This is
    // the mixed-density Write-Track mechanism; the SECOND side (the SD boot track) needs the
    // side-select register Phase 1 does not have, so the two-sided 1,256,704-byte round-trip
    // is the acceptance test's job (task 5), with the real image in hand.
    {
        MemoryMedia* media = nullptr;
        setMediaResolver([&](const std::string& path, bool ro, std::string&) {
            auto m = std::make_unique<MemoryMedia>(path, std::vector<uint8_t>{}, ro);
            media  = m.get();
            return m;
        });

        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "blank.dsk", false, err), "the blank mounts (0 bytes)");
        CHECK(media && media->size() == 0, "...and starts empty");

        const uint8_t ctl = MAXI | DDEN | MOTOR | 0x01;  // 8" DD, drive 0 -> 500k recording

        // Before FORMAT, every sector is Record Not Found.
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctl).empty(), "an unformatted track transfers nothing");
        CHECK((in(b, FD_CMD) & 0x10) != 0, "...and reads Record Not Found (S4)");

        // Format track 0: latch density+drive, seek there, Write Track, stream the raw track.
        out(b, FD_FLG, (uint8_t)(AUTOWAIT | ctl));
        seekTo(b, c, ctl, 0);
        out(b, FD_CMD, 0xF4);  // Write Track
        pollWrite(b, ctl, ddTrack512(0));
        CHECK((in(b, FD_CMD) & 0x20) == 0, "the format took: no WRITE FAULT (S5)");
        CHECK(media->size() == 16u * 512, "track 0 formatted: the file grew to one DD track (8192)");

        // Read sector 1 back: 512 bytes of 0xE5, clean status.
        out(b, FD_TRK, 0);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        std::vector<uint8_t> got = pollRead(b, ctl);
        bool allE5 = got.size() == 512;
        for (uint8_t v : got) if (v != 0xE5) allE5 = false;
        CHECK(allE5, "the formatted DD sector reads back 512 bytes of 0xE5");
        CHECK((in(b, FD_CMD) & 0x1C) == 0, "...with no RNF/CRC/Lost-Data error");

        // Format the remaining 76 tracks the same way; the file grows one DD track each.
        for (int t = 1; t <= 76; ++t) {
            out(b, FD_FLG, (uint8_t)(AUTOWAIT | ctl));
            seekTo(b, c, ctl, t);
            out(b, FD_CMD, 0xF4);
            pollWrite(b, ctl, ddTrack512(t));
        }
        CHECK(media->size() == 77u * 16 * 512,
              "all 77 tracks formatted: a full 630,784-byte side-0 DD disk");

        // The last track reads back too -- the growth stayed contiguous.
        seekTo(b, c, ctl, 76);
        out(b, FD_SEC, 1);
        out(b, FD_CMD, 0x88);
        CHECK(pollRead(b, ctl).size() == 512, "track 76 sector 1 reads back a full DD sector");
    }

    // ---- THE 5501 CONSOLE IS REACHABLE THROUGH PORTS 00/01 (wiring, not the chip) ----
    {
        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        b.power();

        // Plug a scripted terminal straight onto the embedded UART -- the resolver grammar is
        // the chip test's concern; here we only prove the port decode reaches the chip.
        auto s   = std::make_unique<ScriptedStream>();
        auto* tty = s.get();
        b.uart().connect(std::move(s));
        out(b, SER_ST, 0xC0);  // 9600 baud, one stop bit (writeBaud through port 00)

        // Receive: a typed character shows RDA on the status port and yields on the data port.
        tty->feed("A");
        CHECK((in(b, SER_ST) & 0x40) != 0, "RDA set on port 00 once a character arrives");
        CHECK(in(b, SER_DT) == 'A', "port 01 yields the received byte");
        CHECK((in(b, SER_ST) & 0x40) == 0, "...and reading it clears RDA");

        // Transmit: a byte written to the data port leaves down the line.
        out(b, SER_DT, 'X');
        c.advance(100000);      // well past one 9600-baud character time
        (void)in(b, SER_ST);    // a status read pumps the transmitter to its deadline
        CHECK(tty->out() == "X", "a byte written to port 01 goes out the line");
    }

    // ---- THE RDOS BOOT PROM: OUT 40H banks it out, RESET re-arms it (16FDC, 4K) ----
    {
        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        b.power();

        // What the ROM window should read: decode builtin:rdos252 the same way the board does.
        const BuiltinRom* rp = findRom("rdos252");
        CHECK(rp != nullptr, "builtin:rdos252 is compiled in");
        Image img;
        std::string e;
        CHECK(rp && decodeRom(*rp, 0xC000, img, e), "rdos252 decodes");
        auto flat = img.flat();  // lo() == 0xC000

        // Armed at power: the 4K window C000-CFFF answers memory reads, and nothing else does.
        CHECK(b.romArmed(), "the ROM is armed at power");
        CHECK(decodesMem(b, 0xC000) && decodesMem(b, 0xCFFF), "the 4K window C000-CFFF decodes");
        CHECK(!decodesMem(b, 0xBFFF), "BFFF (just below) does not");
        CHECK(!decodesMem(b, 0xD000), "D000 (just above the 4K part) does not -- 16FDC is 4K");
        CHECK(readMem(b, 0xC000) == flat[0], "C000 reads the PROM's first byte");
        CHECK(readMem(b, 0xCFFF) == flat[0x0FFF], "CFFF reads the PROM's last byte");

        // PHANTOM*: the armed PROM shadows a RAM card underneath over the C000 window, on
        // READS ONLY -- so a 64K machine (RAM at C000-FFFF) does not contend, yet a write
        // still falls through to the RAM beneath (CDOS relocates into it and it survives the
        // bank-out). Same window as the decode; nothing outside it is shadowed.
        CHECK(phantomsMem(b, 0xC000) && phantomsMem(b, 0xCFFF),
              "the armed PROM pulls PHANTOM* over its C000-CFFF read window");
        CHECK(!phantomsMem(b, 0xBFFF) && !phantomsMem(b, 0xD000),
              "...and only there -- BFFF and D000 are not shadowed");
        CHECK(!phantomsMem(b, 0xC000, Cycle::MemWrite),
              "writes are NOT shadowed -- the RAM under the ROM keeps them");

        // OUT 40H (any byte) banks the ROM out until RESET.
        out(b, BANK, 0x00);
        CHECK(!b.romArmed(), "OUT 40H disarms the ROM");
        CHECK(!decodesMem(b, 0xC000), "...so the C000 window no longer decodes (RAM shows through)");
        CHECK(!phantomsMem(b, 0xC000), "...and the shadow drops with it -- RAM is exposed at C000");

        // RESET* (the front-panel button) re-arms it -- how you boot the machine again.
        b.reset(Reset::Bus);
        CHECK(b.romArmed(), "RESET re-arms the boot PROM");
        CHECK(decodesMem(b, 0xC000) && readMem(b, 0xC000) == flat[0], "...and it answers C000 again");
    }

    // ---- THE 64FDC's PROM IS 8K (C000-DFFF), and it too banks out / re-arms ----
    {
        Clock c;
        Fdc64Board b;
        b.attachClock(&c);
        b.power();

        const BuiltinRom* rp = findRom("rdos312");
        CHECK(rp != nullptr, "builtin:rdos312 is compiled in");
        Image img;
        std::string e;
        CHECK(rp && decodeRom(*rp, 0xC000, img, e), "rdos312 decodes");
        auto flat = img.flat();

        // The whole 8K window decodes -- D000, which the 16FDC does NOT map, is in range here.
        CHECK(decodesMem(b, 0xC000) && decodesMem(b, 0xD000) && decodesMem(b, 0xDFFF),
              "the 8K window C000-DFFF decodes (D000 included -- RDOS 3.12 grew into it)");
        CHECK(!decodesMem(b, 0xE000), "E000 (just above the 8K part) does not");
        CHECK(readMem(b, 0xDFFF) == flat[flat.size() - 1], "DFFF reads the PROM's last byte");

        out(b, BANK, 0x00);
        CHECK(!decodesMem(b, 0xD000), "OUT 40H banks out the whole 8K window");
        b.reset(Reset::Bus);
        CHECK(decodesMem(b, 0xD000), "RESET re-arms the whole 8K window");
    }

    // ---- bootstrap = off: the ROM does not map, and ¬BOOT reads HIGH ----
    {
        Clock c;
        Fdc16Board b;
        b.attachClock(&c);
        std::string err;
        CHECK(setProperty(b, "bootstrap", "off", err), "the BOOT/MON strap turns off");
        b.power();

        CHECK(!decodesMem(b, 0xC000), "with the strap off, the ROM window does not decode");
        CHECK((in(b, FD_FLG) & F_NBOOT) != 0, "...and ¬BOOT (port 34 D6) reads HIGH");

        // With the strap ON, ¬BOOT reads low (the auto-boot condition).
        Fdc16Board on;
        on.attachClock(&c);
        on.power();
        CHECK((in(on, FD_FLG) & F_NBOOT) == 0, "with the strap on, ¬BOOT reads LOW");
    }

    // ---- A REAL BUS: the ROM answers C000 alongside 48K of RAM, then banks out ----
    {
        // Clock BEFORE the Bus: the Bus owns and deletes the board, whose dtor cancels its
        // wake on the Clock -- so the Clock must outlive the Bus (Windows UAF otherwise).
        Clock c;
        Bus bus;
        bus.setVerify(true);  // re-derive every decode the slow way
        auto* fdc = new Fdc16Board();
        fdc->id = "fdc";
        fdc->attachClock(&c);
        auto* mem = ram48k("mem0");
        bus.attach(fdc);
        bus.attach(mem);
        fdc->power();

        const BuiltinRom* rp = findRom("rdos252");
        Image img;
        std::string e;
        decodeRom(*rp, 0xC000, img, e);
        auto flat = img.flat();

        CHECK(bus.memRead(0xC000) == flat[0], "through the bus, C000 reads the PROM");
        CHECK(bus.memRead(0x0000) == 0x00, "low memory is ordinary RAM (zero)");
        bus.memWrite(0x0005, 0x5A);
        CHECK(bus.memRead(0x0005) == 0x5A, "...read/write RAM");
        CHECK(bus.drain().empty(), "no bus contention: the ROM sits in a hole above RAM");

        bus.ioWrite(0x40, 0x00);  // bank the ROM out
        CHECK(bus.memRead(0xC000) == 0xFF, "banked out, C000 floats (no RAM there) -- 0xFF");
        CHECK(bus.drain().empty(), "...and the released ROM stops driving, so still no contention");

        delete fdc;
        delete mem;
    }
}
