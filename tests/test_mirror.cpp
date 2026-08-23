#include "test.h"

#include "host/endpoint.h"
#include "host/mirror_stream.h"
#include "host/stream.h"
#include "platform/socket.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace altair;

namespace {

// A fake watcher session -- no kernel, no network, so every byte is deterministic and
// asserted. `toClient` is what the mirror sent us (the guest's output a watcher sees);
// `fromClient` is what the watcher typed (a section stages it, the mirror drains it on
// pump). Establish/close are flipped by the test to model the handshake and the hangup.
struct FakeConn : platform::TcpConn {
    std::string toClient;
    std::string fromClient;
    bool        established_ = true;
    bool        closed_      = false;
    std::string peer_        = "127.0.0.1:test";
    // A backpressure knob modelling a finite send buffer: SIZE_MAX = never full;
    // otherwise the bytes the socket can still take before write() starts returning 0
    // (a full buffer), so a section can prove that backpressure never stalls the guest.
    size_t      sinkFree = SIZE_MAX;

    bool   established() const override { return established_; }
    bool   closed() const override { return closed_; }
    size_t read(uint8_t* buf, size_t n) override {  // watcher -> mirror
        size_t k = fromClient.size() < n ? fromClient.size() : n;
        std::memcpy(buf, fromClient.data(), k);
        fromClient.erase(0, k);
        return k;
    }
    size_t write(const uint8_t* buf, size_t n) override {  // mirror -> watcher
        size_t k = sinkFree < n ? sinkFree : n;  // 0 when the buffer is full -- backpressure
        toClient.append((const char*)buf, k);
        if (sinkFree != SIZE_MAX) sinkFree -= k;
        return k;
    }
    void               poll() override {}
    void               close() override { closed_ = true; }
    const std::string& peer() const override { return peer_; }
};

// A listener that hands out ONE staged conn on the first accept, then nothing -- the
// mirror answers one watcher at a time. The test keeps a raw pointer to the conn so it
// can stage input and read output after the mirror has adopted it.
struct FakeListener : platform::TcpListener {
    std::unique_ptr<platform::TcpConn> pending;
    uint16_t                           port_ = 2323;
    std::unique_ptr<platform::TcpConn> accept() override { return std::move(pending); }
    uint16_t                           port() const override { return port_; }
};

void put(ByteStream& s, const std::string& bytes) {
    s.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

std::string get(ByteStream& s, size_t n) {
    std::vector<uint8_t> buf(n);
    size_t               got = s.read(buf.data(), n);
    return std::string(buf.begin(), buf.begin() + got);
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Build a mirror over a fresh ScriptedStream, returning the raw pointers a section needs
// to drive both ends: `sc` is the wrapped line (feed RX, read out), `fc` is the watcher.
MirrorStream makeMirror(ScriptedStream*& sc, FakeConn*& fc, bool readOnly,
                        const std::string& sinkSpec = "socket:2323") {
    auto inner = std::make_unique<ScriptedStream>();
    sc         = inner.get();
    auto conn  = std::make_unique<FakeConn>();
    fc         = conn.get();
    auto lis   = std::make_unique<FakeListener>();
    lis->pending = std::move(conn);
    return MirrorStream(std::move(inner), sinkSpec, std::move(lis), readOnly);
}

// A free TCP port the OS confirms unused -- bind port 0, read what it picked, drop it.
// The one section that binds a real listener (the resolver round-trip) uses this rather
// than hardcode a port and flake on the machine where something already owns it.
uint16_t freePort() {
    std::string err;
    if (auto probe = platform::listenTcp(0, err)) return probe->port();
    return 0;
}

// Poll `ready` for up to ~2 s of REAL time, one pass per 5 ms -- the loopback handshake
// and byte delivery are the kernel's to schedule, so a fixed spin can outrun the OS and
// CHECK a state it was never given a chance to reach (test_lines' lesson, sockettest's).
template <typename Fn>
bool waitFor(Fn ready, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

} // namespace

void test_mirror() {
    SECTION("mirror: the guest's output is copied to a connected watcher");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/false);

        // Output BEFORE anyone is watching goes nowhere -- a watcher who connects later
        // must not open to a faceful of stale text (host/tcp.cpp's rule).
        put(m, "EARLY");
        CHECK(fc->toClient.empty(), "output before a watcher connects is dropped, not buffered");
        CHECK(sc->out() == "EARLY", "...but the guest's own line got every byte");

        m.pump();  // the watcher answers the phone
        put(m, "HELLO");
        CHECK(fc->toClient == "HELLO", "once connected, the watcher sees the guest's output");
        CHECK(sc->out() == "EARLYHELLO", "and the wrapped line still got it all, unchanged");
    }

    SECTION("mirror: what the watcher types is injected as input the guest reads");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/false);
        m.pump();

