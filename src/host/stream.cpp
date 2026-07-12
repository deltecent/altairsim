#include "host/stream.h"

#include <algorithm>
#include <cstring>

namespace altair {

size_t LoopbackStream::read(uint8_t* buf, size_t n) {
    size_t got = std::min(n, q_.size());
    std::memcpy(buf, q_.data(), got);
    q_.erase(0, got);
    return got;
}

size_t LoopbackStream::write(const uint8_t* buf, size_t n) {
    q_.append((const char*)buf, n);
    return n;
}

size_t ScriptedStream::read(uint8_t* buf, size_t n) {
    size_t got = std::min(n, in_.size() - pos_);
    std::memcpy(buf, in_.data() + pos_, got);
    pos_ += got;
    return got;
}

size_t ScriptedStream::write(const uint8_t* buf, size_t n) {
    out_.append((const char*)buf, n);
    return n;
}

} // namespace altair
