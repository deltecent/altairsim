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
}
