#pragma once
//
// Console -- the host keyboard and screen, as a ByteStream (DESIGN.md 7.2).
//
// It is a ByteStream like any other, so the 2SIO connecting to it needs not one
// line of console-specific code. But it is the only stream with a HUMAN on the
// far end, and that costs it two things nothing else needs:
//
// 1. RAW MODE. The tty driver must stop editing lines, stop echoing, and stop
//    turning Ctrl-C into a signal -- because the GUEST wants those bytes. A CP/M
//    program that reads Ctrl-C to abort cannot see it if the driver ate it.
//
// 2. THE ATTN KEY -- and this one is NON-NEGOTIABLE (DESIGN.md 7.2). Once the
//    guest owns the keyboard, it owns ALL of it, including every key you might
//    have wanted to use to get out. A guest in a tight input loop -- which is
//    every monitor, every BASIC, every CP/M prompt ever written -- would trap you
//    with no way back to the simulator short of killing the process.
//
//    So ATTN (default Ctrl-E) is intercepted HERE, by the host, before the guest
//    is ever offered the byte. The guest cannot disable it because the guest
//    never sees it. That is the whole design: it is a key on the FRONT PANEL, not
//    a key on the terminal.
//
// THERE IS EXACTLY ONE OF THESE, because there is exactly one keyboard. Two
// boards both reading it would each get half the characters, which is not a
// hypothetical -- it is what happens the moment a machine has two 2SIOs and you
// forget. Hence `Console::instance()` and the monitor's arbitration: connecting a
// second unit to the console STEALS it, and says who from.
//
// ---------------------------------------------------------------------------
// THE KEYBOARD BUFFER (Patrick, 2026-07-12)
//
// The HOST owns the keyboard, not the board. Keys go into a buffer here, and a
// card that wants a character takes one from it. It is the arrangement AltairZ80
// uses, and it buys three things that all matter:
//
//   1. ATTN IS WATCHED WHETHER OR NOT ANYBODY IS READING. `poll()` runs every
//      slice of a RUN, so the key works while the guest is busy computing, while
//      it is spinning on a disk, and -- the case that broke the first version of
//      this -- while there is no console board in the machine at all.
//
//   2. BYTES CAN BE INJECTED. `inject()` puts characters in the buffer as though
//      they were typed, and no board can tell the difference, because at the level
//      the board sees there IS no difference. That is what MCP's send/expect and a
//      scripted boot are made of, and it costs nothing here.
//
//   3. NO LATCH TO JAM. The old design peeked ONE byte and held it for a guest to
//      collect. With no guest, the latch stayed full, the poll stopped looking, and
//      ATTN went dead exactly when you most needed it.
//
// The buffer is the HOST's, not the card's -- so it does not make the UART any
// less real. The 6850 still holds exactly one character in RDR, still sets RDRF
// when it does, and still takes the next byte only when the guest has cleared it.
// The buffer is the line, and a line is buffered and flow-controlled (DESIGN.md
// 7.1). It is a real keyboard buffer, so it is FINITE: type past the end and the
// keys are dropped, as they are on any terminal that ever beeped at you.
//
// ---------------------------------------------------------------------------
// ATTN IS TRACKED ON CONSOLE INPUT AND NOWHERE ELSE (Patrick, 2026-07-12).
//
// It lives in this class, and this class is the host's keyboard. A unit connected
// to a socket, a serial port, or a loopback IS NOT THE CONSOLE, and its data goes
// through UNALTERED -- 0x05 down a socket is a byte of somebody's protocol, and
// scanning a modem line for a key that only exists on the operator's terminal
// would be a corruption of the data, not a feature.
//
// So: nothing outside this file may look for ATTN. Not FilterStream, not the
// 2SIO, not the endpoint layer. If you find yourself adding it there, the answer
// is no.
//
// ---------------------------------------------------------------------------
// THE TRANSFORM CHAIN IS THE CONSOLE'S, AND ONLY THE CONSOLE'S (Patrick,
// 2026-07-13). DESIGN.md 7.2 always said so; a previous version of this code
// moved the chain onto the LINE -- a FilterStream inside each UART, wrapping
// whatever the unit was connected to -- so that `SET sio0 UPPER=ON` would work
// on a socket too. THAT WAS WRONG, and it was wrong in a way that corrupts data.
//
// A line is not a terminal. The 88-SIO's line goes to a modem, a socket, a
// paper-tape reader or /dev/tty.usbserial, and the next thing down it is XMODEM,
// which is 8-BIT BINARY. A transform on the line masks bit 7 of every block of
// that transfer and does it SILENTLY. The 88-ACR already knew this and refused
// the chain outright ("a tape is binary, not text", tests/test_88acr.cpp) -- the
// same argument holds for every endpoint that is not a human.
//
// So the ONLY place a byte may be altered is here, where there is a human and a
// screen, and the alteration is a fact about the TERMINAL. Everything else --
// socket, serial port, tape, file, loopback -- is 8-BIT CLEAN, ALWAYS.
//
// What a line DOES have is LINE CODING -- baud, data bits, parity, stop bits.
// That is real, it is hardware (the 88-SIO's jumpers, the 6850's control
// register), and it belongs to the card. It sets how long a character occupies
// the wire, and on a REAL serial port it is programmed into the real port
// (ByteStream::setParams). It is a FRAME, and it is never a filter: a card
// strapped for 7 data bits sends seven, because that is what the hardware does.

