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

// THE MODE IS CHECKED HERE AND NOT ONLY IN readable()/writable(), because a caller
// that asks anyway must not be able to move the head. That is the whole guarantee:
// in PLAY nothing can advance the tape except playing it.
size_t TapeStream::read(uint8_t* buf, size_t n) {
    if (mode_ != Mode::Play) return 0;  // a recording deck plays nothing back
    size_t got = 0;
    while (got < n && tape_.read(buf[got])) ++got;
    return got;  // 0 at the end of the tape: a quiet line, not an error
}

size_t TapeStream::write(const uint8_t* buf, size_t n) {
    if (mode_ != Mode::Record) return 0;  // RECORD was never pressed
    size_t put = 0;
    while (put < n && tape_.write(buf[put])) ++put;
    return put;
}

} // namespace altair
