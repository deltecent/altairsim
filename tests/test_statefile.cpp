#include "test.h"

#include "core/crc32.h"
#include "core/statefile.h"

#include <span>
#include <string>
#include <vector>

using namespace altair;

// The serialization primitive behind SNAPSHOT/RESTORE (DESIGN.md 13). Three
// promises: a round trip returns exactly what went in, the bytes are explicitly
// little-endian (so a snapshot reads the same on every target), and a short read
// is DETECTED rather than returning garbage as if it were data.
void test_statefile() {
    SECTION("statefile: round trip");
    {
        StateWriter w;
        w.u8(0x12);
        w.u16(0x3456);
        w.u32(0x89abcdefu);
        w.u64(0x0123456789abcdefull);
        w.boolean(true);
        w.boolean(false);
        w.str("hello");
        std::vector<uint8_t> blob = {1, 2, 3, 4, 5};
        w.blob(blob);
        uint8_t fixed[3] = {0xAA, 0xBB, 0xCC};
        w.raw(fixed, 3);

        StateReader r(w.data());
        CHECK(r.u8() == 0x12, "u8 round trips");
        CHECK(r.u16() == 0x3456, "u16 round trips");
        CHECK(r.u32() == 0x89abcdefu, "u32 round trips");
        CHECK(r.u64() == 0x0123456789abcdefull, "u64 round trips");
        CHECK(r.boolean() == true, "bool true round trips");
        CHECK(r.boolean() == false, "bool false round trips");
        CHECK(r.str() == "hello", "str round trips");
        CHECK(r.blob() == blob, "blob round trips");
        uint8_t got[3] = {0, 0, 0};
        r.raw(got, 3);
        CHECK(got[0] == 0xAA && got[1] == 0xBB && got[2] == 0xCC, "raw round trips");
        CHECK(r.ok(), "reader still ok after reading exactly what was written");
        CHECK(r.remaining() == 0, "and nothing is left over");
    }

    SECTION("statefile: little-endian, byte for byte");
    {
        // The GOLDEN layout -- this is what makes a snapshot cross-platform. If a
        // future edit reorders these bytes, a snapshot from an old build stops
        // loading, and this catches it before a user does.
        StateWriter w;
        w.u32(0x04030201u);
        const auto& d = w.data();
        CHECK(d.size() == 4, "u32 is four bytes");
        CHECK(d[0] == 0x01 && d[1] == 0x02 && d[2] == 0x03 && d[3] == 0x04,
              "u32 is written low byte first");

        StateWriter w2;
        w2.u16(0xBEEF);
        CHECK(w2.data()[0] == 0xEF && w2.data()[1] == 0xBE, "u16 is low byte first");

        StateWriter w3;
        w3.str("AB");
        // u32 length (2, LE) then the bytes.
        CHECK(w3.data().size() == 6, "str is 4-byte length + 2 bytes");
        CHECK(w3.data()[0] == 2 && w3.data()[4] == 'A' && w3.data()[5] == 'B',
              "str length prefix then payload");
    }

    SECTION("statefile: a short read is detected");
    {
        StateWriter w;
        w.u32(0xDEADBEEFu);
        // Hand the reader one byte too few.
        StateReader r(w.data().data(), 3);
        (void)r.u32();
        CHECK(!r.ok(), "reading a u32 from 3 bytes latches an error");
        CHECK(!r.error().empty(), "and it says why");

        // Once bad, it stays bad and every read is a harmless zero.
        StateReader r2(w.data());
        CHECK(r2.u32() == 0xDEADBEEFu, "the good read works");
        CHECK(r2.u8() == 0, "the over-read returns 0");
        CHECK(!r2.ok(), "and is flagged");
    }

    SECTION("statefile: a truncated string does not over-read");
    {
        // A length that claims more bytes than are present must not read past the end.
        StateWriter w;
        w.u32(1000);  // claim a 1000-byte string...
        w.raw((const uint8_t*)"AB", 2);  // ...but only 2 bytes follow
        StateReader r(w.data());
        std::string s = r.str();
        CHECK(!r.ok(), "an oversized length prefix is caught");
        CHECK(s.empty(), "and yields nothing rather than reading past the buffer");
    }

    SECTION("statefile: crc32 detects a flipped bit");
    {
        StateWriter w;
        w.str("the quick brown fox");
        w.u64(0x0123456789abcdefull);
        uint32_t good = crc32(std::span<const uint8_t>(w.data().data(), w.data().size()));

        std::vector<uint8_t> tampered = w.data();
        tampered[5] ^= 0x01;  // flip one bit
        uint32_t bad = crc32(std::span<const uint8_t>(tampered.data(), tampered.size()));
        CHECK(good != bad, "a one-bit change changes the checksum");
    }
}
