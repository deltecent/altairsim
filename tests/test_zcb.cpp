// The Vector Graphic ZCB (docs/boards/vector-zcb.md) and its Intel 8255.
//
// Real bus cycles through the decode. The last sections run the real Monitor 4.3 from the reset at
// 0000, with no help from the test.

#include "boards/s100-memory.h"
#include "boards/vector-fwii.h"
#include "boards/vector-zcb.h"
#include "chips/intel8255.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/display_null.h"
#include "host/stream.h"
#include "test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace altair;

namespace {

constexpr uint8_t kPromFirst = 0xC3;  // VGMON43.BIN opens with JP E015
constexpr uint8_t kTxRDY     = 0x01;
constexpr uint8_t kRxRDY     = 0x02;
constexpr uint8_t kTxEMPTY   = 0x04;
constexpr uint8_t kDSR       = 0x80;

// A ZCB with Monitor 4.3 in one 2732, over 56K of RAM that stands down for PHANTOM*.
struct Rig {
    Machine      m;
    ZcbBoard*    z   = nullptr;
    MemoryBoard* mem = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        z   = dynamic_cast<ZcbBoard*>(m.add("zcb", "zcb0", err));
        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        z->loadSubUnit("socket", {{"at", "E000"}, {"mount", "builtin:vgmon43"}}, err);
        Region ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0xE000;
        mem->addRegion(ram, err);
        setProperty(*mem, "honors_phantom", "all", err);
        setProperty(*z, "clock_hz", "4000000", err);
        m.power();
    }

    uint8_t in(uint8_t p) { return m.bus.ioRead(p); }
    void    out(uint8_t p, uint8_t v) { m.bus.ioWrite(p, v); }
    uint8_t rd(uint16_t a) { return m.bus.memRead(a); }
    void    wr(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }
    void    release() { rd(0xE000); }

    ScriptedStream* line() {
        auto        s = std::make_unique<ScriptedStream>();
        auto*       p = s.get();
        std::string err;
        z->connectStream("serial", std::move(s), err);
        return p;
    }

    // Mode, then a command with TxEN, DTR, RxE and RTS, as in Monitor 4.3.
    void program(uint8_t mode = 0xCE) {
        out(0x05, mode);
        out(0x05, 0x27);
    }

    // How long the transmitter stays busy with one byte.
    uint64_t charTime() {
        const uint64_t start = m.clock.now();
        out(0x04, 'x');
        while (!(in(0x05) & kTxEMPTY)) m.clock.advance(1);
        return m.clock.now() - start;
    }
};

// The machine in which the Monitor comes up: the ZCB, the Flashwriter and RAM.
struct Vector {
    Machine         m;
    NullDisplay     disp;
    ZcbBoard*       z   = nullptr;
    FwiiBoard*      fw  = nullptr;
    ScriptedStream* kbd = nullptr;

    Vector() {
        std::string err;
        FwiiBoard::setDisplay(&disp);
        z  = dynamic_cast<ZcbBoard*>(m.add("zcb", "cpu0", err));
        fw = dynamic_cast<FwiiBoard*>(m.add("fwii", "fw0", err));
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        z->loadSubUnit("socket", {{"at", "E000"}, {"mount", "builtin:vgmon43"}}, err);
        Region ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0xE000;
        mem->addRegion(ram, err);
        setProperty(*mem, "honors_phantom", "all", err);
        fw->connect("keyboard", "scripted", err);
        kbd = dynamic_cast<ScriptedStream*>(fw->unitStream("keyboard"));
        m.power();
    }
    ~Vector() { FwiiBoard::setDisplay(nullptr); }

    bool screenHas(const std::string& text) {
        for (uint16_t a = 0; a + text.size() < 80 * 24; ++a) {
            size_t i = 0;
            while (i < text.size() && fw->vram((uint16_t)(a + i)) == (uint8_t)text[i]) ++i;
            if (i == text.size()) return true;
        }
        return false;
    }

    template <class F> bool run(F done, int limit) {
        for (int i = 0; i < limit; ++i) {
            if ((i & 0x3FF) == 0) {
                if (done()) return true;
                fw->pump();
                z->pump();
            }
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates + m.bus.takeWaitStates());
        }
        return done();
    }
};

// A line that holds DTR off at the far end, so the DSR input is not active.
class NoDsr : public ScriptedStream {
public:
    LineStatus status() const override {
        LineStatus s;
        s.dsr = false;
        return s;
    }
};

