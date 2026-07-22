#include "host/tape.h"

#include <chrono>

namespace altair {

// A MONOTONIC HOST CLOCK IN NANOSECONDS, the default source for a paced tape. steady_clock
// is the right one: it never runs backwards and no NTP step can make a 300-baud byte
// arrive early. It is read ONLY in wall-clock mode, so a full-speed tape -- and every
// acceptance test, which runs full speed -- calls it not once and stays deterministic.
static uint64_t steadyNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

TapeStream::TapeStream(TapeImage& t, Mode m, uint64_t nsPerByte, std::function<uint64_t()> hostNs)
    : tape_(t), mode_(m), nsPerByte_(nsPerByte), hostNs_(hostNs ? std::move(hostNs) : steadyNs) {}

bool TapeStream::readable() const {
    if (mode_ != Mode::Play || tape_.atEnd()) return false;
    if (nsPerByte_ == 0) return true;                 // full speed: always ready
    if (!started_) return true;                       // the first byte does not wait
    return hostNs_() >= nextReadyNs_;                 // the rest arrive at the baud
}

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

    // A byte just left: set when the next one may. From the LATER of now and the byte's
    // own turn, so a loader that dawdles between reads does not let the tape bunch up
    // and then spill a burst -- each byte still waits its full interval from the last.
    if (got && nsPerByte_) {
        uint64_t now  = hostNs_();
        uint64_t base = (started_ && nextReadyNs_ > now) ? nextReadyNs_ : now;
        nextReadyNs_  = base + nsPerByte_ * got;
        started_      = true;
    }
    return got;  // 0 at the end of the tape: a quiet line, not an error
}

size_t TapeStream::write(const uint8_t* buf, size_t n) {
    if (mode_ != Mode::Record) return 0;  // RECORD was never pressed
    size_t put = 0;
    while (put < n && tape_.write(buf[put])) ++put;
    return put;
}

} // namespace altair
