#include "test.h"

#include "boards/mits-2sio.h"
#include "boards/registry.h"
#include "core/machine.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/file.h"
#include "host/filter.h"
#include "host/stream.h"
#include "host/tcp.h"
#include "platform/socket.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using namespace altair;

namespace {

// ---------------------------------------------------------------------------
// A LINE WHOSE PINS THE TEST DRIVES. The ScriptedStream of the modem world.
//
// This is the endpoint that lets us test a carrier drop with no modem, no cable and
// no timing: the test simply says `line->carrier = false` and the card must do what
// the data sheet says a 6850 does about it.
// ---------------------------------------------------------------------------
class PinStream : public ByteStream {
public:
    std::string describe() const override { return "pins"; }

    size_t read(uint8_t* buf, size_t n) override {
        size_t k = in_.size() < n ? in_.size() : n;
        for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)in_[i];
        in_.erase(0, k);
        return k;
    }
    size_t doWrite(const uint8_t* buf, size_t n) override {
        out_.append((const char*)buf, n);
        return n;
    }
    bool readable() const override { return !in_.empty(); }
    bool writable() const override { return writable_; }

    LineStatus status() const override { return st_; }
    void setControl(const LineControl& c) override { ctl_ = c; }
    bool setParams(const LineParams& p, std::string& err) override {
        params_ = p;
        ++programmed_;
        if (refuse_) {
            err = "the host serial driver cannot do " + std::to_string(p.baud) + " baud";
            return false;
        }
        return true;
    }

    void feed(const std::string& s) { in_ += s; }

    // A real serial port, or a host buffer (the default). See ByteStream::realWire().
    bool realWire() const override { return wire_; }
    bool wire_ = false;

    LineStatus  st_;
    LineControl ctl_;
    LineParams  params_;
    int         programmed_ = 0;
    bool        refuse_     = false;
    bool        writable_   = true;
    std::string in_, out_;
};

// A 6850 on a bench, with a clock and a line whose pins we can move. No machine, no
// bus, no CPU -- this is a CHIP test, and the chip does not know what any of those
// are.
struct Chip {
    Clock      clk;
    Mc6850     u{"a"};
    PinStream* line = nullptr;

    // `init = false` leaves the chip as it powers up: held in its internal power-on
    // reset, with no OUT from the guest yet.
    explicit Chip(bool init = true) {
        auto s = std::make_unique<PinStream>();
        line   = s.get();
        u.connect(std::move(s));
        u.powerOn(clk);  // power-on-clear, NOT the guest's master reset. See mc6850.h.

        // ...and the guest's first OUT, which every driver does. The chip comes up held
        // in its internal power-on reset, and only a master reset releases it (issue
        // #938). Each test's own control() is then the second OUT of the pair.
        if (init) u.writeControl(0x03, clk);
    }

    // One character at the word format the chip powers up with (7E2: 11 bits) at the
    // default 9600 baud.
    uint64_t charT() const { return (uint64_t)(clk.hz() * 11 / 9600); }

    // The control register, written the way a guest writes it. 8N1, and the transmit
    // and receive interrupt bits per the caller.
    void control(uint8_t v) { u.writeControl(v, clk); }

    uint8_t status() { return u.readStatus(clk); }
    uint8_t data() { return u.readData(clk); }

    void tick(uint64_t t) { clk.advance(t); }
    void poll() { u.poll(clk); }
};

constexpr uint8_t kRdrf = 0x01;
constexpr uint8_t kTdre = 0x02;
constexpr uint8_t kDcd  = 0x04;
constexpr uint8_t kCts  = 0x08;
constexpr uint8_t kIrq  = 0x80;

constexpr uint8_t k8n1    = 0x15;  // divide/16, 8 data + no parity + 1 stop
constexpr uint8_t kRie    = 0x80;
constexpr uint8_t kRtsOff = 0x40;  // transmit control 10: RTS HIGH (negated)
constexpr uint8_t kBreak  = 0x60;  // transmit control 11: RTS low, transmit a BREAK

// A free TCP port, without hardcoding one and hoping. Port 0 means "the OS picks",
// and then we ask it what it picked -- which is the only way to write this test
// without it failing on the one machine where something else already owns 34567.
std::unique_ptr<platform::TcpListener> listenAnywhere(std::string& err) {
    return platform::listenTcp(0, err);
}

// Poll `done` for up to ~2 s of REAL time, running one `step` each pass with a
// short sleep between. The loopback handshake, the byte delivery and the hangup
// are all the KERNEL's to schedule, not ours -- a bare iteration-bounded spin
// (`for i < 100`) can burn its whole budget in microseconds, before the OS has
// so much as run the three-way handshake, and then CHECK a carrier that simply
// hadn't been given time to rise. That is what made these socket sections flake
// on a fast machine. Bounding by wall-clock, and returning the moment `done`
// holds, is the honest wait. Returns the final state of `done`.
template <class Step, class Pred>
bool waitFor(Step step, Pred done) {
    for (int i = 0; i < 200 && !done(); ++i) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

} // namespace

