// S100Computers IDE-AB (CF) board (src/boards/dualide.h, reference/dual-ide-card.md).
//
// This pins the 8255 -> ATA/IDE register engine as a faithful model of HIDE3.ASM's
// programmed-I/O strobes: the IDEwr8D / IDErd8D single-register handshake, the 16-bit sector
// read/write loops (WRSEC1 / MoreRD16), the LBA the sector/cyl/shd registers assemble, the
// status byte IDEwaitnotbusy / IDEwaitdrq poll, a write-protected card, the two CF sockets and
// the drive-select port, an empty socket floating 0xFF, the port strap, and -- the point of the
// whole exercise -- that a card written by `dualide` is read byte-for-byte by `dualsd`, so the
// same .img/.geo card serves A:/B: (IDE) and C:/D: (SD).

#include "boards/dualide.h"
#include "boards/dualsd.h"
#include "core/board.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint8_t BASE = 0x30;
constexpr size_t  SEC  = 512;

// 8255 port offsets (HIDE3.ASM IDEportA..IDEDrive).
constexpr uint8_t P_A    = BASE + 0;   // data low / register read-back
constexpr uint8_t P_B    = BASE + 1;   // data high
constexpr uint8_t P_C    = BASE + 2;   // control lines
constexpr uint8_t P_CFG  = BASE + 3;   // 8255 mode config
constexpr uint8_t P_DRV  = BASE + 4;   // drive select

// 8255 mode configs and control-line bits (HIDE3.ASM).
constexpr uint8_t READcfg  = 0x92;
constexpr uint8_t WRITEcfg = 0x80;
constexpr uint8_t CS0 = 0x08, WR = 0x20, RD = 0x40, RST = 0x80;

// ATA registers (portC & 0x0F with CS0 asserted) and commands.
constexpr uint8_t REGsector = CS0 | 0x03;  // 0x0B
constexpr uint8_t REGcylLSB = CS0 | 0x04;  // 0x0C
constexpr uint8_t REGcylMSB = CS0 | 0x05;  // 0x0D
constexpr uint8_t REGseccnt = CS0 | 0x02;  // 0x0A
constexpr uint8_t REGdata   = CS0;         // 0x08
constexpr uint8_t REGstatus = CS0 | 0x07;  // 0x0F (== REGcommand)
constexpr uint8_t REGcmd    = CS0 | 0x07;  // 0x0F
constexpr uint8_t CMDread   = 0x20;
constexpr uint8_t CMDwrite  = 0x30;

// The card the resolver hands the board, kept for white-box asserts while the board is alive.
std::vector<MemoryMedia*> g_cards;
uint32_t                  g_cardSectors = 256;
std::vector<uint8_t>      g_preload;   // if non-empty, each card starts as a copy of this

void installCards(uint32_t sectors) {
    g_cards.clear();
    g_cardSectors = sectors;
    g_preload.clear();
    setMediaResolver([](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d = g_preload.empty()
            ? std::vector<uint8_t>((size_t)g_cardSectors * SEC, 0x00)
            : g_preload;
        auto m = std::make_unique<MemoryMedia>(path, std::move(d), ro);
        g_cards.push_back(m.get());
        return std::unique_ptr<MediaFile>(std::move(m));
    });
}

