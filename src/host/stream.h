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

// The modem control lines, as seen from the board. A console or a file has none
// of these in any real sense, so it simply asserts them -- which is exactly what
// wiring DCD and CTS to ground on the connector does, and what period installers
// did constantly.
struct LineStatus {
    bool carrier = true;  // DCD
    bool cts     = true;  // clear to send
    bool dsr     = true;  // data set ready
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

    virtual void flush() {}
    virtual LineStatus status() const { return {}; }

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
class LoopbackStream : public ByteStream {
public:
    std::string describe() const override { return "loopback"; }
    size_t read(uint8_t* buf, size_t n) override;
    size_t write(const uint8_t* buf, size_t n) override;
    bool readable() const override { return !q_.empty(); }
    bool writable() const override { return true; }

private:
    std::string q_;
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
    bool readable() const override { return pos_ < in_.size(); }
    bool writable() const override { return true; }

    void feed(const std::string& s) { in_ += s; }  // as if typed
    const std::string& out() const { return out_; }
    void clearOut() { out_.clear(); }

    // Has the guest consumed everything we typed? Lets a test wait for the guest
    // to catch up without inventing a timeout.
    bool drained() const { return pos_ >= in_.size(); }

private:
    std::string in_;
    size_t      pos_ = 0;
    std::string out_;
};

} // namespace altair
