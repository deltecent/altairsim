#pragma once
//
// ByteStream -- the generic serial endpoint (DESIGN.md 7.1).
//
// EVERY board that moves characters talks ONLY to this: the 88-2SIO, the 88-SIO,
// the ACR cassette, the line printer, the paper-tape reader, the PMMI modem. A
// board never opens a socket, never touches termios, never sees a file handle.
//
// That is not tidiness. It is the reason a board written next year is
// automatically cross-platform, automatically scriptable, and automatically
// replayable -- and the reason `CONNECT sio:a socket:2323` needs no code in the
// 2SIO at all. The board knows it has a serial line. It does not know, and must
// never learn, what is on the other end of it.
//
// DISCIPLINE: THE BOARD NEVER BLOCKS. It asks readable()/writable() and moves on.
// A UART that blocked would stop emulated time, and stopping emulated time to
// wait for a human is precisely how an emulator comes to drop characters.

#include <cstddef>
#include <cstdint>
#include <string>

namespace altair {

// ---------------------------------------------------------------------------
// THE MODEM CONTROL LINES, AND THEY GO BOTH WAYS.
//
// TRUE IS ALWAYS "ASSERTED", in both structs and on every stream. The pin-level
// inversions are real -- the 6850's are /DCD and /CTS, active low -- but they are
// a fact about THAT CHIP'S PINS, and they stay inside the chip that has them
// (mits-2sio.cpp). A stream that reported "carrier = true, meaning the pin is
// high, meaning there ISN'T one" would be exporting one chip's polarity to every
// other card in the machine, and the 88-SIO would be wrong for free.
//
// A console or a file has none of these in any real sense, so it ASSERTS them all
// -- which is exactly what strapping DCD and CTS to ground on the connector does,
// and what period installers did constantly. That is why there is still no "what
// if nothing is plugged in" branch anywhere in any board.
// ---------------------------------------------------------------------------

// INPUTS -- what the far end is driving at us.
struct LineStatus {
    bool carrier = true;   // DCD
    bool cts     = true;   // clear to send
    bool dsr     = true;   // data set ready
    bool ring    = false;  // RI -- the PMMI counts ring bursts to answer the phone
};

// OUTPUTS -- what the CARD is driving at the far end. New: RTS was decoded out of
// the 6850's control register and then dropped on the floor, because there was
// nowhere for it to go.
struct LineControl {
    bool rts = false;  // 6850 control bits 5-6
    bool dtr = false;  // dropping it HANGS UP -- a socket closes, a modem lets go
    bool brk = false;  // transmit break: hold the line spacing
};

// WHAT THE CARD IS PROGRAMMED TO, WHICH IS WHAT IS ON THE WIRE (Patrick,
// 2026-07-12: "the real serial port is the limiting factor").
//
// There is exactly ONE line rate in a serial card and it is the UART's clock -- a
// jumper. The frame format is whatever the guest wrote into the control register.
// So when a unit is CONNECTed to a real host serial port, the CARD PROGRAMS THE
// PORT: `SET sio0:a BAUD=300` restraps the card and the host UART follows, and a
// guest that reconfigures the chip for 7E1 reconfigures the wire for 7E1, exactly
// as it would on the bench.
//
// The alternative -- a second, independent baud rate on the endpoint -- was in the
// plan and is now struck. It could only ever configure a MISMATCH, and a 6850
// strapped for 300 driving a terminal set to 9600 does not give you a fast link on
// real hardware. It gives you garbage.
//
// Every stream but a real serial port ignores this: a socket has no baud rate, and
// pretending it did would be pacing an emulation against a fiction.
enum class LineParity { None, Even, Odd };

struct LineParams {
    long long  baud     = 9600;
    int        dataBits = 8;
    int        stopBits = 1;
    LineParity parity   = LineParity::None;
};

class ByteStream {
public:
    virtual ~ByteStream() = default;

    // What the operator typed to make this: "console", "socket:2323", "null".
    // Reported by SHOW; round-tripped by CONFIG SAVE.
    virtual std::string describe() const = 0;

    // NON-BLOCKING, both of them. read() returns 0 when there is nothing there,
    // which is not an error and not an end of file -- it is a quiet line.
    virtual size_t read(uint8_t* buf, size_t n) = 0;
    virtual size_t write(const uint8_t* buf, size_t n) = 0;

    virtual bool readable() const = 0;  // -> drives RDRF
    virtual bool writable() const = 0;  // -> drives TDRE

    // THIS STREAM CARRIES ITS OWN CLOCK. A cassette releases bytes at a cadence of its
    // own -- full speed, or a wall-clock baud the CPU's speed cannot drag -- and it
    // reports that cadence through readable(). When this is true the UART must NOT also
    // impose its emulated line-rate gate, or the two clocks fight and the tape runs at
    // whatever the crystal happens to be (see Uart1602::poll and host/tape.h). Every
    // other line -- a socket, a real serial port, a keyboard -- has no cadence but the
    // one the UART's baud gives it, so this stays false for them.
    virtual bool pacesItself() const { return false; }

    virtual void flush() {}

