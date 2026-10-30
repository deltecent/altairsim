#include "test.h"

#include "boards/s100-memory.h"
#include "boards/vector-fwii-font.h"
#include "boards/vector-fwii.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/display_null.h"
#include "host/media.h"
#include "host/stream.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>

using namespace altair;

namespace {

// The status byte in each of its four states, from reference/Flashwriter II Manual.md §3.
constexpr uint8_t kNoKey       = 0xBF;
constexpr uint8_t kNoKeyBlank  = 0x9F;
constexpr uint8_t kKey         = 0xFE;
constexpr uint8_t kKeyBlank    = 0xDE;

// 238944 dots to the field. This many whole seconds of a 14334700 Hz crystal is a whole
// number of fields, so a scan line can be set from here, and the host's nanosecond count
// is too large to multiply by the crystal in 64 bits. A board that does that fails.
constexpr uint64_t kFieldStartNs = 238944ULL * 1000000000ULL;

// The first host nanosecond of a scan line, 0-262.
constexpr uint64_t lineNs(uint64_t line) {
    return kFieldStartNs + (line * 912 * 1000000000ULL + 14334699) / 14334700;
}

// A Flashwriter II on a NullDisplay, its keyboard on a scripted line through the real
// resolver path, with host time set by the test. setVerify(true) makes the bus re-derive
// pin 73 on every instruction and stop if the board forgot to announce it.
struct Rig {
    Machine         m;
    NullDisplay     disp;
    FwiiBoard*      fw  = nullptr;
    ScriptedStream* kbd = nullptr;
    uint64_t        ns  = lineNs(0);

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        m.bus.busClock().setHostNs([this] { return ns; });
        fw = dynamic_cast<FwiiBoard*>(m.add("fwii", "fw0", err));
        FwiiBoard::setDisplay(&disp);
        fw->connect("keyboard", "scripted", err);
        kbd = dynamic_cast<ScriptedStream*>(fw->unitStream("keyboard"));
        m.power();
    }
    // setDisplay is a process-global, and `disp` dies with the rig.
    ~Rig() { FwiiBoard::setDisplay(nullptr); }

    void key(const std::string& s) {
        kbd->feed(s);
        fw->pump();  // the keyboard is latched in pump(), never in a bus cycle
    }
    uint8_t status() { return m.bus.ioRead(0x00); }
    uint8_t data() { return m.bus.ioRead(0x01); }

    bool unclaimedOut(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = port;
        return !fw->decodes(c);
    }
    bool decodesMem(uint32_t addr) {
        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = addr;
        return fw->decodes(c);
    }
    bool decodesIn(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return fw->decodes(c);
    }
};

// One dot of a character cell, as painted: both of the scan lines that show that dot line
// must agree, or this returns -1.
int dotAt(const Surface* s, int col, int row, int x, int dotLine) {
    auto         px  = s->pixels();
    const size_t top = (size_t)(row * 10 + dotLine) * 2 * 640 + (size_t)(col * 8 + x);
    return px[top] == px[top + 640] ? px[top] : -1;
}

// True when a whole cell shows `code`'s glyph, reversed or not.
bool cellShows(const Surface* s, int col, int row, uint8_t code, bool reversed) {
    for (int line = 0; line < 10; ++line) {
        const uint8_t bits = fwiifont::glyphRow(code, line);
        for (int x = 0; x < 8; ++x) {
            const int lit = (bits >> (7 - x)) & 1;
            if (dotAt(s, col, row, x, line) != (reversed ? !lit : lit)) return false;
        }
    }
    return true;
}

// The `width` property as the monitor shows it.
std::string widthOf(FwiiBoard& b) {
    for (const Property& p : b.properties())
        if (p.name == "width") return p.get().s();
    return "";
}

// RAM below the board and a processor: what a guest program needs.
struct CpuRig : Rig {
    explicit CpuRig(const char* cpu) {
        std::string err;
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x8000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        m.add(cpu, "cpu0", err);
        m.power();
    }
    void load(std::initializer_list<uint8_t> code, uint16_t at) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
    }
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
        }
    }
};

} // namespace

