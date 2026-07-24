#include "test.h"

#include "boards/cromemco-dazzler.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/display_null.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

// A machine with a Dazzler, 64K of RAM for its framebuffer to live in, and a
// NullDisplay wired to it -- the SAME injection main() does (DazzlerBoard::setDisplay),
// one backend down. So the card renders into memory and the test reads the pixels back,
// with no window: exactly how a headless CI build proves a graphics card (DESIGN.md
// 7.4). The RAM is filled with zero so an un-poked framebuffer byte is a known black.
struct Rig {
    Machine       m;
    NullDisplay   disp;
    DazzlerBoard* daz = nullptr;
    MemoryBoard*  mem = nullptr;

    // Framebuffer base -- 512-byte aligned, so 0x2000 -> control byte 0x80 | 0x10.
    static constexpr uint16_t kBase = 0x2000;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        daz = dynamic_cast<DazzlerBoard*>(m.add("dazzler", "daz0", err));
        DazzlerBoard::setDisplay(&disp);
        m.power();
    }

    void    ctrl(uint8_t v) { m.bus.ioWrite(0x0E, v); }
    void    fmt(uint8_t v) { m.bus.ioWrite(0x0F, v); }
    void    on(uint16_t base) { ctrl((uint8_t)(0x80 | (base >> 9))); }
    void    poke(uint16_t off, uint8_t v) { m.bus.memWrite((uint16_t)(kBase + off), v); }
    uint8_t status() { return m.bus.ioRead(0x0E); }

    // One rendered element, read straight out of the NullDisplay's surface (the pixel
    // value IS the palette index the board put() there).
    uint8_t px(int x, int y) {
        const Surface* s = disp.surface();
        return s->pixels()[(size_t)y * s->width() + x];
    }
    int side() { return disp.surface() ? disp.surface()->width() : 0; }
};

} // namespace

