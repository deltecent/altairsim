// S100Computers V2 Z80 CPU board -- onboard paged monitor EEPROM
// (src/boards/v2z80.h, reference/v2-z80-cpu-board.md).
//
// Pins the two things the board really is: the D3H-controlled A12 paging of an 8K EEPROM into
// the F000-FFFF window (bit1 = low/high 4K page, bit0 = inactivate) and the RAM-under-ROM
// shadow (reads assert PHANTOM*, writes fall through, and disabling the chip vacates the
// window). The distinguishing bytes come from the two built-in images: both pages open with a
// JP at F000 (C3 ..), but F001 differs -- master0 (low) = 84, master1 (high) = 1C -- which is
// how a SECTION proves the A12 line actually switched pages rather than just claiming it did.

#include "boards/v2z80.h"
#include "core/bus.h"
#include "core/clock.h"
#include "core/value.h"
#include "test.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

constexpr uint8_t  D3       = 0xD3;    // the default page/ROM-control latch
constexpr uint16_t WIN      = 0xF000;  // EEPROM window base
constexpr uint8_t  LOW_F001 = 0x84;    // master0 F001 (JP F084)
constexpr uint8_t  HI_F001  = 0x1C;    // master1 F001 (JP F01C)

uint8_t memrd(V2Z80Board& b, uint16_t a) {
    BusCycle c;
    c.type = Cycle::MemRead;
    c.addr = a;
    return b.read(c);
}
bool decodesMemRd(V2Z80Board& b, uint16_t a) {
    BusCycle c;
    c.type = Cycle::MemRead;
    c.addr = a;
    return b.decodes(c);
}
bool phantomMemRd(V2Z80Board& b, uint16_t a) {
    BusCycle c;
    c.type = Cycle::MemRead;
    c.addr = a;
    return b.assertsPhantom(c);
}
bool decodesMemWr(V2Z80Board& b, uint16_t a) {
    BusCycle c;
    c.type = Cycle::MemWrite;
    c.addr = a;
    return b.decodes(c);
}
void outp(V2Z80Board& b, uint8_t port, uint8_t v) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    c.data = v;
    b.write(c);
}
bool decodesIoWr(V2Z80Board& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    return b.decodes(c);
}
bool decodesIoRd(V2Z80Board& b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoRead;
    c.addr = port;
    return b.decodes(c);
}

} // namespace

void test_v2z80() {
    SECTION("v2z80: at reset the low page is visible in the F000-FFFF window");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        CHECK(memrd(b, WIN) == 0xC3, "F000 opens with a JP (both pages do)");
        CHECK(memrd(b, WIN + 1) == LOW_F001, "F001 is the LOW-page byte after power/reset");
        CHECK(decodesMemRd(b, WIN), "the board decodes reads in its window");
        CHECK(!decodesMemRd(b, 0xEFFF), "and nothing below the window");
        CHECK(!decodesMemRd(b, 0x0000), "no NOP-slide / POJ decode -- the machine RUNs F000");
    }

    SECTION("v2z80: OUT D3H bit 1 switches the A12 page line, bit 0 = 0 keeps it enabled");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        outp(b, D3, 0x02);  // bit1 = 1 -> HIGH page (bit0 = 0 -> still enabled)
        CHECK(memrd(b, WIN + 1) == HI_F001, "F001 is now the HIGH-page byte");
        CHECK(memrd(b, WIN) == 0xC3, "F000 still a JP on the high page");

        outp(b, D3, 0x00);  // back to the low page
        CHECK(memrd(b, WIN + 1) == LOW_F001, "F001 is the LOW-page byte again");

        // The monitor's own ACTIVATE stubs write 06H (high) and 04H (low): bit2 is the MMU
        // overlap bit and must be ignored -- only bit1 is the page.
        outp(b, D3, 0x06);
        CHECK(memrd(b, WIN + 1) == HI_F001, "06H selects HIGH (bit2 ignored)");
        outp(b, D3, 0x04);
        CHECK(memrd(b, WIN + 1) == LOW_F001, "04H selects LOW (bit2 ignored)");
    }

    SECTION("v2z80: OUT D3H bit 0 = 1 inactivates the EEPROM -- the window vacates");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        outp(b, D3, 0x01);  // bit0 = 1 -> inactivate
        CHECK(!decodesMemRd(b, WIN), "the board no longer decodes its window");
        CHECK(!phantomMemRd(b, WIN), "and no longer shadows RAM there");
        CHECK(memrd(b, WIN) == 0xFF, "a read of the vacated window is not ours (floats FF)");
        uint8_t out = 0;
        CHECK(!b.peek(WIN, out), "peek reports the window is not ours while disabled");

        outp(b, D3, 0x00);  // re-enable
        CHECK(decodesMemRd(b, WIN), "the board decodes its window again once re-enabled");
        CHECK(memrd(b, WIN + 1) == LOW_F001, "and the low page is back");
    }

    SECTION("v2z80: RAM-under-ROM -- reads shadow, writes fall through, disable is honored");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        CHECK(phantomMemRd(b, WIN), "a READ in the window asserts PHANTOM* (ROM shadows RAM)");
        CHECK(phantomMemRd(b, 0xFFFF), "across the whole window");
        CHECK(!phantomMemRd(b, 0xEFFF), "but not below it");
        CHECK(!decodesMemWr(b, WIN), "a WRITE in the window is not ours -- it reaches the RAM");
    }

    SECTION("v2z80: the unprogrammed tail of each page reads FF");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        // master0 is programmed F000-FF18; the rest of the 4K window is erased EEPROM.
        CHECK(memrd(b, 0xFFFF) == 0xFF, "low page: FFFF is past the image, reads FF");
    }

    SECTION("v2z80: reset returns to the low page with the EEPROM enabled");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        outp(b, D3, 0x03);  // high page AND disabled
        CHECK(!decodesMemRd(b, WIN), "precondition: disabled");
        b.reset(Reset::Bus);
        CHECK(decodesMemRd(b, WIN), "reset re-enables the EEPROM");
        CHECK(memrd(b, WIN + 1) == LOW_F001, "reset selects the low page");
    }

    SECTION("v2z80: the `port` strap relocates the control latch");
    {
        Clock clk;
        V2Z80Board b;
        b.attachClock(&clk);
        b.power();

        std::string err;
        CHECK(setProperty(b, "port", "c3", err), err.c_str());

        outp(b, 0xC3, 0x02);  // the latch now answers at C3H
        CHECK(memrd(b, WIN + 1) == HI_F001, "OUT C3H,2 switched the page");
        outp(b, D3, 0x00);    // the old port D3H is no longer ours
        CHECK(memrd(b, WIN + 1) == HI_F001, "OUT D3H did nothing -- still the high page");
        CHECK(decodesIoWr(b, 0xC3), "the board decodes writes at the new port");
        CHECK(!decodesIoWr(b, D3), "and not at the old one");
        CHECK(!decodesIoRd(b, 0xC3), "D3H is write-only -- reads are not ours");
    }
}
