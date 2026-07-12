#include "test.h"

#include "boards/mits-2sio.h"
#include "core/clock.h"
#include "host/endpoint.h"
#include "host/stream.h"
#include "host/tcp.h"
#include "platform/socket.h"

#include <memory>
#include <string>

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
    size_t write(const uint8_t* buf, size_t n) override {
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

    Chip() {
        auto s = std::make_unique<PinStream>();
        line   = s.get();
        u.connect(std::move(s));
        u.masterReset(clk);
    }

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

        // A character arrives normally.
        g.line->feed("A");
        g.tick(10000);
        g.poll();
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
        g.tick(10000);
        g.poll();
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
        // on CTS and DCD)." A reset button on the front panel does not dial the phone.
        g.u.masterReset(g.clk);
        CHECK((g.status() & kDcd) != 0, "still no carrier: a reset cannot fake a pin");
        CHECK(!g.u.irq(g.clk), "...but the pending INTERRUPT is gone, which reset does clear");
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
        std::vector<Property> ps = g.u.properties();
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
        for (auto& p : g.u.properties())
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

            for (int i = 0; i < 100 && !sock.status().carrier; ++i) {
                client->poll();
                sock.pump();
            }
            CHECK(sock.status().carrier, "the client connected -> DCD ROSE");
            CHECK(sock.status().dsr, "...and DSR with it");
            CHECK(sock.status().cts, "...and CTS, because the send buffer is empty");

            // Bytes, both ways.
            const uint8_t hi[] = {'H', 'I'};
            sock.write(hi, 2);
            uint8_t got[8] = {0};
            for (int i = 0; i < 100; ++i) {
                client->poll();
                if (client->read(got, 2) == 2) break;
            }
            CHECK(got[0] == 'H' && got[1] == 'I', "the guest's bytes reach the client");

            const uint8_t yo[] = {'Y', 'O'};
            client->write(yo, 2);
            for (int i = 0; i < 100 && !sock.readable(); ++i) sock.pump();
            CHECK(sock.readable(), "the client's bytes reach the guest");
            CHECK(sock.readByte() == 'Y', "...in order");

            // THE CLIENT CLOSES ITS WINDOW. That is a carrier drop, and it is exactly
            // the event the 6850 above latches and interrupts on.
            client->close();
            for (int i = 0; i < 100 && sock.status().carrier; ++i) sock.pump();
            CHECK(!sock.status().carrier, "the client hung up -> CARRIER DROPPED");
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
            for (int i = 0; i < 100 && !sock.status().carrier; ++i) {
                if (client) client->poll();
                sock.pump();
            }
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
    }
}
