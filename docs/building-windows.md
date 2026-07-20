# Building `altairsim` on Windows

**Status (2026-07-15): builds, passes the full test suite, and the win32 platform
layer is field-proven with MSVC.** The `src/platform/win32/` layer was written on
macOS and had never been through a compiler until GitHub Actions CI was added. It now
**builds and links cleanly** on `windows-latest` (MSVC, Winsock via `ws2_32`), **all
registered tests pass** on native Windows, and the serial, socket and terminal
implementations are each proved against the real world by a `ctest -L hw` leg — see §5.

The `0xC0000409` fast-fail that the `unit` aggregate used to hit was a **test**
bug, not a win32-layer one: `test_media.cpp` deleted a temp file while a live
`HostFile` still held it open (POSIX unlinks an open file; Windows refuses and
`fs::remove` threw an uncaught `filesystem_error`), and it also stripped only
`owner_write` to simulate a read-only file, which the C++ filesystem maps to the
Windows read-only attribute *only when every write bit is clear*. Both were fixed
in the test — no `#ifdef` — so the teardown unmounts before it deletes and clears
all the write bits. §6 still describes the fast-loop for the next win32 issue.

There are **no third-party dependencies** — the binary links only against the
system C/C++ runtime and `ws2_32` (Winsock).

---

## 1. Prerequisites

**Yes, the free edition works.** You have two free choices for the MSVC C++20
compiler:

| Option | What it is | Free? |
|---|---|---|
| **Build Tools for Visual Studio 2022** | Command-line MSVC toolchain, **no IDE** — all you need to build from CMake | Free, no conditions |
| **Visual Studio Community 2022** | Full IDE + the same MSVC compiler | Free for individuals, open source, and small orgs |

Either one, from <https://visualstudio.microsoft.com/downloads/> (Community is on
the main page; Build Tools is lower down under "Tools for Visual Studio"). Pick
Build Tools if you just want to compile; pick Community if you also want the IDE
and its debugger (handy for §6).

In the installer, check the **"Desktop development with C++"** workload. That one
box installs everything needed:

- **MSVC v143** — the x64/x86 C++ compiler (C++20).
- **Windows 11 SDK** (or 10 — either is fine).
- **C++ CMake tools for Windows** — this bundles **CMake** *and* **Ninja**, so you
  usually do **not** need a separate CMake install.

Install **git** too if you don't have it — the installer offers "Git for Windows"
as an individual component, or get it from <https://git-scm.com/download/win>.
Optionally install the **GitHub CLI** (`winget install GitHub.cli`) for `gh`.

| Need | Minimum | Comes from |
|---|---|---|
| C++20 compiler | MSVC v143 (VS 2022) | Desktop development with C++ |
| CMake | ≥ 3.20 | bundled with that workload |
| git | any | Git for Windows |

---

## 2. Build

**A plain PowerShell is enough — with the default generator.** The Visual Studio
generator invokes MSBuild, which locates the toolchain itself, so `cl.exe` does not
need to be on your `PATH`. CI is the proof: the Windows leg configures with
`shell: bash`, sets up no VS environment at all, and is a required check.

**Where you DO need a *Developer* shell is Ninja** (and NMake), below: those invoke
`cl.exe` directly, and it plus its `INCLUDE`/`LIB` paths only exist inside the VS
environment. From the Start menu open **"Developer PowerShell for VS 2022"** (or
**"x64 Native Tools Command Prompt for VS 2022"**).

This distinction matters more than it looks: it is what lets the build run
unattended — in a script, or under an assistant — without a shell that has to be
launched a particular way.

```powershell
git clone https://github.com/deltecent/altairsim
cd altairsim
cmake -S . -B build
cmake --build build --config Release --target altairsim
```

The default generator on Windows is the **Visual Studio** generator, which is
*multi-config*: you do **not** pass `-DCMAKE_BUILD_TYPE`; you choose the config at
build time with `--config Release`. The result is **`build\Release\altairsim.exe`**.
This matches exactly what CI does.

