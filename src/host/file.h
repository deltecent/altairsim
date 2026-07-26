#pragma once
//
// `in:PATH` / `out:PATH` -- a host file as a ByteStream (DESIGN.md 7.1).
//
// The honest model of a paper-tape station: a READER that feeds a file into the
// line (a byte source) and a PUNCH that writes the line to a file (a byte sink).
// The two are physically independent -- separate files, separate positions -- and
// neither seeks, so DIRECTION IS THE KEYWORD, not an option:
//
//   in:PATH            a reader -- readable() true while bytes remain
//   out:PATH           a punch  -- writable() always, bytes land on disk
//   in:PATH,out:PATH   both, on a unit whose one line is bidirectional (a 4PIO
//                      section, a 2SIO channel)
//
// Putting the direction in the name is what keeps the eager-UART "one head"
// hazard (host/tape.h) away: a 6850 that reads whenever RDRF is clear can never
// disturb the punch, because the punch is a DIFFERENT FILE.
//
// PACING: the reader may carry its own clock -- `in:tape.tap?cps=300` is the
// 88-HSR (issue #152). That rides ByteStream::pacesItself() and the same wall
// clock the cassette uses (host/tape.h): a byte is not readable until its turn.
// nsPerByte == 0 is full speed, and then the wall is never consulted.
//
// 8-BIT CLEAN, like every line (host/filter.h): the files are opened BINARY, so a
// CR the guest sent stays a CR and nothing on the way to disk rewrites a byte.
//
// PROVENANCE, like a mount: describe() returns the exact spec the operator gave,
// so SHOW prints it and CONFIG SAVE round-trips it. A relative PATH is rebased
// against the board's config dir BEFORE the resolver ever sees it (the board is
// the only thing that knows its dir; see rebaseEndpointPaths in host/endpoint.*),
// and the board remembers the ORIGINAL spec, so this stream only ever echoes what
// it was handed.

#include "host/stream.h"

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace altair {

class FileStream : public ByteStream {
public:
    // The resolver opens/validates the files, so it can refuse cleanly with a
    // reason, then hands the results here:
    //   input   -- the reader's bytes (empty vector = a 0-byte tape; nullopt = no
    //              `in:` was given, so the line reads nothing at all)
    //   output  -- the punch, opened for write and seeked to 0 (not is_open() when
    //              no `out:` was given -- writes are then consumed and dropped, as
    //              an unconnected line does)
    //   nsPerByte -- 0 for full speed, else the reader's wall-clock byte interval
    //   hostNs  -- injectable wall clock (a test drives it); defaults to steady_clock
    FileStream(std::string spec, std::optional<std::vector<uint8_t>> input,
               std::fstream output, uint64_t nsPerByte = 0,
               std::function<uint64_t()> hostNs = {});

    std::string describe() const override { return spec_; }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override;
    bool writable() const override { return true; }  // a punch, or a dropped write
    bool pacesItself() const override { return nsPerByte_ != 0; }

    void flush() override {
        if (out_.is_open()) out_.flush();
    }

private:
    std::string          spec_;
    std::vector<uint8_t> in_;
    bool                 hasIn_ = false;
    size_t               pos_   = 0;  // the reader's head
    std::fstream         out_;        // the punch (may be closed = no out:)

    // The reader's own clock -- wall time, not the guest's. See host/tape.h for the
    // absolute-cadence reasoning this mirrors byte for byte.
    uint64_t                  nsPerByte_ = 0;
    std::function<uint64_t()> hostNs_;
    uint64_t                  nextReadyNs_ = 0;
    bool                      started_     = false;
};

} // namespace altair
