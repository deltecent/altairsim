// The Vector Graphic Micropolis Disk Controller Board (docs/boards/vector-micropolis.md).
//
// Real bus cycles through the decode, on sectors made up here. No filesystem: MemoryMedia through
// setMediaResolver. The boot section runs the real MBOOT ROM on a made-up boot sector.

#include "boards/s100-memory.h"
#include "boards/vector-micropolis.h"
#include "core/debug.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/cardimg.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr size_t   kSlot      = 275;
constexpr uint64_t kOneSide   = 338800;
constexpr uint64_t kPerSector = 25000;    // 2 MHz, 300 RPM, 16 sectors: 12.5 ms
constexpr uint64_t kTimer     = 8000000;  // 4 s
constexpr uint64_t kByte      = 64;       // 32 us

constexpr uint16_t kSec = 0xFA00, kSta = 0xFA01, kDat = 0xFA02;

constexpr uint8_t kSelect = 0x20, kInt = 0x40, kStep = 0x60, kWrite = 0x80, kReset = 0xA0;
constexpr uint8_t kSltd = 0x04, kTk0 = 0x08, kWpt = 0x10, kRdy = 0x20, kTf = 0x80;

size_t slotAt(int side, int track, int sector) {
    return ((size_t)side * 77 * 16 + (size_t)track * 16 + (size_t)sector) * kSlot;
}

// Bytes 1 to 268 by add with carry, as MBOOT and diskdrv sum them.
uint8_t checksum(const std::vector<uint8_t>& s) {
    unsigned sum = 0, carry = 0;
    for (size_t i = 1; i < 269; ++i) {
        sum += s[i] + carry;
        carry = sum >> 8;
        sum &= 0xFF;
    }
    return (uint8_t)sum;
}

std::vector<uint8_t> makeSlot(uint8_t track, uint8_t sector) {
    std::vector<uint8_t> s(kSlot, 0);
    s[0] = 0xFF;
    s[1] = track;
    s[2] = sector;
    for (size_t i = 0; i < 266; ++i) s[3 + i] = (uint8_t)(i + track * 16 + sector);
    s[269] = checksum(s);
    return s;
}

std::vector<uint8_t> formatted(uint64_t bytes = kOneSide) {
    std::vector<uint8_t> img((size_t)bytes, 0);
    for (size_t off = 0, n = 0; off + kSlot <= bytes; off += kSlot, ++n) {
        auto s = makeSlot((uint8_t)((n / 16) % 77), (uint8_t)(n % 16));
        std::memcpy(&img[off], s.data(), kSlot);
    }
    return img;
}

struct Rig {
    Machine          m;
    MicropolisBoard* mp   = nullptr;
    MemoryMedia*     disk = nullptr;

    Rig() {
        std::string err;
        mp = dynamic_cast<MicropolisBoard*>(m.add("vgmdc", "mp0", err));
        m.power();
    }
    ~Rig() { setMediaResolver(openHostMedia); }

    bool load(int unit, std::vector<uint8_t> image, bool ro = false) {
        setMediaResolver([this, img = std::move(image)](const std::string& p, bool r, std::string&) {
            auto md = std::make_unique<MemoryMedia>(p, img, r);
            disk    = md.get();
            return std::unique_ptr<MediaFile>(std::move(md));
        });
        std::string err;
        return mp->mount("drive" + std::to_string(unit), "test.vgi", ro, err);
    }

    uint8_t rd(uint16_t a) { return m.bus.memRead(a); }
    void    wr(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }

    // A read starts the timer, as on the board, and a command then sticks.
    void cmd(uint8_t v) {
        (void)rd(kSta);
        wr(kSec, v);
    }
    uint32_t held() { return m.bus.takeWaitStates(); }

    // Moves the clock to `t` T-states into `sector` of the next turn it can reach.
    void seekTo(int sector, uint64_t t) {
        const uint64_t perRev = kPerSector * 16;
        const uint64_t want   = (uint64_t)sector * kPerSector + t;
        const uint64_t into   = m.clock.now() % perRev;
        m.clock.advance(want >= into ? want - into : want + perRev - into);
    }

