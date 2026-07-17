#include "test.h"

#include "core/hex.h"

#include <cstring>

using namespace altair;

static std::span<const uint8_t> sv(const char* s) {
    return std::span<const uint8_t>((const uint8_t*)s, std::strlen(s));
}

void test_hex() {
    SECTION("Intel HEX (DESIGN.md 10.3)");

    {
        Image img;
        std::string err;
        CHECK(loadHex(sv(":03FF0000C3002C0F\n:00000001FF\n"), img, err), "a good file loads");
        CHECK(img.size() == 3, "3 bytes");
        CHECK(img.lo() == 0xFF00, "placed at FF00 by the file itself");
        CHECK(img.bytes[0xFF00] == 0xC3, "C3");
        CHECK(img.bytes[0xFF02] == 0x2C, "2C");
    }

    {
        // A silently truncated load is a miserable bug to chase, and the whole
        // point of a checksum is to not have that bug. So a bad record FAILS the
        // load and NAMES the record.
        Image img;
        std::string err;
        CHECK(!loadHex(sv(":03FF0000C3002C00\n:00000001FF\n"), img, err), "a bad checksum FAILS");
        CHECK(err.find("record 1") != std::string::npos, "and it names the record");
        CHECK(err.find("checksum") != std::string::npos, "and says it was the checksum");
    }

    {
        Image img;
        std::string err;
        CHECK(!loadHex(sv(":05FF0000C30F\n"), img, err), "a length that lies about the record FAILS");
    }

    {
        // Round-trip is a test case, not an aspiration.
        Image a;
        for (uint32_t i = 0; i < 300; ++i) a.bytes[0x100 + i] = (uint8_t)(i * 7 + 1);
        a.bytes[0xF000] = 0xAB;  // a deliberate gap, so sparseness must survive
        std::string text = saveHex(a);

        Image b;
        std::string err;
        CHECK(loadHex(sv(text.c_str()), b, err), "saveHex output re-loads");
        CHECK(a.bytes == b.bytes, "round-trip is byte-for-byte, gaps and all");
        CHECK(!b.contiguous(), "and the gap is still a gap -- we do not invent bytes");
    }

    CHECK(looksLikeHex(sv(":00000001FF")), "autodetect: hex");
    CHECK(!looksLikeHex(sv("\xC3\x00\x2C")), "autodetect: binary");

    SECTION("relocateTo -- AT on a file that carries its own addresses");
    {
        // The FIRST DATA RECORD is the anchor, and everything moves with it by the same
        // delta. AT means "put it here", the same as it means for a flat binary.
        Image img;
        std::string err;
        CHECK(loadHex(sv(":02010000AABB98\n:00000001FF\n"), img, err), "a record at 0100");
        CHECK(img.hasFirst && img.first == 0x0100, "the parser remembered where it started");

        relocateTo(img, 0x0200);
        CHECK(img.bytes.count(0x0200) && img.bytes[0x0200] == 0xAA, "AT 200 moves it to 0200");
        CHECK(img.bytes.count(0x0201) && img.bytes[0x0201] == 0xBB, "...and it moves as a unit");
        CHECK(!img.bytes.count(0x0100), "...and does not stay behind");
        CHECK(img.first == 0x0200, "the anchor moves too, so relocating twice is not cumulative");
    }
    {
        // ANCHORED TO THE FIRST RECORD, NOT THE LOWEST ADDRESS. On an ascending file
        // those are the same byte -- which is exactly why anchoring to the wrong one
        // would be invisible until the day it was not. This file DESCENDS.
        Image img;
        std::string err;
        CHECK(loadHex(sv(":01020000AA53\n:0101000055A9\n:00000001FF\n"), img, err),
              "0200 first, then 0100");
        CHECK(img.first == 0x0200, "first == the first RECORD (0200)...");
        CHECK(img.lo() == 0x0100, "...which is NOT the lowest address (0100)");

        relocateTo(img, 0x0500);  // delta = +0300, anchored on 0200
        CHECK(img.bytes.count(0x0500) && img.bytes[0x0500] == 0xAA,
              "the FIRST record lands on AT...");
        CHECK(img.bytes.count(0x0400) && img.bytes[0x0400] == 0x55,
              "...and the earlier-addressed one lands BELOW it. Anchor is file order.");
    }
    {
        // THE DELTA WRAPS, MODULO 64K -- the format's own arithmetic ("[DRLO + DRI] MOD
        // 64K"), and the address bus's: an 8080 has no seventeenth address line to carry
        // into. Patrick, 2026-07-17: "this has to do the math to wrap from 0000 back to
        // FFFF by AND 0xFFFF."
        Image img;
        std::string err;
        CHECK(loadHex(sv(":01F00000AA65\n:01E00000BB64\n:00000001FF\n"), img, err),
              "F000 first, then E000");
        relocateTo(img, 0x0000);  // delta = -F000

        CHECK(img.bytes.count(0x0000) && img.bytes[0x0000] == 0xAA, "F000 -> 0000, as asked");
        CHECK(img.bytes.count(0xF000) && img.bytes[0xF000] == 0xBB,
              "and E000 wraps to F000 -- round the top, not off the end");
        CHECK(img.bytes.size() == 2, "two bytes in, two bytes out: nothing fell off");
    }
    {
        // The start address is part of the program, so it relocates with it.
        Image img;
        std::string err;
        CHECK(loadHex(sv(":02010000AABB98\n:0400000300000100F8\n:00000001FF\n"), img, err),
              "a file with a start address");
        CHECK(img.hasStart && img.start == 0x0100, "start is 0100");
        relocateTo(img, 0x0300);
        CHECK(img.start == 0x0300, "and it moves with the code -- it points AT the code");
    }
    {
        Image bin;
        loadBin(sv("\x01\x02"), 0x0400, bin);
        CHECK(bin.hasFirst && bin.first == 0x0400,
              "a flat binary's first byte is where you put it -- so AT is not special-cased");
    }
}
