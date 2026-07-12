#pragma once
//
// `serial:/dev/tty...` -- a REAL serial port, as a ByteStream (DESIGN.md 7.1).
//
// THE ONE ENDPOINT WHERE THE PINS ARE THE PINS. Everywhere else in the program a
// modem control line is a metaphor -- a socket's carrier is really a TCP session,
// a loopback's CTS is really its own RTS. Here DCD is DCD: a voltage on pin 8 of a
// DB-25, driven by whatever is actually on the other end of the cable, and the
// card believes it because there is nothing to disbelieve.
//
// WHAT A `wired` PIN READS WITH NOTHING ON THE FAR END (Patrick, 2026-07-12):
// FOLLOW THE HARDWARE. Report what the host UART's modem-status register actually
// says, whatever that is. We do not second-guess a real pin and we do not
// synthesize one -- if the far end is dead, the card sees a dead line, exactly as
// it would on the bench. (Every OTHER stream asserts everything, which is why a
// card strapped `cts=wired` onto the console still transmits.)

#include "host/stream.h"
#include "platform/serial.h"

#include <memory>
#include <string>

namespace altair {

class HostSerialStream : public ByteStream {
public:
    explicit HostSerialStream(std::unique_ptr<platform::SerialPort> port, std::string spec)
        : port_(std::move(port)), spec_(std::move(spec)) {}

    std::string describe() const override { return spec_; }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    bool readable() const override { return !rx_.empty(); }
    bool writable() const override { return tx_.size() < kTxCap; }

    void flush() override;
    void pump() override;

    LineStatus status() const override;
    void       setControl(const LineControl& c) override;
    bool       setParams(const LineParams& p, std::string& err) override;

private:
    // WHY THERE IS A QUEUE AT ALL, when the OS already has one.
    //
    // ByteStream::write() has no way to say "I took two of your three bytes" -- and
    // it must not, because a board that had to handle a short write would be a board
    // that could lose a byte, and 7.1 forbids manufacturing data loss the transport
    // does not have. But a real driver's buffer DOES fill: at 300 baud it fills
    // almost at once. So the bytes the driver would not take are held here, drained
    // in pump(), and the queue's depth is what negates writable() -- which is what
    // holds TDRE clear, which is what makes the GUEST wait.
    //
    // That is the honest chain, and it is the same one the hardware has: a full UART
    // is a UART that has not raised TDRE yet.
    static constexpr size_t kTxCap = 4096;

    std::unique_ptr<platform::SerialPort> port_;
    std::string                           spec_;
    std::string                           rx_, tx_;
};

} // namespace altair
