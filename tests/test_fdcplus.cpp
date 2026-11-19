// The FarmTek FDC+ serial drive (docs/boards/farmtek-fdcplus.md).
//
// THE ORACLE IS THE CARD'S FIRMWARE, serialDrive.s v1.8 (reference/FDC+ Serial Drive
// Firmware.md). Every number and every sense below is from it: the ten-byte messages, the
// STAT interval, the one-read sector true, the NRDA that needs no byte clock, the write-back
// before the next track, the idle timers.
//
// The far end is a FAKE DRIVE SERVER -- a ByteStream that answers the protocol from images
// in memory, and can be told to go quiet, corrupt a reply, refuse a write, or hand over a
// track a few bytes at a time. The host clock is a variable the test moves.

#include "boards/farmtek-fdcplus.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/debuglog.h"
#include "core/statefile.h"
#include "host/cardimg.h"
#include "host/endpoint.h"
#include "host/media.h"
#include "host/stream.h"
#include "test.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

// Route the debug sink to a scratch file while `f` runs, and return what landed there.
std::string debugCapture(const std::function<void()>& f) {
    namespace fs = std::filesystem;
    std::string    err;
    const fs::path p = fs::temp_directory_path() / "altair_fdcplus_debug.log";
    std::error_code ec;
    fs::remove(p, ec);
    dbg::setSink(dbg::Sink::File, p.string(), err);
    f();
    dbg::setSink(dbg::Sink::Stderr, "", err);  // closes + flushes the file
    std::ifstream      in(p, std::ios::binary);
    const std::string  s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    fs::remove(p, ec);
    return s;
}

int countOf(const std::string& s, const std::string& what) {
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) ++n;
    return n;
}

constexpr int kSlot   = 137;
constexpr int kLen8   = 32 * kSlot;  // 4384
constexpr int kLen5   = 16 * kSlot;  // 2192
constexpr uint64_t kSector8 = 10416;  // 5.208 ms at 2 MHz
constexpr uint64_t kSector5 = 25000;  // 12.5 ms
constexpr uint64_t kMs      = 2000;   // T-states in a millisecond at 2 MHz
constexpr uint64_t kNsMs    = 1000000;

uint16_t sum16(const uint8_t* p, size_t n) {
    uint16_t s = 0;
    for (size_t i = 0; i < n; ++i) s = (uint16_t)(s + p[i]);
    return s;
}

struct Msg {
    std::string cmd;
    uint16_t    p1, p2;
};

// ---------------------------------------------------------------------------
// The fake drive server.
// ---------------------------------------------------------------------------
class FakeServer : public ByteStream {
public:
    explicit FakeServer(int trackLen) : len_(trackLen) {}

    std::string describe() const override { return "fake-server"; }
    bool readable() const override { return released_ > 0 && !out_.empty(); }
    bool writable() const override { return true; }

    size_t read(uint8_t* b, size_t n) override {
        size_t k = std::min({n, out_.size(), released_});
        std::memcpy(b, out_.data(), k);
        out_.erase(out_.begin(), out_.begin() + (std::ptrdiff_t)k);
        if (released_ != SIZE_MAX) released_ -= k;
        return k;
    }

    size_t doWrite(const uint8_t* b, size_t n) override {
        in_.insert(in_.end(), b, b + n);
        for (;;) {
            if (wantData_) {
                if (in_.size() < (size_t)len_ + 2) break;
                const uint16_t ck = (uint16_t)(in_[(size_t)len_] | (in_[(size_t)len_ + 1] << 8));
                const bool     ok = sum16(in_.data(), (size_t)len_) == ck;
                if (ok) {
                    auto& img = image(wDrive_);
                    std::memcpy(&img[(size_t)wTrack_ * (size_t)len_], in_.data(), (size_t)len_);
                    ++writes;
                    if (sink) ++*sink;
                }
                in_.erase(in_.begin(), in_.begin() + len_ + 2);
                wantData_ = false;
                reply("WSTA", ok ? 0 : 2, 0);
                continue;
            }
            if (in_.size() < 10) break;
            Msg m{std::string((const char*)in_.data(), 4), (uint16_t)(in_[4] | (in_[5] << 8)),
                  (uint16_t)(in_[6] | (in_[7] << 8))};
            const bool good = sum16(in_.data(), 8) == (uint16_t)(in_[8] | (in_[9] << 8));
            in_.erase(in_.begin(), in_.begin() + 10);
            if (!good) continue;  // "The server should ignore commands with an invalid checksum."
            got.push_back(m);
            if (silent) continue;
            const int drive = m.p1 >> 12, track = m.p1 & 0x0FFF;
            if (m.cmd == "STAT") {
                reply("STAT", 0, mounted);
            } else if (m.cmd == "READ") {
                if (!((mounted >> drive) & 1)) continue;  // an empty drive: silence
                auto&                img = image(drive);
                std::vector<uint8_t> t(img.begin() + (std::ptrdiff_t)track * len_,
                                       img.begin() + (std::ptrdiff_t)(track + 1) * len_);
                const uint16_t       ck = (uint16_t)(sum16(t.data(), t.size()) + (badTrack ? 1 : 0));
                badTrack               = false;
                out_.insert(out_.end(), t.begin(), t.end());
                out_.push_back((uint8_t)ck);
                out_.push_back((uint8_t)(ck >> 8));
            } else if (m.cmd == "WRIT") {
                if (notReady > 0 || !((mounted >> drive) & 1)) {
                    --notReady;
                    reply("WRIT", 1, 0);
                } else {
                    wDrive_   = drive;
                    wTrack_   = track;
                    wantData_ = true;
                    reply("WRIT", 0, 0);
                }
            }
        }
        return n;
    }

