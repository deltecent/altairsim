#include "test.h"

#include "boards/frontpanel-link.h"
#include "boards/mits-88cpu.h"
#include "boards/mits-frontpanel.h"
#include "boards/s100-memory.h"
#include "cli/monitor.h"
#include "config/toml.h"
#include "core/machine.h"
#include "host/stream.h"
#include "platform/socket.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

using namespace altair;

namespace {

// A machine with a front panel in it. By hand, not from a .toml, so a config bug
// cannot make these go red -- and then ONE test at the bottom goes the other way and
// checks the .toml, on purpose.
struct Rig {
    Machine          m;
    FrontPanelBoard* fp  = nullptr;
    MemoryBoard*     mem = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);  // re-derive the decode and the int wire every cycle

        mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region r;
        r.kind = RegionKind::Ram;
        r.at   = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);

        fp = dynamic_cast<FrontPanelBoard*>(m.add("fp", "fp0", err));
        m.add("8080", "cpu0", err);
        m.power();
    }

    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            StepResult s = m.master()->step(m.bus);
            m.clock.advance(s.tStates);
        }
    }

    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
        m.cpu()->setPc(at);
    }

    // The accumulator, read through the reflection layer -- the same way REG and the
    // MCP server read it, so this test cannot see a register the debugger cannot.
    uint32_t reg(const std::string& name) {
        for (const RegDef& rd : m.cpu()->registers())
            if (rd.name == name) return rd.get();
        return 0xFFFFFFFF;
    }
};

// Through the reflection layer, the way SHOW reads it -- not by reaching into the
// board. If `sense` is not gettable, that is a bug in the card.
std::string prop(Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().text(p.radix);
    return "(no such property)";
}

// Poll `done` for up to ~2 s of REAL time, running `step` each pass with a short sleep.
// The TCP handshake, the byte delivery and the reconnect are the KERNEL's to schedule,
// not ours, and the board's throttle/backoff are on std::chrono::steady_clock -- so a
// bare iteration-bounded spin can burn its budget in microseconds before any of it has
// had wall-clock time to happen. Bounding by real time is the honest wait. Same shape as
// test_lines.cpp. Returns the final state of `done`.
template <class Step, class Pred>
bool waitFor(Step step, Pred done) {
    for (int i = 0; i < 200 && !done(); ++i) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

// THE BRIDGE, played by the test. altairsim dials OUT (socket:HOST:PORT), so here the
// test is the SERVER: it listens, accepts the board's call, and reads/writes the wire.
// One client at a time, exactly like the real bridge. accept()/reconnect just work
// because the listener stays up across a hangup.
struct FakeBridge {
    std::unique_ptr<platform::TcpListener> listener;
    std::unique_ptr<platform::TcpConn>     conn;
    std::string                            rx;  // everything the board has sent us

    // One server turn: answer a (re)dial, then drain whatever the board wrote.
    void poll() {
        if (!conn) conn = listener->accept();
        if (!conn) return;
        conn->poll();
        uint8_t b[512];
        for (size_t n; (n = conn->read(b, sizeof b)) > 0;) rx.append((const char*)b, n);
    }

    void send(const std::string& s) {
        if (conn) conn->write((const uint8_t*)s.data(), s.size());
    }

    // The far end puts the phone down -- a carrier drop the board must redial through.
    void hangUp() {
        if (conn) { conn->close(); conn.reset(); }
    }

    bool sawFromBoard(const std::string& needle) const {
        return rx.find(needle) != std::string::npos;
    }
};

} // namespace

