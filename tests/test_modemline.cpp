// `ModemLine`, ON THE REAL NETWORK STACK -- driven with no board in sight.
//
// This is the whole point of building the phone line as its own ByteStream first
// (byte-stream-boards plan, Phase 1): the ring-without-answer gate, the off-hook
// dial and the "idle holds no sockets" rule are all provable here, over loopback,
// with nothing but a free port -- exactly as test_lines.cpp proves the auto-answer
// `socket:` server. The PMMI board (Phase 2) then only has to decode its registers
// onto these calls.
//
// Like the socket sections in test_lines.cpp, this touches the real kernel TCP
// stack, so every state change is waited for by WALL CLOCK (waitFor) rather than a
// fixed spin the OS was never given time to satisfy.

#include "host/modemline.h"
#include "platform/socket.h"
#include "test.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace altair;

namespace {

// A free port, the OS's way: bind port 0, read what it picked, drop it. The number
// is one the OS just confirmed free -- the same trick sockettest.cpp uses for its
// dead-port case. A tiny race remains (something could grab it before we rebind),
// which is why the modem tests below rebind it immediately.
uint16_t freePort() {
    std::string err;
    if (auto l = platform::listenTcp(0, err)) return l->port();
    return 0;
}

// Poll `done` for up to ~2 s of real time, one `step` per pass. Returns the final
// state of `done`. Same shape as test_lines.cpp::waitFor.
template <class Step, class Pred>
bool waitFor(Step step, Pred done) {
    for (int i = 0; i < 200 && !done(); ++i) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

} // namespace

void test_modemline() {
    // -----------------------------------------------------------------------
    // THE RING GATE. An inbound call rings; carrier stays down and NOT ONE BYTE
    // is delivered until the board answers. This is the behavior `socket:`
    // cannot express -- it auto-answers -- and the reason ModemLine exists.
    // -----------------------------------------------------------------------
    SECTION("ModemLine -- an inbound call RINGS, and nothing crosses until we answer");
    {
        std::string err;
        uint16_t    port = freePort();
        CHECK(port != 0, "got a free port to answer on");

        ModemLine m("", 0, port);
        CHECK(m.armAnswer(err), ("armAnswer binds the listener: " + err).c_str());
        CHECK(!m.ringing(), "nobody has called yet: not ringing");
        CHECK(!m.carrier(), "...and no carrier");

        // Call in, from this same process -- a caller dialing our modem.
        auto caller = platform::connectTcp("127.0.0.1", port, err);
        CHECK(caller != nullptr, ("the caller connects: " + err).c_str());

        waitFor([&] { if (caller) caller->poll(); m.pump(); }, [&] { return m.ringing(); });
        CHECK(m.ringing(), "the caller connected -> the line RINGS");
        CHECK(!m.carrier(), "...but carrier stays DOWN until we answer");
        CHECK(m.status().ring, "...and RI is asserted (the level; the board times the bursts)");
        CHECK(!m.readable(), "...and the line is not readable");

        // The caller speaks BEFORE we pick up. Those bytes must sit in the kernel,
        // undelivered -- a modem that handed the guest data before it answered would
        // be answering for it.
        const uint8_t early[] = {'E', 'A', 'R', 'L', 'Y'};
        if (caller) caller->write(early, sizeof early);
        for (int i = 0; i < 10; ++i) {
            if (caller) caller->poll();
            m.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        uint8_t sink[16];
        CHECK(m.read(sink, sizeof sink) == 0, "ringing: read() returns 0 -- no early bytes");
        CHECK(!m.readable(), "...and readable() is still false");
        CHECK(!m.carrier(), "...and still no carrier");

        // PICK UP. Now the held bytes are ours, and carrier is up.
        m.answer();
        CHECK(m.carrier(), "answer() -> carrier UP");
        CHECK(!m.ringing(), "...and no longer ringing");
        CHECK(m.status().carrier && m.status().dsr, "...status reports carrier and DSR");

        std::string got;
        waitFor([&] { if (caller) caller->poll(); m.pump();
                      uint8_t b[16]; got.append((const char*)b, m.read(b, sizeof b)); },
                [&] { return got.size() >= 5; });
        CHECK(got == "EARLY", ("the bytes held during the ring arrive on answer (got '" +
                               got + "')").c_str());

        // ...and bytes cross the OTHER way now too.
        const uint8_t hello[] = {'H', 'I'};
        m.write(hello, sizeof hello);
        std::string back;
        waitFor([&] { if (caller) caller->poll();
                      uint8_t b[16]; if (caller) back.append((const char*)b, caller->read(b, sizeof b));
                      m.pump(); },
                [&] { return back.size() >= 2; });
        CHECK(back == "HI", ("the guest's bytes reach the caller (got '" + back + "')").c_str());

        // HANG UP. The call socket closes (the caller sees it) and the listener is
        // dropped, but describe() still reports the configuration.
        m.hangup();
        CHECK(!m.carrier(), "hangup() -> carrier down");
        bool gone = waitFor([&] { if (caller) { caller->poll(); uint8_t b[16]; caller->read(b, sizeof b); } },
                            [&] { return caller && caller->closed(); });
        CHECK(gone, "...and the call socket closed under the caller");
        CHECK(m.describe() == "modem:answer=" + std::to_string(port),
              "describe() still round-trips the config after a hangup");
    }

    // -----------------------------------------------------------------------
    // ORIGINATE. dial() calls out; the non-blocking handshake means the phone is
    // still ringing (connecting()) until it establishes, then carrier is up. A full
    // send buffer negates writable() -- the CTS backpressure the board maps to TDRE.
    // -----------------------------------------------------------------------
    SECTION("ModemLine -- dial() originates a call, and a full buffer negates CTS");
    {
        std::string err;
        auto        bbs = platform::listenTcp(0, err);  // the far end we dial
        CHECK(bbs != nullptr, ("a listener to dial into: " + err).c_str());

        if (bbs) {
            uint16_t port = bbs->port();
            ModemLine m("127.0.0.1", port, 0);

            CHECK(m.dial(err), ("dial() starts the call: " + err).c_str());
            CHECK(m.connecting() || m.carrier(),
                  "dialed: connecting (or already up on a fast loopback)");
            CHECK(!m.ringing(), "an outbound call is not a ring");

            std::unique_ptr<platform::TcpConn> server;
            waitFor([&] { m.pump(); if (!server) server = bbs->accept(); },
                    [&] { return m.carrier() && server; });
            CHECK(m.carrier(), "the far end answered -> carrier UP");
            CHECK(!m.connecting(), "...and no longer merely connecting");

            // Fill the pipe: the far end never reads, so the kernel send buffer fills
            // and then our tx_ does, and writable() (CTS) must go false rather than
            // drop a byte.
            std::vector<uint8_t> chunk(1024, 'x');
            for (int i = 0; i < 600 && m.writable(); ++i) {
                m.write(chunk.data(), chunk.size());
                m.pump();
            }
            CHECK(!m.writable(), "a full send buffer negates writable() -- this is CTS");
            CHECK(!m.status().cts, "...and status().cts follows it down");
        }
    }

    // -----------------------------------------------------------------------
    // IDLE = NO SOCKETS. A freshly built ModemLine binds nothing and dials
    // nothing: a caller to its answer port is REFUSED until armAnswer() runs.
    // -----------------------------------------------------------------------
    SECTION("ModemLine -- idle holds no sockets");
    {
        std::string err;
        uint16_t    port = freePort();
        CHECK(port != 0, "got a free port");

        ModemLine m("127.0.0.1", 4000, port);  // configured, but nothing opened yet

        // Nobody is listening on the answer port -- the modem is on-hook.
        auto refused = platform::connectTcp("127.0.0.1", port, err);
        if (refused) {
            bool closed = waitFor([&] { refused->poll(); }, [&] { return refused->closed(); });
            CHECK(closed, "idle modem: the answer port refuses -- no listener bound");
            CHECK(!refused->established(), "...and it never reports established()");
        } else {
            CHECK(true, "the answer port refused synchronously (also correct)");
        }

        // Arm it, and now the same call RINGS instead of bouncing.
        CHECK(m.armAnswer(err), ("armAnswer binds the port: " + err).c_str());
        auto caller = platform::connectTcp("127.0.0.1", port, err);
        bool rang = waitFor([&] { if (caller) caller->poll(); m.pump(); },
                            [&] { return m.ringing(); });
        CHECK(rang, "armAnswer(): now a caller RINGS instead of being refused");

        m.hangup();
        CHECK(!m.ringing(), "hangup() drops the ring");
    }

    // -----------------------------------------------------------------------
    // describe() names only the modes actually configured, so SHOW/CONFIG SAVE
    // round-trip what the operator gave.
    // -----------------------------------------------------------------------
    SECTION("ModemLine -- describe() round-trips the configuration");
    {
        CHECK(ModemLine("bbs.example", 23, 2323).describe() == "modem:dial=bbs.example:23,answer=2323",
              "dial + answer");
        CHECK(ModemLine("", 0, 2323).describe() == "modem:answer=2323", "answer only");
        CHECK(ModemLine("bbs.example", 23, 0).describe() == "modem:dial=bbs.example:23", "dial only");
    }
}
