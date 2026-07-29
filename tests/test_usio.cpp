#include "test.h"

#include "boards/usio.h"
#include "core/board.h"
#include "host/endpoint.h"
#include "host/stream.h"

using namespace altair;

namespace {

// A USIO on the bench with a scripted line on its one unit. No Machine and no Clock:
// the card is polled and schedules nothing, so a bus cycle is all it needs. The stream
// is bound through the REAL connect path (resolveEndpoint("scripted")), the same wiring
// an operator's CONNECT drives -- then the test feed()s bytes at the card and reads what
// the guest sent out of out(). setResolver() is installed once in tests/main.cpp.
struct Rig {
    UsioBoard       b;
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
    UsioProfile straps() {
        UsioProfile p;
        p.statusPort = (uint8_t)get("status_port");
        p.dataPort   = (uint8_t)get("data_port");
        p.rdrBit     = (uint8_t)get("rdr_bit");
        p.tdreBit    = (uint8_t)get("tdre_bit");
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

void test_usio() {
    SECTION("USIO -- one serial unit, called 'serial'");
    {
        Rig g;
        CHECK(g.b.units().size() == 1, "one unit -- a universal serial card has one line");
        CHECK(g.b.units()[0].name == "serial", "named 'serial'");
        CHECK(g.b.units()[0].kind == UnitKind::Serial, "a serial unit");

        std::string err;
        CHECK(!g.b.connect("tty", "null", err), "there is no unit but 'serial'");
    }

    SECTION("USIO -- it decodes ONLY its two strapped ports");
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

    SECTION("USIO -- the status byte is synthesized at the strapped bit positions");
    {
        Rig g;
        // A neutral custom strap: status 0x00, data 0x01, RDR in bit 0, TDRE in bit 1,
        // both active high. A ScriptedStream is always writable(), so TDRE is asserted.
        g.set("status_port", "00");
        g.set("data_port", "01");
        g.set("rdr_bit", "0");
        g.set("tdre_bit", "1");
        g.set("rdr_active_low", "false");
        g.set("tdre_active_low", "false");

        uint8_t s = g.status();
        CHECK((s & 0x01) == 0, "RDR clear -- nobody has typed");
        CHECK((s & 0x02) != 0, "TDRE set -- the line is ready to send");
        CHECK((s & ~0x03) == 0, "and every OTHER bit is 0 -- nothing bleeds in");

        g.line->feed("A");
        s = g.status();
        CHECK((s & 0x01) != 0, "RDR sets once a character is on the line");
        CHECK((s & 0x02) != 0, "TDRE is still set");
    }

    SECTION("USIO -- a bit position is a jumper: put RDR/TDRE anywhere");
    {
        Rig g;
        g.set("rdr_bit", "6");
        g.set("tdre_bit", "3");
        g.set("rdr_active_low", "false");
        g.set("tdre_active_low", "false");

        CHECK((g.status() & (1 << 3)) != 0, "TDRE lands in bit 3, where we put it");
        CHECK((g.status() & (1 << 6)) == 0, "RDR quiet in bit 6");
        g.line->feed("Q");
        CHECK((g.status() & (1 << 6)) != 0, "...and sets in bit 6 when a character arrives");
    }

    SECTION("USIO -- active-low inverts a bit's sense, and ONLY that bit's");
    {
        Rig g;
        g.set("rdr_bit", "0");
        g.set("tdre_bit", "1");
        g.set("rdr_active_low", "true");   // asserted reads 0
        g.set("tdre_active_low", "true");

        // Nothing fed: RDR is NOT asserted (line quiet) so active-low reads it as 1;
        // TDRE IS asserted (writable) so active-low reads it as 0.
        uint8_t s = g.status();
        CHECK((s & 0x01) != 0, "active-low RDR: de-asserted reads 1");
        CHECK((s & 0x02) == 0, "active-low TDRE: asserted reads 0");

        g.line->feed("Z");
        s = g.status();
        CHECK((s & 0x01) == 0, "active-low RDR: asserted (char waiting) reads 0");
    }

    SECTION("USIO -- the data path, both directions");
    {
        Rig g;
        g.set("data_port", "01");

        // Receive: a fed byte reaches the guest and clears RDR.
        g.set("rdr_bit", "0");
        g.set("rdr_active_low", "false");
        g.line->feed("K");
        CHECK((g.status() & 0x01) != 0, "a character is waiting");
        CHECK(g.data() == 'K', "the data port yields it");
        CHECK((g.status() & 0x01) == 0, "and reading it clears RDR");
        CHECK(g.b.rxBytes() == 1, "the byte is counted -- rxBytes() saw a transfer arrive");

        // A quiet line reads 0 and is NOT counted.
        CHECK(g.data() == 0, "a quiet line reads 0");
        CHECK(g.b.rxBytes() == 1, "...and an empty read is not a delivered byte");

        // Transmit: a write to the data port lands on the line.
        g.out(0x01, 'X');
        CHECK(g.line->out() == "X", "a write to the data port goes out the line");
    }

    SECTION("USIO -- a write to the status/control port is DISCARDED");
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

    SECTION("USIO -- built-in profiles preset the straps, and an override wins");
    {
        // The built-in table is the one place profiles live; both ship.
        const auto& bi = usioBuiltins();
        CHECK(bi.size() >= 2, "at least the two built-ins ship");
        bool tuart = false, imsai = false, if2 = false, ss1 = false;
        for (auto& e : bi) {
            if (e.name == "tuart") tuart = true;
            if (e.name == "imsai-sio2") imsai = true;
            if (e.name == "compupro-if2") if2 = true;
            if (e.name == "compupro-ss1") ss1 = true;
        }
        CHECK(tuart, "Cromemco TU-ART is a built-in");
        CHECK(imsai, "IMSAI SIO-2 is a built-in");
        CHECK(if2, "CompuPro Interfacer II is a built-in");
        CHECK(ss1, "CompuPro System Support 1 is a built-in");

        Rig g;
        g.set("profile", "tuart");
        CHECK(g.get("status_port") == 0x00, "tuart: status port preset");
        CHECK(g.get("data_port") == 0x01, "tuart: data port preset");
        CHECK(g.get("rdr_bit") == 6, "tuart: RDR is bit 6");
        CHECK(g.get("tdre_bit") == 7, "tuart: TDRE is bit 7");

        g.set("profile", "imsai-sio2");
        CHECK(g.get("data_port") == 0x02, "imsai: data below status");
        CHECK(g.get("status_port") == 0x03, "imsai: status at BASE+3");
        CHECK(g.get("rdr_bit") == 1, "imsai: RxRDY is bit 1");
        CHECK(g.get("tdre_bit") == 0, "imsai: TxRDY is bit 0");

        // CompuPro Interfacer II (1602/1863): data at BASE+0, status at BASE+1, TBMT=bit0,
        // DAV=bit1, active high.
        g.set("profile", "compupro-if2");
        CHECK(g.get("data_port") == 0x00, "if2: data at BASE+0");
        CHECK(g.get("status_port") == 0x01, "if2: status at BASE+1");
        CHECK(g.get("rdr_bit") == 1, "if2: DAV is bit 1");
        CHECK(g.get("tdre_bit") == 0, "if2: TBMT is bit 0");

        // CompuPro System Support 1 (2651): same strap shape, default base 5C/5D.
        g.set("profile", "compupro-ss1");
        CHECK(g.get("data_port") == 0x5C, "ss1: data at 5C");
        CHECK(g.get("status_port") == 0x5D, "ss1: status at 5D");
        CHECK(g.get("rdr_bit") == 1, "ss1: RxRDY is bit 1");
        CHECK(g.get("tdre_bit") == 0, "ss1: TxRDY is bit 0");

        // A byte round-trips through the strapped ports of a CompuPro profile.
        g.line->feed("Q");
        CHECK((g.status() & 0x02) != 0, "ss1: RDR (bit 1) asserts when a byte waits");
        CHECK(g.data() == 'Q', "ss1: the data port yields the received byte");
        CHECK((g.status() & 0x01) != 0, "ss1: TDRE (bit 0) is set on an idle line");
        g.out(g.straps().dataPort, 'Z');
        CHECK(g.line->out() == "Z", "ss1: a data-port write reaches the line");

        // An explicit strap AFTER the profile wins -- the profile only presets.
        g.set("status_port", "50");
        CHECK(g.get("status_port") == 0x50, "an override after the profile is honored");
    }

    SECTION("USIO -- an unconnected line is not an error");
    {
        // A card with nothing plugged into it has a DEAD line, not a dangling one:
        // TDRE set, RDR clear, forever, and a write goes nowhere without crashing.
        UsioBoard b;
        std::string err;
        setProperty(b, "rdr_bit", "0", err);
        setProperty(b, "tdre_bit", "1", err);
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
