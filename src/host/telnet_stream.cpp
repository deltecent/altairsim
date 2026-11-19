#include "host/telnet_stream.h"

namespace altair {

TelnetStream::TelnetStream(std::unique_ptr<TcpStream> inner, std::string spec, bool server)
    : inner_(std::move(inner)), spec_(std::move(spec)), server_(server) {}

size_t TelnetStream::read(uint8_t* buf, size_t n) {
    size_t k = rxClean_.size() < n ? rxClean_.size() : n;
    for (size_t i = 0; i < k; ++i) buf[i] = (uint8_t)rxClean_[i];
    rxClean_.erase(0, k);
    return k;
}

// The guest's bytes, telnet-encoded (a data 0xFF doubled), then written straight to
// the wrapped socket. We always take all of them -- the socket's tx buffer is what
// applies backpressure, exactly as under a bare socket:.
size_t TelnetStream::doWrite(const uint8_t* buf, size_t n) {
    codec_.send(buf, n);
    std::string wire = codec_.takeOut();
    if (!wire.empty()) inner_->write((const uint8_t*)wire.data(), wire.size());
    return n;
}

void TelnetStream::pump() {
    inner_->pump();  // service the socket first: accept/dial, drain into its rx, flush tx

    // A carrier rising edge is a NEW client on the listener (or a completed dial):
    // renegotiate from scratch, because the last session's option state is gone.
    const bool up = inner_->status().carrier;
    if (up && !wasUp_) codec_.reset(server_);
    wasUp_ = up;

    // Pull everything the far end sent and run it through the codec, which appends
    // decoded data (takeData) and any negotiation replies (takeOut).
    uint8_t buf[512];
    for (;;) {
        size_t r = inner_->read(buf, sizeof buf);
        if (r == 0) break;
        codec_.recv(buf, r);
    }
    rxClean_ += codec_.takeData();

    // Ship the telnet commands the codec generated (raw wire bytes, IAC and all).
    std::string wire = codec_.takeOut();
    if (!wire.empty()) inner_->write((const uint8_t*)wire.data(), wire.size());
}

void TelnetStream::greet(const std::string& owner) {
    std::string line = inner_->takeGreeting(owner);
    if (!line.empty()) write((const uint8_t*)line.data(), line.size());
}

} // namespace altair
