// The Vector Graphic Dual-Mode Disk Controller, floppy half (docs/boards/vector-dualmode.md).
//
// Real bus cycles through the decode, on sectors made up here. No filesystem: MemoryMedia
// through setMediaResolver.

#include "boards/s100-memory.h"
#include "boards/vector-dualmode.h"
#include "boards/vector-fwii.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/display_null.h"
#include "host/media.h"
#include "host/stream.h"
#include "test.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr size_t   kSlot      = 275;
constexpr uint64_t kOneSide   = 338800;
constexpr uint64_t kTwoSides  = 677600;
constexpr uint64_t kPerSector = 25000;    // 2 MHz, 300 RPM, 16 sectors: 12.5 ms
constexpr uint64_t kMotor     = 8000000;  // 4 s

constexpr uint8_t kStatus0 = 0xE0;  // bits 5-7 read 1, nothing else
constexpr uint8_t kStatus1 = 0xF1;  // floppy-only, not busy, motor off

constexpr uint8_t kRead = 0x20, kEcc = 0x40;  // control 1
constexpr uint8_t kStep = 0x20, kIn  = 0x40;  // control 0

// The start of a slot in an image: all of side 0, then all of side 1.
size_t slotAt(int side, int track, int sector) {
    return ((size_t)side * 77 * 16 + (size_t)track * 16 + (size_t)sector) * kSlot;
}

// A floppy sector as reference/HD-FD documentation.md §3 lays it out, with the checksum made
// by an add with carry that keeps the carry. `ecc` and `valid` go in as given.
std::vector<uint8_t> makeSlot(uint8_t track, uint8_t sector, uint32_t ecc = 0, uint8_t valid = 0) {
    std::vector<uint8_t> s(kSlot, 0);
    s[0] = 0xFF;
    s[1] = track;
    s[2] = sector;
    for (int i = 0; i < 256; ++i) s[13 + (size_t)i] = (uint8_t)(i + track * 16 + sector);
    unsigned sum = 0, carry = 0;
    for (size_t i = 1; i < 269; ++i) {
        sum += s[i] + carry;
        carry = sum >> 8;
        sum &= 0xFF;
    }
    s[269] = (uint8_t)sum;
    for (size_t i = 0; i < 4; ++i) s[270 + i] = (uint8_t)(ecc >> (24 - 8 * i));
    s[274] = valid;
    return s;
}

// Each slot in the image gets a good sector carrying its own address, as a formatter leaves it.
std::vector<uint8_t> formatted(uint64_t bytes) {
    std::vector<uint8_t> img((size_t)bytes, 0);
    for (size_t off = 0, n = 0; off + kSlot <= bytes; off += kSlot, ++n) {
        auto s = makeSlot((uint8_t)((n / 16) % 77), (uint8_t)(n % 16));
        s[3]   = (uint8_t)(n / (77 * 16));  // the side, in a byte the board does not look at
        std::memcpy(&img[off], s.data(), kSlot);
    }
    return img;
}

struct Rig {
    Machine        m;
    DualModeBoard* dm = nullptr;
    MemoryMedia*   disk = nullptr;

    Rig() {
        std::string err;
        dm = dynamic_cast<DualModeBoard*>(m.add("vgdmdc", "dm0", err));
        m.power();
    }
    ~Rig() { setMediaResolver(nullptr); }

    // A disk in drive `unit`.
    bool load(int unit, std::vector<uint8_t> image, bool ro = false) {
        setMediaResolver([this, img = std::move(image)](const std::string& p, bool r, std::string&) {
            auto md = std::make_unique<MemoryMedia>(p, img, r);
            disk    = md.get();
            return std::unique_ptr<MediaFile>(std::move(md));
        });
        std::string err;
        return dm->mount("drive" + std::to_string(unit), "test.vgi", ro, err);
    }

    uint8_t in(uint8_t p) { return m.bus.ioRead(p); }
    void    out(uint8_t p, uint8_t v) { m.bus.ioWrite(p, v); }

    // Runs one transfer as the driver does: control ports, IN C3, OUT C3, then waits out busy.
    void transfer(uint8_t c0, uint8_t c1) {
        out(0xC1, c1);
        out(0xC0, c0);
        (void)in(0xC3);
        out(0xC3, 0);
        m.clock.advance(kPerSector * 17);
    }

