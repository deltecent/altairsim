#pragma once
//
// `socket:2323` and `socket:host:port` -- a TCP session, as a ByteStream.
//
// ---------------------------------------------------------------------------
// A CLIENT CONNECTING *IS* CARRIER APPEARING. This is the load-bearing mapping of
// the whole endpoint layer, and everything else falls out of it:
//
//   a telnet client connects    -> DCD rises
//   it closes the window        -> DCD falls, and the 6850 LATCHES that and
//                                  interrupts, exactly as it would for a modem
//   the guest drops DTR         -> we hang up on the client
//   the TCP send buffer fills   -> CTS falls, so TDRE stays clear, so the GUEST
//                                  waits rather than losing a byte
//
// That is what every terminal server ever built did, and it is what lets a PMMI
// modem card work over a socket without the board learning what TCP is. The board
// has a serial line with a modem on it. From where the card sits, that is not a
// lie -- it is *exactly* what it has.
// ---------------------------------------------------------------------------

#include "host/stream.h"
#include "platform/socket.h"

#include <memory>
#include <string>

namespace altair {

// The session, and everything that is true of one whichever end started it. The
// only thing dialling out and answering the phone genuinely differ about is how the
// session BEGINS -- so that, and only that, is what the two subclasses override.
class TcpStream : public ByteStream {
public:
    explicit TcpStream(std::string spec) : spec_(std::move(spec)) {}

    std::string describe() const override { return spec_; }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override { return !rx_.empty(); }

    // BACKPRESSURE IS CTS, AND CTS IS TDRE. A full send buffer makes the guest WAIT
    // -- it does not make us drop a byte (DESIGN.md 7.1: never manufacture data loss
    // the transport does not have).
    bool writable() const override { return tx_.size() < kTxCap; }

    void flush() override;
    void pump() override;

    LineStatus status() const override;
    void       setControl(const LineControl& c) override;

protected:
    // Give the session a turn: answer the phone, or finish dialling. Called from
    // pump(), before any bytes move.
    virtual void refresh() = 0;

    std::unique_ptr<platform::TcpConn> conn_;

private:
    static constexpr size_t kTxCap = 8192;

    std::string spec_;
    std::string rx_, tx_;

    // HANGING UP ON DTR ONLY COUNTS IF THE CARD EVER PICKED THE PHONE UP.
    //
    // DTR is de-asserted at power-on and stays that way on a card that does not drive
    // it -- and the 6850 HAS NO DTR PIN (data sheet: it has RTS, CTS and DCD, and
    // that is all). So "DTR dropped, close the client", applied naively, would hang
    // up on every telnet session the instant it connected, on the commonest card in
    // the machine. A card that never raised DTR is not a card that hung up.
    bool sawDtr_ = false;
};

// `socket:2323` -- we LISTEN. One client at a time, which is what a modem is. The
// listener stays up across a disconnect, so the next telnet is the phone ringing
// again rather than a simulator you have to restart.
class TcpListenStream : public TcpStream {
public:
    TcpListenStream(std::unique_ptr<platform::TcpListener> l, std::string spec)
        : TcpStream(std::move(spec)), listener_(std::move(l)) {}

protected:
    void refresh() override;

private:
    std::unique_ptr<platform::TcpListener> listener_;
};

// `socket:host:port` -- we CALL OUT. The handshake is non-blocking, so a session
// still being established is a phone still ringing: the card correctly sees no
// carrier until the far end answers.
class TcpConnectStream : public TcpStream {
public:
    TcpConnectStream(std::unique_ptr<platform::TcpConn> c, std::string spec)
        : TcpStream(std::move(spec)) {
        conn_ = std::move(c);
    }

protected:
    void refresh() override;
};

} // namespace altair
