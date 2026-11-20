// `telnet:` -- the socket line that speaks the Telnet protocol, ON THE REAL NETWORK
// STACK. Everything here is provable over loopback with nothing but a free port: a
// raw client connects, and we assert the exact bytes the negotiation puts on the
// wire and the exact bytes the guest reads back out. The point of the endpoint is
// the handshake a raw socket: cannot do, so the handshake is what the test pins.
//
// Like the socket sections in test_lines.cpp / test_modemline.cpp, this touches the
// kernel TCP stack, so every state change is waited for by WALL CLOCK (waitFor).

#include "core/machine.h"
#include "core/version.h"
#include "host/endpoint.h"
#include "host/stream.h"
#include "host/telnet_codec.h"
#include "platform/socket.h"
#include "test.h"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace altair;

namespace {

// Telnet bytes, named (RFC 854/857/858). Only what the assertions reference.
constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WILL = 251;
constexpr uint8_t OPT_ECHO = 1, OPT_SGA = 3;

// Bytes as a std::string, for exact compares against the codec's buffers.
std::string bytes(std::initializer_list<uint8_t> b) {
    return std::string(b.begin(), b.end());
}

// Feed the codec guest bytes from an initializer list.
void guestOut(TelnetCodec& c, std::initializer_list<uint8_t> b) {
    std::vector<uint8_t> v(b);
    c.send(v.data(), v.size());
}

// A free port, the OS's way (test_modemline's trick): bind 0, read what it picked,
// drop it. A tiny race remains, which is why the endpoint rebinds it immediately.
uint16_t freePort() {
    std::string err;
    if (auto l = platform::listenTcp(0, err)) return l->port();
    return 0;
}

template <class Step, class Pred>
bool waitFor(Step step, Pred done) {
    for (int i = 0; i < 200 && !done(); ++i) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

// Everything currently readable on a raw conn, as a string.
std::string drain(platform::TcpConn* c) {
    std::string s;
    uint8_t     b[512];
    for (;;) {
        size_t r = c->read(b, sizeof b);
        if (r == 0) break;
        s.append((const char*)b, r);
    }
    return s;
}

} // namespace

void test_telnet() {
    // -----------------------------------------------------------------------
    // THE LISTEN (SERVER) ROLE. A human telnets in; on connect the server offers
    // exactly WILL ECHO, WILL SGA, DO SGA -- the trio that makes a stock client drop
    // its local echo and go character-at-a-time. Inbound IAC is stripped, CR LF folds
    // to a bare CR, IAC IAC is one literal 0xFF, and an unknown option is refused.
    // -----------------------------------------------------------------------
    SECTION("telnet: -- a LISTEN offers WILL ECHO/SGA so a client drops local echo");
    {
        std::string err;
        uint16_t    port = freePort();
        CHECK(port != 0, "got a free port to listen on");

        auto stream = resolveEndpoint("telnet:" + std::to_string(port), err);
        CHECK(stream != nullptr, ("telnet:PORT resolves: " + err).c_str());
        CHECK(stream && stream->describe() == "telnet:" + std::to_string(port),
              "describe() round-trips telnet:PORT (SHOW / CONFIG SAVE)");

        auto client = platform::connectTcp("127.0.0.1", port, err);
        CHECK(client != nullptr, ("a client telnets in: " + err).c_str());

        // The negotiation lands the instant the listener accepts.
        std::string neg;
        waitFor([&] { if (client) client->poll();
                      if (stream) stream->pump();
                      if (client) neg += drain(client.get()); },
                [&] { return client && client->established() && neg.size() >= 9; });

        const std::string wantNeg = {(char)IAC, (char)WILL, (char)OPT_ECHO,
                                     (char)IAC, (char)WILL, (char)OPT_SGA,
                                     (char)IAC, (char)DO,   (char)OPT_SGA};
        CHECK(neg == wantNeg, "the server offers WILL ECHO, WILL SGA, DO SGA on connect");

        // The client speaks: an unknown option it WILLs (server must refuse), then
        // data with a CR LF line ending and an escaped 0xFF in the middle.
        const std::vector<uint8_t> in = {IAC, WILL, 24, 'A', 'B', '\r', '\n', IAC, IAC, 'Z'};
        if (client) client->write(in.data(), in.size());

        std::string got, resp;
        waitFor([&] { if (client) client->poll();
                      if (stream) { stream->pump();
                          uint8_t b[64]; size_t r;
                          while ((r = stream->read(b, sizeof b)) > 0) got.append((const char*)b, r); }
                      if (client) resp += drain(client.get()); },
                [&] { return got.size() >= 5 && resp.size() >= 3; });

        const std::string wantData = {'A', 'B', '\r', '\xFF', 'Z'};
        CHECK(got == wantData,
              "inbound IAC stripped, CR LF folds to CR, IAC IAC yields one literal 0xFF");
        const std::string wantResp = {(char)IAC, (char)DONT, 24};
        CHECK(resp == wantResp, "an unknown option the client WILLs is refused with DONT");

        // The guest writes back a byte with a 0xFF in it: it must be DOUBLED on the wire.
        const uint8_t out[] = {'X', 0xFF, 'Y'};
        if (stream) stream->write(out, sizeof out);
        std::string wire;
        waitFor([&] { if (client) client->poll();
                      if (stream) stream->pump();
                      if (client) wire += drain(client.get()); },
                [&] { return wire.size() >= 4; });
        const std::string wantWire = {'X', '\xFF', '\xFF', 'Y'};
        CHECK(wire == wantWire, "a data 0xFF is doubled (IAC IAC) on the way out");
    }

    // -----------------------------------------------------------------------
    // THE DIAL-OUT (CLIENT) ROLE. telnet:HOST:PORT dials a far end and takes the
    // client's part: it asks the SERVER to echo and suppress go-ahead, and never
    // offers to echo itself.
    // -----------------------------------------------------------------------
    SECTION("telnet: -- a dial-OUT asks the far end to echo (DO ECHO / DO SGA)");
    {
        std::string err;
        auto        bbs = platform::listenTcp(0, err);  // the far end we dial
        CHECK(bbs != nullptr, ("a far-end listener to dial: " + err).c_str());

        if (bbs) {
            uint16_t port   = bbs->port();
            auto     stream = resolveEndpoint("telnet:127.0.0.1:" + std::to_string(port), err);
            CHECK(stream != nullptr, ("telnet:HOST:PORT resolves: " + err).c_str());

            std::unique_ptr<platform::TcpConn> server;
            std::string                        neg;
            waitFor([&] { if (stream) stream->pump();
                          if (!server) server = bbs->accept();
                          if (server) { server->poll(); neg += drain(server.get()); } },
                    [&] { return server && neg.size() >= 6; });

            const std::string wantNeg = {(char)IAC, (char)DO, (char)OPT_ECHO,
                                         (char)IAC, (char)DO, (char)OPT_SGA};
            CHECK(neg == wantNeg, "the client asks the server to echo and suppress go-ahead");
        }
    }

    // -----------------------------------------------------------------------
    // The grammar is socket:'s, so its errors are too: a bad port is caught, and the
    // scheme names itself in the message.
    // -----------------------------------------------------------------------
    SECTION("telnet: -- grammar errors name the scheme");
    {
        std::string err;
        CHECK(resolveEndpoint("telnet:", err) == nullptr, "telnet: with no port refuses");
        CHECK(err.find("telnet:") != std::string::npos, "...and the error names telnet:");
        err.clear();
        CHECK(resolveEndpoint("telnet:notaport", err) == nullptr, "telnet: with a bad port refuses");
    }

    // -----------------------------------------------------------------------
    // THE CONNECT BANNER. A LISTEN that greets owes each caller one line --
    // "Connected to AltairSim ... (sio0:b) on port N" -- and pays it when greet()
    // names the line. telnet: greets by default (a person is calling), behind its
    // option negotiation and telnet-encoded; socket: does not unless asked (it is the
    // raw pipe another machine dials, where a banner would be data in the far guest).
    // -----------------------------------------------------------------------
    auto bannerFor = [](const std::string& owner, uint16_t port) {
        return std::string("Connected to ") + versionString() + " (" + owner + ") on port " +
               std::to_string(port) + "\r\n";
    };

    // Connect a raw client to `spec` (which listens on `port`), pump until it is
    // accepted and whatever the stream sends unprompted has landed, greet as `owner`,
    // and return every byte the client received.
    auto callIn = [](ByteStream* stream, uint16_t port, const std::string& owner,
                     std::unique_ptr<platform::TcpConn>& client) {
        std::string err, got;
        client = platform::connectTcp("127.0.0.1", port, err);
        waitFor([&] { if (client) client->poll();
                      stream->pump();
                      stream->greet(owner);
                      if (client) got += drain(client.get()); },
                [&] { return client && client->established() && stream->status().carrier &&
                             got.find('\n') != std::string::npos; });
        // One more round for anything still in flight (the quiet cases wait it out).
        for (int i = 0; i < 5; ++i) {
            if (client) client->poll();
            stream->pump();
            stream->greet(owner);
            if (client) got += drain(client.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return got;
    };

    SECTION("banner: telnet:PORT greets each caller, after the negotiation");
    {
        std::string err;
        uint16_t    port   = freePort();
        auto        stream = resolveEndpoint("telnet:" + std::to_string(port), err);
        CHECK(stream != nullptr, ("telnet:PORT resolves: " + err).c_str());
        if (stream) {
            std::unique_ptr<platform::TcpConn> client;
            std::string got = callIn(stream.get(), port, "sio0:b", client);
            const std::string wantNeg = {(char)IAC, (char)WILL, (char)OPT_ECHO,
                                         (char)IAC, (char)WILL, (char)OPT_SGA,
                                         (char)IAC, (char)DO,   (char)OPT_SGA};
            CHECK(got == wantNeg + bannerFor("sio0:b", port),
                  "the negotiation, then the banner naming the line and the port -- once");
            CHECK(ByteStream::greetingsDue() == 0, "nothing is owed once the banner is sent");

            // The caller hangs up; the NEXT caller is greeted again.
            client.reset();
            waitFor([&] { stream->pump(); }, [&] { return !stream->status().carrier; });
            got = callIn(stream.get(), port, "sio0:b", client);
            CHECK(got == wantNeg + bannerFor("sio0:b", port), "a new caller is greeted afresh");
        }
    }

    SECTION("banner: telnet:PORT?banner=off and plain socket:PORT stay quiet");
    {
        std::string err;
        uint16_t    port   = freePort();
        auto        stream = resolveEndpoint("telnet:" + std::to_string(port) + "?banner=off", err);
        CHECK(stream != nullptr, ("telnet:PORT?banner=off resolves: " + err).c_str());
        CHECK(stream && stream->describe() == "telnet:" + std::to_string(port) + "?banner=off",
              "describe() round-trips the option (SHOW / CONFIG SAVE)");
        if (stream) {
            std::unique_ptr<platform::TcpConn> client;
            std::string got = callIn(stream.get(), port, "sio0:b", client);
            CHECK(got.find("Connected") == std::string::npos, "?banner=off: no banner");
            CHECK(ByteStream::greetingsDue() == 0, "...and none is owed");
        }

        port   = freePort();
        stream = resolveEndpoint("socket:" + std::to_string(port), err);
        CHECK(stream != nullptr, ("socket:PORT resolves: " + err).c_str());
        if (stream) {
            std::unique_ptr<platform::TcpConn> client;
            std::string got = callIn(stream.get(), port, "sio0:b", client);
            CHECK(got.empty(), "a raw socket:PORT sends nothing of its own by default");
            CHECK(ByteStream::greetingsDue() == 0, "...and owes nothing");
        }
    }

    SECTION("banner: socket:PORT?banner greets raw");
    {
        std::string err;
        uint16_t    port   = freePort();
        auto        stream = resolveEndpoint("socket:" + std::to_string(port) + "?banner", err);
        CHECK(stream != nullptr, ("socket:PORT?banner resolves: " + err).c_str());
        if (stream) {
            std::unique_ptr<platform::TcpConn> client;
            std::string got = callIn(stream.get(), port, "sio0:b", client);
            CHECK(got == bannerFor("sio0:b", port), "exactly the banner, no telnet bytes");
        }
    }

    SECTION("banner: a caller who leaves before the banner is owed nothing");
    {
        std::string err;
        uint16_t    port   = freePort();
        auto        stream = resolveEndpoint("socket:" + std::to_string(port) + "?banner", err);
        if (stream) {
            auto client = platform::connectTcp("127.0.0.1", port, err);
            waitFor([&] { if (client) client->poll(); stream->pump(); },
                    [&] { return stream->status().carrier; });
            CHECK(ByteStream::greetingsDue() == 1, "an accepted caller is owed the banner");
            client.reset();
            waitFor([&] { stream->pump(); }, [&] { return !stream->status().carrier; });
            CHECK(ByteStream::greetingsDue() == 0, "...and forgiven it on hanging up first");
        }
        CHECK(stream != nullptr, ("socket:PORT?banner resolves: " + err).c_str());
    }

    SECTION("banner: grammar -- a dial-out cannot greet, an unknown option refuses");
    {
        std::string err;
        CHECK(resolveEndpoint("socket:localhost:2323?banner", err) == nullptr,
              "?banner on a dial-out refuses");
        CHECK(err.find("listening") != std::string::npos, "...and says it is for a listener");
        err.clear();
        CHECK(resolveEndpoint("telnet:2323?bogus", err) == nullptr, "an unknown option refuses");
        CHECK(err.find("bogus") != std::string::npos, "...and names it");
        err.clear();
        CHECK(resolveEndpoint("telnet:2323?banner=maybe", err) == nullptr,
              "banner wants a boolean");
    }

    // THE MACHINE NAMES THE LINE. A board resolves its own endpoint and never tells
    // the stream which line it is; Machine::pump() does, for any stream owing a banner.
    SECTION("banner: Machine::pump() names the board line in the banner");
    {
        std::string err;
        Machine     m;
        Board*      sio = m.add("2sio", "sio0", err);
        CHECK(sio != nullptr, ("add a 2SIO: " + err).c_str());
        uint16_t port = freePort();
        CHECK(sio && sio->connect("b", "telnet:" + std::to_string(port), err),
              ("CONNECT sio0:b telnet:PORT: " + err).c_str());
        if (sio) {
            auto        client = platform::connectTcp("127.0.0.1", port, err);
            std::string got;
            waitFor([&] { if (client) client->poll();
                          m.pump();
                          if (client) got += drain(client.get()); },
                    [&] { return got.find('\n') != std::string::npos; });
            CHECK(got.find(bannerFor("sio0:b", port)) != std::string::npos,
                  "the caller is greeted as sio0:b, named by the machine");
        }
    }

    // -----------------------------------------------------------------------
    // A GUEST CR ON THE WIRE (RFC 854), issue #957. The codec itself, with no socket,
    // so every check is exact bytes. RFC 854 allows a CR only as CR LF or CR NUL, and
    // the far end drops the NUL of CR NUL. So a guest CR not followed by LF gets a NUL,
    // and the far end gets the guest's bytes. Without that NUL, an XMODEM block holding
    // 0D 00 lost the 00 on every retry.
    // -----------------------------------------------------------------------
    SECTION("telnet codec: NVT line -- a guest CR not followed by LF gets a NUL");
    {
        TelnetCodec c;
        c.reset(true);
        c.takeOut();  // the opening offers, pinned by the socket section above

        guestOut(c, {'\r', 0x00});
        CHECK(c.takeOut() == bytes({'\r', 0x00, 0x00}), "guest 0D 00 goes out as 0D 00 00");
        guestOut(c, {'\r', '\n'});
        CHECK(c.takeOut() == bytes({'\r', '\n'}), "guest 0D 0A goes out unchanged");
        guestOut(c, {'\r', 'A'});
        CHECK(c.takeOut() == bytes({'\r', 0x00, 'A'}), "guest 0D 41 goes out as 0D 00 41");
        guestOut(c, {'\r', '\r', '\n'});
        CHECK(c.takeOut() == bytes({'\r', 0x00, '\r', '\n'}),
              "guest 0D 0D 0A: the first CR gets its NUL, the CR LF does not");

        // The guest writes one byte per call: the CR goes out at once, the NUL it owes
        // goes in front of the next byte.
        guestOut(c, {'\r'});
        CHECK(c.takeOut() == bytes({'\r'}), "a lone CR goes out at once, nothing held back");
        guestOut(c, {0x00});
        CHECK(c.takeOut() == bytes({0x00, 0x00}), "...and the next call puts the owed NUL first");
        guestOut(c, {'\r'});
        c.takeOut();
        guestOut(c, {IAC});
        CHECK(c.takeOut() == bytes({0x00, IAC, IAC}), "CR then 0xFF: NUL, then the doubled IAC");
    }

    // The round trip: what the codec sends, an RFC 854 receiver turns back into the
    // guest's bytes. The receiver is written here from the RFC, not taken from the code
    // under test: CR NUL is a CR, IAC IAC is 0xFF, every other byte is itself. That is
    // what Tera Term did by hand (issue #957); this pins it as exact bytes.
    SECTION("telnet codec: NVT line -- an RFC 854 receiver gets the guest's bytes back");
    {
        auto rfc854Receive = [](const std::string& wire) {
            std::string data;
            for (size_t i = 0; i < wire.size(); ++i) {
                data.push_back(wire[i]);
                bool pair = i + 1 < wire.size();
                if (pair && wire[i] == '\r' && wire[i + 1] == '\0') ++i;  // CR NUL -> CR
                else if (pair && (uint8_t)wire[i] == IAC && (uint8_t)wire[i + 1] == IAC)
                    ++i;  // IAC IAC -> 0xFF
            }
            return data;
        };
        TelnetCodec c;
        c.reset(true);
        c.takeOut();
        const std::string guest =
            bytes({'\r', '\n', '\r', 0x00, '\r', 'A', IAC, '\r', '\r', '\n', '\r', 0x00, 0x00});
        c.send((const uint8_t*)guest.data(), guest.size());
        CHECK(rfc854Receive(c.takeOut()) == guest,
              "CR LF, CR NUL, CR A, 0xFF and CR CR LF all arrive as the guest sent them");
    }
}
