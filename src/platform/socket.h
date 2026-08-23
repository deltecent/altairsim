#pragma once
//
// TCP, for `socket:` endpoints (DESIGN.md 2.1, 7.1).
//
// The same contract as serial.h: pure declarations, no OS type in any signature,
// one implementation file per OS. A board never learns what TCP is -- it has a
// serial line, and a telnet client on the far end of it is a MODEM ANSWERING.
//
// NON-BLOCKING THROUGHOUT, including connect(). A simulator that stalled for a
// three-second TCP handshake would have stalled emulated time to do it.

#include <cstdint>
#include <memory>
#include <string>

namespace altair::platform {

// One end of an established (or establishing) TCP session.
class TcpConn {
public:
    virtual ~TcpConn() = default;

    // Has the handshake completed? False on a connect() still in flight. A
    // CONNECTING session is not an error and not a failure -- it is a phone that
    // is still ringing, and the card correctly sees no carrier yet.
    virtual bool established() const = 0;

    // The session is GONE -- the far end hung up, or the connect failed. Distinct
    // from "quiet": this one never comes back, and it is what drops carrier.
    virtual bool closed() const = 0;

    virtual size_t read(uint8_t* buf, size_t n) = 0;

    // Returns what it TOOK, which may be less than n when the send buffer is full.
    // The caller must keep the rest -- see host/tcp.cpp, where that backpressure is
    // exactly what negates CTS, and therefore what makes the guest WAIT rather than
    // lose a byte.
    virtual size_t write(const uint8_t* buf, size_t n) = 0;

    // Let the OS finish the handshake / drain the send buffer. Called from pump().
    virtual void poll() = 0;

    virtual void close() = 0;

    virtual const std::string& peer() const = 0;
};

// A listening socket. One client at a time -- which is what a modem is, and what a
// serial port is. A second caller gets a busy signal, not a party line.
class TcpListener {
public:
    virtual ~TcpListener() = default;

    // Non-blocking. Null when nobody is calling.
    virtual std::unique_ptr<TcpConn> accept() = 0;

    virtual uint16_t port() const = 0;
};

std::unique_ptr<TcpListener> listenTcp(uint16_t port, std::string& err);
std::unique_ptr<TcpConn>     connectTcp(const std::string& host, uint16_t port, std::string& err);

// ---------------------------------------------------------------------------
// UDP -- for `tnfs://` media (DESIGN.md 7.7). A DATAGRAM socket, and a BLOCKING one,
// which is the whole reason it is not a TcpConn.
//
// TcpConn is non-blocking on purpose: a serial line is pumped every emulated cycle
// and must never stall emulated time. A TNFS medium is the opposite case -- it does
// ALL of its network I/O at MOUNT and at sync(), never inside the emulation loop, and
// each step is a strict request/reply that has no meaning until the reply is in hand.
// A blocking send() + a recv() with a timeout is exactly that shape, and expressing it
// with the non-blocking primitive would be a poll-spin reinventing the timeout the
// kernel already has. So this is a separate, deliberately simpler thing.
//
// The peer is fixed at connect() (a connect()ed datagram socket), so send()/recv()
// need no address -- and the kernel will drop datagrams from anywhere else, which is
// the source check a session bound to one server wants anyway.
class UdpSocket {
public:
    virtual ~UdpSocket() = default;

    // Send one datagram. False (and `err` set) only on a hard socket error; a datagram
    // that never arrives is not an error here -- that is what the recv timeout is for.
    virtual bool send(const uint8_t* buf, size_t n, std::string& err) = 0;

    // Wait up to timeoutMs for one datagram. Returns bytes read (>0), 0 on TIMEOUT
    // (the caller retransmits -- see host/tnfs.cpp), or -1 on a hard error (`err` set).
    // A datagram is delivered whole: a return of n truncated to the buffer is a bug in
    // the caller's buffer size, not a short read to loop on.
    virtual ptrdiff_t recv(uint8_t* buf, size_t n, int timeoutMs, std::string& err) = 0;

    virtual const std::string& peer() const = 0;
};

// Resolve host:port and fix it as the datagram peer. Null and `err` set if the name
// will not resolve or the socket cannot be made; note that with UDP there is no
// handshake, so a "connected" socket to a dead server still succeeds here and only
// reveals the silence as a recv timeout.
std::unique_ptr<UdpSocket> connectUdp(const std::string& host, uint16_t port, std::string& err);

} // namespace altair::platform
