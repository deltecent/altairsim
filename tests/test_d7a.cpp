#include "test.h"

#include "boards/cromemco-d7a.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/joystick.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

// A programmable Joystick for the bench: set the axes/buttons the host "sees" and count
// how often the board polls it. The SAME injection main() does (D7aBoard::setJoystick),
// one backend down -- so the card folds host input into its A/D and parallel latches
// exactly as it would from a real gamepad, with no SDL and no controller.
struct StubJoystick : public Joystick {
    int         n = 0;            // how many physical gamepads to report
    StickState  sticks[4];        // their cached state
    std::string names[4];         // their human names, for SHOW/statusLines
    StickState  kbd;              // the keyboard-as-a-stick reading
    int         polls = 0;        // how many times poll() was called

    void       poll() override { ++polls; }
    int        count() const override { return n; }
    StickState stick(int i) const override { return (i >= 0 && i < 4) ? sticks[i] : StickState{}; }
    StickState keyboardStick() const override { return kbd; }
    std::string name(int i) const override { return (i >= 0 && i < 4) ? names[i] : std::string{}; }
};

struct Rig {
    Machine       m;
    StubJoystick  joy;
    D7aBoard*     d7a = nullptr;
    MemoryBoard*  mem = nullptr;

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

        d7a = dynamic_cast<D7aBoard*>(m.add("d7a", "d7a0", err));
        D7aBoard::setJoystick(&joy);
        m.power();
    }

    uint8_t in(uint8_t port) { return m.bus.ioRead(port); }
    void    out(uint8_t port, uint8_t v) { m.bus.ioWrite(port, v); }
};

bool decodesIo(D7aBoard* b, uint8_t port) {
    BusCycle c;
    c.type = Cycle::IoWrite;
    c.addr = port;
    return b->decodes(c);
}

} // namespace