#include "core/value.h"
#include "host/filter.h"
#include "host/stream.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Console : public ByteStream {
public:
    static Console& instance();

    std::string describe() const override { return "console"; }

    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;
    bool   readable() const override;
    bool   writable() const override { return true; }
    void   flush() override;
    void   pump() override { poll(); }

    // ---- The human half ----

    // Raw mode, refcounted and restored on the way out. Safe to call when stdin
    // is not a terminal (a script, the test suite): it does nothing and says so
    // by leaving `raw()` false, and the stream still works -- which is what makes
    // `altairsim -s script.cmd` able to drive a guest with no tty at all.
    void enterRaw();
    void leaveRaw();
    bool raw() const { return raw_; }

    // Drain the keyboard into the buffer, and catch ATTN on the way past. Never
    // waits. Call it every slice of a run -- that is what makes ATTN work while the
    // guest is busy, and what makes it work with no console board in the machine.
    //
    // CALLER MUST NOT CALL THIS UNDER A PIPE UNLESS A UNIT HOLDS THE CONSOLE. There,
    // stdin is the MONITOR's own script, not the guest's keyboard, and draining it
    // would eat the next command.
    void poll() const;

    // Did the operator hit ATTN? Consumes the flag: asking clears it, so the
    // monitor cannot re-trigger on a stale press. ATTN is never buffered -- the
    // guest is not offered the byte, so the guest cannot swallow it.
    bool takeAttn();

    // Type FOR the operator. The bytes land in the keyboard buffer and no board can
    // tell them from a human's: at the level a board sees, there is no difference.
    // This is what MCP's send/expect and a scripted boot are built on.
    void inject(const uint8_t* buf, size_t n);
    void inject(const std::string& s);

    // What is waiting in the buffer, and what has been dropped because it was full.
    // A dropped keystroke that nobody ever mentions is a bug report about "flaky
    // input" six months from now.
    size_t   pending() const { return in_.size(); }
    uint64_t dropped() const { return dropped_; }

    uint8_t attn() const { return attn_; }

    // Is there a human out there at all? False under `-s script.cmd`, under a
    // pipe, and in the test suite -- and CONSOLE mode has to behave differently
    // when the answer is no, or a scripted run would spin forever waiting for
    // somebody to press a key.
    bool isTty() const;

    // The input has ENDED -- not "is quiet right now", but "there will never be
    // another byte". Only a pipe can say this; a terminal never does. It is the
    // difference between a lull and an unplugged cable, and CONSOLE mode needs to
    // tell them apart or it can never return.
    bool eof() const { return eof_; }

    // How many bytes the guest has PRINTED. CONSOLE mode watches this to know
    // whether the machine is still saying anything, which is how a scripted run
    // knows the guest has finished answering and it is safe to leave.
    uint64_t written() const { return written_; }

    // How many times the guest has ASKED FOR A BYTE THAT WILL NEVER COME -- polled the
    // keyboard, after the input ended, and found it empty. This is the difference
    // between a guest that is BUSY and a guest that is BEGGING, and CONSOLE mode cannot
    // work without it.
    //
    // "Silent" is not the same as "finished". A cassette bootstrap is silent for the
    // whole of a 4,439-byte tape -- it is reading the ACR and has nothing to say -- and
    // a scripted run that took silence for completion killed the machine three slices
    // into the load, at PC=0003, before BASIC ever printed a character. That is exactly
    // what this counter exists to prevent: it only moves when the guest is waiting on
    // the KEYBOARD, so a busy machine can be as quiet as it likes.
    uint64_t starved() const { return starved_; }

    std::vector<Property> properties();