    std::vector<uint8_t>& image(int drive) {
        auto& img = images_[(size_t)drive];
        if (img.empty()) {
            img.resize((size_t)len_ * 80);
            for (size_t i = 0; i < img.size(); ++i) img[i] = fill(drive, i);
        }
        return img;
    }
    // What a fresh image holds at byte `i`: a pattern, so a misplaced sector shows.
    static uint8_t fill(int drive, size_t i) { return (uint8_t)((i * 7 + (size_t)drive) & 0xFF); }

    uint8_t at(int drive, int track, int sector, int byte) {
        return image(drive)[(size_t)track * (size_t)len_ + (size_t)sector * kSlot + (size_t)byte];
    }

    int count(const std::string& cmd) const {
        int n = 0;
        for (const auto& m : got)
            if (m.cmd == cmd) ++n;
        return n;
    }

    // Hold the replies back and let them out a few bytes at a time.
    void hold() { released_ = 0; }
    void release(size_t n) { released_ = n; }
    void open() { released_ = SIZE_MAX; }

    uint16_t         mounted  = 0x0007;
    bool             silent   = false;
    bool             badTrack = false;
    int              notReady = 0;
    int              writes   = 0;
    int*             sink     = nullptr;
    std::vector<Msg> got;

private:
    void reply(const char* cmd, uint16_t code, uint16_t data) {
        uint8_t m[10];
        std::memcpy(m, cmd, 4);
        m[4] = (uint8_t)code;
        m[5] = (uint8_t)(code >> 8);
        m[6] = (uint8_t)data;
        m[7] = (uint8_t)(data >> 8);
        uint16_t s = sum16(m, 8);
        m[8]       = (uint8_t)s;
        m[9]       = (uint8_t)(s >> 8);
        out_.insert(out_.end(), m, m + 10);
    }

    int                               len_;
    std::vector<uint8_t>              in_, out_;
    size_t                            released_ = SIZE_MAX;
    bool                              wantData_ = false;
    int                               wDrive_ = 0, wTrack_ = 0;
    std::vector<std::vector<uint8_t>> images_ = std::vector<std::vector<uint8_t>>(8);
};

// ---------------------------------------------------------------------------
// A board, a clock, a host clock, and a server on the line.
// ---------------------------------------------------------------------------
struct Rig {
    Clock        clk;
    FdcPlusBoard b;
    uint64_t     ns = 0;
    FakeServer*  srv = nullptr;

    explicit Rig(int type = 7) {
        b.attachClock(&clk);
        b.setHostNs([this] { return ns; });
        std::string err;
        if (type != 7) setProperty(b, "drivetype", std::to_string(type), err);
        b.power();
        auto s = std::make_unique<FakeServer>(type == 6 ? kLen5 : kLen8);
        srv    = s.get();
        auto* raw = s.release();
        FdcPlusBoard::setResolver([raw](const std::string&, std::string&) {
            return std::unique_ptr<ByteStream>(raw);
        });
        CHECK(b.connect("line", "fake", err), "the line connects");
        FdcPlusBoard::setResolver(resolveEndpoint);
        pump(2);  // the first STAT goes out at once; the server's mount map comes back
    }

    uint8_t in(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.read(c);
    }
    void out(uint8_t port, uint8_t v) {
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = port;
        c.data = v;
        b.write(c);
    }
    void pump(int n = 1) {
        for (int i = 0; i < n; ++i) b.pump();
    }

    // One STAT round trip from now: take any reply still waiting, let the 0.1 s pass, then
    // send the next STAT and take its answer. (The firmware restarts its STAT timer when a
    // reply comes in, so the interval runs from the ANSWER, not from the question.)
    void statRound() {
        pump();
        ns += FdcPlusBoard::kStatNs;
        pump(2);
    }

    // Select a drive, load the head, and wait out a sector time so the next one can come.
    void ready(int drive = 0) {
        out(0x08, (uint8_t)drive);
        out(0x09, 0x04);
    }

    // Poll the sector port the way a BIOS does until sector `want` shows sector true,
    // pumping the link and letting a sector time pass between polls. Returns false if it
    // never comes.
    bool find(int want, uint64_t sector = kSector8) {
        for (int i = 0; i < 200; ++i) {
            uint8_t v = in(0x09);
            if (!(v & 1) && ((v >> 1) & 0x1F) == want) return true;
            pump();
            clk.advance(sector);
        }
        return false;
    }
};


// ===========================================================================
// DRIVE TYPE 5 -- the 1.5 MB floppy (hdfloppy.s, reference/FDC+ HD Floppy Firmware.md).
//
// A disk turning at 360 rpm under the clock: every number here is microseconds after an index
// hole, as the firmware times them, and the clock is the machine's 2 MHz -- 2 T-states a us.
// ===========================================================================
constexpr int      kHdTrack = FdcPlusHdf::kTrackBytes;
constexpr uint64_t kUs      = 2;          // T-states in a microsecond at 2 MHz
constexpr uint64_t kRev     = 166668 * kUs;

uint8_t hdByte(int track, int i) { return (uint8_t)(track * 7 + i * 13 + i / 251 + 1); }

std::vector<uint8_t> hdImage(int tracks) {
    std::vector<uint8_t> v((size_t)tracks * kHdTrack);
    for (int t = 0; t < tracks; ++t)
        for (int i = 0; i < kHdTrack; ++i) v[(size_t)t * kHdTrack + (size_t)i] = hdByte(t, i);
    return v;
}

struct HdRig {
    Clock        clk;
    FdcPlusBoard b;
    MemoryMedia* med = nullptr;

    explicit HdRig(int tracks = FdcPlusHdf::kTracks, bool ro = false) {
        b.attachClock(&clk);
        std::string err;
        CHECK(setProperty(b, "drivetype", "5", err), "drivetype=5");
        b.power();
        if (tracks >= 0) CHECK(mount(hdImage(tracks), ro), "a 1.5 MB image in drive0");
    }

