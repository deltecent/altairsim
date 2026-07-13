#include "host/console.h"

#include "platform/terminal.h"

namespace altair {

// NO OS IN THIS FILE. Raw mode, non-blocking stdin and the three-way read all live
// behind src/platform/terminal.h now (DESIGN.md 2.1) -- this file used to include
// <termios.h> straight, with no conditional to make it look wrong, which is exactly
// why the 2.1 lint has to grep for OS HEADERS and not just for #ifdefs.
//
// What is left here is the part that was never about an OS: WHO owns the keyboard,
// WHEN, and what happens to a keystroke on the way past.

Console& Console::instance() {
    static Console c;
    return c;
}

bool Console::isTty() const { return platform::stdinIsTty(); }

void Console::enterRaw() {
    if (rawDepth_++ > 0) return;

    // Guest mode: the guest gets every byte (Ctrl-C included), and the read never
    // waits -- on a PIPE as much as on a tty, which is what makes CONSOLE scriptable.
    // terminal.h explains both. `raw_` stays false under a pipe, and that is not a
    // failure: there is simply no terminal to have put in raw mode, and the stream
    // still moves bytes.
    raw_   = platform::enterTermMode(platform::TermMode::Guest);
    taken_ = true;  // stdin is OURS now, and only now may we read it
}

void Console::leaveRaw() {
    if (rawDepth_ > 0 && --rawDepth_ > 0) return;

    taken_ = false;  // stdin goes back to the monitor's command line
    platform::restoreTerm();
    raw_ = false;
}

// ---------------------------------------------------------------------------
// ATTN is intercepted HERE and the guest never sees the byte.
//
// It has to be done on the way IN, at the lowest level, because everything above
// this belongs to the guest: a filter could be told to swallow it, a board could
// be in a state where it is not reading, and the guest could simply be ignoring
// its UART. None of those may be allowed to disable the one key that gets you
// out. So the check happens before the character is even put in the buffer.
// ---------------------------------------------------------------------------
void Console::poll() const {
    // WE READ THE KEYBOARD ONLY WHEN WE HAVE TAKEN IT. Outside a RUN, stdin belongs
    // to the monitor -- it is the operator's command line, or a script being piped
    // in. A board that peeked at it there would either steal the next command or
    // block the whole simulator until somebody pressed Enter, and "the emulator hangs
    // when you STEP a program that reads the console" is a bug I would rather not
    // write twice.
    //
    // enterRaw() is what makes stdin ours. Until then the keyboard is quiet, and
    // injected bytes are the only input there is -- which is exactly right for a test
    // and for MCP.
    if (!taken_) return;

    uint8_t buf[64];
    for (;;) {
        bool   ended = false;
        size_t n     = platform::readInput(buf, sizeof buf, ended);

        // END OF INPUT is not the same as a quiet line, and terminal.h keeps them
        // apart so that this can act on the difference: a closed pipe is how a
        // scripted CONSOLE knows to give the monitor its terminal back.
        if (ended) eof_ = true;
        if (!n) return;

        for (size_t i = 0; i < n; ++i) {
            uint8_t b = buf[i];
            if (b == attn_) {
                attnSeen_ = true;  // never buffered: the guest is not offered it, ever
                continue;
            }
            if (in_.size() >= kMaxIn) {
                // Typing faster than the guest can read. A real terminal drops it too
                // -- but a DROPPED KEYSTROKE MUST BE COUNTED, or it becomes a bug
                // report about "flaky input" that nobody can reproduce.
                const_cast<Console*>(this)->dropped_++;
                continue;
            }
            in_.push_back(b);
        }

        if (n < sizeof buf) return;  // a short read means we drained it
    }
}

bool Console::readable() const {
    poll();

    // The guest came to the keyboard and the keyboard is empty AND ENDED. That is a
    // guest waiting for a byte that cannot arrive -- count it. Nothing else counts:
    // a guest busy reading a cassette never gets here, however long it takes and
    // however little it says. See Console::starved().
    if (in_.empty() && eof_) ++starved_;

    return !in_.empty();
}

// THE GUEST'S DOOR, and everything that comes through it is filtered. The chain
// is the console's alone (see console.h) -- a socket, a serial port and a tape
// never see one.
size_t Console::read(uint8_t* buf, size_t n) {
    if (!n) return 0;
    poll();
    return filter_.read(buf, n);
}

size_t Console::readRaw(uint8_t* buf, size_t n) {
    if (!n || in_.empty()) return 0;

    // ONE BYTE. The card asking is a UART with a single receive register, and
    // handing it a block would be handing it something no 6850 ever had.
    buf[0] = in_.front();
    in_.pop_front();
    return 1;
}

void Console::inject(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (in_.size() >= kMaxIn) {
            dropped_++;
            continue;
        }
        in_.push_back(buf[i]);
    }
}

void Console::inject(const std::string& s) {
    inject(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

size_t Console::write(const uint8_t* buf, size_t n) { return filter_.write(buf, n); }

size_t Console::writeRaw(const uint8_t* buf, size_t n) {
    size_t w = platform::writeOutput(buf, n);
    written_ += (uint64_t)w;
    return w;
}

void Console::flush() { flushRaw(); }
void Console::flushRaw() { platform::flushOutput(); }

bool Console::takeAttn() {
    // It does NOT poll. The run loop does that every slice -- which is the whole
    // point of the buffer, and is why the key works when the guest is busy
    // computing, and when there is no console board in the machine to ask.
    bool a    = attnSeen_;
    attnSeen_ = false;
    return a;
}

std::vector<Property> Console::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name    = "attn";
        x.help    = "The key that drops from CONSOLE back to the monitor. The guest never sees it";
        x.kind    = Kind::Int;
        x.radix   = 16;
        x.min     = 1;
        x.max     = 0x1F;  // a control character, or the guest could never type it
        x.get     = [this] { return Value::ofInt(attn_); };
        x.set     = [this](const Value& v, std::string&) {
            attn_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }

    // THE TRANSFORM CHAIN -- upper, strip7in, strip7out, crlf, echo, bell, bsdel.
    //
    // They are properties of the TERMINAL, which is what this class is, and they are
    // declared through the same Property layer as a board's -- so `SET CONSOLE
    // UPPER=ON`, `SHOW CONSOLE`, `[console]` in a machine file, CONFIG SAVE, MCP and
    // tab completion all pick them up with no code anywhere else.
    //
    // MITS BASIC is why `strip7out` exists: BASIC ends a message by setting bit 7 of
    // its LAST character, so `MEMORY SIZE?` leaves the interpreter as
    // ...'S','I','Z','E'|0x80 and a modern terminal prints `MEMORY SIZ?`. The card
    // sent all eight bits and the Teletype IGNORED the eighth -- on a Model 33 that is
    // the parity position and the printer never decodes it. The ignoring belongs to
    // the terminal. It is not a strap, and it is CERTAINLY not a mask in the UART.
    for (Property& f : filter_.properties()) p.push_back(std::move(f));

    return p;
}

} // namespace altair
