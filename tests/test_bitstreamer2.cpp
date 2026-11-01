// The Vector Graphic Bitstreamer II (docs/boards/vector-bitstreamer2.md).
//
// Real bus cycles through the decode. The clock keeps real time (issue #825), so a test sets the
// host time and pumps the board, as the run loop does once a slice.

#include "boards/s100-memory.h"
#include "boards/vector-bitstreamer2.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/stream.h"
#include "test.h"

#include <cstdint>
#include <memory>
#include <string>

using namespace altair;

namespace {

constexpr uint64_t kClockNs = 36608ull * 500;  // 2 MHz / (13 x 4 x 22 x 16 x 2)

struct Rig {
    Machine           m;
    Bitstreamer2Board* bs = nullptr;
    uint64_t          hostNs = 1000000000;

    explicit Rig(const std::string& clockInt = "none", const std::string& j4 = "open") {
        std::string err;
        m.bus.setVerify(true);
        m.add("z80", "cpu0", err);
        bs = dynamic_cast<Bitstreamer2Board*>(m.add("vgbsii", "bs0", err));
        m.bus.busClock().setHostNs([this] { return hostNs; });
        setProperty(*bs, "port", "22", err);
        setProperty(*bs, "clock_int", clockInt, err);
        setProperty(*bs, "j4_d0", j4, err);
        m.power();
    }

    uint8_t in(uint8_t p) { return m.bus.ioRead(p); }
    void    out(uint8_t p, uint8_t v) { m.bus.ioWrite(p, v); }

    // A slice of the run loop, after `ns` of host time.
    void pass(uint64_t ns) {
        hostNs += ns;
        bs->pump();
    }

    ScriptedStream* line(const std::string& unit) {
        auto        s = std::make_unique<ScriptedStream>();
        auto*       p = s.get();
        std::string err;
        bs->connectStream(unit, std::move(s), err);
        return p;
    }

    // The mode word, then a command with TxEN and RxE.
    void program(uint8_t status) {
        out(status, 0x4E);
        out(status, 0x05);
    }
};

}  // namespace

