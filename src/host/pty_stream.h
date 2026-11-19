#pragma once
//
// `pty` and `pty:LINK` -- a serial line on a pseudo-terminal, as a ByteStream
// (issue #685).
//
// THE LINE IS THE PSEUDO-TERMINAL. A terminal program (`screen`, `minicom`, `cu`, a
// file transfer program) opens the link as it opens a serial port, and it is the only
// thing on the line. Contrast `<endpoint>|pty` (host/mirror_stream.h), where the
// pseudo-terminal is a rider on some other line and must never reach the guest's pins.
//
// A PROGRAM OPENING THE LINK *IS* CARRIER APPEARING -- host/tcp.h's mapping, for the
// same reason: it is what a terminal on the far end of a cable looks like to the board.
//
//   a program opens the link    -> DCD and DSR rise
//   it closes the link          -> DCD and DSR fall, and a 6850 latches that
//   the kernel buffer fills     -> CTS falls, so the GUEST waits and loses no byte
//
// ONE THING IS NOT THE SOCKET'S: WITH NOBODY THERE, CTS STAYS UP. A socket with no
// caller negates CTS, and a board that has CTS wired then waits. A line that nobody
// has opened must not stop the guest, so here the output goes nowhere and the board
// is told it may send.
//
// NOTHING IS ECHOED AND NOTHING IS TRANSLATED. platform::Pty keeps the slave side in
// raw mode, so the line is 8-bit clean in both directions -- XMODEM works on it.

#include "host/stream.h"
#include "platform/pty.h"

#include <memory>
#include <string>
#include <vector>

namespace altair {

class PtyStream : public ByteStream {
public:
    // `spec` is the operator's text (`pty`, `pty:/tmp/console`), echoed by describe().
    // The resolver opens the pseudo-terminal, so it can REFUSE at CONNECT.
    PtyStream(std::unique_ptr<platform::Pty> pty, std::string spec)
        : pty_(std::move(pty)), spec_(std::move(spec)) {}

    std::string describe() const override { return spec_; }

    size_t read(uint8_t* buf, size_t n) override;
    size_t doWrite(const uint8_t* buf, size_t n) override;

    bool readable() const override { return !rx_.empty(); }

    // A program that is there and slow makes the guest wait. Nobody there never does.
    bool writable() const override { return !pty_->attached() || tx_.size() < kTxCap; }
    size_t rxPending() const override { return rx_.size(); }
    size_t txPending() const override { return tx_.size(); }

    void flush() override;
    void pump() override;

    LineStatus status() const override;

    // A program has the link open (as of the last pump()).
    bool attached() const { return pty_->attached(); }

    // Where a person finds the line: `LINK (DEVICE)`. A bare `pty` picks its name at
    // run time, so the operator's own text does not say.
    std::string note() const { return pty_->link() + " (" + pty_->device() + ")"; }

    // The same, but only the FIRST time it is asked (by this or by drainLog()), so the
    // operator is told once whichever path gets there first. "" after that.
    std::string takeNote() {
        if (noteSaid_) return {};
        noteSaid_ = true;
        return note();
    }

    std::vector<std::string> drainLog() override;

private:
    static constexpr size_t kTxCap = 8192;

    std::unique_ptr<platform::Pty> pty_;
    std::string                    spec_;
    std::string                    rx_, tx_;
    bool                           wasAttached_ = false;
    bool                           noteSaid_    = false;
};

} // namespace altair