void test_fwii() {
    SECTION("Flashwriter II -- 2K of RAM at base, and not a byte more");
    {
        Rig g;
        std::string err;
        CHECK(!g.decodesMem(0xEFFF), "EFFF is below the block");
        CHECK(g.decodesMem(0xF000), "F000 is the first byte");
        CHECK(g.decodesMem(0xF7FF), "F7FF is the last byte, the last of the 128 not shown");
        CHECK(!g.decodesMem(0xF800), "F800 is above it: the board does not mirror");

        g.m.bus.memWrite(0xF000, 0x41);
        g.m.bus.memWrite(0xF7FF, 0xC2);
        CHECK(g.m.bus.memRead(0xF000) == 0x41, "RAM reads back at the first byte");
        CHECK(g.m.bus.memRead(0xF7FF) == 0xC2, "and at the last, with bit 7 kept");
        CHECK(g.fw->vram(0x7FF) == 0xC2, "the byte is in the video RAM");

        // Every pad of area F, reference §2.
        for (uint32_t base = 0xC000; base <= 0xF800; base += 0x800) {
            char text[8];
            std::snprintf(text, sizeof text, "%X", (unsigned)base);
            CHECK(setProperty(*g.fw, "base", text, err),
                  "every 2K block from C000 to F800 is a pad of area F");
            CHECK(g.decodesMem(base), "the block moves to the new base");
            CHECK(g.decodesMem(base + 0x7FF), "all of it");
            CHECK(!g.decodesMem(base + 0x800), "and stops 2K up");
            CHECK(!g.decodesMem(base - 1), "and starts at the base");
        }
        CHECK(g.decodesMem(0xFFFF), "the top block reaches FFFF with no wrap");
    }

    SECTION("Flashwriter II -- two input ports at `port`, and no output decode");
    {
        Rig g;
        std::string err;
        CHECK(g.decodesIn(0x00) && g.decodesIn(0x01), "IN 00 and IN 01 are the board's");
        CHECK(!g.decodesIn(0x02), "IN 02 is not");
        CHECK(g.unclaimedOut(0x00) && g.unclaimedOut(0x01), "OUT is not decoded: it needs sINP");

        for (int port = 0; port <= 0x0E; port += 2) {
            char text[8];
            std::snprintf(text, sizeof text, "%X", (unsigned)port);
            CHECK(setProperty(*g.fw, "port", text, err), "every pad of area J");
            CHECK(g.decodesIn((uint8_t)port), "status moves to the even port");
            CHECK(g.decodesIn((uint8_t)(port + 1)), "data moves to the odd port above it");
            CHECK(!g.decodesIn((uint8_t)(port + 2)), "and the pair stops there");
        }

        setProperty(*g.fw, "port", "0E", err);
        g.key("Q");
        CHECK(g.m.bus.ioRead(0x0E) == kKey, "the status byte follows the port");
        CHECK(g.m.bus.ioRead(0x0F) == 'Q', "and so does the data");
    }

    SECTION("Flashwriter II -- the four status bytes, and bit 5 off the board's own crystal");
    {
        Rig g;
        CHECK(g.status() == kNoKey, "no key, active line: BF");
        g.ns = lineNs(240);
        CHECK(g.status() == kNoKeyBlank, "no key, blanking: 9F");
        g.key("A");
        CHECK(g.status() == kKeyBlank, "key, blanking: DE");
        g.ns = lineNs(0);
        CHECK(g.status() == kKey, "key, active line: FE");

        g.ns = lineNs(240) - 1;
        CHECK(g.status() == kKey, "the last nanosecond of line 239 is still active");
        g.ns = lineNs(240);
        CHECK(g.status() == kKeyBlank, "line 240 is the first blank line");
        g.ns = lineNs(262) - 1;
        CHECK(g.status() == kKeyBlank, "line 261 is the last");
        g.ns = lineNs(262);
        CHECK(g.status() == kKey, "and line 262 is line 0 of the next field");

        // 16.67 ms of host time is one field, however far the emulated clock has moved.
        g.ns = lineNs(0);
        g.m.clock.advance(1000000);
        CHECK(g.status() == kKey, "T-states do not move the raster");
        g.m.clock.advance(30000);
        CHECK(g.status() == kKey, "at any count");
        g.ns = lineNs(250);
        CHECK(g.status() == kKeyBlank, "host time does");

        // The field is 238944 dots of 14.3347 MHz, 59.99 Hz.
        int edges = 0;
        bool was  = true;
        for (uint64_t t = 0; t < 1000000000ULL; t += 100000) {
            g.ns       = kFieldStartNs + t;
            bool active = (g.status() & 0x20) != 0;
            if (was && !active) ++edges;
            was = active;
        }
        CHECK(edges == 60, "one second of host time holds 60 fields");
    }

    SECTION("Flashwriter II -- IN from status keeps the flag, IN from data clears it");
    {
        Rig g;
        g.key("A");
        CHECK(g.status() == kKey, "a key sets the flag");
        CHECK(g.status() == kKey, "reading status does not clear it");
        CHECK(g.data() == 'A', "IN 01 gives the key");
        CHECK(g.status() == kNoKey, "and that read clears the flag");
        CHECK(g.data() == 'A', "the latch keeps its byte after the flag clears");
        CHECK(g.fw->rxBytes() == 1, "the key counts as live traffic");
    }

    SECTION("Flashwriter II -- all eight key bits reach the guest");
    {
        Rig g;
        for (int v : {0x00, 0x7F, 0x80, 0xE1, 0xFF}) {
            g.key(std::string(1, (char)v));
            CHECK(g.data() == (uint8_t)v, "the board does not mask or invert a bit");
        }
    }

    SECTION("Flashwriter II -- one byte at a time from the host line");
    {
        Rig g;
        g.key("AB");
        CHECK(g.data() == 'A', "the first key");
        CHECK(g.status() == kNoKey, "the second waits for pump()");
        g.fw->pump();
        CHECK(g.status() == kKey, "pump() latches it");
        g.fw->pump();
        CHECK(g.data() == 'B', "and it was not lost");
    }

    SECTION("Flashwriter II -- pin 73 is a level until the data read");
    {
        Rig g;
        std::string err;
        CHECK(!g.m.bus.intPending(), "no key, no interrupt");
        g.key("A");
        CHECK(g.fw->assertsInt() && g.m.bus.intPending(), "interrupt = int: a key pulls pin 73");
        g.status();
        CHECK(g.m.bus.intPending(), "a status read does not release it");
        g.data();
        CHECK(!g.m.bus.intPending(), "the data read does");

        CHECK(setProperty(*g.fw, "interrupt", "none", err), "area L not fitted");
        g.key("B");
        CHECK(g.status() == kKey, "the flag still shows in status");
        CHECK(!g.m.bus.intPending(), "but pin 73 stays high");
        CHECK(setProperty(*g.fw, "interrupt", "int", err), "fit the wire with the key waiting");
        CHECK(g.m.bus.intPending(), "and the waiting key pulls the pin at once");
        g.data();
    }

    SECTION("Flashwriter II -- the acknowledge reads as RST 7 on a real Z80");
    {
        CpuRig g("z80");
        // RST 7 lands at 0038: read the key, store it, come back with interrupts on.
        g.load({0xDB, 0x01,        // IN A,(01)
                0x32, 0x00, 0x20,  // LD (2000),A
                0xFB,              // EI
                0xC9},             // RET
               0x0038);
        g.load({0x31, 0x00, 0x10,  // LD SP,1000
                0xFB,              // EI
                0xC3, 0x04, 0x01}, // JP 0104
               0x0100);
        g.m.cpu()->setPc(0x0100);
        g.run(20);
        CHECK(g.m.bus.memRead(0x2000) == 0x00, "nothing has happened yet");

        g.key("Z");
        CHECK(g.m.bus.intPending(), "the key pulls pin 73");
        g.run(40);
        CHECK(g.m.bus.memRead(0x2000) == 'Z', "RST 7 ran the handler at 0038");
        CHECK(!g.m.bus.intPending(), "and the handler's IN released the pin");
        CHECK(g.m.cpu()->pc() >= 0x0104 && g.m.cpu()->pc() <= 0x0106, "back in the main loop");
    }

    SECTION("Flashwriter II -- POC* clears the 8212, RESET* reaches nothing");
    {
        Rig g;
        std::string err;
        setProperty(*g.fw, "fill", "zero", err);
        g.m.power();
        CHECK(g.fw->vram(0) == 0x00, "fill = zero: the RAM powers up zero");
        g.m.bus.memWrite(0xF010, 0x55);
        g.key("A");

        g.m.reset(Reset::Bus);
        CHECK(g.status() == kKey, "RESET* leaves the flag set");
        CHECK(g.m.bus.intPending(), "and pin 73 pulled");
        CHECK(g.m.bus.memRead(0xF010) == 0x55, "and the video RAM alone");

        g.m.reset(Reset::PowerOn);
        CHECK(g.status() == kNoKey, "POC* clears the flag");
        CHECK(!g.m.bus.intPending(), "and releases pin 73");
        CHECK(g.data() == 0x00, "and clears the latch");
        CHECK(g.m.bus.memRead(0xF010) == 0x55, "a RAM chip has no POC* pin");

        g.m.power();
        CHECK(g.m.bus.memRead(0xF010) == 0x00, "power is what loses the RAM");
        CHECK(g.fw->rxBytes() == 0, "and the traffic count restarts");
    }

    SECTION("Flashwriter II -- fill and seed");
    {
        Rig a, b;
        std::string err;
        bool same = true, zero = true;
        for (uint16_t i = 0; i < 2048; ++i) {
            same = same && a.fw->vram(i) == b.fw->vram(i);
            zero = zero && a.fw->vram(i) == 0;
        }
        CHECK(!zero, "the default is random: real RAM is not zeroed");
        CHECK(same, "the same seed gives the same RAM");

        setProperty(*b.fw, "seed", "2", err);
        b.m.power();
        same = true;
        for (uint16_t i = 0; i < 2048; ++i) same = same && a.fw->vram(i) == b.fw->vram(i);
        CHECK(!same, "another seed gives other RAM");
    }

    SECTION("Flashwriter II -- snapshot round trip");
    {
        Rig src, dst;
        src.m.bus.memWrite(0xF123, 0x9A);
        src.key("K");

        StateWriter w;
        src.fw->serialize(w);
        StateReader r(w.data());
        dst.fw->deserialize(r);
        dst.fw->intChanged();  // Machine::restore rebuilds the wires
        CHECK(r.ok(), "the blob is read in full");
        CHECK(dst.m.bus.memRead(0xF123) == 0x9A, "the video RAM travelled");
        CHECK(dst.status() == kKey, "the flag travelled");
        CHECK(dst.m.bus.intPending(), "and so pin 73 is pulled again");
        CHECK(dst.fw->rxBytes() == 1, "the traffic count travelled");
        CHECK(dst.data() == 'K', "the latch travelled");
    }

    SECTION("Flashwriter II -- properties refuse what the jumpers cannot do");
    {
        Rig g;
        std::string err;
        CHECK(!setProperty(*g.fw, "base", "F400", err), "base must be a 2K block");
        CHECK(err.find("0x800") != std::string::npos, "and the refusal says so");
        CHECK(!setProperty(*g.fw, "base", "B800", err), "base below C000 has no pad");
        CHECK(!setProperty(*g.fw, "port", "01", err), "port must be even");
        CHECK(err.find("even") != std::string::npos, "and the refusal says so");
        CHECK(!setProperty(*g.fw, "port", "10", err), "port above 0E has no pad");
        CHECK(!setProperty(*g.fw, "interrupt", "vi3", err), "the board has no VI connector");
        CHECK(!setProperty(*g.fw, "fill", "maybe", err), "fill is zero or random");

        CHECK(g.fw->connect("keyboard", "scripted", err), "keyboard is the board's one unit");
        CHECK(!g.fw->connect("printer", "scripted", err), "and it has no other");
        CHECK(g.fw->disconnect("keyboard", err), "the line can be taken off");
        g.fw->pump();
        CHECK(g.status() == kNoKey, "and a NullStream brings no key");
    }

    SECTION("Flashwriter II -- a guest polling for a key is seen to find the line empty");
    {
        // The run loop stands a machine down at a prompt only if the guest is counted
        // finding its keyboard empty, about once in 32 instructions. A once-a-slice look
        // from pump() is far below that, and the machine holds a host core at the prompt.
        Rig g;
        const uint64_t before = g.kbd->hungry();
        for (int i = 0; i < 10; ++i) g.status();
        CHECK(g.kbd->hungry() == before + 10, "each status read asks the line");

        g.key("x");
        const uint64_t held = g.kbd->hungry();
        CHECK(g.status() == kKey, "a key is waiting");
        CHECK(g.kbd->hungry() == held, "and a guest with a key waiting is not hungry");
        CHECK(g.data() == 'x', "the data read gives the key");

        g.kbd->feed("y");
        const uint64_t ready = g.kbd->hungry();
        CHECK(g.status() == kNoKey, "the status read does not take the byte");
        CHECK(g.kbd->hungry() == ready, "and a line with a byte on it is not empty");
        g.fw->pump();
        CHECK(g.status() == kKey, "the next pump latches it");
    }

    SECTION("Flashwriter II -- the picture is 80 by 24 cells of 8 by 10 dots, lines doubled");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.fw, "fill", "zero", err), "fill is settable");
        g.m.power();
        g.fw->pump();
        const Surface* s = g.disp.surface();
        CHECK(s != nullptr, "a frame was acquired");
        if (s) {
            CHECK(s->width() == 640 && s->height() == 480, "640x480, the shape of a 4:3 tube");
            CHECK(s->format() == PixelFormat::Indexed8, "two palette entries");
            CHECK(g.disp.palette().size() == 2, "background and foreground");

            g.m.bus.memWrite(0xF000, 'A');
            g.m.bus.memWrite(0xF000 + 1, 'g');
            g.m.bus.memWrite(0xF000 + 80, 'B');
            g.m.bus.memWrite(0xF000 + 1919, 'Z');
            g.fw->pump();
            s = g.disp.surface();
            CHECK(cellShows(s, 0, 0, 'A', false), "an 'A' at the first byte, every dot");
            CHECK(cellShows(s, 1, 0, 'g', false), "the next byte is the next cell");
            CHECK(cellShows(s, 0, 1, 'B', false), "byte 80 starts the second row");
            CHECK(cellShows(s, 79, 23, 'Z', false), "byte 1919 is the last cell");
            CHECK(cellShows(s, 40, 12, 0x00, false), "and a cell not written stays dark");
        }
    }

    SECTION("Flashwriter II -- a graphics code lights its blocks, D7 at the left");
    {
        // From the rule in the font header, not the table: this catches the table and the
        // painter disagreeing on which way a byte runs across the cell.
        Rig g;
        g.m.bus.memWrite(0xF000, 0x15);  // bits 4, 2, 0
        g.fw->pump();
        const Surface* s = g.disp.surface();
        CHECK(dotAt(s, 0, 0, 1, 5) == 1, "bit 4 lights the middle left block");
        CHECK(dotAt(s, 0, 0, 6, 1) == 1, "bit 2 lights the upper right block");
        CHECK(dotAt(s, 0, 0, 6, 8) == 1, "bit 0 lights the lower right block");
        CHECK(dotAt(s, 0, 0, 1, 1) == 0, "the upper left block has no bit");
        CHECK(dotAt(s, 0, 0, 6, 5) == 0, "bit 1 is clear, so the middle right stays dark");
        CHECK(dotAt(s, 0, 0, 1, 8) == 0, "bit 3 is clear, so the lower left stays dark");
    }

    SECTION("Flashwriter II -- bit 7 reverses the whole cell");
    {
        Rig g;
        g.m.bus.memWrite(0xF000, 'A');
        g.m.bus.memWrite(0xF001, 'A' | 0x80);
        g.m.bus.memWrite(0xF002, ' ' | 0x80);
        g.m.bus.memWrite(0xF003, ' ');
        g.fw->pump();
        const Surface* s = g.disp.surface();
        CHECK(cellShows(s, 0, 0, 'A', false), "bit 7 clear shows the glyph");
        CHECK(cellShows(s, 1, 0, 'A', true), "bit 7 set shows every dot the other way");
        int lit = 0;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 8; ++x) lit += dotAt(s, 2, 0, x, y) == 1;
        CHECK(lit == 80, "a reversed space is the guest's cursor block: all 80 dots");
        CHECK(cellShows(s, 3, 0, ' ', false), "and the next cell is not touched");
    }

    SECTION("Flashwriter II -- the last 128 bytes do not show");
    {
        Rig g;
        g.fw->pump();
        const uint64_t before = g.disp.frames();
        const Surface* s      = g.disp.surface();
        const uint8_t  first  = g.m.bus.memRead(0xF000);
        for (uint16_t off = 1920; off < 2048; ++off) g.m.bus.memWrite(0xF000 + off, 'X');
        g.fw->pump();
        CHECK(g.disp.frames() == before, "a write up there changes no cell, so no frame");
        CHECK(g.m.bus.memRead(0xF7FF) == 'X', "and it is RAM all the same");
        CHECK(cellShows(s, 0, 0, first & 0x7F, (first & 0x80) != 0),
              "and the first row does not show it");
    }

    SECTION("Flashwriter II -- a frame is painted when a cell changes, and only then");
    {
        Rig g;
        g.fw->pump();
        const uint64_t first = g.disp.frames();
        CHECK(first >= 1, "the first pump paints: a fresh board owes one frame");

        g.fw->pump();
        g.fw->pump();
        CHECK(g.disp.frames() == first, "nothing changed, so nothing was painted");

        g.m.bus.memWrite(0xF000, 'A');
        g.m.bus.memWrite(0xF001, 'B');
        g.fw->pump();
        CHECK(g.disp.frames() == first + 1, "two writes in one slice earn one frame");

        g.m.bus.memWrite(0xF000, 'A');
        g.fw->pump();
        CHECK(g.disp.frames() == first + 1, "rewriting an identical byte paints nothing");

        g.m.bus.memWrite(0xF000, 'A' | 0x80);
        g.fw->pump();
        CHECK(g.disp.frames() == first + 2, "bit 7 alone is a change to the picture");

        // A status poll moves the raster, and the raster is not part of the picture.
        g.ns = lineNs(250);
        g.status();
        g.fw->pump();
        CHECK(g.disp.frames() == first + 2, "the raster paints nothing");

        g.m.reset(Reset::Bus);
        g.fw->pump();
        CHECK(g.disp.frames() == first + 2, "RESET* does not touch the screen");

        g.m.power();
        g.fw->pump();
        CHECK(g.disp.frames() == first + 3, "POWER gives new RAM, and that shows");
    }

    SECTION("Flashwriter II -- a change the host defers is still owed");
    {
        Rig g;
        g.fw->pump();

        g.disp.setFrameLimitHz(1.0);
        g.m.bus.memWrite(0xF000, 'A');
        g.fw->pump();
        const uint64_t limited = g.disp.frames();
        g.m.bus.memWrite(0xF001, 'B');
        g.fw->pump();
        CHECK(g.disp.frames() == limited, "a second frame inside the period is held back");

        g.disp.setFrameLimitHz(0.0);
        g.fw->pump();
        CHECK(g.disp.frames() == limited + 1, "the deferred change paints");
        CHECK(cellShows(g.disp.surface(), 1, 0, 'B', false), "and 'B' is on it");
    }

    SECTION("Flashwriter II -- the keyboard does not wait for the picture");
    {
        Rig g;
        g.fw->pump();
        g.key("a");  // no cell changed, so this pump paints nothing
        CHECK(g.status() == kKey, "a key latches on a still screen");
        CHECK(g.data() == 'a', "and reads back");

        g.disp.setFrameLimitHz(1.0);
        g.m.bus.memWrite(0xF000, 'A');
        g.fw->pump();
        g.m.bus.memWrite(0xF001, 'B');
        g.key("b");  // this frame is deferred
        CHECK(g.status() == kKey, "a key latches while the host defers a frame");
        CHECK(g.data() == 'b', "and reads back");
        g.disp.setFrameLimitHz(0.0);

        FwiiBoard::setDisplay(nullptr);
        g.key("c");
        CHECK(g.status() == kKey, "a key latches with no display at all");
        CHECK(g.data() == 'c', "and reads back");
    }

    SECTION("Flashwriter II -- restore repaints, and peek looks without touching");
    {
        Rig src;
        src.m.bus.memWrite(0xF000, 'R');
        StateWriter w;
        src.fw->serialize(w);

        Rig dst;
        dst.fw->pump();
        const uint64_t before = dst.disp.frames();
        StateReader    r(w.data());
        dst.fw->deserialize(r);
        dst.fw->pump();
        CHECK(dst.disp.frames() == before + 1, "the restored screen owes one frame");
        CHECK(cellShows(dst.disp.surface(), 0, 0, 'R', false), "and it is the restored one");

        uint8_t o = 0;
        CHECK(dst.fw->peek(0xF000, o) && o == 'R', "peek reads the first byte");
        CHECK(dst.fw->peek(0xF7FF, o), "and the last byte of the 2K");
        CHECK(!dst.fw->peek(0xF800, o), "and declines the byte past it");
        CHECK(!dst.fw->peek(0xEFFF, o), "and the byte below it");
        dst.key("p");
        CHECK(dst.fw->peek(0xF000, o) && dst.status() == kKey, "peek clears no key flag");
    }

    SECTION("Flashwriter II -- width is the host window, and it survives");
    {
        Rig g;
        std::string err;
        CHECK(widthOf(*g.fw) == "auto", "the default is auto");
        CHECK(setProperty(*g.fw, "width", "1280", err), "a number of pixels is accepted");
        CHECK(widthOf(*g.fw) == "1280", "and it reads back");
        CHECK(!setProperty(*g.fw, "width", "wide", err), "and anything else is refused");
    }
}
