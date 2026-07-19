#include "test.h"

#include "host/disk.h"
#include "host/media.h"
#include "host/tape.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

std::unique_ptr<MediaFile> mem(size_t n, bool ro = false) {
    std::vector<uint8_t> b(n);
    // A recognisable fill: byte i is i mod 251 (prime, so it does not line up with
    // any sector size and a wrong offset shows immediately).
    for (size_t i = 0; i < n; ++i) b[i] = (uint8_t)(i % 251);
    return std::make_unique<MemoryMedia>("memory:test", std::move(b), ro);
}

// The DCDD, described exactly as the board will describe it in Phase 5.
std::unique_ptr<DiskImage> dcdd8(std::unique_ptr<MediaFile> m) {
    auto d = std::make_unique<DiskImage>(std::move(m));
    d->init(77, 1, false);
    d->initFormat(0, 76, 0, 0, Density::SD, 32, 137, 0);  // hard sector: the whole slot
    return d;
}

} // namespace

void test_media() {
    SECTION("media: MediaFile");
    {
        auto m = mem(1024);
        CHECK(m->size() == 1024, "size is what we put in");
        CHECK(!m->readOnly(), "not write protected");

        uint8_t buf[4] = {};
        CHECK(m->readAt(500, buf, 4), "read inside");
        CHECK(buf[0] == 500 % 251 && buf[3] == 503 % 251, "read the right bytes");

        CHECK(!m->readAt(1022, buf, 4), "a read off the end FAILS, it does not short-count");
        CHECK(!m->readAt(2000, buf, 1), "a read past the end fails");

        const uint8_t w[3] = {0xDE, 0xAD, 0xBE};
        CHECK(m->writeAt(10, w, 3), "write inside");
        CHECK(m->readAt(10, buf, 3) && buf[0] == 0xDE && buf[2] == 0xBE, "it stuck");

        auto* mm = static_cast<MemoryMedia*>(m.get());
        CHECK(mm->syncs() == 0, "nothing synced yet");
        m->sync();
        CHECK(mm->syncs() == 1, "sync reached the medium");
    }
    {
        auto  m  = mem(64, /*ro=*/true);
        const uint8_t w = 0xFF;
        CHECK(m->readOnly(), "write protected");
        CHECK(!m->writeAt(0, &w, 1), "protected: the write is REFUSED");
        uint8_t b = 0;
        CHECK(m->readAt(0, &b, 1) && b == 0, "and the byte is untouched");
    }

    SECTION("media: HostFile -- a real file, and the write-back");
    {
        // The ONE test here that touches the filesystem, and it earns it: write-back
        // is where a lost disk actually comes from, and no MemoryMedia can prove a
        // byte reached the host. Board tests still never do this.
        namespace fs = std::filesystem;
        fs::path p   = fs::temp_directory_path() / "altairsim_media_test.bin";
        {
            std::ofstream f(p, std::ios::binary);
            for (int i = 0; i < 256; ++i) f.put((char)i);
        }

        std::string err;
        auto        m = openHostFile(p.string(), false, err);
        CHECK(m != nullptr, "opened a real file");
        CHECK(m && m->size() == 256, "with its real size");
        CHECK(m && !m->readOnly() && !m->readOnlyForced(), "writable, and nobody forced anything");

        const uint8_t w[2] = {0xBE, 0xEF};
        CHECK(m->writeAt(100, w, 2), "write it");
        CHECK(static_cast<HostFile*>(m.get())->dirty(), "the buffer is dirty");
        m->sync();
        CHECK(!static_cast<HostFile*>(m.get())->dirty(), "and the sync cleaned it");

        // Did the byte actually reach the disk? Only a fresh open can say.
        auto m2 = openHostFile(p.string(), false, err);
        uint8_t b[2] = {};
        CHECK(m2 && m2->readAt(100, b, 2) && b[0] == 0xBE && b[1] == 0xEF,
              "the byte is ON THE HOST, not just in our buffer");
        CHECK(m2 && m2->size() == 256, "and the file did not change size");

        // SHRINKING IT, AND PROVING THE HOST FILE ACTUALLY SHRANK. This is what an
        // audio tape needs to re-encode itself over a longer recording, and the whole
        // failure it prevents is invisible from inside our own buffer: without the
        // truncate the old tail is still on disk, and only a fresh open can see it.
        CHECK(m->resize(64), "shrink the medium");
        CHECK(m->size() == 64, "the buffer is shorter at once");
        m->sync();
        auto m3 = openHostFile(p.string(), false, err);
        CHECK(m3 && m3->size() == 64, "and the FILE is 64 bytes, not 256 with a short write in it");
        uint8_t tail = 0;
        CHECK(m3 && !m3->readAt(63, &tail, 2), "there is nothing past the new end to read");
        CHECK(m3 && m3->readAt(63, &tail, 1) && tail == 63, "and what is left is what was there");

        CHECK(m->resize(96), "grow it again");
        m->sync();
        auto m4 = openHostFile(p.string(), false, err);
        CHECK(m4 && m4->size() == 96, "the file grew too");
        uint8_t z = 0xAA;
        CHECK(m4 && m4->readAt(95, &z, 1) && z == 0, "and a grown medium is zero-filled");
        m3.reset();
        m4.reset();

        // Protected FOR the operator (Patrick: auto-RO, and say so).
        // Clear EVERY write bit, not just owner_write: a host reports one file as
        // read-only or not (Windows has a single read-only attribute, which the
        // C++ filesystem maps from *all* the write bits, not owner_write alone), so
        // dropping just one leaves the file writable there and it is never protected.
        constexpr auto all_write =
            fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
        fs::permissions(p, all_write, fs::perm_options::remove);
        auto ro = openHostFile(p.string(), /*readOnly=*/false, err);
        CHECK(ro != nullptr, "an unwritable file still MOUNTS -- it is a disk with the tab out");
        CHECK(ro && ro->readOnly(), "read-only");
        CHECK(ro && ro->readOnlyForced(), "and it says WE did that, so the board can tell them");
        CHECK(ro && !ro->writeAt(0, w, 1), "the write bounces here, not at sync time");

        auto asked = openHostFile(p.string(), /*readOnly=*/true, err);
        CHECK(asked && asked->readOnly(), "MOUNT ... RO is read-only");
        CHECK(asked && !asked->readOnlyForced(), "but that one was ASKED for -- nothing to report");

        // Unmount before we delete. A live HostFile keeps the image open (sync()
        // holds the write handle so it can patch in place), and a host that will not
        // delete an open file -- Windows is one -- makes fs::remove throw here; POSIX
        // just unlinks it out from under the handle. Dropping the handles first is
        // what a real UNMOUNT does anyway, so the teardown models the real thing.
        m.reset();
        m2.reset();
        ro.reset();
        asked.reset();

        fs::permissions(p, all_write, fs::perm_options::add);
        fs::remove(p);
    }

    SECTION("media: the resolver seam");
    {
        std::string err;
        CHECK(openMedia("no/such/file.dsk", false, err) == nullptr, "a missing file is an error");
        CHECK(!err.empty(), "and it says so");

        // Swap the host filesystem out from under openMedia() -- which is the whole
        // point of the seam, and how a board test mounts a disk that is not on disk.
        setMediaResolver([](const std::string& path, bool ro, std::string&) {
            return std::unique_ptr<MediaFile>(
                new MemoryMedia(path, std::vector<uint8_t>(256, 0x42), ro));
        });
        auto m = openMedia("pretend.dsk", false, err);
        CHECK(m != nullptr, "the installed resolver answered");
        CHECK(m && m->size() == 256, "with the medium it made");
        CHECK(m && m->describe() == "pretend.dsk", "describe() is the path, for SHOW");
        setMediaResolver(openHostFile);  // put the real one back
    }

    SECTION("disk: CHS offsets");
    {
        auto  raw = mem(77 * 32 * 137);
        auto* mm  = static_cast<MemoryMedia*>(raw.get());
        auto  d   = dcdd8(std::move(raw));

        CHECK(d->tracks() == 77 && d->heads() == 1, "the shape the board declared");
        CHECK(d->geometryBytes() == 337568, "77 x 32 x 137");

        TrackFormat f{};
        CHECK(d->trackFormat(0, 0, f), "track 0 is formatted");
        CHECK(f.sectors == 32 && f.sectorSize == 137 && f.startSector == 0, "as declared");
        CHECK(!d->trackFormat(77, 0, f), "track 77 is off the end of a 77-track disk");

        uint8_t buf[137] = {};
        size_t  n        = sizeof(buf);
        CHECK(d->readSector(0, 0, 0, buf, &n), "track 0 sector 0");
        CHECK(n == 137, "and it is 137 bytes: the whole hard-sector slot");
        CHECK(buf[0] == 0, "which starts at offset 0");

        n = sizeof(buf);
        CHECK(d->readSector(1, 0, 3, buf, &n), "track 1 sector 3");
        uint64_t want = (uint64_t)1 * 32 * 137 + (uint64_t)3 * 137;  // 4795
        CHECK(buf[0] == (uint8_t)(want % 251), "landed at 1*32*137 + 3*137");

        n = sizeof(buf);
        CHECK(!d->readSector(0, 0, 32, buf, &n), "sector 32 does not exist on a 32-sector track");
        n = sizeof(buf);
        CHECK(!d->readSector(0, 0, -1, buf, &n), "nor does sector -1");
        n = 128;
        CHECK(!d->readSector(0, 0, 0, buf, &n), "a buffer too small FAILS, it does not truncate");

        // A write, and it must land exactly where the read did.
        uint8_t w[137];
        for (int i = 0; i < 137; ++i) w[i] = (uint8_t)(0xA0 + (i & 15));
        n = 137;
        CHECK(d->writeSector(76, 0, 31, w, &n), "the last sector of the last track");
        n = sizeof(buf);
        CHECK(d->readSector(76, 0, 31, buf, &n) && buf[0] == 0xA0 && buf[136] == w[136],
              "read it straight back");
        CHECK(mm->bytes()[337568 - 137] == 0xA0, "at the right host offset: the last slot");
        CHECK(mm->size() == 337568, "and the write did not GROW the image");
    }

    SECTION("disk: startSector -- the off-by-one that corrupts a disk");
    {
        // The Tarbell: soft sector, 128-byte payload, sectors numbered from ONE.
        auto raw = mem(77 * 26 * 128);
        auto d   = std::make_unique<DiskImage>(std::move(raw));
        d->init(77, 1, false);
        d->initFormat(0, 76, 0, 0, Density::SD, 26, 128, 1);

        CHECK(d->geometryBytes() == 256256, "77 x 26 x 128");

        uint8_t buf[128] = {};
        size_t  n        = sizeof(buf);
        CHECK(!d->readSector(0, 0, 0, buf, &n), "there is no sector 0 on a 1-based disk");
        n = sizeof(buf);
        CHECK(d->readSector(0, 0, 1, buf, &n), "sector 1 is the FIRST sector");
        CHECK(buf[0] == 0, "and it lives at offset 0 -- not at 128");
        n = sizeof(buf);
        CHECK(d->readSector(0, 0, 26, buf, &n), "sector 26 is the last");
        CHECK(buf[0] == (uint8_t)((25 * 128) % 251), "at offset 25*128");
        n = sizeof(buf);
        CHECK(!d->readSector(0, 0, 27, buf, &n), "27 is off the end");
    }

    SECTION("disk: two initFormat ranges -- a single-density boot track");
    {
        // The case DESIGN.md 7.3 says one global geometry cannot express: track 0
        // stays SD/128 so the boot PROM can read it; the rest is DD/256.
        uint64_t bytes = 26ull * 128 + 76ull * 26 * 256;
        auto     d     = std::make_unique<DiskImage>(mem((size_t)bytes));
        d->init(77, 1, false);
        d->initFormat(0, 0, 0, 0, Density::SD, 26, 128, 1);
        d->initFormat(1, 76, 0, 0, Density::DD, 26, 256, 1);

        CHECK(d->geometryBytes() == bytes, "the tracks are not all the same size");

        TrackFormat f{};
        CHECK(d->trackFormat(0, 0, f) && f.sectorSize == 128 && f.density == Density::SD,
              "track 0 is single density");
        CHECK(d->trackFormat(1, 0, f) && f.sectorSize == 256 && f.density == Density::DD,
              "track 1 is double");

        uint8_t buf[256] = {};
        size_t  n        = sizeof(buf);
        CHECK(d->readSector(1, 0, 1, buf, &n), "track 1 sector 1");
        CHECK(n == 256, "is 256 bytes");
        // Track 1 begins AFTER the short track 0, not after a 256-byte-sized one.
        CHECK(buf[0] == (uint8_t)((26 * 128) % 251), "and begins where the SHORT track 0 ended");
    }

    SECTION("disk: heads, and image order");
    {
        // Interleaved: T0H0, T0H1, T1H0... Not interleaved: all of head 0, then head 1.
        for (bool inter : {false, true}) {
            auto d = std::make_unique<DiskImage>(mem(4 * 2 * 10 * 128));
            d->init(4, 2, inter);
            d->initFormat(0, 3, 0, 1, Density::SD, 10, 128, 0);

            uint8_t buf[128] = {};
            size_t  n        = sizeof(buf);
            CHECK(d->readSector(1, 1, 0, buf, &n), "track 1, head 1");
            uint64_t want = inter ? (1ull * 2 + 1) * 10 * 128   // T0H0 T0H1 [T1H0] T1H1
                                  : (1ull * 4 + 1) * 10 * 128;  // head 0 (4 tracks), then T1H0..
            CHECK(buf[0] == (uint8_t)(want % 251),
                  inter ? "interleaved: T0H0 T0H1 T1H0 T1H1" : "sequential: all of head 0 first");
        }
    }

    SECTION("disk: the 337,664-byte trap (XMODEM padding)");
    {
        CHECK(!sizeMatches(337567, 337568), "a SHORT image is not a match");
        CHECK(sizeMatches(337568, 337568), "the exact size matches");
        CHECK(sizeMatches(337664, 337568), "and so does XMODEM's 128-byte pad -- 96 bytes of slop");
        CHECK(!sizeMatches(337696, 337568), "but a whole extra block over is a different disk");

        // The real thing: an image with the pad on it. Every sector still reads, and
        // the pad is never data.
        auto  raw = mem(337664);
        auto* mm  = static_cast<MemoryMedia*>(raw.get());
        auto  d   = dcdd8(std::move(raw));
        CHECK(d->size() == 337664, "the medium carries the pad");
        CHECK(d->geometryBytes() == 337568, "the geometry does not");

        uint8_t buf[137];
        size_t  n = sizeof(buf);
        CHECK(d->readSector(76, 0, 31, buf, &n), "the last real sector reads");
        uint8_t w[137] = {0xFF};
        n              = 137;
        CHECK(d->writeSector(76, 0, 31, w, &n), "and writes");
        CHECK(mm->bytes()[337568] == (uint8_t)(337568 % 251), "the pad is UNTOUCHED by that write");
        CHECK(mm->size() == 337664, "and the image did not grow");
    }

    SECTION("disk: write protect, and a short image");
    {
        auto    d = dcdd8(mem(337568, /*ro=*/true));
        uint8_t w[137] = {};
        size_t  n      = 137;
        CHECK(d->readOnly(), "the disk is write-protected");
        CHECK(!d->writeSector(0, 0, 0, w, &n), "so the write is refused at the disk, not the file");

        // A truncated image: the geometry says 337,568 bytes and the file has fewer.
        // A write past the end must FAIL, not silently manufacture the missing tracks.
        auto s = dcdd8(mem(1000));
        n      = 137;
        CHECK(!s->writeSector(50, 0, 0, w, &n), "a short image does not grow under a write");
        uint8_t buf[137];
        n = sizeof(buf);
        CHECK(!s->readSector(50, 0, 0, buf, &n), "and reads off the end of it fail");
    }

    SECTION("tape: read, write, rewind");
    {
        std::vector<uint8_t> t = {0x11, 0x22, 0x33};
        TapeImage tape(std::make_unique<MemoryMedia>("basic.tap", t));

        CHECK(tape.size() == 3 && tape.pos() == 0, "at the start of the tape");
        CHECK(!tape.atEnd(), "there is tape to play");

        uint8_t b = 0;
        CHECK(tape.read(b) && b == 0x11, "byte 1");
        CHECK(tape.read(b) && b == 0x22, "byte 2");
        CHECK(tape.pos() == 2, "the head has moved -- this is what SHOW displays");
        CHECK(tape.read(b) && b == 0x33, "byte 3");
        CHECK(tape.atEnd(), "the end of the tape");
        CHECK(!tape.read(b), "and reading past it is FALSE, not an error and not a zero");

        // The whole feature, in one line.
        tape.rewind();
        CHECK(tape.pos() == 0 && !tape.atEnd(), "REWIND");
        CHECK(tape.read(b) && b == 0x11, "and it plays again from the top");

        // Recording. Over the tape where there is tape, extending it at the end.
        tape.rewind();
        CHECK(tape.write(0xAA), "record over byte 1");
        CHECK(tape.pos() == 1, "the head advanced");
        tape.rewind();
        CHECK(tape.read(b) && b == 0xAA, "and it took");

        while (!tape.atEnd()) tape.read(b);
        CHECK(tape.write(0x55), "recording at the end of the tape EXTENDS it");
        CHECK(tape.size() == 4, "the tape is longer");
    }
    {
        std::vector<uint8_t> t = {1, 2};
        TapeImage tape(std::make_unique<MemoryMedia>("basic.tap", t, /*ro=*/true));
        CHECK(tape.readOnly(), "the tab is out");
        CHECK(!tape.write(0xFF), "so it will not record");
        CHECK(tape.size() == 2, "and the tape is unchanged");
    }

    SECTION("tape: TapeStream IS a ByteStream");
    {
        // This is the adapter the 88-ACR is built on: the shared UART sees a
        // ByteStream and never learns it is a cassette.
        std::vector<uint8_t> t = {0x7D, 0x7D, 0xC3};
        TapeImage  tape(std::make_unique<MemoryMedia>("4kbasic.tap", t));
        TapeStream s(tape);  // PLAY, by default -- see below
        ByteStream& bs = s;  // what the chip will actually hold

        CHECK(bs.describe() == "4kbasic.tap", "SHOW says what is mounted");
        CHECK(bs.readable(), "there is tape: RDRF will set");

        uint8_t buf[2] = {};
        CHECK(bs.read(buf, 2) == 2 && buf[0] == 0x7D, "the leader");
        CHECK(bs.readByte() == 0xC3, "and the byte after it");
        CHECK(!bs.readable(), "end of tape: the line goes quiet");
        CHECK(bs.read(buf, 2) == 0, "and a read returns NOTHING -- it does not block or fail");

        // The ByteStream rule, on a tape: the byte waits for the card. A card that
        // reads one byte per character time must find the next one still there.
        tape.rewind();
        CHECK(bs.readable() && bs.readByte() == 0x7D, "rewound, and the leader is back");
        CHECK(bs.readByte() == 0x7D, "byte held until taken -- no loss, ever");
    }

    // -----------------------------------------------------------------------
    // 🔴 PLAY OR RECORD. NEVER BOTH -- AND THIS TEST USED TO ASSERT THE OPPOSITE.
    //
    // It said `CHECK(bs.writable(), "and it is not protected")` on a stream that was
    // also readable, and that was wrong in a way nothing could see until a real card
    // used it. A cassette has ONE head, so read and write share ONE position (they
    // must -- it is the same piece of tape). The 88-ACR's UART reads EAGERLY: it pulls
    // a byte off its line the moment it has room, because that is how DAV works.
    //
    // So a tape that was readable AND writable at once had its first byte pulled away
    // by the card before the guest ever ran, the head sat at 1, and every recording
    // began at byte ONE. Silently, on every tape, in the one direction nobody checks.
    //
    // The fix is the hardware's own, and it costs nothing: the 88-ACR has NO MOTOR
    // CONTROL -- a human pressed the buttons -- and a recorder is in PLAY or in
    // RECORD. Making that exclusive here makes the corruption UNREPRESENTABLE rather
    // than merely unlikely.
    // -----------------------------------------------------------------------
    SECTION("tape: a recorder is in PLAY or in RECORD, and never in both");
    {
        std::vector<uint8_t> t = {'O', 'L', 'D'};
        auto  media = std::make_unique<MemoryMedia>("t.tap", t);
        auto* raw   = media.get();
        TapeImage tape(std::move(media));

        {
            TapeStream play(tape, TapeStream::Mode::Play);
            CHECK(play.readable(), "PLAY: the tape plays back");
            CHECK(!play.writable(), "PLAY: and NOTHING is cut into it -- the head cannot move");
        }
        {
            TapeStream rec(tape, TapeStream::Mode::Record);
            CHECK(!rec.readable(), "RECORD: a recording deck plays nothing back...");
            CHECK(rec.writable(), "RECORD: ...it records");

            // And it records from where the head IS -- which, having never played, is 0.
            CHECK(tape.pos() == 0, "the head never moved while it was not playing");
            const uint8_t nu[3] = {'N', 'E', 'W'};
            CHECK(rec.write(nu, 3) == 3, "three bytes go down");
            CHECK(std::string(raw->bytes().begin(), raw->bytes().end()) == "NEW",
                  "and they land at byte ZERO -- off by one here corrupts every tape");
        }
        {
            // The write-protect tab is a SECOND and INDEPENDENT reason to refuse.
            TapeImage  ro(std::make_unique<MemoryMedia>("t.tap", t, /*readOnly=*/true));
            TapeStream rec(ro, TapeStream::Mode::Record);
            CHECK(!rec.writable(), "RECORD is pressed, but the tab is out: nothing is recorded");
        }
    }
}
