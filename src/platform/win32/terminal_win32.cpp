// Win32 terminal -- SetConsoleMode, ENABLE_VIRTUAL_TERMINAL_*. See platform/terminal.h.
//
// ---------------------------------------------------------------------------
// PROVED ON A REAL CONSOLE, 2026-07-15. Written on macOS with no compiler or keyboard
// near it, now exercised both ways on native Windows.
//
// The PIPE path runs in CI every build: acceptance-basic4k/8k feed a live guest
// keystrokes on a redirected stdin, so readInput()'s PeekNamedPipe branch, its
// never-waiting contract and its broken-pipe EOF (which stops the run with InputEnded)
// are all covered.
//
// The CONSOLE path -- which a piped test cannot reach -- is proved by
// src/platform/win32/terminaltest_win32.cpp (`ctest -L hw`, 15 checks): it takes a real
// console and drives enterTermMode() through BOTH modes, checking that Guest clears line
// input, echo and PROCESSED_INPUT (so Ctrl-C is a byte, not a signal) while LineEdit
// leaves PROCESSED_INPUT as it found it (so Ctrl-C still signals a way out of the
// prompt), that ENABLE_VIRTUAL_TERMINAL_INPUT is set (the arrow-key path #2 below), and
// that restoreTerm() gives the mode back exactly, idempotently.
//
//   1. readInput()'s console path peeks the input queue and discards the records that
//      are not characters (key-UP events, window resizes, mouse), because ReadFile on a
//      console BLOCKS until a character arrives and the peek is the only thing standing
//      between us and that. This one is verified BY HAND, not in CI: proving it means
//      manufacturing keystrokes with WriteConsoleInput and reading them back, and that
//      round trip is racy against a console input buffer shared with the parent shell --
//      a flaky hardware test is a lying one. In practice a broken peek loop hangs the
//      monitor the first time you type, so it does not hide. If keystrokes go missing or
//      input hangs, this is still the first code to read.
//
//   2. ENABLE_VIRTUAL_TERMINAL_INPUT is what turns the arrow keys into the ESC [ A
//      sequences that cli/lineedit.cpp already knows how to read. On a Windows old
//      enough not to have it, SetConsoleMode fails, and the right answer is to fall
//      back rather than to grow a conditional -- see the retry below. The test confirms
//      the flag is set on a modern console.
//
// docs/porting-notes.md records the same. Everything here was right as written.
// ---------------------------------------------------------------------------

#include "platform/terminal.h"

#include <windows.h>

#include <algorithm>
#include <csignal>
#include <cstdio>

