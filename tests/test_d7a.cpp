#include "test.h"

#include "boards/cromemco-d7a.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/audio.h"
#include "host/joystick.h"
#include "host/level_pcm.h"

#ifdef ALTAIRSIM_ENABLE_SDL
#include "host/joystick_sdl.h"
#include <SDL3/SDL.h>
#endif

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

// An Audio that keeps what it is given, one list of samples for each voice, and reports
// whatever queue depth the test sets. The same injection main() does
// (Speaker::setAudio), so the board renders and pushes as it would to a sound device.
struct StubAudio : public Audio {
    std::map<Owner, std::vector<int16_t>> voices;
    int    pushes = 0;      // how many times push() was called
    size_t depth  = 1000;   // what queued() reports: neither dry nor deep
    bool   device = true;   // what available() reports

    int    rate() const override { return 44100; }
    bool   available() const override { return device; }
    void   push(Owner o, std::span<const int16_t> s) override {
        ++pushes;
        voices[o].insert(voices[o].end(), s.begin(), s.end());
    }
    size_t queued(Owner) const override { return depth; }

    // Every sample pushed, when the test has one voice.
    const std::vector<int16_t>& only() const {
        static const std::vector<int16_t> none;
        return voices.size() == 1 ? voices.begin()->second : none;
    }
};

// Sign changes in a run of samples: two for each cycle of a tone.
int crossings(const std::vector<int16_t>& pcm) {
    int n = 0, last = 0;
    for (int16_t v : pcm) {
        int sign = v > 0 ? 1 : (v < 0 ? -1 : 0);
        if (sign == 0) continue;
        if (last != 0 && sign != last) ++n;
        last = sign;
    }
    return n;
}

struct Rig {
    Machine       m;
    StubJoystick  joy;
    StubAudio     aud;
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
        Speaker::setAudio(&aud);
        m.power();
    }
    // Both stubs die with the rig, so neither may stay installed: a D+7A in a later
    // suite would poll a joystick that is gone.
    ~Rig() {
        Speaker::setAudio(nullptr);
        D7aBoard::setJoystick(nullptr);
    }

    // A square wave on `port` for `tStates`: the level flips every `half` T-states, and
    // the board gets its host turn every `slice` T-states, as the run loop gives it.
    void tone(uint8_t port, uint64_t tStates, uint64_t half, uint64_t slice = 20000) {
        bool     hi = true;
        uint64_t sinceSlice = 0;
        for (uint64_t t = 0; t < tStates; t += half, hi = !hi) {
            out(port, hi ? 0x40 : 0xC0);
            m.clock.advance(half);
            sinceSlice += half;
            if (sinceSlice >= slice) {
                d7a->pump();
                sinceSlice = 0;
            }
        }
        d7a->pump();
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
        CHECK(g.in(0x1A) == 0x81, "Y is inverted: SDL +Y (stick down) reads -full 0x81 (-127)");

        g.joy.sticks[0].x = -32768;           // full the other way
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x81, "a full -X reads 0x81 (-127) -- held one short of the A/D's 0x80");
    }

    SECTION("D+7A -- console 2 lands on analog channels 3/4 (0x1B/0x1C)");
    {
        Rig g;
        std::string err;
        setProperty(*g.d7a, "joystick2", "1", err);   // console 2 <- gamepad 1
        g.joy.sticks[1].x = 32767;
        g.joy.sticks[1].y = -32768;
        g.d7a->pump();
        CHECK(g.in(0x1B) == 0x7F, "console 2 X on port 0x1B: +full is +127 (0x7F), the A/D's full scale");
        CHECK(g.in(0x1C) == 0x7F, "console 2 Y inverted: SDL -Y (stick up) reads +full 0x7F (+127)");
    }

    SECTION("D+7A -- a full deflection steers a game that needs a large move");
    {
        // Cromemco's GOTCHA stores the complement of each axis at rest (0x00 -> 0xFF), adds
        // it to every later reading, and takes a direction only when the size of the sum is
        // 0x40 or more. So a full deflection must reach 0x40 after that add, and must keep
        // its sign: 0x80 + 0xFF is 0x7F, a full move the opposite way.
        auto large = [](uint8_t reading, bool positive) {
            uint8_t sum = (uint8_t)(reading + 0xFF);
            bool    neg = sum & 0x80;
            uint8_t mag = neg ? (uint8_t)~sum : sum;
            return neg != positive && mag >= 0x40;
        };
        Rig g;
        g.joy.n = 1;
        g.joy.sticks[0].x = 32767;
        g.joy.sticks[0].y = -32768;           // SDL -Y = stick up = a positive byte
        g.d7a->pump();
        CHECK(large(g.in(0x19), true), "full +X is a large positive move");
        CHECK(large(g.in(0x1A), true), "full up is a large positive move");

        g.joy.sticks[0].x = -32768;
        g.joy.sticks[0].y = 32767;
        g.d7a->pump();
        CHECK(large(g.in(0x19), false), "full -X is a large negative move, not a wrapped positive one");
        CHECK(large(g.in(0x1A), false), "full down is a large negative move");
    }

