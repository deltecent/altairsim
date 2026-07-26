#pragma once
//
// `printer:QUEUE[?key=value&...]` -- a host print queue as a write-only ByteStream
// (DESIGN.md 7.1, 7.7; docs/printing.md; issue #70).
//
// A printer is something on the far end of a line, so it is a ByteStream and not a
// property of the printer card -- exactly like out: and serial:. The 88-C700 works
// the day this lands, and every other card that can put a byte on a line (the future
// 88-LPC, the Sol-20 printer unit, a serial printer on the 2SIO) works later with no
// board changed. The grammar lives in resolveEndpoint (host/endpoint.cpp), the one
// place allowed to know it; this class is handed the parsed pieces.
//
// THE HARD PART IS WHERE ONE JOB ENDS. Centronics has no end-of-job signal and
// neither does a serial line: the guest writes bytes and then stops, and stopping is
// not an event. A host print queue wants a finite job handed over once, so something
// must decide the boundary. This stream buffers the bytes and closes the buffer into
// one job at a boundary, whichever fires first:
//
//   * an IDLE TIMER on WALL seconds -- every byte restarts it; expiry submits. This
//     is the usual boundary, matching what a guest does (print a report, go back to
//     the user). It runs on host time, NOT the emulated Clock: the default clock is
//     free-running, so an emulated timeout could fire in 80ms or never, and nothing
//     here is guest-readable. Same rule that moved the VDM-1 blink off the Clock.
//   * a FORM FEED (0x0C), optionally -- the closest the era had to "page done". Off
//     by default: a multi-page report is one job, and splitting it makes N jobs that
//     interleave with everyone else's on a shared queue.
//   * a BYTE CEILING (max), so a runaway loop cannot eat memory.
//   * teardown: the DESTRUCTOR submits any open job, which is the ONE boundary every
//     lifecycle path reaches -- DISCONNECT (the board swaps in a NullStream and this
//     is destroyed), CONFIG LOAD (the machine is replaced wholesale), and exit. The
//     idle timer stops when the guest stops (pump() is only called from the run
//     loop), so the destructor is the safety net, the MediaFile::commit() precedent.
//
// AN EMPTY BUFFER NEVER SUBMITS. A form feed closes the job and the idle timer then
// expires with nothing in hand; if that submitted, every printout would be followed
// by a blank job -- silent, and diagnosed only by someone at the printer counting
// blank pages. Every boundary checks there is something to send.
//
// flush() IS A NO-OP, DELIBERATELY. ByteStream::flush() means "push what you are
// holding" and the C700 calls it on EVERY pump (mits-88c700.cpp), so if it submitted
// a job every idle slice would submit an empty one. The job boundary is its own
// thing (submitIfPending), and it does not ride on flush().
//
// 8-BIT CLEAN, like every line. A printer control language needs the high bit and
// its own control codes; the bytes buffered are the bytes the guest sent, and the
// bytes submitted are those. Whether a printer line wants CR/LF translation of its
// own is a separate question with its own answer, not the console's transforms.

#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace altair {

class PrinterStream : public ByteStream {
public:
    // The submit sink -- INJECTED, so a test feeds a fake and never touches CUPS or
    // paper (docs/printing.md 4). Returns true on success; false + err on a failed
    // job (queue gone, printer off), which the operator must be told about.
    using Submit = std::function<bool(const std::vector<uint8_t>&, std::string&)>;

    // The endpoint spec's parameters. Defaults match docs/printing.md 2.3; the
    // resolver fills these in from the ?key=value grammar.
    struct Params {
        uint64_t idleSeconds = 5;                 // host seconds of silence; 0 = never
        bool     onFormFeed  = false;             // also end a job on 0x0C
        uint64_t maxBytes    = 16ull * 1024 * 1024;  // a runaway ceiling, not a policy
    };

    // `spec` is the operator's original string, echoed by describe() for SHOW and
    // CONFIG SAVE. `hostNs` is where wall time comes from -- injectable so a test can
    // drive it, defaulting to a monotonic host clock; consulted only when the buffer
    // is non-empty, so a machine that never prints never touches the wall.
    PrinterStream(std::string spec, Params p, Submit submit,
                  std::function<uint64_t()> hostNs = {});

    // Submits any open job (see the header note: DISCONNECT/CONFIG LOAD/exit all
    // reach here). noexcept and swallows every error -- an exception escaping a
    // destructor during machine teardown is a crash, and a failure here cannot reach
    // drainLog() because this object is being destroyed, so it goes to stderr.
    ~PrinterStream() override;

    std::string describe() const override { return spec_; }

    // A quiet line, forever: nothing reads back off a printout.
    size_t read(uint8_t*, size_t) override { return 0; }
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override { return false; }
    bool writable() const override { return true; }

    // NOT the job boundary -- the C700 calls it every slice. See the header note.
    void flush() override {}

    // The idle-timer check. Called once per slice from the board's pump(); submits
    // the open job when wall time has been quiet for idleSeconds.
    void pump() override;

    // Failed-job messages, drained by the board (Board::drainLog) and printed by the
    // monitor after every command and run -- the same route serial: uses to say it
    // cannot do 76800 baud. Move-and-clear, like the chips (uart1602.h).
    std::vector<std::string> drainLog() override;

    // Close the current job NOW, if any. The single boundary primitive every path
    // funnels through; a future operator verb (PRINT FLUSH / EJECT) calls it too.
    // Returns false + err on a failed submit; true (and does nothing) on an empty
    // buffer.
    bool endJob(std::string& err);

private:
    // The one submit path. Returns immediately if the buffer is empty (the
    // never-submit-empty invariant). On a failed submit it records the message in
    // log_ and clears the buffer anyway -- a job that could not be sent is gone, and
    // holding a doomed buffer forever would only submit it again at teardown.
    void submitIfPending();

    std::string               spec_;
    Params                    p_;
    Submit                    submit_;
    std::function<uint64_t()> hostNs_;

    std::vector<uint8_t>     buf_;
    uint64_t                 lastWriteNs_ = 0;
    std::vector<std::string> log_;
};

} // namespace altair
