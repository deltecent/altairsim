#include "test.h"

#include "boards/cromemco-dazzler.h"
#include "boards/cromemco-dazzler2.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/display_null.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

// A machine with a Dazzler II and RAM only below 0x8000, so the picture at 0x8000 has NO
// main memory under it -- the case the original card cannot show. A NullDisplay is wired
// in the same way the Dazzler's own tests do it.
struct Rig2 {
    Machine        m;
    NullDisplay    disp;
    Dazzler2Board* daz = nullptr;
    MemoryBoard*   mem = nullptr;

    explicit Rig2(bool twoCards = false) {
        std::string err;
        m.bus.setVerify(true);

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x8000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        daz = dynamic_cast<Dazzler2Board*>(m.add("dazzler2", "daz0", err));
        (void)twoCards;
        DazzlerBoard::setDisplay(&disp);
        m.power();
    }

    void    ctrl(uint8_t v) { m.bus.ioWrite(0x0E, v); }
    void    fmt(uint8_t v) { m.bus.ioWrite(0x0F, v); }
    void    on(uint16_t base) { ctrl((uint8_t)(0x80 | (base >> 9))); }
    void    wr(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }
    uint8_t status() { return m.bus.ioRead(0x0E); }
    uint8_t px(int x, int y) {
        const Surface* s = disp.surface();
        return s->pixels()[(size_t)y * s->width() + x];
    }
};

} // namespace