void test_d7a() {
    SECTION("D+7A -- eight consecutive I/O ports, and no memory of its own");
    {
        Rig g;
        for (uint8_t p = 0x18; p <= 0x1F; ++p) CHECK(decodesIo(g.d7a, p), "decodes its 8-port block");
        CHECK(!decodesIo(g.d7a, 0x17), "but not the port below the block");
        CHECK(!decodesIo(g.d7a, 0x20), "nor the port above it");
        BusCycle c;
        c.type = Cycle::MemWrite;
        c.addr = 0x18;
        CHECK(!g.d7a->decodes(c), "and no memory -- a MemWrite at 0x18 is not ours");
    }

    SECTION("D+7A -- the port strap moves the whole block, and must be 8-aligned");
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.d7a, "port", "19", err), "a base that is not a multiple of 8 is refused");
        CHECK(setProperty(*g.d7a, "port", "20", err), "an 8-aligned base is taken");
        CHECK(decodesIo(g.d7a, 0x20) && decodesIo(g.d7a, 0x27), "the block moved to 0x20..0x27");
        CHECK(!decodesIo(g.d7a, 0x18), "and no longer answers at the old base");
    }

    SECTION("D+7A -- the parallel port: OUT latches, IN reads back the input byte");
    {
        Rig g;
        g.out(0x18, 0xA5);
        CHECK(g.d7a->parallelOut() == 0xA5, "OUT BASE latches the parallel output byte");
        CHECK(g.d7a->parallelIn() == 0xFF, "the input idles at all-1s (active-low, released) and OUT did not disturb it");
    }

    SECTION("D+7A -- analog D/A: OUT <ch> latches, read back independently of the A/D");
    {
        Rig g;
        g.out(0x19, 0x7F);   // channel 1 D/A -> +2.54 V
        g.out(0x1F, 0x80);   // channel 7 D/A -> -2.56 V
        CHECK(g.d7a->analogOut(0) == 0x7F, "channel 1 D/A latch holds what was written");
        CHECK(g.d7a->analogOut(6) == 0x80, "channel 7 D/A latch too");
        CHECK(g.in(0x19) == 0x00, "reading the port is the A/D input -- independent of the D/A output");
    }

    SECTION("D+7A -- a joystick's X/Y become two's-complement A/D bytes on pump()");
    {
        Rig g;
        g.joy.n = 1;                          // one gamepad -> `auto` picks stick 0
        g.joy.sticks[0].x = 0;                // centered
        g.joy.sticks[0].y = 32767;            // full one way (SDL +Y = stick DOWN)
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x00, "a centered X axis reads 0x00 (0 V)");
        CHECK(g.in(0x1A) == 0x81, "Y is inverted: SDL +Y (stick down) reads -full 0x81");

        g.joy.sticks[0].x = -32768;           // full the other way
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x80, "a full -X reads 0x80 (-2.56 V)");
    }

    SECTION("D+7A -- console 2 lands on analog channels 3/4 (0x1B/0x1C)");
    {
        Rig g;
        std::string err;
        setProperty(*g.d7a, "joystick2", "1", err);   // console 2 <- gamepad 1
        g.joy.sticks[1].x = 32767;
        g.joy.sticks[1].y = -32768;
        g.d7a->pump();
        CHECK(g.in(0x1B) == 0x7E, "console 2 X on port 0x1B: +full clamps to 0x7E (126)");
        CHECK(g.in(0x1C) == 0x7E, "console 2 Y inverted: SDL -Y (stick up) reads +full 0x7E (126)");
    }

    SECTION("D+7A -- buttons: active-low, console 1 in D0-D3, console 2 in D4-D7");
    {
        Rig g;
        std::string err;
        setProperty(*g.d7a, "joystick2", "1", err);
        g.joy.n = 1;
        g.d7a->pump();
        CHECK(g.in(0x18) == 0xFF, "nothing pressed -> every button bit reads 1 (released)");

        g.joy.sticks[0].buttons = 0x05;   // SW1 + SW3 pressed on console 1
        g.joy.sticks[1].buttons = 0x0A;   // SW2 + SW4 pressed on console 2
        g.d7a->pump();
        CHECK((g.in(0x18) & 0x0F) == 0x0A, "console 1's pressed buttons pull their bits LOW (active-low)");
        CHECK((g.in(0x18) & 0xF0) == 0x50, "console 2's pressed buttons, in the high nibble");
    }

    SECTION("D+7A -- `auto` uses a gamepad if present, else the keyboard");
    {
        Rig g;
        g.joy.kbd.x = 32767;              // the keyboard is pushing +X
        g.joy.sticks[0].x = -32768;       // gamepad 0 pushing -X
        g.joy.n = 0;                      // ...but no gamepad connected
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x7E, "with no gamepad, `auto` reads the keyboard (+full clamps to 0x7E)");

        g.joy.n = 1;                      // now a gamepad appears
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x80, "with a gamepad, `auto` reads it and ignores the keyboard");
    }

    SECTION("D+7A -- both consoles default to `auto`, and `auto` is per-console");
    {
        Rig g;
        g.joy.n = 2;                          // two gamepads present
        g.joy.sticks[0].x = -32768;           // pad 0 full -X
        g.joy.sticks[1].x = 32767;            // pad 1 full +X
        g.d7a->pump();                         // both straps default to `auto`
        CHECK(g.in(0x19) == 0x80, "console 1 `auto` reads gamepad 0");
        CHECK(g.in(0x1B) == 0x7E, "console 2 `auto` reads gamepad 1, not gamepad 0");
    }

    SECTION("D+7A -- console 2 `auto` falls back to the keyboard with only one gamepad");
    {
        Rig g;
        g.joy.n = 1;                          // one gamepad -> nothing at index 1
        g.joy.sticks[0].x = -32768;           // pad 0 (console 1's)
        g.joy.kbd.x = 32767;                  // the keyboard is pushing +X
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x80, "console 1 still reads gamepad 0");
        CHECK(g.in(0x1B) == 0x7E, "console 2 `auto`, no gamepad 1, reads the keyboard");
    }

    SECTION("D+7A -- statusLines() reports what each console resolves to");
    {
        Rig g;
        std::string err;
        g.joy.n        = 1;
        g.joy.names[0] = "Test Pad";
        // Defaults: console 1 auto -> gamepad 0; console 2 auto -> keyboard (no gamepad 1).
        auto s = g.d7a->statusLines();
        CHECK(s.size() == 2, "one line per console");
        CHECK(s[0].find("console 1") != std::string::npos, "first line is console 1");
        CHECK(s[0].find("(auto)") != std::string::npos, "console 1 defaults to auto");
        CHECK(s[0].find("gamepad 0") != std::string::npos, "console 1 resolves to gamepad 0");
        CHECK(s[0].find("Test Pad") != std::string::npos, "and names the controller");
        CHECK(s[1].find("(auto)") != std::string::npos, "console 2 also defaults to auto");
        CHECK(s[1].find("keyboard") != std::string::npos, "console 2 falls back to the keyboard");

        CHECK(setProperty(*g.d7a, "joystick2", "none", err), "unwire console 2");
        s = g.d7a->statusLines();
        CHECK(s[1].find("unwired") != std::string::npos, "`none` reads as unwired");

        CHECK(setProperty(*g.d7a, "joystick1", "2", err), "point console 1 at gamepad 2");
        g.joy.n        = 3;
        g.joy.names[2] = "Third Pad";
        s = g.d7a->statusLines();
        CHECK(s[0].find("gamepad 2") != std::string::npos, "an explicit index resolves to it");
        CHECK(s[0].find("Third Pad") != std::string::npos, "named too");

        CHECK(setProperty(*g.d7a, "joystick1", "5", err), "point console 1 at an absent index");
        s = g.d7a->statusLines();
        CHECK(s[0].find("not present") != std::string::npos, "an out-of-range index says so");
    }

    SECTION("D+7A -- the joystick is polled in pump(), not inside a bus cycle");
    {
        Rig g;
        int before = g.joy.polls;
        g.in(0x19);
        g.out(0x19, 0x10);
        CHECK(g.joy.polls == before, "reads and writes do not touch the host controller");
        g.d7a->pump();
        CHECK(g.joy.polls == before + 1, "pump() polls it exactly once");
    }

    SECTION("D+7A -- a bad joystick strap is refused with a reason");
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.d7a, "joystick1", "left", err), "'left' is not a device or keyword");
        CHECK(setProperty(*g.d7a, "joystick1", "none", err), "'none' is fine");
        CHECK(setProperty(*g.d7a, "joystick1", "keyboard", err), "'keyboard' is fine");
        CHECK(setProperty(*g.d7a, "joystick1", "2", err), "a numeric index is fine");
    }

    SECTION("D+7A -- snapshot round-trips the latches");
    {
        Rig g;
        g.out(0x18, 0x3C);    // parallel out
        g.out(0x19, 0x7F);    // a D/A latch
        g.joy.n = 1;
        g.joy.sticks[0].x = -32768;
        g.d7a->pump();        // an A/D shadow + no buttons
        CHECK(g.in(0x19) == 0x80, "precondition: the A/D shadow is set");

        StateWriter w;
        g.d7a->serialize(w);

        // A fresh board with a stub that reports NOTHING -- so if deserialize did not
        // carry the A/D shadow, the read below would be 0x00.
        StubJoystick empty;
        D7aBoard b2;
        D7aBoard::setJoystick(&empty);
        StateReader r(w.data());
        b2.deserialize(r);
        CHECK(b2.parallelOut() == 0x3C, "the parallel output latch travels");
        CHECK(b2.analogOut(0) == 0x7F, "a D/A latch travels");
        CHECK(b2.analogIn(0) == 0x80, "and the A/D input shadow travels");

        D7aBoard::setJoystick(&g.joy);  // put the rig's stub back for any later use
    }
}