void test_frontpanel() {
    SECTION("the front panel -- the SENSE switches, and the lamps");
    {
        Rig r;
        std::string err;

        // ---- The port. 0xFF, read, and nothing else. ----
        r.fp->setSense(0xA5);
        CHECK(r.m.bus.ioRead(0xFF) == 0xA5, "IN 0FFH reads the SENSE switches");

        CHECK(setProperty(*r.fp, "sense", "5A", err), "SET fp0 SENSE=5A -- and it is HEX");
        CHECK(r.m.bus.ioRead(0xFF) == 0x5A, "...and the guest sees the new setting");
        CHECK(prop(*r.fp, "sense") == "0x5A", "...and SHOW agrees with the guest");

        // THE SWITCHES ARE THE TOP HALF OF ONE ROW OF SIXTEEN. Not a separate
        // register -- SA8..SA15 -- which is why setting one does not disturb the
        // other and why there is only one place either can be wrong.
        r.fp->setSwitches((uint16_t)((r.fp->switches() & 0xFF00) | 0x3C));
        CHECK(r.fp->switches() == 0x5A3C, "sense is the HIGH byte of the switch row");
        CHECK(r.m.bus.ioRead(0xFF) == 0x5A, "...and the low half is not on the port");
        CHECK(setProperty(*r.fp, "sense", "77", err), "SET fp0 SENSE again");
        CHECK(r.fp->switches() == 0x773C, "...and it moved only the top eight");
        CHECK(setProperty(*r.fp, "sense", "5A", err), "put it back for what follows");

        // ONE KNOB, AND ONLY ONE. The low half is not a property: nothing in the machine
        // reads it, and there is no panel to flip it on. What this board owes an operator
        // is a way to say what IN 0FFH returns.
        CHECK(!setProperty(*r.fp, "data", "3C", err), "there is no `data` property");

        // ---- OUT 0FFH IS NOT OURS. The buffer enable is gated with sINP; there is
        // no sOUT anywhere near it (schematic 880-106). The byte is discarded by the
        // backplane, and the switches do not move -- which is what a toggle does when
        // you write to it, i.e. nothing.
        r.m.bus.ioWrite(0xFF, 0x00);
        CHECK(r.m.bus.lastUnclaimed(), "OUT 0FFH is unclaimed -- the panel does not latch it");
        CHECK(r.m.bus.ioRead(0xFF) == 0x5A, "...and it did not move a switch");
    }

    {
        // ---- THE REGRESSION THAT MOTIVATED THE WHOLE CARD. ----
        //
        // With no panel in the machine, port FF must still FLOAT. `Machine::sense`
        // used to be a byte that nothing put on the bus, so this was the ONLY
        // behavior the guest ever saw -- 0xFF, whatever the config said. An empty
        // backplane is still allowed to be empty; what is not allowed is a machine
        // that has no panel and pretends to.
        Machine m;
        std::string err;
        m.bus.setVerify(true);
        m.add("memory", "mem0", err);
        m.power();

        CHECK(m.bus.ioRead(0xFF) == 0xFF, "no panel -> port FF floats, and that is honest");
        CHECK(m.bus.lastUnclaimed(), "...because nobody drove it");
    }

    {
        // ---- A TOGGLE IS A TOGGLE. Neither reset is a finger. ----
        Rig r;
        r.fp->setSense(0x3C);

        r.m.reset(Reset::Bus);  // the RESET button on the panel
        CHECK(r.fp->sense() == 0x3C, "RESET does not move a switch");

        r.m.power();  // POC*, the thing that loses RAM
        CHECK(r.fp->sense() == 0x3C, "and neither does POWER -- nothing moves a toggle but a hand");

        // ...but there is no light without power.
        CHECK(r.fp->addressLamps() == 0, "POWER puts the address lamps out");
        CHECK(r.fp->dataLamps() == 0, "...and the data lamps");
        CHECK(r.fp->busStatus() == 0, "...and the status word");
    }

    {
        // ---- THE LAMPS ARE WIRED TO THE BACKPLANE. ----
        //
        // They show the LAST CYCLE THAT WENT BY, including cycles this card had
        // nothing to do with -- which is the entire reason it snoops. The Operator's
        // Manual is candid about what that looks like at speed: "While running a
        // program, however, LEDs may appear to give erroneous indications."
        Rig r;

        r.m.bus.memWrite(0x1234, 0x99);
        CHECK(r.fp->addressLamps() == 0x1234, "a memory WRITE lights the address it went to");
        CHECK(r.fp->dataLamps() == 0x99, "...and the byte that went there");
        // WO* is ACTIVE LOW: 0 on a write. So the status word for a memory write is
        // 0x00 -- no MEMR, and WO* cleared. (The bridge lights the WO LED when it sees
        // WO* == 0; that inversion is the bridge's job, not ours. See bus.h Status8080.)
        CHECK(r.fp->busStatus() == 0x00, "...and the status word is 0 -- WO* active low, 0 on a write");

        // A READ lights the data lamps too, and THAT is the part that needed a bus
        // fix: BusCycle::data is 0 while a read is in flight (nobody has driven the
        // bus yet when read() is called), so Bus::settle() back-fills it with the
        // byte that came back before the snoopers see it. Without that, the data
        // lamps would be dark on every read -- three quarters of all cycles.
        uint8_t v = r.m.bus.memRead(0x1234);
        CHECK(v == 0x99, "the byte reads back");
        CHECK(r.fp->addressLamps() == 0x1234, "a memory READ lights the address");
        CHECK(r.fp->dataLamps() == 0x99, "...and the byte that came BACK -- see Bus::settle()");
        // A read: MEMR set, and WO* set (active low, 1 on a read). 0x82.
        CHECK(r.fp->busStatus() == (StMemR | StWo), "...and the status word is MEMR|WO* (0x82)");

        // The floating bus is a byte too. It is 0xFF because nothing drove it, and
        // eight LEDs wired to eight pulled-up lines will happily show you that.
        r.m.bus.ioRead(0x42);
        CHECK(r.fp->dataLamps() == 0xFF, "an unclaimed port floats, and the lamps show FF");
        CHECK(r.fp->busStatus() == (StInp | StWo), "...on an INP cycle: INP|WO* (0x42)");

        r.m.bus.ioWrite(0x42, 0x07);
        CHECK(r.fp->dataLamps() == 0x07, "an OUT lights the byte");
        // An output: OUT set, WO* cleared (0 on a write). 0x10.
        CHECK(r.fp->busStatus() == StOut, "...with OUT set and WO* clear (0x10)");
    }

    {
        // ---- AND THE GUEST'S OWN IN, WHICH IS THE POINT OF THE CARD. ----
        //
        // This is what DBL does at FF22, reduced to four bytes. It is also the one
        // cycle where the panel is BOTH the card being read and the card watching:
        // the lamps show its own byte, because on the real machine those buffers are
        // driving the very lines the LEDs hang off.
        Rig r;
        r.fp->setSense(0x10);
        r.load({0xDB, 0xFF,   // IN 0FFH
                0xE6, 0x10,   // ANI 10H     -- DBL's stop-bit test
                0x76});       // HLT
        r.run(2);

        CHECK(r.reg("A") == 0x10, "the guest's IN 0FFH lands the switches in A");
        CHECK(r.fp->dataLamps() == 0x10, "and the panel's own byte lit its own data lamps");
    }

    {
        // ---- THE CONFIG. This is the one that goes through the TOML parser. ----
        Machine m;
        std::string err;
        const char* toml =
            "[machine]\n"
            "name = \"t\"\n"
            "[[board]]\n"
            "type  = \"fp\"\n"
            "id    = \"fp0\"\n"
            "sense = 0x12\n";
        CHECK(loadTomlText(toml, "test", m, err), "a panel loads from a [[board]] table");
        Board* b = m.find("fp0");
        CHECK(b != nullptr, "and it is in the backplane");
        if (b) {
            CHECK(prop(*b, "sense") == "0x12", "with the switches set, and read back as HEX");
            CHECK(m.bus.ioRead(0xFF) == 0x12, "...and the guest can read them");
        }
    }

    {
        // ---- THE OLD KEY IS REFUSED, AND IT SAYS WHY. ----
        //
        // `[machine] sense` parsed for months into a byte that nothing put on the
        // bus. A config that LOOKS like it set the switches and did not is worse than
        // one that will not load, so this is an ERROR with a sentence -- the same
        // treatment `clock_hz` got when the crystal moved onto the CPU card.
        Machine m;
        std::string err;
        const char* toml =
            "[machine]\n"
            "name  = \"t\"\n"
            "sense = 0x10\n";
        CHECK(!loadTomlText(toml, "test", m, err), "[machine] sense no longer loads");
        CHECK(err.find("FRONT PANEL") != std::string::npos,
              "...and the error says where the switches went");
        CHECK(err.find("type  = \"fp\"") != std::string::npos,
              "...and hands you the two lines that replace it");
    }

    SECTION("the wire codec -- L/HELLO out, W/S/HELLO in (no socket)");
    {
        using namespace altair::fplink;

        // ---- Outbound frames are the spec's exact grammar: lowercase, padded. ----
        CHECK(encodeL(0x1234, 0x99, 0x82, 0x00) == "L 1234 99 82 00\n",
              "L is `L <addr:04x> <data:02x> <status:02x> <flags:02x>`");
        CHECK(encodeL(0xffff, 0x0f, 0x00, 0x00) == "L ffff 0f 00 00\n",
              "...zero-padded and lowercase throughout");
        CHECK(encodeHello() == "HELLO altairsim-fp 1\n", "HELLO carries our version");

        // ---- THE STATUS BYTE CROSSES VERBATIM. No remap, no inversion. WO* is
        // active low, so a WRITE puts 00 on the wire and a READ puts 82 -- exactly
        // what the bus latched. This is the whole cross-repo contract in two lines.
        CHECK(encodeL(0, 0, 0x00, 0).find(" 00 00\n") != std::string::npos,
              "a write's status word (WO*=0) crosses as 00");
        CHECK(encodeL(0, 0, StMemR | StWo, 0) == "L 0000 00 82 00\n",
              "a read's status word (MEMR|WO*) crosses as 82 -- verbatim");

        // ---- The `flags` byte carries WAIT (bit 2), the one machine-control
        // indicator altairsim drives. It is SEPARATE from the status word above --
        // WAIT is a panel pin, not a bus signal -- and matches altairsim-fp's
        // FlagWait = 1u << 2, which the bridge already renders.
        CHECK(FlWait == 0x04, "FlWait is bit 2, matching altairsim-fp's FlagWait");
        CHECK(encodeL(0x2001, 0x55, StMemR | StWo, FlWait) == "L 2001 55 82 04\n",
              "WAIT lit rides the flags byte as 04, alongside an untouched status word");

        // HLTA (bit 1) is the other machine-control flag altairsim now drives -- the CPU
        // stopped on a HLT. The bridge already composes its D3 HLTA LED from this bit.
        CHECK(FlHalted == 0x02, "FlHalted is bit 1, matching altairsim-fp's FlagHalted");
        CHECK(encodeL(0x2001, 0x76, StMemR | StWo, FlWait | FlHalted) == "L 2001 76 82 06\n",
              "a HLT stop rides flags 06 -- WAIT and HLTA together");

        // ---- Inbound: HELLO, and version is whatever the bridge said (min() is the
        // caller's job, not the codec's). ----
        PanelMsg h = parseLine("HELLO altairsim-fp 1");
        CHECK(h.kind == PanelMsg::Kind::Hello && h.value == 1, "HELLO parses to its version");
        CHECK(parseLine("HELLO altairsim-fp 3").value == 3,
              "...and reports the remote version as-is (negotiation is upstream)");
        CHECK(parseLine("HELLO something-else 1").kind == PanelMsg::Kind::None,
              "a HELLO for a different protocol is ignored");

        // ---- Inbound: W (full 16-bit word) and S (sense byte alone). ----
        PanelMsg w = parseLine("W abcd");
        CHECK(w.kind == PanelMsg::Kind::Switches && w.value == 0xABCD, "W is the 16-bit switch word");
        PanelMsg s = parseLine("S 81");
        CHECK(s.kind == PanelMsg::Kind::Sense && s.value == 0x81, "S is the sense byte");

        // ---- Leniency exactly where the spec grants it, and nowhere else. ----
        CHECK(parseLine("  W abcd\r").value == 0xABCD, "leading spaces and a trailing CR are tolerated");
        CHECK(parseLine("W abcd 9999").value == 0xABCD, "trailing fields are ignored (forward-compat)");
        CHECK(parseLine("X 12").kind == PanelMsg::Kind::None, "an unknown frame type is ignored");
        CHECK(parseLine("W xyz").kind == PanelMsg::Kind::None, "bad hex is dropped");
        CHECK(parseLine("W abc").kind == PanelMsg::Kind::None, "a short (wrong-width) field is dropped");
        CHECK(parseLine("").kind == PanelMsg::Kind::None, "a blank line is nothing");
    }

    SECTION("the codec formats the board's own bus signals -- WO* active low end to end");
    {
        using namespace altair::fplink;

        // The board latches the bus status word; the codec ships it verbatim. Prove
        // the two agree, which is the point of keeping status ON THE BUS.
        Rig r;

        r.m.bus.memWrite(0x2000, 0x55);
        CHECK(encodeL(r.fp->addressLamps(), r.fp->dataLamps(), r.fp->busStatus(), 0)
                  == "L 2000 55 00 00\n",
              "a memory write ships status 00 -- WO* active low, 0 on a write");

        uint8_t v = r.m.bus.memRead(0x2000);
        CHECK(v == 0x55, "the byte reads back");
        CHECK(encodeL(r.fp->addressLamps(), r.fp->dataLamps(), r.fp->busStatus(), 0)
                  == "L 2000 55 82 00\n",
              "a memory read ships status 82 -- MEMR|WO*, verbatim from the bus");
    }

    // -----------------------------------------------------------------------
    // THE M1 LAMP ON STOP. A real Altair halts inside the next instruction's M1 fetch, so
    // the stopped panel shows M1|MEMR|WO lit, ADDRESS on PC and DATA on the opcode. The
    // monitor re-drives that pending fetch at the stop path (a real read at PC with the
    // fetch status word) so snoop() latches it -- this proves the mechanism the monitor
    // uses lands on the lamps. See monitor.cpp's stop path.
    // -----------------------------------------------------------------------
    SECTION("a re-driven M1 fetch latches M1|MEMR|WO, PC and the opcode onto the lamps");
    {
        Rig r;

        // Put an opcode where PC will sit, then run an operand read past it so the lamps
        // hold a NON-M1 cycle -- exactly the stale state a plain stop leaves behind.
        r.m.bus.memWrite(0x2000, 0x3E);            // MVI A -- the opcode about to run
        (void)r.m.bus.memRead(0x1000);            // an unrelated read: lamps now show 0x1000/82
        CHECK(r.fp->busStatus() == (StMemR | StWo), "before: lamps hold a plain read, M1 dark");

        // The stop path's move: a real read at PC carrying the M1 fetch status word.
        (void)r.m.bus.memRead(0x2000, StM1 | StMemR | StWo);
        CHECK(r.fp->busStatus()    == (StM1 | StMemR | StWo), "a stopped panel shows the M1 fetch (0xA2)");
        CHECK(r.fp->addressLamps() == 0x2000,                 "...ADDRESS shows PC");
        CHECK(r.fp->dataLamps()    == 0x3E,                   "...DATA shows the opcode about to run");
    }

    // -----------------------------------------------------------------------
    // OVER A REAL SOCKET. altairsim dials OUT to the bridge; the test IS the bridge,
    // listening on the loopback interface. This is the one place the suite touches the
    // OS, because the whole claim -- the panel handshakes, streams lamps, reads switches
    // and redials -- is a claim about what crosses a socket. Mirrors test_lines.cpp.
    // -----------------------------------------------------------------------
    SECTION("socket: -- the panel dials out, greets, and streams the bus verbatim");
    {
        using namespace altair::fplink;

        std::string err;
        FakeBridge  br;
        br.listener = platform::listenTcp(0, err);
        CHECK(br.listener != nullptr, ("the bridge listens: " + err).c_str());

        if (br.listener) {
            Rig r;

            // A memory write BEFORE we connect, so the very first L frame the panel
            // ships has something recognisable in it (WO* active low -> 00 on a write).
            r.m.bus.memWrite(0x2000, 0x55);

            std::string spec = "socket:127.0.0.1:" + std::to_string(br.listener->port());
            CHECK(r.fp->connect("gui", spec, err), ("CONNECT fp0:gui " + spec + ": " + err).c_str());

            // The dial completes, the panel sees carrier, and it GREETS -- HELLO with our
            // version. The bridge does nothing but listen and read.
            bool greeted = waitFor([&] { br.poll(); r.fp->pump(); },
                                   [&] { return br.sawFromBoard("HELLO altairsim-fp 1\n"); });
            CHECK(greeted, "the panel dials out and greets the bridge");

            // The bridge greets back. min(ours, theirs) is 1 either way; the panel must
            // simply not choke on it (it is an inbound frame like any other).
            br.send("HELLO altairsim-fp 1\n");

            // The first lamp frame carries the pre-connect write, VERBATIM off the bus.
            // flags=04: the board has never been RUN (running_ defaults false), so WAIT
            // is lit -- the machine-control group the run state drives (see setRunning).
            bool sawWrite = waitFor([&] { br.poll(); r.fp->pump(); },
                                    [&] { return br.sawFromBoard("L 2000 55 00 04\n"); });
            CHECK(sawWrite, "a memory write streams as L 2000 55 00 04 -- WO* active low, WAIT lit");

            // A read moves the lamps AND the status word (MEMR|WO* -> 82). The diff gate
            // lets exactly this new frame through; the throttle just paces it.
            uint8_t got = r.m.bus.memRead(0x2000);
            CHECK(got == 0x55, "the byte reads back");
            bool sawRead = waitFor([&] { br.poll(); r.fp->pump(); },
                                   [&] { return br.sawFromBoard("L 2000 55 82 04\n"); });
            CHECK(sawRead, "a read streams as L 2000 55 82 04 -- MEMR|WO*, WAIT lit, verbatim");

            // ---- Inbound: the bridge flips a switch, the GUEST reads it at port FF. ----
            br.send("S 81\n");
            bool sensed = waitFor([&] { br.poll(); r.fp->pump(); },
                                  [&] { return r.fp->sense() == 0x81; });
            CHECK(sensed, "S 81 from the bridge lands in the SENSE switches");
            CHECK(r.m.bus.ioRead(0xFF) == 0x81, "...and a guest IN 0FFH reads exactly that");

            // ...and the full 16-bit switch word, high byte still the sense switches.
            br.send("W abcd\n");
            bool switched = waitFor([&] { br.poll(); r.fp->pump(); },
                                    [&] { return r.fp->switches() == 0xABCD; });
            CHECK(switched, "W abcd sets the whole switch row");
            CHECK(r.m.bus.ioRead(0xFF) == 0xAB, "...and the sense byte is its high half");
        }
    }

    SECTION("socket: -- the bridge goes away, and the panel redials on its own");
    {
        std::string err;
        FakeBridge  br;
        br.listener = platform::listenTcp(0, err);

        if (br.listener) {
            Rig r;
            std::string spec = "socket:127.0.0.1:" + std::to_string(br.listener->port());
            CHECK(r.fp->connect("gui", spec, err), "CONNECT fp0:gui");

            bool up1 = waitFor([&] { br.poll(); r.fp->pump(); },
                               [&] { return br.sawFromBoard("HELLO"); });
            CHECK(up1, "the first session connects and greets");

            // THE BRIDGE CLOSES ITS WINDOW. Carrier drops; the panel must notice and
            // redial the SAME endpoint on its own -- the bridge is relaunched out of band,
            // and only DISCONNECT tells the panel to stay unplugged.
            br.hangUp();
            br.rx.clear();  // so the next HELLO is unmistakably from the REDIAL

            bool up2 = waitFor([&] { br.poll(); r.fp->pump(); },
                               [&] { return br.sawFromBoard("HELLO"); });
            CHECK(up2, "the panel redialled the dropped bridge, with no operator help");

            // ...and the redialled line WORKS: a switch frame still reaches the guest.
            br.send("S 42\n");
            bool sensed = waitFor([&] { br.poll(); r.fp->pump(); },
                                  [&] { return r.fp->sense() == 0x42; });
            CHECK(sensed, "the reconnected session carries switches like the first did");

            // DISCONNECT is the explicit stop: after it, a dropped line is NOT redialled.
            CHECK(r.fp->disconnect("gui", err), "DISCONNECT fp0:gui");
            br.hangUp();
            br.rx.clear();
            bool up3 = waitFor([&] { br.poll(); r.fp->pump(); },
                               [&] { return br.sawFromBoard("HELLO"); });
            CHECK(!up3, "a DISCONNECTed panel does not redial -- the operator pulled the plug");
        }
    }

    // -----------------------------------------------------------------------
    // THE WAIT LAMP. It is not a bus signal -- it is the operator's RUN state, fanned
    // to the board by Machine::setRunning (the monitor calls it around a run). WAIT is
    // lit when the machine is stopped, dark while it runs. It rides the `flags` byte.
    // -----------------------------------------------------------------------
    SECTION("socket: -- WAIT tracks the run state (flags bit 2), driven by setRunning");
    {
        std::string err;
        FakeBridge  br;
        br.listener = platform::listenTcp(0, err);
        CHECK(br.listener != nullptr, ("the bridge listens: " + err).c_str());

        if (br.listener) {
            Rig r;
            std::string spec = "socket:127.0.0.1:" + std::to_string(br.listener->port());
            CHECK(r.fp->connect("gui", spec, err), "CONNECT fp0:gui");

            bool up = waitFor([&] { br.poll(); r.fp->pump(); },
                              [&] { return br.sawFromBoard("HELLO"); });
            CHECK(up, "the panel connects and greets");

            // The machine starts STOPPED (running_ defaults false, power() confirms it):
            // WAIT is lit. A write here ships flags=04 -- the run state, not the bus.
            r.m.bus.memWrite(0x3000, 0x11);
            bool stopped = waitFor([&] { br.poll(); r.fp->pump(); },
                                   [&] { return br.sawFromBoard("L 3000 11 00 04\n"); });
            CHECK(stopped, "a stopped machine ships WAIT lit (flags 04)");

            // The operator RUNs it -- through Machine::setRunning, exactly as the monitor
            // does, which also proves the fan-out reaches this board. WAIT goes dark.
            r.m.setRunning(true);
            r.m.bus.memWrite(0x3000, 0x22);  // a fresh lamp value so the diff gate ships it
            bool running = waitFor([&] { br.poll(); r.fp->pump(); },
                                   [&] { return br.sawFromBoard("L 3000 22 00 00\n"); });
            CHECK(running, "a running machine clears WAIT (flags 00) -- setRunning fanned out");

            // ...and stopping it lights WAIT again. This is a FLAGS-ONLY change (the lamps
            // do not move), which still flips the frame string, so the diff gate ships it.
            r.m.setRunning(false);
            bool again = waitFor([&] { br.poll(); r.fp->pump(); },
                                 [&] { return br.sawFromBoard("L 3000 22 00 04\n"); });
            CHECK(again, "stopping re-lights WAIT -- a flags-only change still ships");
        }
    }

    // -----------------------------------------------------------------------
    // THE HLTA LAMP. Also not a bus signal -- the emulator runs HLT atomically, so no
    // snooped cycle carries HLTA. It is the operator-level halt state fanned by
    // Machine::setHalted (the monitor calls it at the stop path when the reason is a HLT).
    // It rides the flags byte at bit 1 (0x02), and any RUN clears it.
    // -----------------------------------------------------------------------
    SECTION("socket: -- HLTA tracks the halt state (flags bit 1), driven by setHalted");
    {
        std::string err;
        FakeBridge  br;
        br.listener = platform::listenTcp(0, err);
        CHECK(br.listener != nullptr, ("the bridge listens: " + err).c_str());

        if (br.listener) {
            Rig r;
            std::string spec = "socket:127.0.0.1:" + std::to_string(br.listener->port());
            CHECK(r.fp->connect("gui", spec, err), "CONNECT fp0:gui");

            bool up = waitFor([&] { br.poll(); r.fp->pump(); },
                              [&] { return br.sawFromBoard("HELLO"); });
            CHECK(up, "the panel connects and greets");

            // The CPU stops on a HLT: setHalted(true) lights HLTA. The machine is also
            // stopped (WAIT lit), so the flags byte carries both -- 06.
            r.m.setHalted(true);
            r.m.bus.memWrite(0x4000, 0x76);  // a fresh lamp value so the diff gate ships it
            bool halted = waitFor([&] { br.poll(); r.fp->pump(); },
                                  [&] { return br.sawFromBoard("L 4000 76 00 06\n"); });
            CHECK(halted, "a HLT stop ships HLTA + WAIT (flags 06) -- setHalted fanned out");

            // The operator RUNs it again -- setRunning(true) clears the halt latch, so a
            // later stop no longer shows HLTA. WAIT alone (04) returns.
            r.m.setRunning(true);
            r.m.setRunning(false);
            r.m.bus.memWrite(0x4000, 0x77);
            bool cleared = waitFor([&] { br.poll(); r.fp->pump(); },
                                   [&] { return br.sawFromBoard("L 4000 77 00 04\n"); });
            CHECK(cleared, "a RUN clears HLTA -- the next stop is WAIT only (flags 04)");
        }
    }

    // -----------------------------------------------------------------------
    // THE BUG THAT STARTED THIS: a discrete command (EXAMINE/STEP/DEPOSIT/NEXT) ran its
    // bus cycle and snoop() latched the lamps -- but nothing pushed a frame, so the panel
    // sat frozen. The RUN loop was the only pump() site. Each handler now pumps once. This
    // test drives the REAL monitor and asserts the frame lands, WITHOUT pumping the board
    // itself in the poll: if the handler did not pump, no frame ships and the wait fails.
    // -----------------------------------------------------------------------
    SECTION("the monitor refreshes the panel after a discrete command (STEP/EXAMINE)");
    {
        std::string err;
        FakeBridge  br;
        br.listener = platform::listenTcp(0, err);
        CHECK(br.listener != nullptr, ("the bridge listens: " + err).c_str());

        if (br.listener) {
            Rig            r;
            Monitor        mon(r.m);
            std::ostringstream sink;

            // MVI A,55 at 0x2000 -- one opcode fetch and one operand read to look at.
            r.load({0x3E, 0x55}, 0x2000);

            std::string spec = "socket:127.0.0.1:" + std::to_string(br.listener->port());
            CHECK(mon.exec("CONNECT fp0:gui " + spec, sink),
                  "CONNECT fp0:gui through the monitor");

            bool up = waitFor([&] { br.poll(); r.fp->pump(); },
                              [&] { return br.sawFromBoard("HELLO"); });
            CHECK(up, "the panel connects and greets");

            // Let the throttle window (default ~3.9 ms) fully elapse, so the SINGLE pump
            // each handler does is not dropped as too-soon. A human at the panel is always
            // slower than this; the test must be too.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            br.rx.clear();  // so what we match below is unmistakably the command's frame

            // EXAMINE is a bus cycle the CPU drives: PC <- 2000, MEMR the byte (3E),
            // status 82. The handler's pump() must ship it -- WAIT lit (stopped).
            mon.exec("EXAMINE 2000", sink);
            bool sawExamine = waitFor([&] { br.poll(); },  // NB: the poll does NOT pump the board
                                      [&] { return br.sawFromBoard("L 2000 3e 82 04\n"); });
            CHECK(sawExamine, "EXAMINE refreshed the panel -- L 2000 3e 82 04 (WAIT lit)");

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            br.rx.clear();

            // STEP runs the one instruction; the last bus cycle is the operand read at
            // 2001 (data 55, status 82). Again the handler pumps; the poll does not.
            mon.exec("STEP", sink);
            bool sawStep = waitFor([&] { br.poll(); },
                                   [&] { return br.sawFromBoard("L 2001 55 82 04\n"); });
            CHECK(sawStep, "STEP refreshed the panel -- L 2001 55 82 04 (last cycle, WAIT lit)");
        }
    }
}