    std::vector<uint8_t> ramOut(size_t n) {
        (void)in(0xC3);
        std::vector<uint8_t> v(n);
        for (auto& b : v) b = in(0xC2);
        return v;
    }

    // A 443-byte write image: 40 zeros, the slot, zeros.
    void loadWrite(const std::vector<uint8_t>& slot) {
        (void)in(0xC3);
        for (int i = 0; i < 40; ++i) out(0xC2, 0);
        for (uint8_t b : slot) out(0xC2, b);
        for (int i = 0; i < 128; ++i) out(0xC2, 0);
    }

    std::vector<uint8_t> onDisk(int side, int track, int sector) {
        size_t off = slotAt(side, track, sector);
        return {disk->bytes().begin() + (std::ptrdiff_t)off,
                disk->bytes().begin() + (std::ptrdiff_t)(off + kSlot)};
    }
};

// The machine in machines/vector.toml, with Monitor 4.3 in ROM.
struct Vector {
    Machine         m;
    NullDisplay     disp;
    DualModeBoard*  dm  = nullptr;
    FwiiBoard*      fw  = nullptr;
    ScriptedStream* kbd = nullptr;

    Vector() {
        std::string err;
        FwiiBoard::setDisplay(&disp);
        m.add("z80", "cpu0", err);
        fw = dynamic_cast<FwiiBoard*>(m.add("vgfwii", "fw0", err));
        dm = dynamic_cast<DualModeBoard*>(m.add("vgdmdc", "dm0", err));
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region ram, rom, high;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0xE000;
        mem->addRegion(ram, err);
        rom.kind  = RegionKind::Rom;
        rom.at    = 0xE000;
        rom.mount = "builtin:vgmon43";
        mem->addRegion(rom, err);
        high.kind = RegionKind::Ram;
        high.at   = 0xF800;
        high.size = 0x800;
        mem->addRegion(high, err);
        fw->connect("keyboard", "scripted", err);
        kbd = dynamic_cast<ScriptedStream*>(fw->unitStream("keyboard"));
        m.power();
        m.cpu()->setPc(0xE000);
    }
    ~Vector() {
        FwiiBoard::setDisplay(nullptr);
        setMediaResolver(nullptr);
    }

    void load(std::vector<uint8_t> image) {
        setMediaResolver([img = std::move(image)](const std::string& p, bool r, std::string&) {
            return std::make_unique<MemoryMedia>(p, img, r);
        });
        std::string err;
        dm->mount("drive1", "test.vgi", true, err);
    }

    bool promptShown() {
        for (uint16_t a = 0; a < 80 * 24 - 3; ++a)
            if (fw->vram(a) == 'M' && fw->vram(a + 1) == 'o' && fw->vram(a + 2) == 'n' &&
                fw->vram(a + 3) == '>')
                return true;
        return false;
    }

    // Runs until `done` or the step limit, with a turn for the keyboard now and then.
    template <class F> bool run(F done, int limit) {
        for (int i = 0; i < limit; ++i) {
            if ((i & 0x3FF) == 0) {
                if (done()) return true;
                fw->pump();
            }
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
        }
        return done();
    }
};

// Track 0 sector 0 with the given code in the data field, and good checksum and ECC.
void putBootSector(std::vector<uint8_t>& img, std::initializer_list<uint8_t> code) {
    auto   s = makeSlot(0, 0);
    size_t i = 13;
    for (size_t j = 13; j < 269; ++j) s[j] = 0;
    for (uint8_t b : code) s[i++] = b;
    unsigned sum = 0, carry = 0;
    for (size_t j = 1; j < 269; ++j) {
        sum += s[j] + carry;
        carry = sum >> 8;
        sum &= 0xFF;
    }
    s[269] = (uint8_t)sum;
    uint32_t e = DualModeBoard::ecc(s.data(), 270);
    for (size_t j = 0; j < 4; ++j) s[270 + j] = (uint8_t)(e >> (24 - 8 * j));
    s[274] = 0xAA;
    std::memcpy(img.data(), s.data(), kSlot);
}

} // namespace

