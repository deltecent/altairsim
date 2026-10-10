#include "test.h"

#include "host/endpoint.h"
#include "host/mirror_stream.h"
#include "host/stream.h"
#include "platform/pty.h"
#include "platform/serial.h"
#include "platform/socket.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    bool   peerClosed() override { return closed_ && fromClient.empty(); }
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

// A fake serial port -- no driver, no cable, so every byte is asserted. `toTerm` is what
// the mirror sent the terminal; `fromTerm` is what the person typed there. `room` models
// the driver's buffer: the bytes it can still take before write() starts returning 0.
struct FakeSerial : platform::SerialPort {
    std::string toTerm;
    std::string fromTerm;
    size_t      room = SIZE_MAX;
    std::string path_ = "FAKE0";

    size_t read(uint8_t* buf, size_t n) override {
        size_t k = fromTerm.size() < n ? fromTerm.size() : n;
        std::memcpy(buf, fromTerm.data(), k);
        fromTerm.erase(0, k);
        return k;
    }
    size_t write(const uint8_t* buf, size_t n) override {
        size_t k = room < n ? room : n;
        toTerm.append((const char*)buf, k);
        if (room != SIZE_MAX) room -= k;
        return k;
    }
    bool                     configure(const platform::SerialConfig&, std::string&) override { return true; }
    platform::ModemLines     lines() const override { return {}; }
    void                     setControl(bool, bool) override {}
    void                     setBreak(bool) override {}
    void                     flush() override {}
    const std::string&       path() const override { return path_; }
};

// A mirror over a scripted line with a FakeSerial as its sink.
MirrorStream makeSerialMirror(ScriptedStream*& sc, FakeSerial*& fs, bool readOnly,
                              long long baud = 9600) {
    auto inner = std::make_unique<ScriptedStream>();
    sc         = inner.get();
    auto port  = std::make_unique<FakeSerial>();
    fs         = port.get();
    return MirrorStream(std::move(inner), "serial:FAKE0",
                        std::make_unique<SerialMirrorSink>(std::move(port), baud), readOnly);
}

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

// The watcher's end of a pseudo-terminal mirror: a program that opens the link, as
// `screen` would.
struct Watcher {
    std::unique_ptr<platform::PtyPeer> peer;
    explicit Watcher(const std::string& link) {
        std::string err;
        peer = platform::openPtyPeer(link, err);
    }
    bool ok() const { return peer != nullptr; }
    void type(const std::string& keys) {
        peer->write((const uint8_t*)keys.data(), keys.size());
    }
    // Everything that has arrived, appended to `seen`.
    void drain(std::string& seen) {
        uint8_t buf[4096];
        for (;;) {
            size_t r = peer->read(buf, sizeof buf);
            if (r == 0) break;
            seen.append((const char*)buf, r);
        }
    }
    bool echoes() const { return peer->echoes(); }
};

// A link path of our own in the temp folder -- removed first, so a run that died does
// not decide this one.
std::string ptyLink(const char* leaf) {
    namespace fs = std::filesystem;
    fs::path        p = fs::temp_directory_path() / leaf;
    std::error_code ec;
    fs::remove(p, ec);
    return p.string();
}

bool isLink(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_symlink(p, ec);
}

// The path itself is there -- a link counts even when what it points at is gone.
bool pathExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::symlink_status(p, ec).type() != std::filesystem::file_type::not_found;
}

bool isCharDevice(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_character_file(p, ec);
}

