#pragma once
//
// COM2502 -- the 1602-family UART. SMC's part; the same 40-pin chip everyone else
// second-sourced as the AY-5-1013 (GI), the TR1602 (Western Digital) and the
// IM6402 (Intersil). Modeled from reference/com2502.pdf.
//
// A CHIP, NOT A CARD. The 88-SIO has one of these on it (src/boards/mits-88sio.h),
// and so does the 88-ACR -- whose manual identifies it as an AY-5-1013/TR1602,
// which is the same part under a different silk screen. Two boards, one chip, and
// this is where it lives so that the second one does not get its own hand-rolled
// half of it.
//
// It knows nothing about S-100, and it knows nothing about the 88-SIO's inverted
// status bits. THE CHIP'S STATUS IS TRUE SENSE -- RDA high means a character is
// waiting, TBMT high means the transmit buffer is free -- because that is what the
// pins do. The 88-SIO runs some of them through inverting buffers on its way to the
// bus, and the 88-2SIO (a different chip entirely) does not invert at all. THE
// INVERSION IS THE BOARD'S, and it stays on the board. See SioBoard::statusByte().
//
// ---------------------------------------------------------------------------
// WHAT IS A REGISTER HERE AND WHAT IS A PIN, because on this chip it is not what
// forty years of memory-mapped UARTs would lead you to expect.
//
// There is NO control register. None. The word format -- 5-8 data bits, 1 or 2 stop
// bits, no/odd/even parity -- arrives on FIVE DEDICATED PINS (NDB1, NDB2, NSB, NPB,
// POE), and on both cards that use this chip those pins go to solder pads. Software
// cannot change the frame on the wire; a soldering iron can. That is the deepest
// difference between this chip and the 6850, where the identical numbers come out
// of a control register the guest wrote, and it is why they are two files.
//
// The status word is real, though, and it IS a register: /SWE (pin 16, Status Word
// Enable) gates RPE, RFE, ROR, RDA and TBMT onto the bus together, out of the
// chip's own status word buffer. The board decides where those five bits land and
// which way up -- but the chip is what HAS them.
// ---------------------------------------------------------------------------

