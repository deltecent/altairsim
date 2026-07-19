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

    SECTION("VDM-1 -- an unchanged screen is not repainted");
    {
        // THE POINT IS SPEED, AND IT IS NOT A SMALL ONE. The run loop pumps every 2000
        // instructions; painting all 106,496 pixels that often made any machine with a
        // video card in it run 94x slower than one without. A guest waiting on a disk
        // or reading a cassette leaves the screen alone for millions of instructions,
        // and every one of those frames was identical to the last.
        //
        // This gate is the DETERMINISTIC half (the wall-clock frame limiter is the
        // other, and is off here), so a headless build and CI skip exactly the frames
        // the windowed build does.
        Rig g;
        g.vdm->pump();
        const uint64_t first = g.disp.frames();
        CHECK(first >= 1, "the first pump paints -- a fresh card owes one frame");

        g.vdm->pump();
        g.vdm->pump();
        CHECK(g.disp.frames() == first, "nothing changed, so nothing was repainted");

        g.poke(0, 'A');
        g.vdm->pump();
        CHECK(g.disp.frames() == first + 1, "a write to screen RAM earns exactly one frame");
        CHECK(cellForeground(g.disp.surface(), 0, 0) > 0, "and the 'A' is actually on it");

        // Re-writing the SAME byte is not a change. Guests do this constantly.
        g.poke(0, 'A');
        g.vdm->pump();
        CHECK(g.disp.frames() == first + 1, "rewriting an identical byte paints nothing");

        g.poke(0, 'B');
        g.vdm->pump();
        CHECK(g.disp.frames() == first + 2, "a different byte does");

        // The scroll latch moves every row on screen without touching screen RAM.
        g.outScroll(1);
        g.vdm->pump();
        CHECK(g.disp.frames() == first + 3, "a scroll to a new row repaints");
        g.outScroll(1);
        g.vdm->pump();
        CHECK(g.disp.frames() == first + 3, "a scroll to the SAME row does not");
    }

    SECTION("VDM-1 -- a blinking cursor repaints itself, and only when there is one");
    {
        // A cursor changes the picture with no write behind it, so the blink phase is
        // remembered and compared. The guard matters as much as the feature: a screen
        // with NO cursor cell must not repaint on the blink, or the card would repaint
        // twice a second for a change nobody could see.
        //
        // The phase is WALL time (the card's own blink oscillator), so this drives
        // NullDisplay's clock, which moves only when a test moves it.
        Rig g;
        g.vdm->pump();
        const uint64_t base = g.disp.frames();

        // No cursor byte anywhere: half a blink period must change nothing.
        g.disp.advanceHostSeconds(0.5);
        g.vdm->pump();
        CHECK(g.disp.frames() == base, "no cursor on screen, so the blink paints nothing");

        // Now put one there (D7 set) and let the phase flip.
        g.poke(0, 'A' | 0x80);
        g.vdm->pump();
        const uint64_t withCursor = g.disp.frames();
        CHECK(withCursor == base + 1, "the write itself painted once");

        g.disp.advanceHostSeconds(0.5);  // one blink half-period
        g.vdm->pump();
        CHECK(g.disp.frames() == withCursor + 1, "the blink phase flipped, so it repainted");

        // cursor=off means the phase cannot matter, even with a D7 byte on screen.
        std::string err;
        CHECK(setProperty(*g.vdm, "cursor", "off", err), "cursor=off is accepted");
        g.vdm->pump();  // the property change itself is one frame
        const uint64_t off = g.disp.frames();
        g.disp.advanceHostSeconds(0.5);
        g.vdm->pump();
        CHECK(g.disp.frames() == off, "with the cursor off, the blink is not a change");
    }

    SECTION("VDM-1 -- a still screen paints nothing, so input must not ride on frames");
    {
        // THE PRECONDITION BEHIND A DEADLOCK, PINNED SO IT CANNOT BE RE-CREATED.
        //
        // Draining the window's event queue used to live inside present(), which a
        // board reaches only after frameChanged() and wantsFrame() both say yes. This
        // section proves the half that made that fatal: with a steady cursor and
        // nothing being written, a VDM-1 produces NO frames at all, however long it is
        // pumped and however much wall time passes. Keys collected on that path would
        // never arrive -- no frame, so no key; no key, so no echo; no echo, so nothing
        // changed and still no frame -- and the machine would sit deaf at its prompt.
        //
        // So this is not really a test about painting. It is the reason
        // Display::pollEvents() exists and is called from the run loop instead: what a
        // still picture costs must never be what reading the operator costs.
        Rig g;
        std::string err;
        CHECK(setProperty(*g.vdm, "cursor", "steady", err), "cursor=steady is accepted");
        g.poke(0, 'A' | 0x80);  // a cursor cell, but one that does not blink
        g.vdm->pump();

        const uint64_t settled = g.disp.frames();
        for (int i = 0; i < 100; ++i) {
            g.disp.advanceHostSeconds(0.5);  // blink half-periods, for a card not blinking
            g.vdm->pump();
        }
        CHECK(g.disp.frames() == settled,
              "100 pumps across 50 s of wall time and a still screen paints not one frame");
    }

    SECTION("VDM-1 -- the blink is the card's oscillator, not the CPU's crystal");
    {
        // THE REGRESSION. The blink phase used to come off the Clock, so at the DEFAULT
        // `clock_hz = 0` -- where emulated time runs as fast as the host can retire
        // instructions -- the cursor strobed instead of blinking. Nothing on the S-100
        // side can read the blink phase back, and a real VDM-1 blinks at ~1 Hz whatever
        // the 8080 is doing, so emulated time must not be able to move it at all.
        Rig g;
        g.poke(0, 'A' | 0x80);  // a cursor cell, so the phase CAN matter
        g.vdm->pump();
        const uint64_t base = g.disp.frames();

        // Ten emulated SECONDS at 2 MHz -- twenty blink half-periods, had it been
        // T-states -- with the host's wall clock held still.
        g.m.clock.advance(20000000);
        g.vdm->pump();
        CHECK(g.disp.frames() == base, "emulated time alone does not move the blink");

        // And the wall clock alone does.
        g.disp.advanceHostSeconds(0.5);
        g.vdm->pump();
        CHECK(g.disp.frames() == base + 1, "wall time alone does");
    }

    SECTION("VDM-1 -- the host can cap the frame rate, and tests are opted out by default");
    {
        // Two questions, two answers: the board says whether the picture MOVED, the
        // host says whether it wants a frame right NOW. Only the second is wall-clock,
        // which is why it defaults to off -- a test wants a frame every time it asks.
        Rig g;
        g.vdm->pump();
        const uint64_t unlimited = g.disp.frames();
        g.poke(0, 'A');
        g.vdm->pump();
        CHECK(g.disp.frames() == unlimited + 1, "unlimited by default: the change paints");

        // One frame a second: the change below is real, but it is not due yet.
        g.disp.setFrameLimitHz(1.0);
        g.poke(1, 'B');
        g.vdm->pump();
        const uint64_t limited = g.disp.frames();
        g.poke(2, 'C');
        g.vdm->pump();
        CHECK(g.disp.frames() == limited, "a second frame inside the period is held back");

        // ...and it is HELD, not lost: lifting the cap paints what was still owed.
        g.disp.setFrameLimitHz(0.0);
        g.vdm->pump();
        CHECK(g.disp.frames() == limited + 1, "the deferred change is still owed, and paints");
        CHECK(cellForeground(g.disp.surface(), 2, 0) > 0, "'C' made it to the screen");
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