template <class B> uint8_t in(B& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
template <class B> void out(B& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// IDEwr8D: write one byte to an IDE register. The engine acts on the WR rising edge; the config
// writes and the deassert/zero of portC are faithful to the BIOS and harmless to the model.
void writeReg(DualIdeBoard& b, uint8_t reg, uint8_t val) {
    out(b, P_CFG, WRITEcfg);
    out(b, P_A, val);
    out(b, P_C, reg);
    out(b, P_C, (uint8_t)(reg | WR));
    out(b, P_C, reg);
    out(b, P_C, 0x00);
    out(b, P_CFG, READcfg);
}

// IDErd8D: read one byte from an IDE register (the value latches on the RD rising edge).
uint8_t readReg(DualIdeBoard& b, uint8_t reg) {
    out(b, P_C, reg);
    out(b, P_C, (uint8_t)(reg | RD));
    uint8_t v = in(b, P_A);
    out(b, P_C, reg);
    out(b, P_C, 0x00);
    return v;
}

uint8_t statusOf(DualIdeBoard& b) { return readReg(b, REGstatus); }

// wrlba: load the LBA into the sector/cylinder registers. Head bits (REGshd low nibble) stay 0,
// so LBA = sector | cylLSB<<8 | cylMSB<<16 -- the range these tests use.
void setLba(DualIdeBoard& b, uint32_t lba) {
    writeReg(b, REGsector, (uint8_t)(lba & 0xFF));
    writeReg(b, REGcylLSB, (uint8_t)((lba >> 8) & 0xFF));
    writeReg(b, REGcylMSB, (uint8_t)((lba >> 16) & 0xFF));
    writeReg(b, REGseccnt, 1);
}

// HDWRT: issue WRITE, then shift 256 words {low=portA, high=portB} out REGdata. Returns the
// final status byte (bit0 = ERR), as CHECK$RW reads it.
uint8_t writeSector(DualIdeBoard& b, uint32_t lba, const std::vector<uint8_t>& data) {
    setLba(b, lba);
    (void)readReg(b, REGstatus);          // IDEwaitnotbusy
    writeReg(b, REGcmd, CMDwrite);
    (void)readReg(b, REGstatus);          // IDEwaitdrq
    out(b, P_CFG, WRITEcfg);
    for (size_t i = 0; i < SEC; i += 2) {
        out(b, P_A, data[i]);             // low byte
        out(b, P_B, data[i + 1]);         // high byte
        out(b, P_C, REGdata);
        out(b, P_C, (uint8_t)(REGdata | WR));
        out(b, P_C, REGdata);
    }
    out(b, P_CFG, READcfg);
    return readReg(b, REGstatus);
}

// HDRD: issue READ, then shift 256 words in from REGdata (low from portA, high from portB).
std::vector<uint8_t> readSector(DualIdeBoard& b, uint32_t lba, uint8_t* status = nullptr) {
    setLba(b, lba);
    (void)readReg(b, REGstatus);          // IDEwaitnotbusy
    writeReg(b, REGcmd, CMDread);
    (void)readReg(b, REGstatus);          // IDEwaitdrq
    std::vector<uint8_t> v;
    v.reserve(SEC);
    for (size_t i = 0; i < SEC; i += 2) {
        out(b, P_C, REGdata);
        out(b, P_C, (uint8_t)(REGdata | RD));
        v.push_back(in(b, P_A));          // low byte
        v.push_back(in(b, P_B));          // high byte
        out(b, P_C, REGdata);
    }
    uint8_t st = readReg(b, REGstatus);
    if (status) *status = st;
    return v;
}

std::vector<uint8_t> pattern(uint8_t salt) {
    std::vector<uint8_t> v(SEC);
    for (size_t i = 0; i < SEC; ++i) v[i] = (uint8_t)((i * 37u + salt * 101u + 7u) & 0xFF);
    return v;
}

DualIdeBoard* makeBoard(Clock& clk, const char* mountA, const char* mountB = nullptr,
                        bool roA = false) {
    auto* b = new DualIdeBoard();
    b->id = "ide0";
    b->attachClock(&clk);
    b->power();
    std::string err;
    if (mountA) { bool ok = b->mount("drive0", mountA, roA, err); CHECK(ok, err.c_str()); }
    if (mountB) { bool ok = b->mount("drive1", mountB, false, err); CHECK(ok, err.c_str()); }
    return b;
}

bool mediaHas(MemoryMedia* m, uint32_t lba, const std::vector<uint8_t>& data) {
    const auto& b = m->bytes();
    if (b.size() < (size_t)lba * SEC + SEC) return false;
    for (size_t k = 0; k < SEC; ++k)
        if (b[(size_t)lba * SEC + k] != data[k]) return false;
    return true;
}

} // namespace

void test_dualide() {
    SECTION("dualide: write a sector, it lands at LBA*512 on the medium and reads back, ERR clear");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA");

        auto pat = pattern(0x11);
        CHECK((writeSector(*b, 5, pat) & 0x01) == 0, "WRITE finishes with ERR clear");
        CHECK(mediaHas(g_cards[0], 5, pat), "the sector landed at LBA 5 * 512 on the medium");
        CHECK(g_cards[0]->syncs() > 0, "WRITE synced the medium");

        uint8_t st = 0xFF;
        CHECK(readSector(*b, 5, &st) == pat, "read LBA 5 returns what was written");
        CHECK((st & 0x01) == 0, "READ finishes with ERR clear");

        delete b;
    }

    SECTION("dualide: the status byte answers IDEwaitnotbusy and IDEwaitdrq exactly");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA");

        // Idle, card present: BSY clear, RDY set -> IDEwaitnotbusy's (st & 0xC0) == 0x40.
        uint8_t idle = statusOf(*b);
        CHECK((idle & 0xC0) == 0x40, "idle status: BSY clear, RDY set");
        CHECK((idle & 0x08) == 0x00, "idle status: DRQ clear (no transfer)");

        // After READ, before draining: DRQ set -> IDEwaitdrq's (st & 0x88) == 0x08.
        setLba(*b, 1);
        (void)readReg(*b, REGstatus);
        writeReg(*b, REGcmd, CMDread);
        uint8_t drq = statusOf(*b);
        CHECK((drq & 0x88) == 0x08, "after READ: DRQ set, BSY clear (data is ready)");

        // Drain the 256 words; DRQ clears once the sector is spent.
        for (size_t i = 0; i < SEC; i += 2) {
            out(*b, P_C, REGdata);
            out(*b, P_C, (uint8_t)(REGdata | RD));
            (void)in(*b, P_A);
            (void)in(*b, P_B);
            out(*b, P_C, REGdata);
        }
        CHECK((statusOf(*b) & 0x08) == 0x00, "after the last word DRQ clears");

        // The RST control line aborts a transfer in flight (COMMON$INIT pulses it low).
        setLba(*b, 1);
        (void)readReg(*b, REGstatus);
        writeReg(*b, REGcmd, CMDread);
        CHECK((statusOf(*b) & 0x08) == 0x08, "a fresh READ raises DRQ again");
        out(*b, P_C, RST);                     // assert the reset line
        out(*b, P_C, 0x00);                    // release it
        CHECK((statusOf(*b) & 0x08) == 0x00, "RST drops the in-flight transfer (DRQ clear)");

        delete b;
    }

    SECTION("dualide: the LBA spans all three low registers (sector, cyl LSB, cyl MSB)");
    {
        // A card larger than 64K sectors is the only way to exercise the cyl-MSB byte and prove
        // LBA = sector | cylLSB<<8 | cylMSB<<16, not a 16-bit number.
        installCards(0x012346);   // just past LBA 0x012345
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA");

        auto pat = pattern(0x7C);
        const uint32_t lba = 0x012345;   // sector=0x45, cylLSB=0x23, cylMSB=0x01
        CHECK((writeSector(*b, lba, pat) & 0x01) == 0, "WRITE to a 3-byte LBA is OK");
        CHECK(mediaHas(g_cards[0], lba, pat), "the sector landed at LBA 0x012345 * 512");
        CHECK(readSector(*b, lba) == pat, "and reads back from the same LBA");

        delete b;
    }

    SECTION("dualide: the two CF sockets are independent, drive-select via port BASE+4");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA", "cardB");

        auto patA = pattern(0xA0);
        auto patB = pattern(0x0B);

        out(*b, P_DRV, 0x00);           // select drive A:
        writeSector(*b, 2, patA);
        out(*b, P_DRV, 0x01);           // select drive B:
        writeSector(*b, 2, patB);

        CHECK(mediaHas(g_cards[0], 2, patA), "drive A: medium holds pattern A");
        CHECK(mediaHas(g_cards[1], 2, patB), "drive B: medium holds pattern B");

        out(*b, P_DRV, 0x00);
        CHECK(readSector(*b, 2) == patA, "select 0 reads back A");
        out(*b, P_DRV, 0x01);
        CHECK(readSector(*b, 2) == patB, "select 1 reads back B");

        delete b;
    }

    SECTION("dualide: an empty socket floats 0xFF on every register (the 'not Present' gate)");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, nullptr);   // no card mounted

        CHECK(statusOf(*b) == 0xFF, "REGstatus floats 0xFF -> the BIOS times out and reports absent");
        // A read against the absent drive returns floating 0xFF data and never commits anything.
        uint8_t st = 0x00;
        auto ff = std::vector<uint8_t>(SEC, 0xFF);
        CHECK(readSector(*b, 0, &st) == ff, "a read of an empty socket floats 0xFF");
        CHECK(st == 0xFF, "and status stays floating");

        delete b;
    }

    SECTION("dualide: a write-protected card fails the write with ERR and says so");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA", nullptr, /*roA=*/true);

        auto pat = pattern(0xEE);
        CHECK((writeSector(*b, 4, pat) & 0x01) == 0x01, "the write finishes with ERR set");

        std::vector<uint8_t> zeros(SEC, 0x00);
        CHECK(mediaHas(g_cards[0], 4, zeros), "the write-protected sector is untouched");
        CHECK(g_cards[0]->syncs() == 0, "a dropped write never synced");

        auto log = b->drainLog();
        bool told = false;
        for (const auto& s : log)
            if (s.find("write-protected") != std::string::npos) told = true;
        CHECK(told, "the board reported the dropped write via drainLog");

        delete b;
    }

    SECTION("dualide: the port strap relocates all five ports");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA");

        std::string err;
        CHECK(setProperty(*b, "port", "40", err), err.c_str());
        b->configChanged();

        auto decodesIo = [&](uint8_t port) {
            BusCycle c; c.type = Cycle::IoWrite; c.addr = port; c.data = 0;
            return b->decodes(c);
        };
        for (uint8_t off = 0; off <= 4; ++off)
            CHECK(decodesIo((uint8_t)(0x40 + off)), "decodes the new base 40..44");
        CHECK(!decodesIo(0x30) && !decodesIo(0x34), "no longer decodes the old base 30..34");

        delete b;
    }

    SECTION("dualide: a card written by dualide is read byte-for-byte by dualsd (image interchange)");
    {
        // The point of the whole project: the SAME .img/.geo card serves A:/B: through the IDE
        // half and C:/D: through the SD half. Both build LBA*512 with the low data byte first,
        // so a sector written by one is read identically by the other.
        installCards(256);
        Clock clk;

        const uint32_t lba = 42;
        auto pat = pattern(0x5A);

        // Write the sector through dualide, then snapshot the raw card bytes.
        DualIdeBoard* ide = makeBoard(clk, "cardA");
        CHECK((writeSector(*ide, lba, pat) & 0x01) == 0, "dualide WRITE ok");
        g_preload = g_cards[0]->bytes();     // the exact on-card image
        delete ide;

        // Mount a copy of that image into a dualsd board and read the same LBA back. dualsd's
        // SET_TRK_SEC maps track (high byte) then sector (low byte) to LBA = track*256 + sector,
        // so LBA 42 is track 0, sector 42.
        auto* sd = new DualSdBoard();
        sd->id = "sd0";
        sd->attachClock(&clk);
        sd->power();
        std::string err;
        CHECK(sd->mount("drive0", "cardCopy", false, err), err.c_str());

        auto sdOut = [&](uint8_t v) {
            BusCycle c; c.type = Cycle::IoWrite; c.addr = 0x81; c.data = v; sd->write(c);
        };
        auto sdIn = [&]() {
            BusCycle c; c.type = Cycle::IoRead; c.addr = 0x81; return sd->read(c);
        };
        sdOut(0x33); sdOut(0x84); sdOut((uint8_t)(lba >> 8)); sdOut((uint8_t)(lba & 0xFF));
        (void)sdIn();                                  // SET_TRK_SEC status
        sdOut(0x33); sdOut(0x85);                       // READ
        std::vector<uint8_t> got;
        for (size_t i = 0; i < SEC; ++i) got.push_back(sdIn());
        CHECK(got == pat, "dualsd reads back the exact sector dualide wrote");

        delete sd;
        g_preload.clear();
    }

    SECTION("dualide: serialize -> deserialize round-trips the engine and ATA state");
    {
        installCards(256);
        Clock clk;
        DualIdeBoard* b = makeBoard(clk, "cardA");

        // Put the engine mid-read (DRQ up, some words drained) so there is real state to carry.
        auto pat = pattern(0x3C);
        writeSector(*b, 9, pat);
        setLba(*b, 9);
        (void)readReg(*b, REGstatus);
        writeReg(*b, REGcmd, CMDread);
        for (int word = 0; word < 4; ++word) {         // drain 4 of 256 words
            out(*b, P_C, REGdata);
            out(*b, P_C, (uint8_t)(REGdata | RD));
            (void)in(*b, P_A);
            (void)in(*b, P_B);
            out(*b, P_C, REGdata);
        }

        StateWriter w;
        b->serialize(w);

        auto* b2 = makeBoard(clk, "cardA");
        StateReader r(w.data());
        b2->deserialize(r);

        // The restored board continues the very same read: the next word is word #4 of the pattern.
        out(*b2, P_C, REGdata);
        out(*b2, P_C, (uint8_t)(REGdata | RD));
        uint8_t lo = in(*b2, P_A);
        uint8_t hi = in(*b2, P_B);
        CHECK(lo == pat[8] && hi == pat[9], "the restored board resumes the read at the exact word");

        delete b;
        delete b2;
    }
}