#include "core/board.h"   // Clock
#include "host/stream.h"  // ByteStream, LineParams, LineParity

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace altair {

class Uart1602 {
public:
    explicit Uart1602(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    // ---- THE FORMAT PINS. Straps, not registers -- see the header comment. -----
    // Public, because a strap is not state the chip protects: it is a wire someone
    // soldered, and the CARD is what exposes it (SET sio0 data_bits=7). They exist
    // for exactly one reason -- they set how long a character occupies the line, and
    // therefore every deadline the card sets.
    long long  baud     = 9600;              // the transmit/receive clock, at 16x
    int        dataBits = 8;                 // NDB1, NDB2
    int        stopBits = 1;                 // NSB
    LineParity parity   = LineParity::None;  // NPB, POE

    // ---- THE STATUS WORD (/SWE), IN TRUE SENSE. The board may inverte it; we don't.
    bool dataAvailable() const { return rda_; }        // RDA  -- pin 19
    bool txBufferEmpty(const Clock& clk) const;        // TBMT -- pin 22

    // The three error flags EXIST and are always FALSE, deliberately -- they report
    // line noise, and there is no line. A ByteStream delivers the byte that was sent
    // or it delivers nothing. Synthesizing them would mean inventing a noise model,
    // which means inventing a probability, which is the exact kind of number
    // DESIGN.md 0.1 says to ask about rather than make up. ROR (the overrun) has its
    // own reason as well, and it is in the .cpp over poll().
    bool parityError() const { return false; }   // RPE -- pin 13
    bool framingError() const { return false; }  // RFE -- pin 14
    bool overrun() const { return false; }       // ROR -- pin 15

    // ---- THE DATA REGISTERS ----
    //
    // readData() is the receive holding register AND the /RDAR pin in one, because
    // that is how both cards wire it: the read strobe clears Data Available. It
    // clears RDA whether or not it was set -- a read of an empty receive register on
    // a real UART hands you whatever was last in it and does not error.
    uint8_t readData();

    // ...and writeData() is /TDS. The character goes out, and TBMT falls until it has
    // had time to leave. TBMT IS A DEADLINE, NOT A FLAG (DESIGN.md 7.5): the transmit
    // buffer is empty once the character has had time to get off the wire, and a
    // guest that polls it sooner sees it busy, which is the whole point.
    void writeData(uint8_t v, const Clock& clk);

    // MASTER RESET -- pin 21, and unlike the 6850 this chip really has the pin. The
    // data sheet: MR "sets TSO, TEOC and TBMT high, and clears RDA, RPE, RFE, ROR."
    // Note what is NOT in that list: the format pins, because they are PINS, and a
    // reset cannot unsolder anything.
    void masterReset(const Clock& clk);

    // ---- THE LINE ----
    void        connect(std::unique_ptr<ByteStream> s);
    void        disconnect();
    ByteStream& stream() { return *stream_; }
    std::string endpoint() const { return stream_->describe(); }

    // Bytes this chip has handed the guest, monotonic -- the run loop's live-traffic signal.
    // See Mc6850::rxBytes for why it lives at the chip: a transfer runs on some line other
    // than the console, and this is where every line's received bytes actually cross.
    uint64_t rxBytes() const { return rxCount_; }
    void        pump() { stream_->pump(); }

    // The frame the straps describe, and the act of pushing it at the wire. The CARD
    // calls programLine() when a strap moves -- a jumper you resolder is a jumper the
    // real port should be reopened for.
    LineParams params() const;
    void       programLine();

    // What the wire said back, for the card to say out loud (Board::drainLog()). A
    // cable that cannot do 7 data bits is a fact about the world, not a bug.
    std::vector<std::string> drainLog() {
        auto out = std::move(log_);
        log_.clear();
        return out;
    }

    // ADVANCE THE RECEIVER. Take a character off the line if one has finished
    // arriving and the holding register is free to take it.
    //
    // Public because the CARD must be able to call it: an interrupt-driven driver
    // never touches a register, so a UART that only ingested a character when the
    // guest read a port would never ingest one at all. The receive shift register
    // fills on the UART's own clock and owes the CPU nothing.
    void poll(const Clock& clk);

    // ---- THE TWO DEADLINES, RAW, so the CARD can decide what to do about them. ----
    //
    // NOT a nextEdge() like the 6850's, and the difference is the whole chip/board
    // split in one function. The 6850 has its interrupt enables in its own control
    // register, so it can answer "when could my IRQ pin move?" by itself. THIS chip
    // has no interrupt enables and no interrupt pin -- the 88-SIO's two enable
    // flip-flops are a separate IC on the card (IC B), and the card ANDs them with
    // these deadlines itself. So the chip reports when its registers next change,
    // and the board works out whether anyone was listening.
    uint64_t txFreeAt() const { return txFreeAt_; }
    uint64_t rxNextAt() const { return rxNextAt_; }

    // ...and whether there is anything on the line to arrive AT rxNextAt_. A byte
    // appearing out of nowhere is not a deadline -- it is an event in the outside
    // world, and pump() is the door it comes through (DESIGN.md 7.1).
    bool rxWaiting() const { return !rda_ && stream_->readable(); }

private:
    // How long one character occupies the line, in T-states. Falls straight out of
    // the format pins, which is why they are on the chip and not on the card.
    int      bitsPerChar() const;
    uint64_t charTStates(const Clock& clk) const;

    std::string name_;

    // THE LINE, RAW -- no filter. See connect().
    std::unique_ptr<ByteStream> stream_;
    std::vector<std::string>    log_;

    uint8_t  rxData_  = 0;
    uint64_t rxCount_ = 0;  // rxBytes() -- the run loop's live-traffic signal
    bool    rda_    = false;  // Data Available -- the RDAV flip-flop

    uint64_t txFreeAt_ = 0;  // TBMT is a deadline, not a flag
    uint64_t rxNextAt_ = 0;  // ...and receive is paced too, or nothing could overrun
};

} // namespace altair
