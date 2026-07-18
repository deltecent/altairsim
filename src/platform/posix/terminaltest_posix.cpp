// THE TERMINAL LAYER, ON A REAL TTY.
//
// The PIPE path of terminal.h is proven in CI every build: acceptance-basic4k/8k feed a
// live guest keystrokes on a redirected stdin, so readInput()'s never-waiting contract
// and its closed-pipe EOF (which stops the run with InputEnded) all run there. What a
// piped test cannot reach is the TTY path -- a real terminal, which a pipe by definition
// is not -- and until this file existed nothing reached it: the POSIX terminal was
// "proven by hand against a pty", which is to say proven on the days somebody remembered
// to. Issue #25 is what that costs.
//
// WHAT WENT WRONG, and what this therefore asserts. Guest mode sets VMIN=0/VTIME=0, so
// a poll of the keyboard returns at once instead of waiting for a key. On macOS/BSD an
// empty read on THAT tty returns 0 -- the same number a hung-up pipe returns, and
// readInput() reported both as the end of input. So the first console poll of any RUN on
// a terminal latched Console::eof_, and starved() -- empty AND ENDED -- began counting on
// a keyboard that had not ended and never will. It failed for a user in a terminal and
// passed for us and for CI, every time, because we both run the suite on a pipe.
//
// r == 0 IS TWO DIFFERENT ANSWERS, and which one it is depends on what stdin is. Both
// halves are asserted below, side by side, because it is the DIFFERENCE between them
// that is the contract.
//
// WHY IT LIVES IN src/platform/posix/ AND NOT tests/. It opens a pty and drives termios
// directly; those are OS calls, and DESIGN.md 2.1 keeps OS calls in the platform layer.
// A test of OS-specific code is OS-specific code, so it sits beside the file it tests --
// the same reasoning, and the same shape, as terminaltest_win32.cpp.
//
// SKIPS (exit 77, ctest's SKIP_RETURN_CODE) when no pty can be opened -- a container with
// no /dev/pts, a sandbox, some CI runners -- because a tty test that passed with no tty
// would be the green tick that lies.

#include "platform/terminal.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#else
#include <termios.h>
extern "C" int openpty(int*, int*, char*, struct termios*, struct winsize*);
#endif

using namespace altair::platform;

static int g_fail = 0, g_run = 0;
#define CHECK(cond, msg)                                          \
    do {                                                          \
        ++g_run;                                                  \
        bool ok_ = (cond);                                        \
        if (!ok_) ++g_fail;                                       \
        std::printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", (msg)); \
    } while (0)

constexpr int kSkip = 77;  // agrees with SKIP_RETURN_CODE in CMakeLists.txt

// Read with a few attempts. A byte written to the master end has to cross the tty line
// discipline before the slave end can see it, and that is not instantaneous -- but it is
// not slow either, so a bounded retry is honest where a sleep would be a guess.
static size_t readTries(uint8_t* buf, size_t n, bool& eof, int tries) {
    for (int i = 0; i < tries; ++i) {
        size_t got = readInput(buf, n, eof);
        if (got || eof) return got;
        usleep(2000);
    }
    return 0;
}

int main() {
    int master = -1, slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
        std::printf("no pty available -- SKIPPING the POSIX terminal test\n");
        return kSkip;
    }

    int savedStdin = dup(STDIN_FILENO);
    dup2(slave, STDIN_FILENO);

    // ---- A TTY. Empty is NOT ended. ----
    {
        bool raw = enterTermMode(TermMode::Guest);
        CHECK(stdinIsTty(), "the pty IS a terminal, which is the whole premise");
        CHECK(raw, "enterTermMode(Guest) takes a real terminal");

        uint8_t buf[64];
        bool    eof = true;  // start wrong, so a call that never writes it is caught
        size_t  n   = readInput(buf, sizeof buf, eof);
        CHECK(n == 0, "nobody has typed, so there is nothing to read");
        CHECK(!eof, "AND THAT IS NOT THE END OF INPUT -- issue #25, and the whole point");

        // Poll it hard, the way a guest at a prompt does. Not one of those may end it.
        bool everEof = false;
        for (int i = 0; i < 200; ++i) {
            readInput(buf, sizeof buf, eof);
            if (eof) everEof = true;
        }
        CHECK(!everEof, "a terminal does not end, however long the guest sits on it");

        // And it is a live keyboard, not merely a quiet one: a key still arrives.
        const char key = 'K';
        CHECK(write(master, &key, 1) == 1, "type a key at the other end");
        eof = true;
        n   = readTries(buf, sizeof buf, eof, 100);
        CHECK(n == 1 && buf[0] == 'K', "and the guest reads it");
        CHECK(!eof, "a key is not an ending either");

        restoreTerm();
    }

    // ---- A PIPE. Empty AND closed IS ended. The other half of the contract. ----
    {
        int fds[2];
        if (pipe(fds) != 0) {
            std::printf("  [FAIL] could not make a pipe\n");
            ++g_fail;
            ++g_run;
        } else {
            dup2(fds[0], STDIN_FILENO);
            close(fds[0]);
            close(fds[1]);  // the writer hangs up at once

            enterTermMode(TermMode::Guest);
            CHECK(!stdinIsTty(), "a pipe is not a terminal");

            uint8_t buf[64];
            bool    eof = false;
            size_t  n   = readInput(buf, sizeof buf, eof);
            CHECK(n == 0, "a closed pipe has nothing to give");
            CHECK(eof, "AND IT HAS ENDED -- this is what a scripted CONSOLE leaves on");

            restoreTerm();
        }
    }

    dup2(savedStdin, STDIN_FILENO);
    close(savedStdin);
    close(slave);
    close(master);

    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
