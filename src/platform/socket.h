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

} // namespace altair::platform