    std::vector<uint8_t> onDisk(int track, int sector) {
        size_t off = slotAt(0, track, sector);
        return {disk->bytes().begin() + (std::ptrdiff_t)off,
                disk->bytes().begin() + (std::ptrdiff_t)(off + kSlot)};
    }

    // Reads a sector as rdSec does, charging each hold to the clock.
    std::vector<uint8_t> readSector(int sector) {
        seekTo(sector, 2536 + 10);  // TF is true from 1268 us
        std::vector<uint8_t> v(270);
        for (auto& b : v) {
            b = rd(kDat);
            m.clock.advance(held() + 20);
        }
        return v;
    }

    void writeSector(int sector, const std::vector<uint8_t>& slot, size_t n = 270) {
        seekTo(sector, 40);
        wr(kSec, kWrite);
        seekTo(sector, 2464 + 10);  // TF is true from 1232 us
        for (size_t i = 0; i < n; ++i) {
            wr(kDat, slot[i]);
            m.clock.advance(held() + 20);
        }
        seekTo((sector + 1) % 16, 1);
    }
};


// What the debug channel wrote while `f` ran.
std::string debugCapture(const std::function<void()>& f) {
    namespace fs = std::filesystem;
    std::string    err;
    const fs::path p = fs::temp_directory_path() / "altair_micropolis_debug.log";
    std::error_code ec;
    fs::remove(p, ec);
    dbg::setSink(dbg::Sink::File, p.string(), err);
    f();
    dbg::setSink(dbg::Sink::Stderr, "", err);  // closes + flushes the file
    std::ifstream     in(p, std::ios::binary);
    const std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    fs::remove(p, ec);
    return s;
}

