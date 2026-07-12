// Win32 TCP -- Winsock. See src/platform/socket.h.
//
// UNBUILT AND UNTESTED, exactly as serial_win32.cpp is, and for the same reason:
// written on macOS, where no compiler has seen it. See docs/porting-notes.md.

#include "platform/socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace altair::platform {
namespace {

// WINSOCK HAS TO BE STARTED, and exactly once. A static does that at first use and
// never again, with no init hook to remember to call from main() -- and no way for a
// unit test that reaches the socket layer directly to forget it.
void startWinsock() {
    static const bool once = [] {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        return true;
    }();
    (void)once;
}

std::string lastError(const char* what) {
    int   e   = WSAGetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, (DWORD)e, 0, (LPSTR)&msg, 0, nullptr);
    std::string s = std::string(what) + ": " + (msg ? msg : "winsock error");
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

void makeNonBlocking(SOCKET s) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
}

bool wouldBlock() {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINTR;
}

class Win32Conn : public TcpConn {
public:
    Win32Conn(SOCKET s, std::string peer, bool connecting)
        : s_(s), peer_(std::move(peer)), connecting_(connecting) {
        makeNonBlocking(s_);
        BOOL one = TRUE;
        setsockopt(s_, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    }

    ~Win32Conn() override { close(); }

    bool established() const override { return s_ != INVALID_SOCKET && !connecting_; }
    bool closed() const override { return s_ == INVALID_SOCKET; }

    size_t read(uint8_t* buf, size_t n) override {
        if (!established()) return 0;
        int r = ::recv(s_, (char*)buf, (int)n, 0);
        if (r > 0) return (size_t)r;
        // Zero is the far end HANGING UP -- which is what drops carrier. Not "quiet".
        if (r == 0) {
            close();
            return 0;
        }
        if (!wouldBlock()) close();
        return 0;
    }

    size_t write(const uint8_t* buf, size_t n) override {
        if (!established()) return 0;
        int w = ::send(s_, (const char*)buf, (int)n, 0);
        if (w > 0) return (size_t)w;
        if (w < 0 && !wouldBlock()) close();
        return 0;  // send buffer full: the caller keeps the rest. This is backpressure.
    }

    void poll() override {
        if (s_ == INVALID_SOCKET || !connecting_) return;

        // A non-blocking connect reports success by becoming WRITABLE and failure by
        // landing in the EXCEPT set -- and Winsock, unlike POSIX, genuinely uses that
        // third set. Watch both or a refused connection looks like a live one.
        fd_set wr, ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(s_, &wr);
        FD_SET(s_, &ex);
        timeval tv{0, 0};
        if (::select(0, nullptr, &wr, &ex, &tv) <= 0) return;

        if (FD_ISSET(s_, &ex)) {
            close();
            return;
        }
        if (FD_ISSET(s_, &wr)) connecting_ = false;
    }

    void close() override {
        if (s_ != INVALID_SOCKET) closesocket(s_);
        s_ = INVALID_SOCKET;
    }

    const std::string& peer() const override { return peer_; }

private:
    SOCKET      s_ = INVALID_SOCKET;
    std::string peer_;
    bool        connecting_ = false;
};

class Win32Listener : public TcpListener {
public:
    Win32Listener(SOCKET s, uint16_t port) : s_(s), port_(port) { makeNonBlocking(s_); }
    ~Win32Listener() override {
        if (s_ != INVALID_SOCKET) closesocket(s_);
    }

    std::unique_ptr<TcpConn> accept() override {
        sockaddr_in a{};
        int         len = sizeof a;
        SOCKET      c   = ::accept(s_, (sockaddr*)&a, &len);
        if (c == INVALID_SOCKET) return nullptr;  // nobody calling

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        std::string peer = std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
        return std::make_unique<Win32Conn>(c, peer, /*connecting=*/false);
    }

    uint16_t port() const override { return port_; }

private:
    SOCKET   s_    = INVALID_SOCKET;
    uint16_t port_ = 0;
};

} // namespace

std::unique_ptr<TcpListener> listenTcp(uint16_t port, std::string& err) {
    startWinsock();

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        err = lastError("socket");
        return nullptr;
    }

    // NOT SO_REUSEADDR ON WINDOWS. It means something different there -- it lets a
    // second process STEAL a port that is still in use, which is a security bug, not
    // a convenience. Windows already allows the rebind-after-close that POSIX needs
    // SO_REUSEADDR for, so the right amount of code here is none.

    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(port);

    if (::bind(s, (sockaddr*)&a, sizeof a) != 0) {
        err = "cannot listen on port " + std::to_string(port) + ": " + lastError("bind");
        closesocket(s);
        return nullptr;
    }
    if (::listen(s, 1) != 0) {
        err = lastError("listen");
        closesocket(s);
        return nullptr;
    }

    int len = sizeof a;
    if (getsockname(s, (sockaddr*)&a, &len) == 0) port = ntohs(a.sin_port);

    return std::make_unique<Win32Listener>(s, port);
}

std::unique_ptr<TcpConn> connectTcp(const std::string& host, uint16_t port, std::string& err) {
    startWinsock();

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        err = "cannot resolve '" + host + "'";
        return nullptr;
    }

    SOCKET s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        err = lastError("socket");
        freeaddrinfo(res);
        return nullptr;
    }
    makeNonBlocking(s);

    // WSAEWOULDBLOCK is the NORMAL answer to a non-blocking connect and is not a
    // failure: the handshake is under way, and the card correctly sees no carrier
    // until poll() finishes it. The phone is still ringing.
    bool connecting = false;
    if (::connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        if (wouldBlock()) {
            connecting = true;
        } else {
            err = "cannot connect to " + host + ":" + std::to_string(port) + ": " +
                  lastError("connect");
            closesocket(s);
            freeaddrinfo(res);
            return nullptr;
        }
    }
    freeaddrinfo(res);

    return std::make_unique<Win32Conn>(s, host + ":" + std::to_string(port), connecting);
}

} // namespace altair::platform