private:
    Console() = default;

    // THE SCREEN AND THE KEYBOARD, UNFILTERED -- what the transform chain wraps.
    //
    // The chain has to live BELOW the thing every board reaches (Console::read and
    // Console::write, via ConsoleRef), or a board could reach around it. So the
    // filter cannot wrap the Console; it wraps this, and the Console goes through
    // the filter. `echo` then lands on the screen by the same path as everything
    // else, because inner_->write() IS the screen.
    class Raw : public ByteStream {
    public:
        explicit Raw(Console* c) : c_(c) {}
        std::string describe() const override { return "console"; }
        size_t      read(uint8_t* b, size_t n) override { return c_->readRaw(b, n); }
        size_t      write(const uint8_t* b, size_t n) override { return c_->writeRaw(b, n); }
        bool        readable() const override { return !c_->in_.empty(); }
        bool        writable() const override { return true; }
        void        flush() override { c_->flushRaw(); }

    private:
        Console* c_;
    };

    size_t readRaw(uint8_t* buf, size_t n);
    size_t writeRaw(const uint8_t* buf, size_t n);
    void   flushRaw();

    // The ONE transform chain in the simulator. Nothing else owns one.
    FilterStream filter_{std::make_unique<Raw>(this)};

    // A real keyboard buffer, and real ones are finite. 256 is a teletype's worth:
    // far more than a human can get ahead of a 2 MHz machine, and enough that an
    // injected command line never truncates.
    static constexpr size_t kMaxIn = 256;

    uint8_t  attn_     = 0x05;  // Ctrl-E
    bool     raw_      = false;
    // Is stdin OURS? enterRaw() takes it (and makes it non-blocking); until then the
    // monitor owns it, and reading it would steal a command or block the simulator.
    // Distinct from raw_, which is false for a PIPE -- and a piped guest still reads.
    bool     taken_    = false;
    int      rawDepth_ = 0;
    uint64_t written_  = 0;
    uint64_t dropped_  = 0;
    mutable uint64_t starved_ = 0;  // counted in readable(), which is const -- see below

    // MUTABLE, and they earn it. `readable()` is const because "is a character
    // ready?" is a question, not a mutation -- but ANSWERING it means going to the
    // keyboard, and once you have taken a byte off the OS you cannot put it back.
    // So it lands in the buffer here, and ATTN gets caught here, inside a const
    // method.
    //
    // That is not a compromise of the model; it is the model. This is HARDWARE: a
    // UART's receive register fills whether or not anybody asks it to.
    mutable std::deque<uint8_t> in_;
    mutable bool                attnSeen_ = false;
    mutable bool                eof_      = false;
};

// A non-owning handle to the one Console, so it can be handed to a FilterStream
// (which owns what it wraps) without the console being destroyed when a unit is
// disconnected. Unplugging a terminal does not set fire to the terminal.
class ConsoleRef : public ByteStream {
public:
    std::string describe() const override { return "console"; }
    size_t read(uint8_t* b, size_t n) override { return Console::instance().read(b, n); }
    size_t write(const uint8_t* b, size_t n) override { return Console::instance().write(b, n); }
    bool   readable() const override { return Console::instance().readable(); }
    bool   writable() const override { return true; }
    void   flush() override { Console::instance().flush(); }
};

} // namespace altair
