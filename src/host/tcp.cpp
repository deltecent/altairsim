#include "host/tcp.h"

namespace altair {

size_t TcpStream::read(uint8_t* buf, size_t n) {
    size_t k = rx_.size() < n ? rx_.size() : n;
    for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)rx_[i];
    rx_.erase(0, k);
    return k;
}

// ALWAYS TAKES ALL OF IT. What the kernel would not accept is still ours to send,
// and it goes out in flush()/pump(); the depth of that queue is what negates
// writable(), which is what holds TDRE clear. A short write returned to the card
// would be a byte the card has no way to keep.
//
// WITH NO CLIENT ON THE LINE, THE BYTES GO NOWHERE, and that is correct: a 6850
// with no modem attached transmits into the air quite happily. Buffering them for a
// client who has not called yet would mean the first telnet session opens with a
// faceful of output the guest printed an hour ago.
size_t TcpStream::write(const uint8_t* buf, size_t n) {
    if (!conn_ || conn_->closed()) return n;
    tx_.append((const char*)buf, n);
    flush();
    return n;
}

void TcpStream::flush() {
    if (!conn_ || !conn_->established()) return;
    while (!tx_.empty()) {
        size_t w = conn_->write((const uint8_t*)tx_.data(), tx_.size());
        if (w == 0) break;  // send buffer full -- this is the backpressure. Try in pump().
        tx_.erase(0, w);
    }
}

void TcpStream::pump() {
    refresh();  // answer the phone / finish dialling

    if (!conn_) return;
    conn_->poll();

    if (conn_->established()) {
        uint8_t buf[512];
        for (;;) {
            size_t r = conn_->read(buf, sizeof buf);
            if (r == 0) break;
            rx_.append((const char*)buf, r);
        }
        flush();
    }

    // The far end hung up. Drop the session -- and with it, carrier. The bytes
    // already RECEIVED stay in rx_: they arrived before the hangup, the guest is
    // entitled to them, and a modem that ate the last line of a message because the
    // caller put the phone down would be losing data the transport did not lose.
    if (conn_->closed()) {
        conn_.reset();
        tx_.clear();  // ...but nothing more is going OUT down a dead line
    }
}

// CARRIER IS THE SESSION. There is nothing else it could honestly be.
//
// CTS is our own send buffer: full means the far end (or the network) is not
// keeping up, which is precisely what a modem negating CTS is telling you.
LineStatus TcpStream::status() const {
    LineStatus s;
    bool up = conn_ && conn_->established();
    s.carrier = up;
    s.dsr     = up;
    s.cts     = up && writable();

    // RI IS ALWAYS FALSE, and this is a LIMITATION, not a decision (see
    // docs/boards/mits-2sio.md). Ringing would mean NOT answering until the card
    // raises DTR -- and the 6850 has no DTR pin, so nothing in the machine today
    // could ever answer, and every socket would sit there ringing forever. We
    // auto-answer. When the PMMI lands -- which HAS a DTR pin and counts ring bursts
    // -- it gets a real ring, and that is the card that will make it mean something.
    s.ring = false;
    return s;
}

void TcpStream::setControl(const LineControl& c) {
    if (c.dtr) sawDtr_ = true;

    // THE GUEST HUNG UP THE PHONE. On a real modem, dropping DTR ends the call --
    // and this is the mapping that makes a PMMI's ATH work over a socket without the
    // board knowing what a socket is.
    if (sawDtr_ && !c.dtr && conn_) {
        conn_->close();
        conn_.reset();
    }

    // RTS goes nowhere on a socket: there is no wire to raise. TCP does its own flow
    // control, and it is the thing writable() is already reporting.
}

// ---------------------------------------------------------------------------
// Answering the phone. The listener stays up for the NEXT call, always.
// ---------------------------------------------------------------------------
void TcpListenStream::refresh() {
    if (conn_) return;  // one client at a time. A second caller gets a busy signal.
    if (auto c = listener_->accept()) conn_ = std::move(c);
}

// Dialling out. If the call failed, it stays failed -- we do not redial in a loop,
// because a socket: endpoint that silently retried forever would look exactly like a
// working one that nobody had connected to yet. DISCONNECT and CONNECT again.
void TcpConnectStream::refresh() {}

} // namespace altair
