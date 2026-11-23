// The 88-HDSK "Datakeeper" hard disk controller (docs/boards/mits-88hdsk.md).
//
// The acceptance test boots CP/M off this card to `A>` (tests/acceptance/media.cmake,
// tests/media/hdsk). This file pins what the boot cannot: the exact (cylinder,side,sector)
// -> image-offset mapping, the command/handshake sequencing, and the write path -- the
// things that fail on the sector you have not read yet, or fail silently.
//
// No filesystem: MemoryMedia through setMediaResolver, exactly like tests/test_dcdd.cpp.

#include "boards/mits-88hdsk.h"
#include "core/debug.h"
#include "core/statefile.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
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

// Write a 256-byte pattern through Write Buffer, then Write Sector it.
void writeSector(HdskBoard& b, int side, int sector, uint8_t fill) {
    cmd(b, 0x4000);  // Write Buffer, buffer 0, length 0 == 256
    for (int i = 0; i < 256; ++i) out(b, ADATA, fill);
    cmd(b, (uint16_t)(0x2000 | (side << 5) | sector));  // unit 0, buffer 0, platter 0
}

// Fill buffer 0 with a marker, so a read that moves nothing leaves the marker.
void markBuffer(HdskBoard& b, uint8_t fill) {
    cmd(b, 0x4000);
    for (int i = 0; i < 256; ++i) out(b, ADATA, fill);
}

