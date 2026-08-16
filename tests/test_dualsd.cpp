// S100Computers Dual SD card board (src/boards/dualsd.h, reference/dual-sd-card.md).
//
// This pins the board-level command/handshake engine: the two-port (80/81) protocol with
// its 33H command lead, the 512-byte sector write/read round trip, the DI7 read handshake,
// FORMAT's E5 fill, the erased-read fill an unreadable block returns, the two independent
// SD sockets, RESET, write-protect, and the port strap.
//
// The (track,sector)->LBA mapping is CONFIRMED from SD_CARD.Z80: SET_TRK_SEC sends the
// track byte then the sector byte, and the card LBA is track*256 + sector (dualsd.h,
// decodeAddr). The last SECTION pins that mapping white-box; the rest drive round trips
// through the same addressing, locking the engine mechanics.

#include "boards/dualsd.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/value.h"
#include "host/media.h"
#include "test.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint8_t BASE   = 0x80;
constexpr uint8_t STATUS = BASE + 0;   // status (IN) / command (OUT)
constexpr uint8_t DATA   = BASE + 1;   // data (IN/OUT)
constexpr size_t  SEC    = 512;

// Commands (reference section 2).
constexpr uint8_t LEAD    = 0x33;
constexpr uint8_t cINIT_A = 0x80, cINIT_B = 0x81;
constexpr uint8_t cSEL_A  = 0x82, cSEL_B  = 0x83;
constexpr uint8_t cSETTS  = 0x84;
constexpr uint8_t cREAD   = 0x85, cWRITE  = 0x86;
constexpr uint8_t cFORMAT = 0x87, cRESET  = 0x88;

// Raw pointers to the media the resolver hands the board. The BOARD owns them; these are
// for white-box asserts (the bytes that landed, the sync count) while the board is alive.
// File-scope so the installed resolver never dangles a reference into a dead SECTION.
std::vector<MemoryMedia*> g_cards;
uint32_t                  g_cardSectors = 16;

void installCards(uint32_t sectors) {
    g_cards.clear();
    g_cardSectors = sectors;
    setMediaResolver([](const std::string& path, bool ro, std::string&) {
        std::vector<uint8_t> d((size_t)g_cardSectors * SEC, 0x00);
        auto m = std::make_unique<MemoryMedia>(path, std::move(d), ro);
        g_cards.push_back(m.get());
        return std::unique_ptr<MediaFile>(std::move(m));
    });
}

uint8_t in(DualSdBoard& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.read(c);
}
void out(DualSdBoard& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}

// Every command is a 33H lead then the code.
void cmd(DualSdBoard& b, uint8_t code) {
    out(b, STATUS, LEAD);
    out(b, STATUS, code);
}

// Set the current sector, in the firmware's argument order (SD_CARD.Z80 SET_SECTOR): the
// TRACK byte first, then the SECTOR byte. The card LBA is track*256 + sector, so an LBA's
// high byte is the track and its low byte the sector.
void setLba(DualSdBoard& b, uint16_t lba) {
    cmd(b, cSETTS);
    out(b, DATA, (uint8_t)(lba >> 8));     // track (high byte) first
    out(b, DATA, (uint8_t)(lba & 0xFF));   // sector (low byte) second
}

void writeSector(DualSdBoard& b, const std::vector<uint8_t>& data) {
    cmd(b, cWRITE);
    for (uint8_t byte : data) out(b, DATA, byte);
}

std::vector<uint8_t> readSector(DualSdBoard& b) {
    cmd(b, cREAD);
    std::vector<uint8_t> v;
    v.reserve(SEC);
    for (size_t i = 0; i < SEC; ++i) v.push_back(in(b, DATA));
    return v;
}

uint8_t statusOf(DualSdBoard& b) { return in(b, STATUS); }

// A deterministic, position-unique sector so no two look alike.
std::vector<uint8_t> pattern(uint8_t salt) {
    std::vector<uint8_t> v(SEC);
    for (size_t i = 0; i < SEC; ++i) v[i] = (uint8_t)((i * 37u + salt * 101u + 7u) & 0xFF);
    return v;
}