void test_bitstreamer2() {
    SECTION("vgbsii: the port map");
    {
        Rig r;
        CHECK(r.in(0x23) == 0x05, "a fresh 8251: TxRDY and TxEMPTY");
        CHECK(r.in(0x25) == 0x05, "r.in(0x25) == 0x05");
        CHECK(r.in(0x27) == 0x05, "r.in(0x27) == 0x05");
        CHECK(r.in(0x21) == 0xFF, "x0, x1 and xA-xF stay free");
        CHECK(r.in(0x2A) == 0xFF, "r.in(0x2A) == 0xFF");

        std::string err;
        CHECK(setProperty(*r.bs, "port", "72", err), "setProperty(*r.bs, \"port\", \"72\", err)");
        CHECK(r.in(0x73) == 0x05, "r.in(0x73) == 0x05");
        CHECK(r.in(0x23) == 0xFF, "r.in(0x23) == 0xFF");
    }

    SECTION("vgbsii: the base ends in 2 and stays below 80");
    {
        Rig         r;
        std::string err;
        CHECK(!setProperty(*r.bs, "port", "20", err), "!setProperty(*r.bs, \"port\", \"20\", err)");
        CHECK(!setProperty(*r.bs, "port", "23", err), "!setProperty(*r.bs, \"port\", \"23\", err)");
        CHECK(!setProperty(*r.bs, "port", "82", err), "!setProperty(*r.bs, \"port\", \"82\", err)");
        CHECK(setProperty(*r.bs, "port", "02", err), "setProperty(*r.bs, \"port\", \"02\", err)");
        CHECK(!setProperty(*r.bs, "j4_d0", "wire", err), "!setProperty(*r.bs, \"j4_d0\", \"wire\", err)");
        CHECK(!setUnitProperty(*r.bs, "a", "baud", "19200", err), "the switch has no 19200");
    }

    SECTION("vgbsii: open parallel inputs read 1");
    {
        Rig r;
        r.out(0x28, 0x40);
        CHECK(r.in(0x28) == 0xFF, "r.in(0x28) == 0xFF");
        CHECK(r.in(0x29) == 0xFF, "r.in(0x29) == 0xFF");
    }

    SECTION("vgbsii: the clock period comes from the divider chain");
    {
        Rig r("int");
        r.pass(kClockNs - 1);
        CHECK(!r.bs->clockLatch(), "!r.bs->clockLatch()");
        r.pass(1);
        CHECK(r.bs->clockLatch(), "r.bs->clockLatch()");
    }

    SECTION("vgbsii: only IN x9 clears the latch, and the data does not matter");
    {
        Rig r("int");
        r.pass(kClockNs);
        CHECK(r.bs->clockLatch(), "r.bs->clockLatch()");
        r.in(0x28);
        r.in(0x23);
        r.out(0x29, 0x00);
        r.out(0x28, 0x00);
        CHECK(r.bs->clockLatch(), "r.bs->clockLatch()");
        r.in(0x29);
        CHECK(!r.bs->clockLatch(), "!r.bs->clockLatch()");
        r.pass(kClockNs);
        CHECK(r.bs->clockLatch(), "r.bs->clockLatch()");
    }

    SECTION("vgbsii: pin 73 is the latch through mask bit 6");
    {
        Rig r("int");
        r.pass(kClockNs);
        CHECK(r.bs->clockLatch(), "r.bs->clockLatch()");
        CHECK(!r.m.bus.intPending(), "the mask gates the pin, not the latch");

        r.out(0x28, 0xBF);  // every bit but 6
        CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
        r.out(0x28, 0x40);
        CHECK(r.m.bus.intPending(), "unmasking a set latch interrupts at once");
        r.in(0x29);
        CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
        r.pass(kClockNs);
        CHECK(r.m.bus.intPending(), "r.m.bus.intPending()");
    }

    SECTION("vgbsii: a VI jumper drives its line and not pin 73");
    {
        Rig r("vi3");
        r.out(0x28, 0x40);
        r.pass(kClockNs);
        CHECK(r.bs->assertsVi() == 0x08, "r.bs->assertsVi() == 0x08");
        CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
        r.in(0x29);
        CHECK(r.bs->assertsVi() == 0, "r.bs->assertsVi() == 0");
    }

    SECTION("vgbsii: J4 D0 follows the latch when jumpered");
    {
        Rig r("none", "clock-latch");
        CHECK(r.in(0x28) == 0xFE, "latch clear, mask off: low, the mask does not reach U4 pin 5");
        r.pass(kClockNs);
        CHECK(r.in(0x28) == 0xFF, "latch set, mask off: high");
        r.in(0x29);
        CHECK(r.in(0x28) == 0xFE, "the IN clears the latch");
        r.out(0x28, 0x40);
        CHECK(r.in(0x28) == 0xFE, "latch clear, mask on: low");
        r.pass(kClockNs);
        CHECK(r.in(0x28) == 0xFF, "latch set, mask on: high");
        r.in(0x29);
        CHECK(r.in(0x28) == 0xFE, "r.in(0x28) == 0xFE");
        CHECK(!r.m.bus.intPending(), "the jumper is an input, it drives no pin");
    }

    SECTION("vgbsii: no wire, no deadline");
    {
        Rig r;
        CHECK(r.m.clock.queued() == 0, "r.m.clock.queued() == 0");
        r.pass(kClockNs * 3);
        CHECK(!r.bs->clockLatch(), "!r.bs->clockLatch()");

        Rig wired("int");
        CHECK(wired.m.clock.queued() != 0, "wired.m.clock.queued() != 0");
        Rig input("none", "clock-latch");
        CHECK(input.m.clock.queued() != 0, "input.m.clock.queued() != 0");

        Rig         later;
        std::string err;
        CHECK(setProperty(*later.bs, "j4_d0", "clock-latch", err), "the jumper can be set while running");
        CHECK(later.m.clock.queued() != 0, "a jumper set while running starts the clock");
        CHECK(setProperty(*later.bs, "j4_d0", "open", err), "the jumper comes off");
        CHECK(later.m.clock.queued() == 0, "and the deadline goes with it");
        CHECK(setProperty(*later.bs, "clock_int", "int", err), "clock_int can be set while running");
        CHECK(later.m.clock.queued() != 0, "clock_int starts the clock");
        CHECK(setProperty(*later.bs, "clock_int", "none", err), "clock_int comes off");
        CHECK(later.m.clock.queued() == 0, "and the deadline goes with it");
    }

    SECTION("vgbsii: the clock survives the move from a scratch machine");
    {
        uint64_t    hostNs = 1000000000;
        Machine     m;
        Machine     scratch;
        std::string err;
        scratch.add("z80", "cpu0", err);
        auto* bs = dynamic_cast<Bitstreamer2Board*>(scratch.add("vgbsii", "bs0", err));
        setProperty(*bs, "port", "22", err);
        setProperty(*bs, "clock_int", "int", err);
        scratch.power();
        m.bus.busClock().setHostNs([&] { return hostNs; });
        m.replaceWith(scratch);
        CHECK(m.clock.queued() != 0, "the deadline is on the machine's clock, so a HLT can end");

        bs->pump();
        m.bus.ioRead(0x29);
        hostNs += kClockNs;
        bs->pump();
        CHECK(bs->clockLatch(), "the host time comes from the new bus");
    }

    SECTION("vgbsii: RESET* reaches nothing, POC* clears");
    {
        Rig r("int");
        r.out(0x28, 0x40);
        r.pass(kClockNs);
        r.m.reset(Reset::Bus);
        CHECK(r.m.bus.intPending(), "RESET* leaves the mask and the latch");

        r.m.reset(Reset::PowerOn);
        CHECK(!r.bs->clockLatch(), "!r.bs->clockLatch()");
        CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
    }

    SECTION("vgbsii: serial data and status at each channel");
    {
        Rig  r;
        auto a = r.line("a");
        auto b = r.line("b");
        auto c = r.line("c");
        r.program(0x23);
        r.program(0x25);
        r.program(0x27);
        r.out(0x22, 'A');
        r.m.clock.advance(1000000);
        r.out(0x24, 'B');
        r.out(0x26, 'C');
        CHECK(a->out() == "A", "a->out() == \"A\"");
        CHECK(b->out() == "B", "b->out() == \"B\"");
        CHECK(c->out() == "C", "c->out() == \"C\"");

        r.m.clock.advance(1000000);
        c->feed("x");
        r.in(0x27);                  // starts the frame
        r.m.clock.advance(1000000);  // and it ends
        CHECK((r.in(0x27) & 0x02) != 0, "(r.in(0x27) & 0x02) != 0");
        CHECK(r.in(0x26) == 'x', "r.in(0x26) == 'x'");
        CHECK((r.in(0x27) & 0x02) == 0, "(r.in(0x27) & 0x02) == 0");
    }

    SECTION("vgbsii: each serial source has its own mask bit");
    {
        struct Case {
            const char* unit;
            uint8_t     status, data, rxBit, txBit;
        };
        const Case cases[] = {{"a", 0x23, 0x22, 0x02, 0x01},
                              {"b", 0x25, 0x24, 0x08, 0x04},
                              {"c", 0x27, 0x26, 0x20, 0x10}};
        for (const Case& k : cases) {
            Rig         r;
            std::string err;
            auto        s = r.line(k.unit);
            CHECK(setUnitProperty(*r.bs, k.unit, "rx_int", "int", err), "rx_int takes int");
            r.program(k.status);
            s->feed("x");
            r.bs->pump();
            r.m.clock.advance(1000000);
            r.bs->pump();

            r.out(0x28, (uint8_t)~k.rxBit);
            CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
            r.out(0x28, k.rxBit);
            CHECK(r.m.bus.intPending(), "r.m.bus.intPending()");
            r.in(k.data);
            CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
        }
        for (const Case& k : cases) {
            Rig         r;
            std::string err;
            r.line(k.unit);
            CHECK(setUnitProperty(*r.bs, k.unit, "tx_int", "int", err), "tx_int takes int");
            r.out(0x28, (uint8_t)~k.txBit);
            r.out(k.status, 0x4E);
            r.out(k.status, 0x04);  // RxE only
            r.out(0x28, k.txBit);
            CHECK(!r.m.bus.intPending(), "the pin needs TxEN");
            r.out(k.status, 0x05);
            CHECK(r.m.bus.intPending(), "r.m.bus.intPending()");

            // TxRDY falls with a byte, and comes back with no bus cycle and no pump.
            r.out(k.data, 'x');
            CHECK(!r.m.bus.intPending(), "!r.m.bus.intPending()");
            r.m.clock.advance(1000000);
            CHECK(r.m.bus.intPending(), "r.m.bus.intPending()");
        }
    }

    SECTION("vgbsii: the unit properties");
    {
        Rig         r;
        std::string err;
        CHECK(setUnitProperty(*r.bs, "c", "baud", "1200", err), "the switch has 1200");
        CHECK(setUnitProperty(*r.bs, "a", "connect", "null", err), "connect takes null");
        CHECK(!setUnitProperty(*r.bs, "a", "connect", "nosuch:thing", err), "connect refuses a bad endpoint");
        std::string v;
        for (auto& p : r.bs->unitProperties("c"))
            if (p.name == "baud") v = p.get().s();
        CHECK(v == "1200", "v == \"1200\"");
    }

    SECTION("vgbsii: snapshot");
    {
        Rig  src("int", "clock-latch");
        Rig  dst("int", "clock-latch");
        auto s = src.line("a");
        (void)s;
        src.program(0x23);
        src.out(0x28, 0x40);
        src.pass(kClockNs);
        src.out(0x29, 0x5A);

        StateWriter w;
        src.bs->serialize(w);
        StateReader rd(w.data());
        dst.bs->deserialize(rd);

        CHECK(dst.bs->clockLatch(), "dst.bs->clockLatch()");
        CHECK(dst.m.bus.intPending(), "dst.m.bus.intPending()");
        CHECK(dst.in(0x28) == 0xFF, "dst.in(0x28) == 0xFF");
        dst.in(0x29);
        CHECK(!dst.m.bus.intPending(), "!dst.m.bus.intPending()");
        CHECK(dst.in(0x28) == 0xFE, "dst.in(0x28) == 0xFE");
        CHECK(dst.m.clock.queued() != 0, "the host schedule starts again");
        dst.pass(kClockNs);
        CHECK(dst.bs->clockLatch(), "dst.bs->clockLatch()");
    }

    SECTION("vgbsii: a guest takes the clock and returns");
    {
        Machine     m;
        std::string err;
        uint64_t    hostNs = 1000000000;
        m.bus.setVerify(true);
        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0;
        ram.size = 0x10000;
        mem->addRegion(ram, err);
        setProperty(*mem, "fill", "zero", err);
        m.add("z80", "cpu0", err);
        auto* bs = dynamic_cast<Bitstreamer2Board*>(m.add("vgbsii", "bs0", err));
        m.bus.busClock().setHostNs([&] { return hostNs; });
        setProperty(*bs, "port", "22", err);
        setProperty(*bs, "clock_int", "int", err);
        setProperty(*bs, "j4_d0", "clock-latch", err);
        m.power();

        // No board answers the acknowledge, so it floats to RST 7. The routine counts at 0100
        // only when bit 0 of IN 28 says the clock was the source, as OASIS does.
        const uint8_t prog[] = {
            0xED, 0x56,        // 0000  IM 1
            0x31, 0x00, 0x80,  // 0002  LD SP,8000
            0x3E, 0x40,        // 0005  LD A,40
            0xD3, 0x28,        // 0007  OUT (28),A
            0xFB,              // 0009  EI
            0x18, 0xFE,        // 000A  JR $
        };
        const uint8_t isr[] = {
            0xDB, 0x28,        // 0038  IN A,(28)
            0xCB, 0x47,        // 003A  BIT 0,A
            0x28, 0x07,        // 003C  JR Z,+7
            0xDB, 0x29,        // 003E  IN A,(29)
            0x21, 0x00, 0x01,  // 0040  LD HL,0100
            0x34,              // 0043  INC (HL)
            0xFB,              // 0044  EI
            0xED, 0x4D,        // 0045  RETI
        };
        for (size_t i = 0; i < sizeof prog; ++i) m.bus.memWrite((uint16_t)i, prog[i]);
        for (size_t i = 0; i < sizeof isr; ++i) m.bus.memWrite((uint16_t)(0x38 + i), isr[i]);
        m.cpu()->setPc(0);

        auto run = [&] {
            for (int i = 0; i < 2000; ++i) m.master()->step(m.bus);
        };
        run();
        CHECK(m.bus.memRead(0x0100) == 0, "m.bus.memRead(0x0100) == 0");
        for (int tick = 1; tick <= 3; ++tick) {
            hostNs += kClockNs;
            bs->pump();
            run();
            CHECK(m.bus.memRead(0x0100) == tick, "one tick, one pass: no storm");
        }
    }
}