void test_dualmode() {
    SECTION("vgdmdc: the port map");
    {
        Rig g;
        CHECK(g.in(0xC0) == kStatus0, "status 0, with no drive selected");
        CHECK(g.in(0xC1) == kStatus1, "status 1, with the motor off");
        CHECK(g.in(0xC3) == 0xFF, "IN C3 has no driver at all");

        g.out(0xC4, 0x20);
        g.out(0xC8, 0x20);
        CHECK(g.in(0xC1) == kStatus1, "OUT C4 and OUT C8 do not reach control 0");
        g.out(0xC0, 0x00);
        CHECK(g.in(0xC1) & 0x04, "C0 reached the board");
        for (uint8_t p = 0xC4; p != 0xD0; ++p) {
            BusCycle c;
            c.type = Cycle::IoRead;
            c.addr = p;
            CHECK(!g.dm->decodes(c), "C4 to CF are not decoded");
        }

        std::string err;
        CHECK(setProperty(*g.dm, "port", "A0", err), "block D sets A7-A4");
        g.m.bus.invalidateDecode();
        g.out(0xA2, 0x5A);
        CHECK(g.in(0xA3) == 0xFF, "the counter clear moved with the base");
        CHECK(g.in(0xA2) == 0x5A, "and so did the RAM");
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0xC0;
        CHECK(!g.dm->decodes(c), "and C0 is empty now");

        CHECK(!setProperty(*g.dm, "port", "C2", err), "the low nibble is not on block D");
        CHECK(err.find("A7-A4") != std::string::npos, "and the refusal says why");
    }

    SECTION("vgdmdc: the RAM counter");
    {
        Rig g;
        (void)g.in(0xC3);
        for (int i = 0; i < 512; ++i) g.out(0xC2, (uint8_t)(i ^ (i >> 8)));
        g.out(0xC2, 0xEE);  // the 513th lands on address 0
        (void)g.in(0xC3);
        CHECK(g.in(0xC2) == 0xEE, "the counter wraps at 512");
        CHECK(g.in(0xC2) == 1, "and reads go up");
        g.out(0xC3, 0);  // no drive selected, so no busy
        CHECK(g.in(0xC2) == 2, "OUT C3 does not clear the counter");
        (void)g.in(0xC3);
        CHECK(g.in(0xC2) == 0xEE, "only IN C3 clears it");
    }

    SECTION("vgdmdc: status bits and the motor timer");
    {
        Rig g;
        CHECK(g.load(1, formatted(kTwoSides), /*ro=*/true), "mount protected");
        g.out(0xC0, 0x01);
        CHECK(g.in(0xC0) == (kStatus0 | 0x05), "write protect and track 0 from drive 1");
        CHECK(g.in(0xC1) == (kStatus1 | 0x04), "motor on");

        g.out(0xC0, 0x02);
        CHECK(g.in(0xC0) == (kStatus0 | 0x04), "drive 2 is empty, and its head is on track 0");

        g.out(0xC0, 0x01);
        g.m.clock.advance(kMotor - 1);
        CHECK(g.in(0xC1) & 0x04, "one T-state before the timer ends");
        g.m.clock.advance(1);
        CHECK(g.in(0xC1) == kStatus1, "the motor is off at 4 s");
        CHECK(g.in(0xC0) == kStatus0, "and no drive stays selected");

        g.m.clock.advance(kMotor / 2);
        g.out(0xC0, 0x01);
        g.m.clock.advance(kMotor / 2);
        g.out(0xC0, 0x01);
        g.m.clock.advance(kMotor / 2);
        CHECK(g.in(0xC1) & 0x04, "each OUT C0 retriggers the timer");
    }

    SECTION("vgdmdc: step and track 0");
    {
        Rig g;
        g.out(0xC0, 0x01 | kIn);
        g.out(0xC0, 0x01 | kIn | kStep);
        CHECK(g.dm->driveTrack(1) == 1, "a rising edge steps in");
        g.out(0xC0, 0x01 | kIn | kStep);
        CHECK(g.dm->driveTrack(1) == 1, "a level does not step");
        CHECK((g.in(0xC0) & 0x04) == 0, "track 0 is low off track 0");
        g.out(0xC0, 0x01 | kIn);
        CHECK(g.dm->driveTrack(1) == 1, "nor does the falling edge");
        CHECK(g.dm->driveTrack(0) == 0, "the other heads stay");

        g.out(0xC0, 0x01);
        g.out(0xC0, 0x01 | kStep);
        g.out(0xC0, 0x01);
        CHECK(g.dm->driveTrack(1) == 0 && (g.in(0xC0) & 0x04), "direction 0 steps out");
        g.out(0xC0, 0x01 | kStep);
        g.out(0xC0, 0x01);
        CHECK(g.dm->driveTrack(1) == 0, "the stop at track 0");

        for (int i = 0; i < 100; ++i) {
            g.out(0xC0, 0x01 | kIn);
            g.out(0xC0, 0x01 | kIn | kStep);
        }
        CHECK(g.dm->driveTrack(1) == 76, "the stop at track 76");
    }

    SECTION("vgdmdc: busy floats the board");
    {
        Rig g;
        CHECK(g.load(1, formatted(kTwoSides)), "mount");
        g.out(0xC0, 0x01);
        g.out(0xC1, kRead | 3);
        (void)g.in(0xC3);
        g.out(0xC3, 0);

        for (uint8_t p = 0xC0; p <= 0xC3; ++p) CHECK(g.in(p) == 0xFF, "each port floats");

        // Sector 3 is at most 16 sector times away, plus itself.
        uint64_t t = 0;
        while (g.in(0xC1) == 0xFF && t < kPerSector * 18) {
            g.out(0xC2, 0x55);  // lost
            g.out(0xC0, 0x03);  // lost
            g.m.clock.advance(100);
            t += 100;
        }
        CHECK(t > kPerSector / 2 && t <= kPerSector * 17, "busy lasts for the sector to come round");
        CHECK(g.in(0xC1) == (kStatus1 | 0x04), "status 1 bit 1 reads 0 when the board answers");
        CHECK(g.m.clock.queued() == 0, "no deadline is left in the queue");
        CHECK(g.ramOut(1)[0] == 0xFF, "the sync byte, so OUT C2 was lost and the read landed");
        CHECK(g.in(0xC0) & 0x04, "OUT C0 was lost too: the head and the select stayed");
    }

    SECTION("vgdmdc: the wait follows the disk");
    {
        Rig g;
        CHECK(g.load(1, formatted(kTwoSides)), "mount");
        g.m.clock.advance(kPerSector - g.m.clock.now() % kPerSector + 100);  // 100 T into a sector
        const int here = (int)((g.m.clock.now() / kPerSector) % 16);

        g.out(0xC0, 0x01);
        g.out(0xC1, (uint8_t)(kRead | ((here + 1) % 16)));
        g.out(0xC3, 0);
        g.m.clock.advance(kPerSector * 2 - 200);
        CHECK(g.in(0xC1) == 0xFF, "the next sector is not over yet");
        g.m.clock.advance(100);
        CHECK(g.in(0xC1) != 0xFF, "the next sector, and one sector time");

        g.m.clock.advance(kPerSector - g.m.clock.now() % kPerSector + 100);
        const int now = (int)((g.m.clock.now() / kPerSector) % 16);
        g.out(0xC1, (uint8_t)(kRead | now));
        g.out(0xC3, 0);
        g.m.clock.advance(kPerSector * 16);
        CHECK(g.in(0xC1) == 0xFF, "a sector already under the head waits a whole turn");
        g.m.clock.advance(kPerSector);
        CHECK(g.in(0xC1) != 0xFF, "and then passes");
    }

    SECTION("vgdmdc: START with no motor, and with no disk");
    {
        Rig g;
        g.out(0xC3, 0);
        CHECK(g.in(0xC1) == kStatus1, "the timer has ended, so START does not stay busy");

        g.out(0xC0, 0x02);  // an empty drive
        g.out(0xC3, 0);
        g.m.clock.advance(kMotor - 1);
        CHECK(g.in(0xC1) == 0xFF, "no sector pulse comes, so the board waits");
        g.m.clock.advance(1);
        CHECK(g.in(0xC1) == kStatus1, "until the timer frees it");

        CHECK(g.load(1, formatted(kTwoSides)), "mount");
        (void)g.in(0xC3);
        g.out(0xC2, 0x77);
        g.out(0xC1, kRead);
        g.out(0xC0, 0x01);
        g.m.clock.advance(kMotor - kPerSector);
        g.out(0xC3, 0);
        g.m.clock.advance(kPerSector);
        CHECK(g.in(0xC1) == kStatus1, "the timer ends a START that has no time left");
        CHECK(g.ramOut(1)[0] == 0x77, "and no transfer ran");
    }

    SECTION("vgdmdc: reading, by track, side and sector");
    {
        Rig g;
        CHECK(g.load(0, formatted(kTwoSides)), "two sides by size");
        g.out(0xC0, 0x00 | kIn);
        for (int i = 0; i < 5; ++i) {
            g.out(0xC0, 0x00 | kIn | kStep);
            g.out(0xC0, 0x00 | kIn);
        }
        g.transfer(0x00, kRead | 9);
        auto a = g.ramOut(275);
        CHECK(a == g.onDisk(0, 5, 9), "side 0 track 5 sector 9, the slot as stored");
        CHECK(a[3] == 0 && a[1] == 5 && a[2] == 9, "its header");

        g.transfer(0x04, kRead | 9);
        auto b = g.ramOut(275);
        CHECK(b == g.onDisk(1, 5, 9) && b[3] == 1, "bit 2 is the side, and side 1 is at slot 1232");

        (void)g.in(0xC3);
        for (int i = 0; i < 275; ++i) (void)g.in(0xC2);
        CHECK(g.in(0xC2) == 0, "the postamble reads as zeros");

        CHECK(g.load(2, formatted(kOneSide)), "one side by size");
        g.transfer(0x02, kRead | 1);
        CHECK(g.ramOut(1)[0] == 0xFF, "side 0 of the one-sided disk");
        (void)g.in(0xC3);
        g.out(0xC2, 0x77);
        g.transfer(0x06, kRead | 1);
        CHECK(g.ramOut(1)[0] == 0x77, "side 1 of it gives no sync, so RAM keeps what it held");
    }

    SECTION("vgdmdc: the ECC");
    {
        // Track 5, sector 9, data 00 to FF. The two values come from a separate script, whose
        // ECC matches the stored one on 2446 sectors of a real disk.
        std::vector<uint8_t> s(270, 0);
        s[0] = 0xFF;
        s[1] = 5;
        s[2] = 9;
        for (int i = 0; i < 256; ++i) s[13 + (size_t)i] = (uint8_t)i;
        s[269] = 0x0D;
        CHECK(DualModeBoard::ecc(s.data(), 270) == 0xAC73A5D5,
              "g(x) over the bytes from sync to checksum");

        // Good sector, ECC on.
        Rig g;
        auto img = formatted(kTwoSides);
        auto good = makeSlot(0, 2, 0, 0xAA);
        uint32_t e = DualModeBoard::ecc(good.data(), 270);
        for (size_t i = 0; i < 4; ++i) good[270 + i] = (uint8_t)(e >> (24 - 8 * i));
        std::memcpy(&img[slotAt(0, 0, 2)], good.data(), kSlot);

        // The same sector with a data bit changed and the stored ECC left as it was.
        auto bad = good;
        bad[100] ^= 0x10;
        std::memcpy(&img[slotAt(0, 0, 3)], bad.data(), kSlot);

        CHECK(g.load(1, img), "mount");
        g.transfer(0x01, kRead | kEcc | 2);
        auto r = g.ramOut(275);
        CHECK(r[270] == 0 && r[271] == 0 && r[272] == 0 && r[273] == 0, "a good sector reads 00");
        CHECK(r[274] == 0xAA, "and the valid byte is plain data");

        g.transfer(0x01, kRead | 2);
        r = g.ramOut(275);
        CHECK(r == good, "without ECC ENABLE the stored bytes come back");

        g.transfer(0x01, kRead | kEcc | 3);
        r = g.ramOut(275);
        uint32_t want = DualModeBoard::ecc(bad.data(), 270) ^ e;
        uint32_t got  = (uint32_t)r[270] << 24 | (uint32_t)r[271] << 16 | (uint32_t)r[272] << 8 | r[273];
        CHECK(got != 0 && got == want, "a damaged sector gives the syndrome, not 00");
    }

    SECTION("vgdmdc: writing");
    {
        Rig g;
        CHECK(g.load(1, formatted(kTwoSides)), "mount");
        auto slot = makeSlot(0, 7, 0, 0xAA);
        for (int i = 0; i < 256; ++i) slot[13 + (size_t)i] = (uint8_t)~i;

        g.loadWrite(slot);
        g.transfer(0x01, 7);
        CHECK(g.onDisk(0, 0, 7) == slot, "no ECC ENABLE: the bytes after the zeros, as loaded");
        CHECK(g.onDisk(0, 0, 6) == makeSlot(0, 6) && g.onDisk(0, 0, 8) == makeSlot(0, 8),
              "the neighbors are untouched");

        g.loadWrite(slot);
        g.transfer(0x01, kEcc | 8);
        auto w = g.onDisk(0, 0, 8);
        auto want = slot;
        uint32_t e = DualModeBoard::ecc(slot.data(), 270);
        for (size_t i = 0; i < 4; ++i) want[270 + i] = (uint8_t)(e >> (24 - 8 * i));
        CHECK(w == want, "ECC ENABLE puts the ECC in at RAM 310-313");
        CHECK(g.ramOut(315)[310] == 0, "and RAM keeps its 00s");

        g.transfer(0x01, kRead | kEcc | 8);
        auto r = g.ramOut(275);
        CHECK(r[270] == 0 && r[273] == 0 && r[13] == 0xFF, "the write reads back clean");
        CHECK(g.disk->syncs() > 0, "and it reached the medium");

        (void)g.in(0xC3);
        for (int i = 0; i < 512; ++i) g.out(0xC2, 0);
        g.transfer(0x01, 9);
        CHECK(g.onDisk(0, 0, 9) == std::vector<uint8_t>(kSlot, 0), "all zeros has no sync");
        g.transfer(0x01, kRead | 9);
        (void)g.in(0xC3);
        CHECK(g.in(0xC2) == 0, "and that sector is unformatted");
    }

    SECTION("vgdmdc: write protect drops the write with no error");
    {
        Rig g;
        CHECK(g.load(1, formatted(kTwoSides), /*ro=*/true), "mount");
        auto before = g.onDisk(0, 0, 4);
        g.loadWrite(makeSlot(9, 9, 0, 0xAA));
        g.transfer(0x01, 4);
        CHECK(g.in(0xC1) == (kStatus1 | 0x04), "the write ends as usual");
        CHECK(g.onDisk(0, 0, 4) == before, "the disk did not change");
        auto log = g.dm->drainLog();
        CHECK(log.size() == 1, "the operator hears it once");
        g.transfer(0x01, 4);
        CHECK(g.dm->drainLog().empty(), "and only once");
    }

    SECTION("vgdmdc: blank disks, and sizes the board does not know");
    {
        Rig g;
        CHECK(g.load(1, {}), "an empty file is an unformatted disk");
        g.out(0xC2, 0x77);
        g.transfer(0x01, kRead | 2);
        CHECK(g.ramOut(1)[0] == 0x77, "with no sync");
        auto slot = makeSlot(0, 2);
        g.loadWrite(slot);
        g.transfer(0x01, 2);
        CHECK(g.disk->bytes().size() == kSlot * 3, "the file grows as the guest formats it");
        CHECK(g.onDisk(0, 0, 2) == slot, "with the slot where it belongs");

        // The XMODEM pad of a file that crossed a line.
        std::vector<uint8_t> padded = formatted(kOneSide);
        padded.resize((size_t)kOneSide + 100);
        CHECK(g.load(2, padded), "a padded one-sided image");
        g.out(0xC2, 0x77);
        g.transfer(0x06, kRead);
        CHECK(g.ramOut(1)[0] == 0x77, "still reads as one side");
    }

    SECTION("vgdmdc: POC clears the latches, RESET does not");
    {
        Rig g;
        g.out(0xC0, 0x01 | kIn);
        g.out(0xC1, kRead | 5);
        g.dm->reset(Reset::Bus);
        CHECK(g.in(0xC1) & 0x04, "the timer runs on through RESET*");
        g.dm->reset(Reset::PowerOn);
        CHECK(g.in(0xC1) == kStatus1, "POC* clears the one-shot");
    }

    SECTION("vgdmdc: snapshot");
    {
        Rig src;
        CHECK(src.load(1, formatted(kTwoSides)), "mount");
        src.out(0xC0, 0x01 | kIn);
        src.out(0xC0, 0x01 | kIn | kStep);
        (void)src.in(0xC3);
        src.out(0xC2, 0x12);
        src.out(0xC2, 0x34);
        src.out(0xC2, 0x56);
        (void)src.in(0xC3);
        (void)src.in(0xC2);       // the counter is at 1
        src.out(0xC1, kRead | 1);
        src.out(0xC0, 0x02);      // an empty drive, so busy runs to the end of the timer
        src.out(0xC3, 0);

        StateWriter w;
        src.dm->serialize(w);
        Rig dst;
        dst.m.clock.advance(src.m.clock.now());
        StateReader r(w.data());
        dst.dm->deserialize(r);
        CHECK(r.ok(), "the blob is read in full");
        CHECK(dst.dm->driveTrack(1) == 1, "the head travelled");
        CHECK(dst.in(0xC1) == 0xFF, "busy travelled");
        dst.m.clock.advance(kMotor - 1);
        CHECK(dst.in(0xC1) == 0xFF, "busy runs to the same T-state");
        dst.m.clock.advance(1);
        CHECK(dst.in(0xC1) == kStatus1, "the motor timer travelled");
        CHECK(dst.in(0xC2) == 0x34, "the RAM and the counter travelled");
        dst.out(0xC0, 0x00);
        CHECK(dst.in(0xC0) & 0x04, "drive 0 is on track 0");
    }

    SECTION("vgdmdc: the machine file side");
    {
        Rig g;
        KeyValues kv = {{"unit", "1"}, {"mount", "a.vgi"}, {"readonly", "true"}};
        setMediaResolver([](const std::string& p, bool r, std::string&) {
            return std::make_unique<MemoryMedia>(p, std::vector<uint8_t>((size_t)kTwoSides, 0), r);
        });
        std::string err;
        CHECK(g.dm->loadSubUnit("drive", kv, err), "a [[board.drive]]");
        auto su = g.dm->subUnits();
        CHECK(su.size() == 1 && su[0].fields.size() == 3, "CONFIG SAVE writes that one back");
        CHECK(su[0].fields[1].text == "a.vgi", "by the path as written");

        auto u = g.dm->units();
        CHECK(u.size() == 4 && u[1].readOnly && u[0].state == "(empty)", "four drives");

        CHECK(!g.dm->mount("drive4", "b.vgi", false, err), "there is no drive 4");
        CHECK(g.dm->unmount("drive1", err), "unmount");
        CHECK(!g.dm->unmount("drive1", err), "and the drive is empty");
    }

    SECTION("vgdmdc: Monitor 4.3 boots from drive 1");
    {
        Vector g;
        auto   img = formatted(kTwoSides);
        putBootSector(img, {0x3E, 0x5A,        // LD A,5A
                            0x32, 0x00, 0x01,  // LD (0100),A
                            0x76});            // HALT
        g.load(std::move(img));

        // The head starts away from track 0. The Monitor seeks by the track byte it reads back.
        g.m.bus.ioWrite(0xC0, 0x01 | kIn);
        for (int i = 0; i < 3; ++i) {
            g.m.bus.ioWrite(0xC0, 0x01 | kIn | kStep);
            g.m.bus.ioWrite(0xC0, 0x01 | kIn);
        }
        CHECK(g.dm->driveTrack(1) == 3, "the head is on track 3");

        g.m.bus.memWrite(0x0100, 0);
        CHECK(g.run([&] { return g.promptShown(); }, 2000000), "the Monitor comes up first");
        g.kbd->feed("B");
        CHECK(g.run([&] { return g.m.bus.memRead(0x0100) == 0x5A; }, 20000000),
              "B ran the code from track 0 sector 0, at 0600");
        CHECK(g.dm->driveTrack(1) == 0, "and the head went home");
        CHECK(g.m.bus.memRead(0x0600) == 0x3E, "the sector landed at 0600");
    }

    SECTION("vgdmdc: the Monitor refuses a sector that does not start with 3E");
    {
        Vector g;
        auto   img = formatted(kTwoSides);
        putBootSector(img, {0x00, 0x3E, 0x5A, 0x32, 0x00, 0x01, 0x76});
        g.load(std::move(img));
        g.m.bus.memWrite(0x0100, 0);
        g.kbd->feed("B");
        g.run([] { return false; }, 6000000);
        CHECK(g.m.bus.memRead(0x0100) == 0, "the code never ran");
        CHECK(g.m.cpu()->pc() >= 0xE000, "and the guest is still in the Monitor");
    }
}
