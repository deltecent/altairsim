#pragma once
//
// `ENDPOINT|FILE` -- a transparent TAP on a serial line, to a hex log (a poor
// man's protocol analyzer). DESIGN.md 7.1, 7.7.
//
// A TeeStream WRAPS another ByteStream and copies every byte that passes through,
// in both directions, to a text file -- then forwards the call unchanged. It is a
// decorator, the same shape as FilterStream (host/filter.h), but with the one
// difference that makes it safe on ANY line and not just the console:
//
//   A FILTER MUTATES BYTES, so there is exactly one of them and it lives on the
//   console, because `strip7out` on a binary transfer corrupts it silently. A TEE
//   NEVER TOUCHES A BYTE -- it observes and forwards -- so it is 8-bit clean by
//   construction and the "one filter, on the console" rule does not apply to it.
//   You may tap a socket, a real serial port, a tape, anything, and the guest
//   cannot tell the tap is there.
//
// The grammar lives in resolveEndpoint (host/endpoint.cpp), the one place allowed
// to know it: a `|` anywhere in a spec means "pipe this line into a capture file",
// the left side is any endpoint (recursed through the resolver), the right side is
// the log path plus the tee's own `?key=value&...` options. This class is handed
// the parsed pieces and an already-open log file.
//
// TIMING IS WALL TIME, NOT THE GUEST'S. Like the printer's idle timer and the
// paper-tape reader's cadence, the tee stamps events from a monotonic host clock
// (injectable, so a test drives it and asserts exact output). The emulated Clock is
// wrong for this: it counts T-states and stops dead at a monitor prompt, and a
// protocol trace whose timestamps freeze whenever the guest idles is a trace of
// nothing.
//
// describe() ECHOES ITSELF -- `inner|FILE?opts` -- so SHOW prints the tap and
// CONFIG SAVE round-trips it (the `|` re-triggers the branch on reload).

#include "host/stream.h"

#include <cstdint>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class TeeStream : public ByteStream {
public:
    // The three layouts, all fed from one internal event stream (see the .cpp). The
    // resolver maps `fmt=dump|cols|jsonl` onto these.
    enum class Fmt {
        Dump,   // strictly chronological, one hex row per line -- greppable (default)
        Cols,   // TX left, RX right, time-ordered -- a request/response reads down
        Jsonl,  // one JSON record per transfer -- for diffing and scripting
    };
    // Timestamp style: elapsed since the first event, host wall-clock, or none.
    enum class Ts { Elapsed, Wall, None };

    // The endpoint spec's parsed options; the resolver fills these from the `?query`.
    struct Params {
        Fmt      fmt   = Fmt::Dump;
        int      width = 16;                // bytes per hex row (dump/cols)
        uint64_t gapNs = 200'000'000ull;    // idle that ends a burst / flushes a row
        Ts       ts    = Ts::Elapsed;
        bool     pins  = true;              // log DTR/RTS/DCD/CTS/RI/break edges too
    };

    // `inner` is the wrapped line; `fileSpec` is the operator's original `FILE[?opts]`
    // text, echoed after the `|` by describe(). `log` is the already-open, truncated
    // log file (the resolver opens it so it can refuse cleanly at CONNECT). `hostNs` is
    // the monotonic wall source -- injectable for deterministic tests, defaulting to a
    // steady_clock. `wallBase` (0 = "now") seeds `ts=wall`: calendar time is that base
    // plus the monotonic delta, so a test can pin the wall clock too.
    TeeStream(std::unique_ptr<ByteStream> inner, std::string fileSpec, Params p,
              std::ofstream log, std::function<uint64_t()> hostNs = {},
              std::time_t wallBase = 0);

    // Flushes any pending partial row and closes the file -- the one boundary every
    // teardown path (DISCONNECT, CONFIG LOAD, exit) reaches, the PrinterStream precedent.
    ~TeeStream() override;

    // The tap is `inner|FILE?opts`, so SHOW / CONFIG SAVE round-trip it.
    std::string describe() const override { return inner_->describe() + "|" + fileSpec_; }

    // The data path -- the ONLY bytes logged are real transfers, never empty polls:
    //   write -> log a TX event, then forward.
    //   read  -> forward, then log an RX event of what actually arrived (got > 0).
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    // Everything below is verbatim passthrough, so RDRF/TDRE, pacing, the modem pins
    // and the line rate are exactly as if the tap were not there.
    bool readable() const override { return inner_->readable(); }
    bool writable() const override { return inner_->writable(); }
    bool pacesItself() const override { return inner_->pacesItself(); }
    bool pacedReceive() const override { return inner_->pacedReceive(); }

    // flush() pushes the LOG's buffer (so `tail -f` on the capture is live) as well as
    // the inner's -- it is NOT a row boundary (some boards flush every slice).
    void flush() override;

    // pump() forwards, then flushes a burst that has gone quiet for `gap` -- the same
    // host-time idle check the printer's job timer uses.
    void pump() override;

    // The far end's pins pass through untouched; on the way past, an EDGE is logged
    // (when pins=on). status() is const and polled constantly, so its edge-logging
    // state is mutable -- the ScriptedStream::readable() precedent.
    LineStatus status() const override;
    void       setControl(const LineControl& c) override;
    bool       setParams(const LineParams& p, std::string& err) override;

    // The inner's operator messages, merged with the tee's own (e.g. a write error on
    // the log file). Move-and-clear, like the chips.
    std::vector<std::string> drainLog() override;

private:
    // --- formatting (all const: they only touch the mutable logging state below, so
    // status() can keep events in time order by flushing pending data before a pin) ---
    void emitData(bool tx, const uint8_t* buf, size_t n) const;  // dump/cols/jsonl data
    void emitPin(const char* pin, bool up) const;                // a single pin edge
    void flushRow() const;                                       // the pending hex row
    void endBurst() const;    // flush the row and end the same-direction run
    void writeLine(const std::string& s) const;
    std::string stamp() const;              // the timestamp column for the current event
    uint64_t    now() const { return hostNs_(); }

    std::unique_ptr<ByteStream> inner_;
    std::string                 fileSpec_;
    Params                      p_;
    std::function<uint64_t()>   hostNs_;

    // Wall-clock seed for ts=wall: calendar seconds at construction + the monotonic
    // delta since. Sub-second precision comes from the delta, not from a second clock.
    std::time_t wallBaseSecs_ = 0;
    uint64_t    wallBaseNs_   = 0;

    mutable std::ofstream log_;

    // t0 for elapsed timestamps -- the first event, set lazily so the trace starts at +0.
    mutable uint64_t baseNs_  = 0;
    mutable bool     baseSet_ = false;

    // The pending same-direction run ("burst") and the partial hex row within it. A
    // burst spans as many `width`-byte rows as its bytes need; its byte OFFSET column
    // accumulates across those rows and resets when the burst ends (direction change or
    // an idle gap). All mutable so the const status()/pin path can flush it in order.
    mutable bool                 burstActive_ = false;
    mutable bool                 burstTx_     = false;
    mutable size_t               burstOffset_ = 0;
    mutable std::vector<uint8_t> row_;
    mutable uint64_t             rowStartNs_  = 0;
    mutable uint64_t             lastByteNs_  = 0;

    // Previous pin/line levels, for edge detection. statusInit_ suppresses a spurious
    // edge on the very first status() poll (there is no "before" to compare to).
    mutable LineStatus prevStatus_;
    mutable bool       statusInit_ = false;
    LineControl        prevControl_;
    LineParams         prevParams_;
    bool               paramsInit_ = false;

    mutable std::vector<std::string> log_msgs_;
};

} // namespace altair
