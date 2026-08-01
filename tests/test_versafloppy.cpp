// SD Systems VersaFloppy II (docs/boards/sd-versafloppy.md).
//
// The acceptance test boots SDOS end to end. This file pins the board-level behavior the
// boot cannot see: the wait-synced (PRDY) transfer that moves a whole sector on `IN (67H)`
// with no DRQ polling, the 63H control latch, and the format probe.

#include "boards/sd-versafloppy.h"
#include "core/clock.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

uint8_t in(VersaFloppyBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(VersaFloppyBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// A disk whose byte i holds (i + i/256) & 0xFF. The `+ i/256` stamps each 256-byte sector with
// its own linear index, so sector N reads (N, N+1, N+2, ...) -- distinct per sector (a plain
// i&0xFF ramp repeats every 256 bytes, so every sector would look identical and a read could
// land on the wrong one undetected).
void withRampDisk(uint64_t bytes) {
    setMediaResolver([bytes](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d((size_t)bytes);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (uint8_t)((i + i / 256) & 0xFF);
        return std::make_unique<MemoryMedia>(path, std::move(d), ro);
    });
}

constexpr uint8_t P    = 0x60;  // base
constexpr uint8_t SEL  = P + 3; // 63H control/status
constexpr uint8_t CMD  = P + 4; // 64H command/status
constexpr uint8_t TRK  = P + 5; // 65H track
constexpr uint8_t SECR = P + 6; // 66H sector
constexpr uint8_t DAT  = P + 7; // 67H data

// Drive the 63H control latch for drive 0. The latch is NEGATIVE-TRUE (DDB200.ASM's `CPL`
// before `OUT`), so write() inverts on the way in -- we hand it the complement. D0 = drive-0
// one-hot select, D4 = side, D6 = density (VF-II).
void control(VersaFloppyBoard& b, bool dd, int side) {
    uint8_t c = 0x01;             // drive 0 (one-hot D0)
    if (side) c |= 0x10;          // D4 side select
    if (dd)   c |= 0x40;          // D6 double density -> chip data rate 500k
    out(b, SEL, (uint8_t)~c);
}

void seekTo(VersaFloppyBoard& b, int track) {
    out(b, DAT, (uint8_t)track);  // seek target in the data register
    out(b, CMD, 0x18);            // Seek, no verify
}

// A well-formed IBM-3740 track image the way the DDBIOS FMATE lays one down, parameterized over
// FM/MFM and sector geometry. Only the marks (0xFE ID, 0xFB data), the header, and the 0xF7
// CRC-generate terminators matter to the format parser (floppy-drive.cpp); the gaps are the
// slack pollWriteVf would otherwise pad. `trackNum` goes in the (ignored) ID header -- the head
// position wins. The structure MUST stay under one revolution (trackImageBytes) so every sector
// streams before the wait-synced command completes; pollWriteVf pads the remainder with gap.
std::vector<uint8_t> mkTrack(int trackNum, Density d, int sectors, int sectorSize) {
    std::vector<uint8_t> s;
    auto put = [&](uint8_t b, int n) { for (int i = 0; i < n; ++i) s.push_back(b); };
    const uint8_t N = (uint8_t)(sectorSize == 256 ? 1 : sectorSize == 512 ? 2 : 0);  // 128<<N
    if (d == Density::SD) {                                  // FM
        put(0xFF, 40); put(0x00, 6); s.push_back(0xFC);     // pre-index gap + index mark
        for (int sec = 1; sec <= sectors; ++sec) {
            put(0xFF, 26); put(0x00, 6);
            s.push_back(0xFE);                              // ID address mark
            s.push_back((uint8_t)trackNum);                 // track (ignored)
            s.push_back(0x00);                              // side
            s.push_back((uint8_t)sec);                      // sector
            s.push_back(N);                                 // length code
            s.push_back(0xF7);                              // ID CRC
            put(0xFF, 11); put(0x00, 6);
            s.push_back(0xFB);                              // data address mark
            put(0xE5, sectorSize);                          // payload
            s.push_back(0xF7);                              // data CRC
        }
    } else {                                                // MFM
        put(0x4E, 40);                                      // lead-in gap (no index mark)
        for (int sec = 1; sec <= sectors; ++sec) {
            put(0x4E, 12); put(0x00, 8); put(0xF5, 3);      // gap + sync + A1 marks
            s.push_back(0xFE);
            s.push_back((uint8_t)trackNum);
            s.push_back(0x00);
            s.push_back((uint8_t)sec);
            s.push_back(N);
            s.push_back(0xF7);
            put(0x4E, 12); put(0x00, 8); put(0xF5, 3);
            s.push_back(0xFB);
            put(0xE5, sectorSize);
            s.push_back(0xF7);
        }
    }
    return s;
}

// Stream a raw track through the wait-synced Write Track: write a byte (67H), then read the FDC
// status (64H) -- BUSY clear means the whole revolution was consumed and the track committed.
// Once the structured stream runs out, pad with gap 0xFFs until BUSY drops (the DDBIOS ENDTRK
// discipline: the guest does not know the revolution length, it fills until the command ends).
// The caller must have issued Write Track (0xF4) first. The guard bounds the largest budget
// (5.25" DD = 12500 bytes) so a stuck command cannot hang the test.
void pollWriteVf(VersaFloppyBoard& b, const std::vector<uint8_t>& stream) {
    size_t k = 0;
    for (int guard = 0; guard < 30000; ++guard) {
        if ((in(b, CMD) & 0x01) == 0) break;  // BUSY (S0) clear: the track is done
        out(b, DAT, k < stream.size() ? stream[k++] : 0xFF);
    }
}

// Format every track of a blank through the real ports, exactly as the guest's `Z` command does:
// step to a cylinder, then format each side of it (set the control latch for density + side,
// Write Track, stream) before stepping to the next. This is the DDBIOS FMAT discipline -- both
// sides of a cylinder are recorded before NXTRK advances the track (DDB200.ASM). CYLINDER-MAJOR,
// which is ascending image-slot order under the board's interleaved layout, so a double-sided
// blank grows contiguously (host/disk.h). Returns false if any track faulted (S5 WRITE FAULT).
bool formatBlank(VersaFloppyBoard& b, bool dd, int tracks, int heads, int sectors, int sectorSize) {
    for (int t = 0; t < tracks; ++t) {
        for (int side = 0; side < heads; ++side) {
            control(b, dd, side);   // select the drive + this side + density
            seekTo(b, t);           // both sides share the cylinder; re-seek is a no-op after side 0
            out(b, CMD, 0xF4);      // Write Track
            pollWriteVf(b, mkTrack(t, dd ? Density::DD : Density::SD, sectors, sectorSize));
            if (in(b, CMD) & 0x20) return false;  // S5 WRITE FAULT
        }
    }
    return true;
}

// Read one sector back: seek (the head is left on the last track after a format loop), address
// the sector, Read Sector, and pull `sectorSize` bytes off the wait-synced data port.
std::vector<uint8_t> readSectorVf(VersaFloppyBoard& b, int track, int sector, int sectorSize) {
    seekTo(b, track);
    out(b, SECR, (uint8_t)sector);
    out(b, CMD, 0x88);  // Read Sector
    std::vector<uint8_t> got;
    for (int i = 0; i < sectorSize; ++i) got.push_back(in(b, DAT));
    return got;
}

bool allE5(const std::vector<uint8_t>& v, int sectorSize) {
    if ((int)v.size() != sectorSize) return false;
    for (uint8_t x : v) if (x != 0xE5) return false;
    return true;
}

} // namespace

void test_versafloppy() {
    SECTION("boards/sd-versafloppy: the VersaFloppy II");

    // ---- THE WAIT-SYNCED READ: a whole sector arrives on IN (67H), no DRQ polling ----
    {
        withRampDisk(77ull * 26 * 256);  // 8" DD-256, 512,512 bytes
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();

        std::string err;
        CHECK(b.mount("drive0", "ramp.dsk", false, err), "the DD-256 image mounts");

        // Select drive 0 (one-hot D0) + DD (D6). The 63H latch is NEGATIVE-TRUE, so the guest
        // writes the complement -- exactly as DDB200.ASM's DRVSET does (`CPL` before `OUT`).
        out(b, SEL, (uint8_t)~0x41);

        // The head is at track 0 after power; point the FDC at track 0, sector 1.
        out(b, TRK, 0);
        out(b, SECR, 1);

        // Read Sector, then pull 256 bytes straight off the data port -- the DDBIOS INIR loop.
        out(b, CMD, 0x88);  // RDCMD
        std::vector<uint8_t> got;
        for (int i = 0; i < 256; ++i) got.push_back(in(b, DAT));

        CHECK(got.size() == 256, "a wait-synced read delivers a whole 256-byte sector");
        bool ramp = true;
        for (int i = 0; i < 256; ++i) if (got[(size_t)i] != (uint8_t)i) ramp = false;
        CHECK(ramp, "...and the bytes are the sector's, in order");

        // The command has completed: status is not busy (S0 clear).
        CHECK((in(b, CMD) & 0x01) == 0, "the command is done -- BUSY is clear after the last byte");
    }

    // ---- READ A DIFFERENT SECTOR after a seek ----
    {
        withRampDisk(77ull * 26 * 256);
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "ramp.dsk", false, err);
        out(b, SEL, (uint8_t)~0x41);  // negative-true latch: drive 0 + DD

        // Seek to track 1 (data reg = target, then Seek 0x18), then read sector 3.
        out(b, DAT, 1);      // seek target in the data register
        out(b, CMD, 0x18);   // Seek, no verify
        CHECK((in(b, CMD) & 0x01) == 0, "the seek completes at once under PRDY");
        CHECK(in(b, TRK) == 1, "the track register followed the head to track 1");

        out(b, SECR, 3);
        out(b, CMD, 0x88);
        std::vector<uint8_t> got;
        for (int i = 0; i < 256; ++i) got.push_back(in(b, DAT));
        // track 1 sector 3 -> linear sector index 1*26 + (3-1) = 28. The disk stamps sector N
        // as (N, N+1, ...), so this sector reads 28, 29, 30, ...
        CHECK(got[0] == 28, "read lands on the right (track, sector) after the seek");
        CHECK(got[5] == 33, "...and streams that sector's bytes in order");
    }

    // ---- THE WAIT-SYNCED WRITE: OUT (67H) a whole sector, then read it back ----
    {
        withRampDisk(77ull * 26 * 256);
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "ramp.dsk", false, err);
        out(b, SEL, (uint8_t)~0x41);

        out(b, TRK, 0);
        out(b, SECR, 5);

        out(b, CMD, 0xA8);  // WRCMD -- Write Sector
        for (int i = 0; i < 256; ++i) out(b, DAT, (uint8_t)(0xC0 ^ i));
        CHECK((in(b, CMD) & 0x01) == 0, "the write completes -- BUSY clears after the last byte");
        CHECK((in(b, CMD) & 0x40) == 0, "...and it was not write-protected");

        // Read it straight back.
        out(b, SECR, 5);
        out(b, CMD, 0x88);
        bool same = true;
        for (int i = 0; i < 256; ++i) if (in(b, DAT) != (uint8_t)(0xC0 ^ i)) same = false;
        CHECK(same, "the bytes written are the bytes read back");
    }

    // ---- A MIXED-CASE writeprotect from a machine file is honored ----
    // The schema validator accepts True/On/YES case-insensitively; addSubUnit must too, or a
    // disk the operator meant to protect mounts read/write. (loadSubUnit -> addSubUnit is the
    // real [[board.drive]] path.)
    {
        withRampDisk(77ull * 26 * 256);
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        KeyValues kv = {{"unit", "0"}, {"mount", "ramp.dsk"}, {"writeprotect", "On"}};
        CHECK(b.loadSubUnit("drive", kv, err), "the [[board.drive]] table loads");
        auto u = b.units();
        CHECK(!u.empty() && u[0].readOnly, "writeprotect = \"On\" (mixed case) mounts read-only");
    }

    // ---- THE BOARD STILL DECODES AT THE TOP OF ITS PORT RANGE (port = F8) ----
    // `p < port_ + 8` truncates F8+8 to 0 in a uint8_t and the board would go deaf. The
    // decode uses the offset instead, so the F8..FF window is live.
    {
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        std::string err;
        CHECK(setProperty(b, "port", "F8", err), "port F8 is a legal base (the property max)");
        auto decodesPort = [&](uint8_t p) {
            BusCycle x; x.type = Cycle::IoRead; x.addr = p; return b.decodes(x);
        };
        CHECK(decodesPort(0xF8), "port F8: the base decodes");
        CHECK(decodesPort(0xFF), "port F8: the top of the 8-port window (FF) decodes");
        CHECK(!decodesPort(0xF7), "port F8: just below the window does not");
    }

    // ---- A WRITE-PROTECTED drive refuses the write (S6 PROTECTED), writes nothing ----
    {
        withRampDisk(77ull * 26 * 256);
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        b.mount("drive0", "ramp.dsk", /*ro=*/true, err);  // write-protected
        out(b, SEL, (uint8_t)~0x41);
        out(b, TRK, 0);
        out(b, SECR, 2);
        out(b, CMD, 0xA8);
        CHECK((in(b, CMD) & 0x40) != 0, "a write to a read-only disk sets S6 PROTECTED");
    }

    // ---- FORMAT A BLANK, EVERY ONE OF THE TEN SD-SYSTEMS FORMAT CODES ----
    //
    // The heart of the feature. For each `Z`-command format (SD Systems Monitor §3.4), mount a
    // 0-byte blank with media=NAME, format every track (both sides for a double-sided one) through
    // the real ports -- the DDBIOS FMATE discipline: control latch, seek, Write Track, stream --
    // and confirm the host file grew to the exact image size and reads back 0xE5 at representative
    // sectors, including the LAST sector of the LAST track (proves the RPM-aware revolution budget
    // held every sector) and, for double-sided disks, a head-1 sector (proves the cylinder-major
    // blank-grow). The synthetic track (mkTrack) and the streaming (pollWriteVf) are the guest;
    // the emulated controller does the format, exactly as real hardware does.
    {
        struct Code {
            const char* media;
            bool        dd;
            int         tracks, heads, sectors, sectorSize;
        };
        // Ordered as the format codes 0..7,C,D. 8dd256 (fC) shares its 512,512 size with 8sd-ds
        // (f1); both are covered, and the collision itself is the next section.
        const std::vector<Code> codes = {
            {"8sd",       false, 77, 1, 26, 128},  // f0  8" SS-SD   256,256
            {"8sd-ds",    false, 77, 2, 26, 128},  // f1  8" DS-SD   512,512
            {"5sd",       false, 35, 1, 18, 128},  // f2  5" SS-SD    80,640
            {"5sd-ds",    false, 35, 2, 18, 128},  // f3  5" DS-SD   161,280
            {"8dd",       true,  77, 1, 50, 128},  // f4  8" SS-DD   492,800
            {"8dd-ds",    true,  77, 2, 50, 128},  // f5  8" DS-DD   985,600
            {"5dd",       true,  35, 1, 29, 128},  // f6  5" SS-DD   129,920
            {"5dd-ds",    true,  35, 2, 29, 128},  // f7  5" DS-DD   259,840
            {"8dd256",    true,  77, 1, 26, 256},  // fC  8" SS-DD-256  512,512 (SDOS master)
            {"8dd256-ds", true,  77, 2, 26, 256},  // fD  8" DS-DD-256  1,025,024
        };

        for (const Code& fc : codes) {
            MemoryMedia* media = nullptr;
            setMediaResolver([&](const std::string& path, bool ro, std::string&) {
                auto m = std::make_unique<MemoryMedia>(path, std::vector<uint8_t>{}, ro);
                media  = m.get();
                return m;
            });

            Clock c;
            VersaFloppyBoard b;
            b.attachClock(&c);
            b.power();

            std::string err;
            const std::string tag = std::string("media=") + fc.media + ": ";
            std::string       buf;  // outlives each CHECK's full-expression; reused per message
            auto msg = [&](const char* s) -> const char* { buf = tag + s; return buf.c_str(); };

            KeyValues kv = {{"unit", "0"}, {"media", fc.media}, {"mount", "blank.dsk"}};
            CHECK(b.loadSubUnit("drive", kv, err), msg("the blank mounts"));
            CHECK(media && media->size() == 0, msg("...and starts empty (0 bytes)"));

            const bool ok = formatBlank(b, fc.dd, fc.tracks, fc.heads, fc.sectors, fc.sectorSize);
            CHECK(ok, msg("every track formats, no WRITE FAULT"));

            const uint64_t want =
                (uint64_t)fc.tracks * fc.heads * fc.sectors * fc.sectorSize;
            CHECK(media->size() == want, msg("the file grew to the exact image size"));

            // Read back side 0, the LAST sector of the LAST track -- the revolution budget had to
            // hold all of them. Set the control latch to this format's density, side 0, first.
            control(b, fc.dd, 0);
            std::vector<uint8_t> last =
                readSectorVf(b, fc.tracks - 1, fc.sectors, fc.sectorSize);
            CHECK(allE5(last, fc.sectorSize), msg("last sector of last track reads back 0xE5"));
            CHECK((in(b, CMD) & 0x1C) == 0, msg("...with no RNF/CRC/Lost-Data error"));

            // Double-sided: a head-1 sector must read back too (its interleaved slot grew in order).
            if (fc.heads == 2) {
                control(b, fc.dd, 1);  // D4 side select -> side B
                std::vector<uint8_t> h1 =
                    readSectorVf(b, fc.tracks - 1, fc.sectors, fc.sectorSize);
                CHECK(allE5(h1, fc.sectorSize), msg("a head-1 sector reads back 0xE5"));
                CHECK((in(b, CMD) & 0x1C) == 0, msg("...head-1 read is clean"));
            }
        }
    }

    // ---- REFORMAT A TRACK IN PLACE on an already-sized disk ----
    // Not a blank-grow: a full disk reformatted track-by-track keeps the same size, and the fill
    // the guest streams (0xE5) replaces whatever was there. Mount a recognized 8" SD image, verify
    // it is the ramp, reformat one track, and read back 0xE5.
    {
        withRampDisk(77ull * 26 * 128);  // 8" SS-SD, probes as 8sd (formatted)
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;
        CHECK(b.mount("drive0", "ramp.dsk", false, err), "the SD image mounts formatted");

        control(b, /*dd=*/false, /*side=*/0);
        std::vector<uint8_t> before = readSectorVf(b, 5, 1, 128);
        CHECK(before.size() == 128 && before[0] != 0xE5, "track 5 starts as the ramp, not 0xE5");

        seekTo(b, 5);
        out(b, CMD, 0xF4);  // Write Track
        pollWriteVf(b, mkTrack(5, Density::SD, 26, 128));
        CHECK((in(b, CMD) & 0x20) == 0, "the reformat took: no WRITE FAULT");

        std::vector<uint8_t> after = readSectorVf(b, 5, 1, 128);
        CHECK(allE5(after, 128), "the reformatted track reads back 0xE5");
    }

    // ---- THE 512,512 COLLISION: fC by default, f1 only when forced ----
    // 8" SS-DD-256 (fC) and 8" DS-SD-128 (f1) are both 512,512 bytes. An unforced probe must land
    // on fC (the SDOS master, single-sided); media=8sd-ds forces the double-sided f1. The tell is
    // the head count: fC has one side, so a read of side B is Record Not Found; f1 has two.
    {
        withRampDisk(512512);
        Clock c;
        VersaFloppyBoard b;
        b.attachClock(&c);
        b.power();
        std::string err;

        // Unforced -> fC (single-sided). A side-B read finds no track: RNF (S4).
        CHECK(b.mount("drive0", "ramp.dsk", false, err), "512,512 with no media mounts");
        control(b, /*dd=*/true, /*side=*/1);  // side B
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);  // Read Sector on side B
        CHECK((in(b, CMD) & 0x10) != 0, "auto-probe chose fC (one side): side B is Record Not Found");

        // Forced f1 (media=8sd-ds, double-sided). Side B now reads back.
        KeyValues kv = {{"unit", "1"}, {"media", "8sd-ds"}, {"mount", "ramp.dsk"}};
        CHECK(b.loadSubUnit("drive", kv, err), "media=8sd-ds forces the f1 geometry");
        out(b, SEL, (uint8_t)~0x12);  // drive 1 (D1) + side B (D4)
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);  // Read Sector on side B of drive 1
        for (int i = 0; i < 128; ++i) in(b, DAT);
        CHECK((in(b, CMD) & 0x10) == 0, "media=8sd-ds (f1, two sides): side B reads, no RNF");
    }
}