// What the debug channel wrote while `f` ran.
std::string debugCapture(const std::function<void()>& f) {
    namespace fs = std::filesystem;
    std::string    err;
    const fs::path p = fs::temp_directory_path() / "altair_hdsk_debug.log";
    std::error_code ec;
    fs::remove(p, ec);
    dbg::setSink(dbg::Sink::File, p.string(), err);
    f();
    dbg::setSink(dbg::Sink::Stderr, "", err);  // closes + flushes the file
    std::ifstream     in(p, std::ios::binary);
    const std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    fs::remove(p, ec);
    return s;
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

        // A ZERO-byte image (a blank / MOUNT ... CREATE) is refused too, but the message
        // names the mistake -- a fixed geometry cannot make a blank platter -- rather than
        // reporting "0 bytes", which is the size of a file about to be deleted.
        CHECK(!probes(0), "a blank (zero-byte) image is not an HDSK platter either");
        CHECK(err.find("cannot make a blank platter") != std::string::npos,
              "and the error names the mistake, not the 0-byte symptom");

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
    SECTION("88-HDSK -- bad sectors: each kind sets its CSTAT bit (#972)");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        CHECK(!b.takesFaults("drive1") && b.takesFaults("drive0"), "drive0 takes faults; a drive past `drives` does not");
        CHECK(b.faults("drive0") == nullptr, "an empty drive has no table");
        CHECK(b.mount("drive0", "hd.dsk", false, err), err.c_str());
        DiskFaults* f = b.faults("drive0");
        CHECK(f && f->empty(), "a mounted platter has an empty table");
        seek(b, 0, 2);

        // data: the bytes still reach the buffer; the CRC fails.
        CHECK(f->add("2/0/5 data", err), err.c_str());
        markBuffer(b, 0xEE);
        readSector(b, 0, 0, 0, 0, 5);
        CHECK(in(b, CSTAT) == 0x04, "a data fault sets bit 2, CRC error in sector data");
        CHECK(readBuffer(b, 0, 1)[0] == (uint8_t)(2 * 48 + 5), "and the sector's data is in the buffer");
        CHECK(f->list()[0].hits == 1, "the read used the fault once");
        readSector(b, 0, 0, 0, 0, 6);
        CHECK(in(b, CSTAT) == 0x00, "the next sector reads clean");

        // id: nothing moves.
        CHECK(f->add("2/0/5 id", err), err.c_str());
        markBuffer(b, 0xEE);
        readSector(b, 0, 0, 0, 0, 5);
        CHECK(in(b, CSTAT) == 0x08, "an id fault sets bit 3, CRC error in the header, and takes the place of data");
        CHECK(readBuffer(b, 0, 1)[0] == 0xEE, "and the buffer is unchanged");

        // missing: nothing moves, and it comes before id.
        CHECK(f->add("2/0/5 missing", err), err.c_str());
        markBuffer(b, 0xEE);
        readSector(b, 0, 0, 0, 0, 5);
        CHECK(in(b, CSTAT) == 0x10, "a missing fault sets bit 4, header has wrong sector, before id");
        CHECK(readBuffer(b, 0, 1)[0] == 0xEE, "and the buffer is unchanged");
        CHECK(f->list()[0].hits == 1 && f->list()[1].hits == 1 && f->list()[2].hits == 1,
              "each read used only the one fault that won");

        // A not-ready or illegal-sector error comes first and uses no fault.
        CHECK(f->add("*/*/* missing", err), err.c_str());
        readSector(b, 0, 0, 0, 0, 30);
        CHECK(in(b, CSTAT) == 0x02, "an illegal sector is reported, not the fault");
        CHECK(f->list()[3].hits == 0, "and no fault was used");
        f->clearAll();

        // Seek takes no fault.
        CHECK(f->add("3/*/* id", err), err.c_str());
        seek(b, 0, 3);
        CHECK(in(b, CSTAT) == 0x00 && f->list()[0].hits == 0, "a Seek uses no fault");

        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- bad sectors on Write Sector, and the image never changes (#972)");
    {
        std::vector<uint8_t> img((size_t)kPlatter, 0x11);
        MemoryMedia*         seen = nullptr;
        setMediaResolver([&img, &seen](const std::string& p, bool ro, std::string&) {
            auto m = std::make_unique<MemoryMedia>(p, img, ro);
            seen   = m.get();
            return m;
        });
        HdskBoard   b;
        std::string err;
        CHECK(b.mount("drive0", "hd.dsk", false, err), err.c_str());
        DiskFaults* f = b.faults("drive0");
        seek(b, 0, 1);
        const size_t at = (size_t)(1 * 48 + 0 * 24 + 7) * 256;  // cyl 1, side 0, sector 7

        CHECK(f->add("1/0/7 write", err), err.c_str());
        writeSector(b, 0, 7, 0x55);
        CHECK(in(b, CSTAT) == 0x00, "a write fault reports nothing");
        CHECK(seen->bytes()[at] == 0x11, "and the sector is not written");
        f->clearAll();

        CHECK(f->add("1/0/7 id", err), err.c_str());
        writeSector(b, 0, 7, 0x55);
        CHECK(in(b, CSTAT) == 0x08 && seen->bytes()[at] == 0x11, "an id fault: bit 3, not written");
        f->clearAll();

        CHECK(f->add("1/0/7 missing", err), err.c_str());
        writeSector(b, 0, 7, 0x55);
        CHECK(in(b, CSTAT) == 0x10 && seen->bytes()[at] == 0x11, "a missing fault: bit 4, not written");
        f->clearAll();

        CHECK(f->add("1/0/7 data", err), err.c_str());
        writeSector(b, 0, 7, 0x55);
        CHECK(in(b, CSTAT) == 0x00 && seen->bytes()[at] == 0x55, "a data fault does not stop a write");
        CHECK(f->list()[0].hits == 0, "and is not used by it");

        // A read of the data fault never changes the image.
        readSector(b, 0, 0, 0, 0, 7);
        CHECK(in(b, CSTAT) == 0x04 && seen->bytes()[at] == 0x55, "a faulted read leaves the image as it was");

        // A write-protected platter: the controller ignores the write before any fault.
        CHECK(b.mount("drive0", "hd.dsk", true, err), err.c_str());
        f = b.faults("drive0");
        CHECK(f->add("1/0/7 id", err), err.c_str());
        writeSector(b, 0, 7, 0x66);
        CHECK(in(b, CSTAT) == 0x80 && f->list()[0].hits == 0, "write protect wins, and uses no fault");

        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- count=N wears off, so a retry recovers (#972)");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        CHECK(b.mount("drive0", "hd.dsk", false, err), err.c_str());
        DiskFaults* f = b.faults("drive0");
        CHECK(f->add("0/1/3 data count=2", err), err.c_str());
        seek(b, 0, 0);
        readSector(b, 0, 0, 0, 1, 3);
        CHECK(in(b, CSTAT) == 0x04, "try 1 fails");
        readSector(b, 0, 0, 0, 1, 3);
        CHECK(in(b, CSTAT) == 0x04, "try 2 fails");
        readSector(b, 0, 0, 0, 1, 3);
        CHECK(in(b, CSTAT) == 0x00, "try 3 reads clean");
        CHECK(f->list()[0].hits == 2 && f->list()[0].left == 0, "two hits, none left");
        setMediaResolver(openHostFile);
    }

    SECTION("88-HDSK -- DEBUG=fault, the `faults` key, SNAPSHOT and MOUNT (#972)");
    {
        withIndexedDisk();
        HdskBoard   b;
        std::string err;
        CHECK(setProperty(b, "drives", "2", err), err.c_str());
        CHECK(!b.loadSubUnit("drive", {{"unit", "1"}, {"faults", "2/0/0 data"}}, err) && !err.empty(),
              "`faults` with no `mount` is refused");
        CHECK(b.loadSubUnit("drive", {{"unit", "1"}, {"mount", "b.dsk"}, {"faults", "0/0/3 data count=3; 4/*/* missing"}},
                            err),
              err.c_str());
        bool saved = false;
        for (const auto& su : b.subUnits())
            for (const auto& fld : su.fields)
                saved = saved || (fld.key == "faults" && fld.quoted && fld.text == "0/0/3 data count=3; 4/*/* missing");
        CHECK(saved, "CONFIG SAVE writes the `faults` key back, quoted");

        b.ensureDebugChannel();
        CHECK(b.debugChannel()->enable("fault", err), "the fault flag exists");
        seek(b, 0, 0);
        const std::string text = debugCapture([&] { readSector(b, 0, 0, /*platter=*/1, 0, 3); });
        CHECK(text.find("fault drive1 0/0/3 data hit 1, 2 left") != std::string::npos, text.c_str());
        CHECK(b.debugChannel()->disable("fault", err), "the flag goes off");

        StateWriter w;
        b.serialize(w);
        HdskBoard dst;
        CHECK(setProperty(dst, "drives", "2", err), err.c_str());
        CHECK(dst.mount("drive1", "b.dsk", false, err), err.c_str());
        StateReader r(w.data());
        dst.deserialize(r);
        CHECK(r.ok(), "restores cleanly");
        const DiskFaults* f = dst.faults("drive1");
        CHECK(f && f->list().size() == 2 && f->list()[0].hits == 1 && f->list()[0].left == 2,
              "RESTORE brings the faults back with their counts");

        CHECK(b.mount("drive1", "c.dsk", false, err) && b.faults("drive1")->empty(),
              "a new platter in the drive brings no faults");
        b.faults("drive1")->add("0/0/1 data", err);
        CHECK(b.unmount("drive1", err) && b.faults("drive1") == nullptr, "UNMOUNT: no disk, no faults");
        setMediaResolver(openHostFile);
    }
}
