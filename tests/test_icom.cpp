// iCOM FD3712 / FD3812 8" floppy controller (src/boards/icom-fd3712.h,
// docs/boards/icom-fd3712.md).
//
// The acceptance tests boot CP/M and FDOS off the real disks end to end. This file pins the
// board-level command/handshake engine the boot cannot see from the outside: the two-port
// (C0/C1) protocol, the unified read model (an IN advances the buffer pointer, so the 3712's
// cSHIFT-per-byte and the 3812's back-to-back INs both work), the unified write model (the
// 3712's cWRTBUF-strobe and the 3812's streaming OUT C1), the SD/DD geometry probe (128-byte
// sectors everywhere on SD; 128 on DD track 0, 256 on DD tracks 1-76), the status bits
// (write-protect, drive-not-ready), and the derived boot-PROM / 6810 scratch-RAM windows.

#include "boards/icom-fd3712.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/value.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint8_t P    = 0xC0;  // default base
constexpr uint8_t C0   = P + 0;  // command out / status-or-buffer in
constexpr uint8_t C1   = P + 1;  // data out

// Controller commands (reference section 3).
constexpr uint8_t cREAD    = 0x03;
constexpr uint8_t cWRITE   = 0x05;
constexpr uint8_t cSEEK    = 0x09;
constexpr uint8_t cSETTRK  = 0x11;
constexpr uint8_t cDRVSEC  = 0x21;
constexpr uint8_t cWRTBUF12 = 0x30;  // FD3812 streaming form
constexpr uint8_t cWRTBUF71 = 0x31;  // FD3712 strobe form
constexpr uint8_t cRDBUF   = 0x40;
constexpr uint8_t cSHIFT   = 0x41;
constexpr uint8_t cSTATUS  = 0x00;
constexpr uint8_t cCLEAR   = 0x81;   // "Clear" -- abort; must NOT rewind the write buffer

constexpr int kTracks  = 77;
constexpr int kSectors = 26;
constexpr uint64_t kSdBytes = (uint64_t)kTracks * kSectors * 128;                  // 256,256
constexpr uint64_t kDdBytes = (uint64_t)kSectors * 128 + (uint64_t)(kTracks - 1) * kSectors * 256;  // 509,184

