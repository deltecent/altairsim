// Tarbell #1011 (SD) and #2022 (DD) floppy controllers (docs/boards/tarbell-sd.md,
// docs/boards/tarbelldd.md).
//
// The acceptance tests boot CP/M off the tracked disks end to end. This file pins the
// board-level behavior the boot cannot see: the POLLED-DRQ wait-synced transfer (the
// one path test_versafloppy never exercised -- the Tarbell PROM polls the WAIT port
// between bytes, where the VersaFloppy stalls on PRDY), the SD function-decoder vs DD
// bitmap drive select, the SD-uniform / DD-mixed geometry probes, the DD port-FD
// DMA-busy register, and the full boot-PROM / PHANTOM* contract.

#include "boards/s100-memory.h"
#include "boards/tarbell.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/debug.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/diskfaults.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

// Ports, base F8. COMMAND IS AT OFFSET 0 (unlike the VersaFloppy's offset 4).
constexpr uint8_t P    = 0xF8;
constexpr uint8_t CMD  = P + 0;  // F8 command / status
constexpr uint8_t TRK  = P + 1;  // F9 track
constexpr uint8_t SECR = P + 2;  // FA sector
constexpr uint8_t DAT  = P + 3;  // FB data (wait-synced)
constexpr uint8_t CTL  = P + 4;  // FC control (out) / WAIT (in)
constexpr uint8_t EXT  = P + 5;  // FD ext-addr (out) / DMA-busy (in)  -- DD only

// The on-card 8257 DMA controller's register block base (DD only). SD2DD straps it E0,
// which is the board's default. ADR0 = E0, WCT0 = E1, CMND/status = E8.
constexpr uint8_t dmaBase = 0xE0;

uint8_t in(TarbellBoardBase& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(TarbellBoardBase& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// A disk whose byte i holds (i + i/128) & 0xFF -- each 128-byte sector gets its own
// linear offset stamp, so sector N reads (N, N+1, ...) and no two sectors look alike
// (a plain i&0xFF ramp repeats every 256 bytes, and adjacent 128-byte sectors would be
// indistinguishable).
void withRampDisk(uint64_t bytes) {
    setMediaResolver([bytes](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d((size_t)bytes);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)((i + i / 128) & 0xFF);
        return std::make_unique<MemoryMedia>(path, std::move(d), ro);
    });
}

// The RAM the boot PROM shadows: 64K strapped honors_phantom = read, so it stands down
// for READS while the PROM shadows and keeps answering WRITES -- which is how the
// bootstrap's sector lands in the RAM under the PROM. (The same fixture as test_phantom.)
MemoryBoard* ram64k(const char* id) {
    auto* m = new MemoryBoard();
    m->id = id;
    std::string err;
    setProperty(*m, "fill", "zero", err);
    setProperty(*m, "honors_phantom", "read", err);
    Region r;
    r.kind = RegionKind::Ram;
    r.at   = 0x0000;
    r.size = 0x10000;
    m->addRegion(r, err);
    m->power();
    return m;
}

// Read one sector the way the Tarbell PROM does: poll the WAIT port (FC) for DRQ, read
// a data byte (FB), repeat until the WAIT port shows INTRQ (bit7 clear). Returns what
// came off the data port. A hard cap keeps a stuck DRQ from hanging the test.
std::vector<uint8_t> pollRead(TarbellBoardBase& b) {
    std::vector<uint8_t> got;
    for (int guard = 0; guard < 4096; ++guard) {
        if ((in(b, CTL) & 0x80) == 0) break;  // INTRQ: the transfer finished
        got.push_back(in(b, DAT));
    }
    return got;
}

// Stream a raw track the way FORMAT.ASM does: poll the WAIT port (FC) for DRQ, write a byte
// (FB), repeat -- and once the structured stream runs out, keep padding with gap 0xFFs until
// the controller signals INTRQ (bit7 clear). That is exactly the ENDTRK loop: the guest does
// not know the revolution length, it just fills until the wait-synced command completes. The
// caller must have issued the Write Track command (0xF4) first.
void pollWrite(TarbellBoardBase& b, const std::vector<uint8_t>& stream) {
    size_t k = 0;
    for (int guard = 0; guard < 20000; ++guard) {
        if ((in(b, CTL) & 0x80) == 0) break;  // INTRQ: the track is done
        out(b, DAT, k < stream.size() ? stream[k++] : 0xFF);
    }
}

// A well-formed IBM-3740 SSSD track image: 26 sectors of 128 0xE5 bytes, each wrapped in an
// ID field (0xFE track 0 sector N=0 -> 128) and a data field (0xFB ... 0xF7), with gaps. Only
// the marks, the header and the 0xF7 terminators matter to the format parser; the gaps are the
// slack pollWrite would otherwise have to invent. `trackNum` goes in the (ignored) ID header.
std::vector<uint8_t> sssdTrack(int trackNum) {
    std::vector<uint8_t> s;
    auto put = [&](uint8_t b, int n) { for (int i = 0; i < n; ++i) s.push_back(b); };
    put(0xFF, 40); put(0x00, 6); s.push_back(0xFC);   // pre-index gap + index mark
    for (int sec = 1; sec <= 26; ++sec) {
        put(0xFF, 26); put(0x00, 6);
        s.push_back(0xFE);                            // ID address mark
        s.push_back((uint8_t)trackNum);               // track (ignored: head position wins)
        s.push_back(0x00);                            // side
        s.push_back((uint8_t)sec);                    // sector
        s.push_back(0x00);                            // length code N=0 -> 128 bytes
        s.push_back(0xF7);                            // ID CRC
        put(0xFF, 11); put(0x00, 6);
        s.push_back(0xFB);                            // data address mark
        put(0xE5, 128);                               // the 128 payload bytes
        s.push_back(0xF7);                            // data CRC
    }
    return s;  // ~3900 bytes of structure; pollWrite pads the rest of the ~5208-byte revolution
}

// A well-formed DD track image the way DFORMAT lays one down: 51 sectors of 128 0xE5 bytes,
// MFM gap byte 0x4E, 3x 0xF5 (the A1 sync bytes) before each address mark, no index mark, per
// sector `FE trk 00 sec 00 F7 ... FB (128xE5) F7`. Sector numbers are sequential (skew is
// irrelevant -- the image has no IDs; Write Track writes the data fields in order). The format
// parser fall through the 0xF5/0x4E gap bytes and terminates data fields at 0xF7, so it is
// density- and size-agnostic. `trackNum` goes in the (ignored) ID header.
std::vector<uint8_t> ddTrack(int trackNum) {
    std::vector<uint8_t> s;
    auto put = [&](uint8_t b, int n) { for (int i = 0; i < n; ++i) s.push_back(b); };
    put(0x4E, 40);                                    // pre-index / lead-in gap (no index mark)
    for (int sec = 1; sec <= 51; ++sec) {
        put(0x4E, 12); put(0x00, 8); put(0xF5, 3);    // gap + sync field + A1 marks
        s.push_back(0xFE);                            // ID address mark
        s.push_back((uint8_t)trackNum);               // track (ignored: head position wins)
        s.push_back(0x00);                            // side
        s.push_back((uint8_t)sec);                    // sector
        s.push_back(0x00);                            // length code N=0 -> 128 bytes
        s.push_back(0xF7);                            // ID CRC
        put(0x4E, 12); put(0x00, 8); put(0xF5, 3);    // gap + sync + A1 marks
        s.push_back(0xFB);                            // data address mark
        put(0xE5, 128);                               // the 128 payload bytes
        s.push_back(0xF7);                            // data CRC
    }
    // ~9300 bytes of structure (51 sectors x ~182), which MUST stay under the ~10416-byte DD
    // revolution budget so all 51 sectors stream before the wait-synced command completes;
    // pollWrite pads the remainder with gap 0xFFs.
    return s;
}

// `timing = real` (issue #637): a whole machine -- 64K RAM, the card with its boot PROM off,
// an 8080 at 2 MHz -- because a READY hold is charged by the run loop, at the instruction
// boundary, and nowhere else.
struct TimedRig {
    Machine            m;
    TarbellBoardBase*  fdc = nullptr;
    TimedRig(const char* type, const char* timing, const std::vector<uint8_t>& code) {
        std::string err;
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0x0000;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        fdc = dynamic_cast<TarbellBoardBase*>(m.add(type, "fdc0", err));
        setProperty(*fdc, "bootstrap", "off", err);
        setProperty(*fdc, "timing", timing, err);
        Board* cpu = m.add("8080", "cpu0", err);
        setProperty(*cpu, "clock_hz", "2000000", err);
        m.power();
        fdc->mount("drive0", "ramp.dsk", false, err);
        for (size_t i = 0; i < code.size(); ++i) m.bus.memWrite((uint16_t)i, code[i]);
        m.cpu()->setPc(0);
    }
    // The T-states one instruction took, READY holds included.
    uint64_t step() {
        uint64_t t0 = m.clock.now();
        m.debug.run(1);
        return m.clock.now() - t0;
    }
};

} // namespace