DualSdBoard* makeBoard(Clock& clk, const char* mountA, const char* mountB = nullptr,
                       bool roA = false) {
    auto* b = new DualSdBoard();
    b->id = "sd0";
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

void test_dualsd() {
    SECTION("dualsd: write a sector, it lands on the medium and reads back");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        cmd(*b, cINIT_A);
        auto pat = pattern(0x11);
        setLba(*b, 5);
        writeSector(*b, pat);

        CHECK(mediaHas(g_cards[0], 5, pat), "the sector landed at LBA 5 * 512 on the medium");
        CHECK(g_cards[0]->syncs() > 0, "WRITE synced the medium");

        setLba(*b, 5);
        CHECK(readSector(*b) == pat, "read LBA 5 returns what was written");

        delete b;
    }

    SECTION("dualsd: the DI7 read handshake asserts while data is waiting, then clears");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        setLba(*b, 2);
        writeSector(*b, pattern(0x22));

        setLba(*b, 2);
        cmd(*b, cREAD);
        CHECK((statusOf(*b) & 0x80) != 0, "DI7 set right after READ -- a byte is waiting");
        for (int i = 0; i < 256; ++i) (void)in(*b, DATA);
        CHECK((statusOf(*b) & 0x80) != 0, "DI7 still set mid-sector");
        for (int i = 0; i < 256; ++i) (void)in(*b, DATA);   // drain the rest (512 total)
        CHECK((statusOf(*b) & 0x80) == 0, "DI7 clears once the sector is drained");
        CHECK(in(*b, DATA) == 0xFF, "a DATA read past the sector floats 0xFF");

        delete b;
    }

    SECTION("dualsd: FORMAT fills the current sector with E5");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        setLba(*b, 3);
        cmd(*b, cFORMAT);

        std::vector<uint8_t> e5(SEC, 0xE5);
        CHECK(mediaHas(g_cards[0], 3, e5), "FORMAT wrote 512 bytes of E5 to LBA 3");
        setLba(*b, 3);
        CHECK(readSector(*b) == e5, "the formatted sector reads back all E5");

        delete b;
    }

    SECTION("dualsd: an unreadable block reads the erased-card fill (FF), not zeros");
    {
        installCards(16);   // a 16-sector card; LBA 100 is past its end
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        setLba(*b, 100);
        std::vector<uint8_t> ff(SEC, 0xFF);
        CHECK(readSector(*b) == ff, "a read past the card returns the erased byte (FF)");

        delete b;
    }

    SECTION("dualsd: the two SD sockets are independent");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA", "cardB");

        auto patA = pattern(0xA0);
        auto patB = pattern(0x0B);

        cmd(*b, cINIT_A); setLba(*b, 2); writeSector(*b, patA);
        cmd(*b, cINIT_B); setLba(*b, 2); writeSector(*b, patB);

        CHECK(mediaHas(g_cards[0], 2, patA), "drive A medium holds pattern A");
        CHECK(mediaHas(g_cards[1], 2, patB), "drive B medium holds pattern B");

        cmd(*b, cSEL_A); setLba(*b, 2);
        CHECK(readSector(*b) == patA, "SELECT A reads back A");
        cmd(*b, cSEL_B); setLba(*b, 2);
        CHECK(readSector(*b) == patB, "SELECT B reads back B");

        delete b;
    }

    SECTION("dualsd: RESET clears the engine mid-transaction");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        // Start a write to LBA 7 but only send part of the sector, then RESET.
        setLba(*b, 7);
        cmd(*b, cWRITE);
        for (int i = 0; i < 100; ++i) out(*b, DATA, 0xAB);
        cmd(*b, cRESET);

        CHECK(statusOf(*b) == 0, "after RESET the status is idle (no data-ready)");
        CHECK(in(*b, DATA) == 0xFF, "after RESET a DATA read floats -- no transfer in flight");

        // RESET returned the address to LBA 0; a bare READ now reads sector 0 (all zeros here),
        // proving the aborted LBA-7 write never happened and lba_ was cleared.
        std::vector<uint8_t> zeros(SEC, 0x00);
        CHECK(readSector(*b) == zeros, "RESET cleared LBA to 0 and dropped the partial write");

        delete b;
    }

    SECTION("dualsd: a write-protected card drops the write and says so");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA", nullptr, /*roA=*/true);

        setLba(*b, 4);
        writeSector(*b, pattern(0xEE));

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

    SECTION("dualsd: the port strap relocates both ports");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        std::string err;
        CHECK(setProperty(*b, "port", "90", err), err.c_str());
        b->configChanged();

        auto decodesIo = [&](uint8_t port) {
            BusCycle c; c.type = Cycle::IoWrite; c.addr = port; c.data = 0;
            return b->decodes(c);
        };
        CHECK(decodesIo(0x90) && decodesIo(0x91), "decodes the new base 90/91");
        CHECK(!decodesIo(0x80) && !decodesIo(0x81), "no longer decodes the old base 80/81");

        // A round trip at the new base still works. SET_TRK_SEC is track (00) then sector (06)
        // -> LBA 6.
        auto pat = pattern(0x5A);
        out(*b, 0x90, LEAD); out(*b, 0x90, cSETTS); out(*b, 0x91, 0); out(*b, 0x91, 6);
        out(*b, 0x90, LEAD); out(*b, 0x90, cWRITE);
        for (uint8_t byte : pat) out(*b, 0x91, byte);
        CHECK(mediaHas(g_cards[0], 6, pat), "write works at the relocated data port");

        delete b;
    }

    SECTION("dualsd: SET_TRK_SEC maps track then sector to LBA = track*256 + sector");
    {
        // The confirmed mapping (SD_CARD.Z80): the address bytes are track then sector, and
        // the LBA is track*256 + sector. A card larger than one track (>256 sectors) is the
        // only way to tell that mapping apart from a flat low/high 16-bit number.
        installCards(600);   // spans track 0 (LBA 0..255), track 1 (256..), track 2 (512..)
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        // Send the address bytes by hand so the test states the byte order outright: 84H,
        // then TRACK, then SECTOR.
        auto setTrkSec = [&](uint8_t trk, uint8_t sec) {
            cmd(*b, cSETTS);
            out(*b, DATA, trk);
            out(*b, DATA, sec);
        };

        // Track 1, sector 2 -> LBA 1*256 + 2 = 258.
        auto pat = pattern(0x7C);
        setTrkSec(1, 2);
        writeSector(*b, pat);
        CHECK(mediaHas(g_cards[0], 258, pat), "track 1 / sector 2 lands at LBA 258");

        // Read it back by the LBA the mapping predicts (setLba decomposes LBA->track,sector).
        setLba(*b, 258);
        CHECK(readSector(*b) == pat, "LBA 258 reads back track 1 / sector 2");

        // And track 0 / sector 5 is plain LBA 5 -- the low byte alone when track is 0.
        auto pat0 = pattern(0x05);
        setTrkSec(0, 5);
        writeSector(*b, pat0);
        CHECK(mediaHas(g_cards[0], 5, pat0), "track 0 / sector 5 lands at LBA 5");

        delete b;
    }
}
