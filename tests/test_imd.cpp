#include "test.h"

#include "host/imd.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

// ---- synthetic IMD builders -----------------------------------------------------------
// An IMD is bytes, so a test is bytes. These append the exact on-disk shapes the parser
// reads: an ASCII header ending in 0x1A, then one track record per putTrack().

void putHeader(std::vector<uint8_t>& b, const std::string& text) {
    b.insert(b.end(), text.begin(), text.end());
    b.push_back(0x1A);
}

struct Sec {
    uint8_t type;  // 0x00 unavailable, 0x01 normal, 0x02 compressed
    uint8_t fill;  // normal: the byte the whole sector holds; compressed: the fill byte
};

// mode, cyl, head, sizeCode; `ids` is the numbering map (physical order); `secs` is one
// data record per physical sector, in the SAME order as `ids`.
void putTrack(std::vector<uint8_t>& b, uint8_t mode, uint8_t cyl, uint8_t head, uint8_t szc,
              const std::vector<uint8_t>& ids, const std::vector<Sec>& secs) {
    const int size = 128 << szc;
    b.push_back(mode);
    b.push_back(cyl);
    b.push_back(head);
    b.push_back((uint8_t)ids.size());
    b.push_back(szc);
    b.insert(b.end(), ids.begin(), ids.end());
    for (const Sec& s : secs) {
        b.push_back(s.type);
        if (s.type == 0x01)
            b.insert(b.end(), (size_t)size, s.fill);
        else if (s.type == 0x02)
            b.push_back(s.fill);
        // 0x00 (unavailable): no bytes follow
    }
}

// A track of `n` normal sectors, IDs 1..n, each filled with a distinct byte via fill(id).
std::vector<Sec> ramp(int n, uint8_t (*fill)(int)) {
    std::vector<Sec> v;
    for (int i = 1; i <= n; ++i) v.push_back(Sec{0x01, fill(i)});
    return v;
}

bool bytesAre(const std::vector<uint8_t>& v, size_t lo, size_t hi, uint8_t val) {
    if (hi > v.size()) return false;
    for (size_t i = lo; i < hi; ++i)
        if (v[i] != val) return false;
    return true;
}

// Deciders for the head-order callback.
auto yes = [](uint64_t) { return true; };
auto no  = [](uint64_t) { return false; };

} // namespace

