// S100Computers Dual SD card board (src/boards/dualsd.h, reference/dual-sd-card.md).
//
// This pins the board-level command/handshake engine as a faithful port of the ESP32
// firmware (S100_ESP32_Firmware_v1.5): the single-input-path port model (the 33H lead, the
// command code and every argument/write byte all go OUT the DATA port; the STATUS port is
// read-only status), the DI7 read handshake, the trailing STATUS byte every command returns,
// the 512-byte read/write round trip, FORMAT's sector-count + E5 fill + sector-0 guard, a
// read past the card, the two independent SD sockets, RESET, write-protect, and the strap.
//
// Addressing is CONFIRMED from the firmware (getTrkSec): SET_TRK_SEC sends the track byte then
// the sector byte, and the current sector is track*256 + sector. One SECTION pins that
// white-box on a >256-sector card; the rest drive round trips through the same addressing.

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
constexpr uint8_t STATUS = BASE + 0;   // status (IN) / flush (OUT)
constexpr uint8_t DATA   = BASE + 1;   // data + commands (IN/OUT)
constexpr size_t  SEC    = 512;

// Commands (firmware CMD_*; reference section 2).
constexpr uint8_t LEAD    = 0x33;
constexpr uint8_t cINIT_A = 0x80, cINIT_B = 0x81;
constexpr uint8_t cSEL_A  = 0x82, cSEL_B  = 0x83;
constexpr uint8_t cSETTS  = 0x84;
constexpr uint8_t cREAD   = 0x85, cWRITE  = 0x86;
constexpr uint8_t cFORMAT = 0x87, cRESET  = 0x88;
constexpr uint8_t STAT_OK = 0x00, STAT_ERR = 0x1A;

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

uint8_t statusOf(DualSdBoard& b) { return in(b, STATUS); }

// The trailing STATUS byte every command returns (firmware SendData(runCmd(...))), read back
// through the DATA port exactly like the real drivers' GET_DATA.
uint8_t result(DualSdBoard& b) { return in(b, DATA); }

// Every command is a 33H lead then the code -- BOTH written to the DATA port (the firmware's
// single input path).
void command(DualSdBoard& b, uint8_t code) {
    out(b, DATA, LEAD);
    out(b, DATA, code);
}

// A no-argument command that returns only a STATUS byte (INIT, SELECT, ...).
uint8_t simpleCmd(DualSdBoard& b, uint8_t code) {
    command(b, code);
    return result(b);
}

// Set the current sector in the firmware's argument order (getTrkSec): the TRACK byte first,
// then the SECTOR byte. The card LBA is track*256 + sector, so an LBA's high byte is the track
// and its low byte the sector. Returns the STATUS byte.
uint8_t setLba(DualSdBoard& b, uint16_t lba) {
    command(b, cSETTS);
    out(b, DATA, (uint8_t)(lba >> 8));     // track (high byte) first
    out(b, DATA, (uint8_t)(lba & 0xFF));   // sector (low byte) second
    return result(b);
}

// WRITE a sector: 512 data bytes out the DATA port, then read the STATUS byte.
uint8_t writeSector(DualSdBoard& b, const std::vector<uint8_t>& data) {
    command(b, cWRITE);
    for (uint8_t byte : data) out(b, DATA, byte);
    return result(b);
}

// READ a sector: 512 data bytes, then the trailing STATUS byte. `status` (if given) receives it.
std::vector<uint8_t> readSector(DualSdBoard& b, uint8_t* status = nullptr) {
    command(b, cREAD);
    std::vector<uint8_t> v;
    v.reserve(SEC);
    for (size_t i = 0; i < SEC; ++i) v.push_back(in(b, DATA));
    uint8_t st = result(b);
    if (status) *status = st;
    return v;
}