void test_dazzler() {
    SECTION("Dazzler -- two I/O ports, and no memory of its own");
    {
        Rig g;
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = 0x0E;
        CHECK(g.daz->decodes(c), "decodes its control port (0x0E)");
        c.addr = 0x0F;
        CHECK(g.daz->decodes(c), "...and its format port (0x0F)");
        c.addr = 0x0D;
        CHECK(!g.daz->decodes(c), "but not the port below");
        c.addr = 0x10;
        CHECK(!g.daz->decodes(c), "nor the port above");
        c.type = Cycle::MemWrite;
        c.addr = 0x2000;
        CHECK(!g.daz->decodes(c), "the framebuffer is RAM -- the Dazzler decodes no memory");
    }

    SECTION("Dazzler -- the control and format ports latch what the guest wrote");
    {
        Rig g;
        g.on(0x2000);
        CHECK(g.daz->on(), "control D7 turns the card on");
        CHECK(g.daz->base() == 0x2000, "control D6-D0 set the framebuffer base (A15-A9)");
        g.fmt(0x30);
        CHECK(g.daz->format() == 0x30, "format latches the whole byte");
        g.ctrl(0x10);  // D7 clear
        CHECK(!g.daz->on(), "control D7 clear turns it off");
        CHECK(g.daz->base() == 0x2000, "and still carries the base bits");
    }

    SECTION("Dazzler -- normal mode: one byte is two nibble-elements, low left, high right");
    {
        Rig g;
        g.fmt(0x10);      // normal, 512 B, color -> 32x32
        g.on(0x2000);
        g.poke(0, 0x21);  // low nibble 1 (red), high nibble 2 (green)
        g.daz->pump();
        CHECK(g.side() == 32, "512-byte normal picture is 32x32 elements");
        CHECK(g.px(0, 0) == 1, "low nibble is the left element");
        CHECK(g.px(1, 0) == 2, "high nibble is the element to its right");

        g.poke(16, 0x0F);  // byte row 1, col 0
        g.daz->pump();
        CHECK(g.px(0, 1) == 0x0F, "16 bytes per row -- byte 16 is the start of element row 1");
        CHECK(g.px(2, 0) == 0, "and only where it was written");
    }

    SECTION("Dazzler -- a 2 KB picture is four 512-byte quadrants tiled 2x2");
    {
        Rig g;
        g.fmt(0x30);  // normal, 2 KB, color -> 64x64
        g.on(0x2000);
        g.poke(0, 0x01);     // Q0 byte 0    -> top-left
        g.poke(512, 0x01);   // Q1 byte 0    -> top-right
        g.poke(1024, 0x01);  // Q2 byte 0    -> bottom-left
        g.poke(1536, 0x01);  // Q3 byte 0    -> bottom-right
        g.daz->pump();
        CHECK(g.side() == 64, "2 KB normal picture is 64x64 elements");
        CHECK(g.px(0, 0) == 1, "quadrant 0 is the top-left 32x32 block");
        CHECK(g.px(32, 0) == 1, "quadrant 1 is the top-right");
        CHECK(g.px(0, 32) == 1, "quadrant 2 is the bottom-left");
        CHECK(g.px(32, 32) == 1, "quadrant 3 is the bottom-right");
    }

    SECTION("Dazzler -- X4 mode: one byte is eight on/off bits in a 4x2 cell");
    {
        Rig g;
        g.fmt(0x5F);  // X4 (D6), 512 B, color (D4), whole-picture color nibble = 0x0F
        g.on(0x2000);
        g.poke(0, 0x01);  // only D0 -> top-left of the cell
        g.daz->pump();
        CHECK(g.side() == 64, "512-byte X4 picture is 64x64 elements");
        CHECK(g.px(0, 0) == 0x0F, "a set bit lights its element in the format nibble's color");
        CHECK(g.px(1, 0) == 0, "and a clear bit stays black");

        g.poke(0, 0xFF);  // all eight bits
        g.daz->pump();
        CHECK(g.px(0, 0) == 0x0F && g.px(1, 0) == 0x0F && g.px(2, 0) == 0x0F && g.px(3, 0) == 0x0F,
              "the top row of the 4x2 cell (D0,D1,D4,D5) all light");
        CHECK(g.px(0, 1) == 0x0F && g.px(1, 1) == 0x0F && g.px(2, 1) == 0x0F && g.px(3, 1) == 0x0F,
              "and the bottom row (D2,D3,D6,D7)");

        // The exact bit->position map (reference 4.2): D4 is the top-left of the RIGHT 2x2.
        g.poke(0, 0x10);
        g.daz->pump();
        CHECK(g.px(2, 0) == 0x0F && g.px(0, 0) == 0, "D4 lights column 2, row 0 -- and nothing else");
    }

    SECTION("Dazzler -- the palette is color or 16 greys, by format D4");
    {
        Rig g;
        g.fmt(0x10);  // color
        g.on(0x2000);
        g.daz->pump();
        CHECK(g.disp.palette().size() == 16, "sixteen palette entries");
        Color red = g.disp.palette()[1];  // element 1 = red primary
        CHECK(red.r > 0 && red.g == 0 && red.b == 0, "index 1 is red in color mode");

        g.fmt(0x00);  // black-and-white
        g.poke(0, 0x11);  // force a repaint
        g.daz->pump();
        Color grey = g.disp.palette()[1];
        CHECK(grey.r == grey.g && grey.g == grey.b, "index 1 is a neutral grey in B&W mode");
        CHECK(g.disp.palette()[15].r > g.disp.palette()[1].r, "and the ramp climbs to white");
    }

    SECTION("Dazzler -- status (IN): ODD/EVEN line (D7) and END OF FRAME (D6), off the Clock");
    {
        Rig g;
        uint8_t s0 = g.status();  // emulated time = 0
        CHECK((s0 & 0x80) != 0, "line 0 is even -- D7 set");
        CHECK((s0 & 0x40) != 0, "the start of a frame is not vblank -- D6 set");

        g.m.clock.advance(128);  // one scan line
        CHECK((g.status() & 0x80) == 0, "the next scan line is odd -- D7 clears");

        g.m.clock.advance(25333 - 128);  // into the ~4 ms end-of-frame window
        CHECK((g.status() & 0x40) == 0, "during vblank D6 is 0");
    }

    SECTION("Dazzler -- an unchanged framebuffer is not repainted");
    {
        Rig g;
        g.fmt(0x30);
        g.on(0x2000);
        g.daz->pump();
        const uint64_t first = g.disp.frames();
        CHECK(first >= 1, "the first pump paints -- a fresh card owes one frame");

        g.daz->pump();
        g.daz->pump();
        CHECK(g.disp.frames() == first, "nothing in RAM changed, so nothing was repainted");

        g.poke(0, 0xAA);
        g.daz->pump();
        CHECK(g.disp.frames() == first + 1, "a write to the framebuffer earns exactly one frame");

        g.poke(0, 0xAA);  // same byte
        g.daz->pump();
        CHECK(g.disp.frames() == first + 1, "rewriting the identical byte paints nothing");

        g.poke(0, 0xBB);
        g.daz->pump();
        CHECK(g.disp.frames() == first + 2, "a different byte does");
    }

    SECTION("Dazzler -- turning it off blanks the picture");
    {
        Rig g;
        g.fmt(0x30);
        g.on(0x2000);
        g.poke(0, 0x0F);
        g.daz->pump();
        CHECK(g.px(0, 0) == 0x0F, "with the card on, the pixel is lit");

        g.ctrl(0x00);  // off
        g.daz->pump();
        CHECK(g.px(0, 0) == 0, "with the card off, the screen is black");
    }

    SECTION("Dazzler -- the port strap is validated to a buildable block");
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.daz, "port", "0F", err), "an odd BASE is refused (it owns BASE+1)");
        CHECK(setProperty(*g.daz, "port", "10", err), "an even BASE is taken");
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = 0x11;
        CHECK(g.daz->decodes(c), "and the card now decodes BASE+1 there");
    }
}