    // The far end's pins. A LEVEL, with no memory: the stream says "carrier is
    // down", and it says it for as long as carrier is down. THE CHIP DOES THE
    // LATCHING -- the 6850 holds a DCD flag after the pin comes back, and clears it
    // only on a status-then-data read (data sheet, and mits-2sio.cpp). Put that in
    // the stream and every stream re-implements it slightly differently.
    //
    // The same division as PHANTOM*: the wire carries a level, the honoring card
    // decides what it means.
    virtual LineStatus status() const { return {}; }

    // The card's pins. Most streams ignore it -- a file has no RTS.
    virtual void setControl(const LineControl&) {}

    // The card programs the line. See LineParams: only a real serial port cares,
    // and it is the only one that can REFUSE -- an FTDI cable that cannot do 76800
    // baud is a fact about the world, and the card must be able to say so out loud
    // (Board::drainLog()) rather than run at the wrong speed in silence.
    //
    // False + err means "the wire is not what you asked for". It does NOT mean the
    // connection failed: the card is still strapped to what it is strapped to, and
    // it goes on pacing the guest at that rate, because that pacing is the half the
    // guest can actually measure.
    virtual bool setParams(const LineParams&, std::string& err) {
        (void)err;
        return true;
    }

    // Called once per time slice by the run loop, NOT by the board. This is where
    // a stream may talk to the host: accept a connection, drain a socket, poll a
    // keyboard. Keeping it out of the board's path is what makes the board pure.
    virtual void pump() {}

    uint8_t readByte() {
        uint8_t b = 0;
        return read(&b, 1) == 1 ? b : 0;
    }
    void writeByte(uint8_t b) { write(&b, 1); }
};

// ---------------------------------------------------------------------------
// /dev/null with a DB-25 on it. A disconnected line is NOT an error condition
// and must never be one: an unconnected 6850 on a real card sits there happily
// with TDRE set and RDRF clear forever, and software that writes to it works
// fine and simply talks to nobody.
//
// So this is what an unconnected unit is bound to -- there is no null pointer
// anywhere in a board's stream path, and therefore no branch in any board for
// "what if nothing is plugged in".
// ---------------------------------------------------------------------------
class NullStream : public ByteStream {
public:
    std::string describe() const override { return "null"; }
    size_t read(uint8_t*, size_t) override { return 0; }
    size_t write(const uint8_t*, size_t n) override { return n; }  // consumed, gone
    bool readable() const override { return false; }
    bool writable() const override { return true; }
};

// Write to it, read it straight back. A jumper between TX and RX -- which is
// what you actually put on a suspect card before you accuse the software. The
// guest sees its own bytes come back.
//
// AND IT LOOPS THE MODEM PINS BACK TOO, because that is what the plug in the
// drawer does: a loopback plug is TX-RX, RTS-CTS, and DTR-DCD-DSR. So a card
// strapped `cts=wired` onto a loopback sees its OWN RTS as clear-to-send, and a
// card that drops DTR watches its own carrier drop. That makes the loopback the
// one endpoint that can test modem control with no hardware at all -- which is
// exactly what it is for.
class LoopbackStream : public ByteStream {
public:
    std::string describe() const override { return "loopback"; }
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;
    bool readable() const override { return !q_.empty(); }
    bool writable() const override { return true; }

    LineStatus status() const override {
        LineStatus s;
        s.cts     = ctl_.rts;  // RTS-CTS, soldered across the plug
        s.carrier = ctl_.dtr;  // DTR-DCD
        s.dsr     = ctl_.dtr;  // ...and DSR
        s.ring    = false;     // nothing on a loopback plug can ring
        return s;
    }
    void setControl(const LineControl& c) override { ctl_ = c; }

private:
    std::string q_;
    LineControl ctl_;
};

// A terminal with a script instead of a human. NOT a loopback -- the two
// directions are separate, which is the whole point: the test types into `feed()`
// and reads what the guest printed out of `out()`.
//
// This is what makes a guest program a UNIT TEST. ALTMON's banner, its prompt and
// its answer to a D command are all just bytes it wrote to a serial port, and a
// test that can see them can assert on them with no terminal, no thread and no
// timing anywhere in the picture.
class ScriptedStream : public ByteStream {
public:
    std::string describe() const override { return "scripted"; }
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;

    // AND IT COUNTS THE EMPTY POLLS, exactly as the Console does (host/console.cpp).
    // The guest came to the line and found nothing on it: that, and not silence, is
    // what tells a prompt spinning on CONIN from a loader that is merely quiet while
    // it works. The MCP `run` loop reads the delta to know when to hand control back.
    bool readable() const override {
        if (pos_ < in_.size()) return true;
        ++hungry_;
        return false;
    }
    bool writable() const override { return true; }

    void feed(const std::string& s) { in_ += s; }  // as if typed
    const std::string& out() const { return out_; }
    void clearOut() { out_.clear(); }

    // Has the guest consumed everything we typed? Lets a caller wait for the guest
    // to catch up without inventing a timeout.
    bool drained() const { return pos_ >= in_.size(); }

    // Empty polls since power-on -- see readable(). Monotonic; the run loop watches
    // its delta, never its absolute value.
    uint64_t hungry() const { return hungry_; }

private:
    std::string      in_;
    size_t           pos_ = 0;
    std::string      out_;
    mutable uint64_t hungry_ = 0;
};

} // namespace altair
