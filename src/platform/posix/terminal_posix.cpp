// POSIX terminal -- termios raw mode, restored on the way out however we leave.
// See src/platform/terminal.h.
//
// This file is ALLOWED to know it is on a POSIX system. Nothing else in the tree is
// (DESIGN.md 2.1), which is why there is not one conditional in it: there is nothing
// to be conditional ABOUT. The file simply IS the POSIX answer.

#include "platform/terminal.h"

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace altair::platform {
namespace {

termios g_saved{};
int     g_savedFl  = 0;
bool    g_haveTerm = false;  // we changed the tty and owe it back
bool    g_haveFl   = false;  // we changed stdin's flags and owe them back

// ---------------------------------------------------------------------------
// RESTORE ON SIGNAL.
//
// A C++ destructor does not run when a signal kills the process, and both of the ways
// out of this program that are not `EXIT` are signals: the operator's Ctrl-C at the
// monitor prompt (LineEdit mode leaves signals ON, deliberately), and whatever the
// window manager or the shell sends when a terminal window is closed.
//
// Without this, either one leaves the tty with ECHO off -- you get your shell back and
// it is silent, and you have to type `stty sane` at a prompt you cannot see. That is
// not a small annoyance; it is the difference between a tool people keep and one they
// uninstall.
//
// tcsetattr(), fcntl() and raise() are all on the POSIX async-signal-safe list, so this
// handler is doing nothing it is not allowed to do. It restores, puts the signal back
// to its default behaviour, and re-raises -- so the process still dies exactly as it
// would have, with the right exit status, and nobody upstream can tell we were here.
// ---------------------------------------------------------------------------
extern "C" void onFatalSignal(int sig) {
    restoreTerm();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

// IF IT WAS IGNORED, LEAVE IT IGNORED. This is the POSIX idiom and it is not optional:
// a process started in the BACKGROUND, or under `nohup`, inherits SIGINT and SIGQUIT
// already set to SIG_IGN -- and SIG_IGN survives exec, precisely so that a ^C in the
// terminal cannot reach into a job that was deliberately detached from it.
//
// Installing a handler unconditionally would quietly override that, and
// `nohup altairsim -s longboot.cmd &` would start dying on a keystroke meant for
// something else. Found the moment this was tested on a pty, because the test harness
// happened to launch the simulator as a background job -- which is the only reason the
// old build and the new one behaved differently, and it took some untangling.
void armSignalHandlers() {
    static bool armed = false;
    if (armed) return;
    armed = true;
    static const int kFatal[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    for (int sig : kFatal) {
        if (std::signal(sig, onFatalSignal) == SIG_IGN) std::signal(sig, SIG_IGN);
    }
}

} // namespace

bool stdinIsTty() { return isatty(STDIN_FILENO) != 0; }
bool stdoutIsTty() { return isatty(STDOUT_FILENO) != 0; }

bool enterTermMode(TermMode m) {
    armSignalHandlers();

    // O_NONBLOCK GOES ON WHETHER OR NOT THIS IS A TERMINAL, and that is the whole
    // reason CONSOLE mode can be scripted at all.
    //
    // A tty in raw mode with VMIN=0 already returns immediately. A PIPE does not: a
    // blocking read on a pipe with no data yet BLOCKS, and it would stop emulated time
    // dead in the middle of a scripted run. With O_NONBLOCK the read has three
    // distinguishable answers instead of two -- a byte, EAGAIN (nothing yet), and 0
    // (end of input, forever) -- and it is that third one that lets a scripted CONSOLE
    // know when to give the monitor its terminal back.
    //
    // The line editor does NOT want this: it is waiting for a human, and waiting is the
    // correct thing for it to be doing.
    if (m == TermMode::Guest && !g_haveFl) {
        int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (fl >= 0) {
            g_savedFl = fl;
            g_haveFl  = true;
            fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
        }
    }

    if (!isatty(STDIN_FILENO)) return false;

    // SAVE ONLY ONCE. If a second mode is entered over the top of a first, what we owe
    // the operator is still the state their SHELL handed us -- not some intermediate
    // raw mode of our own making.
    termios cur{};
    if (tcgetattr(STDIN_FILENO, &cur) != 0) return false;
    if (!g_haveTerm) {
        g_saved    = cur;
        g_haveTerm = true;
    }

    termios raw = g_saved;

    if (m == TermMode::LineEdit) {
        // The MONITOR edits the line, so the driver must not. But ISIG STAYS ON: at a
        // monitor prompt, Ctrl-C is a way out, not a character -- and it is the signal
        // handler above that makes leaving that way survivable.
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 1;  // wait for exactly one byte
        raw.c_cc[VTIME] = 0;
    } else {
        // The GUEST gets EVERYTHING. No canonical line editing, no echo (the guest
        // echoes, or does not, and that is its business), no IXON (Ctrl-S is CP/M's
        // byte, not the driver's), no output post-processing -- and, the important one,
        // NO ISIG: Ctrl-C is a byte a CP/M program is entitled to read, not a signal.
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | INLCR | ISTRIP);
        raw.c_oflag &= (tcflag_t) ~(OPOST);
        raw.c_cc[VMIN]  = 0;  // never wait
        raw.c_cc[VTIME] = 0;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    return true;
}

void restoreTerm() {
    if (g_haveFl) {
        fcntl(STDIN_FILENO, F_SETFL, g_savedFl);
        g_haveFl = false;
    }
    if (g_haveTerm) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
        g_haveTerm = false;
    }
}

size_t readInput(uint8_t* buf, size_t n, bool& eof) {
    eof = false;
    ssize_t r = ::read(STDIN_FILENO, buf, n);
    if (r > 0) return (size_t)r;
    if (r == 0) {  // a CLOSED PIPE. A terminal never says this.
        eof = true;
        return 0;
    }
    return 0;  // EAGAIN: a quiet line, not an error, and certainly not an ending
}

int readInputBlocking() {
    unsigned char c = 0;
    return ::read(STDIN_FILENO, &c, 1) == 1 ? (int)c : -1;
}

size_t writeOutput(const uint8_t* buf, size_t n) {
    ssize_t w = ::write(STDOUT_FILENO, buf, n);
    return w > 0 ? (size_t)w : 0;
}

void flushOutput() { std::fflush(stdout); }

} // namespace altair::platform