void test_tarbell() {
    SECTION("boards/tarbell: the Tarbell single- and double-density controllers");

    // ---- THE POLLED-DRQ WAIT-SYNCED READ (the Tarbell PROM's RLOOP) ----
    {
        withRampDisk(77ull * 26 * 128);  // 8" SD, 256,256 bytes
        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "ramp.dsk", false, err), "the SD image mounts");

        // Default strap selects drive 0, so a single-drive card never writes FC. Head is
        // at track 0 after power; point the FDC at track 0, sector 1.
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);  // FD1771 Read Sector

        std::vector<uint8_t> got = pollRead(b);
        CHECK(got.size() == 128, "a whole 128-byte sector arrives through the polled-DRQ loop");
        bool ramp = true;
        for (int i = 0; i < 128; ++i) if (got[(size_t)i] != (uint8_t)i) ramp = false;
        CHECK(ramp, "...and the bytes are track 0 sector 1's, in order");
        CHECK((in(b, CMD) & 0x01) == 0, "BUSY is clear -- the command completed");
    }

    // ---- READ A DIFFERENT SECTOR after a seek ----
    {
        withRampDisk(77ull * 26 * 128);
        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "ramp.dsk", false, err);

        out(b, DAT, 1);     // seek target in the data register
        out(b, CMD, 0x18);  // Seek, no verify
        CHECK((in(b, CMD) & 0x01) == 0, "the seek completes at once under the wait-sync");
        CHECK(in(b, TRK) == 1, "the track register followed the head to track 1");

        out(b, SECR, 3);
        out(b, CMD, 0x88);
        std::vector<uint8_t> got = pollRead(b);
        // track 1 sector 3 -> linear sector 1*26 + (3-1) = 28; stamped (28, 29, 30, ...).
        CHECK(got.size() == 128, "a full sector after the seek");
        CHECK(got[0] == 28, "read lands on the right (track, sector)");
        CHECK(got[5] == 33, "...and streams that sector's bytes in order");
    }

    // ---- READS ACROSS THE DISK after seeks (the CBIOS directory-access pattern) ----
    {
        withRampDisk(77ull * 26 * 128);
        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "ramp.dsk", false, err);
        for (int t : {0, 2, 34, 76}) {
            out(b, DAT, (uint8_t)t);
            out(b, CMD, 0x18);  // Seek to track t
            CHECK(in(b, TRK) == t, "the seek reached the track");
            out(b, SECR, 1);
            out(b, CMD, 0x88);  // Read Sector 1
            std::vector<uint8_t> got = pollRead(b);
            CHECK(got.size() == 128, "a full sector on the seeked track");
            CHECK((in(b, CMD) & 0x1C) == 0, "status is clean: no RNF/CRC/Lost-Data error");
            CHECK(got[0] == (uint8_t)((t * 26 * 128 + t * 26) & 0xFF),
                  "the seeked track's sector 1 data");
        }
    }

    // ---- THE SD FUNCTION DECODER: OUT FC picks the drive ----
    {
        withRampDisk(77ull * 26 * 128);
        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "ramp.dsk", false, err);  // only drive 0 has a disk

        // The SELECT idiom loads ~drive into D5:D4 (the CBIOS does CMA first), so drive 1
        // is 0xE2 and drive 0 is 0xF2 -- both with low bits 010 to strobe the latch.
        out(b, CTL, 0xE2);  // select drive 1 (empty)
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).empty(), "an empty selected drive is NOT READY -- no bytes transfer");

        out(b, CTL, 0xF2);  // select drive 0 again (~0 = 11 in D5:D4)
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).size() == 128, "reselecting drive 0 finds its disk again");

        // A non-select function (001, the step-out pulse) does NOT move the selection.
        out(b, CTL, 0x01);
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).size() == 128, "a step-out pulse leaves the drive selected");
    }

    // ---- THE GEOMETRY PROBE: SD is 256,256 and nothing else ----
    {
        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();

        withRampDisk(77ull * 26 * 128);
        std::string err;
        CHECK(b.mount("drive0", "sd.dsk", false, err), "the 256,256-byte SD image mounts");

        withRampDisk(499456);  // OVERSIZED for SD -- larger than one revolution per track
        err.clear();
        CHECK(!b.mount("drive1", "dd.dsk", false, err),
              "an oversized image is refused by the SD card");
        CHECK(err.find("single-density") != std::string::npos, "...with a reason that says why");
    }

    // ---- A BLANK / SHORT IMAGE MOUNTS (unformatted), and RNFs every sector until FORMAT ----
    //
    // MOUNT ... CREATE makes a 0-byte file. On a soft-sector card that used to hard-fail the
    // probe; now anything smaller than the full disk mounts with EMPTY per-track geometry --
    // READY and steppable, but every access is Record Not Found until Write Track lays a track
    // down. (Oversized is still an error; that is the block above.)
    {
        withRampDisk(0);  // a 0-byte CREATE'd blank
        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "blank.dsk", false, err), "a 0-byte blank image mounts");

        // Every sector RNFs: no bytes transfer and status shows Record Not Found (S4).
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);  // Read Sector
        CHECK(pollRead(b).empty(), "an unformatted track transfers nothing");
        CHECK((in(b, CMD) & 0x10) != 0, "...and reads Record Not Found (S4)");

        // A short-but-nonzero image mounts the same way.
        withRampDisk(1024);
        err.clear();
        CHECK(b.mount("drive1", "short.dsk", false, err), "a short (1 KB) image mounts unformatted");
    }

    // ---- THE GUEST FORMATS A BLANK DISK: Write Track fills it, and it GROWS ----
    //
    // The whole point of the feature. A 0-byte disk, formatted track by track through the real
    // FC WAIT / FB data ports (the FORMAT.ASM discipline), grows to a usable 256,256-byte SSSD
    // disk of 0xE5 -- the emulated controller doing the format, exactly as real hardware does.
    {
        // A 0-byte MemoryMedia we keep a handle on, so we can watch the host file grow.
        MemoryMedia* media = nullptr;
        setMediaResolver([&](const std::string& path, bool ro, std::string&) {
            auto m = std::make_unique<MemoryMedia>(path, std::vector<uint8_t>{}, ro);
            media  = m.get();
            return m;
        });

        Clock c;
        TarbellBoard b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "blank.dsk", false, err), "the blank mounts (0 bytes)");
        CHECK(media && media->size() == 0, "...and starts empty");

        // Format track 0: seek there, issue Write Track, stream the raw track + gap padding.
        out(b, DAT, 0);
        out(b, CMD, 0x18);   // Seek to track 0 (head load)
        out(b, CMD, 0xF4);   // Write Track
        pollWrite(b, sssdTrack(0));
        CHECK((in(b, CMD) & 0x20) == 0, "the format took: no WRITE FAULT (S5)");
        CHECK(media->size() == 26u * 128, "track 0 formatted: the file grew to one track (3328)");

        // Read sector 1 of the freshly formatted track: 128 bytes of 0xE5, clean status.
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);   // Read Sector
        std::vector<uint8_t> got = pollRead(b);
        CHECK(got.size() == 128, "the formatted sector reads back a full 128-byte sector");
        bool allE5 = got.size() == 128;
        for (uint8_t v : got) if (v != 0xE5) allE5 = false;
        CHECK(allE5, "...and it is the 0xE5 fill the formatter wrote");
        CHECK((in(b, CMD) & 0x1C) == 0, "...with no RNF/CRC/Lost-Data error");

        // Format the remaining 76 tracks the same way. The file grows one track per format.
        for (int t = 1; t <= 76; ++t) {
            out(b, DAT, (uint8_t)t);
            out(b, CMD, 0x18);   // Seek to track t
            out(b, CMD, 0xF4);   // Write Track
            pollWrite(b, sssdTrack(t));
        }
        CHECK(media->size() == 77u * 26 * 128,
              "all 77 tracks formatted: the file is a full 256,256-byte SSSD disk");
    }

    // ---- THE DD CARD FORMATS A BLANK INTO A MIXED-DENSITY DISK (what DFORMAT does) ----
    //
    // The payoff for the double-density card. A 0-byte blank, formatted through the real ports:
    // track 0 SINGLE density (OUT-FC density bit clear -> chip at 250k) and tracks 1-76 DOUBLE
    // density (OUT-FC density bit set -> chip at 500k). Each track's recorded density comes from
    // the chip's data rate at Write Track time, so the file grows into a valid 499,456-byte mixed
    // image (SD track 0 = 3328, +6528 per DD track) and reads back 0xE5 at the right geometry.
    {
        MemoryMedia* media = nullptr;
        setMediaResolver([&](const std::string& path, bool ro, std::string&) {
            auto m = std::make_unique<MemoryMedia>(path, std::vector<uint8_t>{}, ro);
            media  = m.get();
            return m;
        });

        Clock c;
        TarbellDdBoard b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "blank.dsk", false, err), "the blank mounts on the DD card (0 bytes)");
        CHECK(media && media->size() == 0, "...and starts empty");

        // Track 0: SINGLE density. OUT-FC = 0x00 (density bit clear, drive 0, side 0) -> 250k.
        out(b, CTL, 0x00);
        out(b, DAT, 0);
        out(b, CMD, 0x18);   // Seek to track 0
        out(b, CMD, 0xF4);   // Write Track
        pollWrite(b, sssdTrack(0));
        CHECK((in(b, CMD) & 0x20) == 0, "track 0 formats: no WRITE FAULT (S5)");
        CHECK(media->size() == 26u * 128, "track 0 (SD) grew the file to 3328");

        // Tracks 1-76: DOUBLE density. OUT-FC = 0x08 (density bit set) -> 500k, 51 sectors each.
        out(b, CTL, 0x08);
        for (int t = 1; t <= 76; ++t) {
            out(b, DAT, (uint8_t)t);
            out(b, CMD, 0x18);   // Seek to track t
            out(b, CMD, 0xF4);   // Write Track
            pollWrite(b, ddTrack(t));
        }
        CHECK(media->size() == 26u * 128 + 76u * 51 * 128,
              "all 77 tracks formatted: a full 499,456-byte mixed-density disk");

        // Read back: track 0 is an SD 128-byte sector of 0xE5, track 1 a DD one, both clean.
        // The head is on track 76 after the format loop, so seek it home first.
        out(b, CTL, 0x00);   // density SD, drive 0
        out(b, DAT, 0);
        out(b, CMD, 0x18);   // Seek to track 0
        out(b, SECR, 1);
        out(b, CMD, 0x88);   // Read Sector
        std::vector<uint8_t> t0 = pollRead(b);
        bool t0e5 = t0.size() == 128;
        for (uint8_t v : t0) if (v != 0xE5) t0e5 = false;
        CHECK(t0e5, "SD track 0 sector 1 reads back 128 bytes of 0xE5");
        CHECK((in(b, CMD) & 0x1C) == 0, "...with no RNF/CRC/Lost-Data error");

        out(b, CTL, 0x08);   // density DD, drive 0
        out(b, DAT, 1);
        out(b, CMD, 0x18);   // Seek to track 1 (DD)
        out(b, SECR, 51);    // the last DD sector -- proves all 51 landed
        out(b, CMD, 0x88);   // Read Sector
        std::vector<uint8_t> t1 = pollRead(b);
        bool t1e5 = t1.size() == 128;
        for (uint8_t v : t1) if (v != 0xE5) t1e5 = false;
        CHECK(t1e5, "DD track 1 sector 51 reads back 128 bytes of 0xE5");
        CHECK((in(b, CMD) & 0x1C) == 0, "...with no RNF/CRC/Lost-Data error");
    }

    // ---- THE DOUBLE-DENSITY CARD: bitmap select, mixed geometry, port FD ----
    {
        Clock c;
        TarbellDdBoard b;
        b.attachClock(&c);
        b.power();

        // The mixed-density image: SD track 0 (26x128) + DD tracks 1-76 (51x128).
        withRampDisk(26ull * 128 + 76ull * 51 * 128);  // 499,456
        std::string err;
        CHECK(b.mount("drive0", "dd.dsk", false, err), "the mixed-density DD image mounts");

        // Read track 0 sector 1 -- the SINGLE-density track, exactly the boot path.
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        std::vector<uint8_t> t0 = pollRead(b);
        CHECK(t0.size() == 128, "track 0 (SD) reads a 128-byte sector on the DD card");
        CHECK(t0[0] == 0, "...and it is track 0 sector 1");

        // OUT FC is a plain bitmap latch on the DD card: bit3 density, D5:D4 drive, D6 side.
        // Select drive 1 (empty) with DD density set: bits = 0x18 (density) | 0x10 (drive 1).
        out(b, CTL, 0x18 | 0x10);
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).empty(), "the DD bitmap latch selected the empty drive 1");
        out(b, CTL, 0x00);  // density SD (track 0 is single density), drive 0, side 0
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).size() == 128, "...and clearing D5:D4 reselected drive 0");

        // Port FD IN is the DMA-busy check: bit7 follows INTRQ, 0 once the command is over.
        CHECK((in(b, EXT) & 0x80) == 0, "port FD reports the finished read complete (bit7 = 0)");
        (void)in(b, CMD);
        CHECK((in(b, EXT) & 0x80) != 0, "...and busy again once a status read resets INTRQ");

        // EVERY sector of the SD track 0 (26) and a DD track (51) delivers exactly 128
        // bytes and completes -- a mixed-density boot reads straight down track 0. The
        // density bit follows the track, as the CBIOS sets it: clear for track 0, set after.
        out(b, CTL, 0x00);  // density SD, drive 0
        bool t0ok = true;
        for (int s = 1; s <= 26; ++s) {
            out(b, TRK, 0);
            out(b, SECR, (uint8_t)s);
            out(b, CMD, 0x88);
            std::vector<uint8_t> got = pollRead(b);
            if (got.size() != 128 || (in(b, CMD) & 0x1C) != 0) t0ok = false;
        }
        CHECK(t0ok, "all 26 SD sectors of track 0 read exactly 128 bytes and complete");

        out(b, CTL, 0x08);  // density DD, drive 0
        out(b, DAT, 1);
        out(b, CMD, 0x18);  // seek to track 1 (DD)
        bool t1ok = true;
        for (int s = 1; s <= 51; ++s) {
            out(b, SECR, (uint8_t)s);
            out(b, CMD, 0x88);
            std::vector<uint8_t> got = pollRead(b);
            if (got.size() != 128 || (in(b, CMD) & 0x1C) != 0) t1ok = false;
        }
        CHECK(t1ok, "all 51 DD sectors of track 1 read exactly 128 bytes and complete");

        // THE WRONG DENSITY READS NOTHING (issue #692). An FD1791 clocked for one density
        // finds no ID field recorded at the other: Record Not Found (S4), and no data.
        out(b, CTL, 0x00);  // density SD -- but the head is on track 1, which is DD
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).empty(), "a DD track read with the density bit clear gives no data");
        CHECK((in(b, CMD) & 0x10) != 0, "...and Record Not Found");

        out(b, CTL, 0x08);  // density DD
        out(b, DAT, 0);
        out(b, CMD, 0x18);  // seek to track 0 (SD)
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).empty(), "the SD track 0 read with the density bit set gives no data");
        CHECK((in(b, CMD) & 0x10) != 0, "...and Record Not Found");

        out(b, CTL, 0x00);  // density SD: the same sector is there again
        out(b, CMD, 0x88);
        CHECK(pollRead(b).size() == 128 && (in(b, CMD) & 0x1C) == 0,
              "...and with the bit right for the track the sector reads clean");
    }

    // ---- THE ON-CARD 8257 STEALS THE BUS: a DMA sector read (DESIGN.md 4.5) ----
    //
    // The payoff for the whole card being a bus master. With a channel programmed the
    // RWDMA way (2abios48.asm) and a WD1791 Read Sector issued, the card pulls pHOLD and
    // the run loop drives the on-card 8257 to move the sector into RAM -- 128 stolen
    // cycles the CPU never executes but the clock is charged for. Byte for byte it matches
    // what the polled-DRQ loop reads above, but no IN FB ever touches the data port: the
    // guest programs the transfer and HLTs, and the sector is simply there when it wakes.
    {
        withRampDisk(26ull * 128 + 76ull * 51 * 128);  // the mixed DD disk (499,456)

        Machine m;
        m.bus.setVerify(true);  // re-derive the decode AND the pHOLD wire every instruction
        std::string err;

        Board* mb  = m.add("memory", "mem0", err);
        auto*  mem = dynamic_cast<MemoryBoard*>(mb);
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0x0000;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();

        Board* fb  = m.add("tarbelldd", "fdc0", err);
        auto*  fdc = dynamic_cast<TarbellDdBoard*>(fb);
        // Plain controller for this test: with the boot PROM off, low RAM is ordinary
        // read/write and nothing shadows the program or the DMA target.
        setProperty(*fdc, "bootstrap", "off", err);
        fdc->power();
        CHECK(fdc->mount("drive0", "dd.dsk", false, err), "the mixed DD disk mounts on the DMA card");

        m.add("8080", "cpu0", err);
        m.cpu()->reset(Reset::PowerOn);

        // The RWDMA program: reset the 8257 flip-flop, load count low/high (0x7F, 0x40 =
        // a 128-byte READ into memory), load address low/high (0x2C00), enable channel 0
        // with TC-STOP, point the FD1791 at track 0 sector 1, issue Read Sector, HLT. The
        // burst drains at the instruction boundary right after the Read Sector OUT.
        const uint8_t ADR0 = dmaBase + 0, WCT0 = dmaBase + 1, CMND = dmaBase + 8;
        uint16_t pc = 0;
        auto emit = [&](std::initializer_list<uint8_t> bytes) {
            for (uint8_t x : bytes) m.bus.memWrite(pc++, x);
        };
        emit({0xAF, 0xD3, CMND});         // XRA A ; OUT CMND   -- reset the first/last flip-flop
        emit({0x3E, 0x7F, 0xD3, WCT0});   // MVI A,7F ; OUT WCT0 -- count low (128 - 1)
        emit({0x3E, 0x40, 0xD3, WCT0});   // MVI A,40 ; OUT WCT0 -- count high + mode 01 (read)
        emit({0x3E, 0x00, 0xD3, ADR0});   // MVI A,00 ; OUT ADR0 -- address low
        emit({0x3E, 0x2C, 0xD3, ADR0});   // MVI A,2C ; OUT ADR0 -- address high (-> 0x2C00)
        emit({0x3E, 0x41, 0xD3, CMND});   // MVI A,41 ; OUT CMND -- enable ch0 + TC-STOP
        emit({0x3E, 0x00, 0xD3, TRK});    // MVI A,00 ; OUT F9   -- track 0
        emit({0x3E, 0x01, 0xD3, SECR});   // MVI A,01 ; OUT FA   -- sector 1
        emit({0x3E, 0x88, 0xD3, CMD});    // MVI A,88 ; OUT F8   -- FD1791 Read Sector
        emit({0x76});                     // HLT

        m.cpu()->setPc(0);
        uint64_t before = m.clock.now();
        RunResult rr = m.debug.run(0);

        CHECK(rr.why == StopReason::Halted, "the CPU reaches its HLT -- DMA did not derail it");

        // Track 0 sector 1 of the ramp disk is bytes 0..127, stamped (i + i/128) = i. The
        // whole sector must be sitting at 0x2C00 with nothing spilled on either side.
        bool landed = true;
        for (int i = 0; i < 128; ++i)
            if (m.bus.memRead((uint16_t)(0x2C00 + i)) != (uint8_t)i) landed = false;
        CHECK(landed, "the whole 128-byte sector was DMA'd into RAM at 0x2C00");
        CHECK(m.bus.memRead(0x2BFF) == 0 && m.bus.memRead(0x2C80) == 0,
              "...and nothing spilled outside the 128-byte window");

        // The CPU's own StepResult counts only its instructions; the clock is charged 128
        // stolen bytes x 4 T-states = 512 more, which is the CPU genuinely losing the bus.
        CHECK(m.clock.now() - before == rr.tStates + 512,
              "exactly 512 stolen DMA T-states were charged on top of the CPU's own");
        CHECK((in(*fdc, CMD) & 0x01) == 0, "the FD1791 command completed (BUSY clear)");

        // pHOLD is released: the channel disabled itself at terminal count, so nobody is
        // still asking for the bus once the burst is done.
        CHECK(!fdc->requestsBus(), "the 8257 dropped its bus request at terminal count");
    }

    // ---- `timing = real`: the WAIT port holds READY (issue #637) ----
    //
    // The Tarbell PROM's RLOOP, run by a real 8080. Under `real` each IN FC holds READY until
    // the FD1771 has the next byte, so the loop runs at the disk's pace; under `full` it runs
    // at the CPU's.
    SECTION("boards/tarbell: timing = real -- the wait port holds READY");
    {
        // 00 IN FC ; MVI A,1 ; OUT FA ; MVI A,8C ; OUT F8 ; LXI H,2000
        // 0D L: IN FC ; ORA A ; JP 1A ; IN FB ; MOV M,A ; INX H ; JMP L
        // 1A HLT
        const std::vector<uint8_t> rloop = {
            0xDB, CTL, 0x3E, 0x01, 0xD3, SECR, 0x3E, 0x8C, 0xD3, CMD, 0x21, 0x00, 0x20,
            0xDB, CTL, 0xB7, 0xF2, 0x1A, 0x00, 0xDB, DAT, 0x77, 0x23, 0xC3, 0x0D, 0x00,
            0x76};
        const uint64_t settle = 20000;  // 10 ms at 2 MHz
        const uint64_t byte   = 64;     // 32 us at 250 kbit/s

        withRampDisk(77ull * 26 * 128);
        {
            TimedRig g("tarbell", "real", rloop);
            CHECK(g.step() == 10,
                  "the first IN FC holds nothing: a Restore already at track 0 ends at once");
            g.step();  // MVI
            g.step();  // OUT FA
            g.step();  // MVI
            uint64_t t = g.step() + g.step() + g.step();  // OUT F8 ; LXI ; IN FC
            CHECK(t == 10 + settle + byte,
                  "from the Read Sector OUT, the first IN FC ends after the settle and one byte time");
            t = 0;
            for (int i = 0; i < 7; ++i) t += g.step();  // ORA JP IN-FB MOV INX JMP IN-FC
            CHECK(t == byte, "each later pass of the loop takes one byte time");
        }
        {
            TimedRig g("tarbell", "real", rloop);
            const uint64_t t0 = g.m.clock.now();
            RunResult rr = g.m.debug.run(2000000);
            CHECK(rr.why == StopReason::Halted, "the timed RLOOP reaches its HLT");
            bool ramp = true;
            for (int i = 0; i < 128; ++i)
                if (g.m.bus.memRead((uint16_t)(0x2000 + i)) != (uint8_t)i) ramp = false;
            CHECK(ramp, "...with track 0 sector 1 in RAM, in order");
            CHECK(g.m.bus.memRead(0x2080) == 0, "...and not one byte more");
            CHECK((in(*g.fdc, CMD) & 0x04) == 0, "...and no Lost Data");
            CHECK(g.m.clock.now() - t0 >= settle + 128 * byte,
                  "...and it took the disk's time: the settle and 128 byte times");
        }
        {
            TimedRig g("tarbell", "full", rloop);
            CHECK(g.step() == 10, "under timing = full an IN FC is the 8080's 10 T-states");
            const uint64_t t0 = g.m.clock.now();
            RunResult rr = g.m.debug.run(2000000);
            CHECK(rr.why == StopReason::Halted && g.m.bus.memRead(0x2005) == 5,
                  "...and the same loop reads the same sector");
            CHECK(g.m.clock.now() - t0 < settle, "...in less time than the head settle alone");
        }
    }

    // ---- `timing = real` on the DD card: the 8257 steals one cycle per DRQ ----
    //
    // The chip is no longer wait-synced, so DRQ comes up once a byte time and the 8257 takes
    // each byte as it comes -- cycle stealing, with the CPU running in between. The program
    // spins in a delay loop instead of halting, because a halted 8080 under this test's run
    // loop would stop before the disk has turned.
    {
        withRampDisk(26ull * 128 + 76ull * 51 * 128);
        const uint8_t ADR0 = dmaBase + 0, WCT0 = dmaBase + 1, CMND = dmaBase + 8;
        TimedRig g("tarbelldd", "real", {
            0xAF, 0xD3, CMND,                // XRA A ; OUT CMND
            0x3E, 0x7F, 0xD3, WCT0,          // count low  (128 - 1)
            0x3E, 0x40, 0xD3, WCT0,          // count high + mode 01 (read)
            0x3E, 0x00, 0xD3, ADR0,          // address low
            0x3E, 0x2C, 0xD3, ADR0,          // address high (-> 0x2C00)
            0x3E, 0x41, 0xD3, CMND,          // enable ch0 + TC-STOP
            0x3E, 0x00, 0xD3, TRK,           // track 0
            0x3E, 0x01, 0xD3, SECR,          // sector 1
            0xDB, CTL,                       // IN FC: wait out the power-on Restore first
            0x3E, 0x88, 0xD3, CMD,           // Read Sector
            0x01, 0x00, 0x04,                // LXI B,0400
            0x0B, 0x78, 0xB1, 0xC2, 0x28, 0x00,  // 28 D: DCX B ; MOV A,B ; ORA C ; JNZ D
            0x76});                          // HLT
        const uint64_t t0 = g.m.clock.now();
        RunResult rr = g.m.debug.run(2000000);
        CHECK(rr.why == StopReason::Halted, "the CPU runs its delay loop to the HLT");
        bool landed = true;
        for (int i = 0; i < 128; ++i)
            if (g.m.bus.memRead((uint16_t)(0x2C00 + i)) != (uint8_t)i) landed = false;
        CHECK(landed, "the whole sector was DMA'd into RAM at 0x2C00");
        CHECK(g.m.bus.memRead(0x2C80) == 0, "...and nothing past it");
        CHECK((in(*g.fdc, CMD) & 0x05) == 0, "the command completed with no Lost Data");
        CHECK(g.m.clock.now() - t0 >= 128 * 64, "...and the transfer took 128 byte times");
    }

    // ---- Port FD waits out a Seek: the DMA CBIOS's completion poll ----
    //
    // The DMA build of the tracked CBIOS issues a Seek and then spins `IN FD ; RLC ; JC` before
    // it reads status -- no DMA armed at all. So FD bit 7 is the FD1791's INTRQ, not the 8257's
    // terminal count. Under `real` the Seek takes 5 x 10 ms; a FD that said "complete" at once
    // would hand the CBIOS a status with BUSY still set.
    for (const char* timing : {"real", "full"}) {
        withRampDisk(26ull * 128 + 76ull * 51 * 128);
        TimedRig g("tarbelldd", timing, {
            0x3E, 0x05, 0xD3, DAT,             // MVI A,5 ; OUT FB  (target track)
            0x3E, 0x12, 0xD3, CMD,             // MVI A,12 ; OUT F8 (Seek, 10 ms steps)
            0xDB, EXT, 0x07, 0xDA, 0x08, 0x00, // 08 L: IN FD ; RLC ; JC L
            0xDB, CMD, 0x32, 0x00, 0x30,       // IN F8 ; STA 3000
            0x76});                            // HLT
        const uint64_t t0 = g.m.clock.now();
        RunResult rr = g.m.debug.run(2000000);
        const bool real = std::string(timing) == "real";
        CHECK(rr.why == StopReason::Halted, real ? "timing = real: the FD poll ends at the HLT"
                                                 : "timing = full: the FD poll ends at the HLT");
        CHECK((g.m.bus.memRead(0x3000) & 0x01) == 0 && in(*g.fdc, TRK) == 5,
              "...with the Seek done when FD said so: BUSY clear, on track 5");
        if (real)
            CHECK(g.m.clock.now() - t0 >= 5 * 20000, "...after the five 10 ms steps");
    }

    // The DD card READS a single-density-sized image too -- the DD controller is a superset,
    // not a single-size gate (existing SSSD disks, PD disk 2). A 256,256 image mounts as all
    // single density and track 0 sector 1 reads back clean.
    {
        Clock c;
        TarbellDdBoard b;
        b.attachClock(&c);
        b.power();
        withRampDisk(77ull * 26 * 128);  // 256,256 -- the SD size
        std::string err;
        CHECK(b.mount("drive0", "sd.dsk", false, err),
              "a single-density-sized image is ACCEPTED by the DD card (it reads SD media)");

        out(b, CTL, 0x00);  // density SD, drive 0, side 0
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);  // Read Sector
        std::vector<uint8_t> got = pollRead(b);
        CHECK(got.size() == 128, "track 0 sector 1 reads a full 128-byte SD sector");
        CHECK(got[0] == 0 && (in(b, CMD) & 0x1C) == 0, "...clean, and it is sector 1's data");
    }

    // The DD card refuses an OVERSIZED image (larger than the mixed DD disk -- a real one never is).
    {
        Clock c;
        TarbellDdBoard b;
        b.attachClock(&c);
        b.power();
        withRampDisk(499456 + 128);  // one sector past the mixed disk
        std::string err;
        CHECK(!b.mount("drive0", "big.dsk", false, err),
              "an oversized image is refused by the DD card");
        CHECK(err.find("too large") != std::string::npos, "...with a reason that says why");
    }

    // ---- THE BOOT PROM / PHANTOM* CONTRACT (the real 32-byte TARPROM) ----
    //
    // The board loads builtin:tarbell-sd on power(); byte 00 is DB (IN FC) and byte 1F
    // is 76 (HLT). This is the test_phantom.cpp contract, retargeted onto the shipping
    // board with its real PROM. setVerify re-derives every decode the slow way.
    {
        Bus bus;
        bus.setVerify(true);
        auto* tar = new TarbellBoard();
        tar->id = "tar";
        auto* mem = ram64k("mem0");
        bus.attach(tar);
        bus.attach(mem);
        tar->power();  // loads the PROM and arms the shadow (no clock needed for this half)

        // 1. The PROM shadows RAM for reads.
        CHECK(bus.memRead(0x0000) == 0xDB, "0000 reads the boot PROM, not RAM");
        CHECK(bus.memRead(0x001F) == 0x76, "001F reads the PROM's last byte (HLT)");
        CHECK(tar->promArmed(), "still armed -- no A5-high read yet");

        // 2. Writes fall through to the RAM UNDER the shadow, at the shadowed address.
        bus.memWrite(0x0000, 0x42);
        CHECK(bus.memRead(0x0000) == 0xDB, "the read still comes back from the PROM");
        CHECK(mem->storeAt(0x0000) == 0x42, "...but the byte DID land in the RAM underneath");
        CHECK(tar->promArmed(), "a write does not release the shadow");

        // 2b. A WRITE with A5 set does NOT release it -- the sector load walks past 0020
        //     while the loader still fetches itself from the PROM.
        bus.memWrite(0x0040, 0x99);
        CHECK(tar->promArmed(), "a WRITE with A5 set does not release the shadow");
        CHECK(bus.memRead(0x0000) == 0xDB, "...so the PROM is still shadowing mid-load");

        // 3. A READ with A5 set releases combinationally, on that very cycle -- the PROM's
        //    own `JZ 07DH` (0x7D has A5 set) is the first such read and must land in RAM.
        bus.memWrite(0x007D, 0xC3);
        CHECK(bus.memRead(0x007D) == 0xC3, "the A5-high read is already un-shadowed (reads RAM)");
        CHECK(!tar->promArmed(), "...and that fetch latched the shadow off");

        // 4. It stays released, back down inside what used to be the PROM's range.
        CHECK(bus.memRead(0x0000) == 0x42, "0000 now reads the RAM we wrote in step 2");
        CHECK(bus.drain().empty(), "no contention: the released PROM stops driving too");

        // 5. POC* re-arms it -- how you boot the machine a second time.
        tar->reset(Reset::PowerOn);
        CHECK(tar->promArmed(), "reset re-arms the boot PROM");
        CHECK(bus.memRead(0x0000) == 0xDB, "...and the PROM shadows again");

        delete tar;
        delete mem;
    }

    // ---- A5 IS A WIRE, NOT A THRESHOLD (0040 is above the PROM but has A5 clear) ----
    {
        Bus bus;
        bus.setVerify(true);
        auto* tar = new TarbellBoard();
        tar->id = "tar";
        auto* mem = ram64k("mem0");
        bus.attach(tar);
        bus.attach(mem);
        tar->power();

        bus.memRead(0x0040);
        CHECK(tar->promArmed(), "0040 is above the PROM but A5 is CLEAR -- still shadowing");
        bus.memRead(0x0060);
        CHECK(!tar->promArmed(), "0060 has A5 set -- released");

        delete tar;
        delete mem;
    }

    // ---- bootstrap = off: the whole PROM/PHANTOM* half is disabled ----
    {
        Bus bus;
        bus.setVerify(true);
        auto* tar = new TarbellBoard();
        tar->id = "tar";
        auto* mem = ram64k("mem0");
        bus.attach(tar);
        bus.attach(mem);
        std::string err;
        CHECK(setProperty(*tar, "bootstrap", "off", err), "the boot-PROM DIP turns off");
        tar->power();

        CHECK(bus.memRead(0x0000) == 0x00, "with the DIP off, 0000 reads plain RAM (zero)");
        bus.memWrite(0x0005, 0x5A);
        CHECK(bus.memRead(0x0005) == 0x5A, "...and low memory is ordinary read/write RAM");
        BusCycle probe;
        probe.type = Cycle::MemRead;
        probe.addr = 0x0000;
        CHECK(!tar->assertsPhantom(probe), "PHANTOM* is not asserted with the boot DIP off");

        delete tar;
        delete mem;
    }

    // ---- THE CBIOS `HOME`: Restore, then select the new drive, with no wait between ----
    //
    // 2ABIOS24.ASM does `MVI A,STPRAT / OUT DCOM` and falls into SETTRK, which does
    // `OUT DCONT`. The step pulses go to the drive selected when they happen, so it is the NEW
    // drive that goes home. The board must not step in the cycle that loads the command.
    SECTION("boards/tarbell: a drive select straight after a Restore homes the new drive");
    for (int oldTrack : {2, 0}) {  // the old drive off track 0, and already on it
        withRampDisk(77ull * 26 * 128);
        Clock c;
        TarbellDdBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        b.mount("drive1", "b.dsk", false, err);
        const uint8_t selA = 0x00, selB = 0x10;  // FC bits 4-5 = drive, bit 3 clear = SD

        // Put drive B on track 5 and drive A on `oldTrack`, each with its own Seek.
        out(b, CTL, selB);
        out(b, TRK, 0);
        out(b, DAT, 5);
        out(b, CMD, 0x10);  // Seek, no verify
        CHECK((in(b, CMD) & 0x04) == 0, "drive B is off track 0");
        out(b, CTL, selA);
        out(b, TRK, 0);
        out(b, DAT, (uint8_t)oldTrack);
        out(b, CMD, 0x10);
        CHECK(((in(b, CMD) & 0x04) != 0) == (oldTrack == 0), "drive A is where the test put it");

        out(b, CMD, 0x00);   // HOME: Restore...
        b.pump();            // (the run loop may poll the board in between: it moves nothing)
        out(b, CTL, selB);   // ...and SETTRK selects drive B at once
        CHECK((in(b, CTL) & 0x80) == 0, "the wait port reports the command done");
        CHECK((in(b, CMD) & 0x04) != 0, "drive B is home");
        CHECK(in(b, TRK) == 0, "...and the track register is zero");
        out(b, CTL, selA);
        CHECK(((in(b, CMD) & 0x04) != 0) == (oldTrack == 0), "drive A did not move");
    }

    SECTION("boards/tarbell: bad sectors -- the `faults` key, CONFIG SAVE and SNAPSHOT (#966)");
    {
        withRampDisk(256256);
        std::string  err;
        TarbellBoard b;
        b.id = "fdc0";
        CHECK(b.takesFaults("drive0") && b.takesFaults("drive3") && !b.takesFaults("drive9"),
              "every drive takes faults, and nothing else does");
        CHECK(b.faults("drive0") == nullptr, "an empty drive has no fault table");

        CHECK(!b.loadSubUnit("drive", {{"unit", "1"}, {"faults", "2/0/1 data"}}, err) && !err.empty(),
              "`faults` with no `mount` is refused");
        err.clear();
        CHECK(!b.loadSubUnit("drive", {{"unit", "0"}, {"mount", "a.dsk"}, {"faults", "2/0/1 nonsense"}}, err) &&
                  err.find("fault kind") != std::string::npos,
              "a bad fault list is refused with the parser's reason");

        CHECK(b.loadSubUnit("drive", {{"unit", "0"}, {"mount", "a.dsk"}, {"faults", "2/0/1 data count=3; 5/*/* id"}},
                            err),
              "a drive table with `faults` loads");
        DiskFaults* f = b.faults("drive0");
        CHECK(f && f->list().size() == 2 && f->toText() == "2/0/1 data count=3; 5/*/* id", "...and the drive has them");

        bool saved = false;
        for (const auto& su : b.subUnits())
            for (const auto& fld : su.fields)
                saved = saved || (fld.key == "faults" && fld.quoted && fld.text == "2/0/1 data count=3; 5/*/* id");
        CHECK(saved, "CONFIG SAVE writes the `faults` key back, quoted");

        // SNAPSHOT keeps the counts: use the data fault once, save, restore into a second card.
        DiskFaults::use(*f->find(2, 0, 1, FaultKind::Data));
        StateWriter w;
        b.serialize(w);
        TarbellBoard c;
        c.id = "fdc0";
        CHECK(c.loadSubUnit("drive", {{"unit", "0"}, {"mount", "a.dsk"}}, err), "the same disk in a second card");
        StateReader r(w.data());
        c.deserialize(r);
        CHECK(r.ok() && r.remaining() == 0, "RESTORE reads the whole record");
        const DiskFaults* g = c.faults("drive0");
        CHECK(g && g->list().size() == 2 && g->list()[0].left == 2 && g->list()[0].hits == 1,
              "...and the faults come back with their counts");

        CHECK(b.unmount("drive0", err) && b.faults("drive0") == nullptr, "UNMOUNT: no disk, no faults");
        CHECK(b.mount("drive0", "a.dsk", false, err) && b.faults("drive0")->empty(),
              "a new disk in the drive brings no faults");
    }
}