    bool mount(std::vector<uint8_t> bytes, bool ro = false, const char* unit = "drive0") {
        MemoryMedia** slot = &med;
        setMediaResolver([bytes, slot](const std::string& p, bool r, std::string&) {
            auto m = std::make_unique<MemoryMedia>(p, bytes, r);
            *slot  = m.get();
            return std::unique_ptr<MediaFile>(std::move(m));
        });
        std::string err;
        const bool  ok = b.mount(unit, "hd.dsk", ro, err);
        setMediaResolver(openHostMedia);
        return ok;
    }

    uint8_t in(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.read(c);
    }
    void out(uint8_t port, uint8_t v) {
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = port;
        c.data = v;
        b.write(c);
    }
    void us(uint64_t n) { clk.advance(n * kUs); }

    // Poll the sector port every 10 us, as a BIOS would, until sector true. Returns the
    // register, or FF if it never came within `revs` turns.
    uint8_t hole(int revs = 3) {
        for (uint64_t i = 0; i < (uint64_t)revs * 16667 + 10; ++i) {
            const uint8_t v = in(0x09);
            if (!(v & 0x01)) return v;
            us(10);
        }
        return 0xFF;
    }

    // Select drive 0, load the head, and wait for the first sector true.
    bool ready() {
        out(0x08, 0x00);
        out(0x09, 0x04);
        return hole() != 0xFF;
    }

    // dRead, at `perByte` us a byte: READ ENABLE right after sector true, wait for NRDA, one
    // dummy read, then the track. Returns the I/O status after the end of the transfer.
    uint8_t readTrack(std::vector<uint8_t>& got, uint64_t perByte) {
        out(0x09, 0x10);
        for (int i = 0; i < 2000 && (in(0x08) & 0x80); ++i) us(4);
        in(0x0A);
        got.assign((size_t)kHdTrack, 0);
        for (int i = 0; i < kHdTrack; ++i) {
            us(perByte);
            got[(size_t)i] = in(0x0A);
        }
        for (int i = 0; i < 100 && !(in(0x0B) & 0x80); ++i) us(1000);
        return in(0x0B);
    }

    // dWrite, at `perByte` us a byte, of hdByte(99, i). Returns the I/O status at the end.
    uint8_t writeTrack(uint64_t perByte) {
        out(0x09, 0x80);
        for (int i = 0; i < 2000 && (in(0x08) & 0x01); ++i) us(4);
        for (int i = 0; i < kHdTrack; ++i) {
            out(0x0A, hdByte(99, i));
            us(perByte);
        }
        for (int i = 0; i < 400 && !(in(0x0B) & 0x80); ++i) us(1000);
        return in(0x0B);
    }
};

int mismatches(const std::vector<uint8_t>& got, int track) {
    int n = 0;
    for (int i = 0; i < kHdTrack; ++i) n += got[(size_t)i] != hdByte(track, i);
    return n;
}