void test_lines() {
    SECTION("Loopback -- the plug loops the PINS too, not just the data");
    {
        LoopbackStream lb;
        CHECK(!lb.status().cts, "RTS not asserted -> CTS not asserted: it is the same wire");
        CHECK(!lb.status().carrier, "...and DTR-DCD likewise");

        LineControl c;
        c.rts = true;
        lb.setControl(c);
        CHECK(lb.status().cts, "raise RTS and the plug hands it straight back as CTS");
        CHECK(!lb.status().carrier, "...and DTR is a different wire, so DCD is unmoved");

        c.dtr = true;
        lb.setControl(c);
        CHECK(lb.status().carrier, "raise DTR and DCD comes back");
        CHECK(lb.status().dsr, "...and DSR, which is soldered to the same pin");
    }

    SECTION("Every line counts the bytes the guest sent (#948)");
    {
        // A byte sent is the guest working. Only the guest's write sends one, so the line
        // can count it. (The guest WAITING is counted at the chip, on its status read --
        // Board::emptyPolls -- because a chip also checks its line for its own reasons.)
        ScriptedStream s;
        CHECK(s.txBytes() == 0, "a new line has counted nothing");
        const uint8_t out[3] = {'x', 'y', 'z'};
        CHECK(s.write(out, 3) == 3, "the line takes three bytes");
        s.writeByte('!');
        CHECK(s.txBytes() == 4, "write() and writeByte() both count");

        // A decorator forwards to the line it wraps, so both levels count. The run loop
        // reads only the outermost one, the stream the unit holds, so nothing counts twice.
        auto  inner = std::make_unique<ScriptedStream>();
        auto* raw   = inner.get();
        FilterStream f(std::move(inner));
        f.write(out, 2);
        CHECK(f.txBytes() == 2 && raw->txBytes() == 2, "both levels counted the bytes");

        // NullStream: nothing plugged in. A guest that writes to it is working, even though
        // nobody hears it.
        NullStream n;
        n.writeByte('q');
        CHECK(n.txBytes() == 1, "a null line counts too");
    }

    // EVERY BOARD WITH A SERIAL LINE REPORTS THE GUEST WAITING (#948). The run loop sums
    // Board::emptyPolls over the backplane; a board that forgets to count makes a guest
    // polling its line look busy, so the machine never naps. Read every I/O port the board
    // decodes, with nothing on any line: at least one of those reads is a status read that
    // finds no byte. This walks the registry, so a new board is checked without a new test.
    SECTION("Every board with a serial line counts the guest's empty status reads (#948)");
    {
        // A line the guest never reads a byte from has nothing to wait for. Each name here
        // says why; a board is never added to make this test pass.
        const std::vector<std::string> noReceive = {
            "c700",     // 88-C700 printer: the line carries only the guest's output
            "lpc",      // 88-LPC printer: likewise
            "fdcplus",  // the drive-server link: the board reads it in pump(); the guest
                        // polls the disk registers, never this line
        };
        for (const BoardType& t : boardTypes()) {
            if (std::find(noReceive.begin(), noReceive.end(), t.name) != noReceive.end()) continue;
            Machine     m;
            std::string err;
            Board*      b = m.add(t.name, "x0", err);
            if (!b) continue;
            bool serial = false;
            for (const auto& u : b->units()) serial = serial || u.kind == UnitKind::Serial;
            if (!serial) continue;
            m.power();
            for (int port = 0; port < 256; ++port) {
                BusCycle c;
                c.type = Cycle::IoRead;
                c.addr = (uint16_t)port;
                if (b->decodes(c)) (void)b->read(c);
            }
            CHECK(m.lineActivity().emptyPolls > 0,
                  (t.name + ": a status read with no byte counts as the guest waiting").c_str());
        }
    }

    SECTION("Backlog (#934): what a line holds that the guest has not read");
    {
        LoopbackStream lb;
        CHECK(lb.rxPending() == 0 && lb.txPending() == 0, "a quiet plug holds nothing");
        const uint8_t abc[] = {'a', 'b', 'c'};
        lb.write(abc, 3);
        CHECK(lb.rxPending() == 3, "three bytes looped back, three waiting for the guest");
        (void)lb.readByte();
        CHECK(lb.rxPending() == 2, "the guest read one: two left");

        ScriptedStream sc;
        sc.feed("HELLO");
        CHECK(sc.rxPending() == 5, "typed five, five waiting");
        uint8_t b2[2];
        sc.read(b2, 2);
        CHECK(sc.rxPending() == 3, "the guest read two: three left");
        sc.write(abc, 3);
        CHECK(sc.txPending() == 0, "the guest's output is the test's at once: no tx queue");

        FileStream fs("in:x", std::vector<uint8_t>{'1', '2', '3', '4'}, std::fstream{});
        CHECK(fs.rxPending() == 4, "a file's unread input counts");
        (void)fs.readByte();
        CHECK(fs.rxPending() == 3, "...and goes down as the guest reads it");
        FileStream punch("out:x", std::nullopt, std::fstream{});
        CHECK(punch.rxPending() == 0, "a punch with no reader has nothing to receive");

        // A decorator reports the line it wraps -- the filter a unit always has, and the
        // handle the --mcp console holds.
        auto        line = std::make_shared<ScriptedStream>();
        FilterStream f(std::make_unique<SharedStream>(line));
        line->feed("xyz");
        CHECK(f.rxPending() == 3, "the filter and the shared handle pass the count through");
        f.read(b2, 1);
        CHECK(f.rxPending() == 2, "...and see the read");
    }

    SECTION("The strap lives on the CARD (default: grounded)");
    {
        Chip g;
        g.control(k8n1);

        // The far end is asserting NOTHING. A grounded pin does not care, and this is
        // the case that must keep working: it is every existing config in the tree.
        g.line->st_.carrier = false;
        g.line->st_.cts     = false;
        g.poll();

        CHECK(g.u.carrier(), "dcd=ground: the pin is tied asserted ON THE CARD");
        CHECK(g.u.clearToSend(), "cts=ground: likewise");
        CHECK((g.status() & kDcd) == 0, "...so the status register reports no carrier loss");
        CHECK((g.status() & kCts) == 0, "...and clear to send");
        CHECK((g.status() & kTdre) != 0, "...and the transmitter runs, on a dead line");

        // Now move the jumper. The SAME dead far end is suddenly visible.
        g.u.dcdStrap = PinStrap::Wired;
        g.u.ctsStrap = PinStrap::Wired;
        g.poll();

        CHECK(!g.u.carrier(), "dcd=wired: now the card believes the far end");
        CHECK((g.status() & kCts) != 0, "/CTS negated -> status bit 3 SET (the pin is /CTS)");
    }

    SECTION("/CTS inhibits TDRE -- and therefore the transmit interrupt");
    {
        Chip g;
        g.u.ctsStrap = PinStrap::Wired;
        g.control(k8n1 | 0x20);  // transmit control 01: RTS low, transmit interrupt ENABLED

        g.line->st_.cts = true;
        g.tick(10000);
        CHECK((g.status() & kTdre) != 0, "clear to send, character long gone: TDRE set");
        CHECK(g.u.irq(g.clk), "...and the transmit interrupt with it");

        // The far end says stop. The data sheet: "In the high state, the Transmit Data
        // Register Empty bit is inhibited."
        g.line->st_.cts = false;
        CHECK((g.status() & kTdre) == 0, "/CTS negated: TDRE is INHIBITED, not merely reported");
        CHECK(!g.u.irq(g.clk), "...and the transmit interrupt is derived from TDRE, so it goes too");

        g.line->st_.cts = true;
        CHECK((g.status() & kTdre) != 0, "CTS back: the transmitter is free again");
    }

    SECTION("A full endpoint negates TDRE the same way a modem does");
    {
        Chip g;
        g.control(k8n1);
        g.tick(10000);
        CHECK((g.status() & kTdre) != 0, "TDRE set on an idle transmitter");

        // TCP backpressure, a full driver buffer -- physically the same situation as a
        // modem holding CTS low, so it lands in the same bit and the guest just waits.
        g.line->writable_ = false;
        CHECK((g.status() & kTdre) == 0, "endpoint cannot take the byte: TDRE stays clear");
        CHECK(g.line->out_.empty(), "...and the guest has not transmitted into a full buffer");
    }

    SECTION("/DCD -- a LATCHED edge, an interrupt, and a DEAD receiver");
    {
        Chip g;
        g.u.dcdStrap = PinStrap::Wired;
        g.line->st_.carrier = true;
        g.control(k8n1 | kRie);  // receive interrupt enable: this is what arms DCD too

        // A character arrives normally. The receiver has to SEE it (a poll) before its
        // character-time begins, then that time has to pass (issue #469).
        g.line->feed("A");
        g.poll();
        g.tick(10000);
        CHECK((g.status() & kRdrf) != 0, "carrier up: the receiver receives");
        CHECK(g.data() == 'A', "...and the byte is the byte");

        // THE CALL DROPS.
        g.line->feed("B");
        g.line->st_.carrier = false;
        g.tick(10000);
        g.poll();

        uint8_t s = g.status();
        CHECK((s & kDcd) != 0, "carrier lost -> DCD status bit SET");
        CHECK((s & kIrq) != 0, "...and it INTERRUPTS: a program in a HLT finds out");
        CHECK(g.u.irq(g.clk), "...on the chip's own IRQ pin");
        CHECK((s & kRdrf) == 0,
              "...and the RECEIVER IS DEAD: /DCD high inhibits it, so RDRF reads empty");

        // ...and the byte that was on the line stayed there. It was not delivered into
        // an inhibited receiver, and it was not silently thrown away either.
        CHECK(g.line->readable(), "the byte is still on the line, undelivered");

        // CARRIER COMES BACK -- and the bit does NOT clear by itself. That is the whole
        // point of a latch: a guest that was not looking still finds out.
        g.line->st_.carrier = true;
        g.poll();       // the receiver is alive again and NOW sees the waiting byte...
        g.tick(10000);  // ...which then takes its character-time to shift in (issue #469)
        CHECK((g.status() & kDcd) != 0, "carrier back, bit STILL SET -- it is latched");
        CHECK(g.u.irq(g.clk), "...and still interrupting: nobody has acknowledged it");
        CHECK((g.status() & kRdrf) != 0,
              "...and the receiver is alive again: the byte that was waiting lands");

        // THE TWO-STEP CLEAR IS STATUS, THEN DATA -- and the data read that clears the
        // DCD latch is THE SAME READ THAT TAKES THE CHARACTER. There is only one data
        // register, and a 6850 does not have a way to read it twice.
        //
        // This test originally asserted that the byte survived the clear sequence, and
        // it was the TEST that was wrong: an acknowledge that did not consume the
        // receive register would be an acknowledge no 6850 has ever performed.
        (void)g.status();
        CHECK(g.data() == 'B', "the data read that clears DCD IS the read that takes the byte");
        CHECK((g.status() & kDcd) == 0, "status-then-data cleared the latch");
        CHECK(!g.u.irq(g.clk), "...and the interrupt with it");
        CHECK((g.status() & kRdrf) == 0, "...and the receive register is empty, as it must be");
    }

    SECTION("/DCD -- cleared while the line is STILL down, the bit follows the pin");
    {
        Chip g;
        g.u.dcdStrap = PinStrap::Wired;
        g.line->st_.carrier = true;
        g.control(k8n1 | kRie);
        g.poll();

        g.line->st_.carrier = false;  // the call drops...
        g.poll();
        CHECK((g.status() & kDcd) != 0, "latched");

        // ...and the guest acknowledges it WHILE THE LINE IS STILL DOWN. The data
        // sheet: "the interrupt is cleared, the DCD status bit remains high and will
        // follow the DCD input." The guest is not told the carrier is back merely
        // because it admitted that it went.
        (void)g.status();
        (void)g.data();
        CHECK((g.status() & kDcd) != 0, "acknowledged, but the line is STILL down: bit stays set");
        CHECK(!g.u.irq(g.clk), "...the INTERRUPT is cleared, though -- it is not re-raised");

        // Now the line really does come back, and the bit -- now following the pin --
        // finally clears on its own.
        g.line->st_.carrier = true;
        g.poll();
        CHECK((g.status() & kDcd) == 0, "carrier back for real: the bit follows the pin down");

        // ...and a SECOND drop latches afresh and interrupts again.
        g.line->st_.carrier = false;
        g.poll();
        CHECK((g.status() & kDcd) != 0, "a new drop is a new edge");
        CHECK(g.u.irq(g.clk), "...and a new interrupt");
    }

    SECTION("A master reset does not put the carrier back");
    {
        Chip g;
        g.u.dcdStrap = PinStrap::Wired;
        g.line->st_.carrier = false;
        g.control(k8n1 | kRie);
        g.poll();
        CHECK((g.status() & kDcd) != 0, "no carrier");

        // "Master reset ... clears the Status Register (except for external conditions
        // on CTS and DCD)." Switching the machine off and on again does not dial the
        // phone. (It has to be the power-on-clear: a BUS reset cannot touch a 6850 at
        // all -- the chip has no reset pin. See Sio2Board::reset.)
        g.u.powerOn(g.clk);
        g.control(0x03);  // the guest's master reset, which the power-on hold waits for
        CHECK((g.status() & kDcd) != 0, "still no carrier: a reset cannot fake a pin");
        CHECK(!g.u.irq(g.clk), "...but the pending INTERRUPT is gone, which reset does clear");
    }

    SECTION("Power-on: the chip is HELD until the guest's master reset (#938)");
    {
        // "Internal power-on reset logic detects the power-turn-on transition and holds
        // the chip in a reset state to prevent erroneous output transitions prior to
        // initialization; released by the bus-programmed Master Reset." (reference/6850.md)
        Chip g(false);
        CHECK((g.status() & kTdre) == 0, "power-on: TDRE is 0");
        g.tick(5000);
        CHECK((g.status() & kTdre) == 0, "...and stays 0 however long nobody programs it");
        CHECK(!g.line->ctl_.rts, "RTS is held negated");
        CHECK(!g.line->ctl_.brk, "...and there is no break");
        CHECK(!g.u.irq(g.clk), "...and no interrupt");

        g.u.writeData('X', g.clk);
        CHECK(g.line->out_.empty(), "a data write sends NOTHING -- the IMDW case");

        // A real divide ratio without a master reset first does not release it: "after
        // power-on or a power-fail restart, Master Reset must occur first."
        g.control(k8n1);
        CHECK((g.status() & kTdre) == 0, "a control word alone does not release the hold");
        CHECK(!g.line->ctl_.rts, "...RTS is still held");
        g.u.writeData('X', g.clk);
        CHECK(g.line->out_.empty(), "...and still nothing is sent");

        g.control(0x03);
        CHECK((g.status() & kTdre) == 0, "the master reset: held in the reset it started");
        g.control(k8n1);
        CHECK((g.status() & kTdre) != 0, "...and the second OUT releases it: TDRE is 1");
        CHECK(g.line->ctl_.rts, "RTS follows the control register now");
        g.u.writeData('Y', g.clk);
        CHECK(g.line->out_ == "Y", "and a data write goes out");
    }

    SECTION("While held, the receiver is OFF: on a real wire a byte that arrives then is lost");
    {
        // The power-on hold, on a real serial port. Eight bytes are on the line from the
        // start. Two and a half character times pass before the guest's master reset, so
        // the line delivered two of them to a receiver that was off. Those two are lost and
        // no more: the bytes are dropped at the line rate, not all at once.
        Chip g(false);
        g.line->wire_ = true;
        g.line->feed("ABCDEFGH");
        g.tick(g.charT() * 5 / 2);
        g.control(0x03);
        g.control(k8n1);
        CHECK((g.status() & kRdrf) == 0, "nothing was received while held");
        g.poll();
        g.tick(5000);
        CHECK((g.status() & kRdrf) != 0, "after release, the receiver receives");
        CHECK(g.data() == 'C', "...and A and B were lost while it was off");
        CHECK(g.u.rxBytes() == 1, "a dropped byte is not counted as received");

        // The guest's own master-reset hold is the same condition.
        Chip h;
        h.line->wire_ = true;
        h.control(k8n1);
        h.control(0x03);
        h.line->feed("Q");
        h.tick(5000);
        h.poll();
        CHECK((h.status() & kRdrf) == 0, "held by the guest's master reset: nothing received");
        h.control(k8n1);
        h.poll();
        h.tick(5000);
        CHECK((h.status() & kRdrf) == 0, "...and the byte that arrived then is gone");
        h.line->feed("R");
        h.poll();
        h.tick(5000);
        CHECK((h.status() & kRdrf) != 0 && h.data() == 'R', "the next byte is received");

        // A data write during the master-reset hold sends nothing either.
        h.control(0x03);
        h.u.writeData('Z', h.clk);
        CHECK(h.line->out_.empty(), "held by a master reset: a data write sends nothing");
    }

    SECTION("While held, a host BUFFER keeps its bytes: piped keys wait for the guest");
    {
        // The console, a pipe, a file, a socket: not a wire. Keys piped in before the guest
        // sets the chip up are still there when it does -- a pipe cannot wait for a prompt.
        Chip g(false);
        g.line->feed("ABCDEFGH");
        g.tick(g.charT() * 20);
        g.poll();
        CHECK((g.status() & kRdrf) == 0, "held: nothing is received");
        g.control(0x03);
        g.control(k8n1);
        g.poll();
        g.tick(5000);
        CHECK((g.status() & kRdrf) != 0 && g.data() == 'A', "after release, the first key is 'A'");
    }

    SECTION("A snapshot keeps the power-on hold");
    {
        Chip g(false);
        StateWriter w;
        g.u.serialize(w);

        Chip h;  // master reset already written: not in the power-on hold
        StateReader r(w.data());
        h.u.deserialize(r);
        CHECK(r.ok(), "the state reads back");
        h.control(k8n1);
        CHECK((h.status() & kTdre) == 0, "restored: still held, a control word alone does not release it");
        h.control(0x03);
        h.control(k8n1);
        CHECK((h.status() & kTdre) != 0, "...and a master reset does");
    }

    SECTION("RTS goes somewhere now, and so does BREAK");
    {
        Chip g;

        g.control(k8n1);  // transmit control 00: RTS asserted
        CHECK(g.line->ctl_.rts, "control bits 5-6 = 00 -> RTS asserted");
        CHECK(!g.line->ctl_.brk, "...and no break");

        g.control(k8n1 | kRtsOff);  // 10: RTS negated
        CHECK(!g.line->ctl_.rts, "control bits 5-6 = 10 -> RTS NEGATED, and the far end sees it");

        g.control(k8n1 | kBreak);  // 11: RTS low + break
        CHECK(g.line->ctl_.rts, "control bits 5-6 = 11 -> RTS asserted again");
        CHECK(g.line->ctl_.brk, "...and the transmitter holds the line SPACING: a break");

        CHECK(!g.line->ctl_.dtr, "and there is NO DTR: the 6850 has no such pin");
    }

    SECTION("The CARD programs the WIRE");
    {
        Chip g;
        int before = g.line->programmed_;

        g.control(k8n1);
        CHECK(g.line->programmed_ > before, "a control-register write reprograms the line");
        CHECK(g.line->params_.dataBits == 8, "8 data bits");
        CHECK(g.line->params_.stopBits == 1, "1 stop bit");
        CHECK(g.line->params_.parity == LineParity::None, "no parity");
        CHECK(g.line->params_.baud == 9600, "and the baud is the CARD's jumper");

        // 7 data, even parity, 2 stop -- a Teletype. The guest wrote it into the
        // control register, so it IS the frame on the wire.
        g.control(0x01);
        CHECK(g.line->params_.dataBits == 7, "7E2: seven data bits...");
        CHECK(g.line->params_.parity == LineParity::Even, "...even parity...");
        CHECK(g.line->params_.stopBits == 2, "...two stop bits");

        // The jumper moves, and the wire follows it.
        std::string err;
        std::vector<Property> ps = g.u.properties(resolveEndpoint);
        for (auto& p : ps)
            if (p.name == "baud") CHECK(p.set(Value::ofInt(300), err), "restrap to 300 baud");
        CHECK(g.line->params_.baud == 300, "the WIRE is reprogrammed to 300 too");
    }

    SECTION("A rate the host cannot do is SAID OUT LOUD, not swallowed");
    {
        Chip g;
        g.line->refuse_ = true;
        g.control(k8n1);

        auto log = g.u.drainLog();
        CHECK(!log.empty(), "the chip has something to say about it");
        CHECK(log[0].find("baud") != std::string::npos, "...and it says what");
        CHECK(g.u.drainLog().empty(), "draining clears it");
    }

    SECTION("`lines` is a PIN, and you cannot SET a pin");
    {
        Chip g;
        bool found = false;
        for (auto& p : g.u.properties(resolveEndpoint))
            if (p.name == "lines") {
                found = true;
                CHECK(!p.set, "no setter: it reports the far end, it does not drive it");
            }
        CHECK(found, "the card exposes its live pin state");
    }

    // -----------------------------------------------------------------------
    // A CLIENT CONNECTING *IS* CARRIER APPEARING. Real TCP, over the loopback
    // interface -- the one place a test in this suite touches the OS, because the
    // whole claim being made is about what the OS does.
    // -----------------------------------------------------------------------
    SECTION("socket: -- a telnet client IS a modem answering");
    {
        std::string err;
        auto listener = listenAnywhere(err);
        CHECK(listener != nullptr, ("a listening socket: " + err).c_str());

        if (listener) {
            uint16_t port = listener->port();
            TcpListenStream sock(std::move(listener), "socket:test");

            CHECK(!sock.status().carrier, "nobody has called yet: NO CARRIER");
            CHECK(!sock.readable(), "...and a quiet line");

            // Call in, from the same process. A telnet client, in three lines.
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("the client connects: " + err).c_str());

            waitFor([&] { client->poll(); sock.pump(); },
                    [&] { return sock.status().carrier; });
            CHECK(sock.status().carrier, "the client connected -> DCD ROSE");
            CHECK(sock.status().dsr, "...and DSR with it");
            CHECK(sock.status().cts, "...and CTS, because the send buffer is empty");

            // Bytes, both ways.
            const uint8_t hi[] = {'H', 'I'};
            sock.write(hi, 2);
            uint8_t got[8] = {0};
            size_t  rd     = 0;
            waitFor([&] { client->poll(); if (rd < 2) rd += client->read(got + rd, 2 - rd); },
                    [&] { return rd >= 2; });
            CHECK(got[0] == 'H' && got[1] == 'I', "the guest's bytes reach the client");

            const uint8_t yo[] = {'Y', 'O'};
            client->write(yo, 2);
            waitFor([&] { sock.pump(); }, [&] { return sock.readable(); });
            CHECK(sock.readable(), "the client's bytes reach the guest");
            CHECK(sock.readByte() == 'Y', "...in order");

            // THE CLIENT CLOSES ITS WINDOW. That is a carrier drop, and it is exactly
            // the event the 6850 above latches and interrupts on.
            client->close();
            waitFor([&] { sock.pump(); }, [&] { return !sock.status().carrier; });
            CHECK(!sock.status().carrier, "the client hung up -> CARRIER DROPPED");
        }
    }

    SECTION("socket: -- the backlog both ways (#934)");
    {
        std::string err;
        auto listener = listenAnywhere(err);
        CHECK(listener != nullptr, ("a listening socket: " + err).c_str());
        if (listener) {
            uint16_t port = listener->port();
            TcpListenStream sock(std::move(listener), "socket:test");
            auto client = platform::connectTcp("127.0.0.1", port, err);
            // BOTH ENDS (#945). Carrier is the listener's side. The client is a non-blocking
            // connect that poll() marks established only when it sees the socket writable,
            // and until then its write() takes nothing. The handshake can finish between the
            // client's poll() and the listener's pump(), so carrier alone can end the wait
            // with the client still connecting.
            waitFor([&] { if (client) client->poll(); sock.pump(); },
                    [&] { return sock.status().carrier && client && client->established(); });
            CHECK(sock.status().carrier && client && client->established(),
                  "connected, on both ends");

            // RECEIVE: the host program's write is over long before the guest reads it.
            const std::string msg(1000, 'r');
            CHECK(client && client->write((const uint8_t*)msg.data(), msg.size()) == msg.size(),
                  "the client sent all 1000 bytes");
            waitFor([&] { client->poll(); sock.pump(); },
                    [&] { return sock.rxPending() == msg.size(); });
            CHECK(sock.rxPending() == 1000, "1000 bytes sent, 1000 waiting for the guest");
            uint8_t buf[400];
            sock.read(buf, sizeof buf);
            CHECK(sock.rxPending() == 600, "the guest read 400: 600 left");

            // TRANSMIT: a client that does not read lets the kernel fill, then tx_.
            const std::vector<uint8_t> chunk(64 * 1024, 't');
            for (int i = 0; i < 256 && sock.txPending() == 0; ++i)
                sock.write(chunk.data(), chunk.size());
            CHECK(sock.txPending() > 0, "the kernel is full: the guest's bytes wait in altairsim");
            std::vector<uint8_t> sink(64 * 1024);
            waitFor([&] { client->poll(); while (client->read(sink.data(), sink.size()) > 0) {}
                          sock.pump(); },
                    [&] { return sock.txPending() == 0; });
            CHECK(sock.txPending() == 0, "the client read it all: nothing left in altairsim");
            client->close();
        }
    }

    SECTION("socket: -- the guest drops DTR, and we hang up on the client");
    {
        std::string err;
        auto listener = listenAnywhere(err);
        if (listener) {
            uint16_t port = listener->port();
            TcpListenStream sock(std::move(listener), "socket:test");

            auto client = platform::connectTcp("127.0.0.1", port, err);
            waitFor([&] { if (client) client->poll(); sock.pump(); },
                    [&] { return sock.status().carrier; });
            CHECK(sock.status().carrier, "connected");

            // A card that never raised DTR is not a card that hung up -- so a bare
            // de-asserted DTR (which is every 6850 in the machine, always) must NOT
            // close the session. This is the bug that would have closed every telnet
            // connection the instant it opened.
            LineControl c;
            c.dtr = false;
            sock.setControl(c);
            sock.pump();
            CHECK(sock.status().carrier, "DTR was never raised, so this is not a hangup");

            // Raise it -- the card has picked up the phone -- and then drop it.
            c.dtr = true;
            sock.setControl(c);
            c.dtr = false;
            sock.setControl(c);
            CHECK(!sock.status().carrier, "DTR dropped: WE hung up on the client");
        }
    }

    SECTION("The endpoint grammar");
    {
        std::string err;

        CHECK(resolveEndpoint("loopback", err) != nullptr, "loopback still resolves");

        // ANY CASE for the grammar's own words; never for a path, host or device.
        CHECK(resolveEndpoint("LoopBack", err) != nullptr, "LoopBack is loopback");
        CHECK(canonicalEndpoint("IN:Tape.TAP?CPS=300,Out:Punch.TAP") ==
                  "in:Tape.TAP?cps=300,out:Punch.TAP",
              "in:/out: and their options fold; the two paths keep their case");
        CHECK(canonicalEndpoint("Scripted|Cap.LOG?FMT=Text") == "scripted|Cap.LOG?fmt=text",
              "a log file sink keeps its name; its options fold");
        CHECK(canonicalEndpoint("null|PTY:/tmp/Link") == "null|pty:/tmp/Link",
              "a pty mirror sink folds its word, not its link");
        CHECK(canonicalEndpoint("null|Console") == "null|Console",
              "a log file called Console is a file, not the console");
        CHECK(canonicalEndpoint("Ptylog") == "Ptylog", "a word that only STARTS like pty is not pty");
        CHECK(canonicalEndpoint("SERIAL:/dev/tty.USB0") == "serial:/dev/tty.USB0",
              "a serial device keeps its case");
        CHECK(canonicalEndpoint("Telnet:Bbs.Example:23?Banner=Off") ==
                  "telnet:Bbs.Example:23?banner=off",
              "a host keeps its case");
        CHECK(canonicalEndpoint("PTY") == "pty", "a bare pty folds");
        CHECK(canonicalEndpoint("Terminal?Size=80x24") == "terminal?size=80x24",
              "terminal and its options fold");
        CHECK(canonicalEndpoint("PRINTER:Queue") == "printer:Queue", "a queue keeps its case");
        CHECK(canonicalEndpoint("null|SOCKET:2323") == "null|socket:2323",
              "a socket mirror sink folds");

        // One sink list (issue #778): the resolver, the rebase and --mirror all ask this.
        CHECK(isMirrorSink("Pty") && isMirrorSink("pty:/tmp/x") && isMirrorSink("socket:1") &&
                  isMirrorSink("Serial:/dev/x"),
              "pty, pty:LINK, socket: and serial: are mirror sinks, in any case");
        CHECK(!isMirrorSink("ptylog") && !isMirrorSink("Console") && !isMirrorSink("cap.hex") &&
                  !isMirrorSink("telnet:23"),
              "a log file, and a line-only scheme, are not mirror sinks");

        CHECK(resolveEndpoint("socket:", err) == nullptr, "socket: with no port is an error");
        CHECK(!err.empty(), "...and it says so");

        CHECK(resolveEndpoint("socket:notaport", err) == nullptr, "...as is a port that isn't one");
        CHECK(resolveEndpoint("socket:0", err) == nullptr,
              "port 0 is not a port an operator can mean -- it is the OS's 'pick one'");
        CHECK(resolveEndpoint("serial:", err) == nullptr, "serial: with no device is an error");
        CHECK(resolveEndpoint("serial:/dev/nonesuch-xyzzy", err) == nullptr,
              "a device that is not there does not silently become a NullStream");

        // The one that matters: it RESOLVES, and binds a real socket. Ask the OS for a
        // free port rather than hardcoding one and hoping -- a test that fails on the
        // one machine where something already owns 2323 is a test nobody trusts.
        uint16_t free = 0;
        if (auto probe = listenAnywhere(err)) free = probe->port();
        CHECK(free != 0, "the OS hands us a free port");

        auto s = resolveEndpoint("socket:" + std::to_string(free), err);
        CHECK(s != nullptr, ("socket:PORT binds: " + err).c_str());
        if (s)
            CHECK(s->describe() == "socket:" + std::to_string(free),
                  "and describes itself as what was typed");
        s.reset();
        s = resolveEndpoint("SOCKET:" + std::to_string(free), err);
        CHECK(s != nullptr, ("SOCKET:PORT binds too: " + err).c_str());
        if (s)
            CHECK(s->describe() == "socket:" + std::to_string(free),
                  "and describes itself in the grammar's own spelling");
    }

    SECTION("rxBytes -- the chip counts bytes it HANDS THE GUEST, and only those");
    {
        // The run loop's live-traffic signal (mc6850.h, monitor.cpp). It exists because a
        // guest taking a transfer looks exactly like a guest at a prompt by every other
        // measure -- the only tell is that bytes are ARRIVING. So the count must be of
        // RECEIVED bytes, and it must not be fooled by anything else the chip does.
        Chip g;
        g.control(0x15);  // 8N1, polled
        CHECK(g.u.rxBytes() == 0, "nothing received yet");

        g.line->feed("Hi");
        g.poll();         // the receiver notices 'H' -> its character time begins
        g.tick(5000);     // ...and one character time passes (issue #469)
        CHECK((g.status() & kRdrf) != 0, "a byte has arrived");
        CHECK(g.data() == 'H', "and it is the first one");
        CHECK(g.u.rxBytes() == 1, "ONE byte handed to the guest -- the count moved");

        g.tick(5000);
        CHECK(g.data() == 'i', "the second byte");
        CHECK(g.u.rxBytes() == 2, "and the count is two");

        // NOT fooled by transmit. A guest sending is not a guest receiving, and if a
        // transfer's OUTBOUND traffic reset the idle clock the signal would be useless
        // for PCPUT.
        uint8_t before = g.u.rxBytes();
        g.u.writeData('Z', g.clk);
        g.tick(5000);
        CHECK(g.u.rxBytes() == before, "transmitting a byte does NOT count as receiving one");

        // NOT fooled by a bare status poll on a quiet line -- which is exactly what a
        // prompt does, hundreds of times, and must never look like traffic.
        for (int i = 0; i < 50; ++i) (void)g.status();
        CHECK(g.u.rxBytes() == before, "polling an empty line is not receiving");
    }
}
