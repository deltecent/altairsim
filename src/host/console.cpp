#include "host/console.h"

#include <cstdio>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace altair {

// The saved tty state. Same approach as the line editor (src/cli/lineedit.cpp):
// POSIX directly, no #ifdef, and a clean seam for a Windows implementation when
// someone needs one. Not a terminal? Every one of these is a no-op and the
// stream still moves bytes -- which is exactly how `altairsim -s script.cmd`
// drives a guest from a pipe.
namespace {
termios g_saved{};
bool    g_savedOk  = false;
int     g_savedFl  = 0;
bool    g_savedFlOk = false;
} // namespace

Console& Console::instance() {
    static Console c;
    return c;
}

bool Console::isTty() const { return isatty(STDIN_FILENO) != 0; }

void Console::enterRaw() {
    if (rawDepth_++ > 0) return;

    // O_NONBLOCK GOES ON REGARDLESS OF WHETHER THIS IS A TERMINAL, and that is the
    // whole reason CONSOLE mode can be scripted at all.
    //
    // A tty in raw mode with VMIN=0 already returns immediately. A PIPE does not:
    // a blocking read on a pipe with no data yet BLOCKS, and it would stop
    // emulated time dead in the middle of a scripted run. With O_NONBLOCK we get
    // three distinguishable answers instead of two -- a byte, EAGAIN (nothing
    // yet), and 0 (end of input, forever) -- and it is that third one that lets a
    // scripted CONSOLE know when to give the monitor its terminal back.
    g_savedFl = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (g_savedFl >= 0) {
        g_savedFlOk = true;
        fcntl(STDIN_FILENO, F_SETFL, g_savedFl | O_NONBLOCK);
    }

    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return;
    g_savedOk = true;

    termios raw = g_saved;
    // The guest gets EVERYTHING. No canonical line editing, no echo (the guest
    // echoes, or does not, and that is its business), and -- the important one --
    // no ISIG: Ctrl-C is a byte a CP/M program is entitled to read, not a signal.
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | INLCR | ISTRIP);
    raw.c_oflag &= (tcflag_t) ~(OPOST);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    raw_ = true;
}

void Console::leaveRaw() {
    if (rawDepth_ > 0 && --rawDepth_ > 0) return;

    if (g_savedFlOk) {
        fcntl(STDIN_FILENO, F_SETFL, g_savedFl);
        g_savedFlOk = false;
    }
    if (!raw_) return;
    if (g_savedOk) tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    raw_ = false;
}

// One byte, or nothing, and NEVER a wait. Three answers:
//   n == 1   a byte
//   n == 0   END OF INPUT. A pipe that has been closed. A tty never says this.
//   n < 0    EAGAIN -- nothing right now, ask again later. A quiet line.
//
// Conflating the last two is the classic bug: treat EAGAIN as EOF and a scripted
// run stops the instant the writer pauses; treat EOF as EAGAIN and it never
// stops at all.
bool Console::pollByte(uint8_t& out) const {
    uint8_t b = 0;
    ssize_t n = ::read(STDIN_FILENO, &b, 1);
    if (n == 0) {
        eof_ = true;
        return false;
    }
    if (n != 1) return false;
    out = b;
    return true;
}

// ---------------------------------------------------------------------------
// ATTN is intercepted HERE and the guest never sees the byte.
//
// It has to be done on the way IN, at the lowest level, because everything above
// this belongs to the guest: a filter could be told to swallow it, a board could
// be in a state where it is not reading, and the guest could simply be ignoring
// its UART. None of those may be allowed to disable the one key that gets you
// out. So the check happens before anybody is offered the character at all.
// ---------------------------------------------------------------------------
bool Console::readable() const {
    if (peeked_) return true;

    uint8_t b = 0;
    if (!pollByte(b)) return false;

    if (b == attn_) {
        // The guest is never offered this byte, and so it can never swallow it.
        // The answer to "is a character ready?" is no -- and for this key it will
        // stay no however long anyone waits.
        attnSeen_ = true;
        return false;
    }

    peekByte_ = b;
    peeked_   = true;
    return true;
}

size_t Console::read(uint8_t* buf, size_t n) {
    if (!n) return 0;

    if (!peeked_ && !readable()) return 0;
    if (!peeked_) return 0;

    buf[0]  = peekByte_;
    peeked_ = false;
    return 1;
}

size_t Console::write(const uint8_t* buf, size_t n) {
    ssize_t w = ::write(STDOUT_FILENO, buf, n);
    if (w > 0) written_ += (uint64_t)w;
    return w > 0 ? (size_t)w : 0;
}

void Console::flush() { ::fflush(stdout); }

bool Console::takeAttn() {
    // A poll may be needed to notice the key at all: a guest sitting in a tight
    // spin loop reads the status port constantly, so readable() runs and catches
    // it -- but a guest that is busy computing never asks, and the operator would
    // find ATTN dead exactly when they most want it.
    if (!attnSeen_ && !peeked_) (void)readable();

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
        x.runtime = true;
        x.get     = [this] { return Value::ofInt(attn_); };
        x.set     = [this](const Value& v, std::string&) {
            attn_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

} // namespace altair