// FORMAT `count` sectors from the current sector: a 16-bit count (LSB, MSB), then STATUS.
uint8_t formatSectors(DualSdBoard& b, uint16_t count) {
    command(b, cFORMAT);
    out(b, DATA, (uint8_t)(count & 0xFF));   // LSB first
    out(b, DATA, (uint8_t)(count >> 8));     // MSB second
    return result(b);
}

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
    SECTION("dualsd: write a sector, it lands on the medium and reads back, with STATUS=OK");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        CHECK(simpleCmd(*b, cINIT_A) == STAT_OK, "INIT drive A returns STATUS OK (a card is present)");
        auto pat = pattern(0x11);
        CHECK(setLba(*b, 5) == STAT_OK, "SET_TRK_SEC returns STATUS OK");
        CHECK(writeSector(*b, pat) == STAT_OK, "WRITE returns STATUS OK");

        CHECK(mediaHas(g_cards[0], 5, pat), "the sector landed at LBA 5 * 512 on the medium");
        CHECK(g_cards[0]->syncs() > 0, "WRITE synced the medium");

        setLba(*b, 5);
        uint8_t st = 0xFF;
        CHECK(readSector(*b, &st) == pat, "read LBA 5 returns what was written");
        CHECK(st == STAT_OK, "READ returns STATUS OK");

        delete b;
    }

    SECTION("dualsd: the DI7 read handshake asserts through the data + STATUS, then clears");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        setLba(*b, 2);
        writeSector(*b, pattern(0x22));

        setLba(*b, 2);
        command(*b, cREAD);
        CHECK((statusOf(*b) & 0x80) != 0, "DI7 set right after READ -- a byte is waiting");
        for (int i = 0; i < 512; ++i) (void)in(*b, DATA);          // drain the 512 data bytes
        CHECK((statusOf(*b) & 0x80) != 0, "DI7 still set -- the trailing STATUS byte is waiting");
        CHECK(in(*b, DATA) == STAT_OK, "the trailing byte is STATUS OK");
        CHECK((statusOf(*b) & 0x80) == 0, "DI7 clears once data + STATUS are drained");
        CHECK(in(*b, DATA) == 0xFF, "a DATA read past the reply floats 0xFF");

        delete b;
    }

    SECTION("dualsd: FORMAT fills count sectors with E5 and returns STATUS OK");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        setLba(*b, 3);
        CHECK(formatSectors(*b, 1) == STAT_OK, "FORMAT of 1 sector returns STATUS OK");

        std::vector<uint8_t> e5(SEC, 0xE5);
        CHECK(mediaHas(g_cards[0], 3, e5), "FORMAT wrote 512 bytes of E5 to LBA 3");
        setLba(*b, 3);
        CHECK(readSector(*b) == e5, "the formatted sector reads back all E5");

        delete b;
    }

    SECTION("dualsd: FORMAT never touches sector 0 (the boot sector), but advances past it");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        // Format 3 sectors starting at LBA 0: sector 0 must be left untouched, 1 and 2 filled.
        setLba(*b, 0);
        CHECK(formatSectors(*b, 3) == STAT_OK, "FORMAT from LBA 0 still returns OK");

        std::vector<uint8_t> zeros(SEC, 0x00), e5(SEC, 0xE5);
        CHECK(mediaHas(g_cards[0], 0, zeros), "sector 0 is NOT formatted (still zeros)");
        CHECK(mediaHas(g_cards[0], 1, e5),    "sector 1 is E5");
        CHECK(mediaHas(g_cards[0], 2, e5),    "sector 2 is E5");

        delete b;
    }

    SECTION("dualsd: a read past the card returns 512 zeros and STATUS ERR");
    {
        installCards(16);   // a 16-sector card; LBA 100 is past its end
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        setLba(*b, 100);
        uint8_t st = STAT_OK;
        std::vector<uint8_t> zeros(SEC, 0x00);
        CHECK(readSector(*b, &st) == zeros, "a read past the card returns 512 zeros (firmware)");
        CHECK(st == STAT_ERR, "and STATUS ERR");

        delete b;
    }

    SECTION("dualsd: the two SD sockets are independent");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA", "cardB");

        auto patA = pattern(0xA0);
        auto patB = pattern(0x0B);

        simpleCmd(*b, cINIT_A); setLba(*b, 2); writeSector(*b, patA);
        simpleCmd(*b, cINIT_B); setLba(*b, 2); writeSector(*b, patB);

        CHECK(mediaHas(g_cards[0], 2, patA), "drive A medium holds pattern A");
        CHECK(mediaHas(g_cards[1], 2, patB), "drive B medium holds pattern B");

        CHECK(simpleCmd(*b, cSEL_A) == STAT_OK, "SELECT A returns OK");
        setLba(*b, 2);
        CHECK(readSector(*b) == patA, "SELECT A reads back A");
        CHECK(simpleCmd(*b, cSEL_B) == STAT_OK, "SELECT B returns OK");
        setLba(*b, 2);
        CHECK(readSector(*b) == patB, "SELECT B reads back B");

        delete b;
    }

    SECTION("dualsd: RESET reboots the engine and returns the current sector to 0");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        // Prime the engine: select LBA 7. (A command mid-collect cannot be interrupted -- the
        // firmware's single input path would swallow the RESET as sector data -- so RESET is a
        // between-commands operation, exactly as on the real board.)
        setLba(*b, 7);
        command(*b, cRESET);   // RESET reboots the ESP32 and returns no STATUS

        CHECK(statusOf(*b) == 0, "after RESET the status is idle (no data-ready)");
        CHECK(in(*b, DATA) == 0xFF, "after RESET a DATA read floats -- no reply in flight");

        // RESET returned the current sector to LBA 0: a bare READ now reads sector 0 (zeros here).
        std::vector<uint8_t> zeros(SEC, 0x00);
        uint8_t st = 0xFF;
        CHECK(readSector(*b, &st) == zeros, "RESET cleared the current sector to 0");
        CHECK(st == STAT_OK, "and that read succeeds");

        delete b;
    }

    SECTION("dualsd: a write-protected card fails the write with STATUS ERR and says so");
    {
        installCards(16);
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA", nullptr, /*roA=*/true);

        setLba(*b, 4);
        CHECK(writeSector(*b, pattern(0xEE)) == STAT_ERR, "the write returns STATUS ERR");

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

        // A round trip at the new base still works. Everything (33H, command, arguments) goes
        // out the new DATA port 91; STATUS is read from the new base 90. SET_TRK_SEC is track
        // (00) then sector (06) -> LBA 6.
        auto pat = pattern(0x5A);
        out(*b, 0x91, LEAD); out(*b, 0x91, cSETTS); out(*b, 0x91, 0); out(*b, 0x91, 6);
        CHECK(in(*b, 0x91) == STAT_OK, "SET_TRK_SEC STATUS OK at the relocated ports");
        out(*b, 0x91, LEAD); out(*b, 0x91, cWRITE);
        for (uint8_t byte : pat) out(*b, 0x91, byte);
        CHECK(in(*b, 0x91) == STAT_OK, "WRITE STATUS OK at the relocated ports");
        CHECK(mediaHas(g_cards[0], 6, pat), "write works at the relocated data port");

        delete b;
    }

    SECTION("dualsd: SET_TRK_SEC maps track then sector to LBA = track*256 + sector");
    {
        // The confirmed mapping (firmware getTrkSec): the address bytes are track then sector,
        // and the LBA is track*256 + sector. A card larger than one track (>256 sectors) is the
        // only way to tell that mapping apart from a flat low/high 16-bit number.
        installCards(600);   // spans track 0 (LBA 0..255), track 1 (256..), track 2 (512..)
        Clock clk;
        DualSdBoard* b = makeBoard(clk, "cardA");

        // Send the address bytes by hand so the test states the byte order outright: 84H,
        // then TRACK, then SECTOR, then the STATUS byte.
        auto setTrkSec = [&](uint8_t trk, uint8_t sec) {
            command(*b, cSETTS);
            out(*b, DATA, trk);
            out(*b, DATA, sec);
            return result(*b);
        };

        // Track 1, sector 2 -> LBA 1*256 + 2 = 258.
        auto pat = pattern(0x7C);
        CHECK(setTrkSec(1, 2) == STAT_OK, "SET_TRK_SEC(1,2) STATUS OK");
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