// Pump until the mirror has noticed the watcher (or its leaving).
bool pumpUntil(MirrorStream& m, bool watching) {
    return waitFor([&] {
        m.pump();
        return m.watching() == watching;
    });
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
        auto* ms = dynamic_cast<MirrorStream*>(mirror.get());
        if (mirror && port) CHECK(ms && !ms->watching(), "nobody has dialed in yet: no watcher");
        if (ms && port) {
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("a watcher dials in: " + err).c_str());

            // Wait for BOTH ends. The client reporting connected is not enough: the kernel
            // completes the handshake into the listen backlog, so connect() can finish
            // before pump() has accept()ed -- and until it has, the mirror has no watcher
            // and drops guest output BY DESIGN (a late watcher gets no backlog). Sending
            // BANNER in that window lost it: 27 of 40 runs on a fast Windows box.
            bool up = waitFor([&] {
                mirror->pump();  // answer the phone
                if (client) client->poll();
                return client && client->established() && ms->watching();
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
    if (platform::havePty()) {
    SECTION("mirror pty: `|pty:LINK` makes a pseudo-terminal and a link to it");
    {
        const std::string link = ptyLink("altairsim-test-make");
        std::string       err;
        auto              s = resolveEndpoint("scripted|pty:" + link, err);
        CHECK(s != nullptr, "scripted|pty:LINK resolves");
        if (s) {
            CHECK(isLink(link), "the link is there, and it is a symbolic link");
            CHECK(isCharDevice(link), "and it points at a character device");
            CHECK(s->describe() == "scripted|pty:" + link,
                  "describe() round-trips the operator's text for SHOW / CONFIG SAVE");
            auto* ms = dynamic_cast<MirrorStream*>(s.get());
            CHECK(ms && has(ms->sinkNote(), link) && has(ms->sinkNote(), "/dev/"),
                  "the note names the link and the device behind it");
            auto log = s->drainLog();
            CHECK(log.size() == 1 && has(log[0], link), "the operator is told where it is, once");
            CHECK(s->drainLog().empty(), "...and only once");
        }
        s.reset();
        CHECK(!pathExists(link), "the link is removed with the line");
    }

    SECTION("mirror pty: with nobody on it the guest's output is dropped, not kept");
    {
        const std::string link = ptyLink("altairsim-test-drop");
        std::string       err;
        auto              s  = resolveEndpoint("scripted|pty:" + link, err);
        auto*             ms = dynamic_cast<MirrorStream*>(s.get());
        CHECK(ms != nullptr, "the mirror resolves");
        if (ms) {
            ms->pump();
            CHECK(!ms->watching(), "nobody has opened the link: no watcher");
            put(*ms, "EARLY");

            Watcher w(link);
            CHECK(w.ok(), "a program opens the link");
            CHECK(pumpUntil(*ms, true), "and the mirror sees it arrive");
            put(*ms, "HELLO");
            std::string seen;
            CHECK(waitFor([&] { w.drain(seen); return seen.size() >= 5; }),
                  "the watcher sees what the guest prints now");
            CHECK(seen == "HELLO", "and none of what it printed before anyone was there");
        }
    }

    SECTION("mirror pty: every byte value arrives unchanged, with no echo back");
    {
        const std::string link = ptyLink("altairsim-test-raw");
        std::string       err;
        auto              s  = resolveEndpoint("scripted|pty:" + link, err);
        auto*             ms = dynamic_cast<MirrorStream*>(s.get());
        CHECK(ms != nullptr, "the mirror resolves");
        if (ms) {
            Watcher w(link);
            CHECK(pumpUntil(*ms, true), "the watcher is on the line");
            CHECK(!w.echoes(), "the line is raw before a byte is sent: no echo");

            std::string all;
            for (int i = 0; i < 256; ++i) all.push_back((char)i);
            put(*ms, all);
            std::string seen;
            CHECK(waitFor([&] { ms->pump(); w.drain(seen); return seen.size() >= 256; }),
                  "all 256 arrive");
            CHECK(seen == all, "byte for byte: no CR added to LF, nothing eaten as a control key");
            ms->pump();
            CHECK(!ms->readable(), "and none of it came back to the guest as typed keys");
        }
    }

    SECTION("mirror pty: what the watcher types reaches the guest; ?ro throws it away");
    {
        const std::string link = ptyLink("altairsim-test-type");
        std::string       err;
        {
            auto  s  = resolveEndpoint("scripted|pty:" + link, err);
            auto* ms = dynamic_cast<MirrorStream*>(s.get());
            CHECK(ms != nullptr, "the mirror resolves");
            if (ms) {
                Watcher w(link);
                CHECK(pumpUntil(*ms, true), "the watcher is on the line");
                w.type("DIR\r");
                CHECK(waitFor([&] { ms->pump(); return ms->readable(); }), "the keys arrive");
                CHECK(get(*ms, 16) == "DIR\r", "and the guest reads them, CR and all");
            }
        }
        {
            auto  s  = resolveEndpoint("scripted|pty:" + link + "?ro", err);
            auto* ms = dynamic_cast<MirrorStream*>(s.get());
            CHECK(ms != nullptr, "the read-only mirror resolves");
            if (ms) {
                Watcher w(link);
                CHECK(pumpUntil(*ms, true), "the watcher is on the line");
                w.type("DIR\r");
                for (int i = 0; i < 20; ++i) {
                    ms->pump();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                CHECK(!ms->readable(), "read-only: the keys go nowhere");
                put(*ms, "A>");
                std::string seen;
                CHECK(waitFor([&] { w.drain(seen); return seen == "A>"; }),
                      "but the watcher still sees the guest");
            }
        }
    }

    SECTION("mirror pty: a watcher leaves and another arrives, and the line is raw again");
    {
        const std::string link = ptyLink("altairsim-test-again");
        std::string       err;
        auto              s  = resolveEndpoint("scripted|pty:" + link, err);
        auto*             ms = dynamic_cast<MirrorStream*>(s.get());
        CHECK(ms != nullptr, "the mirror resolves");
        if (ms) {
            {
                Watcher w(link);
                CHECK(pumpUntil(*ms, true), "the first watcher is on the line");
            }
            CHECK(pumpUntil(*ms, false), "it closes, and the mirror sees it go");
            put(*ms, "UNSEEN");

            Watcher w2(link);
            CHECK(pumpUntil(*ms, true), "the next watcher is noticed");
            CHECK(!w2.echoes(), "a reopened line is raw again (it comes back cooked on macOS)");
            put(*ms, "LINE\n");
            std::string seen;
            CHECK(waitFor([&] { ms->pump(); w2.drain(seen); return seen.size() >= 5; }),
                  "it sees the guest");
            CHECK(seen == "LINE\n", "unchanged, and with nothing from while nobody was there");
        }
    }

    SECTION("mirror pty: a reopen the mirror never saw happen still leaves the line raw");
    {
        // Issue #687. A program that closes the slave and opens it again between two
        // pumps shows the mirror no "nobody there" in between -- and macOS has put the
        // line back in echo mode. Echo would send the guest's output back as typed keys.
        const std::string link = ptyLink("altairsim-test-fast-reopen");
        std::string       err;
        auto              s  = resolveEndpoint("scripted|pty:" + link, err);
        auto*             ms = dynamic_cast<MirrorStream*>(s.get());
        CHECK(ms != nullptr, "the mirror resolves");
        if (ms) {
            auto w = std::make_unique<Watcher>(link);
            CHECK(pumpUntil(*ms, true), "the first watcher is on the line");
            w.reset();
            Watcher w2(link);  // no pump between the close and this open
            ms->pump();
            CHECK(ms->watching(), "the mirror never saw the line empty");
            CHECK(!w2.echoes(), "and the reopened line is raw all the same");

            put(*ms, "LINE\n");
            std::string seen;
            CHECK(waitFor([&] { ms->pump(); w2.drain(seen); return seen.size() >= 5; }),
                  "the watcher sees the guest");
            CHECK(seen == "LINE\n", "unchanged");
            for (int i = 0; i < 20; ++i) ms->pump();
            uint8_t b[16];
            CHECK(ms->read(b, sizeof b) == 0, "and none of it came back as typed keys");
        }
    }

    SECTION("mirror pty: a watcher that does not read never stalls the guest");
    {
        const std::string link = ptyLink("altairsim-test-slow");
        std::string       err;
        auto              s  = resolveEndpoint("scripted|pty:" + link, err);
        auto*             ms = dynamic_cast<MirrorStream*>(s.get());
        CHECK(ms != nullptr, "the mirror resolves");
        if (ms) {
            Watcher w(link);
            CHECK(pumpUntil(*ms, true), "the watcher is on the line");
            const std::string chunk(1000, 'x');
            size_t            took = 0;
            for (int i = 0; i < 400; ++i)  // 400 KB: past the kernel buffer and the queue cap
                took += ms->write((const uint8_t*)chunk.data(), chunk.size());
            CHECK(took == 400u * 1000u, "the guest's every write is accepted in full");
            auto* sc = dynamic_cast<ScriptedStream*>(ms->inner());
            CHECK(sc && sc->out().size() == 400u * 1000u, "and its own line has every byte");
            CHECK(ms->watching(), "the watcher is still on the line");
        }
    }

    SECTION("mirror pty: a leftover link is replaced; anything else at LINK is refused");
    {
        const std::string link = ptyLink("altairsim-test-left");
        std::error_code ec;
        std::filesystem::create_symlink("/dev/altairsim-no-such-device", link, ec);
        CHECK(!ec && isLink(link), "a link from a run that did not clean up");
        std::string err;
        auto        s = resolveEndpoint("scripted|pty:" + link, err);
        CHECK(s != nullptr, "the leftover link is replaced");
        CHECK(isCharDevice(link), "and now points at ours");
        s.reset();

        { std::ofstream f(link); f << "mine"; }
        err.clear();
        CHECK(resolveEndpoint("scripted|pty:" + link, err) == nullptr,
              "an ordinary file at LINK is refused");
        CHECK(has(err, "not a link"), "and the error says why");
        std::ifstream f(link);
        std::string   body;
        f >> body;
        CHECK(body == "mine", "the file is untouched");
        f.close();
        std::filesystem::remove(link, ec);
    }

    SECTION("mirror pty: bare `|pty` takes the first free /tmp/altairsim{n}");
    {
        std::string err;
        auto        a  = resolveEndpoint("scripted|pty", err);
        auto        b  = resolveEndpoint("scripted|pty?ro", err);
        auto*       ma = dynamic_cast<MirrorStream*>(a.get());
        auto*       mb = dynamic_cast<MirrorStream*>(b.get());
        CHECK(ma && mb, "two numbered mirrors resolve");
        if (ma && mb) {
            auto linkOf = [](const std::string& note) { return note.substr(0, note.find(' ')); };
            const std::string la = linkOf(ma->sinkNote()), lb = linkOf(mb->sinkNote());
            CHECK(la.rfind("/tmp/altairsim", 0) == 0 && lb.rfind("/tmp/altairsim", 0) == 0,
                  "each is /tmp/altairsim{n}");
            CHECK(la != lb, "and they are two different names");
            CHECK(isLink(la) && isLink(lb), "both links are there");
            CHECK(a->describe() == "scripted|pty" && b->describe() == "scripted|pty?ro",
                  "describe() keeps the operator's text, not the number");
            a.reset();
            b.reset();
            CHECK(!pathExists(la) && !pathExists(lb), "both are removed with their lines");
        }
    }
    } else {
    SECTION("mirror pty: refused where there is no pseudo-terminal, with what to use");
    {
        std::string err;
        CHECK(resolveEndpoint("scripted|pty", err) == nullptr, "|pty is refused");
        CHECK(has(err, "not available on Windows") && has(err, "socket:PORT"),
              "and the error names the sink that works");
    }
    }

    SECTION("mirror pty: the grammar -- options, and what rebases");
    {
        std::string err;
        CHECK(resolveEndpoint("scripted|pty:", err) == nullptr, "`pty:` with no link is refused");
        CHECK(has(err, "link"), "and the error asks for the link");
        err.clear();
        CHECK(resolveEndpoint("scripted|pty?bogus", err) == nullptr,
              "an unknown option is refused before anything is made");
        CHECK(has(err, "ro"), "and the error names the one option there is");

        auto rebase = [](const std::string& p) { return "/cfg/" + p; };
        CHECK(rebaseEndpointPaths("scripted|pty", rebase) == "scripted|pty",
              "a bare pty sink is not a path");
        CHECK(rebaseEndpointPaths("scripted|pty?ro", rebase) == "scripted|pty?ro",
              "with or without an option");
        CHECK(rebaseEndpointPaths("scripted|pty:con", rebase) == "scripted|pty:/cfg/con",
              "the link after `pty:` is a path, and rebases like one");
    }
    SECTION("mirror serial: the guest's output reaches the port, every byte value unchanged");
    {
        ScriptedStream* sc = nullptr;
        FakeSerial*     fs = nullptr;
        MirrorStream    m  = makeSerialMirror(sc, fs, /*readOnly=*/false);
        CHECK(m.watching(), "a serial sink is always there: nothing says when a terminal is");
        std::string all;
        for (int i = 0; i < 256; ++i) all.push_back((char)i);
        put(m, all);
        CHECK(fs->toTerm == all, "all 256 values arrive, with nothing added or eaten");
        CHECK(sc->out() == all, "and the wrapped line got every byte too");
        m.pump();
        CHECK(!m.readable(), "the mirror echoes nothing back to the guest");
    }

    SECTION("mirror serial: what is typed at the port reaches the guest; ?ro throws it away");
    {
        ScriptedStream* sc = nullptr;
        FakeSerial*     fs = nullptr;
        MirrorStream    m  = makeSerialMirror(sc, fs, /*readOnly=*/false);
        fs->fromTerm = "DIR\r";
        CHECK(!m.readable(), "nothing is readable until pump drains the port");
        m.pump();
        CHECK(get(m, 16) == "DIR\r", "the guest reads exactly what was typed");

        ScriptedStream* sc2 = nullptr;
        FakeSerial*     fs2 = nullptr;
        MirrorStream    ro  = makeSerialMirror(sc2, fs2, /*readOnly=*/true);
        fs2->fromTerm = "rm -rf";
        ro.pump();
        CHECK(!ro.readable() && fs2->fromTerm.empty(),
              "read-only drains the port and drops the keys");
        put(ro, "OUT");
        CHECK(fs2->toTerm == "OUT", "but output still flows to the terminal");
    }

    SECTION("mirror serial: a port that takes a few bytes never stalls the guest");
    {
        ScriptedStream* sc = nullptr;
        FakeSerial*     fs = nullptr;
        MirrorStream    m  = makeSerialMirror(sc, fs, /*readOnly=*/false);
        fs->room = 4;  // the driver's buffer is nearly full
        put(m, "ABCDEFGHIJ");
        CHECK(fs->toTerm == "ABCD", "the port took what it could");
        CHECK(sc->out() == "ABCDEFGHIJ", "the guest's line got all ten: nothing was held up");
        CHECK(m.writable(), "and the guest is still allowed to write");

        fs->room = SIZE_MAX;  // the wire drains
        m.pump();
        CHECK(fs->toTerm == "ABCDEFGHIJ", "the rest follows at the next pump, in order");
    }

    SECTION("mirror serial: a port that never drains stays bounded and loses the oldest");
    {
        ScriptedStream* sc = nullptr;
        FakeSerial*     fs = nullptr;
        MirrorStream    m  = makeSerialMirror(sc, fs, /*readOnly=*/false);
        fs->room = 0;
        const std::string chunk(1024, 'x');
        for (int i = 0; i < 600; ++i) put(m, chunk);  // 600 KB against a 256 KB cap
        put(m, "TAIL");
        fs->room = SIZE_MAX;
        m.pump();
        CHECK(fs->toTerm.size() == 256 * 1024, "the backlog is the cap, no more");
        CHECK(fs->toTerm.compare(fs->toTerm.size() - 4, 4, "TAIL") == 0,
              "and it is the newest bytes that are kept");
    }

    SECTION("mirror serial: modem pins and the rate are the wrapped line's, not the port's");
    {
        ScriptedStream* sc = nullptr;
        FakeSerial*     fs = nullptr;
        MirrorStream    m  = makeSerialMirror(sc, fs, /*readOnly=*/false, 19200);
        LineParams      p;
        std::string     err;
        CHECK(m.setParams(p, err) == sc->setParams(p, err), "setParams goes to the inner line");
        CHECK(m.sinkNote() == "FAKE0 at 19200 baud", "the note names the port and the rate");
    }

    SECTION("mirror serial: the grammar -- baud, ro, and what is refused");
    {
        std::string err;
        CHECK(resolveEndpoint("scripted|serial:", err) == nullptr, "no device is refused");
        CHECK(has(err, "device"), "and the error asks for one");
        err.clear();
        CHECK(resolveEndpoint("scripted|serial:/dev/altairsim-no-such-port", err) == nullptr,
              "a port that is not there is refused at resolve time");
        CHECK(has(err, "cannot open"), "and the error says what failed");
        err.clear();
        CHECK(resolveEndpoint("scripted|serial:COM9?baud=fast", err) == nullptr,
              "a rate that is not a number is refused");
        CHECK(has(err, "baud"), "and the error names the option");
        err.clear();
        CHECK(resolveEndpoint("scripted|serial:COM9?baud=0", err) == nullptr, "baud=0 is refused");
        err.clear();
        CHECK(resolveEndpoint("scripted|serial:COM9?bogus", err) == nullptr,
              "an unknown option is refused");
        CHECK(has(err, "baud") && has(err, "ro"), "and the error lists baud and ro");
        err.clear();
        CHECK(resolveEndpoint("scripted|socket:2323?baud=9600", err) == nullptr,
              "baud is the serial sink's: the socket sink refuses it");

        auto rebase = [](const std::string& p) { return "/cfg/" + p; };
        CHECK(rebaseEndpointPaths("scripted|serial:/dev/cu.x?baud=19200", rebase) ==
                  "scripted|serial:/dev/cu.x?baud=19200",
              "a serial sink is a device name, not a file in the config folder");
        CHECK(rebaseEndpointPaths("in:t.tap|serial:COM3", rebase) == "in:/cfg/t.tap|serial:COM3",
              "while the inner path still rebases");
    }

    if (platform::havePty()) {
    SECTION("mirror serial: end to end on a real device (a pty slave stands in for the port)");
    {
        const std::string link = ptyLink("altairsim-test-serial");
        std::string       err;
        auto              term = platform::openPty(link, err);
        CHECK(term != nullptr, "a pseudo-terminal to play the terminal");
        if (term) {
            const std::string dev = term->device();
            auto s = resolveEndpoint("scripted|serial:" + dev + "?baud=19200", err);
            if (!s) std::printf("  note: %s\n", err.c_str());
            CHECK(s != nullptr, "the mirror opens the device as a serial port");
            if (s) {
                CHECK(s->describe() == "scripted|serial:" + dev + "?baud=19200",
                      "describe() round-trips for SHOW and CONFIG SAVE");
                auto log = s->drainLog();
                CHECK(log.size() == 1 && has(log[0], dev) && has(log[0], "19200"),
                      "the operator is told the device and the rate, once");
                std::string all;
                for (int i = 0; i < 256; ++i) all.push_back((char)i);
                put(*s, all);
                std::string seen;
                uint8_t     buf[4096];
                CHECK(waitFor([&] {
                          s->pump();
                          term->poll();
                          for (size_t r; (r = term->read(buf, sizeof buf)) > 0;)
                              seen.append((const char*)buf, r);
                          return seen.size() >= 256;
                      }),
                      "all 256 values reach the far end of the device");
                CHECK(seen == all, "byte for byte");

                const std::string typed = "DIR\r";
                term->write((const uint8_t*)typed.data(), typed.size());
                CHECK(waitFor([&] { s->pump(); return s->readable(); }), "keys typed there arrive");
                CHECK(get(*s, 16) == typed, "and the guest reads them unchanged");
            }
        }
    }
    }
}