> **Prefer a single-config, faster build?** Use Ninja (it ships with the C++ CMake
> tools), which behaves like the Linux/macOS build — `CMAKE_BUILD_TYPE` at
> configure time, no `Release\` subfolder. **This is the path that needs a Developer
> shell**, because Ninja runs `cl.exe` directly:
> ```powershell
> cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
> cmake --build build --target altairsim
> ```
> The binary is then `build\altairsim.exe`.

The build compiles with `/W4 /permissive-` and may print warnings, but there is no
`/WX`, so warnings never fail the build.

---

## 3. Smoke test

```powershell
build\Release\altairsim.exe --version        # prints "AltairSim 0.1.0-…"
build\Release\altairsim.exe --list           # lists built-in machines (4k, default, minidisk, ...)
build\Release\altairsim.exe -x "help" default   # boots the default machine, runs one command, exits
build\Release\altairsim.exe                  # interactive: front panel + monitor
```

`--list` succeeding confirms the embedded machines and ROMs load; `-x "help"
default` confirms a machine actually initializes.

---

## 4. Running the tests

```powershell
ctest --test-dir build -C Release -LE slow --output-on-failure
```

- **`expect` is not on Windows**, so the interactive acceptance tests
  (`acceptance-cli`, `acceptance-ps2-*`, `acceptance-2sio-echo`, the minidisk/DCDD
  ones) do not register — they self-skip at configure time. That is expected and
  does not fail the build.
- **The `-L hw` leg** (`serial-hw`, `socket-hw`, `terminal-hw`) touches the real
  world. `socket-hw` always runs; `serial-hw` self-skips without real ports
  (`ALTAIR_SERIAL_A/B`) and `terminal-hw` self-skips without a console. Run it
  explicitly — all three are described in §5:
  ```powershell
  $env:ALTAIR_SERIAL_A="COM4"; $env:ALTAIR_SERIAL_B="COM10"   # serial-hw only
  ctest --test-dir build -C Release -L hw --output-on-failure
  ```

Everything that registers passes. If a new win32 problem crashes the `unit`
aggregate with `0xC0000409`, §6 shows how to isolate it.

---

## 5. What is verified so far

- **Compiles and links** on `windows-latest` (MSVC, CI). The win32 serial, socket,
  and terminal implementations build; `CMakeLists.txt` links `ws2_32` for Winsock.
- **All registered tests pass** (the ones that register without `expect`/disk
  images), including the `unit` aggregate now that its teardown bug is fixed.
- **The whole win32 platform layer is field-proven (2026-07-15).** `ctest -L hw` is
  the real-world leg, and all three pass on native Windows:
  - **serial-hw** — two USB FTDI ports (`COM4 <-> COM10`) with a null modem, 25
    checks: the platform layer plus an in-machine 6850 doing real `/CTS` flow
    control and a latched `/DCD` carrier-drop interrupt.
  - **socket-hw** — the real kernel TCP stack, 11 checks: non-blocking connect
    (success, bytes both ways, hangup) and the **refused** connect that lands in
    `select()`'s except set, the flagged Winsock case.
  - **terminal-hw** — a real console, 15 checks: `enterTermMode` through both modes
    (Guest clears line-input/echo/`PROCESSED_INPUT`; LineEdit leaves `PROCESSED_INPUT`
    as found, so Ctrl-C still signals), `ENABLE_VIRTUAL_TERMINAL_INPUT` set, and an
    exact, idempotent `restoreTerm`. Skips (77) where no console can be taken. (The
    terminal *pipe* path — `readInput` + broken-pipe EOF — is covered separately by
    `acceptance-basic4k/8k` in the default suite; the peek-and-discard loop is verified
    by hand, being too racy against the shared console buffer to commit — see
    `docs/porting-notes.md`.)

  ```powershell
  $env:ALTAIR_SERIAL_A="COM4"; $env:ALTAIR_SERIAL_B="COM10"   # serial-hw only
  ctest --test-dir build -C Release -L hw --output-on-failure
  ```

---

## 6. NOT verified: building without a Developer shell, and MinGW

**This section is a job, not a description.** It states three things that are *believed*
true and have never been run on this machine, gives the exact commands to settle each,
and says how to get the answer back into the project. **If you are an assistant on the
Windows box, this is the work.**

Why it matters: an unattended build — a script, or an assistant — cannot rely on a shell
that has to be launched from the Start menu. And **an assistant's environment does not
survive between commands**: the working directory carries over, but environment variables
do not, so `call vcvars64.bat` in one command and `cmake` in the next starts clean. Each
approach below is a different way around that.

### First: set the session up correctly, or the answers will be wrong

**Use native Windows, not WSL.** The MSVC toolchain is a Windows-native tool and is not
visible from inside WSL. WSL is the better choice for a Linux toolchain and is the only one
that supports sandboxing, but it cannot build this the way we ship it.

**Launch from a PLAIN PowerShell, not a "Developer PowerShell for VS 2022."** Two reasons,
and the second is the one that bites:

1. Approach A below asks *"does this work without a Developer shell?"* Testing it from a
   Developer shell answers a different question and reports a false pass.
2. **It would not help anyway.** Claude Code does **not** inherit environment variables from
   the terminal that launched it — each command runs in a fresh process. Whatever
   `vcvars64.bat` set in your launching shell is gone by the time a build command runs. There
   is no "set it up once at the top" option.

**Know which shell you will actually get, because it changes the syntax below.** With Git for
Windows installed, Claude Code uses **Git Bash**; without it, **PowerShell**. There is also a
PowerShell tool rolling out progressively, which is the better fit for this project — it runs
Windows commands natively with no wrapper. To ask for it, before launching:

```json
// .claude/settings.json  (or settings.local.json)
{ "env": { "CLAUDE_CODE_USE_POWERSHELL_TOOL": "1" } }
```

**The commands in this section are written in PowerShell.** If you end up on Git Bash, they
still work but need wrapping — `cmd /c "…"` or `powershell -Command "…"` — and the quoting is
fussy: double-quote the whole Windows command and mind that backslashes survive. Say in your
report which shell you had, because it changes what the commands mean.

*(If you later want Ninja to work without chaining `vcvars` into every command, the supported
route is `CLAUDE_ENV_FILE` or a `SessionStart` hook to populate the environment per command —
not a specially-launched terminal. Out of scope for settling the three questions below.)*

### The three approaches to settle

**A. MSVC + Visual Studio generator — believed to need NO setup at all.**
Strongly evidenced but not confirmed interactively: CI's Windows leg configures with
`shell: bash`, no workflow contains a `vcvars` step, and it is a required check that
passes. Run this from a **plain** PowerShell — *not* a Developer one:

```powershell
cmake -S . -B build
cmake --build build --config Release --target altairsim
build\Release\altairsim.exe --version
```

**B. MSVC + Ninja — needs `vcvars`, chained into the same command.**
Ninja runs `cl.exe` directly, so the environment must exist *within* the one invocation.
Adjust the path to your VS edition:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-ninja --target altairsim"
```

