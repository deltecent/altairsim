#include "test.h"

#include "boards/mits-88cpu.h"
#include "boards/mits-88pio.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/stream.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

// A machine with an 88-PIO in it, its output device and its input device each on a
// scripted line -- bound through the REAL connect path, so the test exercises the
// same wiring an operator's CONNECT does. The output is read back with out(); input
// is typed with feed() and delivered to the card on m.pump().
struct Rig {
    Machine         m;
    PioBoard*       pio = nullptr;
    ScriptedStream* out = nullptr;  // the output device
    ScriptedStream* in  = nullptr;  // the input device

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        m.add("memory", "mem0", err);
        pio = dynamic_cast<PioBoard*>(m.add("pio", "pio0", err));
        pio->connect("out", "scripted", err);
        pio->connect("in", "scripted", err);
        out = dynamic_cast<ScriptedStream*>(pio->unitStream("out"));
        in  = dynamic_cast<ScriptedStream*>(pio->unitStream("in"));
        m.add("8080", "cpu0", err);
        m.power();
    }

    uint8_t status() { return m.bus.ioRead(0x04); }         // IN  04 -- DI0/DI1
    uint8_t readData() { return m.bus.ioRead(0x05); }       // IN  05 -- the input latch
    void    send(uint8_t b) { m.bus.ioWrite(0x05, b); }     // OUT 05 -- to the output device
    void    control(uint8_t b) { m.bus.ioWrite(0x04, b); }  // OUT 04 -- interrupt enable
};

constexpr uint8_t kOutReady = 0x01;  // DI0
constexpr uint8_t kInFull   = 0x02;  // DI1

} // namespace

void test_pio() {
    SECTION("88-PIO -- two lines, an output device and an input device");
    {
        Rig g;
        CHECK(g.pio->units().size() == 2, "two connectors: an output and an input");
        CHECK(g.pio->units()[0].name == "out", "the output device is 'out'");
        CHECK(g.pio->units()[1].name == "in", "the input device is 'in'");
        CHECK(g.pio->units()[0].kind == UnitKind::Serial, "serial units");
    }

    SECTION("88-PIO -- decode: an even/odd pair, BOTH directions on BOTH ports");
    {
        Rig g;
        BusCycle c;
        // Unlike the C700, the PIO has an input latch, so IN at the data port is a
        // real read the card drives -- both ports answer both directions.
        for (uint8_t port : {(uint8_t)0x04, (uint8_t)0x05}) {
            c.addr = port;
            c.type = Cycle::IoRead;
            CHECK(g.pio->decodes(c), "decodes the IN");
            c.type = Cycle::IoWrite;
            CHECK(g.pio->decodes(c), "decodes the OUT");
        }
        c.type = Cycle::IoRead;
        c.addr = 0x06;
        CHECK(!g.pio->decodes(c), "does NOT decode the port after them");
        c.type = Cycle::MemRead;
        c.addr = 0x04;
        CHECK(!g.pio->decodes(c), "decodes no MEMORY -- it is an I/O card");

        // A0 picks the channel, so an odd base is not a card you could build.
        std::string err;
        CHECK(!setProperty(*g.pio, "port", "05", err), "an ODD base is refused");
        CHECK(err.find("even") != std::string::npos, "and it says why, in words");
        CHECK(setProperty(*g.pio, "port", "10", err), "an even base is taken");
        c.type = Cycle::IoRead;
        c.addr = 0x10;
        CHECK(g.pio->decodes(c), "and the card MOVED with its base");
        c.addr = 0x04;
        CHECK(!g.pio->decodes(c), "and no longer answers where it was");
    }

    SECTION("88-PIO -- status: DI0 out-ready is ACTIVE HIGH, DI1 in-full follows a byte");
    {
        Rig g;
        uint8_t s = g.status();
        CHECK((s & kOutReady) != 0, "idle: DI0 SET -- the output device will take a byte");
        CHECK((s & kInFull) == 0, "and DI1 CLEAR -- no input byte waiting");
    }

    SECTION("88-PIO -- a byte written to the data port lands on the output line");
    {
        Rig g;
        g.send('H');
        g.send('i');
        CHECK(g.out->out() == "Hi", "the bytes reached the output device, in order");

        // RAW, 8-bit clean: control codes are data to this card.
        g.out->clearOut();
        const char* line = "AB\r\n\x0E\x7F";
        for (const char* p = line; *p; ++p) g.send((uint8_t)*p);
        CHECK(g.out->out() == std::string(line), "every byte, control codes and all, verbatim");
    }

    SECTION("88-PIO -- a byte from the input device is latched and read back");
    {
        Rig g;
        CHECK((g.status() & kInFull) == 0, "nothing waiting yet");

        g.in->feed("Q");
        g.m.pump();  // the one door the outside world comes through
        CHECK((g.status() & kInFull) != 0, "after pump, DI1 SET -- a byte arrived");
        CHECK(g.readData() == 'Q', "and the data port hands it over");
        CHECK((g.status() & kInFull) == 0, "reading the data port empties the latch (DI1 clear)");

        // The latch is one byte deep -- the next byte arrives on the next pump.
        g.in->feed("RS");
        g.m.pump();
        CHECK(g.readData() == 'R', "first byte");
        g.m.pump();
        CHECK(g.readData() == 'S', "then the second, one pump each");
    }

    SECTION("88-PIO -- the control channel stores the interrupt-enable bits, polled");
    {
        Rig g;
        g.control(0x03);  // DO0 | DO1 -- enable both
        // POLLED: no request is raised and no wire is pulled.
        CHECK(!g.m.bus.intPending(), "enabling interrupts pulls no wire -- the card is polled");
        // The status word has no interrupt bit; it stays DI0/DI1 only.
        CHECK((g.status() & ~(kOutReady | kInFull)) == 0, "status is DI0/DI1 and nothing else");
    }

    SECTION("88-PIO -- connect round-trips, disconnect leaves a dead line");
    {
        Rig g;
        std::string err;
        CHECK(g.pio->disconnect("in", err), "the input device is unplugged");
        CHECK(g.pio->units()[1].state == "null", "and the unit reports a dead line");

        CHECK(g.pio->connect("out", "null", err), "connect out to null");
        // Read the per-unit `connect` property back, as CONFIG SAVE does.
        std::string got = "(none)";
        for (Property& p : g.pio->unitProperties("out"))
            if (p.name == "connect") got = p.get().text(p.radix);
        CHECK(got == "null", "the per-unit connect property round-trips");

        CHECK(!g.pio->connect("bogus", "null", err), "there is no unit but out/in");
        CHECK(err.find("out") != std::string::npos, "the error names the real ones");
    }
}
