#pragma once
//
// Intel 8251 / 8251A USART -- a CHIP, NOT A CARD.
//
// The SD Systems SBC-100/200 has one of these as its console USART; the next card
// with an 8251 on it gets this for free. Modeled from the Intel 8251 data sheet
// (reference/Intel 8251 USART.md), NOT from the one monitor that happens to drive it.
//
// It knows nothing about S-100. It has a clock, some pins, and a ByteStream. The
// pacing (a character occupies the line for baud-many T-states) is the same idea as
// the Mc6850's, and the code below is its sibling. What is NEW here, and the reason
// this chip exists rather than another 6850, is:
//
//   1. THE WRITE-TARGET STATE MACHINE. The 8251 has ONE control address that is the
//      mode word, then command words, disambiguated by an internal one-bit state --
//      not by the address. See writeControl() and expectMode_.
//
//   2. THE DSR PIN CAN FOLLOW THE RECEIVE LINE. The 8251's /DSR status bit (D7) is
//      just an input pin -- but the SBC-200 STRAPS RxD to it, so the monitor can
//      auto-detect baud by timing the start bit of the first character. To make that
//      work on the console, this chip models the incoming frame BIT BY BIT in
//      emulated T-states and can report the current RxD line state in D7. See
//      DsrSource and dsrBit().

#include "core/board.h"     // IrqJumper, irqJumperProperty, Property
#include "host/stream.h"    // ByteStream, LineParams, LineParity

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Clock;
class StateWriter;
class StateReader;

// TURNING AN ENDPOINT STRING INTO A STREAM IS THE MONITOR'S JOB, NOT THE CHIP'S
// (DESIGN.md 7.7). The board installs the resolver and hands it down here.
using EndpointResolver =
    std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;

// ---------------------------------------------------------------------------
// WHERE THE /DSR INPUT PIN COMES FROM, AND IT IS A FACT ABOUT THE CARD.
//
// A generic 8251 leaves /DSR tied to its inactive level (a card that does not use
// modem control grounds or pulls it), so D7 reads a constant. The SBC-200 solders
// the 8251's RxD pin to /DSR so the monitor's auto-baud can watch the serial line in
// the status register -- see the board reference. That is a strap on THAT card, not a
// property of the chip, so it lives here as a strap the board sets, exactly like the
// 6850's dcd/cts PinStraps.
// ---------------------------------------------------------------------------
enum class DsrSource {
    Inactive,   // D7 reads 0 (the /DSR pin is tied to its inactive level)
    FollowRxD,  // D7 = NOT(current RxD line bit) while a receive frame is in flight
};

class Intel8251 {
public:
    explicit Intel8251(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }

    // ---- bus register access. The CARD decodes C/D: data at C/D=0, status/command
    //      at C/D=1. On the SBC that is 7C (data) and 7D (status read / control write).
    uint8_t readData(const Clock& clk);                 // RxRDY clears
    uint8_t readStatus(const Clock& clk);               // the status byte
    void    writeData(uint8_t v, const Clock& clk);     // transmit
    void    writeControl(uint8_t v, const Clock& clk);  // MODE, then COMMANDs

    // POWER-ON. The machine switched on: a known-good state, expecting a mode word,
    // endpoint still connected. There is no bus-reset path (see the board's reset()).
    void powerOn(const Clock& clk);

    // ---- straps (config; NOT serialized) ----
    long long baud   = 9600;               // paces the receive frame AND sizes the DSR
                                           // bit width. Min 50, no zero (see the .cpp).
    DsrSource dsrSrc = DsrSource::Inactive;
    IrqJumper jumper = IrqJumper::None;     // decoded, NOT honored in Phase 1

    // ---- the line ----
    void        connect(std::unique_ptr<ByteStream> s);
    void        disconnect();
    ByteStream& stream() { return *stream_; }
    std::string endpoint() const { return stream_->describe(); }
    uint64_t    rxBytes() const { return rxCount_; }
    void        pump() { stream_->pump(); }

    // ---- state in TRUE sense, for the card and for tests ----
    uint8_t statusByte(const Clock& clk) const;  // full D0..D7 incl. the DSR bit
    bool    rxReady() const { return rxRdy_; }   // status D1
    bool    txReady(const Clock& clk) const;     // status D0 (a deadline)
    bool    txEmpty(const Clock& clk) const;     // status D2

    // ADVANCE THE RECEIVER. Take the next byte off the line if the last frame has
    // finished, and clock the in-flight frame forward. Public because the CARD calls
    // it on pump()/a deadline/a register access -- an interrupt-driven guest never
    // touches a register, so the receiver cannot depend on one being read.
    void poll(const Clock& clk);

    // Deadlines the CARD's nextEdge() needs. Absolute T-states; 0 where n/a.
    uint64_t txFreeAt() const { return txFreeAt_; }
    uint64_t rxDoneAt() const { return rxDoneAt_; }   // when the in-flight frame ends
    uint64_t rxNextAt() const { return rxNextAt_; }   // earliest a new frame may start
    bool     rxFrameActive() const { return rxActive_; }
    bool     rxWaiting() const;                        // a byte queued, no frame yet

    LineParams params() const;   // the frame, from the MODE word, for a real serial port
    void       programLine();
    std::vector<std::string> drainLog();

    // Unit-level reflection presented by the board (baud, dsr, interrupt, connect,
    // lines). `resolve` turns the connect string into a stream (the chip is not
    // allowed to know the grammar).
    std::vector<Property> properties(const EndpointResolver& resolve);

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // The MODE word, read as a bit count (paces the emulation) and as a frame (what a
    // real host UART is programmed with). One decode, two views -- they must not
    // disagree.
    int      dataBits() const;
    LineParity parity() const;
    int      stopBits() const;   // 1 or 2 (1.5 rounds up for the bit-count)
    int      bitsPerChar() const;
    uint64_t bitTStates(const Clock& clk) const;   // one bit period
    uint64_t charTStates(const Clock& clk) const;  // a whole frame
    bool     dsrBit(const Clock& clk) const;       // D7 when dsrSrc == FollowRxD
    void     driveControl();                        // RTS/DTR/BREAK out to the wire

    std::string name_;
    std::unique_ptr<ByteStream> stream_;
    std::vector<std::string> log_;

    // ---- the write-target state machine ----
    bool    expectMode_ = true;   // true after reset/internal-reset: next ctrl write = MODE
    uint8_t mode_       = 0x4E;    // last MODE word (default 8N1 x16, a sane frame)
    uint8_t command_    = 0;       // last COMMAND word

    // ---- receive: a frame clocked in over T-states (the new part vs. Mc6850) ----
    uint8_t  rxData_    = 0;       // the completed byte, readable at the data port
    bool     rxRdy_     = false;   // RxRDY (D1)
    uint8_t  rxPending_ = 0;       // the byte whose frame is on the line right now
    bool     rxActive_  = false;   // a frame is being clocked in
    uint64_t rxStart_   = 0;       // T-state the in-flight frame's start bit began
    uint64_t rxDoneAt_  = 0;       // rxStart_ + charTStates: when RxRDY rises
    uint64_t rxNextAt_  = 0;       // earliest a NEW frame may begin (flow-control pacing)
    uint64_t rxCount_   = 0;       // bytes taken off the line -- the run loop's traffic signal

    // ---- transmit (mirrors Mc6850's TDRE deadline) ----
    uint64_t txFreeAt_  = 0;       // the transmit buffer is free at this T-state
};

} // namespace altair
