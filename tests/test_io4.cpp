#include "test.h"

#include "boards/io4.h"
#include "core/board.h"
#include "host/endpoint.h"
#include "host/stream.h"

using namespace altair;

namespace {

// An IO-4 on the bench with a scripted line on EACH of its two units. No Machine and no
// Clock: the card is polled and schedules nothing, so a bus cycle is all it needs. Each
// stream is bound through the REAL connect path (connect(unit,"scripted")), the same wiring
// an operator's CONNECT drives -- then the test feed()s bytes at a channel and reads what
// the guest sent out of out(). setResolver() is installed once in tests/main.cpp.
//
// io4's straps live PER UNIT (units "a"/"b", configured as [board.unit.a]); board.properties()
// is empty. So the strap get/set here go through b.unitProperties(unit), the same door a
// [board.unit.a] table in a machine file uses.
struct Rig {
    Io4Board        b;
    ScriptedStream* a = nullptr;
    ScriptedStream* bb = nullptr;

    Rig() {
        std::string err;
        CHECK(b.connect("a", "scripted", err), "the scripted line connects to channel a");
        CHECK(b.connect("b", "scripted", err), "the scripted line connects to channel b");
        a  = dynamic_cast<ScriptedStream*>(b.unitStream("a"));
        bb = dynamic_cast<ScriptedStream*>(b.unitStream("b"));
        CHECK(a != nullptr && bb != nullptr, "and both are ScriptedStreams we can drive");
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
    bool decodes(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.decodes(c);
    }

    // A channel's straps, read back through the per-unit property layer -- what SHOW prints.
    long long get(const std::string& unit, const std::string& key) {
        for (auto& pr : b.unitProperties(unit))
            if (pr.name == key) return pr.get().i();
        return -1;
    }
    // Set a per-unit strap, then re-settle the board (as the monitor's SET does).
    void set(const std::string& unit, const std::string& key, const std::string& v) {
        std::string err;
        CHECK(setPropertyIn(b.unitProperties(unit), b.id + ":" + unit, key, v, err),
              ("SET " + unit + "." + key + "=" + v).c_str());
        b.configChanged();
    }
    uint8_t statusOf(const std::string& unit) { return in((uint8_t)get(unit, "status_port")); }
    uint8_t dataOf(const std::string& unit)   { return in((uint8_t)get(unit, "data_port")); }
};

} // namespace

void test_io4() {
    SECTION("IO-4 -- two serial units, 'a' and 'b'");
    {
        Rig g;
        CHECK(g.b.type() == "io4", "identifies as io4");
        CHECK(g.b.units().size() == 2, "two units -- a dual-serial card has two lines");
        CHECK(g.b.units()[0].name == "a", "first unit is 'a'");
        CHECK(g.b.units()[1].name == "b", "second unit is 'b'");
        CHECK(g.b.units()[0].kind == UnitKind::Serial, "a is serial");
        CHECK(g.b.units()[1].kind == UnitKind::Serial, "b is serial");
        CHECK(g.b.properties().empty(), "straps live per-unit -- board-level properties is empty");

        std::string err;
        CHECK(!g.b.connect("tty", "null", err), "there is no unit but a/b");
        CHECK(err.find("tty") != std::string::npos, "and the error names the bad unit");
    }

    SECTION("IO-4 -- the default 4-port block: a at 0/1, b at 2/3, both sior0");
    {
        // The IO-4's default serial block is 0-3: Serial A at 0/1, Serial B at 2/3
        // (reference/SSM IO-4 2P+2S IO Board.md). Both come up as MITS SIO Rev 0.
        Rig g;
        CHECK(g.get("a", "status_port") == 0x00, "a: status at 0");
        CHECK(g.get("a", "data_port") == 0x01, "a: data at 1");
        CHECK(g.get("a", "dav") == 0, "a: DAV is bit 0 (sior0)");
        CHECK(g.get("a", "tbmt") == 7, "a: TBMT is bit 7 (sior0)");

        CHECK(g.get("b", "status_port") == 0x02, "b: status at 2");
        CHECK(g.get("b", "data_port") == 0x03, "b: data at 3");
        CHECK(g.get("b", "dav") == 0, "b: DAV is bit 0 (sior0 shape)");
        CHECK(g.get("b", "tbmt") == 7, "b: TBMT is bit 7 (sior0 shape)");
    }

    SECTION("IO-4 -- decodes ONLY its four strapped ports, and no memory");
    {
        Rig g;
        CHECK(g.decodes(0x00), "answers a's status port");
        CHECK(g.decodes(0x01), "answers a's data port");
        CHECK(g.decodes(0x02), "answers b's status port");
        CHECK(g.decodes(0x03), "answers b's data port");
        CHECK(!g.decodes(0x04), "and nothing past the block");
        CHECK(!g.decodes(0xFF), "nor a far-off port");

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0x00;
        CHECK(!g.b.decodes(c), "decodes no MEMORY -- it is an I/O card");
    }

    SECTION("IO-4 -- the two channels are independent: no cross-talk");
    {
        // Different straps AND different data on each channel; each must answer only for
        // itself. This is the whole point of a two-channel board.
        Rig g;
        // Leave a at sior0 (0/1). Move b well away and give it an active-high shape.
        g.set("b", "status_port", "20");
        g.set("b", "data_port", "21");
        g.set("b", "dav", "1");
        g.set("b", "tbmt", "0");
        g.set("b", "inverter_gate", "false");

        // Feed distinct bytes at each line.
        g.a->feed("A");
        g.bb->feed("B");

        // a's data port yields a's byte; b's yields b's -- neither sees the other.
        CHECK(g.dataOf("a") == 'A', "a delivers the byte fed to a");
        CHECK(g.dataOf("b") == 'B', "b delivers the byte fed to b");
        CHECK(g.b.rxBytes() == 2, "rxBytes() sums both channels' deliveries");

        // A transmit on a goes out a's line only.
        g.out((uint8_t)g.get("a", "data_port"), 'x');
        CHECK(g.a->out() == "x", "a's transmit reaches a's line");
        CHECK(g.bb->out().empty(), "and nothing bled onto b's line");
    }

    SECTION("IO-4 -- overlapping straps: the earlier channel wins, and it is flagged");
    {
        Rig g;
        g.b.drainLog();  // clear any prior advisories
        // Strap b onto a's ports. a is the earlier channel, so it owns 0/1; b is shadowed.
        g.set("b", "status_port", "00");
        g.set("b", "data_port", "01");

        // Decode still answers (a owns the ports). A byte fed to a is what a byte read at
        // port 1 returns -- b never gets a look-in.
        g.a->feed("E");
        g.bb->feed("Z");
        CHECK(g.in(0x01) == 'E', "port 1 dispatches to channel a (the earlier channel wins)");

        bool flagged = false;
        for (auto& s : g.b.drainLog())
            if (s.find("overlap") != std::string::npos || s.find("shadow") != std::string::npos)
                flagged = true;
        CHECK(flagged, "and configChanged() logged an overlap advisory");
    }

    SECTION("IO-4 -- status synthesis + the inverter gate, on channel a");
    {
        Rig g;
        // A neutral custom strap on a: DAV in bit 0, TBMT in bit 1, gate off. A
        // ScriptedStream is always writable(), so TBMT is asserted.
        g.set("a", "dav", "0");
        g.set("a", "tbmt", "1");
        g.set("a", "inverter_gate", "false");

        uint8_t s = g.statusOf("a");
        CHECK((s & 0x01) == 0, "DAV clear -- nobody has typed");
        CHECK((s & 0x02) != 0, "TBMT set -- the line is ready to send");
        CHECK((s & ~0x03) == 0, "and every OTHER bit is 0 -- nothing bleeds in");

        g.a->feed("A");
        CHECK((g.statusOf("a") & 0x01) != 0, "DAV sets once a character is on the line");

        // The inverter gate flips BOTH bits together (one shared buffer).
        g.set("a", "inverter_gate", "true");
        // Now: DAV asserted (char waiting) reads 0; TBMT asserted (writable) reads 0.
        s = g.statusOf("a");
        CHECK((s & 0x01) == 0, "gate on: asserted DAV reads 0");
        CHECK((s & 0x02) == 0, "gate on: asserted TBMT reads 0");
    }

    SECTION("IO-4 -- a bit position is a jumper: put DAV/TBMT anywhere");
    {
        Rig g;
        g.set("a", "dav", "6");
        g.set("a", "tbmt", "3");
        g.set("a", "inverter_gate", "false");

        CHECK((g.statusOf("a") & (1 << 3)) != 0, "TBMT lands in bit 3, where we put it");
        CHECK((g.statusOf("a") & (1 << 6)) == 0, "DAV quiet in bit 6");
        g.a->feed("Q");
        CHECK((g.statusOf("a") & (1 << 6)) != 0, "...and sets in bit 6 when a character arrives");
    }

    SECTION("IO-4 -- the data path both directions, and a discarded control write");
    {
        Rig g;
        g.set("a", "dav", "0");
        g.set("a", "inverter_gate", "false");

        g.a->feed("K");
        CHECK((g.statusOf("a") & 0x01) != 0, "a character is waiting");
        CHECK(g.dataOf("a") == 'K', "the data port yields it");
        CHECK((g.statusOf("a") & 0x01) == 0, "and reading it clears DAV");
        CHECK(g.b.rxBytes() == 1, "the byte is counted");
        CHECK(g.dataOf("a") == 0, "a quiet line reads 0");
        CHECK(g.b.rxBytes() == 1, "...and an empty read is not a delivered byte");

        g.out((uint8_t)g.get("a", "data_port"), 'X');
        CHECK(g.a->out() == "X", "a write to the data port goes out the line");

        // OUT to the status/control port is accepted and discarded -- the line is unchanged
        // (out() is cumulative, so "still just X" is "the control write added nothing").
        g.out((uint8_t)g.get("a", "status_port"), 0xAA);
        CHECK(g.a->out() == "X", "control writes go nowhere -- there is no chip to program");
    }

    SECTION("IO-4 -- built-in profiles preset a channel's straps, and an override wins");
    {
        const auto& bi = serialBuiltins();
        CHECK(bi.size() >= 2, "the built-ins ship");
        bool sior0 = false, tuart = false, imsai = false, if2 = false, ss1 = false;
        for (auto& e : bi) {
            if (e.name == "sior0") sior0 = true;
            if (e.name == "tuart") tuart = true;
            if (e.name == "imsai-sio2") imsai = true;
            if (e.name == "compupro-if2") if2 = true;
            if (e.name == "compupro-ss1") ss1 = true;
        }
        CHECK(sior0 && tuart && imsai && if2 && ss1, "all five profiles ship");

        Rig g;
        // Select a profile on channel a; the straps preset.
        g.set("a", "profile", "imsai-sio2");
        CHECK(g.get("a", "data_port") == 0x02, "imsai: data below status");
        CHECK(g.get("a", "status_port") == 0x03, "imsai: status at BASE+3");
        CHECK(g.get("a", "dav") == 1, "imsai: RxRDY is bit 1");
        CHECK(g.get("a", "tbmt") == 0, "imsai: TxRDY is bit 0");

        // A profile on a does NOT disturb b -- each channel is strapped alone.
        CHECK(g.get("b", "status_port") == 0x02, "b's straps are untouched by a's profile");
        CHECK(g.get("b", "data_port") == 0x03, "b still at its own ports");

        // An explicit strap AFTER the profile wins.
        g.set("a", "status_port", "50");
        CHECK(g.get("a", "status_port") == 0x50, "an override after the profile is honored");
    }

    SECTION("IO-4 -- connectStream installs a pre-built line on a named channel");
    {
        // The --mcp console binding hands the board an already-wrapped stream. It must land
        // on the named channel and carry bytes, exactly like connect() but without the resolver.
        Io4Board b;
        auto s = std::make_unique<ScriptedStream>();
        ScriptedStream* raw = s.get();
        std::string err;
        CHECK(b.connectStream("b", std::move(s), err), "connectStream binds channel b");
        CHECK(b.unitStream("b") == raw, "and b's line is the stream we handed in");

        raw->feed("W");
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x03;  // b's data port
        CHECK(b.read(c) == 'W', "a byte on b's pre-built line reaches the guest");

        CHECK(!b.connectStream("z", std::make_unique<NullStream>(), err),
              "connectStream to a missing unit fails");
    }

    SECTION("IO-4 -- an unconnected channel is not an error");
    {
        // A channel with nothing plugged into it has a DEAD line: TBMT set, DAV clear,
        // and a write goes nowhere without crashing. Test the active-high sense.
        Io4Board b;
        std::string err;
        setPropertyIn(b.unitProperties("a"), "io40:a", "dav", "0", err);
        setPropertyIn(b.unitProperties("a"), "io40:a", "tbmt", "1", err);
        setPropertyIn(b.unitProperties("a"), "io40:a", "inverter_gate", "false", err);

        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = 0x00;  // a's default status port
        uint8_t s = b.read(c);
        CHECK((s & 0x02) != 0, "unconnected: still READY TO SEND (NullStream is writable)");
        CHECK((s & 0x01) == 0, "unconnected: never anything to receive");

        c.type = Cycle::IoWrite;
        c.addr = 0x01;  // a's default data port
        c.data = 'x';
        b.write(c);  // must not crash, must not block
        CHECK(b.rxBytes() == 0, "and nothing was ever delivered");
    }
}
