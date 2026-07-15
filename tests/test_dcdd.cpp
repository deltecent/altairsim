// The 88-DCDD (docs/boards/mits-dcdd.md).
//
// THE BOOT IS THE ACCEPTANCE TEST AND IT PASSES -- CP/M 2.2b comes up to `A>` off a
// real period image and DIR lists the disk. So why this file?
//
// BECAUSE CP/M BOOTED WHILE THE CARD WAS WRONG.
//
// The first version of this board made SECTOR TRUE last for the whole inter-sector
// gap -- 1,648 T-states. The manual says it is a 30 microsecond one-shot: SIXTY. A
// factor of twenty-seven, and CP/M booted perfectly either way, because a window that
// is too generous is FORGIVING. The guest never complains, so nothing ever tells you,
// and you have shipped a card that excuses exactly the races the real one punished.
//
// That is what this file is for. Every test below pins a number or a sense that the
// boot CANNOT catch -- the ones that fail silently, or fail only on the disk you
// haven't written yet. The quirks table in the board's .md is the list, and these are
// its teeth.
//
// No filesystem: MemoryMedia through setMediaResolver. A board test that needs a real
// file has usually confused itself with an integration test.

#include "boards/mits-88dcdd.h"
#include "core/clock.h"
#include "host/media.h"
#include "test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace altair;

