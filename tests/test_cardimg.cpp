#include "test.h"

#include "host/cardimg.h"
#include "host/media.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace altair;
namespace fs = std::filesystem;

// A CardImage IS a real file plus a `.geo` sidecar -- so, like the host-bridge sandbox
// tests, this suite legitimately touches the filesystem: streaming per-block from the
// backing file and growing it is the whole point, and no MemoryMedia can stand in for it.
// Board tests still never do this; they get a MemoryMedia.

namespace {

fs::path testRoot() { return fs::temp_directory_path() / "altairsim_cardimg_test"; }

std::vector<uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Build a card: the backing image `<sub>.img` at the given byte size (all zero bytes) and
// its `<sub>.geo` sidecar holding the descriptor text. Returns the image path.
fs::path buildCard(const std::string& sub, const std::string& geo, size_t imgBytes) {
    fs::path dir = testRoot();
    fs::create_directories(dir);
    fs::path img = dir / (sub + ".img");
    fs::path geoPath = dir / (sub + ".geo");
    fs::remove(img);
    fs::remove(geoPath);
    { std::ofstream f(img, std::ios::binary); for (size_t i = 0; i < imgBytes; ++i) f.put('\0'); }
    { std::ofstream g(geoPath, std::ios::binary); g << geo; }
    return img;
}

// A malformed descriptor must be REFUSED with a message, never a silent empty card.
void badGeometry(const std::string& sub, const std::string& geo, const char* why) {
    fs::path    img = buildCard(sub, geo, 0);
    std::string err;
    auto        m = openCardImage(img.string(), false, err);
    CHECK(m == nullptr, why);
    CHECK(!err.empty(), "and it says why");
}

} // namespace