**C. MinGW — needs only `PATH`, set once and persistently.**
`setx` writes the user `PATH` in the registry, so **new** shells inherit it (the current
one does not — open a fresh shell to test). **MinGW has never been built here at all**;
expect to correct this file.

```powershell
setx PATH "$env:PATH;C:\mingw64\bin"
# then, in a NEW shell:
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw --target altairsim
```

SDL3 for MinGW is upstream's `SDL3-devel-<ver>-mingw.tar.gz`, not vcpkg. Expect the one
known MSVC-ism to warn rather than fail: `src/platform/win32/socket_win32.cpp` has a
`#pragma comment(lib, "ws2_32.lib")` that GCC ignores and `-Wall` complains about. It is
redundant — CMake already links `ws2_32` — so if it is noisy, that is the fix.

### What to record

For **each** of A, B and C, capture four things. Partial results are worth reporting;
"A works, C fails at the configure step" is a useful answer.

1. The **exact command** you ran, and from what kind of shell (plain PowerShell, Developer
   PowerShell, `cmd`).
2. Whether configure printed **`-- SDL3 found -- video boards enabled (windowed)`** or the
   `not found` line. Quote it.
3. The **first** error in full, if it failed. Not a summary — the actual text.
4. `build\...\altairsim.exe --version` output, and `ctest --test-dir build -C Release -LE slow`
   pass line, if you got that far.