namespace {

// ---- The bus, reduced to what a card actually sees ----
uint8_t in(DcddBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(DcddBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// Hand the board a disk of exactly `bytes` bytes, with no host filesystem anywhere.
void withDisk(uint64_t bytes, uint8_t fill = 0) {
    setMediaResolver([bytes, fill](const std::string& path, bool ro, std::string&) {
        return std::make_unique<MemoryMedia>(path, std::vector<uint8_t>((size_t)bytes, fill), ro);
    });
}

// The card as the BIOS finds it: drive 0 selected, head loaded.
void ready(DcddBoard& b) {
    out(b, 0x08, 0x00);  // select drive 0
    out(b, 0x09, 0x04);  // cHDLOAD
}

constexpr uint64_t kPerSector = 10416;  // 360 RPM / 32 sectors @ 2 MHz
constexpr uint64_t kByte      = 64;     // 32 us -- 250 kbit/s
constexpr uint64_t kTrue      = 60;     // 30 us -- the F4 one-shot
constexpr uint64_t kReadStart = 280;    // 140 us -- the READ CLEAR one-shot

} // namespace

void test_dcdd() {
    SECTION("88-DCDD -- status is INVERTED, and that is the first thing to get right");
    {
        Clock      c;
        DcddBoard  b;
        b.attachClock(&c);

        // Nothing selected: every flag false, and false reads as ONE.
        CHECK(in(b, 0x08) == 0xFF, "with no drive selected the status floats to FF");

        withDisk(337568);
        std::string err;
        CHECK(b.mount("drive0", "a.dsk", false, err), "an 8-inch disk mounts");

        out(b, 0x08, 0x00);  // select drive 0
        uint8_t s = in(b, 0x08);

        // The card is enabled, the head is on track 0, movement is allowed. All THREE
        // are true -- so all three read ZERO.
        CHECK(!(s & 0x08), "sDSKEN true -> bit 3 reads 0 (the manual calls it `not used, = 0`;");
        CHECK(!(s & 0x40), "sTRACK0 true -> bit 6 reads 0     DBL calls it ENABLD. Same fact.)");
        CHECK(!(s & 0x02), "sMOVEOK true -> bit 1 reads 0");

        // The head is NOT loaded yet, so HDSTAT is false -- and false is ONE.
        CHECK(s & 0x04, "sHDSTAT false -> bit 2 reads 1. An inverted bus reads FALSE as high.");

        // The bit nobody uses, which is exactly why it is easy to get wrong.
        CHECK(!(s & 0x10), "bit 4 reads ZERO on an enabled card -- `D4 - Not Used, = 0`");

        out(b, 0x09, 0x04);  // cHDLOAD
        CHECK(!(in(b, 0x08) & 0x04), "and once the head loads, HDSTAT goes true -> bit 2 reads 0");
    }

    SECTION("88-DCDD -- the disk turns on its own. Reading the port does NOT turn it.");
    {
        // THE BUG THIS CARD WAS BUILT TO MAKE IMPOSSIBLE. SIMH advances the sector
        // counter every second read of port 0x09, and the board's .md copied that and
        // called it "the entire rotating-disk simulation". It makes the platter spin at
        // the speed of whatever loop is polling it: a tight BIOS outruns a slow one, and
        // a recorded session stops replaying identically.
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        uint8_t first = in(b, 0x09);
        bool    same  = true;
        for (int i = 0; i < 1000; ++i)
            if (in(b, 0x09) != first) same = false;

        CHECK(same, "a thousand reads of the sector port return the SAME sector");
        CHECK(c.now() == 0, "and cost no time -- the card does not advance the clock either");

        // Now let TIME pass, and it moves.
        c.advance(kPerSector);
        CHECK(((in(b, 0x09) >> 1) & 0x1F) == 1, "one sector-time later the head is over sector 1");
        c.advance(kPerSector * 31);
        CHECK(((in(b, 0x09) >> 1) & 0x1F) == 0, "and 31 more wraps it to 0 -- 32 sectors, not 33");
    }

    SECTION("88-DCDD -- SECTOR TRUE is a 30us one-shot, NOT the inter-sector gap");
    {
        // The one that booted while wrong by 27x. If this test ever starts passing with
        // a wider window, someone has "fixed" the card into forgiving races the real one
        // punished -- and CP/M will boot, and nothing will tell you.
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        auto sectorTrue = [&] { return (in(b, 0x09) & 0x01) == 0; };  // ACTIVE LOW

        CHECK(sectorTrue(), "at the sector hole, sector-true is asserted");

        c.advance(kTrue - 1);
        CHECK(sectorTrue(), "it is still asserted one T-state before 30us");

        c.advance(1);
        CHECK(!sectorTrue(), "and it DROPS at exactly 30us (60 T) -- the F4 74123 one-shot");

        // And it stays dropped for the whole rest of the sector. THIS is the assertion
        // that fails if anyone widens it to the gap.
        bool stayedLow = true;
        for (uint64_t t = kTrue; t < kPerSector - 1; t += 37)  // 37: prime, so it lands everywhere
            if ((in(b, 0x09) & 0x01) == 0) stayedLow = false;
        CHECK(stayedLow, "and stays dropped for the other 10,356 T of the sector (0.58% duty)");

        // Sixty T-states against the BIOS's 24-T `in`/`rar`/`jc` loop: about 2.5 spins
        // of margin. Tight ON PURPOSE -- the manual tells the programmer "the write mode
        // should begin AS CLOSE AS POSSIBLE to the time that D0 goes true."
        CHECK(kTrue / 24 >= 2, "and 60 T still gives a 24-T poll loop two chances to see it");
    }

    SECTION("88-DCDD -- the sector byte is laid out so that ONE `RAR` does everything");
    {
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        c.advance(kPerSector * 5 + 100);  // sector 5, past the sector-true window
        uint8_t v = in(b, 0x09);

        CHECK((v & 0xC0) == 0xC0, "bits 7 and 6 read as 1");
        CHECK(((v >> 1) & 0x1F) == 5, "the sector number sits in bits 5..1");
        CHECK((v & 0x01) == 1, "and bit 0 is sector-true, which is 1 = NOT true");

        // Now do what the BIOS does, and prove the layout is FOR this:
        //     dnLoop  in DRVSEC / rar / jc dnLoop / ani SECMASK
        // RAR drops bit 0 into carry AND shifts the sector down into bits 4..0.
        bool    carry = v & 0x01;
        uint8_t a     = (uint8_t)(v >> 1);  // (carry-in is irrelevant to the low 5 bits)
        CHECK(carry, "RAR puts sector-true into CARRY -- so `jc` spins while it is false");
        CHECK((a & 0x1F) == 5, "and the SAME instruction leaves the sector in bits 4..0 for `ani 1Fh`");
    }

    SECTION("88-DCDD -- NRDA paces the read at 250 kbit/s, and the guest cannot outrun it");
    {
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        auto nrda = [&] { return (in(b, 0x08) & 0x80) == 0; };  // inverted: true reads 0

        CHECK(!nrda(), "at the sector hole there is no data yet -- the read path is held cleared");

        c.advance(kReadStart - 1);
        CHECK(!nrda(), "still nothing one T before 140us");

        c.advance(1);
        CHECK(nrda(), "the first byte lands at 140us (the F1 READ CLEAR one-shot)");

        in(b, 0x0A);  // take it
        CHECK(!nrda(), "take it and NRDA drops -- the disk has not turned past another one");

        c.advance(kByte);
        CHECK(nrda(), "32us later the next byte is under the head (250,000 bits/sec)");

        // The byte clock is the MEDIUM's, not the CPU's.
        Clock     fast;
        fast.setHz(4000000);
        DcddBoard b2;
        b2.attachClock(&fast);
        withDisk(337568);
        b2.mount("drive0", "a.dsk", false, err);
        ready(b2);
        fast.advance(560 - 1);  // 140us at 4 MHz is 560 T, not 280
        CHECK((in(b2, 0x08) & 0x80) != 0, "at 4 MHz the first byte has NOT arrived at 280 T...");
        fast.advance(1);
        CHECK((in(b2, 0x08) & 0x80) == 0, "...it arrives at 560 T. 140us is 140us. The motor does");
        CHECK(true, "  not care what crystal the CPU has.");
    }

    SECTION("88-DCDD -- the probe, and the XMODEM pad that rejects both real 8-inch disks");
    {
        std::string err;
        auto probes = [&](uint64_t bytes) {
            Clock     c;
            DcddBoard b;
            b.attachClock(&c);
            withDisk(bytes);
            return b.mount("drive0", "a.dsk", false, err);
        };

        CHECK(probes(337568), "77 x 32 x 137 = 337,568 is an 8-inch disk");
        CHECK(probes(337664), "AND SO IS 337,664 -- the XMODEM pad. Both real 8-inch images in");
        CHECK(true, "  the tree are this size, and `size == 337568` REJECTS BOTH OF THEM.");
        CHECK(probes(337568 + 127), "the tolerance is the whole 128-byte block...");
        CHECK(!probes(337568 + 128), "...and not one byte more -- 128 over is a DIFFERENT disk");

        CHECK(probes(8978432), "2048 x 32 x 137 = 8,978,432 is the FDC+ 8 MB drive");
        CHECK(!probes(123456), "and a size that is no format at all is an error, not a guess");

        // THE MINIDISK IS NOT A MEDIUM OF THIS CARD, and this is the tripwire that keeps it
        // from coming back. It used to probe here -- one row in dcddFormats() -- and a card
        // that accepted it turned the platter at 360 RPM instead of 300 and clocked bytes at
        // twice the real rate. A 5.25" minidisk goes in an 88-MDS (tests/test_mds.cpp).
        CHECK(!probes(76720), "76,720 is a MINIDISK, and it is NOT an 8-inch controller's disk");
    }

    SECTION("88-DCDD -- sectors are numbered FROM ZERO (the Tarbell's are not)");
    {
        // DESIGN.md 7.3: "exactly the off-by-one that silently corrupts a disk." Both
        // cards can sit in one machine with both conventions live, so neither may
        // assume. Sector 0 must be a REAL, ADDRESSABLE sector -- at offset 0.
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);

        // A disk whose every byte says which sector it is: fill sector N with N.
        std::vector<uint8_t> img((size_t)337568, 0);
        for (int s = 0; s < 32; ++s)
            std::memset(&img[(size_t)s * 137], (uint8_t)(0xA0 + s), 137);
        setMediaResolver([&img](const std::string& p, bool ro, std::string&) {
            return std::make_unique<MemoryMedia>(p, img, ro);
        });

        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        // Sit on sector 0 and read its first byte.
        c.advance(kReadStart);
        CHECK(((in(b, 0x09) >> 1) & 0x1F) == 0, "the head is over sector 0 at t=0...");
        CHECK(in(b, 0x0A) == 0xA0, "...and sector 0 is the FIRST 137 bytes of the image, not the second");

        // And sector 1 is the next slot along, not the first.
        c.advance(kPerSector);
        c.advance(0);
        CHECK(in(b, 0x0A) == 0xA1, "sector 1 is the second slot -- startSector = 0, all the way down");
    }

    SECTION("88-DCDD -- a write is 137 bytes, and a SHORT one is padded with the LAST byte");
    {
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568, 0xEE);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        // A SYSTEM SECTOR IS 133 BYTES AND NEVER REACHES 137. A card that waited for the
        // 137th byte before committing would lose every system sector ever written --
        // and you would only find out on a disk you had booted from.
        out(b, 0x09, 0x80);  // cWRTEN, at the top of sector 0
        for (int i = 0; i < 132; ++i) out(b, 0x0A, (uint8_t)(0x10 + i));
        out(b, 0x0A, 0x00);  // the 133rd byte: software's trailing zero

        out(b, 0x09, 0x01);  // cSTEPI -- steps away, which must FLUSH FIRST

        // Come back and read what actually landed.
        out(b, 0x09, 0x02);  // step back out to track 0
        c.advance(kReadStart);
        CHECK(((in(b, 0x09) >> 1) & 0x1F) == 0, "back on sector 0");
        c.advance(0);

        CHECK(in(b, 0x0A) == 0x10, "byte 0 of the short write is there");
        for (int i = 1; i < 132; ++i) { c.advance(kByte); in(b, 0x0A); }
        c.advance(kByte);
        CHECK(in(b, 0x0A) == 0x00, "byte 132 is software's trailing zero");

        // THE TAIL. The manual: "write circuit will continue writing LAST BYTE OUTPUTTED
        // to the end of that sector." The zero is not a terminator -- it is the FILL.
        c.advance(kByte);
        CHECK(in(b, 0x0A) == 0x00, "and byte 133 is that same zero, re-clocked by the shift register");
        c.advance(kByte);
        CHECK(in(b, 0x0A) == 0x00, "and 134...");
        CHECK(true, "  (software writes the zero, so the bytes match either way -- but the CARD's");
        CHECK(true, "   rule is `repeat the last byte`, and that is what is implemented.)");
    }