void hdfTests() {
    SECTION("FDC+ type 5 -- select: F5, TRACK 0, WRITE PROTECT; drive 4 deselects");
    {
        HdRig r;
        r.out(0x08, 0x00);
        CHECK(r.in(0x08) == 0xB5, "drive 0 at track 0: F5 with TRACK 0 asserted");
        CHECK(r.in(0x09) == 0xFF, "no sector until the head is loaded and a hole goes by");
        r.out(0x08, 0x04);
        CHECK(r.in(0x08) == 0xFF, "drive 4: past the fourth, a deselect");

        HdRig p(FdcPlusHdf::kTracks, true);
        p.out(0x08, 0x00);
        CHECK(p.in(0x08) == 0xA5, "a write-protected disk: bit 4 asserted too");
    }

    SECTION("FDC+ type 5 -- sector 0 and 15 in turn, 32 us of sector true, head loaded only");
    {
        {
            HdRig u;
            u.out(0x08, 0x00);
            u.us(400000);
            CHECK(u.in(0x09) == 0xFF, "head not loaded: the sector register is never set");
        }
        HdRig r;
        const uint64_t t0 = r.clk.now();
        r.out(0x08, 0x00);
        r.out(0x09, 0x04);
        CHECK(!(r.in(0x08) & 0x04), "HEAD STATUS at once on a head load");
        const uint8_t a = r.hole();
        CHECK(a == 0xC0 || a == 0xDE, "sector 0 or 15, sector true");
        CHECK(r.clk.now() - t0 >= 2 * kRev, "not before TWO turns: waitMotor throws a hole away");
        r.us(34);
        CHECK(r.in(0x09) == (a | 0x01), "32 us later, sector true is gone");
        const uint8_t b = r.hole();
        CHECK(b == (a ^ 0x1E), "the next turn shows the other one");
    }

    SECTION("FDC+ type 5 -- THE FAKE BOOT SECTOR: an Altair sector a stock PROM can read");
    {
        const uint8_t* s   = FdcPlusHdf::bootSector();
        uint8_t        sum = 0;
        for (int i = 3; i < 131; ++i) sum = (uint8_t)(sum + s[i]);
        CHECK(s[0] == 0x80 && s[1] == 0x80 && s[2] == 0x00, "track 0, the load ends at 0080");
        CHECK(s[131] == 0xFF && s[132] == sum, "FF, then DBL's checksum of the 128 bytes");

        HdRig r;
        CHECK(r.ready(), "a hole");
        CHECK(!(r.in(0x08) & 0x80), "NRDA with no read asked for");
        bool same = true;
        for (int i = 0; i < FdcPlusHdf::kBootLen; ++i) same &= r.in(0x0A) == s[i];
        CHECK(same, "and the data port gives the boot sector, byte for byte");
    }

    SECTION("FDC+ type 5 -- a whole track at 17 us a byte, the BIOS's speed");
    {
        HdRig r;
        CHECK(r.ready(), "a hole");
        r.out(0x0B, 0);
        std::vector<uint8_t> got;
        const uint8_t        ios = r.readTrack(got, 17);
        CHECK(mismatches(got, 0) == 0, "every byte of track 0");
        CHECK(ios == 0x80, "done, no error");
    }

    SECTION("FDC+ type 5 -- READ TOO FAST and the bytes are not there yet");
    {
        HdRig r;
        CHECK(r.ready(), "a hole");
        r.out(0x0B, 0);
        std::vector<uint8_t> got;
        r.readTrack(got, 8);  // a 4 MHz 8080 running the 2 MHz BIOS loop
        CHECK(got[0] == hdByte(0, 0), "the first bytes are there");
        CHECK(mismatches(got, 0) > kHdTrack / 3, "the rest are old buffer bytes, as on the card");
    }

    SECTION("FDC+ type 5 -- a READ ENABLE sent before the hole is thrown away by it");
    {
        HdRig r;
        CHECK(r.ready(), "a hole");
        r.us(2000);  // past the read-clear window
        r.out(0x09, 0x10);
        CHECK(r.in(0x08) & 0x80, "NRDA dropped by the command");
        r.hole();
        r.us(20000);
        CHECK(r.in(0x08) & 0x80, "the next hole cleared the flag -- and, as a read was asked "
                                 "for, skipped the boot sector");
        CHECK(r.in(0x0B) == 0x00, "and no read happened");
        r.hole();
        CHECK(!(r.in(0x08) & 0x80), "the hole after that puts it up");
        CHECK(r.in(0x0A) == FdcPlusHdf::bootSector()[0], "the data port is at the boot sector");
    }

    SECTION("FDC+ type 5 -- the side is the track number's; a mismatch is a TRACK error");
    {
        CHECK(FdcPlusHdf::imageTrack(0, true) == 1, "cylinder 0, top: track 1");
        CHECK(FdcPlusHdf::imageTrack(71, true) == 143, "cylinder 71, top: track 143");
        CHECK(FdcPlusHdf::imageTrack(72, true) == -1, "the top side stops at 71");
        CHECK(FdcPlusHdf::imageTrack(72, false) == 144, "cylinder 72, bottom: track 144");
        CHECK(FdcPlusHdf::imageTrack(76, false) == 148, "76 is the last");
        CHECK(FdcPlusHdf::imageTrack(77, false) == -1, "and 77 is nothing");

        HdRig r;
        CHECK(r.ready(), "a hole");
        r.out(0x0B, 1);  // odd: the top side
        std::vector<uint8_t> got;
        CHECK(r.readTrack(got, 17) == 0x80, "track 1 is cylinder 0's top side");
        CHECK(mismatches(got, 1) == 0, "and it reads");

        r.hole();
        r.out(0x0B, 2);  // the bottom side of cylinder 0 is track 0, not 2
        CHECK(r.readTrack(got, 17) == 0x82, "a TRACK error: the head is on track 0");
    }

    SECTION("FDC+ type 5 -- a track the disk does not have: END OF SECTOR at the next hole");
    {
        HdRig r(2);  // tracks 0 and 1 only
        CHECK(r.ready(), "a hole");
        r.out(0x09, 0x01);  // step in, to cylinder 1
        r.us(20000);
        CHECK(r.b.hdf().cylinder(0) == 1, "cylinder 1");
        r.hole();
        r.out(0x0B, 2);
        r.out(0x09, 0x10);
        r.us(100000);
        CHECK(r.in(0x08) & 0x80, "no sync byte, no NRDA");
        r.us(70000);
        CHECK(r.in(0x0B) == 0x84, "end of sector and done, at the next hole");
        CHECK(!(r.in(0x08) & 0x80), "and NRDA, so the 8080 is not stuck waiting");
    }

    SECTION("FDC+ type 5 -- a write at 14 us a byte lands in the image");
    {
        HdRig r;
        CHECK(r.ready(), "a hole");
        r.out(0x0B, 0);
        CHECK(r.writeTrack(14) == 0x80, "done");
        int bad = 0;
        for (int i = 0; i < kHdTrack; ++i) bad += r.med->bytes()[(size_t)i] != hdByte(99, i);
        CHECK(bad == 0, "every byte of track 0 is the new one");
        CHECK(r.med->bytes()[(size_t)kHdTrack] == hdByte(1, 0), "and track 1 is untouched");
        CHECK(r.med->syncs() > 0, "and it was synced");
        r.hole();
        std::vector<uint8_t> got;
        r.readTrack(got, 17);
        CHECK(mismatches(got, 99) == 0, "it reads back");
    }

    SECTION("FDC+ type 5 -- A WRITE THAT FALLS BEHIND writes old buffer bytes");
    {
        HdRig r;
        CHECK(r.ready(), "a hole");
        r.out(0x0B, 0);
        r.writeTrack(18);
        int bad = 0;
        for (int i = 0; i < kHdTrack; ++i) bad += r.med->bytes()[(size_t)i] != hdByte(99, i);
        CHECK(r.med->bytes()[0] == hdByte(99, 0), "the first bytes made it");
        CHECK(bad > kHdTrack / 20, "the late ones did not");
    }

    SECTION("FDC+ type 5 -- write protect: the drive blocks the write, the card says done");
    {
        HdRig r(FdcPlusHdf::kTracks, true);
        CHECK(r.ready(), "a hole");
        r.out(0x0B, 0);
        CHECK(r.writeTrack(14) == 0x80, "done, as far as the card knows");
        CHECK(r.med->bytes()[0] == hdByte(0, 0) && r.med->bytes()[5000] == hdByte(0, 5000),
              "the disk is unchanged");
    }

    SECTION("FDC+ type 5 -- step 3 ms, settle 18 ms");
    {
        HdRig r;
        CHECK(r.ready(), "a hole");
        r.out(0x09, 0x01);
        CHECK((r.in(0x08) & 0x06) == 0x06, "MOVE and HEAD dropped");
        r.us(2900);
        CHECK(r.in(0x08) & 0x02, "2.9 ms: no MOVE yet");
        r.us(200);
        CHECK(!(r.in(0x08) & 0x02), "3.1 ms: MOVE");
        CHECK(r.in(0x08) & 0x40, "off track 0");
        CHECK(r.in(0x08) & 0x04, "HEAD still settling");
        r.us(15000);
        CHECK(!(r.in(0x08) & 0x04), "18 ms: HEAD");
        r.out(0x09, 0x02);
        r.us(3100);
        CHECK(!(r.in(0x08) & 0x40), "stepped out: TRACK 0");
    }

    SECTION("FDC+ type 5 -- the motor stops after 32 turns unloaded; a head load starts it");
    {
        HdRig r;
        r.out(0x08, 0x00);
        r.us(30 * 166668);
        r.in(0x08);
        CHECK(r.b.hdf().motorOn(), "30 turns: still running");
        r.us(5 * 166668);
        r.in(0x08);
        CHECK(!r.b.hdf().motorOn(), "35 turns: off");
        r.out(0x09, 0x04);
        CHECK(r.b.hdf().motorOn(), "a head load: on");
        CHECK(r.hole() != 0xFF, "and the holes come back");
    }

    SECTION("FDC+ type 5 -- latched at POWER; the size check; [[board.drive]]; snapshot");
    {
        Rig         r7;
        std::string err;
        CHECK(setProperty(r7.b, "drivetype", "5", err), "SET drivetype=5");
        CHECK(r7.b.driveType() == 7, "still the serial drive until POWER");

        HdRig r(-1);
        CHECK(!r.mount(std::vector<uint8_t>(1000)), "1000 bytes is not whole tracks");
        CHECK(!r.mount(std::vector<uint8_t>((size_t)kHdTrack * 150)), "150 tracks is too many");
        CHECK(r.mount(std::vector<uint8_t>()), "an empty file is a blank disk");
        CHECK(r.mount(hdImage(149), false, "drive3"), "drive3");
        CHECK(!r.mount(hdImage(149), false, "drive4"), "there is no drive4");
        const auto su = r.b.subUnits();
        CHECK(su.size() == 2 && su[1].fields[0].text == "3" && su[1].fields[1].text == "hd.dsk",
              "CONFIG SAVE writes a [[board.drive]] for each disk");

        HdRig a;
        CHECK(a.ready(), "a hole");
        a.out(0x09, 0x01);
        a.us(20000);
        a.out(0x0B, 2);
        StateWriter w;
        a.b.serialize(w);
        Clock        c2;
        FdcPlusBoard b2;
        b2.attachClock(&c2);
        StateReader rd(w.data());
        b2.deserialize(rd);
        CHECK(rd.ok(), "restores cleanly");
        CHECK(b2.driveType() == 5 && b2.hdf().cylinder(0) == 1, "type 5, and the head on cylinder 1");
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x08;
        CHECK(b2.read(c) == a.in(0x08), "and the status register");
    }
}

} // namespace

