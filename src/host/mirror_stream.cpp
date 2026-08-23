#include "host/mirror_stream.h"

#include <utility>

namespace altair {

MirrorStream::MirrorStream(std::unique_ptr<ByteStream>            inner,
                           std::string                            sinkSpec,
                           std::unique_ptr<platform::TcpListener> listener,
                           bool                                   readOnly)
    : inner_(std::move(inner)),
      sinkSpec_(std::move(sinkSpec)),
      listener_(std::move(listener)),
      readOnly_(readOnly) {}

// INJECTED BYTES FIRST. A human taking over is driving the line; their keystrokes lead
// the inner's own input (under --mcp, the AI's scripted feed). One source per call is
// enough -- the UART re-polls constantly, so the other is serviced on the next read.
size_t MirrorStream::read(uint8_t* buf, size_t n) {
    if (!injectBuf_.empty()) {
        size_t k = injectBuf_.size() < n ? injectBuf_.size() : n;
        for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)injectBuf_[i];
        injectBuf_.erase(0, k);
        return k;
    }
    return inner_->read(buf, n);
}

// Forward to the inner line, then COPY what it accepted to the watcher. The guest's
// return value is the inner's -- the watcher is never allowed to shorten a write, or a
// slow telnet would look to the guest like a full UART and cost it a byte.
size_t MirrorStream::write(const uint8_t* buf, size_t n) {
    size_t w = inner_->write(buf, n);
    queueToClient(buf, w);
    sendToClient();
    return w;
}

// Bounded, drop-oldest, and a no-op with no watcher on the line. See the header: a
// watcher who connects later must not receive output the guest emitted before they
// arrived, and a watcher who falls behind loses scrollback, not the guest a byte.
void MirrorStream::queueToClient(const uint8_t* buf, size_t n) {
    if (!conn_ || conn_->closed() || n == 0) return;
    txQueue_.append((const char*)buf, n);
    if (txQueue_.size() > kTxCap) txQueue_.erase(0, txQueue_.size() - kTxCap);
}

void MirrorStream::sendToClient() {
    if (!conn_ || !conn_->established()) return;
    while (!txQueue_.empty()) {
        size_t w = conn_->write((const uint8_t*)txQueue_.data(), txQueue_.size());
        if (w == 0) break;  // send buffer full -- try again in pump(). No guest stall.
        txQueue_.erase(0, w);
    }
}

void MirrorStream::flush() {
    inner_->flush();
    sendToClient();
}

void MirrorStream::pump() {
    inner_->pump();  // let the wrapped line do its own host I/O first

    // Answer a waiting watcher. One at a time -- a serial line is one wire; a second
    // caller waits on the listener (host/tcp.cpp's rule). accept() is non-blocking and
    // null when nobody is calling.
    if (!conn_) {
        if (auto c = listener_->accept()) conn_ = std::move(c);
    }
    if (!conn_) return;

    conn_->poll();

    if (conn_->established()) {
        // Drain the watcher. Read-only throws the bytes away rather than leaving them to
        // pile up in the kernel buffer -- a spectator's stray keystroke is not an error,
        // it just goes nowhere. Otherwise it becomes take-over input, bounded like tx.
        uint8_t buf[512];
        for (;;) {
            size_t r = conn_->read(buf, sizeof buf);
            if (r == 0) break;
            if (!readOnly_) {
                injectBuf_.append((const char*)buf, r);
                if (injectBuf_.size() > kInjectCap)
                    injectBuf_.erase(0, injectBuf_.size() - kInjectCap);
            }
        }
        sendToClient();
    }

    // The watcher hung up. Drop the session; the listener stays up for the next one.
    // Bytes already INJECTED stay in injectBuf_ -- they arrived before the hangup and
    // the guest is entitled to them. Nothing more goes OUT a dead line, so txQueue_ is
    // cleared (a fresh watcher starts at the live tail, not this one's backlog).
    if (conn_->closed()) {
        conn_.reset();
        txQueue_.clear();
    }
}

} // namespace altair