#ifdef ALTAIRSIM_ENABLE_SDL
    SECTION("D+7A -- a host stick resting off center keeps full moves in the right direction");
    {
        bool inited = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        CHECK(inited, "the gamepad subsystem starts without a window");
        if (inited) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = 2;
            desc.nbuttons = 4;
            desc.axis_mask = (1 << SDL_GAMEPAD_AXIS_LEFTX) | (1 << SDL_GAMEPAD_AXIS_LEFTY);
            desc.button_mask = 0x0F;
            desc.name = "D+7A test stick";
            SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
            CHECK(id != 0, "a virtual gamepad is attached");
            SDL_Joystick* pad = SDL_OpenJoystick(id);
            CHECK(pad != nullptr, "the virtual axes can be driven");
            if (pad) {
                SdlJoystick host;
                Rig g;
                D7aBoard::setJoystick(&host);
                int n = 0, index = -1;
                SDL_JoystickID* ids = SDL_GetGamepads(&n);
                for (int i = 0; i < n; ++i) if (ids[i] == id) index = i;
                SDL_free(ids);
                CHECK(index >= 0, "SDL exposes the virtual stick as a gamepad");
                std::string err;
                CHECK(setProperty(*g.d7a, "joystick1", std::to_string(index), err),
                      "console 1 reads the virtual gamepad");

                for (int axis : {0, 1}) {
                    uint8_t port = axis == 0 ? 0x19 : 0x1A;
                    for (int rest : {-4096, -300, -257, -256, -1, 0, 255, 256, 300, 4096}) {
                        for (int full : {-32768, 32767}) {
                            CHECK(SDL_SetJoystickVirtualAxis(pad, axis, (int16_t)rest),
                                  "set the raw rest offset");
                            g.d7a->pump();
                            uint8_t center = g.in(port);
                            CHECK(center == 0x00, "host center drift reads 0x00 on either axis");
                            CHECK(SDL_SetJoystickVirtualAxis(pad, axis, (int16_t)full),
                                  "set the raw full deflection");
                            g.d7a->pump();
                            bool positive = (full > 0) != (axis == 1);
                            uint8_t reading = g.in(port);
                            CHECK(reading == (positive ? 0x7F : 0x81),
                                  "host mapping preserves full-scale guest bytes and Y inversion");
                            uint8_t sum = (uint8_t)(reading + (uint8_t)~center);
                            bool neg = sum & 0x80;
                            uint8_t mag = neg ? (uint8_t)~sum : sum;
                            CHECK(neg != positive, "GOTCHA keeps the full move's direction after calibration");
                            CHECK(mag >= 0x40, "GOTCHA sees a large move after calibration");
                        }
                    }

                    // Just outside the dead zone, and both edges of Doodle's drawing range.
                    const struct { int raw; uint8_t x; uint8_t y; bool drawX; bool drawY; } cases[] = {
                        {-4097, 0xEF, 0x11, true, true}, {4097, 0x10, 0xF0, true, true},
                        {-16385, 0xBF, 0x41, false, false}, {-16129, 0xC0, 0x40, true, false},
                        {-16128, 0xC1, 0x3F, true, true}, {16128, 0x3F, 0xC1, true, true},
                        {16384, 0x40, 0xC0, false, true}, {16640, 0x41, 0xBF, false, false},
                    };
                    for (const auto& c : cases) {
                        CHECK(SDL_SetJoystickVirtualAxis(pad, axis, (int16_t)c.raw),
                              "set an axis outside the center dead zone");
                        g.d7a->pump();
                        uint8_t reading = g.in(port);
                        CHECK(reading == (axis == 0 ? c.x : c.y),
                              "outside the dead zone the guest byte is unchanged");
                        bool draws = !((uint8_t)(reading + 0x40) & 0x80);
                        CHECK(draws == (axis == 0 ? c.drawX : c.drawY),
                              "Dazzle Doodle draws only within C0..3F");
                    }
                }
                SDL_CloseJoystick(pad);
            }
            if (id) CHECK(SDL_DetachVirtualJoystick(id), "detach the virtual gamepad");
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    }
#endif

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
        CHECK(g.in(0x19) == 0x7F, "with no gamepad, `auto` reads the keyboard (+full is 0x7F)");

        g.joy.n = 1;                      // now a gamepad appears
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x81, "with a gamepad, `auto` reads it and ignores the keyboard");
    }

    SECTION("D+7A -- both consoles default to `auto`, and `auto` is per-console");
    {
        Rig g;
        g.joy.n = 2;                          // two gamepads present
        g.joy.sticks[0].x = -32768;           // pad 0 full -X
        g.joy.sticks[1].x = 32767;            // pad 1 full +X
        g.d7a->pump();                         // both straps default to `auto`
        CHECK(g.in(0x19) == 0x81, "console 1 `auto` reads gamepad 0");
        CHECK(g.in(0x1B) == 0x7F, "console 2 `auto` reads gamepad 1, not gamepad 0");
    }

    SECTION("D+7A -- console 2 `auto` falls back to the keyboard with only one gamepad");
    {
        Rig g;
        g.joy.n = 1;                          // one gamepad -> nothing at index 1
        g.joy.sticks[0].x = -32768;           // pad 0 (console 1's)
        g.joy.kbd.x = 32767;                  // the keyboard is pushing +X
        g.d7a->pump();
        CHECK(g.in(0x19) == 0x81, "console 1 still reads gamepad 0");
        CHECK(g.in(0x1B) == 0x7F, "console 2 `auto`, no gamepad 1, reads the keyboard");
    }

    SECTION("D+7A -- statusLines() reports what each console resolves to");
    {
        Rig g;
        std::string err;
        g.joy.n        = 1;
        g.joy.names[0] = "Test Pad";
        // Defaults: console 1 auto -> gamepad 0; console 2 auto -> keyboard (no gamepad 1).
        auto s = g.d7a->statusLines();
        CHECK(s.size() == 4, "one line per console, then one per speaker");
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
        CHECK(g.in(0x19) == 0x81, "precondition: the A/D shadow is set");

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
        CHECK(b2.analogIn(0) == 0x81, "and the A/D input shadow travels");

        D7aBoard::setJoystick(&g.joy);  // put the rig's stub back for any later use
    }

    SECTION("D+7A -- a tone on the speaker port reaches the sound service at its pitch");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.aud.depth = 0;                       // the device has nothing: a cushion goes first
        g.d7a->pump();
        CHECK(g.aud.pushes == 0, "a speaker nobody has driven pushes nothing");

        // One second at 2 MHz, a flip every 1000 T-states: 1000 Hz.
        g.tone(0x19, 2000000, 1000);
        const auto& pcm = g.aud.only();
        CHECK(g.aud.voices.size() == 1, "one speaker is one voice");
        CHECK(g.aud.pushes == 100, "one push for each slice");
        // Every push found the queue empty, so each carries its 50 ms cushion.
        CHECK(pcm.size() == 44100 + 100 * 2205, "a second of sound, and a cushion a push");
        int n = crossings(pcm);
        CHECK(n >= 1990 && n <= 2400, "1000 Hz crosses zero about 2000 times");
        bool swing = false;
        for (int16_t v : pcm) swing = swing || v == 0x40 * LevelPcm::kGain;
        CHECK(swing, "at the level the guest wrote, times the gain");
    }

    SECTION("D+7A -- with sound queued, a slice of time is the same length of sound");
    {
        Rig g;
        g.m.clock.setHz(2000000);              // depth 1000: neither dry nor deep
        g.tone(0x19, 2000000, 1000);
        const auto& pcm = g.aud.only();
        CHECK(pcm.size() == 44100, "one emulated second is one second of samples, no cushion");
        int n = crossings(pcm);
        CHECK(n >= 1998 && n <= 2000, "and 1000 Hz crosses zero 2000 times");
    }

    SECTION("D+7A -- a machine with no crystal plays nothing, and SHOW says why");
    {
        Rig g;
        CHECK(g.m.clock.free(), "precondition: flat out is the default");
        g.tone(0x19, 2000000, 1000);
        CHECK(g.aud.pushes == 0, "flat out pushes no samples at all");
        auto s = g.d7a->statusLines();
        CHECK(s.size() == 4 && s[2].find("speaker 1") != std::string::npos &&
                  s[2].find("(channel 1)") != std::string::npos,
              "the third line is speaker 1, on channel 1");
        CHECK(s.size() == 4 && s[2].find("no crystal (clock_hz = 0)") != std::string::npos,
              "and it names the reason");

        // The time that went by flat out is not played late when a crystal is set.
        g.m.clock.setHz(2000000);
        g.d7a->pump();
        CHECK(g.aud.pushes == 0, "setting a crystal does not play what went before");
        g.tone(0x19, 200000, 1000);
        CHECK(g.aud.only().size() == 4410, "a tenth of a second from here is a tenth of sound");
        s = g.d7a->statusLines();
        CHECK(s[2].find("-> playing") != std::string::npos, "a driven speaker reads as playing");
        CHECK(s[3].find("(channel 3)") != std::string::npos &&
                  s[3].find("-> silent") != std::string::npos,
              "speaker 2 is on channel 3 and nothing drives it");

        g.aud.device = false;
        s = g.d7a->statusLines();
        CHECK(s[2].find("no sound device") != std::string::npos, "no device behind the service");
    }

    SECTION("D+7A -- the speaker straps: unwired, and the Dazzler II wiring");
    {
        Rig g;
        std::string err;
        g.m.clock.setHz(2000000);
        CHECK(!setProperty(*g.d7a, "speaker1", "8", err), "there is no channel 8");
        CHECK(!setProperty(*g.d7a, "speaker1", "0", err), "nor a channel 0");
        CHECK(setProperty(*g.d7a, "speaker1", "none", err), "'none' unwires the speaker");
        g.tone(0x19, 200000, 1000);
        CHECK(g.aud.pushes == 0, "an unwired speaker pushes nothing");
        CHECK(g.d7a->statusLines()[2].find("unwired") != std::string::npos, "and reads as unwired");

        // Default: console 2's speaker is on channel 3 (port 1B). Port 1A is not a speaker.
        g.tone(0x1A, 200000, 1000);
        CHECK(g.aud.pushes == 0, "a D/A channel with no speaker on it pushes nothing");
        CHECK(g.d7a->analogOut(1) != 0, "though the latch took the writes");
        CHECK(g.in(0x1A) == 0x00, "and the A/D on the same port still reads the stick");

        CHECK(setProperty(*g.d7a, "speaker2", "2", err), "move speaker 2 to channel 2");
        g.tone(0x1A, 200000, 1000);
        CHECK(g.aud.only().size() == 4410, "and port 1A now plays");
        int n = crossings(g.aud.only());
        CHECK(n >= 198 && n <= 200, "at its pitch");
    }

    SECTION("D+7A -- two speakers are two voices");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        for (int i = 0; i < 100; ++i) {
            g.out(0x19, i & 1 ? 0x40 : 0xC0);
            g.out(0x1B, i & 1 ? 0x20 : 0xE0);
            g.m.clock.advance(1000);
        }
        g.d7a->pump();
        CHECK(g.aud.voices.size() == 2, "each speaker pushes under its own owner");
    }

    SECTION("D+7A -- a deep queue drops the slice, and the sound does not come late");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.aud.depth = 44100 / 4 + 1;           // more than a quarter second waiting
        g.tone(0x19, 2000000, 1000);
        CHECK(g.aud.pushes == 0, "the machine is ahead of the device: nothing is pushed");
        g.aud.depth = 1000;                    // the device caught up
        g.tone(0x19, 200000, 1000);
        CHECK(g.aud.only().size() == 4410, "only the time from here on is played");
    }

    SECTION("D+7A -- a speaker left alone goes quiet");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.tone(0x19, 200000, 1000);
        size_t played = g.aud.only().size();
        // The level now sits where the tone left it. Half a second later it is silence.
        for (int i = 0; i < 200; ++i) {
            g.m.clock.advance(20000);
            g.d7a->pump();
        }
        size_t tail = g.aud.only().size() - played;
        // To the slice: the last change was 1000 T-states before the tone ended, and a
        // slice is 441 samples.
        CHECK(tail > 22050 - 2 * 441 && tail <= 22050, "a held level is played for half a second");
        int before = g.aud.pushes;
        g.aud.depth = 0;
        g.d7a->pump();
        CHECK(g.aud.pushes == before, "and a quiet speaker gets no cushion");
        CHECK(g.d7a->statusLines()[2].find("-> silent") != std::string::npos, "then it is silent");
    }

    SECTION("D+7A -- sound is pushed in pump(), not inside a bus cycle");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        for (int i = 0; i < 50; ++i) {
            g.out(0x19, i & 1 ? 0x40 : 0xC0);
            g.m.clock.advance(1000);
        }
        CHECK(g.aud.pushes == 0, "fifty writes to the speaker port push nothing");
        g.d7a->pump();
        CHECK(g.aud.pushes == 1, "pump() pushes the slice, once");
        g.aud.depth = 0;
        g.d7a->pump();
        CHECK(g.aud.pushes == 1, "a second pump() with no time gone by pushes nothing");
    }

    SECTION("D+7A -- a bus reset takes the speaker to 0 V");
    {
        Rig g;
        g.m.clock.setHz(441000);               // ten T-states to the sample
        g.out(0x19, 0x40);
        g.m.clock.advance(1000);
        g.m.reset(Reset::Bus);
        g.m.clock.advance(1000);
        g.d7a->pump();
        const auto& pcm = g.aud.only();
        CHECK(pcm.size() == 200, "two hundred samples");
        CHECK(pcm.size() == 200 && pcm[99] == 0x40 * LevelPcm::kGain, "the level, up to the reset");
        CHECK(pcm.size() == 200 && pcm[100] == 0 && pcm[199] == 0, "and 0 V after it");
    }

    SECTION("D+7A -- one volume for the board, on both speakers");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.d7a, "volume", "-6", err), "-6 dB is taken");
        g.m.clock.setHz(441000);               // ten T-states to the sample
        g.out(0x19, 0x40);                     // speaker 1, channel 1
        g.out(0x1B, 0x40);                     // speaker 2, channel 3
        g.m.clock.advance(1000);
        g.d7a->pump();
        CHECK(g.aud.voices.size() == 2, "two speakers, two voices");
        const int half = (int)std::lround(0x40 * LevelPcm::kGain * std::pow(10.0, -6 / 20.0));
        bool both = g.aud.voices.size() == 2;
        for (const auto& [owner, pcm] : g.aud.voices)
            both = both && !pcm.empty() && std::abs(pcm.back() - half) <= 1;
        CHECK(both, "each is about half the level the guest wrote, times the gain");
        CHECK(!setProperty(*g.d7a, "volume", "-41", err), "-41 dB is refused");
    }

    SECTION("D+7A -- power-on and RESTORE do not play the time the clock jumped");
    {
        Rig g;
        g.m.clock.setHz(2000000);
        g.tone(0x19, 200000, 1000);
        g.m.power();                            // the clock is back at zero
        g.aud.voices.clear();
        g.aud.pushes = 0;
        g.tone(0x19, 200000, 1000);
        CHECK(g.aud.only().size() == 4410, "after power-on a tenth of a second is a tenth");

        StateWriter w;
        g.d7a->serialize(w);
        g.m.clock.advance(3600ull * 2000000);   // an hour on, as a RESTORE can move it
        StateReader r(w.data());
        g.d7a->deserialize(r);
        g.aud.voices.clear();
        g.aud.pushes = 0;
        g.d7a->pump();
        CHECK(g.aud.pushes == 0, "the hour is not rendered");
        g.tone(0x19, 200000, 1000);
        CHECK(g.aud.only().size() == 4410, "and the sound after it is the right length");
    }

    // ---- The READY hold (issue #619) ----
    //
    // A real D+7A holds READY low for 5.5 us on every analog IN and OUT -- 11 wait
    // states at 2 MHz (reference/D+7A.md §4). These run a real 8080 through the
    // debugger's run loop, because that is where an instruction's time is charged.

    // The rig, with a CPU at `hz` and `code` at 0.
    struct CpuRig : Rig {
        Board* cpu = nullptr;
        CpuRig(const std::vector<uint8_t>& code, long long hz = 2000000) {
            std::string err;
            cpu = m.add("8080", "cpu0", err);
            setProperty(*cpu, "clock_hz", std::to_string(hz), err);
            m.power();
            for (size_t i = 0; i < code.size(); ++i) m.bus.memWrite((uint16_t)i, code[i]);
            m.cpu()->setPc(0);
        }
        // The T-states one instruction took.
        uint64_t step() {
            uint64_t t0 = m.clock.now();
            m.debug.run(1);
            return m.clock.now() - t0;
        }
    };

    SECTION("D+7A -- an analog IN or OUT holds READY 11 wait states at 2 MHz; the parallel port none");
    {
        // OUT 18h ; OUT 19h ; IN 18h ; IN 19h ; OUT 1Fh ; NOP
        CpuRig g({0xD3, 0x18, 0xD3, 0x19, 0xDB, 0x18, 0xDB, 0x19, 0xD3, 0x1F, 0x00});
        CHECK(g.step() == 10, "OUT to the parallel port is the 8080's 10 T-states");
        CHECK(g.step() == 21, "OUT to analog channel 1 is 10 + 11 wait states");
        CHECK(g.step() == 10, "IN from the parallel port is 10");
        CHECK(g.step() == 21, "IN from analog channel 1 is 10 + 11");
        CHECK(g.step() == 21, "channel 7 holds READY the same as channel 1");
        CHECK(g.step() == 4, "and the next instruction pays nothing for them");
    }

    SECTION("D+7A -- the hold is 5.5 us, so a faster clock waits more states");
    {
        CpuRig g4({0xD3, 0x19}, 4000000);
        CHECK(g4.step() == 10 + 22, "at 4 MHz, 5.5 us is 22 wait states");
        CpuRig g3({0xD3, 0x19}, 3000000);
        CHECK(g3.step() == 10 + 17, "at 3 MHz, 16.5 states round up to 17");
        CpuRig g0({0xD3, 0x19}, 0);
        CHECK(g0.step() == 10 + 11, "flat out the divisor is 2 MHz, so 11");
    }

    SECTION("D+7A -- a monitor OUT between runs takes no emulated time");
    {
        CpuRig g({0x00});
        g.out(0x19, 0x40);  // the monitor's OUT: a real bus cycle, but no instruction
        CHECK(g.step() == 4, "the NOP after it is still 4 T-states");
    }

    SECTION("D+7A -- a JS-1 tone loop plays at the pitch the wait states give it");
    {
        // A square wave on channel 1. Each half: MVI A ; OUT 19h ; MVI B,10 ; DCR B/JNZ.
        //   L:  MVI A,40h  OUT 19h  MVI B,10  D1: DCR B  JNZ D1
        //       MVI A,C0h  OUT 19h  MVI B,10  D2: DCR B  JNZ D2   JMP L
        // Per period: 2 x (7 + 10+11 + 7 + 10 x 15) + 10 = 380 T-states with the hold
        // (5263 Hz at 2 MHz); 358 without it (5587 Hz). A tenth of a second is 526.3
        // periods -- 1052 sign changes -- against 1117 without the hold.
        CpuRig g({0x3E, 0x40, 0xD3, 0x19, 0x06, 0x0A, 0x05, 0xC2, 0x06, 0x00,
                  0x3E, 0xC0, 0xD3, 0x19, 0x06, 0x0A, 0x05, 0xC2, 0x10, 0x00,
                  0xC3, 0x00, 0x00});
        uint64_t sinceSlice = 0;
        while (g.m.clock.now() < 200000) {
            sinceSlice += g.step();
            if (sinceSlice >= 20000) { g.d7a->pump(); sinceSlice = 0; }
        }
        g.d7a->pump();
        int n = crossings(g.aud.only());
        CHECK(n >= 1050 && n <= 1055,"the tone is 5263 Hz, not the 5587 Hz it played without the hold");
    }
}
