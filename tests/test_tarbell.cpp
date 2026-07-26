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

uint8_t in(TarbellBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(TarbellBoard& b, uint8_t port, uint8_t v) {
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
std::vector<uint8_t> pollRead(TarbellBoard& b) {
    std::vector<uint8_t> got;
    for (int guard = 0; guard < 4096; ++guard) {
        if ((in(b, CTL) & 0x80) == 0) break;  // INTRQ: the transfer finished
        got.push_back(in(b, DAT));
    }
    return got;
}

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

        withRampDisk(499456);  // the DD size -- NOT a single-density disk
        err.clear();
        CHECK(!b.mount("drive1", "dd.dsk", false, err),
              "a double-density-sized image is refused by the SD card");
        CHECK(err.find("single-density") != std::string::npos, "...with a reason that says why");
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
        out(b, CTL, 0x08);  // density DD, drive 0, side 0
        out(b, TRK, 0);
        out(b, SECR, 1);
        out(b, CMD, 0x88);
        CHECK(pollRead(b).size() == 128, "...and clearing D5:D4 reselected drive 0");

        // Port FD IN is the DMA-busy check: bit7 = 0 means "complete", so PIO code that
        // polls it never hangs.
        CHECK((in(b, EXT) & 0x80) == 0, "port FD reports DMA complete (bit7 = 0)");

        // EVERY sector of the SD track 0 (26) and a DD track (51) delivers exactly 128
        // bytes and completes -- a mixed-density boot reads straight down track 0.
        out(b, CTL, 0x08);  // density DD, drive 0
        bool t0ok = true;
        for (int s = 1; s <= 26; ++s) {
            out(b, TRK, 0);
            out(b, SECR, (uint8_t)s);
            out(b, CMD, 0x88);
            std::vector<uint8_t> got = pollRead(b);
            if (got.size() != 128 || (in(b, CMD) & 0x1C) != 0) t0ok = false;
        }
        CHECK(t0ok, "all 26 SD sectors of track 0 read exactly 128 bytes and complete");

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
    }

    // The DD card refuses a single-density-sized image (the mirror of the SD probe).
    {
        Clock c;
        TarbellDdBoard b;
        b.attachClock(&c);
        b.power();
        withRampDisk(77ull * 26 * 128);  // 256,256 -- the SD size
        std::string err;
        CHECK(!b.mount("drive0", "sd.dsk", false, err),
              "a single-density-sized image is refused by the DD card");
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
}
