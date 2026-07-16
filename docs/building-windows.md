# Building `altairsim` on Windows

**Status (2026-07-15): builds, links, and passes the full test suite with MSVC.**
The `src/platform/win32/` layer was written on macOS and had never been through a
compiler until GitHub Actions CI was added. It now **builds and links cleanly** on
`windows-latest` (MSVC, Winsock via `ws2_32`), and **all registered tests pass**
locally on native Windows.

The `0xC0000409` fast-fail that the `unit` aggregate used to hit was a **test**
bug, not a win32-layer one: `test_media.cpp` deleted a temp file while a live
`HostFile` still held it open (POSIX unlinks an open file; Windows refuses and
`fs::remove` threw an uncaught `filesystem_error`), and it also stripped only
`owner_write` to simulate a read-only file, which the C++ filesystem maps to the
Windows read-only attribute *only when every write bit is clear*. Both were fixed
in the test — no `#ifdef` — so the teardown unmounts before it deletes and clears
all the write bits. §6 still describes the fast-loop for the next win32 issue.
Treat a Windows build as a debugging job, not finished work
(`docs/porting-notes.md`).

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

**Use a *Developer* shell, not a plain one.** The MSVC compiler (`cl.exe`) and its
`INCLUDE`/`LIB` paths only exist inside the VS environment. From the Start menu
open **"Developer PowerShell for VS 2022"** (or **"x64 Native Tools Command Prompt
for VS 2022"**). A normal PowerShell/cmd will not find `cl.exe`.

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
> configure time, no `Release\` subfolder:
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
build\Release\altairsim.exe --version        # prints "altairsim 0.1.0 ..."
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
- **`serial-hw`** self-skips (needs real serial ports via `ALTAIR_SERIAL_A/B`).
  It has since been **run for real** on native Windows — two USB serial ports with
  a null modem, `$env:ALTAIR_SERIAL_A="COM4"; $env:ALTAIR_SERIAL_B="COM10"; ctest
  -C Release -L hw` — and passes (25 checks, 0 failed). See §5.

Everything that registers passes. If a new win32 problem crashes the `unit`
aggregate with `0xC0000409`, §6 shows how to isolate it.

---

## 5. What is verified so far

- **Compiles and links** on `windows-latest` (MSVC, CI). The win32 serial, socket,
  and terminal implementations build; `CMakeLists.txt` links `ws2_32` for Winsock.
- **All registered tests pass** (the ones that register without `expect`/disk
  images), including the `unit` aggregate now that its teardown bug is fixed.
- **Serial verified against real hardware (2026-07-15):** `serial_win32.cpp` passes
  the full `ctest -L hw` suite on native Windows — two USB FTDI ports (`COM4 <->
  COM10`) with a null modem, 25 checks, 0 failed, covering both the platform layer
  and an in-machine 6850 doing real `/CTS` flow control and a latched `/DCD`
  carrier-drop interrupt.
- **Not yet verified:** the **socket** code against a real remote peer and the
  **terminal** code against an interactive console. (The socket loopback path is
  exercised by the suite, but no real peer; the terminal has had no interactive
  run.) The win32 layer's remaining most-likely-wrong spots are enumerated in
  `docs/porting-notes.md`.

---

## 6. Debugging the win32 layer locally (the fast loop)

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

## 7. The §2.1 rule still applies on Windows

Do **not** fix a Windows problem by adding an `#ifdef _WIN32` (or any OS header) to
shared code in `src/` or `tests/`. The `platform_lint` target (DESIGN.md §2.1) runs
as a build dependency and scans `tests/` too — an OS macro or header outside
`src/platform/` **fails the build**. OS-specific code goes in `src/platform/win32/`;
everything else stays platform-agnostic. (This is why the MSVC `strncasecmp` fix in
`tests/cputest.cpp` is a macro-free `<cctype>` helper rather than an `#ifdef`.)
