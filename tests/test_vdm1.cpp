#include "test.h"

#include "boards/proctech-vdm1.h"
#include "core/machine.h"
#include "host/display_null.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

// A machine with a VDM-1 in it and a NullDisplay wired to it -- the SAME injection
// main() does (VdmBoard::setDisplay), one backend down. So the card renders into
// memory and the test reads the pixels back, with no window: exactly how a headless
// CI build proves a graphics card (DESIGN.md 7.4). No memory board, so nothing else
// decodes the 0xCC00 page -- setVerify(true) would flag a clash if it did.
struct Rig {
    Machine     m;
    NullDisplay disp;
    VdmBoard*   vdm = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        vdm = dynamic_cast<VdmBoard*>(m.add("vdm1", "vdm0", err));
        VdmBoard::setDisplay(&disp);
        m.power();
    }

    void    poke(uint16_t off, uint8_t v) { m.bus.memWrite(0xCC00 + off, v); }
    uint8_t status() { return m.bus.ioRead(0xCC); }
    void    outScroll(uint8_t v) { m.bus.ioWrite(0xCC, v); }
};

// The lit (foreground, index 1) pixels in one 8x13 character cell of the surface.
int cellForeground(const Surface* s, int col, int row) {
    int cnt = 0, px = col * 8, py = row * 13;
    auto px8 = s->pixels();
    for (int y = 0; y < 13; ++y)
        for (int x = 0; x < 8; ++x) {
            int gx = px + x, gy = py + y;
            if (gx < s->width() && gy < s->height())
                if (px8[(size_t)gy * s->width() + gx] == 1) ++cnt;
        }
    return cnt;
}

} // namespace

void test_vdm1() {
    SECTION("VDM-1 -- memory-mapped screen RAM, and one I/O port");
    {
        Rig g;
        BusCycle c;
        c.type = Cycle::MemWrite;
        c.addr = 0xCC00;
        CHECK(g.vdm->decodes(c), "decodes the screen-RAM base (0xCC00)");
        c.addr = 0xCFFF;
        CHECK(g.vdm->decodes(c), "...and the top of the 1 KB window");
        c.addr = 0xD000;
        CHECK(!g.vdm->decodes(c), "but nothing above it");
        c.addr = 0xCBFF;
        CHECK(!g.vdm->decodes(c), "nor below it");
        c.type = Cycle::IoRead;
        c.addr = 0xCC;
        CHECK(g.vdm->decodes(c), "decodes its one I/O port (0xCC)");
        c.addr = 0xCD;
        CHECK(!g.vdm->decodes(c), "and only that port");
    }

    SECTION("VDM-1 -- the guest writes ASCII into the screen exactly like RAM");
    {
        Rig g;
        g.poke(0, 'A');
        g.poke(1, 'B');
        CHECK(g.vdm->vram(0) == 'A' && g.vdm->vram(1) == 'B', "bytes land in screen RAM");
        CHECK(g.m.bus.memRead(0xCC00) == 'A', "and read back over the bus");

        uint8_t o = 0;
        CHECK(g.vdm->peek(0xCC00, o) && o == 'A', "peek is a side-effect-free read");
        CHECK(!g.vdm->peek(0xC000, o), "peek outside the window declines (not this card)");
    }

    SECTION("VDM-1 -- it renders the screen into the injected Display");
    {
        Rig g;
        g.vdm->pump();
        const Surface* s = g.disp.surface();
        CHECK(s != nullptr, "a frame was acquired");
        if (s) {
            CHECK(s->width() == 512 && s->height() == 208, "64x16 cells of 8x13 = 512x208");
            CHECK(g.disp.frames() >= 1, "present() was called");
            CHECK(cellForeground(s, 0, 0) == 0, "a blank screen lights nothing");
        }

        g.poke(0, 'A');
        g.vdm->pump();
        s = g.disp.surface();
        CHECK(cellForeground(s, 0, 0) > 0, "an 'A' lights foreground pixels in cell (0,0)");
        CHECK(cellForeground(s, 5, 5) == 0, "and only where it was written");
    }

    SECTION("VDM-1 -- hardware scroll moves which character row is at the top");
    {
        Rig g;
        g.poke(1 * 64 + 0, 'X');  // 'X' at row 1, column 0
        g.vdm->pump();
        CHECK(cellForeground(g.disp.surface(), 0, 0) == 0, "unscrolled: display row 0 is blank");
        CHECK(cellForeground(g.disp.surface(), 0, 1) > 0, "and 'X' shows on display row 1");

        g.outScroll(1);  // scroll: character row 1 becomes the top
        g.vdm->pump();
        CHECK(cellForeground(g.disp.surface(), 0, 0) > 0, "scrolled: row 1 is now at the top");
        CHECK(g.vdm->scroll() == 1, "the scroll latch took the low nibble of the OUT");
    }

    SECTION("VDM-1 -- status (IN): OUT arms the one-shot timer (D0)");
    {
        Rig g;
        CHECK((g.status() & 0x01) == 0, "before any OUT, the one-shot bit is clear");
        g.outScroll(0);
        CHECK((g.status() & 0x01) != 0, "after an OUT, D0 is set (timer running)");
    }

    SECTION("VDM-1 -- reverse video swaps the palette, no re-render of glyphs needed");
    {
        Rig g;
        g.vdm->pump();
        CHECK(g.disp.palette().size() == 2, "two colors: background and foreground");
        Color n0 = g.disp.palette()[0];

        std::string err;
        CHECK(setProperty(*g.vdm, "video", "reverse", err), "video=reverse is accepted");
        g.vdm->pump();
        Color r0 = g.disp.palette()[0];
        CHECK(n0.r != r0.r || n0.g != r0.g || n0.b != r0.b,
              "the background color changed with polarity");
    }

    SECTION("VDM-1 -- geometry properties are validated to buildable hardware");
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.vdm, "base", "CC01", err), "a base that is not 1 KB-aligned is refused");
        CHECK(setProperty(*g.vdm, "base", "D000", err), "a 1 KB-aligned base is taken");
        CHECK(!setProperty(*g.vdm, "port", "CD", err), "a port not on a 4-boundary is refused");
        CHECK(setProperty(*g.vdm, "port", "C8", err), "a multiple of 4 is taken");
    }
}
