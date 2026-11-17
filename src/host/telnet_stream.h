#pragma once
//
// `telnet:PORT` / `telnet:HOST:PORT` -- a socket line that speaks the Telnet
// protocol (RFC 854), so a stock `telnet` client behaves like a real terminal
// server instead of a raw pipe. DESIGN.md 7.1, 7.7.
//
// WHY THIS EXISTS, AND WHY IT IS NOT `socket:`. A `socket:` endpoint is a RAW
// byte pipe -- that is exactly right for machine-to-machine `CONNECT` and for the
// live mirror, and it must stay that way. But when a human points `telnet` at a
// raw socket, nothing negotiates the terminal, so the client stays in its default
// LINE mode with LOCAL echo on: every keystroke is echoed by the client AND again
// by the guest (a BBS echoes what it receives), and Enter arrives as a whole line,
// CR mangled to LF. That double-echo is not a modem bug -- it is a missing telnet
// handshake, and this decorator supplies it.
//
// A TelnetStream WRAPS a TcpStream (the same decorator shape as TeeStream and
// MirrorStream) and runs a TelnetCodec (host/telnet_codec.h) over the bytes that
// cross it: on each new session it offers the terminal-server options, strips
// inbound IAC, doubles a data 0xFF, and folds the client's CR LF / CR NUL to a bare
// CR. The codec is shared with the PMMI modem's answer/dial line, so a person
// telnetting in behaves whether the far end is a `telnet:` port or a `dial=`/`answer=`
// modem.
//
// Everything else -- carrier, CTS backpressure, DTR-hangup, the line rate, SHOW --
// is the wrapped TcpStream's, forwarded verbatim. describe() echoes `telnet:...`,
// so SHOW and CONFIG SAVE round-trip it.

#include "host/stream.h"
#include "host/tcp.h"
#include "host/telnet_codec.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class TelnetStream : public ByteStream {
public:
    // `inner` is the raw TCP line (a TcpListenStream or TcpConnectStream); `spec` is
    // the operator's `telnet:...` text, echoed by describe(). `server` is true for a
    // LISTEN (we host a BBS, so WE offer to echo) and false for a dial-out (we are
    // the client, so we ask the far end to echo).
    TelnetStream(std::unique_ptr<TcpStream> inner, std::string spec, bool server);

    std::string describe() const override { return spec_; }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    // Readability is about DECODED data, not raw bytes: the far end may have sent
    // nothing but a negotiation, which the codec eats whole in pump().
    bool readable() const override { return !rxClean_.empty(); }
    bool writable() const override { return inner_->writable(); }
    bool pacesItself() const override { return inner_->pacesItself(); }
    bool pacedReceive() const override { return inner_->pacedReceive(); }
    bool realWire() const override { return inner_->realWire(); }
    // Decoded bytes the guest has not read, plus raw ones the codec has not reached yet.
    size_t rxPending() const override { return rxClean_.size() + inner_->rxPending(); }
    size_t txPending() const override { return inner_->txPending(); }

    void flush() override { inner_->flush(); }
    void pump() override;

    // The far end's pins, the card's pins, the line rate and the operator log are
    // all the wrapped socket's -- telnet negotiation rides in the data stream and
    // changes none of them.
    LineStatus status() const override { return inner_->status(); }
    void       setControl(const LineControl& c) override { inner_->setControl(c); }
    bool       setParams(const LineParams& p, std::string& err) override {
        return inner_->setParams(p, err);
    }
    std::vector<std::string> drainLog() override { return inner_->drainLog(); }

    // The socket owes the banner; we SAY it, telnet-encoded, behind the negotiation
    // pump() already sent on this session's carrier edge.
    void greet(const std::string& owner) override;

private:
    std::unique_ptr<TcpStream>  inner_;
    std::string                 spec_;
    bool                        server_;
    TelnetCodec                 codec_;

    std::string rxClean_;  // decoded data waiting for the guest
    bool        wasUp_ = false;  // carrier edge, to spot a new session and renegotiate
};

} // namespace altair
