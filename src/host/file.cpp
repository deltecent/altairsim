#include "host/file.h"

#include <chrono>
#include <utility>

namespace altair {

// A MONOTONIC HOST CLOCK IN NANOSECONDS, the default source for a paced reader.
// steady_clock never runs backwards and no NTP step can make a 300-cps byte arrive
// early. It is read ONLY in wall-clock mode, so a full-speed reader -- and every
// acceptance test, which runs flat out -- never calls it and stays deterministic.
// (The cassette has the identical helper in host/tape.cpp; kept local to each so a
// stream carries its own default and neither has to expose it.)
static uint64_t steadyNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

FileStream::FileStream(std::string spec, std::optional<std::vector<uint8_t>> input,
                       std::fstream output, uint64_t nsPerByte,
                       std::function<uint64_t()> hostNs)
    : spec_(std::move(spec)), out_(std::move(output)), nsPerByte_(nsPerByte),
      hostNs_(hostNs ? std::move(hostNs) : steadyNs) {
    if (input) {
        in_    = std::move(*input);
        hasIn_ = true;
    }
}

bool FileStream::readable() const {
    if (!hasIn_ || pos_ >= in_.size()) return false;  // no reader, or off the end
    if (nsPerByte_ == 0) return true;                  // full speed: always ready
    if (!started_) return true;                        // the first byte does not wait
    return hostNs_() >= nextReadyNs_;                  // the rest arrive at the rate
}

size_t FileStream::read(uint8_t* buf, size_t n) {
    if (!hasIn_ || pos_ >= in_.size()) return 0;  // a quiet line, not an error
    if (nsPerByte_ != 0 && started_ && hostNs_() < nextReadyNs_) return 0;

    size_t got = 0;
    while (got < n && pos_ < in_.size()) buf[got++] = in_[pos_++];

    // A byte just left: set when the next one may. Advance the schedule from where
    // it ALREADY WAS -- an absolute cadence -- not from `now`, so the pauses a
    // single-threaded run loop keeps taking do not slip the whole feed later and
    // later (host/tape.cpp spells out the drag this avoids). The exception is a gap
    // longer than a whole byte time: that is the transport PAUSED (ATTN, a
    // breakpoint), and resuming re-paces from now so the backlog is not spilled as
    // a burst.
    if (got && nsPerByte_) {
        uint64_t now = hostNs_();
        if (!started_) {
            nextReadyNs_ = now;
            started_     = true;
        }
        nextReadyNs_ += nsPerByte_ * got;
        if (now > nextReadyNs_) nextReadyNs_ = now + nsPerByte_ * got;
    }
    return got;
}

// ALWAYS all of it. Like every ByteStream, write() cannot say "I took two of three"
// -- 7.1 forbids a board manufacturing a data loss the transport does not have. If
// no `out:` file is connected the bytes are dropped, exactly as an unconnected line
// drops them; either way the whole run is reported consumed.
size_t FileStream::write(const uint8_t* buf, size_t n) {
    if (out_.is_open()) out_.write(reinterpret_cast<const char*>(buf), (std::streamsize)n);
    return n;
}

} // namespace altair