// BAD SECTORS (#971). The fault damages the bytes the guest reads; the image never changes.
void faultTests() {
    SECTION("vgmdc: a data fault inverts the checksum, and counts when it is read");
    {
        Rig g;
        std::string err;
        CHECK(g.mp->takesFaults("drive0") && g.mp->takesFaults("drive3") && !g.mp->takesFaults("drive4"),
              "drive0 to drive3 take faults");
        CHECK(g.mp->faults("drive0") == nullptr, "an empty drive has no table");
        CHECK(g.load(0, formatted()), "mount");
        const auto before = g.disk->bytes();
        g.cmd(kSelect);
        g.cmd(kStep | 1);
        DiskFaults* f = g.mp->faults("drive0");
        CHECK(f && f->add("1/0/3 data", err), err.c_str());

        g.seekTo(3, 2546);
        for (int i = 0; i < 269; ++i) {
            (void)g.rd(kDat);
            g.m.clock.advance(g.held() + 20);
        }
        CHECK(f->list()[0].hits == 0, "no hit before the checksum is read");
        auto got = g.readSector(3);
        auto exp = makeSlot(1, 3);
        CHECK(std::equal(got.begin(), got.begin() + 269, exp.begin()), "the header and the data are intact");
        CHECK(got[269] == (uint8_t)(exp[269] ^ 0xFF), "the checksum reads inverted");
        CHECK(f->list()[0].hits == 1, "one hit, when the checksum is read");

        auto next = g.readSector(4);
        CHECK(next[269] == makeSlot(1, 4)[269], "the next sector is clean");
        CHECK(g.disk->bytes() == before, "the image does not change");
    }

    SECTION("vgmdc: an id fault inverts the track, in place of a data fault");
    {
        Rig g;
        std::string err;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        g.cmd(kStep | 1);
        DiskFaults* f = g.mp->faults("drive0");
        f->add("1/0/3 data", err);
        f->add("1/0/3 id", err);
        auto got = g.readSector(3);
        auto exp = makeSlot(1, 3);
        CHECK(got[1] == (uint8_t)~1 && got[2] == 3, "the track reads inverted, the sector is right");
        CHECK(got[269] == exp[269], "the checksum is not damaged as well");
        CHECK(f->find(1, 0, 3, FaultKind::Id)->hits == 1 && f->find(1, 0, 3, FaultKind::Data)->hits == 0,
              "the id fault counts, the data fault does not");
    }

    SECTION("vgmdc: a missing sector gives no TF, and counts once for each pass");
    {
        Rig g;
        std::string err;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        DiskFaults* f = g.mp->faults("drive0");
        f->add("0/0/3 missing", err);

        g.seekTo(3, 2000);
        CHECK(!(g.rd(kSta) & kTf), "before TF is due");
        CHECK(f->list()[0].hits == 0, "no hit before TF is due");
        g.seekTo(3, 2546);
        for (int i = 0; i < 50; ++i) {
            CHECK(!(g.rd(kSta) & kTf), "no sync byte, so no TF");
            g.m.clock.advance(100);
        }
        CHECK(g.rd(kDat) == 0 && g.held() == 0, "a read of data gives 00 and no hold");
        CHECK(f->list()[0].hits == 1, "fifty polls in one pass are one hit");
        (void)g.rd(kSec);
        g.seekTo(4, 2546);
        CHECK(g.rd(kSta) & kTf, "the next sector has its sync byte");
        g.seekTo(3, 2546);
        (void)g.rd(kSta);
        CHECK(f->list()[0].hits == 2, "the next pass is another hit");
    }

    SECTION("vgmdc: a write fault drops the write");
    {
        Rig g;
        std::string err;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        DiskFaults* f = g.mp->faults("drive0");
        f->add("0/0/5 write", err);
        auto slot = makeSlot(0, 5);
        slot[10]  = 0xAA;
        const auto old = g.onDisk(0, 5);
        g.writeSector(5, slot);
        CHECK(g.onDisk(0, 5) == old, "the old bytes stay");
        CHECK(f->list()[0].hits == 1, "one hit");
        g.writeSector(6, slot);
        CHECK(g.onDisk(0, 6)[10] == 0xAA, "another sector is written");
    }

    SECTION("vgmdc: count=2, the side and *");
    {
        Rig g;
        std::string err;
        CHECK(g.load(0, formatted(kOneSide * 2)), "mount two sides");
        g.cmd(kSelect | 0x10);  // head 1
        DiskFaults* f = g.mp->faults("drive0");
        f->add("0/1/2 data count=2", err);
        f->add("*/*/7 data", err);
        const uint8_t good = makeSlot(0, 2)[269];
        CHECK(g.readSector(2)[269] != good, "the first read is bad");
        CHECK(g.readSector(2)[269] != good, "the second read is bad");
        CHECK(g.readSector(2)[269] == good, "the third read is clean");
        CHECK(f->list()[0].left == 0 && f->list()[0].hits == 2, "worn off after two hits");
        CHECK(g.readSector(7)[269] != makeSlot(0, 7)[269], "* matches any track and side");

        g.cmd(kSelect);  // head 0
        f->add("0/1/9 data", err);
        CHECK(g.readSector(9)[269] == makeSlot(0, 9)[269], "a side 1 fault leaves side 0 alone");
    }

    SECTION("vgmdc: DEBUG=fault, the `faults` key, SNAPSHOT and MOUNT");
    {
        Rig g;
        std::string err;
        CHECK(!g.mp->loadSubUnit("drive", {{"unit", "1"}, {"faults", "2/0/0 data"}}, err) && !err.empty(),
              "`faults` with no `mount` is refused");
        CHECK(g.load(0, formatted()), "mount");
        CHECK(g.mp->loadSubUnit("drive", {{"unit", "1"}, {"mount", "b.vgi"}, {"faults", "0/0/3 data count=3; 4/*/* missing"}},
                                err),
              err.c_str());
        bool saved = false;
        for (const auto& su : g.mp->subUnits())
            for (const auto& fld : su.fields)
                saved = saved || (fld.key == "faults" && fld.quoted && fld.text == "0/0/3 data count=3; 4/*/* missing");
        CHECK(saved, "CONFIG SAVE writes the `faults` key back, quoted");

        g.mp->ensureDebugChannel();
        CHECK(g.mp->debugChannel()->enable("fault", err), "the fault flag exists");
        g.cmd(kSelect | 1);
        const std::string text = debugCapture([&] { (void)g.readSector(3); });
        CHECK(text.find("fault drive1 0/0/3 data hit 1, 2 left") != std::string::npos, text.c_str());
        CHECK(g.mp->debugChannel()->disable("fault", err), "the flag goes off");

        g.mp->faults("drive1")->add("0/0/5 missing", err);
        g.seekTo(5, 2546);
        (void)g.rd(kSta);
        StateWriter w;
        g.mp->serialize(w);

        Rig dst;
        CHECK(dst.load(1, formatted()), "mount in the second machine");
        dst.m.clock.advance(g.m.clock.now());
        StateReader r(w.data());
        dst.mp->deserialize(r);
        CHECK(r.ok(), "restores cleanly");
        const DiskFaults* f = dst.mp->faults("drive1");
        CHECK(f && f->list().size() == 3 && f->list()[0].hits == 1 && f->list()[0].left == 2,
              "RESTORE brings the faults back with their counts");
        (void)dst.rd(kSta);
        CHECK(f->list()[2].hits == 1, "...and the pass already counted is not counted again");

        CHECK(g.load(1, formatted()) && g.mp->faults("drive1")->empty(),
              "a new disk in the drive brings no faults");
        g.mp->faults("drive1")->add("0/0/1 data", err);
        CHECK(g.mp->unmount("drive1", err) && g.mp->faults("drive1") == nullptr, "UNMOUNT: no disk, no faults");
    }
}
} // namespace

