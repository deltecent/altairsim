#include "host/tape.h"

namespace altair {

bool TapeImage::read(uint8_t& b) {
    if (atEnd()) return false;
    if (!media_->readAt(pos_, &b, 1)) return false;
    ++pos_;
    return true;
}

bool TapeImage::write(uint8_t b) {
    if (readOnly()) return false;
    if (!media_->writeAt(pos_, &b, 1)) return false;
    ++pos_;
    return true;
}

size_t TapeStream::read(uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n && tape_.read(buf[got])) ++got;
    return got;  // 0 at the end of the tape: a quiet line, not an error
}

size_t TapeStream::write(const uint8_t* buf, size_t n) {
    size_t put = 0;
    while (put < n && tape_.write(buf[put])) ++put;
    return put;
}

} // namespace altair
