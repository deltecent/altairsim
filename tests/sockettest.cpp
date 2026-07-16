// THE SOCKET LAYER, ON THE REAL NETWORK STACK.
//
// tests/test_lines.cpp already drives a `socket:` endpoint over loopback and proves
// the happy path: a client connects, DCD rises, bytes cross, the client hangs up and
// carrier drops. That runs in the `unit` aggregate and needs nothing but a free port.
//
// This is the half that loopback-in-one-process does NOT reach: the NON-BLOCKING
// CONNECT, both when it succeeds against a real listener and -- the one that matters --
// when it is REFUSED. docs/porting-notes.md named the refusal as the single most
// likely thing to be wrong on Winsock, because a non-blocking connect reports failure
// by landing in select()'s THIRD fd_set (the except set) that POSIX ignores, and a
// port scanner that read that set wrong would see every dead port as a live one.
//
// It uses only platform/socket.h -- no OS type crosses the boundary, so §2.1 is happy
// and this file builds and runs identically on every host. It is labelled `hw` not
// because it needs a cable but because, like serialtest, it touches the real world:
// the kernel's TCP stack, not a ScriptedStream.

#include "platform/socket.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace altair::platform;

int g_fail = 0;
int g_run  = 0;

namespace {

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_run;                                                             \
        bool ok_ = (cond);                                                   \
        if (!ok_) ++g_fail;                                                  \
        std::printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", (msg));            \
    } while (0)

// A TCP round trip is the kernel's to schedule, not ours -- the three-way handshake,
// the byte delivery and the RST of a refusal all take real time, however little. So:
// poll briefly and give up with a verdict rather than spin a fixed count and CHECK a
// state the OS was never given a chance to reach. (This is the same lesson test_lines
// learned when its socket sections flaked on a fast machine.)
template <typename Fn>
bool waitFor(Fn ready, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

} // namespace

int main() {
    std::string err;

    // -----------------------------------------------------------------------
    // A non-blocking connect that SUCCEEDS, across the real stack.
    //
    // A listener on an OS-picked port is the far end; connectTcp is the client. Same
    // two-endpoint shape as a telnet-in, run in one process so no second binary is
    // needed -- but the sockets are two real kernel objects and the handshake is real.
    // -----------------------------------------------------------------------
    std::printf("A non-blocking connect that SUCCEEDS\n");

    auto listener = listenTcp(0, err);
    CHECK(listener != nullptr, ("listenTcp binds a port: " + err).c_str());
    if (!listener) {
        std::printf("\n%d checks, %d failed\n", g_run, g_fail);
        return 1;
    }
    uint16_t port = listener->port();
    std::printf("  (listening on 127.0.0.1:%u)\n", port);

    auto client = connectTcp("127.0.0.1", port, err);
    CHECK(client != nullptr, ("connectTcp starts (may still be handshaking): " + err).c_str());

    // The far end accepts; the client's connect finishes via poll(), which on Winsock
    // watches the WRITE set for success. Both sides must come up.
    std::unique_ptr<TcpConn> server;
    bool up = waitFor([&] {
        if (client) client->poll();
        if (!server) server = listener->accept();
        return client && client->established() && server != nullptr;
    });
    CHECK(up, "poll() completes the handshake -> established()");
    CHECK(client && client->established(), "the client reports established()");
    CHECK(server != nullptr, "the listener's accept() returns the far end");

    if (client && server) {
        // Bytes, both ways.
        const uint8_t hi[] = {'H', 'I'};
        client->write(hi, 2);
        std::string got;
        waitFor([&] {
            server->poll();
            uint8_t b[64];
            got.append((const char*)b, server->read(b, sizeof b));
            return got.size() >= 2;
        });
        CHECK(got == "HI", ("client -> server carries the bytes (got '" + got + "')").c_str());

        const uint8_t yo[] = {'Y', 'O'};
        server->write(yo, 2);
        std::string back;
        waitFor([&] {
            client->poll();
            uint8_t b[64];
            back.append((const char*)b, client->read(b, sizeof b));
            return back.size() >= 2;
        });
        CHECK(back == "YO", ("server -> client carries the bytes (got '" + back + "')").c_str());

        // The far end hangs up. recv() returns 0, which the layer must turn into
        // closed() -- that is the carrier drop the 6850 above latches on.
        server->close();
        bool gone = waitFor([&] {
            client->poll();
            uint8_t b[64];
            client->read(b, sizeof b);  // the 0-byte recv that detects the hangup
            return client->closed();
        });
        CHECK(gone, "the far end closes -> the client sees closed() (a carrier drop)");
    }

    // -----------------------------------------------------------------------
    // A non-blocking connect that is REFUSED -- THE PATH porting-notes FLAGGED.
    //
    // Nobody is listening. On Winsock the refusal surfaces in select()'s except set,
    // which socket_win32.cpp::poll() watches ON PURPOSE. The session must end up
    // closed(), and must NEVER once report established(): a phone that rang and got a
    // disconnect tone is not a phone somebody answered.
    // -----------------------------------------------------------------------
    std::printf("\nA non-blocking connect that is REFUSED\n");

    // Bind a port, read its number, drop it -- so the number is one the OS just
    // confirmed free, with nothing accepting on it.
    uint16_t deadPort = 0;
    {
        auto probe = listenTcp(0, err);
        if (probe) deadPort = probe->port();
    }  // probe destroyed here: the port is now free and unlistened
    CHECK(deadPort != 0, "got a free port with nothing listening on it");
    std::printf("  (calling 127.0.0.1:%u, where nobody answers)\n", deadPort);

    auto refused = connectTcp("127.0.0.1", deadPort, err);
    if (refused) {
        // Returned "connecting" -- so the refusal must arrive through poll()/select().
        bool closed = waitFor([&] {
            refused->poll();
            return refused->closed();
        });
        CHECK(closed, "refused connect -> poll() reads the except set -> closed()");
        CHECK(!refused->established(), "...and it NEVER reports established() on a refusal");
    } else {
        // Some stacks refuse a loopback dead port synchronously; that is equally correct.
        CHECK(true, "refused connect failed synchronously (also a correct answer)");
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
