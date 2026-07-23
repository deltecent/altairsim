// The 88-HDSK "Datakeeper" hard disk controller (docs/boards/mits-88hdsk.md).
//
// The acceptance test boots CP/M off this card to `A>` (tests/acceptance/examples.cmake,
// examples/hdsk). This file pins what the boot cannot: the exact (cylinder,side,sector)
// -> image-offset mapping, the command/handshake sequencing, and the write path -- the
// things that fail on the sector you have not read yet, or fail silently.
//
// No filesystem: MemoryMedia through setMediaResolver, exactly like tests/test_dcdd.cpp.

#include "boards/mits-88hdsk.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint64_t kPlatter = 4988928;  // 406 cyl x 2 sides x 24 sectors x 256

// Port offsets from the board base (default 0xA0).
enum {
    CREADY = 0xA0, CSTAT = 0xA1, ACSTA = 0xA2, ACMD = 0xA3,
    CDSTA  = 0xA4, CDATA = 0xA5, ADSTA = 0xA6, ADATA = 0xA7,
};

uint8_t in(HdskBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(HdskBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// Issue a 16-bit command: low byte to A7, high byte to A3 (which starts it).
void cmd(HdskBoard& b, uint16_t word) {
    out(b, ADATA, (uint8_t)(word & 0xFF));
    out(b, ACMD, (uint8_t)(word >> 8));
}

// Seek unit to a cylinder.
void seek(HdskBoard& b, int unit, int cyl) {
    cmd(b, (uint16_t)((unit << 10) | (cyl & 0x3FF)));  // opcode 0x0
}

// Read one disk sector into an internal buffer.
void readSector(HdskBoard& b, int unit, int buf, int platter, int side, int sector) {
    uint16_t low  = (uint16_t)((platter << 6) | (side << 5) | (sector & 0x1F));
    uint16_t word = (uint16_t)(0x3000 | (unit << 10) | (buf << 8) | low);
    cmd(b, word);
}

// Read a whole buffer out through CDATA (len 0 == 256).
std::vector<uint8_t> readBuffer(HdskBoard& b, int buf, int len) {
    cmd(b, (uint16_t)(0x5000 | (buf << 8) | (len & 0xFF)));  // opcode 0x5
    int n = len ? len : 256;
    std::vector<uint8_t> out_(n);
    for (int i = 0; i < n; ++i) {
        CHECK((in(b, CDSTA) & 0x80) != 0, "CDSTA stays asserted for the whole read stream");
        out_[(size_t)i] = in(b, CDATA);
    }
    return out_;
}

// An image where every sector is filled with its own sector index (mod 256), so the
// bytes a read returns identify which offset they came from.
void withIndexedDisk() {
    setMediaResolver([](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> img((size_t)kPlatter);
        for (size_t s = 0; s * 256 < img.size(); ++s)
            for (int i = 0; i < 256; ++i) img[s * 256 + (size_t)i] = (uint8_t)(s & 0xFF);
        return std::make_unique<MemoryMedia>(path, std::move(img), ro);
    });
}

} // namespace

void test_hdsk() {
    SECTION("88-HDSK -- the probe accepts one platter and nothing else");
    {
        std::string err;
        auto probes = [&](uint64_t bytes) {
            setMediaResolver([bytes](const std::string& p, bool ro, std::string&) {
                return std::make_unique<MemoryMedia>(p, std::vector<uint8_t>((size_t)bytes), ro);
            });
            HdskBoard b;
            return b.mount("drive0", "hd.dsk", false, err);
        };

        CHECK(probes(kPlatter), "406 x 2 x 24 x 256 = 4,988,928 is one Datakeeper platter");
        CHECK(probes(kPlatter + 127), "the 128-byte XMODEM tolerance holds...");
        CHECK(!probes(kPlatter + 128), "...and not one byte more");
        CHECK(!probes(337568), "an 8-inch floppy is not an HDSK platter");
        CHECK(err.find("88-HDSK platter") != std::string::npos, "and the error says so");

        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- section 6: all status bits read 1 on the first read after power-on");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        b.mount("drive0", "hd.dsk", false, err);
        b.power();

        CHECK(in(b, CSTAT) == 0xFF, "the first CSTAT read after power-on is 0xFF");

        seek(b, 0, 0);
        readSector(b, 0, 0, 0, 0, 0);
        CHECK(in(b, CSTAT) == 0x00, "and after a clean command it reads 0x00");

        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- Seek, Read Sector, Read Buffer: the boot loader's own sequence");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        b.mount("drive0", "hd.dsk", false, err);

        // The descriptor page: unit 0, cyl 0, side 0, sector 0 -> image offset 0 -> index 0.
        seek(b, 0, 0);
        CHECK((in(b, CREADY) & 0x80) != 0, "CREADY is asserted the moment the seek completes");
        CHECK(in(b, CSTAT) == 0, "with no error");

        readSector(b, 0, /*buf=*/0, /*platter=*/0, /*side=*/0, /*sector=*/0);
        auto page = readBuffer(b, /*buf=*/0, /*len=*/0);
        CHECK(page.size() == 256, "a length of 0 transfers a full 256-byte page");
        bool allZero = true;
        for (uint8_t v : page)
            if (v != 0) allZero = false;
        CHECK(allZero, "sector (0,0,0) is image offset 0 -- the first page, index 0");
    }

    SECTION("88-HDSK -- the (cylinder,side,sector) -> offset mapping, pinned three ways");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        b.mount("drive0", "hd.dsk", false, err);
        seek(b, 0, 0);

        auto pageIndex = [&](int platter, int side, int sector) -> uint8_t {
            readSector(b, 0, 0, platter, side, sector);
            return readBuffer(b, 0, 0)[0];
        };

        // offset = (cyl*48 + side*24 + sector)*256, so the sector INDEX is
        // cyl*48 + side*24 + sector.
        CHECK(pageIndex(0, 0, 1) == 1, "cyl 0 side 0 sector 1 -> index 1 (offset 256)");
        CHECK(pageIndex(0, 0, 23) == 23, "cyl 0 side 0 sector 23 -> index 23");
        CHECK(pageIndex(0, 1, 0) == 24, "cyl 0 side 1 sector 0 -> index 24 (the second surface)");

        seek(b, 0, 1);
        CHECK(pageIndex(0, 0, 0) == 48, "cyl 1 side 0 sector 0 -> index 48 (the next cylinder)");

        seek(b, 0, 2);
        CHECK(pageIndex(0, 0, 0) == 96, "cyl 2 side 0 sector 0 -> index 96");
    }

    SECTION("88-HDSK -- Write Buffer then Write Sector lands on the disk and syncs");
    {
        std::vector<uint8_t> img((size_t)kPlatter, 0xEE);
        MemoryMedia*         seen = nullptr;
        setMediaResolver([&img, &seen](const std::string& p, bool ro, std::string&) {
            auto m = std::make_unique<MemoryMedia>(p, img, ro);
            seen   = m.get();
            return m;
        });

        HdskBoard   b;
        std::string err;
        b.mount("drive0", "hd.dsk", false, err);
        seek(b, 0, 1);  // cyl 1

        // Fill buffer 0 with a known pattern via Write Buffer.
        cmd(b, 0x4000);  // Write Buffer, buffer 0, length 0 == 256
        for (int i = 0; i < 256; ++i) {
            CHECK((in(b, ADSTA) & 0x80) != 0, "ADSTA is asserted for the whole write stream");
            out(b, ADATA, (uint8_t)(0x40 + (i & 0x3F)));
        }
        CHECK((in(b, CREADY) & 0x80) != 0, "CREADY comes back after the last buffer byte");

        // Write buffer 0 to cyl 1, side 1, sector 5.
        cmd(b, (uint16_t)(0x2000 | (1 << 5) | 5));  // Write Sector, unit0 buf0 platter0 side1 sec5
        CHECK(in(b, CSTAT) == 0, "the write reports no error");
        CHECK(seen && seen->syncs() > 0, "and the medium was synced -- the bytes are durable");

        // Read it straight back through the card.
        readSector(b, 0, 0, 0, 1, 5);
        auto back = readBuffer(b, 0, 0);
        bool match = true;
        for (int i = 0; i < 256; ++i)
            if (back[(size_t)i] != (uint8_t)(0x40 + (i & 0x3F))) match = false;
        CHECK(match, "and reading the sector back returns exactly what was written");

        // ...at the right offset: (1*48 + 1*24 + 5) = 77, byte 77*256 in the vector.
        CHECK(seen->bytes()[(size_t)77 * 256] == 0x40, "the write landed at offset (cyl*48+side*24+sec)*256");

        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- error flags: illegal sector, and a not-ready unit");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        b.mount("drive0", "hd.dsk", false, err);  // unit 0, platter 0 only

        seek(b, 0, 0);
        readSector(b, 0, 0, 0, 0, /*sector=*/30);  // 30 >= 24
        CHECK((in(b, CSTAT) & 0x02) != 0, "sector 30 sets the illegal-sector flag (bit1)");

        readSector(b, /*unit=*/1, 0, 0, 0, 0);  // no platter mounted on unit 1
        CHECK((in(b, CSTAT) & 0x01) != 0, "a read on an unmounted unit sets drive-not-ready (bit0)");

        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- a poll loop on CREADY terminates in one pass");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        b.mount("drive0", "hd.dsk", false, err);

        seek(b, 0, 0);
        int spins = 0;
        while (!(in(b, CREADY) & 0x80)) {
            if (++spins > 4) break;  // a real guest would spin forever; we must exit at once
        }
        CHECK(spins == 0, "CREADY is already high when the guest first looks");

        setMediaResolver(openHostFile);
    }
}
