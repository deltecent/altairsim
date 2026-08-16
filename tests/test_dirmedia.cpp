#include "test.h"

#include "host/dirmedia.h"
#include "host/media.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace altair;
namespace fs = std::filesystem;

// A DirectoryMedia IS a directory of files -- so, like the host-bridge sandbox tests,
// this suite legitimately touches the filesystem: routing per-partition backing files
// and growing them is the whole point, and no MemoryMedia can stand in for it. Board
// tests still never do this; they get a MemoryMedia.

namespace {

fs::path testRoot() { return fs::temp_directory_path() / "altairsim_dirmedia_test"; }

std::vector<uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Build a card directory: the descriptor text plus each named backing file created at
// the given byte size (all zero bytes).
fs::path buildCard(const std::string& sub, const std::string& geo,
                   const std::vector<std::pair<std::string, size_t>>& files) {
    fs::path d = testRoot() / sub;
    fs::remove_all(d);
    fs::create_directories(d);
    { std::ofstream g(d / kGeometryFile, std::ios::binary); g << geo; }
    for (const auto& [name, sz] : files) {
        std::ofstream f(d / name, std::ios::binary);
        for (size_t i = 0; i < sz; ++i) f.put('\0');
    }
    return d;
}

// A malformed descriptor must be REFUSED with a message, never a silent empty card.
void badGeometry(const std::string& sub, const std::string& geo,
                 const std::vector<std::pair<std::string, size_t>>& files,
                 const char* why) {
    fs::path    d = buildCard(sub, geo, files);
    std::string err;
    auto        m = openDirectoryMedia(d.string(), false, err);
    CHECK(m == nullptr, why);
    CHECK(!err.empty(), "and it says why");
}

} // namespace

