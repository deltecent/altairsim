#include "test.h"

#include "core/hex.h"

#include <cstring>

using namespace altair;

static std::span<const uint8_t> sv(const char* s) {
    return std::span<const uint8_t>((const uint8_t*)s, std::strlen(s));
}

void test_srec() {
    SECTION("Motorola S-record -- the Altair 680b's world, not Intel HEX");

    {
        // S0 header (skipped), one S1 data record at 0100, S9 term carrying the
        // entry address. Checksums are the one's-complement of the running sum.
        Image img;
        std::string err;
        CHECK(loadSrec(sv("S00600004844521B\n"
                          "S1060100C3002C09\n"
                          "S9030100FB\n"),
                       img, err),
              "a good file loads");
        CHECK(img.size() == 3, "3 data bytes -- the S0 header contributed none");
        CHECK(img.lo() == 0x0100, "placed at 0100 by the record itself");
        CHECK(img.bytes[0x0100] == 0xC3, "C3");
        CHECK(img.bytes[0x0102] == 0x2C, "2C");
        CHECK(img.hasFirst && img.first == 0x0100, "the first data record is the AT anchor");
        CHECK(img.hasStart && img.start == 0x0100, "S9 carried the entry address 0100");
    }

    {
        // A bad record FAILS the load and NAMES the record -- same contract as hex.
        // (Record 2 here: the S0 header is record 1.)
        Image img;
        std::string err;
        CHECK(!loadSrec(sv("S00600004844521B\n"
                           "S1060100C3002C00\n"),
                        img, err),
              "a bad checksum FAILS");
        CHECK(err.find("record 2") != std::string::npos, "and it names the record");
        CHECK(err.find("checksum") != std::string::npos, "and says it was the checksum");
    }

    {
        // The count byte must match what the record actually holds.
        Image img;
        std::string err;
        CHECK(!loadSrec(sv("S1080100C3002C09\n"), img, err),
              "a count that lies about the record FAILS");
    }

    {
        // Ctrl-Z (0x1A) soft-EOF padding is tolerated, exactly as loadHex tolerates it.
        Image img;
        std::string err;
        CHECK(loadSrec(sv("S1060100C3002C09\r\nS9030000FC\r\n\x1A\x1A\x1A"), img, err),
              "trailing Ctrl-Z padding is a soft-EOF, not a parse error");
        CHECK(img.size() == 3, "and the real records still loaded");
        CHECK(img.bytes[0x0100] == 0xC3, "C3");
    }

    {
        // Round-trip is a test case, not an aspiration.
        Image a;
        for (uint32_t i = 0; i < 300; ++i) a.bytes[0x100 + i] = (uint8_t)(i * 7 + 1);
        a.bytes[0xF000] = 0xAB;  // a deliberate gap, so sparseness must survive
        a.hasStart = true;
        a.start = 0x0100;
        std::string text = saveSrec(a);

        Image b;
        std::string err;
        CHECK(loadSrec(sv(text.c_str()), b, err), "saveSrec output re-loads");
        CHECK(a.bytes == b.bytes, "round-trip is byte-for-byte, gaps and all");
        CHECK(!b.contiguous(), "and the gap is still a gap -- we do not invent bytes");
        CHECK(b.hasStart && b.start == 0x0100, "the S9 start survives the round-trip");
    }

    SECTION("S-record autodetect");
    CHECK(looksLikeSrec(sv("S1060100C3002C09")), "autodetect: S-record");
    CHECK(!looksLikeSrec(sv(":00000001FF")), "an Intel HEX file is not an S-record");
    CHECK(!looksLikeSrec(sv("\xC3\x00\x2C")), "a flat binary is not an S-record");
    CHECK(!looksLikeHex(sv("S1060100C3002C09")), "...and hex autodetect stays clear of it");
}