namespace altair::platform {
namespace {

HANDLE hIn() { return GetStdHandle(STD_INPUT_HANDLE); }
HANDLE hOut() { return GetStdHandle(STD_OUTPUT_HANDLE); }

DWORD g_savedIn   = 0;
DWORD g_savedOut  = 0;
bool  g_haveIn    = false;
bool  g_haveOut   = false;

// See the POSIX file for why this exists at all: a destructor does not run when a
// signal kills the process, and a simulator that leaves the console with echo off is a
// simulator you run once. Win32's C runtime gives us the same signals, in name.
extern "C" void onFatalSignal(int sig) {
    restoreTerm();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

// IF IT WAS IGNORED, LEAVE IT IGNORED -- see the POSIX file, where a pty test caught
// this: installing a handler over an inherited SIG_IGN takes a detached job's immunity
// away from it.
void armSignalHandlers() {
    static bool armed = false;
    if (armed) return;
    armed = true;
    // SIGBREAK is Win32's Ctrl-Break, and it is the closest thing here to SIGHUP.
    static const int kFatal[] = {SIGINT, SIGTERM, SIGBREAK};
    for (int sig : kFatal) {
        if (std::signal(sig, onFatalSignal) == SIG_IGN) std::signal(sig, SIG_IGN);
    }
}

bool isConsole(HANDLE h) {
    DWORD m = 0;
    return GetConsoleMode(h, &m) != 0;
}

} // namespace

bool stdinIsTty() { return isConsole(hIn()); }
bool stdoutIsTty() { return isConsole(hOut()); }

bool enterTermMode(TermMode m) {
    armSignalHandlers();

    // NOTHING TO DO FOR A PIPE. Unlike POSIX -- where the guest's never-waiting read
    // needs O_NONBLOCK put on the descriptor first -- readInput() below asks the pipe
    // how many bytes are there (PeekNamedPipe) before it reads any, so a pipe is
    // already non-blocking by construction and there is no flag to set. Same contract,
    // different mechanism: which is the entire reason this is a separate file and not
    // an #ifdef in a shared one.
    if (!isConsole(hIn())) return false;

    DWORD in = 0;
    if (!GetConsoleMode(hIn(), &in)) return false;
    if (!g_haveIn) {
        g_savedIn = in;
        g_haveIn  = true;
    }

    DWORD out = 0;
    if (GetConsoleMode(hOut(), &out) && !g_haveOut) {
        g_savedOut = out;
        g_haveOut  = true;
        // The line editor writes ANSI: \r, the line, ESC[K, ESC[nD. That is not a
        // Windows-specific code path in lineedit.cpp -- it is the ONE code path, and
        // this is the flag that makes Windows honour it.
        SetConsoleMode(hOut(), out | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    DWORD raw = g_savedIn;

    // ENABLE_LINE_INPUT is ICANON. ENABLE_ECHO_INPUT is ECHO. Both go, in both modes:
    // the monitor draws its own line, and the guest echoes or does not as it pleases.
    raw &= (DWORD) ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

    if (m == TermMode::Guest) {
        // ENABLE_PROCESSED_INPUT is ISIG: it is what turns Ctrl-C into a signal instead
        // of a byte. The guest is entitled to that byte -- a CP/M program reads Ctrl-C
        // to abort -- so it goes. In LineEdit mode it STAYS, because at a monitor prompt
        // Ctrl-C is a way out, and the signal handler above is what makes it a safe one.
        raw &= (DWORD)~ENABLE_PROCESSED_INPUT;
    }

    // The arrow keys, as the ESC sequences lineedit.cpp already parses.
    if (!SetConsoleMode(hIn(), raw | ENABLE_VIRTUAL_TERMINAL_INPUT)) {
        // Too old for VT input. Take what we can get: no echo and no line discipline
        // still gives a usable editor, minus the arrows.
        if (!SetConsoleMode(hIn(), raw)) return false;
    }
    return true;
}

void restoreTerm() {
    if (g_haveIn) {
        SetConsoleMode(hIn(), g_savedIn);
        g_haveIn = false;
    }
    if (g_haveOut) {
        SetConsoleMode(hOut(), g_savedOut);
        g_haveOut = false;
    }
}

size_t readInput(uint8_t* buf, size_t n, bool& eof) {
    eof = false;
    if (!n) return 0;
    HANDLE h = hIn();

    if (isConsole(h)) {
        // ReadFile on a console BLOCKS until a character arrives, and "never waits" is
        // the contract. So: look at the head of the input queue, throw away everything
        // that is not a character a human just typed -- key-UP events, window resizes,
        // focus changes, the mouse -- and only call ReadFile once we KNOW it will find
        // something and return at once.
        for (;;) {
            INPUT_RECORD rec;
            DWORD        got = 0;
            if (!PeekConsoleInputA(h, &rec, 1, &got) || got == 0) return 0;  // quiet
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown &&
                rec.Event.KeyEvent.uChar.AsciiChar != 0)
                break;  // a real character is waiting: ReadFile will not block
            if (!ReadConsoleInputA(h, &rec, 1, &got)) return 0;  // discard it, look again
        }
        DWORD got = 0;
        if (!ReadFile(h, buf, (DWORD)n, &got, nullptr)) return 0;
        return got;
    }

    if (GetFileType(h) == FILE_TYPE_PIPE) {
        // ASK BEFORE READING, for the same reason as the console: a read of a pipe with
        // nothing in it waits, and waiting stops emulated time.
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
            // The WRITER HAS GONE. This -- and only this -- is end of input: the third
            // answer, the one a terminal never gives. See terminal.h.
            if (GetLastError() == ERROR_BROKEN_PIPE) eof = true;
            return 0;
        }
        if (avail == 0) return 0;  // a quiet line, not an ending
        n = std::min<size_t>(n, avail);
    }

    // A pipe with bytes ready, or a plain file -- neither can block now.
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)n, &got, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) eof = true;
        return 0;
    }
    if (got == 0) eof = true;  // a file, ended
    return got;
}

int readInputBlocking() {
    uint8_t c   = 0;
    DWORD   got = 0;
    if (!ReadFile(hIn(), &c, 1, &got, nullptr) || got != 1) return -1;
    return c;
}

size_t writeOutput(const uint8_t* buf, size_t n) {
    DWORD put = 0;
    if (!WriteFile(hOut(), buf, (DWORD)n, &put, nullptr)) return 0;
    return put;
}

void flushOutput() { fflush(stdout); }

} // namespace altair::platform
