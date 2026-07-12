#pragma once
//
// The host terminal -- raw mode, and the bytes that come through it (DESIGN.md 2.1).
//
// This is the third and last thing in the project that had an OS underneath it. The
// serial port and the socket moved into src/platform/ when they were built; the
// terminal was already here, and it had been sitting in src/cli/lineedit.cpp and
// src/host/console.cpp since the beginning, with a preprocessor conditional in one of
// them and a bare #include <termios.h> in the other.
//
// THE CONTRACT (2.1): pure declarations, zero conditionals, and NO OS TYPE IN ANY
// SIGNATURE -- no termios, no HANDLE, no file descriptor. A caller of this header
// cannot tell what it is running on, and that is the whole point: the OS layer is only
// sealed if the seal is airtight.
//
// ---------------------------------------------------------------------------
// THE TERMINAL HAS EXACTLY TWO CLAIMANTS, AND THEY WANT OPPOSITE THINGS FROM IT.
//
// The MONITOR wants a line editor: no echo and no line discipline, because it draws
// and edits the line itself -- but Ctrl-C must still SIGNAL, because at a monitor
// prompt Ctrl-C is a way out, not data.
//
// The GUEST wants everything: Ctrl-C is a byte a CP/M program is entitled to read, and
// so is Ctrl-S, and so is every other control character the driver would otherwise have
// an opinion about. And its reads may NEVER WAIT, because a blocking read stops
// emulated time dead.
//
// Two modes, then, and no third. Anything that thinks it needs a third should look
// again at which of these two it actually is.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace altair::platform {

// Is there a human out there at all? False under a pipe, a script, and the test suite,
// and the answer changes what CONSOLE mode and the line editor are permitted to do --
// a scripted run that waited for somebody to press a key would wait forever.
bool stdinIsTty();
bool stdoutIsTty();

enum class TermMode {
    // The MONITOR's line editor (src/cli/lineedit.h says why we edit the line ourselves
    // rather than let the driver do it). No line discipline, no echo -- but SIGNALS
    // STAY ON. A read WAITS: there is nothing else for the monitor to be doing while
    // the operator types.
    LineEdit,

    // The GUEST owns the keyboard. No line discipline, no echo, no signals, no XON/XOFF
    // (flow control is the guest's protocol, not ours), no output post-processing. And
    // reads NEVER WAIT.
    Guest,
};

// Take the terminal. Returns true if a TERMINAL was actually reconfigured.
//
// FALSE IS NOT A FAILURE. It means stdin is a pipe, a file, or a script -- there is no
// terminal state to change, and none needs changing. Guest mode still arranges for
// reads not to wait (a pipe with no data yet WOULD block, and that is the case that
// matters), so `altairsim -s script.cmd` drives a guest with no tty at all.
//
// NESTING IS THE CALLER'S BUSINESS -- this is a set, not a stack. But what gets saved
// is the state the terminal was in before ANY of this started, so restoreTerm() always
// gives back what the operator's shell handed us, never some intermediate raw mode.
bool enterTermMode(TermMode m);

// Give the terminal back exactly as it was found. Idempotent, and safe to call when
// nothing was ever taken.
//
// IT ALSO RUNS IF WE ARE KILLED. The implementation restores on SIGINT, SIGTERM,
// SIGHUP and SIGQUIT before letting the signal do its work -- because a simulator that
// leaves your shell with no echo when it dies is a simulator you run exactly once, and
// C++ destructors do not run when a signal terminates the process.
void restoreTerm();

// ---------------------------------------------------------------------------
// The bytes.
// ---------------------------------------------------------------------------

// Read what is there, NEVER waiting. THREE ANSWERS, and a correct caller tells all
// three apart:
//
//     returns > 0             bytes -- this many of them
//     returns 0, eof false    NOTHING RIGHT NOW. A quiet line. Ask again later.
//     returns 0, eof true     THE INPUT HAS ENDED, and there will never be another
//                             byte. Only a pipe ever says this. A terminal never does.
//
// Conflating the last two is the classic bug, and it breaks in both directions: read
// EOF as "quiet" and a scripted run never finishes, read "quiet" as EOF and it stops
// the instant the writer pauses to think.
size_t readInput(uint8_t* buf, size_t n, bool& eof);

// Read ONE byte, WAITING for it. -1 at end of input. This is the line editor's read,
// and waiting is exactly what it wants.
int readInputBlocking();

// Straight to the handle, past every buffer the C++ library owns -- a guest that prints
// a prompt with no newline after it must still see the prompt appear.
size_t writeOutput(const uint8_t* buf, size_t n);
void   flushOutput();

} // namespace altair::platform