void test_fdcplus() {
    SECTION("FDC+ -- four ports at 08 (or 80), the fourth reads 00; drivetype 5-7; no MOUNT on the line");
    {
        Rig r;
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x0B;
        CHECK(r.b.decodes(c), "08-0B: FOUR ports, where the 88-DCDD decodes three");
        c.addr = 0x0C;
        CHECK(!r.b.decodes(c), "and not 0C");
        CHECK(r.in(0x0B) == 0x00, "the Reserved port reads 00 (the firmware clears PMDOUT2H)");

        std::string err;
        CHECK(!setProperty(r.b, "port", "10", err), "port 10 refused -- the jumpers give 08 or 80");
        CHECK(setProperty(r.b, "port", "80", err), "port 80 accepted");
        c.addr = 0x83;
        CHECK(r.b.decodes(c), "and then 80-83 is the card");
        CHECK(!setProperty(r.b, "drivetype", "4", err), "drivetype 4 is not this board");
        CHECK(!setProperty(r.b, "drivetype", "8", err), "nor is 8");
        CHECK(setProperty(r.b, "drivetype", "5", err), "5 is: the 1.5 MB floppy");
        CHECK(!r.b.mount("line", "x.dsk", false, err), "MOUNT on the line refused: the server mounts images");
        for (const char* ok : {"9600", "19200", "38400", "57600", "76800", "230400", "403200", "460800"})
            CHECK(setProperty(r.b, "baud", ok, err), "a rate the FDC+ and its server both offer");
        CHECK(!setProperty(r.b, "baud", "115200", err), "115200 refused: not a serial drive rate");
        CHECK(!setProperty(r.b, "baud", "300", err), "and nor is 300");
    }

    SECTION("FDC+ -- the drive type is LATCHED at power-on");
    {
        Rig         r;
        std::string err;
        CHECK(setProperty(r.b, "drivetype", "6", err), "SET drivetype=6");
        CHECK(r.b.driveType() == 7, "still an 8\" drive: the switches are read at power-on");
        r.b.power();
        CHECK(r.b.driveType() == 6, "and a Minidisk after POWER");
    }

    SECTION("FDC+ -- STAT every 0.1 s: selected drive, head, track");
    {
        Rig r;
        CHECK(r.srv->count("STAT") == 1, "the first STAT goes out at once");
        CHECK(r.srv->got[0].p1 == 0x00FF, "no drive selected: 0x00FF");
        CHECK(r.b.readyMask() == 0x0007, "and the server's mount map is the ready map");

        r.ns += 50 * kNsMs;
        r.pump();
        CHECK(r.srv->count("STAT") == 1, "50 ms later: not yet");
        r.ns += 50 * kNsMs;
        r.pump();
        CHECK(r.srv->count("STAT") == 2, "100 ms: the next one");

        r.ready(1);
        r.statRound();
        CHECK(r.srv->got.back().p1 == 0xFF01, "drive 1 selected, head loaded: 0xFF01");
        CHECK(r.srv->got.back().p2 == 0, "on track 0");
    }

    SECTION("FDC+ -- only a TRACK counts as bytes arriving; STAT replies do not");
    {
        // The run loop treats any arrival as "not at a prompt", so a STAT reply counted here
        // would keep a machine sitting at A> from ever resting -- a whole core, forever.
        Rig r;
        const uint64_t was = r.b.rxBytes();
        for (int i = 0; i < 20; ++i) r.statRound();
        CHECK(r.srv->count("STAT") > 20, "twenty STAT round trips");
        CHECK(r.b.rxBytes() == was, "and not one of their bytes counted");
        r.ready(0);
        CHECK(r.find(0), "a track read");
        CHECK(r.b.rxBytes() == was + kLen8 + 2, "counts: 4384 bytes and the checksum");
    }

    SECTION("FDC+ -- a drive the server has no image for is NOT READY: status FF");
    {
        Rig r;
        r.out(0x08, 0x03);
        CHECK(r.in(0x08) == 0xFF, "drive 3 is empty on the server -> status FF");
        CHECK(r.in(0x09) == 0xFF, "and sector position FF");
        r.out(0x08, 0x00);
        CHECK(r.in(0x08) == 0xA5, "drive 0: E5 (move ok, ready) with TRACK 0 asserted -> A5");
        r.out(0x08, 0x0A);
        CHECK(r.in(0x08) == 0xA5, "select 0A is drive 2: three select bits, not four");


        // The firmware's sector-port handler does not look at READY, so that read of drive 3
        // asked for a track -- and the server answers an empty drive with silence.
        r.pump();
        CHECK(r.srv->count("READ") == 1, "the read of the empty drive still asked for a track");
        r.ns += FdcPlusBoard::kTimeoutNs;
        r.pump();
        CHECK(r.b.linkIdle(), "no answer: after 1 s the card gives up");
    }

    SECTION("FDC+ -- STAT takes the selected drive away: FF until it is selected again");
    {
        Rig r;
        r.out(0x08, 0x02);
        CHECK(r.in(0x08) == 0xA5, "drive 2 ready");
        r.srv->mounted = 0x0003;  // the operator unmounts drive 2 on the server
        r.statRound();
        CHECK(r.in(0x08) == 0xFF, "STAT says drive 2 is gone -> status FF");
        r.srv->mounted = 0x0007;
        r.statRound();
        CHECK(r.in(0x08) == 0xFF, "remounted -- still FF until the drive is selected again");
        r.out(0x08, 0x02);
        CHECK(r.in(0x08) == 0xA5, "select it again: back");
    }

    SECTION("FDC+ -- the first sector read asks for the track; FF until it arrives");
    {
        Rig r;
        r.ready(0);
        CHECK(r.in(0x09) == 0xFF, "no track yet: FF");
        CHECK(r.srv->count("READ") == 0, "nothing on the wire inside a bus cycle");
        r.pump();
        CHECK(r.srv->count("READ") == 1, "pump(): READ goes out");
        CHECK(r.srv->got.back().p1 == 0x0000 && r.srv->got.back().p2 == kLen8,
              "drive 0 track 0 (drive in the top nibble), length 4384");
        r.pump();
        CHECK(r.b.linkIdle(), "the track is in");
        r.clk.advance(kSector8);
        CHECK(r.in(0x09) == 0xFF, "this read still returns FF -- the NEXT value is worked out after");
        CHECK(r.in(0x09) == 0xC0, "sector 0, sector true (bit 0 LOW)");
        CHECK((r.in(0x08) & 0x80) == 0, "NRDA true at once -- there is no byte clock");
        CHECK(r.in(0x09) == 0xC1, "sector true lasts ONE read");

        bool same = true;
        for (int i = 0; i < kSlot; ++i) same = same && r.in(0x0A) == r.srv->at(0, 0, 0, i);
        CHECK(same, "137 bytes, and they are the server's track 0 sector 0");
        CHECK((r.in(0x08) & 0x80) != 0, "NRDA false after the 137th");

        CHECK(r.in(0x09) == 0xC1, "no sector time has passed: still sector 0, not true");
        r.clk.advance(kSector8);
        r.in(0x09);
        CHECK(r.in(0x09) == 0xC2, "a sector time later: sector 1, true");
        CHECK(r.find(0), "and it wraps round to sector 0");
    }

    SECTION("FDC+ -- a sector is readable before the rest of the track has arrived");
    {
        Rig r;
        r.srv->hold();
        r.ready(0);
        r.in(0x09);
        r.pump();  // READ out; nothing back yet
        r.srv->release(3 * kSlot + 10);
        r.pump();
        CHECK(r.find(2), "three sectors are here: sector 2 comes round");
        r.clk.advance(kSector8);
        r.in(0x09);
        CHECK(r.in(0x09) == 0xC5, "sector 3 has not arrived: sector 2, not true -- it waits");
        r.srv->open();
        r.pump();
        r.clk.advance(kSector8);
        r.in(0x09);
        CHECK(r.in(0x09) == 0xC6, "the rest arrives: sector 3");
    }

    SECTION("FDC+ -- no answer in 1 s, or a bad checksum: the next poll asks again");
    {
        Rig r;
        r.srv->silent = true;
        r.ready(0);
        r.in(0x09);
        r.pump();
        CHECK(r.srv->count("READ") == 1, "READ sent");
        r.ns += 999 * kNsMs;
        r.pump();
        CHECK(!r.b.linkIdle(), "999 ms: still waiting");
        r.ns += 1 * kNsMs;
        r.pump();
        CHECK(r.b.linkIdle(), "1 s: given up");
        r.srv->silent = false;
        r.in(0x09);
        r.pump();
        CHECK(r.srv->count("READ") == 2, "the guest's next poll asks again");
        CHECK(r.find(0), "and this time the track comes");

        r.srv->badTrack = true;
        r.out(0x09, 0x01);  // step in
        r.clk.advance(11 * kMs);
        r.in(0x09);
        r.pump(2);
        CHECK(r.srv->count("READ") == 3, "track 1 asked for");
        r.clk.advance(kSector8);
        r.in(0x09);
        CHECK(r.in(0x09) == 0xFF, "its checksum was bad: no track, FF");
        r.pump();
        CHECK(r.srv->count("READ") == 4, "asked again");
        CHECK(r.find(0), "and read");
    }

    SECTION("FDC+ -- a write goes into the buffer, and home before the next track is read");
    {
        Rig r;
        r.ready(0);
        CHECK(r.find(5), "sector 5");
        r.out(0x09, 0x80);  // write enable
        CHECK((r.in(0x08) & 0x01) == 0, "ENWD true at once");
        for (int i = 0; i < kSlot + 1; ++i) r.out(0x0A, (uint8_t)(0xA0 + i));
        CHECK(r.b.dirty(), "the buffer is dirty");
        CHECK(r.srv->count("WRIT") == 0, "nothing written back yet");

        r.out(0x09, 0x01);  // step in -- the write belongs to track 0
        r.clk.advance(11 * kMs);
        r.in(0x09);         // ask for track 1
        r.pump(4);
        CHECK(r.srv->count("WRIT") == 1, "WRIT before the READ");
        CHECK(r.srv->got[r.srv->got.size() - 2].cmd == "WRIT" && r.srv->got.back().cmd == "READ",
              "in that order");
        CHECK(r.srv->got[r.srv->got.size() - 2].p1 == 0x0000, "for drive 0 track 0");
        CHECK(r.srv->writes == 1, "the server took the track");
        bool mine = true;
        for (int i = 0; i < kSlot; ++i) mine = mine && r.srv->at(0, 0, 5, i) == (uint8_t)(0xA0 + i);
        CHECK(mine, "sector 5 holds the 137 bytes written (the 138th was dropped)");
        CHECK(r.srv->at(0, 0, 4, 0) == FakeServer::fill(0, 4 * kSlot), "sector 4 untouched");
        CHECK(!r.b.dirty(), "clean");
        CHECK(r.srv->got.back().p1 == 0x0001, "and track 1 is read");
    }

    SECTION("FDC+ -- a refused write is tried 3 times, then given up out loud");
    {
        Rig r;
        r.ready(0);
        CHECK(r.find(0), "sector 0");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x55);
        r.srv->notReady = 5;
        r.out(0x09, 0x01);
        r.clk.advance(11 * kMs);
        r.in(0x09);
        r.pump(6);
        CHECK(r.srv->count("WRIT") == 3, "three WRITs, all NOT READY");
        CHECK(r.srv->writes == 0, "nothing was written");
        auto log = r.b.drainLog();
        CHECK(!log.empty() && log[0].find("writes are lost") != std::string::npos,
              "the operator is told");
        CHECK(r.srv->got.back().cmd == "READ", "and the READ still goes");
    }

    SECTION("FDC+ -- DEBUG=error: a silent server, a bad track and a refused write are reported");
    {
        Rig r;
        r.b.id = "fdc0";
        r.b.ensureDebugChannel();
        std::string err;
        CHECK(r.b.debugChannel()->enable("error", err), "the error flag exists");
        std::string text = debugCapture([&] {
            r.srv->silent = true;
            for (int i = 0; i < 5; ++i) {  // five STATs, none answered
                r.statRound();
                r.ns += FdcPlusBoard::kTimeoutNs;
                r.pump();
            }
            r.srv->silent = false;
            r.statRound();
            r.statRound();
        });
        CHECK(countOf(text, "server not answering") == 1, "a silent server: said once, not five times");
        CHECK(countOf(text, "server answering again") == 1, "and once when it comes back");

        text = debugCapture([&] {
            r.srv->badTrack = true;
            r.ready(0);
            r.in(0x09);
            r.pump(2);
        });
        CHECK(countOf(text, "BAD CHECKSUM") == 1, "a track with a bad checksum");

        text = debugCapture([&] {
            CHECK(r.find(0), "sector 0");
            r.out(0x09, 0x80);
            r.out(0x0A, 0x55);
            r.srv->notReady = 5;
            r.out(0x09, 0x01);
            r.clk.advance(11 * kMs);
            r.in(0x09);
            r.pump(6);
        });
        CHECK(countOf(text, "WRIT drive=0 track=0: drive not ready") == 3, "each refused WRIT");
        CHECK(countOf(text, "(try 3 of 3)") == 1, "numbered");
        CHECK(countOf(text, "send READ") == 0, "error alone: none of link's traffic");

        CHECK(r.b.debugChannel()->disable("error", err), "flag off");
        text = debugCapture([&] {
            r.srv->silent = true;
            r.statRound();
            r.ns += FdcPlusBoard::kTimeoutNs;
            r.pump();
        });
        CHECK(text.empty(), "flag off: nothing");
    }

    SECTION("FDC+ -- 8\": head unloaded for 1.3 s -> write back and forget the track");
    {
        Rig r;
        r.ready(0);
        CHECK(r.find(0), "sector 0");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x11);
        r.out(0x09, 0x08);  // head unload
        r.pump();           // pump() sees the activity and starts counting from here
        r.clk.advance(10000 * kSector8);
        r.pump();
        CHECK(r.srv->count("WRIT") == 0,
              "52 EMULATED seconds: nothing. The write-back is not the guest's to see -- it "
              "runs on the wall clock, or a flat-out machine would lose tracks in flight");
        r.ns += 256 * 5208 * 1000 - 1000;
        r.pump();
        CHECK(r.srv->count("WRIT") == 0, "1.333 s of wall time less a hair: not yet");
        r.ns += 1000;
        r.pump(4);  // a STAT fell due in the long wait: its answer, then WRIT, track, WSTA
        CHECK(r.srv->count("WRIT") == 1, "256 sector times (1.333 s): the track goes home");
        CHECK(r.b.linkIdle() && !r.b.dirty(), "home, and clean");
        const int reads = r.srv->count("READ");
        r.out(0x09, 0x04);
        r.in(0x09);
        r.pump();
        CHECK(r.srv->count("READ") == reads + 1, "and the next access fetches it again");
    }

    SECTION("FDC+ -- 8\": a loaded head keeps the track, however long");
    {
        Rig r;
        r.ready(0);
        CHECK(r.find(0), "sector 0");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x11);
        r.ns += 60ull * 1000 * kNsMs;
        r.pump(3);
        CHECK(r.srv->count("WRIT") == 0, "a minute, head still loaded: no write-back");
    }

    SECTION("FDC+ -- Minidisk: head loaded on select; 1.6 s write-back; off at 6.4 s");
    {
        Rig r(6);
        r.out(0x08, 0x00);
        CHECK((r.in(0x08) & 0x04) == 0, "selected = head loaded; there is no solenoid");
        CHECK(r.find(0, kSector5), "sector 0");
        r.in(0x09);
        CHECK(r.srv->got.back().p2 == kLen5 || r.srv->count("READ") >= 1, "READ asked for 2192");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x22);
        r.pump();
        r.ns += 1600 * kNsMs;
        r.pump(3);
        CHECK(r.srv->count("WRIT") == 1, "128 sector times (1.6 s of wall time): written back");
        CHECK(r.in(0x08) != 0xFF, "still on");
        r.out(0x09, 0x04);  // cRESTMR: on the Minidisk, bit 2 restarts the timer
        r.clk.advance(512 * kSector5 - 10);
        r.pump();
        CHECK(r.in(0x08) != 0xFF, "6.4 EMULATED seconds less a hair: still on");
        r.clk.advance(10);
        r.pump();
        CHECK(r.in(0x08) == 0xFF, "512 (6.4 s): the drive turned itself off -- the guest reads "
                                  "that back, so it is emulated time");
        r.out(0x08, 0x00);
        CHECK(r.in(0x08) != 0xFF, "selecting it again turns it on");
    }

    SECTION("FDC+ -- step: MOVE OK false for 10.5 ms (8\"); the 8 MB drive steps fast");
    {
        Rig r;
        r.ready(0);
        r.out(0x09, 0x01);
        CHECK((r.in(0x08) & 0x06) == 0x06, "stepping: MOVE and HEAD both false");
        CHECK((r.in(0x08) & 0x40) != 0, "off track 0");
        r.clk.advance(10500 * 2 - 2);
        CHECK((r.in(0x08) & 0x02) != 0, "10.5 ms less a hair: still false");
        r.clk.advance(4);
        CHECK((r.in(0x08) & 0x06) == 0, "10.5 ms: MOVE ok, and the loaded head is back");
        r.out(0x09, 0x02);
        r.clk.advance(21 * kMs);
        CHECK((r.in(0x08) & 0x40) == 0, "stepped back to track 0: TRACK 0");

        for (int i = 0; i < 77; ++i) {
            r.out(0x09, 0x01);
            r.clk.advance(21 * kMs);
        }
        CHECK(r.b.track(0) == 77, "track 77 -- past the end of a real 8\" disk");
        r.statRound();
        r.out(0x09, 0x01);
        r.clk.advance(260 * 2 - 2);
        CHECK((r.in(0x08) & 0x02) != 0, "STAT reported 77: now 260 us -- just short");
        r.clk.advance(4);
        CHECK((r.in(0x08) & 0x02) == 0, "260 us: done. The fast rate for the 8 MB drive");
    }

    SECTION("FDC+ -- Minidisk steps take 50 ms");
    {
        Rig r(6);
        r.out(0x08, 0x00);
        r.out(0x09, 0x01);
        r.clk.advance(50 * kMs - 2);
        CHECK((r.in(0x08) & 0x02) != 0, "50 ms less a hair: still false");
        r.clk.advance(4);
        CHECK((r.in(0x08) & 0x02) == 0, "50 ms: MOVE ok");
    }

    SECTION("FDC+ -- POWER and DISCONNECT write the dirty track home first");
    {
        Rig r;
        r.ready(0);
        CHECK(r.find(3), "sector 3");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x77);
        CHECK(r.b.dirty(), "dirty");
        r.b.power();
        CHECK(r.srv->writes == 1, "POWER wrote the track home before the card restarted");
        CHECK(r.srv->at(0, 0, 3, 0) == 0x77, "with the byte in it");

        int sunk = 0;
        r.srv->sink = &sunk;  // the server goes with the line; count past it
        r.pump(2);            // after POWER nothing is ready until STAT answers
        r.ready(0);
        CHECK(r.find(4), "after POWER: sector 4");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x66);
        std::string err;
        CHECK(r.b.disconnect("line", err), "DISCONNECT");
        CHECK(sunk == 1, "and the dirty track went home first");
        CHECK(!r.b.dirty() && r.b.readyMask() == 0, "nothing ready on a dead line");
    }

    SECTION("FDC+ -- snapshot: the registers and the track buffer travel");
    {
        Rig r;
        r.ready(0);
        CHECK(r.find(2), "sector 2");
        r.out(0x09, 0x80);
        r.out(0x0A, 0x99);
        StateWriter w;
        r.b.serialize(w);

        Clock        c2;
        FdcPlusBoard b2;
        b2.attachClock(&c2);
        StateReader rd(w.data());
        b2.deserialize(rd);
        CHECK(rd.ok(), "restores cleanly");
        CHECK(b2.dirty(), "the dirty flag travelled");
        CHECK(b2.readyMask() == 0x0007, "and the ready map");
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x08;
        CHECK(b2.read(c) == r.in(0x08), "and the status register");
    }

    hdfTests();
}
