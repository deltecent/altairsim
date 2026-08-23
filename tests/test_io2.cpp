#include "test.h"

#include "boards/io2.h"
#include "core/board.h"
#include "host/endpoint.h"
#include "host/stream.h"

using namespace altair;

namespace {

// An IO-2 on the bench with a scripted line on its one unit. No Machine and no Clock:
// the card is polled and schedules nothing, so a bus cycle is all it needs. The stream
// is bound through the REAL connect path (resolveEndpoint("scripted")), the same wiring
// an operator's CONNECT drives -- then the test feed()s bytes at the card and reads what
// the guest sent out of out(). setResolver() is installed once in tests/main.cpp.
struct Rig {
    Io2Board        b;
    ScriptedStream* line = nullptr;

    Rig() {
        std::string err;
        CHECK(b.connect("serial", "scripted", err), "the scripted line connects");
        line = dynamic_cast<ScriptedStream*>(b.unitStream("serial"));
        CHECK(line != nullptr, "and it is a ScriptedStream we can drive");
    }

    // Read/write a port the way the bus does -- a cycle, not a method call.
    uint8_t in(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.read(c);
    }
    void out(uint8_t port, uint8_t v) {
        BusCycle c;
        c.type = Cycle::IoWrite;
        c.addr = port;
        c.data = v;
        b.write(c);
    }

    uint8_t status() { return in(straps().statusPort); }
    uint8_t data()   { return in(straps().dataPort); }

    // The live straps, read back through the property layer -- what SHOW would print.
    Io2Profile straps() {
        Io2Profile p;
        p.statusPort = (uint8_t)get("status_port");
        p.dataPort   = (uint8_t)get("data_port");
        p.davBit     = (uint8_t)get("dav");
        p.tbmtBit    = (uint8_t)get("tbmt");
        return p;
    }
    long long get(const std::string& key) {
        for (auto& pr : b.properties())
            if (pr.name == key) return pr.get().i();
        return -1;
    }
    void set(const std::string& key, const std::string& v) {
        std::string err;
        CHECK(setProperty(b, key, v, err), ("SET " + key + "=" + v).c_str());
    }
};

} // namespace

