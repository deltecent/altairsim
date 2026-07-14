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
    // A KEYBOARD IS NOT WORTH A SYSCALL PER GUEST INSTRUCTION (Patrick, 2026-07-13).
    //
    // readable() polls, and readable() is reached from EVERY status-register read the
    // guest makes (chips/mc6850.cpp): a CP/M prompt spins on it once every three
    // instructions, so a 2,000-instruction slice was doing ~600 read(2) calls on stdin.
    // That cost MORE THAN THE EMULATION -- with the idle nap in, the syscalls alone
    // still held a quarter of a core, and they are all asking the same question of the
    // same empty keyboard.
    //
    // So we ask the OS at most every 500 microseconds. No human types faster than
    // that, no board can tell (a byte is simply seen on the next poll instead of this
    // one, half a millisecond of HOST time later), and ATTN is still watched every
    // slice, which is the only latency anybody can feel.
    //
    // This is a CACHE ON THE OS, not on the buffer: bytes already in `in_` are always
    // visible, and inject() bypasses this entirely -- an injected byte is readable the
    // instant it is injected.
    using hostClock = std::chrono::steady_clock;
    static constexpr auto kPollEvery = std::chrono::microseconds(500);
    const auto now = hostClock::now();
    if (polled_ && now - lastPoll_ < kPollEvery) return;
    polled_   = true;
    lastPoll_ = now;

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
        // TAKE FROM THE OS ONLY WHAT WE HAVE ROOM FOR, AND LEAVE THE REST WHERE IT IS.
        //
        // This loop used to read the OS dry into a 256-byte buffer and DROP the
        // overflow, silently -- and that turned a perfectly reliable pipe into a lossy
        // one. Feed a 28 KB assembler source into `PIP R.ASM=CON:` and 256 bytes of it
        // arrived; the other 28,000 were counted in dropped_, which nothing prints, and
        // the run then ended early looking for all the world like the guest had crashed.
        //
        // A PIPE HAS BACKPRESSURE AND A TERMINAL DOES NOT, and that is the whole
        // asymmetry. Bytes we do not read stay in the OS's own buffer and wait for us,
        // so there is no reason to take them before we can hold them -- the pipe IS the
        // rest of the buffer, and it is a much bigger one. Leaving them there costs
        // nothing and makes a scripted console feed of any size exact.
        //
        // The drop below is therefore now only reachable from a real keyboard, which is
        // where it belonged all along: type-ahead into a guest that is not listening has
        // genuinely nowhere to go, and a real terminal drops it too.
        const size_t room = (in_.size() >= kMaxIn) ? 0 : kMaxIn - in_.size();
        if (!room) return;  // full. Come back when the guest has eaten something.

        const size_t want  = room < sizeof buf ? room : sizeof buf;
        bool         ended = false;
        size_t       n     = platform::readInput(buf, want, ended);

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

        if (n < want) return;  // a short read means we drained what the OS had
    }
}

bool Console::readable() const {
    poll();

    // TWO COUNTERS, AND THE DIFFERENCE BETWEEN THEM IS THE WHOLE POINT.
    //
    // The guest came to the keyboard and found it EMPTY. That is all `hungry` means, and
    // it is true on a terminal -- which is what makes it the live idle signal the run
    // loop paces against (console.h, DESIGN.md 8).
    //
    // `starved` is empty AND ENDED: a guest asking a pipe that has hung up. That is a
    // guest which will wait for ever, and a scripted run uses it to know it may leave.
    // A terminal NEVER ends, so it never starves -- and a machine at a prompt with a
    // human in front of it is not begging, it is waiting. Keep them apart.
    if (in_.empty()) ++hungry_;
    if (in_.empty() && eof_) ++starved_;

    return !in_.empty();
}

// THE GUEST'S DOOR, and everything that comes through it is filtered. The chain
// is the console's alone (see console.h) -- a socket, a serial port and a tape
// never see one.
size_t Console::read(uint8_t* buf, size_t n) {
    if (!n) return 0;
    poll();

    // A BYTE CROSSING INTO THE GUEST IS THE PROOF THAT IT IS NOT IDLE, and it is the
    // only proof that holds. The run loop's other signals do not: a guest receiving
    // XMODEM polls an empty keyboard hundreds of times per slice (it is waiting for the
    // next byte at 76,800 bps) and prints nothing at all, so by every other measure it
    // looks exactly like a machine sitting at a prompt. The difference -- the whole
    // difference -- is that bytes are ARRIVING. Count them, and the run loop can tell.
    //
    // This was not hypothetical. With only the poll ratio and the warmup, a console fed
    // a byte every 5 ms still napped: the slice that swallowed the byte polled 600 times
    // either side of it and was judged idle anyway. See monitor.cpp.
    //
    // BUT THE CONSOLE IS NOT THE ONLY LINE, and this counter only ever saw this one.
    // A transfer running on a 2SIO's OTHER port -- the usual arrangement, console on 'a'
    // and the file on 'b' -- crossed no console byte, so it napped anyway (bug #6). The
    // run loop's idle signal is now Machine::rxBytes(), which is THIS SAME IDEA counted at
    // every UART in the backplane (Mc6850::rxBytes). This counter stays as the console's
    // own honest tally of what it delivered; it is no longer what the nap consults.
    size_t got = filter_.read(buf, n);
    consumed_ += got;
    return got;
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
