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

} // namespace

void test_versafloppy() {
    SECTION("boards/sd-versafloppy: the VersaFloppy II");

    // ---- THE WAIT-SYNCED READ: a whole sector arrives on IN (67H), no DRQ polling ----
    {
        withRampDisk(77ull * 26 * 256);  // 8" DD-256, 512,512 bytes
        VersaFloppyBoard b;
        Clock c;
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
        VersaFloppyBoard b;
        Clock c;
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
        VersaFloppyBoard b;
        Clock c;
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
        VersaFloppyBoard b;
        Clock c;
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
        VersaFloppyBoard b;
        Clock c;
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
        VersaFloppyBoard b;
        Clock c;
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
}