void test_dazzler2() {
    SECTION("Dazzler II -- a second board type, with the Dazzler's ports and no memory decode");
    {
        Rig2 g;
        CHECK(g.daz->type() == "dazzler2", "the type is dazzler2");
        CHECK(g.daz->wantsSnoop(), "it watches memory writes -- it does not answer them");
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = 0x0E;
        CHECK(g.daz->decodes(c), "it decodes the control port (0x0E)");
        c.addr = 0x0F;
        CHECK(g.daz->decodes(c), "...and the format port (0x0F)");
        c.type = Cycle::MemWrite;
        c.addr = 0x8000;
        CHECK(!g.daz->decodes(c), "it answers no memory cycle: main memory still takes the write");
        CHECK(!g.daz->requestsBus(), "and it never takes the bus");
    }

    SECTION("Dazzler II -- a write 0000-0FFF above the base lands in card RAM, no other");
    {
        Rig2 g;
        g.on(0x8000);
        g.wr(0x8000, 0x11);
        g.wr(0x8FFF, 0x22);
        g.wr(0x9000, 0x33);   // one past the window
        g.wr(0x7FFF, 0x44);   // one below the base -- must not wrap into the window
        CHECK(g.daz->cardRam(0x000) == 0x11, "base+0000 is captured");
        CHECK(g.daz->cardRam(0xFFF) == 0x22, "base+0FFF is captured");
        CHECK(g.daz->cardRam(0x000) != 0x33 && g.daz->cardRam(0xFFF) != 0x33,
              "base+1000 is outside the window");
        CHECK(g.daz->cardRam(0xFFF) != 0x44, "an address below the base does not wrap into it");
    }

    SECTION("Dazzler II -- the window is the base set by OUT 0E, and the display need not be on");
    {
        Rig2 g;
        g.ctrl(0x10);          // base 0x2000, display OFF
        g.wr(0x2005, 0x5A);
        CHECK(g.daz->cardRam(5) == 0x5A, "a write is captured with the display off");
        g.on(0x4000);
        g.wr(0x4005, 0xA5);
        CHECK(g.daz->cardRam(5) == 0xA5, "moving the base moves the window");

        g.on(0xF800);          // the top of the address space: the window ends at FFFF
        g.wr(0xFFFF, 0x6B);
        g.wr(0x0005, 0x9C);    // would be offset 0805 if the subtraction wrapped at 64K
        CHECK(g.daz->cardRam(0x7FF) == 0x6B, "the last address is the last byte of the window");
        CHECK(g.daz->cardRam(0x805) != 0x9C, "the window does not wrap round past FFFF");
    }

    SECTION("Dazzler II -- a picture drawn before OUT 0E shows the card's undefined contents");
    {
        Rig2 g;
        g.fmt(0x10);                       // normal, 512 B, color
        for (int i = 0; i < 512; ++i) g.wr((uint16_t)(0x8000 + i), 0xA5);   // base is not set yet
        g.on(0x8000);
        g.daz->pump();
        bool any = false;
        for (int i = 0; i < 16; ++i)
            if (g.daz->cardRam((uint16_t)i) != 0xA5) any = true;
        CHECK(any, "the card RAM is not the picture the guest drew first");
        CHECK(g.px(0, 0) == (g.daz->cardRam(0) & 0x0F),
              "the screen shows the card RAM, not what the guest wrote");

        Rig2 h;   // the same seed gives the same undefined contents
        CHECK(h.daz->cardRam(0) == g.daz->cardRam(0) && h.daz->cardRam(77) == g.daz->cardRam(77),
              "the seed makes a power repeatable");
    }

    SECTION("Dazzler II -- the picture shows from an address with no main memory under it");
    {
        Rig2 g;
        uint8_t v = g.m.bus.memRead(0x8000);
        (void)v;   // nothing there: the bus floats
        g.fmt(0x10);
        g.on(0x8000);
        g.wr(0x8000, 0x21);
        g.daz->pump();
        CHECK(g.px(0, 0) == 1 && g.px(1, 0) == 2, "a byte written to nowhere is on the screen");
    }

    SECTION("Dazzler II -- reads stay in main memory; the card is not read back");
    {
        Rig2 g;
        g.fmt(0x10);
        g.on(0x2000);
        g.wr(0x2000, 0x21);
        CHECK(g.m.bus.memRead(0x2000) == 0x21, "a read returns main memory");
        g.mem->poke(0x2000, 0x77);   // main memory changes without a bus write
        CHECK(g.m.bus.memRead(0x2000) == 0x77, "a read still comes from main memory");
        g.daz->pump();
        CHECK(g.px(0, 0) == 1 && g.px(1, 0) == 2, "but the picture is what the card captured");
    }

    SECTION("Dazzler II -- page shows the first or the second 2 KB of the card RAM");
    {
        Rig2 g;
        std::string err;
        g.fmt(0x30);                       // 2 KB picture
        g.on(0x8000);
        g.wr(0x8000, 0x01);                // first half, byte 0
        g.wr(0x8800, 0x02);                // second half, byte 0
        g.daz->pump();
        CHECK(g.px(0, 0) == 1, "page 0 shows the first half");
        CHECK(setProperty(*g.daz, "page", "800", err), "page 800 is accepted");
        g.daz->pump();
        CHECK(g.px(0, 0) == 2, "page 800 shows the half at +800H, and repaints with no write");
        CHECK(!setProperty(*g.daz, "page", "400", err), "any other page is refused");
    }

    SECTION("Dazzler II -- IN BASE drives D5-D0 low; the Dazzler floats them high");
    {
        Rig2 g;
        CHECK(g.status() == 0xC0, "time 0: line 0 even (D7), not vblank (D6), D5-D0 low");
        g.m.clock.advance(128);
        CHECK(g.status() == 0x40, "the next line is odd -- D7 clears");
        g.m.clock.advance(25333 - 128);
        CHECK(g.status() == 0x00, "in vblank D6 and D7 are low: the port reads 00, not 3F");

        Machine m;
        std::string err;
        m.add("dazzler", "d", err);
        m.power();
        CHECK(m.bus.ioRead(0x0E) == 0xFF, "the original still reads FF at time 0");
    }

    SECTION("Dazzler II -- RESET keeps the card RAM; POWER refills it; the snapshot carries it");
    {
        Rig2 g;
        g.on(0x8000);
        g.wr(0x8010, 0xC3);
        g.m.reset(Reset::Bus);
        CHECK(g.daz->cardRam(0x10) == 0xC3, "RESET does not clear the card RAM");

        StateWriter w;
        g.daz->serialize(w);
        Rig2 h;
        StateReader r(w.data());
        h.daz->deserialize(r);
        CHECK(r.ok(), "restore consumed the blob cleanly");
        CHECK(h.daz->cardRam(0x10) == 0xC3, "the card RAM travelled");
        CHECK(h.daz->base() == 0x8000, "so did the base");

        std::string err;
        setProperty(*g.daz, "seed", "7", err);
        g.m.power();
        CHECK(g.daz->cardRam(0x10) != 0xC3 || g.daz->cardRam(0x11) != h.daz->cardRam(0x11),
              "POWER refills it from the seed");
    }
}
