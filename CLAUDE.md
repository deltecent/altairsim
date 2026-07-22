# Working on `altairsim`

A C++20 simulator of the MITS Altair 8800 and the S-100 bus. No dependencies beyond a C++20
compiler and CMake; SDL3 is optional and detected, never required.

## If you are here to build or ship a release

**Read [`DISTRIBUTION.md`](DISTRIBUTION.md) and follow it.** It is written to be executed
step by step on a machine that has never seen this repository — literal commands, the exact
output to check after each, and a STOP condition on every check.

**If you are on the Intel Mac, the Windows box, or the Linux box, your job is §4.2 and
nothing else.** Build, test, package, upload to the draft release. **A build machine never
tags, never publishes, and never decides a version number.** If a check in §4.2 fails, stop
and report it — do not work around it and do not judge it probably fine. Nothing gates a
package after you upload it.

**On Windows, you do not need a Developer shell.** With CMake's default Visual Studio
generator, MSBuild finds the toolchain itself — a plain PowerShell works. Remember that your
own environment does not survive between commands: if you use Ninja instead, set up the
environment *within* the same command (`cmd /c "call vcvars64.bat && …"`).
`DISTRIBUTION.md` §4.4 has the table. **MSVC is the only supported Windows toolchain.**

**Your environment does not carry over from the terminal that launched you** — each command
runs in a fresh process, so a Developer PowerShell's `vcvars` setup never reaches your build
commands. There is no "set it up once at the top". Use the Visual Studio generator, which
needs no environment at all.

> **First time on the Windows box? Start with `docs/building-windows.md` §6.** It is a job,
> not a description: three build approaches that are *believed* to work and have never been
> run here, with the commands to settle each and how to report what you find — including
> `tools\build-sdl3-static.bat`, which was written from the working shell script and never
> executed. Do that before attempting a release build, and if something fails, report it
> rather than working around it. A failure there is the point.

## Before changing anything else

| | |
|---|---|
| [`DESIGN.md`](DESIGN.md) | The architecture and the reasoning. **Read the relevant section before implementing** — most surprises here are deliberate and explained. |
| [GitHub issues](https://github.com/deltecent/altairsim/issues) | The public work list: bugs and features. Check before proposing something — a declined idea may be recorded here with its reason so it does not get re-raised. (`TODO.md` is the maintainer's local, **untracked** brainstorming scratch and is not in the tree.) |
| [`DISTRIBUTION.md`](DISTRIBUTION.md) | How a release is built and shipped. |
| `docs/manual/` | The User Manual, for someone holding a release package. |
| `docs/devguide/` | The Developer Guide, for someone changing the source. |

## Rules that bite

- **Every change goes on a branch off `master` and is merged when done.**
- **`TODO.md` is untracked** — a local, fast-moving working doc, not in the tree. Its edits
  never go through git, so they need no branch and no PR. Anything in it meant for the public
  becomes a GitHub issue instead.
- **The word is "board", not "card"** (`DESIGN.md` §0.3), for the object, the command and
  the table. *Card* only where the sentence is genuinely about the physical 1970s artifact.
- **`docs/manual/ref/` is GENERATED from the binary.** Edit the emitter and run
  `cmake --build build --target docs-reference`; never hand-edit those files.
- **The manual may not name anything outside the package** — no source paths, no
  `CMakeLists`, no `ctest`, no `DESIGN.md`. `tests/acceptance/docs-manual.cmake` enforces
  this, so a chapter that cites the repository fails the build. The Developer Guide is
  allowed to, and is not checked.
- **A new manual chapter must be added to `docs/manual/ORDER`**, or it is written,
  committed, and silently not in the PDF.
- **The PDFs are built and committed by CI** (`.github/workflows/docs.yml`, pandoc pinned at
  3.6). If you run `tools/build-docs.sh` locally, `git checkout --` the PDFs afterwards —
  a local pandoc is a different pandoc, and a different pandoc is a different document.
- **Never give hardware a behavior it never had** to fix a software symptom. Check the host,
  the filter and the monitor layers first.

## Testing

```sh
ctest --test-dir build -LE slow     # after every edit  (~30 tests, under a minute)
ctest --test-dir build              # before a commit   (~33 tests, ~4 minutes)
```

**Match the pass line, not the absence of errors** — read `100% tests passed out of N`. A
`cd` that leaves the build directory can make `ctest` not run at all, which looks identical
to success if you are only checking for the word "error".

The acceptance tests are not smoke tests: each boots real period software on a whole machine
through the real CLI and reads back what landed on the terminal.