std::string writeImage(const char* name, size_t bytes, uint8_t fill) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f << std::string(bytes, (char)fill);
    return path.string();
}

} // namespace

void test_intel8255() {
    SECTION("intel8255: reset makes every port an input");
    {
        Intel8255 p;
        CHECK(p.control() == 0x9B, "mode 0, all four inputs");
        p.pinA = 0x12;
        p.pinB = 0x34;
        p.pinC = 0x56;
        CHECK(p.read(0) == 0x12, "port A reads its pins");
        CHECK(p.read(1) == 0x34, "port B reads its pins");
        CHECK(p.read(2) == 0x56, "port C reads its pins");
        p.write(3, 0x80);
        p.write(0, 0x77);
        p.reset();
        CHECK(p.read(0) == 0x12, "a reset puts port A back to input");
        CHECK(p.outA() == 0, "and clears its latch");
    }

    SECTION("intel8255: mode 0, all 16 direction sets");
    {
        for (int n = 0; n < 16; ++n) {
            const bool    inA = n & 8, inCh = n & 4, inB = n & 2, inCl = n & 1;
            const uint8_t word =
                (uint8_t)(0x80 | (inA ? 0x10 : 0) | (inCh ? 0x08 : 0) | (inB ? 0x02 : 0) | (inCl ? 1 : 0));
            Intel8255 p;
            p.pinA = 0xA5;
            p.pinB = 0xB6;
            p.pinC = 0xC7;
            p.write(3, word);
            p.write(0, 0x11);
            p.write(1, 0x22);
            p.write(2, 0x33);
            CHECK(p.read(0) == (inA ? 0xA5 : 0x11), "port A follows D4");
            CHECK(p.read(1) == (inB ? 0xB6 : 0x22), "port B follows D1");
            const uint8_t c = (uint8_t)(((inCh ? 0xC7 : 0x33) & 0xF0) | ((inCl ? 0xC7 : 0x33) & 0x0F));
            CHECK(p.read(2) == c, "port C splits on D3 and D0");
        }
    }

    SECTION("intel8255: outputs are latched, inputs are not");
    {
        Intel8255 p;
        p.write(3, 0x90);  // A input, the rest output
        p.write(1, 0x5A);
        p.pinB = 0x00;
        CHECK(p.read(1) == 0x5A, "an output keeps its byte");
        p.pinA = 0x01;
        CHECK(p.read(0) == 0x01, "an input shows the pin");
        p.pinA = 0x02;
        CHECK(p.read(0) == 0x02, "at once");
    }

    SECTION("intel8255: a mode set clears the output registers");
    {
        Intel8255 p;
        p.write(3, 0x80);
        p.write(0, 0xFF);
        p.write(1, 0xFF);
        p.write(2, 0xFF);
        p.write(3, 0x80);
        CHECK(p.outA() == 0 && p.outB() == 0 && p.outC() == 0, "even for the same mode");
    }

    SECTION("intel8255: port C bit set and reset");
    {
        Intel8255 p;
        p.write(3, 0x80);
        for (int bit = 0; bit < 8; ++bit) {
            p.write(3, (uint8_t)((bit << 1) | 1));
            CHECK(p.outC() == (uint8_t)(1 << bit), "set one bit");
            p.write(3, (uint8_t)(bit << 1));
            CHECK(p.outC() == 0, "reset it");
        }
        p.write(3, 0x70 | (3 << 1) | 1);
        CHECK(p.outC() == 0x08, "D6 to D4 are not looked at");
        CHECK(p.control() == 0x80, "and the mode stays");
    }

    SECTION("intel8255: the control register cannot be read");
    {
        Intel8255 p;
        p.write(3, 0x89);
        CHECK(p.read(3) == 0xFF, "the data bus stays 3-state");
    }

    SECTION("intel8255: modes 1 and 2 store the word and set the directions");
    {
        Intel8255 p;
        p.write(3, 0xA0);  // group A mode 1, A output
        CHECK(p.control() == 0xA0, "stored");
        p.write(0, 0x3C);
        CHECK(p.read(0) == 0x3C, "port A is an output");
    }

    SECTION("intel8255: snapshot");
    {
        Intel8255 a, b;
        a.write(3, 0x89);
        a.write(0, 0x11);
        a.write(1, 0x22);
        a.write(3, 0x01);
        StateWriter w;
        a.serialize(w);
        StateReader r(w.data());
        b.deserialize(r);
        CHECK(b.control() == 0x89, "the mode travels");
        CHECK(b.outA() == 0x11 && b.outB() == 0x22 && b.outC() == 0x01, "and the latches");
    }
}