void test_micropolis() {
    SECTION("vgmdc: decode");
    {
        Rig g;
        CHECK(g.rd(0xF800) != 0xFF, "the boot PROM answers at the base");
        CHECK(g.rd(0xF900) == 0xFF, "no chip at base + 100");
        CHECK(g.rd(0xF7FF) == 0xFF, "below the block is not decoded");
        CHECK(g.rd(0xFC00) == 0xFF, "above the block is not decoded");

        const uint8_t first = g.rd(0xF800);
        g.wr(0xF800, (uint8_t)~first);
        CHECK(g.rd(0xF800) == first, "a write to the PROM is ignored");

        std::string err;
        CHECK(setProperty(*g.mp, "base", "D800", err), "W1 and W4 set D800");
        CHECK(g.rd(0xD800) == first, "the PROM moves with the base");
        CHECK(g.rd(0xF800) == 0xFF, "and leaves F800");
        CHECK(g.rd(0xDA01) == kSltd, "the registers move with the base");
        CHECK(!setProperty(*g.mp, "base", "F900", err), "the base is a 1K boundary");
        CHECK(!setProperty(*g.mp, "base", "BC00", err), "the base is C000 or more");
        uint8_t b = 0;
        CHECK(g.mp->peek(0xD800, b) && b == first, "peek shows the PROM");
        CHECK(!g.mp->peek(0xDA00, b), "peek leaves the registers alone");
    }

    SECTION("vgmdc: the registers repeat");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        CHECK(g.rd(0xFA05) == g.rd(0xFA01), "status at each +4");
        CHECK(g.rd(0xFBFD) == g.rd(0xFA01), "status at the top of the block");
        (void)g.rd(0xFA07);
        g.wr(0xFBF8, kSelect | 2);
        CHECK((g.rd(kSta) & 3) == 2, "a command at each +4");
        g.wr(0xFA01, kSelect | 1);
        CHECK((g.rd(kSta) & 3) == 1, "a command at base + 201");
    }

    SECTION("vgmdc: status");
    {
        Rig g;
        CHECK(g.rd(kSta) == kSltd, "nothing selected: SLTD/ reads 1 and no bit is on");
        CHECK(g.rd(kSec) == 0x10, "W10 out reads 1, W9 in reads 0");

        CHECK(g.load(2, formatted(), true), "mount read-only");
        g.cmd(kSelect | 2);
        const uint8_t s = g.rd(kSta);
        CHECK(!(s & kSltd), "selected: SLTD/ is low true");
        CHECK((s & 3) == 2, "the unit");
        CHECK(s & kTk0, "track 0");
        CHECK(s & kWpt, "write protect");
        CHECK(s & kRdy, "ready");

        g.cmd(kSelect | 1);
        CHECK(g.rd(kSta) & kRdy, "forced: an empty drive is ready, the Rev. 1 standard");
        CHECK(!(g.rd(kSec) & 0x80), "an empty drive gives no SF");
        std::string err;
        CHECK(setProperty(*g.mp, "ready", "drive", err), "ready=drive");
        CHECK(!(g.rd(kSta) & kRdy), "drive: an empty drive is not ready");
        g.cmd(kSelect | 2);
        CHECK(g.rd(kSta) & kRdy, "drive: a loaded drive is ready");

        CHECK(setProperty(*g.mp, "speed", "2mhz", err), "speed=2mhz");
        CHECK(g.rd(kSec) & 0x20, "W9 out reads 1");
    }

    SECTION("vgmdc: sector flag and number");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        g.seekTo(5, 0);
        CHECK(g.rd(kSec) == (0x80 | 0x10 | 5), "SF true at the hole, with the number");
        g.m.clock.advance(59);
        CHECK(g.rd(kSec) & 0x80, "SF at 29.5 us");
        g.m.clock.advance(1);
        CHECK(g.rd(kSec) == (0x10 | 5), "SF false at 30 us, and the number stays");
        g.seekTo(15, kPerSector - 1);
        CHECK((g.rd(kSec) & 0x0F) == 15, "the last T-state of sector 15");
        g.m.clock.advance(1);
        CHECK((g.rd(kSec) & 0x8F) == 0x80, "then sector 0");
    }

    SECTION("vgmdc: step");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        (void)g.rd(kSta);
        g.wr(kSec, kStep | 1);
        CHECK(g.mp->driveTrack(0) == 0, "no drive selected, no step");
        g.cmd(kSelect);
        g.cmd(kStep);
        CHECK(g.mp->driveTrack(0) == 0, "out at track 0 stays there");
        g.cmd(kStep | 1);
        CHECK(g.mp->driveTrack(0) == 1, "bit 0 = 1 steps in");
        CHECK(!(g.rd(kSta) & kTk0), "off track 0");
        g.cmd(kStep);
        CHECK(g.mp->driveTrack(0) == 0, "bit 0 = 0 steps out");
        for (int i = 0; i < 90; ++i) g.cmd(kStep | 1);
        CHECK(g.mp->driveTrack(0) == 76, "the stop at track 76");
    }

    SECTION("vgmdc: read");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        g.cmd(kStep | 1);

        g.seekTo(3, 2535);
        CHECK(!(g.rd(kSta) & kTf), "TF false before 1268 us");
        CHECK(g.rd(kDat) == 0 && g.held() == 0, "TF false: a read gives 00 and no hold");
        g.m.clock.advance(1);
        CHECK(g.rd(kSta) & kTf, "TF true at 1268 us");

        CHECK(g.rd(kDat) == 0xFF, "the sync byte");
        CHECK(g.held() == kByte, "the read holds PRDY up to the sync byte");

        auto got = g.readSector(3);
        auto exp = makeSlot(1, 3);
        bool same = true;
        for (size_t i = 0; i < got.size(); ++i) same = same && got[i] == exp[i];
        CHECK(same, "each byte of the slot, in order, paced by PRDY");

        g.seekTo(4, 0);
        CHECK(!(g.rd(kSta) & kTf), "the next hole ends TF");
    }

    SECTION("vgmdc: a late guest loses bytes");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        g.seekTo(0, 2600 + 10 * kByte + 1);
        CHECK(g.rd(kDat) == makeSlot(0, 0)[11], "the byte that passes next");
        CHECK(g.held() == kByte - 1, "and no more hold than that");
    }

    SECTION("vgmdc: an unformatted sector");
    {
        Rig g;
        CHECK(g.load(0, std::vector<uint8_t>(kOneSide, 0)), "mount");
        g.cmd(kSelect);
        g.seekTo(0, 3000);
        CHECK(!(g.rd(kSta) & kTf), "no sync, no TF");
    }

    SECTION("vgmdc: write");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);

        auto s = makeSlot(0, 7);
        s[100] = 0xA5;
        s[269] = checksum(s);
        g.seekTo(7, 40);
        g.wr(kSec, kWrite);
        g.seekTo(7, 2463);
        CHECK(!(g.rd(kSta) & kTf), "TF false before 1232 us");
        g.wr(kDat, 0x55);
        CHECK(g.held() == 0, "TF false: a write does not hold");
        g.m.clock.advance(1);
        CHECK(g.rd(kSta) & kTf, "TF true at 1232 us");
        g.wr(kDat, s[0]);
        CHECK(g.held() == kByte, "the sync byte is taken one byte time after TF");
        g.m.clock.advance(kByte + 20);
        for (size_t i = 1; i < 270; ++i) {
            g.wr(kDat, s[i]);
            g.m.clock.advance(g.held() + 20);
        }
        CHECK(g.onDisk(0, 7) == makeSlot(0, 7), "nothing reaches the disk before the next sector");
        g.seekTo(8, 0);
        auto disk = g.onDisk(0, 7);
        CHECK(disk == s, "the next sector pulse stores the slot, zeros after the checksum");
        CHECK(g.onDisk(0, 8) == makeSlot(0, 8), "the next sector is untouched");
        CHECK(!g.m.clock.queued(), "with the write done, no event stays queued");

        auto back = g.readSector(7);
        CHECK(back[100] == 0xA5, "the write reads back");
    }

    SECTION("vgmdc: the checksum is the guest's");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        auto s = makeSlot(0, 2);
        s[269] ^= 0xFF;
        g.writeSector(2, s);
        CHECK(g.onDisk(0, 2)[269] == s[269], "a bad checksum goes to the disk as written");
        CHECK(g.readSector(2)[269] == s[269], "and reads back as it is");
    }

    SECTION("vgmdc: a protected disk");
    {
        Rig g;
        CHECK(g.load(0, formatted(), true), "mount read-only");
        g.cmd(kSelect);
        auto s = makeSlot(0, 2);
        s[50] ^= 0xFF;
        g.writeSector(2, s);
        CHECK(g.onDisk(0, 2) == makeSlot(0, 2), "the disk keeps its bytes");
        CHECK(g.mp->drainLog().size() == 1, "and the board says so once");
    }

    SECTION("vgmdc: the 4-second timer");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.wr(kSec, kSelect);
        CHECK(g.rd(kSta) & kSltd, "at power the timer is out, so a select is lost");
        g.wr(kSec, kSelect);
        CHECK(!(g.rd(kSta) & kSltd), "a read started the timer, so this one sticks");

        g.m.clock.advance(kTimer - 1);
        CHECK(!(g.rd(kSta) & kSltd), "any read starts the 4 seconds again");
        g.m.clock.advance(kTimer - 1);
        (void)g.rd(0xF800);
        g.m.clock.advance(kTimer - 1);
        CHECK(!(g.rd(kSta) & kSltd), "a read of the PROM starts them too");

        g.wr(kSec, kStep | 1);
        g.m.clock.advance(kTimer - 1);
        g.wr(kSec, kStep);
        g.m.clock.advance(1);
        CHECK(g.rd(kSta) & kSltd, "a step does not, and the timer ends the select");
    }

    SECTION("vgmdc: the reset command");
    {
        Rig g;
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        g.cmd(kStep | 1);
        g.cmd(kReset);
        CHECK(g.rd(kSta) & kSltd, "reset clears the select");
        CHECK(g.mp->driveTrack(0) == 1, "and the head stays");
    }

    SECTION("vgmdc: interrupt");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.mp, "interrupt", "vi3", err), "interrupt=vi3");
        CHECK(g.load(0, formatted()), "mount");
        g.cmd(kSelect);
        CHECK(!g.m.clock.queued(), "an idle board queues nothing");
        g.seekTo(2, 100);
        g.cmd(kInt | 1);
        CHECK(!(g.rd(kSec) & 0x40), "INT waits for the next boundary");
        CHECK(g.mp->assertsVi() == 0, "no line yet");
        g.seekTo(3, 0);
        CHECK(g.rd(kSec) & 0x40, "INT at the boundary");
        CHECK(g.mp->assertsVi() == (1u << 3), "on VI3");
        g.cmd(kInt);
        CHECK(!(g.rd(kSec) & 0x40), "bit 0 = 0 clears INT");
        CHECK(g.mp->assertsVi() == 0, "and the line");
        CHECK(!g.m.clock.queued(), "and nothing stays queued");

        CHECK(setProperty(*g.mp, "interrupt", "int", err), "interrupt=int");
        g.cmd(kInt | 1);
        g.m.clock.advance(kPerSector);
        CHECK(g.mp->assertsInt(), "on pin 73");
        g.cmd(kReset);
        CHECK(!g.mp->assertsInt(), "reset clears it");
    }

    SECTION("vgmdc: image sizes");
    {
        Rig g;
        CHECK(g.load(0, formatted(kOneSide + 16)), "the 16 extra bytes of five images");
        g.cmd(kSelect);
        CHECK(g.readSector(1)[1] == 0, "still one side, slots in place");
        CHECK(g.disk->bytes().size() == kOneSide + 16, "the size stays");
    }

    SECTION("vgmdc: units");
    {
        Rig g;
        std::string err;
        CHECK(g.load(3, formatted()), "drive3");
        CHECK(!g.mp->mount("drive4", "x", false, err), "there is no drive 4");
        CHECK(g.mp->unmount("drive3", err), "unmount");
        CHECK(!g.mp->unmount("drive3", err), "an empty drive");
    }

    SECTION("vgmdc: snapshot");
    {
        Rig src;
        CHECK(src.load(1, formatted()), "mount");
        src.cmd(kSelect | 1);
        src.cmd(kStep | 1);
        StateWriter w;
        src.mp->serialize(w);

        Rig dst;
        CHECK(dst.load(1, formatted()), "mount");
        dst.m.clock.advance(src.m.clock.now());
        StateReader r(w.data());
        dst.mp->deserialize(r);
        CHECK(r.ok(), "the blob is read in full");
        CHECK(dst.mp->driveTrack(1) == 1, "the head travelled");
        CHECK((dst.rd(kSta) & 7) == 1, "the select travelled");
    }

    SECTION("vgmdc: the real boot ROM");
    {
        Machine m;
        std::string err;
        m.add("8080", "cpu0", err);
        m.add("vgmdc", "mp0", err);
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0xE000;
        mem->addRegion(ram, err);

        // Sector 0 names 0200 as its load address, and the code enters at load + 0C.
        auto img = formatted();
        std::vector<uint8_t> s(kSlot, 0);
        s[0] = 0xFF;
        const uint8_t code[] = {0x3E, 0x5A, 0x32, 0x00, 0x01, 0x76};  // MVI A,5A STA 0100 HLT
        std::memcpy(&s[1 + 0x0C], code, sizeof code);
        s[1 + 0x10A] = 0x00;
        s[1 + 0x10B] = 0x02;
        s[269] = checksum(s);
        std::memcpy(&img[0], s.data(), kSlot);

        setMediaResolver([img](const std::string& p, bool r, std::string&) {
            return std::make_unique<MemoryMedia>(p, img, r);
        });
        auto* mp = dynamic_cast<MicropolisBoard*>(m.find("mp0"));
        CHECK(mp && mp->mount("drive0", "boot.vgi", true, err), "mount");
        m.power();
        m.cpu()->setPc(0xF800);

        for (int i = 0; i < 4000000 && m.bus.memRead(0x0100) != 0x5A; ++i) {
            StepResult st = m.master()->step(m.bus);
            m.clock.advance(st.tStates + m.bus.takeWaitStates());  // as the run loop charges PRDY
        }
        CHECK(m.bus.memRead(0x0100) == 0x5A, "MBOOT restores, reads sector 0 twice and enters it");
        setMediaResolver(openHostMedia);
    }

    faultTests();
}
