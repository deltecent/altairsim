#pragma once
//
// Signetics / National 2651 (2661) USART -- a CHIP, NOT A CARD.
//
// The CompuPro System Support 1 has one of these as its serial channel; the next card
// with a 2651 on it gets this for free. Modeled from the 2651 data sheet as distilled in
// reference/Signetics 2651 USART.md (cross-checked against the SS-1 manual, pp.21-26),
// NOT from the one monitor that happens to drive it.
//
// It knows nothing about S-100. It has a clock, some pins, and a ByteStream. The pacing
// (a character occupies the line for one frame-time) is the same idea as the Intel8251's
// and the Mc6850's, and the receive path here is that code's sibling. What is DIFFERENT
// from the 8251, and the reason this is its own chip rather than another 8251:
//
//   1. FOUR SEPARATELY ADDRESSABLE PORTS, not two. The card decodes A0/A1 into data,
//      status, mode and command -- dispatch is by ADDRESS, so there is no data/status
//      write-target ambiguity like the 8251's. The ONE piece of internal sequencing left
//      is the mode register: MR1 and MR2 share the mode address and a one-bit pointer
//      routes the first mode access to MR1 and the next to MR2 (see writeMode/modePtr_).
//
//   2. THE BAUD RATE IS ON THE CHIP. The 2651 has an internal baud-rate generator; the
//      guest programs the rate in the low nibble of Mode Register 2 (kBaudTable). So the
//      line rate FOLLOWS the guest's MR2, unlike the SBC's 8251 where an external CTC set
//      it and `baud` was a card strap. The `baud` field here is the current effective
//      rate: it seeds the line before the guest programs it, and MR2 overwrites it.
//
//   3. THE STATUS BITS DIFFER. TxRDY = D0, RxRDY = D1 (both active-high, the 8251's
//      order); DCD = D6, DSR = D7 are inverting reporters of active-low modem lines and
//      read ASSERTED (1) for a byte-clean transport. PE/OE/FE stay 0 (the house stance --
//      a ByteStream has no line noise to report; see the .cpp).

#include "core/board.h"   // IrqJumper, irqJumperProperty, Property
#include "host/stream.h"  // ByteStream, LineParams, LineParity

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

class Sig2651 {
public:
    explicit Sig2651(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }

    // ---- bus register access. The CARD decodes A1/A0 into these four ports:
    //      data (+0), status/SYN (+1), mode (+2), command (+3) off the chip's block. ----
    uint8_t readData(const Clock& clk);                 // RxRDY clears
    uint8_t readStatus(const Clock& clk);               // the status byte
    uint8_t readMode();                                 // current MR1/MR2 (pointer-sequenced)
    uint8_t readCommand() const { return command_; }    // the current command word
    void    writeData(uint8_t v, const Clock& clk);     // transmit
    void    writeMode(uint8_t v);                        // MR1 then MR2 (see modePtr_)
    void    writeCommand(uint8_t v, const Clock& clk);   // TxEN/RxEN/DTR/RTS/break/reset-error

    // POWER-ON. A known-good state: pointer at MR1, transmitter idle, endpoint still
    // connected. There is no bus-reset path (see the board's reset()).
    void powerOn(const Clock& clk);

    // ---- straps (config; NOT serialized) ----
    long long baud   = 9600;            // the effective line rate; MR2 overwrites it. Min 50.
    IrqJumper jumper = IrqJumper::None;  // where the chip IRQ is routed (RxRDY; see the board)

    // ---- the line ----
    void        connect(std::unique_ptr<ByteStream> s);
    void        disconnect();
    ByteStream& stream() { return *stream_; }
    std::string endpoint() const { return stream_->describe(); }
    uint64_t    rxBytes() const { return rxCount_; }
    void        pump() { stream_->pump(); }

    // ---- state in the TRUE sense, for the card and for tests ----
    uint8_t statusByte(const Clock& clk) const;  // full D0..D7
    bool    rxReady() const { return rxRdy_; }    // status D1
    bool    txReady(const Clock& clk) const;      // status D0 (a deadline)
    bool    txEmpty(const Clock& clk) const;      // status D2

    // ADVANCE THE RECEIVER. Take the next byte off the line if the last frame finished,
    // and clock the in-flight frame forward. Public because the CARD calls it on
    // pump()/a deadline/a register access -- an interrupt-driven guest never touches a
    // register, so the receiver cannot depend on one being read.
    void poll(const Clock& clk);

    // Deadlines the CARD's nextEdge() needs. Absolute T-states; 0 where n/a.
    uint64_t txFreeAt() const { return txFreeAt_; }
    uint64_t rxDoneAt() const { return rxDoneAt_; }
    uint64_t rxNextAt() const { return rxNextAt_; }
    bool     rxFrameActive() const { return rxActive_; }
    bool     rxWaiting() const;

    LineParams params() const;   // the frame, from MR1 + MR2's baud, for a real serial port
    void       programLine();
    std::vector<std::string> drainLog();

    // Unit-level reflection presented by the board (baud, interrupt, connect). `resolve`
    // turns the connect string into a stream (the chip is not allowed to know the grammar).
    std::vector<Property> properties(const EndpointResolver& resolve);

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    // MR1 decoded, as a bit count (paces the emulation) and as a frame (what a real host
    // UART is programmed with). One decode, two views -- they must not disagree.
    int        dataBits() const;
    LineParity parity() const;
    int        stopBits() const;   // 1 or 2 (1.5 rounds up for the bit count)
    int        bitsPerChar() const;
    uint64_t   bitTStates(const Clock& clk) const;
    uint64_t   charTStates(const Clock& clk) const;
    void       driveControl();     // RTS/DTR/BREAK out to the wire
    void       applyMr2(uint8_t v);  // pull the baud out of MR2's low nibble

    std::string name_;
    std::unique_ptr<ByteStream> stream_;
    std::vector<std::string> log_;

    // ---- the programmed frame ----
    uint8_t mode1_   = 0x4E;   // Mode Register 1 (8 data, 1 stop, no parity by default)
    uint8_t mode2_   = 0x7E;   // Mode Register 2 (board convention 0111 hi nibble, 9600)
    uint8_t command_ = 0;      // the last command word
    int     modePtr_ = 0;      // 0 -> next mode access is MR1, 1 -> MR2 (wraps)

    // ---- receive: a frame clocked in over T-states (mirrors Intel8251) ----
    uint8_t  rxData_    = 0;
    bool     rxRdy_     = false;
    uint8_t  rxPending_ = 0;
    bool     rxActive_  = false;
    uint64_t rxStart_   = 0;
    uint64_t rxDoneAt_  = 0;
    uint64_t rxNextAt_  = 0;
    uint64_t rxCount_   = 0;

    // ---- transmit (a TxRDY deadline, like the 8251/6850) ----
    uint64_t txFreeAt_  = 0;
};

}  // namespace altair