void test_io2() {
    SECTION("IO-2 -- one serial unit, called 'serial'");
    {
        Rig g;
        CHECK(g.b.units().size() == 1, "one unit -- a single-port serial card has one line");
        CHECK(g.b.units()[0].name == "serial", "named 'serial'");
        CHECK(g.b.units()[0].kind == UnitKind::Serial, "a serial unit");

        std::string err;
        CHECK(!g.b.connect("tty", "null", err), "there is no unit but 'serial'");
    }

    SECTION("IO-2 -- it comes up as sior0, the SSM 8080 monitor console");
    {
        // A bare `type=\"io2\"` board is the SSM IO-2's own MITS-SIO-Rev-0 personality:
        // status/data at 00/01, DAV in bit 0, TBMT in bit 7, inverter gate ON (both bits
        // active low) -- exactly what roms/SSM-8080MON/SSM_8080MonV10.asm polls.
        Rig g;
        CHECK(g.get("status_port") == 0x00, "sior0: status at BASE+0");
        CHECK(g.get("data_port") == 0x01, "sior0: data at BASE+1");
        CHECK(g.get("dav") == 0, "sior0: DAV is status bit 0");
        CHECK(g.get("tbmt") == 7, "sior0: TBMT is status bit 7");

        // Inverter gate on: an idle line reads DAV=1 (nothing waiting) and TBMT=0 (ready).
        uint8_t s = g.status();
        CHECK((s & 0x01) != 0, "sior0: DAV reads 1 when nothing is waiting (active low)");
        CHECK((s & 0x80) == 0, "sior0: TBMT reads 0 when ready to send (active low)");
        g.line->feed("A");
        s = g.status();
        CHECK((s & 0x01) == 0, "sior0: DAV reads 0 once a character is waiting");
    }

    SECTION("IO-2 -- it decodes ONLY its two strapped ports");
    {
        Rig g;
        g.set("status_port", "10");  // hex
        g.set("data_port", "11");

        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x10;
        CHECK(g.b.decodes(c), "answers the status port");
        c.addr = 0x11;
        CHECK(g.b.decodes(c), "answers the data port");
        c.addr = 0x12;
        CHECK(!g.b.decodes(c), "and nothing else -- a third port belongs to another card");
        c.addr = 0x0F;
        CHECK(!g.b.decodes(c), "nor the port before them");

        // It is an I/O card. It answers no memory address at all.
        c.type = Cycle::MemRead;
        c.addr = 0x10;
        CHECK(!g.b.decodes(c), "decodes no MEMORY -- it is not there");
    }

    SECTION("IO-2 -- the status byte is synthesized at the strapped bit positions");
    {
        Rig g;
        // A neutral custom strap: status 0x00, data 0x01, DAV in bit 0, TBMT in bit 1,
        // inverter gate off. A ScriptedStream is always writable(), so TBMT is asserted.
        g.set("status_port", "00");
        g.set("data_port", "01");
        g.set("dav", "0");
        g.set("tbmt", "1");
        g.set("inverter_gate", "false");

        uint8_t s = g.status();
        CHECK((s & 0x01) == 0, "DAV clear -- nobody has typed");
        CHECK((s & 0x02) != 0, "TBMT set -- the line is ready to send");
        CHECK((s & ~0x03) == 0, "and every OTHER bit is 0 -- nothing bleeds in");

        g.line->feed("A");
        s = g.status();
        CHECK((s & 0x01) != 0, "DAV sets once a character is on the line");
        CHECK((s & 0x02) != 0, "TBMT is still set");
    }

    SECTION("IO-2 -- a bit position is a jumper: put DAV/TBMT anywhere");
    {
        Rig g;
        g.set("dav", "6");
        g.set("tbmt", "3");
        g.set("inverter_gate", "false");

        CHECK((g.status() & (1 << 3)) != 0, "TBMT lands in bit 3, where we put it");
        CHECK((g.status() & (1 << 6)) == 0, "DAV quiet in bit 6");
        g.line->feed("Q");
        CHECK((g.status() & (1 << 6)) != 0, "...and sets in bit 6 when a character arrives");
    }

    SECTION("IO-2 -- the inverter gate inverts BOTH status bits together");
    {
        Rig g;
        g.set("dav", "0");
        g.set("tbmt", "1");
        g.set("inverter_gate", "true");   // one knob -- both bits, asserted reads 0

        // Nothing fed: DAV is NOT asserted (line quiet) so the gate reads it as 1;
        // TBMT IS asserted (writable) so the gate reads it as 0. One knob moved both.
        uint8_t s = g.status();
        CHECK((s & 0x01) != 0, "gate on, DAV de-asserted reads 1");
        CHECK((s & 0x02) == 0, "gate on, TBMT asserted reads 0");

        g.line->feed("Z");
        s = g.status();
        CHECK((s & 0x01) == 0, "gate on, DAV asserted (char waiting) reads 0");
    }

    SECTION("IO-2 -- the data path, both directions");
    {
        Rig g;
        g.set("data_port", "01");

        // Receive: a fed byte reaches the guest and clears DAV.
        g.set("dav", "0");
        g.set("inverter_gate", "false");
        g.line->feed("K");
        CHECK((g.status() & 0x01) != 0, "a character is waiting");
        CHECK(g.data() == 'K', "the data port yields it");
        CHECK((g.status() & 0x01) == 0, "and reading it clears DAV");
        CHECK(g.b.rxBytes() == 1, "the byte is counted -- rxBytes() saw a transfer arrive");

        // A quiet line reads 0 and is NOT counted.
        CHECK(g.data() == 0, "a quiet line reads 0");
        CHECK(g.b.rxBytes() == 1, "...and an empty read is not a delivered byte");

        // Transmit: a write to the data port lands on the line.
        g.out(0x01, 'X');
        CHECK(g.line->out() == "X", "a write to the data port goes out the line");
    }

    SECTION("IO-2 -- a write to the status/control port is DISCARDED");
    {
        Rig g;
        g.set("status_port", "00");
        g.set("data_port", "01");
        g.out(0x00, 0xAA);  // OUT to the control port
        CHECK(g.line->out().empty(), "control writes go nowhere -- there is no chip to program");
        // ...and it did not disturb the data path.
        g.line->feed("m");
        (void)g.status();
        CHECK(g.data() == 'm', "the data path is unharmed");
    }

    SECTION("IO-2 -- built-in profiles preset the straps, and an override wins");
    {
        // The built-in table is the one place profiles live; all ship.
        const auto& bi = io2Builtins();
        CHECK(bi.size() >= 2, "the built-ins ship");
        bool sior0 = false, tuart = false, imsai = false, if2 = false, ss1 = false;
        for (auto& e : bi) {
            if (e.name == "sior0") sior0 = true;
            if (e.name == "tuart") tuart = true;
            if (e.name == "imsai-sio2") imsai = true;
            if (e.name == "compupro-if2") if2 = true;
            if (e.name == "compupro-ss1") ss1 = true;
        }
        CHECK(sior0, "MITS SIO Rev 0 is a built-in (the default)");
        CHECK(tuart, "Cromemco TU-ART is a built-in");
        CHECK(imsai, "IMSAI SIO-2 is a built-in");
        CHECK(if2, "CompuPro Interfacer II is a built-in");
        CHECK(ss1, "CompuPro System Support 1 is a built-in");

        Rig g;
        g.set("profile", "sior0");
        CHECK(g.get("status_port") == 0x00, "sior0: status port preset");
        CHECK(g.get("data_port") == 0x01, "sior0: data port preset");
        CHECK(g.get("dav") == 0, "sior0: DAV is bit 0");
        CHECK(g.get("tbmt") == 7, "sior0: TBMT is bit 7");

        g.set("profile", "tuart");
        CHECK(g.get("status_port") == 0x00, "tuart: status port preset");
        CHECK(g.get("data_port") == 0x01, "tuart: data port preset");
        CHECK(g.get("dav") == 6, "tuart: DAV is bit 6");
        CHECK(g.get("tbmt") == 7, "tuart: TBMT is bit 7");

        g.set("profile", "imsai-sio2");
        CHECK(g.get("data_port") == 0x02, "imsai: data below status");
        CHECK(g.get("status_port") == 0x03, "imsai: status at BASE+3");
        CHECK(g.get("dav") == 1, "imsai: RxRDY is bit 1");
        CHECK(g.get("tbmt") == 0, "imsai: TxRDY is bit 0");

        // CompuPro Interfacer II (1602/1863): data at BASE+0, status at BASE+1, TBMT=bit0,
        // DAV=bit1, active high.
        g.set("profile", "compupro-if2");
        CHECK(g.get("data_port") == 0x00, "if2: data at BASE+0");
        CHECK(g.get("status_port") == 0x01, "if2: status at BASE+1");
        CHECK(g.get("dav") == 1, "if2: DAV is bit 1");
        CHECK(g.get("tbmt") == 0, "if2: TBMT is bit 0");

        // CompuPro System Support 1 (2651): same strap shape, default base 5C/5D.
        g.set("profile", "compupro-ss1");
        CHECK(g.get("data_port") == 0x5C, "ss1: data at 5C");
        CHECK(g.get("status_port") == 0x5D, "ss1: status at 5D");
        CHECK(g.get("dav") == 1, "ss1: RxRDY is bit 1");
        CHECK(g.get("tbmt") == 0, "ss1: TxRDY is bit 0");

        // A byte round-trips through the strapped ports of a CompuPro profile.
        g.line->feed("Q");
        CHECK((g.status() & 0x02) != 0, "ss1: DAV (bit 1) asserts when a byte waits");
        CHECK(g.data() == 'Q', "ss1: the data port yields the received byte");
        CHECK((g.status() & 0x01) != 0, "ss1: TBMT (bit 0) is set on an idle line");
        g.out(g.straps().dataPort, 'Z');
        CHECK(g.line->out() == "Z", "ss1: a data-port write reaches the line");

        // An explicit strap AFTER the profile wins -- the profile only presets.
        g.set("status_port", "50");
        CHECK(g.get("status_port") == 0x50, "an override after the profile is honored");
    }

    SECTION("IO-2 -- an unconnected line is not an error");
    {
        // A card with nothing plugged into it has a DEAD line, not a dangling one:
        // TBMT set, DAV clear, forever, and a write goes nowhere without crashing.
        // Test the active-high sense so the reading is unambiguous.
        Io2Board b;
        std::string err;
        setProperty(b, "dav", "0", err);
        setProperty(b, "tbmt", "1", err);
        setProperty(b, "inverter_gate", "false", err);
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x00;  // default status port
        uint8_t s = b.read(c);
        CHECK((s & 0x02) != 0, "unconnected: still READY TO SEND (NullStream is writable)");
        CHECK((s & 0x01) == 0, "unconnected: never anything to receive");

        c.type = Cycle::IoWrite;
        c.addr = 0x01;  // default data port
        c.data = 'x';
        b.write(c);  // must not crash, must not block
        CHECK(b.rxBytes() == 0, "and nothing was ever delivered");
    }
}
