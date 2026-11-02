#include "test.h"

#include "boards/compupro-interfacer1.h"
#include "config/toml.h"
#include "core/machine.h"
#include "core/board.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "host/stream.h"

using namespace altair;

namespace {

// A scripted line that also records what the card programmed into it, as a real host serial
// port would be told. Counts setParams() calls and keeps the last frame.
class RecordingStream : public ScriptedStream {
public:
    int                      calls = 0;
    LineParams               last;
    std::vector<std::string> farEnd;  // what the far end has to say (a printer that could not spool)
    std::vector<std::string> drainLog() override {
        auto out = std::move(farEnd);
        farEnd.clear();
        return out;
    }
    bool setParams(const LineParams& p, std::string&) override {
        ++calls;
        last = p;
        return true;
    }
};

// An Interfacer 1 on the bench with a scripted line on each channel, bound through the real
// connect path. Clock first: a board holding a Clock deadline must not outlive it.
struct Rig {
    Clock              clk;
    InterfacerOneBoard b;
    ScriptedStream*    a  = nullptr;
    ScriptedStream*    bb = nullptr;

    Rig() {
        b.attachClock(&clk);
        std::string err;
        CHECK(b.connect("a", "scripted", err), "a scripted line connects to channel a");
        CHECK(b.connect("b", "scripted", err), "a scripted line connects to channel b");
        a  = dynamic_cast<ScriptedStream*>(b.unitStream("a"));
        bb = dynamic_cast<ScriptedStream*>(b.unitStream("b"));
        CHECK(a != nullptr && bb != nullptr, "and both are ScriptedStreams");
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
    bool decodes(uint8_t port) {
        BusCycle c;
        c.type = Cycle::IoRead;
        c.addr = port;
        return b.decodes(c);
    }
    bool trySet(const std::string& unit, const std::string& key, const std::string& v) {
        std::string err;
        bool ok = setPropertyIn(b.unitProperties(unit), b.id + ":" + unit, key, v, err);
        if (ok) b.configChanged();
        return ok;
    }
    void set(const std::string& unit, const std::string& key, const std::string& v) {
        CHECK(trySet(unit, key, v), ("SET " + unit + "." + key + "=" + v).c_str());
    }
    void setBoard(const std::string& key, const std::string& v) {
        std::string err;
        CHECK(setProperty(b, key, v, err), ("SET " + key + "=" + v).c_str());
        b.configChanged();
    }
};

} // namespace

void test_interfacer1() {
    SECTION("Interfacer 1 -- decode: A at 00-01, B at 02-03, data first; S2/S3 move them");
    {
        Rig g;
        CHECK(g.decodes(0x00) && g.decodes(0x01) && g.decodes(0x02) && g.decodes(0x03),
              "the CompuPro standard ports 00-03 are ours");
        CHECK(!g.decodes(0x04), "04 is not");
        g.set("b", "port", "10");
        CHECK(!g.decodes(0x02) && g.decodes(0x10) && g.decodes(0x11), "channel B moved to 10-11");
        CHECK(!g.trySet("a", "port", "11"), "an odd address is refused (A7-A1 decode)");
        g.set("b", "enabled", "false");
        CHECK(!g.decodes(0x10), "S3 position 8 takes channel B off the bus");
        CHECK(g.decodes(0x00), "and leaves A alone");
    }

    SECTION("Interfacer 1 -- status: TBMT D0 and DAV D1 read true; data read clears DAV");
    {
        Rig g;
        CHECK((g.in(0x01) & 0x03) == 0x01, "idle: TBMT set, DAV clear");
        g.a->feed("K");
        g.b.pump();
        CHECK((g.in(0x01) & 0x02) == 0x02, "a character arrived: DAV set");
        CHECK(g.in(0x00) == 'K', "the data port hands it over");
        CHECK((g.in(0x01) & 0x02) == 0, "and the read cleared DAV (RDAV tied to RDE)");
        g.out(0x00, 'X');
        CHECK((g.in(0x01) & 0x01) == 0, "a character going out drops TBMT");
        CHECK(g.a->out() == "X", "and it reached the line");
        for (int i = 0; i < 100; ++i) g.clk.advance(1000);
        CHECK((g.in(0x01) & 0x01) == 0x01, "TBMT returns when it has left");
        CHECK(g.bb->out().empty(), "channel B saw nothing");
    }

    SECTION("Interfacer 1 -- J14 swaps status and data on both channels");
    {
        Rig g;
        g.setBoard("swap_ports", "true");
        g.bb->feed("Q");
        g.b.pump();
        CHECK((g.in(0x02) & 0x02) == 0x02, "B's status is now at 02");
        CHECK(g.in(0x03) == 'Q', "and its data at 03");
    }

    SECTION("Interfacer 1 -- control latch: a 1 flips a strap, a 0 restores it, reset clears");
    {
        Rig g;
        const uint8_t strap = g.b.controlLevel("a");
        CHECK(strap == (InterfacerOneBoard::Nbi | InterfacerOneBoard::Np),
              "default straps: 8 bits, no parity, 1 stop, CA/CD spacing, interrupts off");
        g.out(0x01, InterfacerOneBoard::Nbi | InterfacerOneBoard::Tsb);
        CHECK(g.b.controlLevel("a") == (InterfacerOneBoard::Np | InterfacerOneBoard::Tsb),
              "D7 flipped 8 bits to 7, D4 flipped 1 stop bit to 2");
        CHECK(g.b.controlLevel("b") == strap, "channel B's latch is its own");
        g.out(0x01, 0x00);
        CHECK(g.b.controlLevel("a") == strap, "writing 0 returns every line to its strap");
        g.out(0x01, 0xFF);
        g.b.reset(Reset::Bus);
        CHECK(g.b.controlLevel("a") == strap, "pRESET clears the 74LS175s");
        g.set("a", "data_bits", "7");
        CHECK(!(g.b.controlLevel("a") & InterfacerOneBoard::Nbi), "the data_bits strap moves NBI");
        g.out(0x01, InterfacerOneBoard::Nbi);
        CHECK(g.b.controlLevel("a") & InterfacerOneBoard::Nbi, "and a 1 flips it back to 8");
    }

    SECTION("Interfacer 1 -- the RS-232 lines: CA/CD out, CB/CC/CF in, ON reads 0");
    {
        Rig         g;
        std::string err;
        CHECK(g.b.connect("a", "loopback", err), "a loopback plug on channel A");
        // Loopback: RTS->CTS (CA->CB), DTR->DCD+DSR (CD->CF+CC). Straps leave CA/CD spacing (ON).
        CHECK((g.in(0x01) & 0xC4) == 0x00, "CA and CD ON: CB, CC and CF all read 0");
        g.out(0x01, InterfacerOneBoard::CaOut);
        CHECK((g.in(0x01) & 0xC4) == 0x80, "flip CA to marking: CB drops and reads 1");
        g.out(0x01, InterfacerOneBoard::CdOut);
        CHECK((g.in(0x01) & 0xC4) == 0x44, "only CD marking: CC and CF read 1, CB is back to 0");
        g.set("a", "opt", "eoc");
        CHECK((g.in(0x01) & 0x04) == 0x04, "J12 B-C: D2 is the UART's EOC, idle = 1");
    }

    SECTION("Interfacer 1 -- interrupts: gated by the enable, on a VI line, never pin 73");
    {
        Rig g;
        g.set("a", "rx_int", "vi1");
        g.set("a", "tx_int", "vi2");
        CHECK(!g.trySet("a", "rx_int", "int"), "J11 has no pin-73 position: `int` is refused");
        CHECK(g.b.assertsVi() == 0, "both sources jumpered, both enables off: nothing");
        g.out(0x01, InterfacerOneBoard::TxIntE);
        CHECK(g.b.assertsVi() == viBit(IrqJumper::Vi2), "TXINTE on, idle transmitter: VI2");
        g.out(0x00, 'X');
        CHECK((g.b.assertsVi() & viBit(IrqJumper::Vi2)) == 0, "busy transmitter drops it");
        for (int i = 0; i < 100; ++i) g.clk.advance(1000);
        CHECK((g.b.assertsVi() & viBit(IrqJumper::Vi2)) != 0, "and it rises when TBMT returns");
        g.out(0x01, InterfacerOneBoard::RxIntE);
        g.a->feed("K");
        g.b.pump();
        CHECK(g.b.assertsVi() == viBit(IrqJumper::Vi1), "RXINTE on, a character: VI1 only");
        CHECK(!g.b.assertsInt(), "and pin 73 never");
        g.in(0x00);
        CHECK(g.b.assertsVi() == 0, "reading the character drops VI1");
        g.set("a", "rx_int_enable", "on");
        g.out(0x01, InterfacerOneBoard::RxIntE);
        g.a->feed("L");
        g.b.pump();
        for (int i = 0; i < 100 && !(g.in(0x01) & 0x02); ++i) g.clk.advance(1000);
        CHECK((g.in(0x01) & 0x02) == 0x02, "the second character really arrived (DAV set)");
        CHECK(g.b.assertsVi() == 0, "strap on, latch bit 1: the enable is flipped OFF");
        CHECK(g.trySet("B", "tx_int", "vi3"), "unit names match in any case, as on the io4");
        g.out(0x03, InterfacerOneBoard::TxIntE);
        CHECK(g.b.assertsVi() == viBit(IrqJumper::Vi3), "channel B's TxINT on its own VI line");
    }

    SECTION("Interfacer 1 -- the latch reprograms the host line, and RESTORE does too");
    {
        Clock              clk;
        InterfacerOneBoard b;
        b.attachClock(&clk);
        auto*       rec = new RecordingStream;
        std::string err;
        CHECK(b.connectStream("a", std::unique_ptr<ByteStream>(rec), err), "a recording line on A");
        const int before = rec->calls;
        BusCycle  c;
        c.type = Cycle::IoWrite;
        c.addr = 0x01;
        c.data = InterfacerOneBoard::Nbi | InterfacerOneBoard::Tsb;  // 7 bits, 2 stop bits
        b.write(c);
        CHECK(rec->calls > before && rec->last.dataBits == 7 && rec->last.stopBits == 2,
              "a format change in the latch reaches the host line as 7N2");
        StateWriter w;
        b.serialize(w);

        Clock              clk2;
        InterfacerOneBoard h;
        h.attachClock(&clk2);
        auto* rec2 = new RecordingStream;
        CHECK(h.connectStream("a", std::unique_ptr<ByteStream>(rec2), err), "a fresh board's line");
        StateReader r(w.data());
        h.deserialize(r);
        CHECK(r.remaining() == 0, "deserialize reads exactly what serialize wrote");
        CHECK(rec2->last.dataBits == 7 && rec2->last.stopBits == 2,
              "RESTORE reprograms the host line to the restored 7N2");
    }

    SECTION("Interfacer 1 -- reset returns the modem lines to the straps");
    {
        Rig         g;
        std::string err;
        CHECK(g.b.connect("a", "loopback", err), "a loopback plug on channel A");
        g.out(0x01, InterfacerOneBoard::CaOut);
        CHECK((g.in(0x01) & 0x80) == 0x80, "CA flipped to marking: CB reads OFF");
        g.b.reset(Reset::Bus);
        CHECK((g.in(0x01) & 0x80) == 0x00, "pRESET: CA back to spacing, CB reads ON again");
    }

    SECTION("Interfacer 1 -- both channels on one block: A answers, and the log says so");
    {
        Rig g;
        g.b.drainLog();
        g.set("b", "port", "0");
        g.out(0x00, 'Z');
        CHECK(g.a->out() == "Z" && g.bb->out().empty(), "channel A answers, B is shadowed");
        bool warned = false;
        for (const auto& l : g.b.drainLog())
            if (l.find("both at port 00") != std::string::npos) warned = true;
        CHECK(warned, "and configChanged() logs the collision");
        g.set("a", "baud", "300");
        warned = false;
        for (const auto& l : g.b.drainLog())
            if (l.find("both at port 00") != std::string::npos) warned = true;
        CHECK(!warned, "only once: an unrelated SET does not repeat it");
    }

    SECTION("Interfacer 1 -- the far end of a line is heard in the log");
    {
        Clock              clk;
        InterfacerOneBoard b;
        b.attachClock(&clk);
        auto*       rec = new RecordingStream;
        std::string err;
        CHECK(b.connectStream("b", std::unique_ptr<ByteStream>(rec), err), "a line on B");
        b.drainLog();
        rec->farEnd.push_back("could not spool");
        bool heard = false;
        for (const auto& l : b.drainLog())
            if (l.find("could not spool") != std::string::npos) heard = true;
        CHECK(heard, "drainLog() passes the stream's own messages on, as gsio does");
    }

    SECTION("Interfacer 1 -- from a machine file, on the bus, and back out with CONFIG SAVE");
    {
        const char* kText = R"(
[machine]
name = "if1test"

[[board]]
type = "interfacer1"
id   = "if1"

  [board.unit.a]
  data_bits = 7
  tx_int_enable = "on"
  tx_int = "vi2"

  [board.unit.b]
  port = 0x10
  baud = "19200"
)";
        Machine     m;
        std::string err;
        CHECK(loadTomlText(kText, "if1test", m, err), "a machine file with an interfacer1 loads");
        auto* b = dynamic_cast<InterfacerOneBoard*>(m.find("if1"));
        CHECK(b != nullptr, "the registry makes the board");
        if (b) {
            m.power();
            CHECK(b->controlLevel("a") == (InterfacerOneBoard::Np | InterfacerOneBoard::TxIntE),
                  "data_bits = 7 as a TOML integer, and the TX enable strap, reach the level");
            CHECK(m.bus.viLines() == viBit(IrqJumper::Vi2),
                  "an idle transmitter with TXINTE on drives VI2 on the BUS, not only the pin");
            const std::string out = saveTomlText(m);
            CHECK(out.find("tx_int = \"vi2\"") != std::string::npos &&
                      out.find("baud = \"19200\"") != std::string::npos &&
                      out.find("port = 0x10") != std::string::npos,
                  "CONFIG SAVE writes the unit settings back");
        }
    }

    SECTION("Interfacer 1 -- a snapshot keeps the control latch");
    {
        Rig g;
        g.out(0x01, InterfacerOneBoard::Nbi | InterfacerOneBoard::Np);  // 7 bits, parity, odd
        StateWriter w;
        g.b.serialize(w);
        Rig h;
        StateReader r(w.data());
        h.b.deserialize(r);
        CHECK(h.b.controlLevel("a") == 0x00, "the restored latch gives 7 bits, odd parity, 1 stop");
        CHECK(h.b.controlLevel("b") == g.b.controlLevel("b"), "and channel B is unchanged");
    }
}
