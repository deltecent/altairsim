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

#include "core/value.h"
#include "host/stream.h"

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
    void   pump() override {}

    // ---- The human half ----

    // Raw mode, refcounted and restored on the way out. Safe to call when stdin
    // is not a terminal (a script, the test suite): it does nothing and says so
    // by leaving `raw()` false, and the stream still works -- which is what makes
    // `altairsim -s script.cmd` able to drive a guest with no tty at all.
    void enterRaw();
    void leaveRaw();
    bool raw() const { return raw_; }

    // Did the operator hit ATTN? Consumes the flag: asking clears it, so the
    // monitor cannot re-trigger on a stale press.
    bool takeAttn();

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

    std::vector<Property> properties();

private:
    Console() = default;

    uint8_t  attn_     = 0x05;  // Ctrl-E
    bool     raw_      = false;
    int      rawDepth_ = 0;
    uint64_t written_  = 0;

    // MUTABLE, and they earn it. `readable()` is const because "is a character
    // ready?" is a question, not a mutation -- but ANSWERING it means going to the
    // keyboard, and once you have taken a byte off the OS you cannot put it back.
    // So the byte gets latched here, and the ATTN key gets caught here, and both
    // happen inside a const method.
    //
    // That is not a compromise of the model; it is the model. These are HARDWARE
    // LATCHES. A UART's receive register fills whether or not anybody asks.
    mutable bool    attnSeen_ = false;
    mutable bool    peeked_   = false;
    mutable uint8_t peekByte_ = 0;
    mutable bool    eof_      = false;

    bool pollByte(uint8_t& out) const;
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