void test_zcb() {
    SECTION("zcb: the serial port, with A1 not decoded");
    {
        Rig  r;
        auto s = r.line();
        r.out(0x07, 0x4E);  // the mode word through the upper pair
        r.out(0x05, 0x27);  // the command through the lower pair
        CHECK(r.z->usart().params().stopBits == 1, "07 reached the same chip as 05");
        r.out(0x06, 'A');
        while (!(r.in(0x07) & kTxRDY)) r.m.clock.advance(1);
        r.out(0x04, 'B');
        CHECK(s->out() == "AB", "04 and 06 are one data register");
        CHECK(r.in(0x05) == r.in(0x07), "05 and 07 are one status register");
    }

    SECTION("zcb: the status bits");
    {
        Rig  r;
        auto s = r.line();
        r.program();
        CHECK(r.in(0x05) == (kTxRDY | kTxEMPTY | kDSR), "idle: DSR is active with the line open");
        r.out(0x04, 'x');
        CHECK(r.in(0x05) == kDSR, "a byte in the transmitter");
        s->feed("Q");
        r.z->pump();
        r.m.clock.advance(100000);
        r.z->pump();
        CHECK(r.in(0x05) & kRxRDY, "a received byte");
        CHECK(r.in(0x04) == 'Q', "the data");
        CHECK(!(r.in(0x05) & kRxRDY), "and the read clears RxRDY");

        std::string err;
        auto        off = std::make_unique<NoDsr>();
        r.z->connectStream("serial", std::move(off), err);
        CHECK(!(r.in(0x05) & kDSR), "D7 reads the line");
    }

    SECTION("zcb: the board has no interrupt");
    {
        Rig  r;
        auto s = r.line();
        r.program();
        s->feed("Q");
        r.z->pump();
        r.m.clock.advance(100000);
        r.z->pump();
        CHECK(r.in(0x05) & kRxRDY, "a byte is waiting");
        CHECK(!r.m.bus.intPending(), "and pin 73 stays free");
    }

    SECTION("zcb: S1 gives the real rate, and the mode word divides it");
    {
        Rig  r;
        auto s = r.line();
        (void)s;
        std::string err;
        r.program(0x4E);  // 10 bits, factor 16
        CHECK(r.charTime() == 26 * 16 * 10, "the 9600 label: 2 MHz / 13 / 16, 416 T-states a bit");
        r.m.reset(Reset::Bus);
        r.program(0x4F);
        CHECK(r.charTime() == 26 * 64 * 10, "factor 64 is a quarter of the label");
        r.m.reset(Reset::Bus);
        r.program(0x4D);
        CHECK(r.charTime() == 26 * 10, "factor 1 is 16 times the label");

        CHECK(setUnitProperty(*r.z, "serial", "baud", "110", err), "the 110 rocker");
        r.m.reset(Reset::Bus);
        r.program(0x4E);
        CHECK(r.charTime() == 2288 * 16 * 10, "the 1200 rate over 11");
        CHECK(r.z->usart().params().baud == 110, "and a real port gets the label");
        CHECK(!setUnitProperty(*r.z, "serial", "baud", "19200", err), "S1 has eight rockers");
    }

    SECTION("zcb: the parallel port");
    {
        Rig r;
        CHECK(r.in(0x08) == 0xFF && r.in(0x09) == 0xFF && r.in(0x0A) == 0xFF, "J1 is open");
        CHECK(r.in(0x0B) == 0xFF, "the control register has no read");
        r.out(0x0B, 0x89);  // the printer driver's word
        r.out(0x08, 0x12);
        r.out(0x09, 0x34);
        CHECK(r.in(0x08) == 0x12, "port A is an output");
        CHECK(r.in(0x09) == 0x34, "port B too");
        CHECK(r.in(0x0A) == 0xFF, "port C is an input");
        r.out(0x0B, 0x80);
        r.out(0x0B, 0x0F);
        CHECK(r.in(0x0A) == 0x80, "bit set on port C");
    }

    SECTION("zcb: both chips move with their jumpers, and nothing stays behind");
    {
        Rig         r;
        auto        s = r.line();
        std::string err;
        CHECK(setProperty(*r.z, "group", "40", err), "group 40");
        CHECK(setProperty(*r.z, "serial_port", "10", err), "serial 10");
        CHECK(setProperty(*r.z, "parallel_port", "14", err), "parallel 14");
        r.out(0x51, 0xCE);
        r.out(0x51, 0x27);
        r.out(0x50, 'Z');
        CHECK(s->out() == "Z", "the 8251 is at 50-53");
        r.out(0x57, 0x80);
        r.out(0x54, 0x66);
        CHECK(r.in(0x54) == 0x66, "the 8255 is at 54-57");
        CHECK(r.m.bus.respondersTo({Cycle::IoRead, 0x04}).empty(), "04 floats");
        CHECK(r.m.bus.respondersTo({Cycle::IoRead, 0x08}).empty(), "08 floats");
        CHECK(r.m.bus.respondersTo({Cycle::IoRead, 0x58}).empty(), "58 is past the chip");
    }

    SECTION("zcb: the jumpers refuse what the board cannot do");
    {
        Rig         r;
        std::string err;
        CHECK(!setProperty(*r.z, "group", "10", err), "a group is 32 ports");
        CHECK(!setProperty(*r.z, "serial_port", "02", err), "a base is a multiple of 4");
        CHECK(!setProperty(*r.z, "serial_port", "20", err), "and stays in the group");
        CHECK(!setProperty(*r.z, "serial_port", "08", err), "two chips, two bases");
        CHECK(!setProperty(*r.z, "parallel_port", "04", err), "from either side");
        CHECK(!setProperty(*r.z, "wait", "two", err), "area N has three settings");
    }

    SECTION("zcb: the memory map");
    {
        Rig r;
        r.release();
        CHECK(r.rd(0xE000) == kPromFirst, "PROM at E000");
        CHECK(r.rd(0xEFFF) != 0xFF || r.rd(0xEFFE) != 0xFF, "through EFFF");
        CHECK(r.rd(0xF000) == 0xFF && r.m.bus.respondersTo({Cycle::MemRead, 0xF000}).empty(),
              "F000-FBFF is left to the bus");
        CHECK(r.m.bus.respondersTo({Cycle::MemRead, 0xFBFF}).empty(), "up to FBFF");
        r.wr(0xFC00, 0x5A);
        r.wr(0xFFFF, 0xA5);
        CHECK(r.rd(0xFC00) == 0x5A && r.rd(0xFFFF) == 0xA5, "RAM at FC00-FFFF");
        r.wr(0xE000, 0x00);
        CHECK(r.rd(0xE000) == kPromFirst, "a write does not reach the PROM");
        CHECK(!r.z->assertsPhantom({Cycle::MemRead, 0xE000}), "and no PHANTOM* in normal running");
    }

    SECTION("zcb: a write to the on-board RAM goes out on the bus too");
    {
        Machine     m;
        std::string err;
        auto*       z   = dynamic_cast<ZcbBoard*>(m.add("zcb", "zcb0", err));
        auto*       mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region      ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0xF000;
        ram.size = 0x1000;
        mem->addRegion(ram, err);
        m.power();
        m.bus.memRead(0xF000);  // releases the latch
        m.bus.memWrite(0xFC10, 0x77);
        uint8_t mine = 0, under = 0;
        CHECK(z->peek(0xFC10, mine) && mine == 0x77, "the on-board RAM has it");
        CHECK(mem->peek(0xFC10, under) && under == 0x77, "and so has the board under it");
    }

    SECTION("zcb: after a reset every address reads as the E000 block");
    {
        Rig r;
        r.wr(0x0100, 0x11);
        CHECK(r.z->armed(), "the latch is set at power, and a low write does not clear it");
        CHECK(r.rd(0x0000) == kPromFirst, "0000 reads E000");
        CHECK(r.rd(0x2000) == kPromFirst, "2000 reads E000");
        CHECK(r.rd(0xC000) == kPromFirst, "C000 reads E000");
        CHECK(r.rd(0x0001) == 0x15, "0001 reads E001");
        CHECK(r.z->assertsPhantom({Cycle::MemRead, 0x0000}), "with PHANTOM* held");
        r.in(0x05);
        CHECK(r.z->armed(), "an I/O cycle does not clear it");
    }

    SECTION("zcb: the first read in E000-FFFF ends the override");
    {
        Rig r;
        r.rd(0x0003);
        CHECK(r.z->armed(), "still set below the block");
        r.release();
        CHECK(!r.z->armed(), "clear after a read at E000");
        CHECK(!r.z->assertsPhantom({Cycle::MemRead, 0x0000}), "PHANTOM* is gone");
        r.wr(0x0000, 0x42);
        CHECK(r.rd(0x0000) == 0x42, "0000 is the RAM under it");
    }

    SECTION("zcb: the first write in E000-FFFF ends it too");
    {
        Rig r;
        r.wr(0xE123, 0);
        CHECK(!r.z->armed(), "a write to the PROM range");
        r.m.reset(Reset::Bus);
        CHECK(r.z->armed(), "a reset sets it again");
        r.wr(0xFC00, 0x99);
        CHECK(!r.z->armed(), "a write to the RAM");
        CHECK(r.rd(0xFC00) == 0x99, "and the byte went in");
    }

    SECTION("zcb: an address below the block writes the mirror of the RAM");
    {
        Rig r;
        r.wr(0x1C05, 0x3C);
        CHECK(r.z->armed(), "1C05 is not in the block");
        CHECK(r.rd(0x1C05) == 0x3C, "it reads back through the override");
        r.release();
        CHECK(r.rd(0xFC05) == 0x3C, "it landed at FC05");
    }

    SECTION("zcb: jumper B cut");
    {
        Rig         r;
        std::string err;
        setProperty(*r.z, "boot", "off", err);
        CHECK(r.z->armed(), "PHANTOM* still holds");
        CHECK(r.rd(0x0000) == 0xFF, "the RAM stands down and nothing drives 0000");
        r.release();
        CHECK(!r.z->armed(), "until a cycle in the block");
    }

    SECTION("zcb: jumper C cut");
    {
        Rig         r;
        std::string err;
        setProperty(*r.z, "phantom", "off", err);
        CHECK(!r.z->assertsPhantom({Cycle::MemRead, 0x0000}), "no PHANTOM*");
        CHECK(r.z->armed(), "the decode is still overridden");
    }

    SECTION("zcb: both jumpers cut boots from off the board");
    {
        Rig         r;
        std::string err;
        setProperty(*r.z, "boot", "off", err);
        setProperty(*r.z, "phantom", "off", err);
        r.wr(0x0000, 0x3E);
        CHECK(r.rd(0x0000) == 0x3E, "0000 is the RAM");
    }

    SECTION("zcb: a reset reaches both chips and spares the RAM");
    {
        Rig r;
        r.release();
        r.wr(0xFC20, 0x6B);
        r.program(0x4E);
        r.out(0x0B, 0x80);
        r.out(0x08, 0x55);
        r.m.reset(Reset::Bus);
        CHECK(r.in(0x08) == 0xFF, "port A is an input again");
        r.out(0x05, 0xCE);  // the next control write is a mode word
        CHECK(r.z->usart().params().stopBits == 2, "the 8251 expects a mode word");
        r.release();
        CHECK(r.rd(0xFC20) == 0x6B, "the RAM has its byte");

        r.m.power();
        CHECK(r.z->armed(), "power sets the latch");
    }

    SECTION("zcb: the wait states of area N");
    {
        constexpr uint8_t kFetch = StM1 | StMemR | StWo;
        Rig               r;
        std::string       err;
        r.m.bus.takeWaitStates();
        r.m.bus.memRead(0xE000, kFetch);
        CHECK(r.m.bus.takeWaitStates() == 1, "one after M1, as shipped");
        r.m.bus.memRead(0xE000);
        r.in(0x05);
        CHECK(r.m.bus.takeWaitStates() == 0, "none on any other cycle");
        r.m.bus.memRead(0x0100, kFetch);
        CHECK(r.m.bus.takeWaitStates() == 1, "and an off-board fetch has it too");

        setProperty(*r.z, "wait", "none", err);
        r.m.bus.memRead(0xE000, kFetch);
        CHECK(r.m.bus.takeWaitStates() == 0, "wait = none");

        setProperty(*r.z, "wait", "all", err);
        r.m.bus.memRead(0xE000, kFetch);
        r.m.bus.memRead(0xE000);
        r.in(0x05);
        CHECK(r.m.bus.takeWaitStates() == 3, "wait = all is one on each cycle");
    }

    SECTION("zcb: the sockets");
    {
        const std::string oneK = writeImage("zcb-test-1k.bin", 0x400, 0x11);
        const std::string twoK = writeImage("zcb-test-2k.bin", 0x800, 0x22);
        std::string       err;

        Machine m;
        auto*   z = dynamic_cast<ZcbBoard*>(m.add("zcb", "zcb0", err));
        CHECK(z->loadSubUnit("socket", {{"at", "E000"}, {"mount", twoK}}, err), "2K at E000");
        CHECK(z->loadSubUnit("socket", {{"at", "E800"}, {"mount", oneK}}, err), "1K at E800");
        CHECK(z->loadSubUnit("socket", {{"at", "EC00"}, {"mount", oneK}}, err), "1K at EC00");
        CHECK(!z->loadSubUnit("socket", {{"at", "E400"}, {"mount", oneK}}, err), "no fourth socket");
        m.power();
        m.bus.memRead(0xFC00);
        CHECK(m.bus.memRead(0xE7FF) == 0x22, "the 2K chip");
        CHECK(m.bus.memRead(0xE800) == 0x11, "the first 1K chip");
        CHECK(m.bus.memRead(0xEFFF) == 0x11, "the second 1K chip");
        CHECK(z->subUnits().size() == 3, "all three save");

        Machine m2;
        auto*   y = dynamic_cast<ZcbBoard*>(m2.add("zcb", "zcb0", err));
        CHECK(!y->loadSubUnit("socket", {{"at", "D000"}, {"mount", oneK}}, err), "below the range");
        CHECK(!y->loadSubUnit("socket", {{"at", "E100"}, {"mount", oneK}}, err), "not on a 1K line");
        CHECK(y->loadSubUnit("socket", {{"at", "E400"}, {"mount", twoK}}, err), "the file is read");
        CHECK(!y->drainLog().empty(), "and a 2K chip at E400 is reported");
        m2.power();
        m2.bus.memRead(0xFC00);
        CHECK(m2.bus.respondersTo({Cycle::MemRead, 0xE400}).empty(), "and it does not answer");

        std::filesystem::remove(oneK);
        std::filesystem::remove(twoK);
    }

    SECTION("zcb: the snapshot");
    {
        Rig src, dst;
        src.wr(0xFC40, 0x4D);  // this also clears the latch
        src.out(0x0B, 0x80);
        src.out(0x08, 0x12);
        src.program(0xCE);

        StateWriter w;
        src.z->serialize(w);
        StateReader rd(w.data());
        dst.z->deserialize(rd);
        CHECK(!dst.z->armed(), "the latch travels");
        CHECK(dst.rd(0xFC40) == 0x4D, "the RAM travels");
        CHECK(dst.in(0x08) == 0x12, "the 8255 travels");
        CHECK(dst.z->usart().params().stopBits == 2, "the 8251 travels");
    }

    SECTION("zcb: Monitor 4.3 starts from the reset at 0000");
    {
        Vector g;
        CHECK(g.m.cpu()->pc() == 0x0000, "the Z-80 starts at 0000");
        CHECK(g.run([&] { return g.screenHas("Mon>"); }, 2000000), "the Monitor comes up");
        CHECK(!g.z->armed(), "the jump ended the override");
        CHECK(g.m.cpu()->pc() >= 0xE000, "and the guest runs in the PROM");
        LineParams p = g.z->usart().params();
        CHECK(p.dataBits == 8 && p.stopBits == 2 && p.parity == LineParity::None,
              "INIT left mode CE, through 05 and 07");
        CHECK(g.m.cpu()->pc() < 0xF000, "the stack at FFD0 held");
    }

    SECTION("zcb: the Monitor's E command passes a byte both ways");
    {
        Vector      g;
        auto        line = std::make_unique<ScriptedStream>();
        auto*       s    = line.get();
        std::string err;
        g.z->connectStream("serial", std::move(line), err);
        CHECK(g.run([&] { return g.screenHas("Mon>"); }, 2000000), "the Monitor comes up");
        g.kbd->feed("E");
        CHECK(g.run([&] { return g.screenHas("EXT COM"); }, 2000000), "E prints EXT COM");
        s->feed("Q");
        CHECK(g.run([&] { return g.screenHas("Q"); }, 2000000), "the line reaches the screen");
        g.kbd->feed("W");
        CHECK(g.run([&] { return s->out().find('W') != std::string::npos; }, 2000000),
              "the keyboard reaches the line");
    }
}