void test_dirmedia() {
    fs::remove_all(testRoot());

    SECTION("dirmedia: geometry parse, size, describe");
    {
        // Two 4-sector (2048-byte) partitions, one 512-byte sector each -> 4096 bytes.
        fs::path d = buildCard("basic",
                               "# a card of two volumes\n"
                               "sector_size 512\n"
                               "partition A a.img 4\n"
                               "partition B b.img 4\n",
                               {{"a.img", 0}, {"b.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "a well-formed card opens");
        CHECK(m && m->size() == 4096, "size is Sum(sectors) x sector_size");
        CHECK(m && !m->readOnly() && !m->readOnlyForced(), "writable, nobody forced it");
        CHECK(m && m->describe() == d.string(), "describe() is the directory, for SHOW");
    }

    SECTION("dirmedia: a blank card reads the ERASED byte, not zeros or 0xE5");
    {
        fs::path d = buildCard("blank",
                               "sector_size 512\npartition A a.img 4\npartition B b.img 4\n",
                               {{"a.img", 0}, {"b.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "opened");

        uint8_t buf[512];
        std::memset(buf, 0x00, sizeof(buf));
        CHECK(m && m->readAt(0, buf, 512), "read the first sector of a never-written card");
        bool allErased = true;
        for (uint8_t b : buf) allErased &= (b == kErasedByte);
        CHECK(allErased, "and every byte is the erased-card value (0xFF), not 0x00 or 0xE5");

        // And a sector deep in the second partition, past both files' (zero) ends.
        std::memset(buf, 0x00, sizeof(buf));
        CHECK(m && m->readAt(2560, buf, 512) && buf[0] == kErasedByte && buf[511] == kErasedByte,
              "erased everywhere within the declared geometry");
    }

    SECTION("dirmedia: writes route to the covering partition and grow its file");
    {
        fs::path d = buildCard("route",
                               "sector_size 512\npartition A a.img 4\npartition B b.img 4\n",
                               {{"a.img", 0}, {"b.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "opened");

        const uint8_t wa[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        CHECK(m && m->writeAt(100, wa, 4), "write into partition A at byte 100");
        const uint8_t wb[4] = {0xCA, 0xFE, 0xBA, 0xBE};
        CHECK(m && m->writeAt(2048 + 50, wb, 4), "write into partition B at its byte 50");

        uint8_t r[4] = {};
        CHECK(m && m->readAt(100, r, 4) && std::memcmp(r, wa, 4) == 0, "A reads back");
        CHECK(m && m->readAt(2048 + 50, r, 4) && std::memcmp(r, wb, 4) == 0, "B reads back");
        m->sync();

        // The bytes landed in the RIGHT backing file, at the RIGHT in-file offset --
        // and each file grew exactly to cover its write (100+4, 50+4), gap-filled with
        // the erased byte, not zeros.
        auto a = slurp(d / "a.img");
        auto b = slurp(d / "b.img");
        CHECK(a.size() == 104 && a[100] == 0xDE && a[103] == 0xEF, "a.img grew to 104 with A's bytes");
        CHECK(a[0] == kErasedByte && a[99] == kErasedByte, "the gap before A's write is erased-filled");
        CHECK(b.size() == 54 && b[50] == 0xCA && b[53] == 0xBE, "b.img grew to 54 with B's bytes");
        CHECK(b[49] == kErasedByte, "and B's gap is erased-filled too");
    }

    SECTION("dirmedia: a read and a write that straddle the partition boundary");
    {
        fs::path d = buildCard("straddle",
                               "sector_size 512\npartition A a.img 4\npartition B b.img 4\n",
                               {{"a.img", 0}, {"b.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "opened");

        // Eight bytes centred on the A|B boundary at 2048: four into A's tail, four
        // into B's head.
        const uint8_t w[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK(m && m->writeAt(2044, w, 8), "a write across the boundary");
        m->sync();

        uint8_t r[8] = {};
        CHECK(m && m->readAt(2044, r, 8) && std::memcmp(r, w, 8) == 0, "and it reads straight back");

        auto a = slurp(d / "a.img");
        auto b = slurp(d / "b.img");
        CHECK(a.size() == 2048 && a[2044] == 1 && a[2047] == 4, "A got its four tail bytes");
        CHECK(b.size() == 4 && b[0] == 5 && b[3] == 8, "B got its four head bytes -- at ITS offset 0");
    }

    SECTION("dirmedia: an out-of-order write leaves an ERASED gap, not a zero gap");
    {
        fs::path d = buildCard("gap",
                               "sector_size 512\npartition A a.img 4\npartition B b.img 4\n",
                               {{"a.img", 0}, {"b.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "opened");

        const uint8_t w[2] = {0x55, 0xAA};
        CHECK(m && m->writeAt(200, w, 2), "write at 200, leaving 0..199 never written");

        uint8_t gapbuf[200];
        std::memset(gapbuf, 0x00, sizeof(gapbuf));
        CHECK(m && m->readAt(0, gapbuf, 200), "read the gap the write skipped over");
        bool erased = true;
        for (uint8_t b : gapbuf) erased &= (b == kErasedByte);
        CHECK(erased, "and the gap reads erased (0xFF), the value the physical card would return");
        m->sync();
        auto a = slurp(d / "a.img");
        CHECK(a.size() == 202 && a[0] == kErasedByte && a[199] == kErasedByte && a[200] == 0x55,
              "on disk the gap is erased-filled ahead of the write");
    }

    SECTION("dirmedia: a write past the card's declared end is refused");
    {
        fs::path d = buildCard("bounds",
                               "sector_size 512\npartition A a.img 4\npartition B b.img 4\n",
                               {{"a.img", 0}, {"b.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "opened");

        const uint8_t w[8] = {};
        CHECK(m && !m->writeAt(4096, w, 1), "a write starting at the end is refused");
        CHECK(m && !m->writeAt(4090, w, 8), "a write that runs off the end is refused whole");
        uint8_t r[8];
        CHECK(m && !m->readAt(4090, r, 8), "and reads off the end fail too -- no short count");
        CHECK(m && m->readAt(4088, r, 8), "the last 8 bytes DO read");
    }

    SECTION("dirmedia: sync() puts bytes on the host");
    {
        fs::path d = buildCard("durable",
                               "sector_size 512\npartition A a.img 4\n",
                               {{"a.img", 0}});
        std::string err;
        auto        m = openDirectoryMedia(d.string(), false, err);
        CHECK(m != nullptr, "opened");
        const uint8_t w[3] = {0x11, 0x22, 0x33};
        CHECK(m && m->writeAt(0, w, 3), "write");
        m->sync();

        // A SECOND, independent card over the same directory must see it -- only a
        // real flush to the host makes that true.
        auto m2 = openDirectoryMedia(d.string(), false, err);
        uint8_t r[3] = {};
        CHECK(m2 && m2->readAt(0, r, 3) && std::memcmp(r, w, 3) == 0,
              "a fresh card reads the bytes -- they reached the host, not just our buffer");
    }

    SECTION("dirmedia: a non-writable backing file forces read-only, and says so");
    {
        fs::path d = buildCard("ro",
                               "sector_size 512\npartition A a.img 4\n",
                               {{"a.img", 0}});
        constexpr auto all_write =
            fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
        fs::permissions(d / "a.img", all_write, fs::perm_options::remove);

        std::string err;
        auto        m = openDirectoryMedia(d.string(), /*readOnly=*/false, err);
        CHECK(m != nullptr, "an unwritable card still MOUNTS -- a card with the tab out");
        CHECK(m && m->readOnly(), "read-only");
        CHECK(m && m->readOnlyForced(), "and it says WE forced it, so the board can tell the operator");
        const uint8_t w = 0xFF;
        CHECK(m && !m->writeAt(0, &w, 1), "the write bounces here, not at sync time");

        auto asked = openDirectoryMedia(d.string(), /*readOnly=*/true, err);
        CHECK(asked && asked->readOnly(), "MOUNT ... RO is read-only");
        CHECK(asked && !asked->readOnlyForced(), "but that one was ASKED for -- nothing to report");

        m.reset();
        asked.reset();
        fs::permissions(d / "a.img", all_write, fs::perm_options::add);
    }

    SECTION("dirmedia: malformed descriptors are refused");
    {
        auto files = std::vector<std::pair<std::string, size_t>>{{"a.img", 0}};
        badGeometry("bad_unknown", "sector_size 512\nsplat A a.img 4\n", files,
                    "an unknown directive is refused");
        badGeometry("bad_zero", "sector_size 512\npartition A a.img 0\n", files,
                    "a zero-sector partition is refused");
        badGeometry("bad_fields", "sector_size 512\npartition A\n", files,
                    "a partition missing fields is refused");
        badGeometry("bad_none", "sector_size 512\n# nothing else\n", files,
                    "a card with no partitions is refused");
        badGeometry("bad_slash", "partition A sub/a.img 4\n", files,
                    "a backing file with a path separator is refused");
        badGeometry("bad_missing", "partition A gone.img 4\n", files,
                    "a partition naming a missing backing file is refused");

        // A backing file bigger than its declared slot: malformed, not silently clipped.
        badGeometry("bad_overflow", "sector_size 512\npartition A a.img 1\n",
                    {{"a.img", 1024}}, "a backing file overflowing its slot is refused");

        // Not a card at all.
        std::string err;
        CHECK(openDirectoryMedia((testRoot() / "no_such_dir").string(), false, err) == nullptr,
              "a missing directory is not a card");
        fs::path empty = testRoot() / "empty_dir";
        fs::create_directories(empty);
        CHECK(openDirectoryMedia(empty.string(), false, err) == nullptr,
              "a directory with no card.geometry is not a card");
    }

    SECTION("dirmedia: openHostMedia routes directory vs file");
    {
        // A directory -> a DirectoryMedia; a plain file -> a HostFile. This is the
        // resolver both mains install.
        fs::path d = buildCard("resolve",
                               "sector_size 512\npartition A a.img 4\n", {{"a.img", 0}});
        std::string err;
        auto        card = openHostMedia(d.string(), false, err);
        CHECK(card != nullptr && card->size() == 2048, "a directory resolves to a card");

        fs::path fp = testRoot() / "plain.bin";
        { std::ofstream f(fp, std::ios::binary); for (int i = 0; i < 16; ++i) f.put((char)i); }
        auto file = openHostMedia(fp.string(), false, err);
        CHECK(file != nullptr && file->size() == 16, "a file resolves to a host file");
    }

    SECTION("dirmedia: parseCardSpec turns MOUNT options into a card spec");
    {
        using Opt = std::pair<std::string, std::string>;

        // Explicit sector size + two volumes.
        {
            CardSpec    s;
            std::string err;
            CHECK(parseCardSpec({{"sector_size", "512"}, {"volume", "A:15616"},
                                 {"volume", "B:8"}}, s, err),
                  "sector_size + two volume= parse");
            CHECK(s.sectorSize == 512 && s.volumes.size() == 2, "sector size and volume count");
            CHECK(s.volumes[0].name == "A" && s.volumes[0].sectors == 15616 &&
                      s.volumes[1].name == "B" && s.volumes[1].sectors == 8,
                  "names and sector counts, in order");
        }

        // A named template on its own.
        {
            CardSpec    s;
            std::string err;
            CHECK(parseCardSpec({{"format", "dualsd"}}, s, err), "format=dualsd parses");
            CHECK(s.sectorSize == 512 && s.volumes.size() == 1 && s.volumes[0].name == "A" &&
                      s.volumes[0].sectors == 15616,
                  "the dualsd template is one 15616-sector volume A");
        }

        // Explicit bits override/extend the template REGARDLESS of order (the two-pass point):
        // volume= typed BEFORE format= must survive, and explicit sector_size wins.
        {
            CardSpec    s;
            std::string err;
            CHECK(parseCardSpec({{"volume", "B:100"}, {"format", "dualsd"},
                                 {"sector_size", "1024"}}, s, err),
                  "volume= before format= parses");
            CHECK(s.sectorSize == 1024, "explicit sector_size overrides the template's 512");
            CHECK(s.volumes.size() == 2, "template volume plus the explicit one");
            bool haveA = false, haveB = false;
            for (auto& v : s.volumes) {
                if (v.name == "A" && v.sectors == 15616) haveA = true;
                if (v.name == "B" && v.sectors == 100) haveB = true;
            }
            CHECK(haveA && haveB, "the explicit volume was NOT clobbered by the template");
        }

        // Malformed specs are refused with a message.
        auto bad = [](std::vector<Opt> o, const char* why) {
            CardSpec    s;
            std::string err;
            CHECK(!parseCardSpec(o, s, err) && !err.empty(), why);
        };
        bad({}, "no options -> no volumes -> refused");
        bad({{"sector_size", "512"}}, "sector size but no volume is refused");
        bad({{"volume", "A"}}, "volume without :sectors is refused");
        bad({{"volume", "A:0"}}, "a zero-sector volume is refused");
        bad({{"volume", "A:xyz"}}, "a non-numeric sector count is refused");
        bad({{"sector_size", "0"}}, "a zero sector size is refused");
        bad({{"format", "nope"}}, "an unknown template is refused");
        bad({{"volume", "A:4"}, {"volume", "A:8"}}, "two volumes named A collide -- refused");
        bad({{"volume", "sub/x:4"}}, "a volume name with a path separator is refused");
        bad({{"splat", "1"}}, "an option that is not a card key is refused");
    }

    SECTION("dirmedia: hasCardSpecKeys distinguishes a card CREATE from a plain-file CREATE");
    {
        CHECK(hasCardSpecKeys({{"format", "dualsd"}}), "format= is a card key");
        CHECK(hasCardSpecKeys({{"sector_size", "512"}}), "sector_size= is a card key");
        CHECK(hasCardSpecKeys({{"volume", "A:4"}}), "volume= is a card key");
        CHECK(hasCardSpecKeys({{"VOLUME", "A:4"}}), "case-insensitive");
        CHECK(!hasCardSpecKeys({{"counter", "off"}, {"stop", "2:05"}}),
              "ordinary unit properties are NOT card keys");
        CHECK(!hasCardSpecKeys({}), "no options -> no card keys");
    }

    SECTION("dirmedia: createDirectoryMedia authors a blank, unformatted card");
    {
        fs::path dir = testRoot() / "authored";
        fs::remove_all(dir);

        CardSpec s;
        s.sectorSize = 512;
        s.volumes    = {{"A", 4}, {"DATA", 8}};
        std::string err;
        CHECK(createDirectoryMedia(dir.string(), s, err), "createDirectoryMedia succeeds");

        // The descriptor and one EMPTY backing file per volume, named <volume>.img.
        CHECK(fs::exists(dir / kGeometryFile), "card.geometry was written");
        CHECK(fs::exists(dir / "A.img") && fs::file_size(dir / "A.img") == 0,
              "A.img exists and is empty (0 bytes, growable -- a blank card)");
        CHECK(fs::exists(dir / "DATA.img") && fs::file_size(dir / "DATA.img") == 0,
              "DATA.img exists and is empty too");

        // It is a real, openable card: contiguous (4+8) x 512 bytes, all erased.
        auto m = openDirectoryMedia(dir.string(), false, err);
        CHECK(m != nullptr && m->size() == (4 + 8) * 512,
              "the authored card opens at the declared size");
        uint8_t buf[512];
        std::memset(buf, 0, sizeof(buf));
        CHECK(m && m->readAt(0, buf, 512) && buf[0] == kErasedByte && buf[511] == kErasedByte,
              "and every block reads erased -- genuinely unformatted, no 0xE5 directory");

        // A guest can format/write it, and the writes persist across a remount.
        const uint8_t w[4] = {0x01, 0x02, 0x03, 0x04};
        CHECK(m && m->writeAt(6 * 512, w, 4), "write into the DATA volume");
        m->sync();
        m.reset();
        auto m2 = openDirectoryMedia(dir.string(), false, err);
        uint8_t r[4] = {};
        CHECK(m2 && m2->readAt(6 * 512, r, 4) && std::memcmp(r, w, 4) == 0,
              "the write survived -- a fresh open of the authored card sees it");

        // CREATE never clobbers: a second author at the same path is refused.
        std::string err2;
        CHECK(!createDirectoryMedia(dir.string(), s, err2) && !err2.empty(),
              "authoring over an existing directory is refused");
    }

    SECTION("dirmedia: createDirectoryMedia refuses a malformed spec and leaves nothing behind");
    {
        fs::path dir = testRoot() / "notmade";
        fs::remove_all(dir);
        CardSpec    s;  // no volumes
        std::string err;
        CHECK(!createDirectoryMedia(dir.string(), s, err), "an empty spec is refused");
        CHECK(!fs::exists(dir), "and no half-built directory is left on disk");
    }

    // Tidy up -- drop any handles first (Windows will not delete an open file).
    fs::remove_all(testRoot());
}
