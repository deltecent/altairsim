// PropIo -- the S100Computers Console IO Board (src/boards/propio.h,
// reference/Console IO Board.md). propio is a SUBTYPE of the usio engine (tested in
// test_usio.cpp); this suite does not re-test the engine mechanics -- it asserts the two
// things that make propio propio: it IS a usio underneath (the engine's data path works
// through it) and it comes up preset to the Console IO Board's documented convention
// (ports 00/01, RX-ready = status bit 1, TX-ready = status bit 2, both active high), while
// every strap stays overridable because the real board is jumpered.

#include "test.h"

#include "boards/propio.h"
#include "boards/usio.h"
#include "core/board.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <string>

using namespace altair;

namespace {

// A propio on the bench with a scripted line, driven exactly as test_usio's Rig -- the same
// real connect path (resolveEndpoint installed in tests/main.cpp).
struct Rig {
    PropIoBoard     b;
    ScriptedStream* line = nullptr;

    Rig() {
        std::string err;
        CHECK(b.connect("serial", "scripted", err), "the scripted line connects");
        line = dynamic_cast<ScriptedStream*>(b.unitStream("serial"));
        CHECK(line != nullptr, "and it is a ScriptedStream we can drive");
    }

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

void test_propio() {
    SECTION("propio -- identifies as the Console IO Board and is one serial unit");
    {
        Rig g;
        CHECK(g.b.type() == "propio", "type() is propio, not usio");
        CHECK(g.b.units().size() == 1, "one line -- a console");
        CHECK(g.b.units()[0].name == "serial", "named 'serial' (inherited from the usio engine)");
        CHECK(g.b.units()[0].kind == UnitKind::Serial, "a serial unit");
    }

    SECTION("propio -- the profile helper is the board's documented convention");
    {
        // reference/Console IO Board.md: status 00 / data 01; RX-ready = bit 1, TX-ready =
        // bit 2, both active high. The board and the ctor share this one source of truth.
        UsioProfile p = propioProfile();
        CHECK(p.statusPort == 0x00, "status port 00");
        CHECK(p.dataPort == 0x01, "data port 01");
        CHECK(p.rdrBit == 1, "RX-ready is status bit 1 (AND 02H)");
        CHECK(p.tdreBit == 2, "TX-ready is status bit 2 (AND 04H)");
        CHECK(!p.rdrActiveLow, "RX-ready active high (1 = a key waits)");
        CHECK(!p.tdreActiveLow, "TX-ready active high (0 = busy)");
    }

    SECTION("propio -- comes up preset, no configuration needed");
    {
        Rig g;
        CHECK(g.get("status_port") == 0x00, "status_port preset to 00");
        CHECK(g.get("data_port") == 0x01, "data_port preset to 01");
        CHECK(g.get("rdr_bit") == 1, "rdr_bit preset to 1");
        CHECK(g.get("tdre_bit") == 2, "tdre_bit preset to 2");

        // It decodes exactly those two ports and nothing else.
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x00;
        CHECK(g.b.decodes(c), "answers the status port 00");
        c.addr = 0x01;
        CHECK(g.b.decodes(c), "answers the data port 01");
        c.addr = 0x02;
        CHECK(!g.b.decodes(c), "and nothing else");
    }

    SECTION("propio -- the status byte follows the SD-Systems-8024 monitor convention");
    {
        Rig g;
        // Idle line (ScriptedStream is always writable): TX-ready (bit 2) set, RX-ready
        // (bit 1) clear. This is exactly what `IN 00H; AND 04H; JP Z,OUTPUT` waits for.
        uint8_t s = g.in(0x00);
        CHECK((s & 0x04) != 0, "TX-ready (bit 2) set on an idle line -- OUTPUT loop falls through");
        CHECK((s & 0x02) == 0, "RX-ready (bit 1) clear -- nobody has typed");

        // Feed a key: RX-ready (bit 1) sets -- `IN 00H; AND 02H; JP Z,INPUT` falls through.
        g.line->feed("A");
        s = g.in(0x00);
        CHECK((s & 0x02) != 0, "RX-ready (bit 1) sets once a key is waiting");
        CHECK((s & 0x04) != 0, "TX-ready (bit 2) still set");
    }

    SECTION("propio -- the data path round-trips through ports 00/01");
    {
        Rig g;
        // Receive: a fed byte reaches the guest through the data port and clears RX-ready.
        g.line->feed("K");
        CHECK((g.in(0x00) & 0x02) != 0, "a character is waiting");
        CHECK(g.in(0x01) == 'K', "the data port yields it");
        CHECK((g.in(0x00) & 0x02) == 0, "reading it clears RX-ready (INPUT_ENABLE* resets the flag)");
        CHECK(g.b.rxBytes() == 1, "the byte is counted");

        // Transmit: a write to the data port lands on the line.
        g.out(0x01, 'X');
        CHECK(g.line->out() == "X", "a write to the data port goes out the line");
    }

    SECTION("propio -- the presets are still jumpers: an override wins");
    {
        Rig g;
        // The real Console IO Board relocates via SW2/SW3 (e.g. its own test software uses
        // 14/15) and re-jumpers the status bits (P74-P77). A property override models that.
        g.set("status_port", "14");
        g.set("data_port", "15");
        CHECK(g.get("status_port") == 0x14, "status_port relocates to the test base 14");
        CHECK(g.get("data_port") == 0x15, "data_port relocates to 15");

        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x14;
        CHECK(g.b.decodes(c), "now decodes the new status base");
        c.addr = 0x00;
        CHECK(!g.b.decodes(c), "and no longer the old one");

        // The data path still works at the relocated ports.
        g.line->feed("m");
        (void)g.in(0x14);
        CHECK(g.in(0x15) == 'm', "receive works at the relocated data port");
    }
}
