#include "test.h"

#include "boards/cromemco-dazzler.h"
#include "boards/proctech-vdm1.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/display_null.h"

#include <cstdint>
#include <string>

using namespace altair;

// ONE WINDOW PER VIDEO BOARD (issue #234). Before this, a Display kept a single Surface, so
// two video boards -- and especially two of the SAME resolution -- clobbered each other onto
// one buffer: whoever drew last won, and the other board's picture was gone. The seam now
// carries the drawing board's Owner handle (its `this`), and every host (the SDL back end's
// window, the NullDisplay's Surface) is kept one-per-owner. These tests prove the buffers no
// longer collide, headless, with no window (DESIGN.md 7.4).

void test_multiwindow() {
    SECTION("multi-window -- two owners at the SAME resolution get distinct surfaces");
    {
        // The heart of #234, at the seam itself: two owners ask for an identical (w,h,fmt).
        // The old single-surface Display handed both the same buffer; each must now get its
        // own. Opaque owner handles -- the Display never dereferences them, so two local
        // objects' addresses stand in for two boards.
        NullDisplay disp;
        char tagA = 0, tagB = 0;
        Display::Owner a = &tagA, b = &tagB;

        Surface* sa = disp.acquire(a, "vdmA", 512, 208, PixelFormat::Indexed8, 0);
        Surface* sb = disp.acquire(b, "vdmB", 512, 208, PixelFormat::Indexed8, 0);
        CHECK(sa != nullptr && sb != nullptr, "both owners get a surface");
        CHECK(sa != sb, "and they are DISTINCT buffers, not the one shared surface");
        CHECK(disp.surface(a) == sa && disp.surface(b) == sb,
              "each owner reads back its own surface");

        // Paint a signature into each and confirm the other is untouched -- proof they are
        // not aliases of one buffer.
        sa->clear(1);
        sb->clear(2);
        CHECK(disp.surface(a)->pixels()[0] == 1, "owner A keeps what A drew");
        CHECK(disp.surface(b)->pixels()[0] == 2, "owner B keeps what B drew, undisturbed by A");
    }

    SECTION("multi-window -- frames and palette are counted per owner, not pooled");
    {
        NullDisplay disp;
        char tagA = 0, tagB = 0;
        Display::Owner a = &tagA, b = &tagB;

        Surface* sa = disp.acquire(a, "a", 64, 64, PixelFormat::Indexed8, 0);
        Surface* sb = disp.acquire(b, "b", 64, 64, PixelFormat::Indexed8, 0);

        disp.present(a, sa);
        disp.present(a, sa);
        disp.present(b, sb);
        CHECK(disp.frames(a) == 2, "owner A's presents count only A's");
        CHECK(disp.frames(b) == 1, "owner B's count only B's");

        const Color redA[1]  = {{0xFF, 0, 0, 0xFF}};
        const Color blueB[1] = {{0, 0, 0xFF, 0xFF}};
        disp.setPalette(a, redA);
        disp.setPalette(b, blueB);
        CHECK(disp.palette(a).size() == 1 && disp.palette(a)[0].r == 0xFF,
              "owner A keeps its own palette");
        CHECK(disp.palette(b).size() == 1 && disp.palette(b)[0].b == 0xFF,
              "owner B keeps its own, unmerged with A's");
    }

    SECTION("multi-window -- a VDM-1 and a Dazzler in one machine draw into their own surfaces");
    {
        // The wiring, end to end: two real video boards share the one injected Display (the
        // SAME per-class setDisplay main() does), each pumps, and each lands on its own buffer.
        // Under the old single-surface Display the second board's pump() overwrote the first's
        // frame; here disp.surface(board) reads each back independently.
        Machine     m;
        NullDisplay disp;
        std::string err;
        m.bus.setVerify(true);

        // RAM below the VDM's fixed 0xCC00 window, so the Dazzler's framebuffer (base 0x2000)
        // has somewhere to live and nothing clashes.
        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0xC000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        VdmBoard*     vdm = dynamic_cast<VdmBoard*>(m.add("vdm1", "vdm0", err));
        DazzlerBoard* daz = dynamic_cast<DazzlerBoard*>(m.add("dazzler", "daz0", err));
        CHECK(vdm && daz, "both video boards are in the machine");
        VdmBoard::setDisplay(&disp);
        DazzlerBoard::setDisplay(&disp);
        m.power();

        // The VDM writes an 'A'; the Dazzler is turned on with a small framebuffer.
        m.bus.memWrite(0xCC00, 'A');
        m.bus.ioWrite(0x0E, (uint8_t)(0x80 | (0x2000 >> 9)));  // Dazzler on, base 0x2000

        vdm->pump();
        daz->pump();

        const Surface* sv = disp.surface(vdm);
        const Surface* sd = disp.surface(daz);
        CHECK(sv != nullptr && sd != nullptr, "both boards acquired a surface");
        CHECK(sv != sd, "and they are two different surfaces, not one shared one");
        if (sv) CHECK(sv->width() == 512 && sv->height() == 208, "the VDM keeps its 512x208 frame");
        if (sd) CHECK(sd->width() == sd->height(), "the Dazzler keeps its own square frame");
        CHECK(disp.frames(vdm) >= 1 && disp.frames(daz) >= 1, "each board presented its own");
    }
}