Fill this in, replacing every `?`:

```
shell Claude actually used : ?   (Git Bash / PowerShell tool / PowerShell fallback)
launched from             : ?   (plain PowerShell / other -- should be plain)

approach A (MSVC + VS generator, no env setup)  : PASS / FAIL  ?
approach B (MSVC + Ninja, chained vcvars)       : PASS / FAIL  ?
approach C (MinGW, setx PATH)                   : PASS / FAIL  ?

VS edition + version : ?
MinGW flavour        : ?    (w64devkit / MSYS2 UCRT64 / other)
SDL3 found by A/B/C  : ?
commands needed rewriting for the shell? : ?
first error, if any  : ?
```

### How to report it back

**Preferred — put it in the repository, so the answer outlives the conversation.** From
the Windows box:

```powershell
git checkout -b windows/env-findings
# edit THIS section: replace the block above with what actually happened,
# and correct any command that was wrong
git commit -am "Record what actually works for a Windows build without a Developer shell"
git push -u origin windows/env-findings
```

**Do not merge it to `master` yourself** — push the branch and say so. Findings that
contradict this file are the most valuable kind and want reading before they land.

**Also paste the filled-in block into the conversation on the Mac.** The branch is the
durable record; the paste is what lets the next step happen without waiting on a review.

**If an approach fails, say so and stop.** A failure here is information — this section
exists precisely because nobody knows the answer. Do not work around it silently, and do
not "fix" the build to make an approach succeed; that turns an unknown into a different
unknown.

Once settled: this section shrinks to a statement of fact and moves into §5, `DISTRIBUTION.md`
§4.4's table gets corrected, and `TODO.md`'s mingw item can close.

---

## 7. Debugging the win32 layer locally (the fast loop)

CI shows *that* something fails but not *where* — it can only give one MSVC error at
a time and can't attach a debugger. A native Windows box with the toolchain above
gives the same fast compile → run → fix loop we already have on macOS/Linux, and
lets you run the crashing binary under a debugger.

**This is also why running Claude Code on Windows helps:** with MSVC + CMake
installed, open this repo in Claude Code on the Windows machine and it can build and
debug `src/platform/win32/` directly, instead of pushing to CI and waiting.

To isolate a `unit` crash, run the test binary directly so you can see which
sub-test prints last before it dies (unbuffering `stdout` first, so the last line
survives the fast-fail, is what pinned down the `test_media` one):

```powershell
cmake --build build --config Debug --target altair_tests   # Debug = better diagnostics
build\Debug\altair_tests.exe                               # last line before the crash names the area
```

A Debug build trips the runtime's checks with a readable message and, if VS is
installed, offers to attach the debugger at the fault — which points straight at the
offending line. (Under WinDbg/VS, `0xC0000409` breaks at the `__fastfail` site.)

> **WSL will not help here.** WSL is Linux: it builds `src/platform/posix/`, not the
> win32 code, so it would give a green build that never touched the files under
> repair. The bring-up must happen on **native** Windows with MSVC.

---

## 8. The §2.1 rule still applies on Windows

Do **not** fix a Windows problem by adding an `#ifdef _WIN32` (or any OS header) to
shared code in `src/` or `tests/`. The `platform_lint` target (DESIGN.md §2.1) runs
as a build dependency and scans `tests/` too — an OS macro or header outside
`src/platform/` **fails the build**. OS-specific code goes in `src/platform/win32/`;
everything else stays platform-agnostic. (This is why the MSVC `strncasecmp` fix in
`tests/cputest.cpp` is a macro-free `<cctype>` helper rather than an `#ifdef`.)
