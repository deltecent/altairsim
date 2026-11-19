#include "host/pty_stream.h"

namespace altair {

size_t PtyStream::read(uint8_t* buf, size_t n) {
    size_t k = rx_.size() < n ? rx_.size() : n;
    for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)rx_[i];
    rx_.erase(0, k);
    return k;
}

// ALWAYS TAKES ALL OF IT, as host/tcp.cpp does: what the kernel would not accept is
// still ours to send, and the depth of that queue is what negates writable().
//
// WITH NOBODY ON THE LINE, THE BYTES GO NOWHERE. Keeping them for a program that has
// not opened the link yet would mean it opens to output the guest printed an hour ago.
size_t PtyStream::doWrite(const uint8_t* buf, size_t n) {
    if (!pty_->attached()) return n;
    tx_.append((const char*)buf, n);
    flush();
    return n;
}

void PtyStream::flush() {
    if (!pty_->attached()) return;
    while (!tx_.empty()) {
        size_t w = pty_->write((const uint8_t*)tx_.data(), tx_.size());
        if (w == 0) break;  // kernel buffer full -- this is the backpressure. Try in pump().
        tx_.erase(0, w);
    }
}

void PtyStream::pump() {
    pty_->poll();

    if (pty_->attached()) {
        wasAttached_ = true;
        uint8_t buf[512];
        for (;;) {
            size_t r = pty_->read(buf, sizeof buf);
            if (r == 0) break;
            rx_.append((const char*)buf, r);
        }
        flush();
    }

    // The program closed the link. Bytes already RECEIVED stay in rx_ -- they arrived
    // before it left and the guest is entitled to them. Nothing more goes OUT, so the
    // next program starts at the live tail and not at this one's backlog.
    if (wasAttached_ && !pty_->attached()) {
        wasAttached_ = false;
        tx_.clear();
    }
}

LineStatus PtyStream::status() const {
    LineStatus s;
    const bool up = pty_->attached();
    s.carrier = up;
    s.dsr     = up;
    s.cts     = writable();  // true with nobody there: see the header
    s.ring    = false;
    return s;
}

std::vector<std::string> PtyStream::drainLog() {
    std::vector<std::string> v;
    if (std::string n = takeNote(); !n.empty()) v.push_back("pty: " + n);
    return v;
}

} // namespace altair
