#include "test.h"

#include "boards/mits-884pio.h"
#include "boards/mits-88cpu.h"
#include "boards/s100-memory.h"
#include "core/machine.h"
#include "host/stream.h"

#include <cstdint>
#include <string>

using namespace altair;

namespace {

// A machine with an 88-4PIO (one populated 6820, port J), sections A and B each on
// a scripted line through the REAL connect path. Default base 20, so port J is at
// 20..23: 20 = A control, 21 = A data/DDR, 22 = B control, 23 = B data/DDR.
struct Rig {
    Machine         m;
    Pio4Board*      pio = nullptr;
    ScriptedStream* ja  = nullptr;  // port J, section A
    ScriptedStream* jb  = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);
        m.add("memory", "mem0", err);
        pio = dynamic_cast<Pio4Board*>(m.add("4pio", "pio0", err));
        pio->connect("ja", "scripted", err);
        pio->connect("jb", "scripted", err);
        ja = dynamic_cast<ScriptedStream*>(pio->unitStream("ja"));
        jb = dynamic_cast<ScriptedStream*>(pio->unitStream("jb"));
        m.add("8080", "cpu0", err);
        m.power();
    }

    uint8_t aCtrl() { return m.bus.ioRead(0x20); }
    uint8_t aData() { return m.bus.ioRead(0x21); }
    void    setACtrl(uint8_t b) { m.bus.ioWrite(0x20, b); }
    void    setAData(uint8_t b) { m.bus.ioWrite(0x21, b); }
};

constexpr uint8_t kDdrSelect = 0x04;  // control bit 2: 1 = data register, 0 = DDR
constexpr uint8_t kIrq1Flag  = 0x80;  // control bit 7: data available

} // namespace

void test_4pio() {
    SECTION("88-4PIO -- one populated port shows two sections; `ports` grows them");
    {
        Rig g;
        CHECK(g.pio->units().size() == 2, "one 6820 -> two sections");
        CHECK(g.pio->units()[0].name == "ja", "port J section A");
        CHECK(g.pio->units()[1].name == "jb", "port J section B");

        std::string err;
        CHECK(setProperty(*g.pio, "ports", "2", err), "populate a second 6820");
        CHECK(g.pio->units().size() == 4, "now four sections (ja jb ka kb)");
        CHECK(g.pio->units()[3].name == "kb", "port K section B appeared");
    }

    SECTION("88-4PIO -- decode: 16 addresses from a 16-aligned base");
    {
        Rig g;
        BusCycle c;
        c.type = Cycle::IoRead;
        for (uint8_t p = 0x20; p <= 0x23; ++p) {
            c.addr = p;
            CHECK(g.pio->decodes(c), "answers within one port's four addresses");
        }
        c.addr = 0x24;
        CHECK(!g.pio->decodes(c), "but not the second port -- only one is populated");

        std::string err;
        CHECK(!setProperty(*g.pio, "port", "28", err), "a base off the 16-boundary is refused");
        CHECK(err.find("16") != std::string::npos, "and it says why");
        CHECK(setProperty(*g.pio, "port", "30", err), "an aligned base is taken");
        c.addr = 0x30;
        CHECK(g.pio->decodes(c), "the card moved with its base");
    }

    SECTION("88-4PIO -- the DDR is reached through the data address when control bit 2 is 0");
    {
        Rig g;
        // Out of reset control = 0, so the data address is the DDR.
        g.setAData(0xFF);
        CHECK(g.aData() == 0xFF, "wrote and read the DDR (all lines outputs)");

        // Flip control bit 2: now the data address is the DATA register.
        g.setACtrl(kDdrSelect);
        CHECK((g.aCtrl() & kDdrSelect) != 0, "control bit 2 read back set");
        g.setAData(0x00);  // this is now a data write, not a DDR write
        CHECK(g.aData() != 0xFF, "the data address no longer reaches the DDR");
    }

    SECTION("88-4PIO -- a byte written to a section's data register goes out on its line");
    {
        Rig g;
        g.setACtrl(kDdrSelect);  // select the data register
        g.setAData('X');
        g.setAData('Y');
        CHECK(g.ja->out() == "XY", "the bytes reached section A's device, in order");
    }

    SECTION("88-4PIO -- an input byte sets status bit 7 and is read from the data register");
    {
        Rig g;
        g.setACtrl(kDdrSelect);  // data register selected
        CHECK((g.aCtrl() & kIrq1Flag) == 0, "no data yet -- bit 7 clear");

        g.ja->feed("Z");
        g.m.pump();
        CHECK((g.aCtrl() & kIrq1Flag) != 0, "after pump, bit 7 SET -- data available");
        CHECK(g.aData() == 'Z', "the data register hands the byte over");
        CHECK((g.aCtrl() & kIrq1Flag) == 0, "reading the data register clears bit 7 (6820)");
    }

    SECTION("88-4PIO -- the two sections are independent");
    {
        Rig g;
        g.m.bus.ioWrite(0x20, kDdrSelect);  // A: data register
        g.m.bus.ioWrite(0x22, kDdrSelect);  // B: data register
        g.m.bus.ioWrite(0x21, 'A');         // A data
        g.m.bus.ioWrite(0x23, 'B');         // B data
        CHECK(g.ja->out() == "A", "section A got its byte");
        CHECK(g.jb->out() == "B", "section B got its own");
    }

    SECTION("88-4PIO -- polled: no interrupt wire is pulled");
    {
        Rig g;
        g.setACtrl(0x2D);  // some C1/C2 enable bits set
        g.ja->feed("!");
        g.m.pump();
        CHECK(!g.m.bus.intPending(), "a ready section pulls no wire -- the card is polled");
    }

    SECTION("88-4PIO -- connect round-trips, disconnect leaves a dead line");
    {
        Rig g;
        std::string err;
        CHECK(g.pio->disconnect("jb", err), "unplug section B");
        CHECK(g.pio->units()[1].state == "null", "the unit reports a dead line");
        CHECK(!g.pio->connect("zz", "null", err), "no such section");
        CHECK(err.find("ja") != std::string::npos, "the error names a real one");

        std::string got = "(none)";
        for (Property& p : g.pio->unitProperties("ja"))
            if (p.name == "connect") got = p.get().text(p.radix);
        CHECK(got == "scripted", "the per-unit connect property round-trips");
    }
}