    SECTION("88-DCDD -- a step FLUSHES before it invalidates. Reverse them and disks rot.");
    {
        // The doc's quirks table says this one SILENTLY CORRUPTS DISKS, and it is the
        // subtlest row in it: the pending write belongs to the track we are LEAVING, so
        // the flush needs the position that the invalidation is about to destroy.
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568, 0xEE);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        out(b, 0x09, 0x80);                                     // arm the write on TRACK 0
        for (int i = 0; i < 137; ++i) out(b, 0x0A, 0xC3);       // a full sector of C3
        out(b, 0x09, 0x01);                                     // step to track 1

        // Track 1 must be untouched. If the flush ran AFTER the step invalidated the
        // position, this sector would carry the bytes meant for track 0.
        c.advance(kReadStart);
        CHECK(in(b, 0x0A) == 0xEE, "the write landed on track 0 -- track 1 is still blank");

        out(b, 0x09, 0x02);  // back to track 0
        c.advance(kPerSector - kReadStart);
        c.advance(kReadStart);
        CHECK(((in(b, 0x09) >> 1) & 0x1F) == 1, "and on track 0...");
        CHECK(true, "  (the flush went to the track the head was on when the bytes arrived.)");
    }

    SECTION("88-DCDD -- RESET keeps the disks in the drives and does NOT home the head");
    {
        // DESIGN.md 6.1: "a board does on a reset exactly what the real board does." A
        // real drive does not move its head because the CPU was reset, and a warm reset
        // that homed every drive would be inventing a convenience the hardware never had.
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(337568);
        std::string err;
        b.mount("drive0", "a.dsk", false, err);
        ready(b);

        out(b, 0x09, 0x01);
        out(b, 0x09, 0x01);
        out(b, 0x09, 0x01);  // three tracks in
        CHECK(in(b, 0x08) & 0x40, "not on track 0 any more -- TRACK0 false reads 1");

        b.reset(Reset::Bus);

        CHECK(in(b, 0x08) == 0xFF, "after RESET no drive is selected");

        auto u = b.units();
        CHECK(u.size() == 4 && u[0].state == "a.dsk", "but the DISK IS STILL IN THE DRIVE");

        out(b, 0x08, 0x00);  // reselect
        CHECK(in(b, 0x08) & 0x40, "and the head is STILL ON TRACK 3 -- a reset does not seek");
        CHECK(!(in(b, 0x08) & 0x04) == false, "and the head is unloaded");
    }

    SECTION("88-DCDD -- [[board.drive]] survives a CONFIG SAVE");
    {
        // Before Board::subUnits() existed this board would have LOADED AND SILENTLY NOT
        // SAVED: you would configure four drives, save, and get a controller with none.
        Clock     c;
        DcddBoard b;
        b.attachClock(&c);
        withDisk(8978432);

        std::string err;
        CHECK(b.loadSubUnit("drive", {{"unit", "0"}, {"mount", "m.dsk"}, {"media", "fdc8mb"}}, err),
              "a [[board.drive]] with a forced media loads");

        // ...and the media this card does NOT have is refused BY NAME, rather than quietly
        // probing into a wrong geometry. `minidisk` is an 88-MDS word now.
        DcddBoard bad;
        bad.attachClock(&c);
        std::string e2;
        CHECK(!bad.loadSubUnit("drive", {{"unit", "0"}, {"media", "minidisk"}}, e2),
              "`media = \"minidisk\"` on a dcdd is an ERROR -- it is a different controller");
        CHECK(e2.find("minidisk") != std::string::npos && e2.find("dcdd") != std::string::npos,
              "and the error names both the media and the card that will not take it");

        auto su = b.subUnits();
        CHECK(su.size() == 1, "an empty drive writes NO table -- four drives and one disk saves as one");
        CHECK(su[0].table == "drive", "and the one it does write is a `drive`");

        // Feed our own output straight back in. That is the whole claim, executed.
        DcddBoard b2;
        b2.attachClock(&c);
        KeyValues kv;
        for (const auto& f : su[0].fields) kv.push_back({f.key, f.text});
        CHECK(b2.loadSubUnit("drive", kv, err), "and it loads straight back into a fresh board");
        CHECK(b2.units()[0].state == "m.dsk", "with the same disk in the same drive");

        setMediaResolver(openHostFile);  // put the real filesystem back
    }
}