uint8_t in(IcomFdBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(IcomFdBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// A deterministic, position-unique disk byte -- no two 128/256-byte windows look alike.
uint8_t diskByte(uint64_t i) { return (uint8_t)((i * 131u + (i >> 7) * 17u + 3u) & 0xFF); }

// Where physical (track, sector) lands in a raw image of this density. Sectors number from 1.
uint64_t sdOffset(int track, int sector) {
    return ((uint64_t)track * kSectors + (uint64_t)(sector - 1)) * 128;
}
uint64_t ddOffset(int track, int sector, size_t& len) {
    if (track == 0) { len = 128; return (uint64_t)(sector - 1) * 128; }
    len = 256;
    return (uint64_t)kSectors * 128 + ((uint64_t)(track - 1) * kSectors + (uint64_t)(sector - 1)) * 256;
}

void withDisk(uint64_t bytes) {
    setMediaResolver([bytes](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d((size_t)bytes);
        for (uint64_t i = 0; i < bytes; ++i) d[(size_t)i] = diskByte(i);
        return std::make_unique<MemoryMedia>(path, std::move(d), ro);
    });
}

// Build a powered board with `drives` drives and (optionally) a disk in drive 0.
IcomFdBoard* makeBoard(Clock& clk, int drives, const char* mount, bool ro = false,
                       const char* rom = "builtin:icom-fd3712-cpm") {
    auto* b = new IcomFdBoard();
    b->id = "fdc0";
    b->attachClock(&clk);
    std::string err;
    setProperty(*b, "rom", rom, err);
    setProperty(*b, "drives", std::to_string(drives), err);
    b->power();
    if (mount) {
        bool ok = b->mount("drive0", mount, ro, err);
        CHECK(ok, err.empty() ? "mount failed" : err.c_str());
    }
    return b;
}

// Point the controller at (unit, track, sector) and issue a read. The PROM always trails a
// command with an examine-status (cSTATUS) to clear the strobe; we do the same.
void seekTo(IcomFdBoard& b, int unit, int track, int sector) {
    out(b, C1, (uint8_t)((unit << 6) | sector)); out(b, C0, cDRVSEC); out(b, C0, cSTATUS);
    out(b, C1, (uint8_t)track);                  out(b, C0, cSETTRK); out(b, C0, cSTATUS);
    out(b, C0, cSEEK);                            out(b, C0, cSTATUS);
}

uint8_t statusOf(IcomFdBoard& b) {
    out(b, C0, cSTATUS);  // examine-status mode
    return in(b, C0);
}

// Read n bytes the FD3712 way: cRDBUF, IN, then (cSHIFT, IN) per following byte.
std::vector<uint8_t> read3712(IcomFdBoard& b, int n) {
    std::vector<uint8_t> v;
    out(b, C0, cRDBUF);
    v.push_back(in(b, C0));
    out(b, C0, cSTATUS);
    for (int i = 1; i < n; ++i) {
        out(b, C0, cSHIFT);
        v.push_back(in(b, C0));
        out(b, C0, cSTATUS);
    }
    return v;
}

// Read n bytes the FD3812 way: one cRDBUF, then n back-to-back INs.
std::vector<uint8_t> read3812(IcomFdBoard& b, int n) {
    std::vector<uint8_t> v;
    out(b, C0, cRDBUF);
    for (int i = 0; i < n; ++i) v.push_back(in(b, C0));
    return v;
}

// Fill the write buffer the FD3712 way: per byte OUT C1, cWRTBUF(0x31), cSTATUS.
void write3712(IcomFdBoard& b, const std::vector<uint8_t>& data) {
    for (uint8_t byte : data) {
        out(b, C1, byte);
        out(b, C0, cWRTBUF71);
        out(b, C0, cSTATUS);
    }
}

// Fill the write buffer the FD3812 way: cWRTBUF(0x30) once, then stream OUT C1.
void write3812(IcomFdBoard& b, const std::vector<uint8_t>& data) {
    out(b, C0, cWRTBUF12);
    for (uint8_t byte : data) out(b, C1, byte);
    out(b, C0, cSTATUS);
}

bool matches(const std::vector<uint8_t>& got, uint64_t off, size_t len) {
    if (got.size() != len) return false;
    for (size_t k = 0; k < len; ++k)
        if (got[k] != diskByte(off + k)) return false;
    return true;
}

} // namespace

void test_icom() {
    SECTION("icom: single-density read, both buffer protocols");
    {
        withDisk(kSdBytes);
        Clock clk;
        IcomFdBoard* b = makeBoard(clk, 2, "cpm.dsk");

        // A representative sector, read back two ways -- the 3712 cSHIFT loop and the 3812
        // back-to-back INs must agree, because the engine is one model.
        seekTo(*b, 0, 12, 7);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK((statusOf(*b) & 0x28) == 0, "read status: no CRC/not-ready");
        CHECK(matches(read3712(*b, 128), sdOffset(12, 7), 128), "3712 read of t12 s7");

        seekTo(*b, 0, 12, 7);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(matches(read3812(*b, 128), sdOffset(12, 7), 128), "3812 read of t12 s7");

        // First and last sector of the disk, to pin the geometry ends.
        seekTo(*b, 0, 0, 1);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(matches(read3712(*b, 128), sdOffset(0, 1), 128), "read of t0 s1");

        seekTo(*b, 0, 76, 26);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(matches(read3712(*b, 128), sdOffset(76, 26), 128), "read of t76 s26");

        delete b;
    }

    SECTION("icom: single-density write, both buffer protocols, then read back");
    {
        withDisk(kSdBytes);
        Clock clk;
        IcomFdBoard* b = makeBoard(clk, 2, "cpm.dsk");

        // 3712 strobe-form write to t5 s10.
        std::vector<uint8_t> pat(128);
        for (int k = 0; k < 128; ++k) pat[(size_t)k] = (uint8_t)(0xA0 + k);
        seekTo(*b, 0, 5, 10);
        write3712(*b, pat);
        out(*b, C0, cWRITE); out(*b, C0, cSTATUS);
        CHECK((statusOf(*b) & 0x28) == 0, "write status: no error");

        seekTo(*b, 0, 5, 10);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(read3712(*b, 128) == pat, "3712 write then read back");

        // 3812 streaming-form write to a different sector.
        std::vector<uint8_t> pat2(128);
        for (int k = 0; k < 128; ++k) pat2[(size_t)k] = (uint8_t)(0x5A ^ k);
        seekTo(*b, 0, 40, 3);
        write3812(*b, pat2);
        out(*b, C0, cWRITE); out(*b, C0, cSTATUS);

        seekTo(*b, 0, 40, 3);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(read3812(*b, 128) == pat2, "3812 write then read back");

        delete b;
    }

    SECTION("icom: the Clear command does not rewind the write buffer");
    {
        // Real hardware never resets the write "shift register" pointer on a Clear (81); a bug
        // shared by the FDC+ and AltairZ80 FD3712. Load half a sector, issue a Clear mid-fill,
        // load the rest, and the buffer must still hold a contiguous 128 bytes.
        withDisk(kSdBytes);
        Clock clk;
        IcomFdBoard* b = makeBoard(clk, 2, "cpm.dsk");

        std::vector<uint8_t> pat(128);
        for (int k = 0; k < 128; ++k) pat[(size_t)k] = (uint8_t)(0x11 + 3 * k);

        seekTo(*b, 0, 7, 4);
        write3712(*b, {pat.begin(), pat.begin() + 64});   // first half
        out(*b, C0, cCLEAR); out(*b, C0, cSTATUS);        // Clear must not rewind writePtr_
        write3712(*b, {pat.begin() + 64, pat.end()});     // second half continues in place
        out(*b, C0, cWRITE); out(*b, C0, cSTATUS);
        CHECK((statusOf(*b) & 0x28) == 0, "write status after Clear: no error");

        seekTo(*b, 0, 7, 4);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(read3712(*b, 128) == pat, "Clear mid-fill left the write buffer contiguous");

        delete b;
    }

    SECTION("icom: status bits -- write protect and drive not ready");
    {
        withDisk(kSdBytes);
        Clock clk;
        IcomFdBoard* b = makeBoard(clk, 2, "cpm.dsk", /*ro=*/true);

        // Unit 0 has a write-protected disk: bit4 set, bit5 clear.
        seekTo(*b, 0, 3, 1);
        uint8_t s0 = statusOf(*b);
        CHECK((s0 & 0x10) != 0, "unit0 write-protected (bit4)");
        CHECK((s0 & 0x20) == 0, "unit0 present (bit5 clear)");

        // A write is dropped and the medium is untouched.
        std::vector<uint8_t> pat(128, 0xEE);
        write3712(*b, pat);
        out(*b, C0, cWRITE); out(*b, C0, cSTATUS);
        seekTo(*b, 0, 3, 1);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        CHECK(matches(read3712(*b, 128), sdOffset(3, 1), 128), "write-protected sector unchanged");

        // Unit 1 has no disk: bit5 set.
        out(*b, C1, (uint8_t)((1 << 6) | 1)); out(*b, C0, cDRVSEC); out(*b, C0, cSTATUS);
        CHECK((statusOf(*b) & 0x20) != 0, "unit1 empty -> drive not ready (bit5)");

        delete b;
    }

    SECTION("icom: double-density geometry -- 128-byte track 0, 256-byte data tracks");
    {
        withDisk(kDdBytes);
        Clock clk;
        IcomFdBoard* b = makeBoard(clk, 2, "cpmdd.dsk", /*ro=*/false, "builtin:icom-fd3812-cpm");

        // Track 0 stays single density: a 128-byte sector.
        seekTo(*b, 0, 0, 1);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        size_t len0; uint64_t off0 = ddOffset(0, 1, len0);
        CHECK(matches(read3812(*b, 128), off0, len0), "DD track 0 is 128-byte SD");

        // A data track is double density: a 256-byte sector, read back-to-back.
        seekTo(*b, 0, 1, 1);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        size_t len1; uint64_t off1 = ddOffset(1, 1, len1);
        CHECK(len1 == 256, "DD data-track sector is 256 bytes");
        CHECK(matches(read3812(*b, 256), off1, len1), "DD track 1 is 256-byte DD");

        // The last DD sector, to pin the top of the image.
        seekTo(*b, 0, 76, 26);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        size_t lenN; uint64_t offN = ddOffset(76, 26, lenN);
        CHECK(matches(read3812(*b, 256), offN, lenN), "DD track 76 s26");

        // Reading past the sector floats -- the pointer clamps at the bytes actually read.
        seekTo(*b, 0, 1, 2);
        out(*b, C0, cREAD); out(*b, C0, cSTATUS);
        std::vector<uint8_t> over = read3812(*b, 257);
        CHECK(over.size() == 257 && over[256] == 0xFF, "IN past the sector returns 0xFF");

        delete b;
    }

    SECTION("icom: boot PROM and 6810 scratch RAM windows");
    {
        Clock clk;
        IcomFdBoard* b = makeBoard(clk, 2, nullptr);  // CP/M PROM, base derived at F000

        // The CP/M PROM begins `jmp boot` (0xC3) at F000, and the interface RAM is 1K above it.
        uint8_t v = 0;
        CHECK(b->peek(0xF000, v) && v == 0xC3, "PROM at F000 begins with JMP (0xC3)");
        BusCycle mr; mr.type = Cycle::MemRead; mr.addr = 0xF000;
        CHECK(b->read(mr) == 0xC3, "MemRead F000 returns the PROM byte");

        // The scratch RAM at F400 is read/write; the PROM at F000 ignores writes.
        BusCycle wr; wr.type = Cycle::MemWrite; wr.addr = 0xF400; wr.data = 0x5A;
        b->write(wr);
        CHECK(b->peek(0xF400, v) && v == 0x5A, "6810 scratch RAM at F400 holds a written byte");
        BusCycle wrom; wrom.type = Cycle::MemWrite; wrom.addr = 0xF000; wrom.data = 0x00;
        b->write(wrom);
        CHECK(b->peek(0xF000, v) && v == 0xC3, "a write into the PROM window is ignored");

        // The FDOS PROM places itself at C000 instead -- the window base is derived, not fixed.
        std::string err;
        setProperty(*b, "rom", "builtin:icom-fd3712-fdos", err);
        b->power();
        CHECK(b->peek(0xC000, v) && v == 0xC3, "FDOS PROM lands at C000 (JMP fdos)");
        CHECK(!b->peek(0xF000, v), "nothing at F000 once the FDOS PROM is loaded");

        delete b;
    }
}