void test_imd() {
    SECTION("imd: single-density one-sector round-trip");
    {
        std::vector<uint8_t> imd;
        putHeader(imd, "IMD test 1.0");
        putTrack(imd, /*mode=*/0, /*cyl=*/0, /*head=*/0, /*szc=*/0, {1}, {{0x01, 0xAB}});

        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;
        CHECK(convertImdToRaw(imd, raw, info, err, yes), "converts");
        CHECK(raw.size() == 128, "one 128-byte sector");
        CHECK(bytesAre(raw, 0, 128, 0xAB), "payload preserved");
        CHECK(info.rawBytes == 128, "rawBytes == emitted size");
        CHECK(info.heads == 1, "single-sided");
        CHECK(info.description == "IMD test 1.0", "header text captured");
        CHECK(info.tracks.size() == 1, "one coalesced track line");
    }

    SECTION("imd: de-interleave -- payloads land in ascending sector-ID order");
    {
        // Physical order {1,3,5,2,4,6}; each sector filled with its own ID. After the
        // de-interleave the raw image must be 1,2,3,4,5,6 by 128-byte block.
        std::vector<uint8_t> imd;
        putHeader(imd, "");
        std::vector<uint8_t> ids = {1, 3, 5, 2, 4, 6};
        std::vector<Sec>     secs;
        for (uint8_t id : ids) secs.push_back(Sec{0x01, id});
        putTrack(imd, 3, 0, 0, 0, ids, secs);

        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;
        CHECK(convertImdToRaw(imd, raw, info, err, yes), "converts");
        CHECK(raw.size() == 6u * 128, "six sectors");
        bool ordered = true;
        for (int k = 0; k < 6; ++k)
            ordered = ordered && bytesAre(raw, (size_t)k * 128, (size_t)(k + 1) * 128, (uint8_t)(k + 1));
        CHECK(ordered, "block k holds sector ID k+1 -- interleave undone");
    }

    SECTION("imd: compressed sector expands to N fill bytes");
    {
        std::vector<uint8_t> imd;
        putHeader(imd, "");
        putTrack(imd, 3, 0, 0, /*szc=*/1 /*256*/, {1}, {{0x02, 0x77}});

        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;
        CHECK(convertImdToRaw(imd, raw, info, err, yes), "converts");
        CHECK(raw.size() == 256, "256-byte sector from one stored byte");
        CHECK(bytesAre(raw, 0, 256, 0x77), "expanded to the fill byte");
    }

    SECTION("imd: unavailable sector fills 0xE5");
    {
        std::vector<uint8_t> imd;
        putHeader(imd, "");
        putTrack(imd, 3, 0, 0, 0, {1}, {{0x00, 0x00}});

        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;
        CHECK(convertImdToRaw(imd, raw, info, err, yes), "converts");
        CHECK(raw.size() == 128, "a full sector's worth");
        CHECK(bytesAre(raw, 0, 128, 0xE5), "CP/M blank fill");
    }

    SECTION("imd: two-sided head order follows the decider");
    {
        // 2 cyls x 2 heads, one sector each; fill = cyl*10 + head, so the emit order is
        // legible: interleaved is C0H0,C0H1,C1H0,C1H1; head-major is C0H0,C1H0,C0H1,C1H1.
        auto build = [] {
            std::vector<uint8_t> imd;
            putHeader(imd, "sides");
            for (uint8_t c = 0; c < 2; ++c)
                for (uint8_t h = 0; h < 2; ++h)
                    putTrack(imd, 3, c, h, 0, {1}, {{0x01, (uint8_t)(c * 10 + h)}});
            return imd;
        };

        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;

        CHECK(convertImdToRaw(build(), raw, info, err, yes), "converts (interleaved)");
        CHECK(info.heads == 2, "double-sided");
        CHECK(info.interleaved, "info records the interleaved order");
        CHECK(raw[0] == 0 && raw[128] == 1 && raw[256] == 10 && raw[384] == 11,
              "interleaved: C0H0,C0H1,C1H0,C1H1");

        CHECK(convertImdToRaw(build(), raw, info, err, no), "converts (head-major)");
        CHECK(!info.interleaved, "info records the head-major order");
        CHECK(raw[0] == 0 && raw[128] == 10 && raw[256] == 1 && raw[384] == 11,
              "head-major: all H0 then all H1");
    }

    SECTION("imd: the decider is not consulted for a single-sided disk");
    {
        std::vector<uint8_t> imd;
        putHeader(imd, "");
        putTrack(imd, 0, 0, 0, 0, {1}, {{0x01, 0x01}});

        bool asked = false;
        auto spy   = [&](uint64_t) { asked = true; return true; };
        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;
        CHECK(convertImdToRaw(imd, raw, info, err, spy), "converts");
        CHECK(!asked, "one head -> ordering is moot, controller never asked");
    }

    SECTION("imd: malformed files are refused, not half-converted");
    {
        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;

        std::vector<uint8_t> noEof = {'I', 'M', 'D'};  // never a 0x1A
        CHECK(!convertImdToRaw(noEof, raw, info, err, yes), "no header terminator -> false");
        CHECK(!err.empty(), "...with a reason");

        std::vector<uint8_t> truncated;
        putHeader(truncated, "");
        truncated.push_back(3);
        truncated.push_back(0);  // a track header that stops mid-record
        CHECK(!convertImdToRaw(truncated, raw, info, err, yes), "truncated track header -> false");

        std::vector<uint8_t> mixed;
        putHeader(mixed, "");
        mixed.push_back(3);     // mode
        mixed.push_back(0);     // cyl
        mixed.push_back(0);     // head
        mixed.push_back(1);     // sectors
        mixed.push_back(0xFF);  // sizeCode 0xFF -- per-sector size table, unsupported
        CHECK(!convertImdToRaw(mixed, raw, info, err, yes), "sizeCode 0xFF -> false");
    }

    SECTION("imd: ImdInfo coalesces a uniform disk and totals the bytes");
    {
        std::vector<uint8_t> imd;
        putHeader(imd, "IMD 1.18: uniform");
        for (uint8_t c = 0; c < 3; ++c)
            putTrack(imd, 3, c, 0, 0, {1, 2}, ramp(2, [](int) -> uint8_t { return 0; }));

        std::vector<uint8_t> raw;
        ImdInfo              info;
        std::string          err;
        CHECK(convertImdToRaw(imd, raw, info, err, yes), "converts");
        CHECK(info.description == "IMD 1.18: uniform", "header captured");
        CHECK(info.tracks.size() == 1, "three identical cylinders coalesce to one line");
        CHECK(info.rawBytes == raw.size() && raw.size() == 3u * 2 * 128, "byte total is right");
    }
}