void test_cardimg() {
    fs::remove_all(testRoot());

    SECTION("cardimg: geometry parse, size, describe");
    {
        // 4 sectors x 512 bytes = 2048-byte card, image starts empty.
        fs::path img = buildCard("basic",
                                 "# a blank 4-sector card\n"
                                 "sector_size 512\n"
                                 "sectors     4\n",
                                 0);
        std::string err;
        auto        m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr, "a well-formed card opens");
        CHECK(m && m->size() == 2048, "size is sectors x sector_size");
        CHECK(m && !m->readOnly() && !m->readOnlyForced(), "writable, nobody forced it");
        CHECK(m && m->describe() == img.string(), "describe() is the image path, for SHOW");
    }

    SECTION("cardimg: a blank card reads the ERASED byte, not zeros or 0xE5");
    {
        fs::path    img = buildCard("blank", "sector_size 512\nsectors 8\n", 0);
        std::string err;
        auto        m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr, "opened");

        uint8_t buf[512];
        std::memset(buf, 0x00, sizeof(buf));
        CHECK(m && m->readAt(0, buf, 512), "read the first sector of a never-written card");
        bool allErased = true;
        for (uint8_t b : buf) allErased &= (b == kErasedByte);
        CHECK(allErased, "and every byte is the erased-card value (0xFF), not 0x00 or 0xE5");

        // And a sector deep in the card, well past the (zero-length) image.
        std::memset(buf, 0x00, sizeof(buf));
        CHECK(m && m->readAt(3072, buf, 512) && buf[0] == kErasedByte && buf[511] == kErasedByte,
              "erased everywhere within the declared geometry");
    }

    SECTION("cardimg: a short image serves real bytes then erased tail");
    {
        // The image holds one written sector (0xAA x 512); the card is declared much larger,
        // so the rest is erased -- the truncated-boot-image case.
        fs::path img = buildCard("short", "sector_size 512\nsectors 100\n", 0);
        { std::ofstream f(img, std::ios::binary); for (int i = 0; i < 512; ++i) f.put((char)0xAA); }

        std::string err;
        auto        m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr && m->size() == 100 * 512, "opens at the DECLARED size, not the file size");

        uint8_t buf[512];
        CHECK(m && m->readAt(0, buf, 512) && buf[0] == 0xAA && buf[511] == 0xAA,
              "the backed sector reads its real bytes");
        std::memset(buf, 0x00, sizeof(buf));
        CHECK(m && m->readAt(512, buf, 512) && buf[0] == kErasedByte && buf[511] == kErasedByte,
              "the very next (unbacked) sector reads erased");
    }

    SECTION("cardimg: writes grow the backing file and read back");
    {
        fs::path    img = buildCard("route", "sector_size 512\nsectors 8\n", 0);
        std::string err;
        auto        m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr, "opened");

        const uint8_t w[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        CHECK(m && m->writeAt(100, w, 4), "write at byte 100");

        uint8_t r[4] = {};
        CHECK(m && m->readAt(100, r, 4) && std::memcmp(r, w, 4) == 0, "reads back");
        m->sync();

        // The image grew exactly to cover the write (100+4), and the gap before it is
        // erased-filled, not zero-filled.
        auto a = slurp(img);
        CHECK(a.size() == 104 && a[100] == 0xDE && a[103] == 0xEF, "image grew to 104 with the bytes");
        CHECK(a[0] == kErasedByte && a[99] == kErasedByte, "the gap before the write is erased-filled");
    }

    SECTION("cardimg: an out-of-order write leaves an ERASED gap, not a zero gap");
    {
        fs::path    img = buildCard("gap", "sector_size 512\nsectors 8\n", 0);
        std::string err;
        auto        m = openCardImage(img.string(), false, err);
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
        auto a = slurp(img);
        CHECK(a.size() == 202 && a[0] == kErasedByte && a[199] == kErasedByte && a[200] == 0x55,
              "on disk the gap is erased-filled ahead of the write");
    }

    SECTION("cardimg: a write past the card's declared end is refused");
    {
        fs::path    img = buildCard("bounds", "sector_size 512\nsectors 8\n", 0);  // 4096 bytes
        std::string err;
        auto        m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr, "opened");

        const uint8_t w[8] = {};
        CHECK(m && !m->writeAt(4096, w, 1), "a write starting at the end is refused");
        CHECK(m && !m->writeAt(4090, w, 8), "a write that runs off the end is refused whole");
        uint8_t r[8];
        CHECK(m && !m->readAt(4090, r, 8), "and reads off the end fail too -- no short count");
        CHECK(m && m->readAt(4088, r, 8), "the last 8 bytes DO read");
    }

    SECTION("cardimg: sync() puts bytes on the host");
    {
        fs::path    img = buildCard("durable", "sector_size 512\nsectors 4\n", 0);
        std::string err;
        auto        m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr, "opened");
        const uint8_t w[3] = {0x11, 0x22, 0x33};
        CHECK(m && m->writeAt(0, w, 3), "write");
        m->sync();

        // A SECOND, independent card over the same image must see it -- only a real flush to
        // the host makes that true.
        auto    m2 = openCardImage(img.string(), false, err);
        uint8_t r[3] = {};
        CHECK(m2 && m2->readAt(0, r, 3) && std::memcmp(r, w, 3) == 0,
              "a fresh card reads the bytes -- they reached the host, not just our buffer");
    }

    SECTION("cardimg: a non-writable image forces read-only, and says so");
    {
        fs::path img = buildCard("ro", "sector_size 512\nsectors 4\n", 0);
        constexpr auto all_write =
            fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
        fs::permissions(img, all_write, fs::perm_options::remove);

        std::string err;
        auto        m = openCardImage(img.string(), /*readOnly=*/false, err);
        CHECK(m != nullptr, "an unwritable card still MOUNTS -- a card with the tab out");
        CHECK(m && m->readOnly(), "read-only");
        CHECK(m && m->readOnlyForced(), "and it says WE forced it, so the board can tell the operator");
        const uint8_t w = 0xFF;
        CHECK(m && !m->writeAt(0, &w, 1), "the write bounces here, not at sync time");

        auto asked = openCardImage(img.string(), /*readOnly=*/true, err);
        CHECK(asked && asked->readOnly(), "MOUNT ... RO is read-only");
        CHECK(asked && !asked->readOnlyForced(), "but that one was ASKED for -- nothing to report");

        m.reset();
        asked.reset();
        fs::permissions(img, all_write, fs::perm_options::add);
    }

    SECTION("cardimg: malformed descriptors are refused");
    {
        badGeometry("bad_unknown", "sector_size 512\nsplat 4\n", "an unknown directive is refused");
        badGeometry("bad_zero", "sector_size 512\nsectors 0\n", "a zero-sector card is refused");
        badGeometry("bad_none", "sector_size 512\n# nothing else\n",
                    "a descriptor with no 'sectors' is refused");
        badGeometry("bad_size", "sector_size 0\nsectors 4\n", "a zero sector_size is refused");

        // An image bigger than its declared card: malformed, not silently clipped.
        {
            fs::path    img = buildCard("bad_overflow", "sector_size 512\nsectors 1\n", 1024);
            std::string err;
            CHECK(openCardImage(img.string(), false, err) == nullptr && !err.empty(),
                  "an image overflowing its declared card is refused");
        }

        // A missing sidecar, and a missing image.
        std::string err;
        {
            fs::path img = testRoot() / "orphan.img";
            { std::ofstream f(img, std::ios::binary); }
            CHECK(openCardImage(img.string(), false, err) == nullptr,
                  "an image with no .geo sidecar is not a card");
        }
        CHECK(openCardImage((testRoot() / "gone.img").string(), false, err) == nullptr,
              "a missing image is not a card");
    }

    SECTION("cardimg: openHostMedia routes sidecar cards, bare .img, and plain files");
    {
        std::string err;

        // An image WITH a sibling .geo -> a CardImage.
        fs::path img  = buildCard("resolve", "sector_size 512\nsectors 4\n", 0);
        auto     card = openHostMedia(img.string(), false, err);
        CHECK(card != nullptr && card->size() == 2048, "an image + sidecar resolves to a card");

        // A `.img` with NO sidecar -> a hard error (a card needs its geometry), NOT a plain
        // image at the wrong size.
        fs::path bare = testRoot() / "bare.img";
        { std::ofstream f(bare, std::ios::binary); for (int i = 0; i < 16; ++i) f.put((char)i); }
        auto barem = openHostMedia(bare.string(), false, err);
        CHECK(barem == nullptr && !err.empty(), "a .img without a .geo is refused, with a message");

        // Mounting the sidecar itself -> a helpful error, not a nonsense card.
        fs::path geo = testRoot() / "resolve.geo";
        auto     geom = openHostMedia(geo.string(), false, err);
        CHECK(geom == nullptr && !err.empty(), "mounting the .geo sidecar is refused");

        // A plain non-.img file (a .dsk, say) with no sidecar -> a HostFile, unchanged.
        fs::path fp = testRoot() / "plain.dsk";
        { std::ofstream f(fp, std::ios::binary); for (int i = 0; i < 16; ++i) f.put((char)i); }
        auto file = openHostMedia(fp.string(), false, err);
        CHECK(file != nullptr && file->size() == 16, "a plain file resolves to a host file");
    }

    SECTION("cardimg: parseCardSpec turns MOUNT options into a card spec");
    {
        using Opt = std::pair<std::string, std::string>;

        // Explicit sector size + sector count.
        {
            CardSpec    s;
            std::string err;
            CHECK(parseCardSpec({{"sector_size", "512"}, {"sectors", "15616"}}, s, err),
                  "sector_size + sectors parse");
            CHECK(s.sectorSize == 512 && s.sectors == 15616, "sector size and count");
        }

        // A named template on its own.
        {
            CardSpec    s;
            std::string err;
            CHECK(parseCardSpec({{"format", "dualsd"}}, s, err), "format=dualsd parses");
            CHECK(s.sectorSize == 512 && s.sectors == 15616,
                  "the dualsd template is a 15616-sector 512-byte card");
        }

        // Explicit bits override the template REGARDLESS of order (the two-pass point):
        // sectors typed BEFORE format= must win, and explicit sector_size wins too.
        {
            CardSpec    s;
            std::string err;
            CHECK(parseCardSpec({{"sectors", "100"}, {"format", "dualsd"},
                                 {"sector_size", "1024"}}, s, err),
                  "sectors= before format= parses");
            CHECK(s.sectorSize == 1024, "explicit sector_size overrides the template's 512");
            CHECK(s.sectors == 100, "explicit sectors overrides the template's 15616");
        }

        // Malformed specs are refused with a message.
        auto bad = [](std::vector<Opt> o, const char* why) {
            CardSpec    s;
            std::string err;
            CHECK(!parseCardSpec(o, s, err) && !err.empty(), why);
        };
        bad({}, "no options -> no size -> refused");
        bad({{"sector_size", "512"}}, "sector size but no sectors is refused");
        bad({{"sectors", "0"}}, "a zero sector count is refused");
        bad({{"sectors", "xyz"}}, "a non-numeric sector count is refused");
        bad({{"sector_size", "0"}}, "a zero sector size is refused");
        bad({{"format", "nope"}}, "an unknown template is refused");
        bad({{"splat", "1"}}, "an option that is not a card key is refused");
    }

    SECTION("cardimg: hasCardSpecKeys distinguishes a card CREATE from a plain-file CREATE");
    {
        CHECK(hasCardSpecKeys({{"format", "dualsd"}}), "format= is a card key");
        CHECK(hasCardSpecKeys({{"sector_size", "512"}}), "sector_size= is a card key");
        CHECK(hasCardSpecKeys({{"sectors", "4"}}), "sectors= is a card key");
        CHECK(hasCardSpecKeys({{"SECTORS", "4"}}), "case-insensitive");
        CHECK(!hasCardSpecKeys({{"counter", "off"}, {"stop", "2:05"}}),
              "ordinary unit properties are NOT card keys");
        CHECK(!hasCardSpecKeys({}), "no options -> no card keys");
    }

    SECTION("cardimg: createCardImage authors a blank, unformatted card");
    {
        fs::path img = testRoot() / "authored.img";
        fs::path geo = testRoot() / "authored.geo";
        fs::remove(img);
        fs::remove(geo);

        CardSpec s;
        s.sectorSize = 512;
        s.sectors    = 12;
        std::string err;
        CHECK(createCardImage(img.string(), s, err), "createCardImage succeeds");

        // An EMPTY backing image plus its sidecar.
        CHECK(fs::exists(img) && fs::file_size(img) == 0,
              "the image exists and is empty (0 bytes, growable -- a blank card)");
        CHECK(fs::exists(geo), "the .geo sidecar was written");

        // It is a real, openable card: 12 x 512 bytes, all erased.
        auto m = openCardImage(img.string(), false, err);
        CHECK(m != nullptr && m->size() == 12 * 512, "the authored card opens at the declared size");
        uint8_t buf[512];
        std::memset(buf, 0, sizeof(buf));
        CHECK(m && m->readAt(0, buf, 512) && buf[0] == kErasedByte && buf[511] == kErasedByte,
              "and every block reads erased -- genuinely unformatted, no 0xE5 directory");

        // A guest can format/write it, and the writes persist across a remount.
        const uint8_t w[4] = {0x01, 0x02, 0x03, 0x04};
        CHECK(m && m->writeAt(6 * 512, w, 4), "write into the card");
        m->sync();
        m.reset();
        auto    m2 = openCardImage(img.string(), false, err);
        uint8_t r[4] = {};
        CHECK(m2 && m2->readAt(6 * 512, r, 4) && std::memcmp(r, w, 4) == 0,
              "the write survived -- a fresh open of the authored card sees it");

        // CREATE never clobbers: a second author at the same path is refused.
        std::string err2;
        CHECK(!createCardImage(img.string(), s, err2) && !err2.empty(),
              "authoring over an existing image is refused");
    }

    SECTION("cardimg: createCardImage refuses a malformed spec and leaves nothing behind");
    {
        fs::path img = testRoot() / "notmade.img";
        fs::path geo = testRoot() / "notmade.geo";
        fs::remove(img);
        fs::remove(geo);
        CardSpec    s;  // zero sectors
        std::string err;
        CHECK(!createCardImage(img.string(), s, err), "an empty spec is refused");
        CHECK(!fs::exists(img) && !fs::exists(geo), "and no half-built card is left on disk");
    }

    // Tidy up -- drop any handles first (Windows will not delete an open file).
    fs::remove_all(testRoot());
}
