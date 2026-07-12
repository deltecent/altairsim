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
    taken_ = true;  // stdin is OURS now, and only now may we read it

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

    taken_ = false;  // stdin goes back to the monitor's command line
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
    // WE READ THE KEYBOARD ONLY WHEN WE HAVE TAKEN IT. Outside a RUN, stdin belongs
    // to the monitor -- it is the operator's command line, or a script being piped
    // in -- and it is in blocking mode. A board that peeked at it there would either
    // steal the next command or block the whole simulator until somebody pressed
    // Enter, and "the emulator hangs when you STEP a program that reads the console"
    // is a bug I would rather not write twice.
    //
    // enterRaw() is what makes stdin ours (and non-blocking). Until then the
    // keyboard is quiet, and injected bytes are the only input there is -- which is
    // exactly right for a test and for MCP.
    if (!taken_) return false;

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
// out. So the check happens before the character is even put in the buffer.
// ---------------------------------------------------------------------------
void Console::poll() const {
    uint8_t b = 0;
    while (pollByte(b)) {
        if (b == attn_) {
            attnSeen_ = true;  // never buffered: the guest is not offered it, ever
            continue;
        }
        if (in_.size() >= kMaxIn) {
            // Typing faster than the guest can read. A real terminal drops it too --
            // but a DROPPED KEYSTROKE MUST BE COUNTED, or it becomes a bug report
            // about "flaky input" that nobody can reproduce.
            const_cast<Console*>(this)->dropped_++;
            continue;
        }
        in_.push_back(b);
    }
}

bool Console::readable() const {
    poll();
    return !in_.empty();
}

size_t Console::read(uint8_t* buf, size_t n) {
    if (!n) return 0;
    poll();
    if (in_.empty()) return 0;

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

size_t Console::write(const uint8_t* buf, size_t n) {
    ssize_t w = ::write(STDOUT_FILENO, buf, n);
    if (w > 0) written_ += (uint64_t)w;
    return w > 0 ? (size_t)w : 0;
}

void Console::flush() { ::fflush(stdout); }

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
    return p;
}

} // namespace altair
