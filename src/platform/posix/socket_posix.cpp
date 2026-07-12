// POSIX TCP -- BSD sockets. See src/platform/socket.h.

#include "platform/socket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace altair::platform {
namespace {

// WRITING TO A SOCKET THE FAR END HAS CLOSED RAISES SIGPIPE, WHICH BY DEFAULT KILLS
// THE PROCESS. A telnet client closing its window would take the simulator down and
// the guest's unsaved work with it, and it would look like a crash, not a hangup.
//
// The obvious fixes are MSG_NOSIGNAL (Linux) and SO_NOSIGPIPE (macOS) -- and they
// are a genuine macOS/Linux divergence, which DESIGN.md 2.1 says gets its own FILE,
// never an #ifdef in a shared one. Ignoring the signal is plain POSIX, works on
// both, and needs no branch at all: send() then simply returns EPIPE and we close
// the connection like any other error. The right answer to a platform conditional
// is usually to stop needing it.
void ignoreSigPipe() {
    static const bool once = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
}

void makeNonBlocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

class PosixConn : public TcpConn {
public:
    PosixConn(int fd, std::string peer, bool connecting)
        : fd_(fd), peer_(std::move(peer)), connecting_(connecting) {
        makeNonBlocking(fd_);

        // A terminal is interactive: a one-character write must GO, not wait for
        // Nagle to collect a packet's worth. Without this, an echoed keystroke can
        // sit in the kernel for 40 ms and the line feels broken.
        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }

    ~PosixConn() override { close(); }

    bool established() const override { return fd_ >= 0 && !connecting_; }
    bool closed() const override { return fd_ < 0; }

    size_t read(uint8_t* buf, size_t n) override {
        if (!established()) return 0;
        ssize_t r = ::recv(fd_, buf, n, 0);
        if (r > 0) return (size_t)r;
        // ZERO IS NOT "QUIET" ON A SOCKET -- it is the far end hanging up, and that
        // is precisely the event that drops carrier. A read that returned 0 as "no
        // data" would leave a card holding DCD for a client that left an hour ago.
        if (r == 0) {
            close();
            return 0;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) close();
        return 0;
    }

    size_t write(const uint8_t* buf, size_t n) override {
        if (!established()) return 0;
        ssize_t w = ::send(fd_, buf, n, 0);
        if (w > 0) return (size_t)w;
        if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) close();
        return 0;  // send buffer full: the caller keeps the rest. THIS is backpressure.
    }

    void poll() override {
        if (fd_ < 0 || !connecting_) return;

        // Is the handshake done? A non-blocking connect reports completion by
        // becoming writable, and reports FAILURE the same way -- so the error has to
        // be fetched explicitly, or a refused connection looks like a connected one.
        int       err = 0;
        socklen_t len = sizeof err;
        pollfd    p{fd_, POLLOUT, 0};
        if (::poll(&p, 1, 0) <= 0) return;
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            close();
            return;
        }
        connecting_ = false;
    }

    void close() override {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

    const std::string& peer() const override { return peer_; }

private:
    int         fd_ = -1;
    std::string peer_;
    bool        connecting_ = false;
};

class PosixListener : public TcpListener {
public:
    PosixListener(int fd, uint16_t port) : fd_(fd), port_(port) { makeNonBlocking(fd_); }
    ~PosixListener() override {
        if (fd_ >= 0) ::close(fd_);
    }

    std::unique_ptr<TcpConn> accept() override {
        sockaddr_in a{};
        socklen_t   len = sizeof a;
        int         c   = ::accept(fd_, (sockaddr*)&a, &len);
        if (c < 0) return nullptr;  // nobody calling

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        std::string peer = std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
        return std::make_unique<PosixConn>(c, peer, /*connecting=*/false);
    }

    uint16_t port() const override { return port_; }

private:
    int      fd_ = -1;
    uint16_t port_ = 0;
};

} // namespace

std::unique_ptr<TcpListener> listenTcp(uint16_t port, std::string& err) {
    ignoreSigPipe();

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::string("socket: ") + std::strerror(errno);
        return nullptr;
    }

    // Without SO_REUSEADDR, restarting the simulator inside the TIME_WAIT window --
    // which is to say, restarting it -- fails to bind with "address already in use".
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(port);

    if (::bind(fd, (sockaddr*)&a, sizeof a) != 0) {
        err = "cannot listen on port " + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    if (::listen(fd, 1) != 0) {
        err = std::string("listen: ") + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }

    // Port 0 means "the OS picks one" -- and then we have to ASK it which, or the
    // test that used it could never find out where to call.
    socklen_t len = sizeof a;
    if (getsockname(fd, (sockaddr*)&a, &len) == 0) port = ntohs(a.sin_port);

    return std::make_unique<PosixListener>(fd, port);
}

std::unique_ptr<TcpConn> connectTcp(const std::string& host, uint16_t port, std::string& err) {
    ignoreSigPipe();

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0 || !res) {
        err = "cannot resolve '" + host + "': " + gai_strerror(rc);
        return nullptr;
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        err = std::string("socket: ") + std::strerror(errno);
        freeaddrinfo(res);
        return nullptr;
    }
    makeNonBlocking(fd);

    // EINPROGRESS is the NORMAL answer here and is not a failure: the handshake is
    // under way and poll() will finish it. The card sees no carrier until it does,
    // which is exactly right -- the phone is still ringing.
    bool connecting = false;
    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            connecting = true;
        } else {
            err = "cannot connect to " + host + ":" + std::to_string(port) + ": " +
                  std::strerror(errno);
            ::close(fd);
            freeaddrinfo(res);
            return nullptr;
        }
    }
    freeaddrinfo(res);

    return std::make_unique<PosixConn>(fd, host + ":" + std::to_string(port), connecting);
}

} // namespace altair::platform
