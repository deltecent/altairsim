#pragma once
//
// BitSerialLine -- an RS-232 line with NO UART behind it: the guest moves the bits itself.
//
// A board that bit-bangs its serial port (the CompuPro Disk 1, port 3 D7) has a latch on the
// transmit wire and a buffer on the receive wire, and nothing else. The guest makes each
// start bit, data bit and stop bit with an OUT, and times them with its own delay loop. The
// far end -- a terminal, a socket, the console -- is a UART, and it is the far end's UART
// that this object models: it turns the levels the guest writes into bytes, and the bytes
// the far end sends into levels the guest reads.
//
// A CHIP, NOT A BOARD, and the levels here are the WIRE'S LOGIC LEVELS: true = MARKING
// (logic 1, the idle line), false = SPACING (logic 0, a start bit). Which way up D7 is on
// the bus is the BOARD'S (the Disk 1 inverts it), and it stays on the board.
//
// THE FRAME is 8 data bits, LSB first, no parity, 1 stop bit, at `baud`. One bit time is
// clock.hz() / baud T-states. hz() is the CPU crystal, and never 0, so under `clock_hz = 0`
// the guest's delay loop and this line still agree: both count T-states.
//
// TRANSMIT. Each level the guest writes is kept with its time. A MARK-to-SPACE edge on an
// idle line starts a frame. The far end samples the middle of each bit: the start bit at
// 0.5 bit times (still SPACE, or it was a glitch and is ignored), data bit i at 1.5 + i,
// and the stop bit at 9.5. A MARK stop bit sends the byte to the stream. A SPACE stop bit
// is a framing error (a break is one): the byte is dropped and drainLog() says so. One
// Clock deadline at 9.5 bit times finishes each frame, so the last byte of a burst goes
// out without waiting for another write.
//
// RECEIVE. A byte waiting on the stream is taken when the guest next reads the line while
// it is idle, and its frame starts at that moment. The level is then a function of the time
// alone: SPACE for the start bit, the data bits, then MARK. No clock event runs per bit.
// Starting the frame when the guest looks, and not when the byte reached the host, is the
// rule Uart1602::poll keeps for the same reason: a ByteStream is buffered and flow-
// controlled, and a byte the guest was not watching for would be data loss the host
// transport does not have.

#include "core/board.h"   // Clock, Property
#include "host/stream.h"  // ByteStream, LineParams

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace altair {

class StateWriter;
class StateReader;

class BitSerialLine {
public:
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;

    BitSerialLine();
    ~BitSerialLine();

    BitSerialLine(const BitSerialLine&)            = delete;  // a deadline with `this` in it
    BitSerialLine& operator=(const BitSerialLine&) = delete;  // lives in the clock queue

    // The board forwards its Clock from clockAttached(). Unclocked, the line is dead: it
    // reads MARK and sends nothing.
    void attachClock(Clock* c);

    // The line rate. On a bit-banged port it is the far end's setting and the guest's delay
    // loop; nothing on the board sets it.
    long long baud = 9600;

    // ---- the two wires, in logic levels (true = MARK) ----
    void setTx(bool mark);  // the transmit latch moved (or was written with the same level)
    bool rx();              // the receive wire now; may start the next waiting byte
    bool tx() const { return txMark_; }

    // Power applied: the line is idle at MARK and any frame in flight is gone, because the
    // clock's time and queue were reset with it.
    void powerOn();

    // ---- the connector ----
    void connect(std::unique_ptr<ByteStream> s);
    void disconnect();
    ByteStream& stream() { return *stream_; }
    std::string endpoint() const { return stream_->describe(); }
    void pump() { stream_->pump(); }

    // Bytes handed to the guest, monotonic -- the run loop's live-traffic signal.
    uint64_t rxBytes() const { return rxCount_; }
    // READS OF AN IDLE WIRE, monotonic: the guest asked and the line had nothing
    // for it -- the guest WAITING, the run loop's idle signal (#948, Board::emptyPolls).
    // Counted here, on the guest's read of the wire, and not in the stream.
    uint64_t emptyPolls() const { return emptyPolls_; }

    // Framing errors and wire refusals, for the board to say out loud.
    std::vector<std::string> drainLog() {
        auto out = std::move(log_);
        log_.clear();
        return out;
    }

    // `baud` and `connect`, for the board's serial unit.
    std::vector<Property> properties(EndpointResolver resolve);

    // SNAPSHOT/RESTORE. The levels, the frame in flight on each wire and the receive count.
    // `baud` is a setting and the stream a host handle: neither travels.
    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // T-states from a frame's start to `halfBits` half bit times into it.
    uint64_t at(int halfBits) const;
    void     programLine();
    void     arm();
    void     finishFrame();
    void     catchUp();
    bool     txLevelAt(uint64_t t) const;

    std::unique_ptr<ByteStream> stream_;
    std::vector<std::string>    log_;
    Clock*                      clock_ = nullptr;
    Clock::Handle               wake_  = Clock::kNone;

    // ---- transmit (snapshot) ----
    bool                                   txMark_  = true;
    bool                                   txBusy_  = false;  // a frame is being sampled
    uint64_t                               txStart_ = 0;
    std::vector<std::pair<uint64_t, bool>> txEdges_;          // since txStart_, in order

    // ---- receive (snapshot) ----
    bool     rxBusy_  = false;
    uint64_t rxStart_ = 0;
    uint8_t  rxByte_  = 0;
    uint64_t rxCount_ = 0;
    uint64_t emptyPolls_ = 0;  // emptyPolls() -- see above
};

} // namespace altair