        fc->fromClient = "DIR\r";
        CHECK(!m.readable(), "nothing is readable until pump drains the socket");
        m.pump();
        CHECK(m.readable(), "after pump the injected keystrokes make the line readable");
        CHECK(get(m, 16) == "DIR\r", "and the guest reads exactly what the watcher typed");
        CHECK(!m.readable(), "with the inject buffer drained the line is quiet again");
    }

    SECTION("mirror: injected take-over leads the wrapped line's own input");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/false);
        m.pump();

        sc->feed("AI");         // the inner line (under --mcp, the AI's scripted feed)
        fc->fromClient = "H";   // a human taking over
        m.pump();
        // The human is driving: their byte leads, then the inner's flow through.
        CHECK(get(m, 1) == "H", "the watcher's keystroke is served first");
        CHECK(get(m, 8) == "AI", "then the wrapped line's own input follows");
    }

    SECTION("mirror: the mirror adds no echo -- only the guest's writes reach the watcher");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/false);
        m.pump();

        fc->fromClient = "D";
        m.pump();
        (void)get(m, 1);  // the guest READS the injected 'D'
        CHECK(fc->toClient.empty(),
              "reading injected input echoes nothing -- the guest is the echo authority");
        // Only when the guest WRITES (as a monitor echoing) does the watcher see it.
        put(m, "D");
        CHECK(fc->toClient == "D", "the watcher sees the character only via the guest's echo");
    }

    SECTION("mirror: read-only suppresses the inject path (watch, no take-over)");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/true);
        m.pump();

        fc->fromClient = "rm -rf";  // a spectator leaning on the keyboard
        m.pump();
        CHECK(!m.readable(), "a read-only watcher's keystrokes never reach the line");
        CHECK(get(m, 16).empty(), "the guest reads nothing from a spectator");

        // But it is still a WATCHER: output flows to it as ever.
        put(m, "OUT");
        CHECK(fc->toClient == "OUT", "read-only still mirrors the guest's output to the watcher");
    }

    SECTION("mirror: a slow watcher never stalls the guest (bytes queue, guest flows)");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/false);
        m.pump();

        fc->sinkFree = 2;  // the watcher's send buffer has room for only 2 bytes
        put(m, "ABCDEF");
        CHECK(m.writable(), "the guest's writable() is the inner line's -- a slow watcher cannot clear it");
        CHECK(fc->toClient == "AB", "only what the socket took went out; the rest is queued");
        fc->sinkFree = SIZE_MAX;  // the watcher catches up
        m.pump();
        CHECK(fc->toClient == "ABCDEF", "pump flushes the backlog once the watcher can take it");
    }

    SECTION("mirror: a watcher hanging up mid-session is clean; the guest carries on");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, /*readOnly=*/false);
        m.pump();
        put(m, "HI");
        CHECK(fc->toClient == "HI", "the watcher is receiving output");

        fc->closed_ = true;  // the far end hangs up
        m.pump();            // the mirror drops the dead session
        // The guest writes on, into the air, exactly as a 6850 with no modem attached.
        put(m, "BYE");
        CHECK(sc->out() == "HIBYE", "the guest's own line is unaffected by the hangup");
        CHECK(m.writable(), "and the line stays writable -- no wedged session");
    }

    SECTION("mirror: modem pins and flow control are the wrapped line's, not the watcher's");
    {
        // A loopback inner reflects control back as status, so we can prove the mirror
        // forwards the pins untouched -- a watcher connecting does not move the carrier.
        auto            inner = std::make_unique<LoopbackStream>();
        auto            lis   = std::make_unique<FakeListener>();
        lis->pending          = std::make_unique<FakeConn>();
        MirrorStream    m(std::move(inner), "socket:2323", std::move(lis), false);

        CHECK(!m.status().carrier, "carrier starts down -- the loopback's DTR is low");
        m.setControl(LineControl{true, true, false});  // raise DTR through the mirror
        CHECK(m.status().carrier, "the pin reaches the wrapped line: DTR->DCD reflected back");
    }

    // ---- grammar, through the REAL resolver an operator's CONNECT uses ----

    SECTION("mirror: describe() round-trips inner|socket:PORT for SHOW / CONFIG SAVE");
    {
        ScriptedStream* sc = nullptr;
        FakeConn*       fc = nullptr;
        MirrorStream    m  = makeMirror(sc, fc, false, "socket:2323?ro");
        CHECK(m.describe() == "scripted|socket:2323?ro",
              "describe echoes the wrapped line and the socket sink, options and all");
    }

    SECTION("mirror: a socket: right side selects the mirror, and it binds a real port");
    {
        std::string err;
        uint16_t    port = freePort();
        CHECK(port != 0, "the OS hands us a free port");

        std::string spec = "scripted|socket:" + std::to_string(port);
        auto        s    = resolveEndpoint(spec, err);
        CHECK(s != nullptr, ("scripted|socket:PORT resolves to a mirror: " + err).c_str());
        if (s) {
            CHECK(s->describe() == spec, "and describe() round-trips what was typed");
            CHECK(dynamic_cast<MirrorStream*>(s.get()) != nullptr,
                  "a socket right side is a MirrorStream, not a TeeStream");
        }
    }

    SECTION("mirror: end to end over a REAL socket -- a client watches and takes over");
    {
        // The fakes above prove the LOGIC; this proves the plumbing against the kernel's
        // own TCP stack -- listenTcp/accept and a real TcpConn, the one thing a fake
        // cannot stand in for. Same shape as test_lines' socket sections: a client in the
        // same process, but two real sockets and a real handshake.
        std::string err;
        uint16_t    port = freePort();
        CHECK(port != 0, "the OS hands us a free port");

        auto mirror = resolveEndpoint("scripted|socket:" + std::to_string(port), err);
        CHECK(mirror != nullptr, ("scripted|socket:PORT binds a real listener: " + err).c_str());
        if (mirror && port) {
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("a watcher dials in: " + err).c_str());

            bool up = waitFor([&] {
                mirror->pump();  // answer the phone
                if (client) client->poll();
                return client && client->established();
            });
            CHECK(up, "the watcher connects and the mirror accepts it");

            // Guest output crosses the real wire to the watcher.
            put(*mirror, "BANNER\r\n");
            mirror->flush();
            std::string seen;
            waitFor([&] {
                mirror->pump();
                client->poll();
                uint8_t b[64];
                seen.append((const char*)b, client->read(b, sizeof b));
                return seen.find("BANNER") != std::string::npos;
            });
            CHECK(seen.find("BANNER") != std::string::npos,
                  "the watcher receives the guest's output over the real socket");

            // The watcher types; the guest reads it back through the mirror (take-over).
            const uint8_t typed[] = {'D', 'I', 'R', '\r'};
            if (client) client->write(typed, sizeof typed);
            std::string got;
            waitFor([&] {
                if (client) client->poll();
                mirror->pump();  // drain the socket into the inject buffer
                got += get(*mirror, 16);
                return got.find("DIR\r") != std::string::npos;
            });
            CHECK(got.find("DIR\r") != std::string::npos,
                  "and what the watcher types is injected as input the guest reads");
        }
    }

    SECTION("mirror: the ro option parses; an unknown option or bad port is refused early");
    {
        std::string err;
        // These all fail BEFORE binding a listener -- pure grammar, no network.
        CHECK(resolveEndpoint("scripted|socket:notaport", err) == nullptr,
              "a port that isn't a number is refused");
        err.clear();
        CHECK(resolveEndpoint("scripted|socket:bbs.example:23", err) == nullptr,
              "a host:port mirror sink is refused -- a mirror listens, it does not dial");
        CHECK(has(err, "listens"), "and the error explains the mirror listens for a watcher");
        err.clear();
        CHECK(resolveEndpoint("scripted|socket:2323?bogus", err) == nullptr,
              "an unknown mirror option is refused");
        CHECK(has(err, "ro"), "and the error names the one option there is");
    }

    SECTION("mirror: rebaseEndpointPaths leaves a socket sink alone but rebases a file sink");
    {
        auto rebase = [](const std::string& p) { return "/cfg/" + p; };
        CHECK(rebaseEndpointPaths("in:tape.tap|socket:2323", rebase) ==
                  "in:/cfg/tape.tap|socket:2323",
              "the inner path rebases; the socket mirror sink is untouched");
        CHECK(rebaseEndpointPaths("in:tape.tap|cap.hex", rebase) ==
                  "in:/cfg/tape.tap|/cfg/cap.hex",
              "a FILE sink still rebases, as the tee always has");
    }
}
