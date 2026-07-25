#include "host/printer_stream.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace altair {
namespace {

// A monotonic host clock in nanoseconds -- the default wall source for the idle
// timer, and the same one TapeStream uses (host/tape.cpp). steady_clock never runs
// backwards and no NTP step can make a five-second timeout fire early. It is read
// ONLY while the buffer is non-empty, so a machine that never prints never calls it.
uint64_t steadyNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr uint64_t kNsPerSecond = 1'000'000'000ull;
constexpr uint8_t  kFormFeed    = 0x0C;

} // namespace

PrinterStream::PrinterStream(std::string spec, Params p, Submit submit,
                             std::function<uint64_t()> hostNs)
    : spec_(std::move(spec)), p_(p), submit_(std::move(submit)),
      hostNs_(hostNs ? std::move(hostNs) : steadyNs) {}

PrinterStream::~PrinterStream() {
    // The universal safety net: whatever the guest left in the buffer becomes one
    // last job here, because this is the one point DISCONNECT/CONFIG LOAD/exit all
    // reach. Anything that goes wrong is swallowed -- a throw out of a destructor
    // during teardown is a crash, and there is no drainLog() left to carry an error,
    // so a failure is reported to stderr and no further.
    if (buf_.empty()) return;
    try {
        std::string err;
        if (!endJob(err) && !err.empty())
            std::fprintf(stderr, "altairsim: printer: %s\n", err.c_str());
    } catch (...) {
        // never propagate
    }
}

size_t PrinterStream::write(const uint8_t* buf, size_t n) {
    lastWriteNs_ = hostNs_();  // every byte restarts the idle timer
    for (size_t i = 0; i < n; ++i) {
        buf_.push_back(buf[i]);
        // A form feed is the LAST byte of the page, so it is already in the buffer
        // when we submit -- the page it ended goes out whole.
        if (p_.onFormFeed && buf[i] == kFormFeed)
            submitIfPending();
        else if (buf_.size() >= p_.maxBytes)
            submitIfPending();
    }
    return n;  // 7.1: a line never manufactures back-pressure by reporting a short write
}

void PrinterStream::pump() {
    if (buf_.empty() || p_.idleSeconds == 0) return;
    if (hostNs_() - lastWriteNs_ >= p_.idleSeconds * kNsPerSecond)
        submitIfPending();
}

bool PrinterStream::endJob(std::string& err) {
    if (buf_.empty()) return true;  // never submit an empty buffer
    bool ok = submit_ ? submit_(buf_, err) : true;
    // Clear either way: a job that could not be sent is gone, and keeping the doomed
    // buffer would only try to submit the same bytes again at the next boundary and
    // once more in the destructor.
    buf_.clear();
    return ok;
}

void PrinterStream::submitIfPending() {
    if (buf_.empty()) return;
    std::string err;
    if (!endJob(err) && !err.empty())
        log_.push_back(err);  // drained by the board, printed by the monitor
}

std::vector<std::string> PrinterStream::drainLog() {
    auto out = std::move(log_);
    log_.clear();
    return out;
}

} // namespace altair
