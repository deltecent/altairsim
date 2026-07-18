// THE TERMINAL LAYER, ON A REAL CONSOLE.
//
// The PIPE path of terminal.h is proven in CI every build: acceptance-basic4k/8k feed a
// live guest keystrokes on a redirected stdin, so readInput()'s PeekNamedPipe branch,
// its never-waiting contract and its broken-pipe EOF (which stops the run with
// InputEnded) all run there. What a piped test cannot reach is the CONSOLE path -- a
// real interactive terminal, which a pipe by definition is not -- and this proves it:
// enterTermMode() reconfiguring a real console into the two raw modes the header
// describes, and restoreTerm() giving it back byte-for-byte.
//
// WHY IT LIVES IN src/platform/win32/ AND NOT tests/. It reads and asserts on
// SetConsoleMode / GetConsoleMode, which are Win32 calls, and DESIGN.md 2.1 keeps OS
// calls in the platform layer. A test of OS-specific code is OS-specific code, so it
// sits beside the file it tests. The POSIX terminal has its own mirror of this file,
// src/platform/posix/terminaltest_posix.cpp, for the same reason.
//
// WHAT IT DELIBERATELY LEAVES TO A HUMAN. terminal_win32.cpp's #1 flagged risk is
// readInput()'s peek-and-discard loop over the console INPUT queue. Proving that with no
// human means manufacturing keystrokes with WriteConsoleInput and reading them back --
// and that round trip is irreducibly racy here: the console input buffer is SHARED with
// the parent shell across processes, and under ENABLE_VIRTUAL_TERMINAL_INPUT the console
// re-encodes input, so an assertion on the queue passes some runs and fails others. A
// hardware test that lies green half the time is exactly what serialtest.cpp warns
// against, so it is NOT committed. The loop was verified by hand (feed a key-up + a
// resize, confirm readInput returns 0 without blocking, then a real key comes through)
// and, in practice, a broken peek loop hangs the monitor the first time you type -- it
// does not hide. The DETERMINISTIC half, the mode reconfiguration, is what runs here.
//
// SKIPS (exit 77, ctest's SKIP_RETURN_CODE) when there is no console to take -- a
// service, a detached job, some CI runners -- because a console test that passed with no
// console would be the green tick that lies.

#include "platform/terminal.h"

#include <windows.h>

#include <cstdio>

using namespace altair::platform;

static int g_fail = 0, g_run = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_run;                                                             \
        bool ok_ = (cond);                                                   \
        if (!ok_) ++g_fail;                                                  \
        std::printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", (msg));            \
    } while (0)

constexpr int kSkip = 77;  // agrees with SKIP_RETURN_CODE in CMakeLists.txt

int main() {
    // Get a real console to drive. When stdin is redirected (a pipe, as under ctest)
    // stdinIsTty() is false even though a console is attached, so open CONIN$/CONOUT$
    // directly -- that succeeds whenever ANY console exists -- and point the std handles
    // at it, which is what terminal_win32.cpp reads through GetStdHandle. Only if there
    // is no console at all do we make one; if even that fails, we SKIP.
    if (!stdinIsTty()) {
        HANDLE cin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
        if (cin == INVALID_HANDLE_VALUE) {
            if (!AllocConsole()) {
                std::printf("SKIPPED: no console, and AllocConsole failed (err %lu).\n"
                            "  This test reconfigures a real console; run it from a\n"
                            "  terminal, not a service or a fully detached job.\n",
                            GetLastError());
                return kSkip;
            }
            cin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
        }
        HANDLE cout = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (cin != INVALID_HANDLE_VALUE) SetStdHandle(STD_INPUT_HANDLE, cin);
        if (cout != INVALID_HANDLE_VALUE) SetStdHandle(STD_OUTPUT_HANDLE, cout);
    }

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    CHECK(stdinIsTty(), "stdinIsTty() true on a real console");
    CHECK(stdoutIsTty(), "stdoutIsTty() true on a real console");

    DWORD modeBefore = 0;
    CHECK(GetConsoleMode(hIn, &modeBefore) != 0, "read the console's original mode");

    // ---- Guest mode: the guest owns the keyboard, signals included ----
    CHECK(enterTermMode(TermMode::Guest),
          "enterTermMode(Guest) reconfigured a real console (returned true)");

    DWORD guest = 0;
    GetConsoleMode(hIn, &guest);
    CHECK((guest & ENABLE_LINE_INPUT) == 0, "Guest: ENABLE_LINE_INPUT cleared (ICANON off)");
    CHECK((guest & ENABLE_ECHO_INPUT) == 0, "Guest: ENABLE_ECHO_INPUT cleared (no echo)");
    CHECK((guest & ENABLE_PROCESSED_INPUT) == 0,
          "Guest: ENABLE_PROCESSED_INPUT cleared -> Ctrl-C is a BYTE the guest reads, not a signal");
    CHECK((guest & ENABLE_VIRTUAL_TERMINAL_INPUT) != 0,
          "Guest: ENABLE_VIRTUAL_TERMINAL_INPUT set -> arrow keys become ESC[ sequences");

    restoreTerm();
    DWORD afterGuest = 0;
    GetConsoleMode(hIn, &afterGuest);
    CHECK(afterGuest == modeBefore, "restoreTerm() after Guest restored the mode exactly");

    // ---- LineEdit mode: the monitor's editor -- raw, but SIGNALS ARE LEFT ALONE ----
    // This is the difference that matters between the two modes and the one thing a
    // mode-only test can prove that the guest path cannot: at a monitor prompt Ctrl-C is
    // a way out, so LineEdit must NOT clear ENABLE_PROCESSED_INPUT the way Guest does.
    //
    // The invariant is RELATIVE, not absolute: LineEdit leaves the signal bit exactly as
    // it found it. Asserting it is simply "set" would be wrong -- a parent shell can hand
    // us a console that already has it clear (PowerShell's PSReadLine does exactly that),
    // and honouring that is the point. Guest clears it, LineEdit does not touch it.
    CHECK(enterTermMode(TermMode::LineEdit),
          "enterTermMode(LineEdit) reconfigured the console (returned true)");

    DWORD edit = 0;
    GetConsoleMode(hIn, &edit);
    CHECK((edit & ENABLE_LINE_INPUT) == 0, "LineEdit: ENABLE_LINE_INPUT cleared (we draw the line)");
    CHECK((edit & ENABLE_ECHO_INPUT) == 0, "LineEdit: ENABLE_ECHO_INPUT cleared (we echo it)");
    CHECK((edit & ENABLE_PROCESSED_INPUT) == (modeBefore & ENABLE_PROCESSED_INPUT),
          "LineEdit: ENABLE_PROCESSED_INPUT LEFT AS FOUND -> Ctrl-C still signals (Guest clears it, LineEdit does not)");

    restoreTerm();
    DWORD afterEdit = 0;
    GetConsoleMode(hIn, &afterEdit);
    CHECK(afterEdit == modeBefore, "restoreTerm() after LineEdit restored the mode exactly");

    // restoreTerm is idempotent and safe when nothing is held (terminal.h) -- calling it
    // once more must not throw or change the already-restored mode.
    restoreTerm();
    DWORD twice = 0;
    GetConsoleMode(hIn, &twice);
    CHECK(twice == modeBefore, "a second restoreTerm() is a harmless no-op");

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
