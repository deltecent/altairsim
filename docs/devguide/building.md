# Building it

**There are no dependencies.** A C++20 compiler and CMake ≥ 3.20 is the entire list. The TOML
parser, the JSON encoder and the line editor are all in the tree, so a fresh clone builds with
nothing to download.

```sh
git clone https://github.com/deltecent/altairsim.git
cd altairsim
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -LE slow      # drop -LE slow for the full 8080 exerciser
./build/altairsim                    # the default machine
```

If CMake is unfamiliar, the two `cmake` calls are two distinct steps — **configure**, then
**build** — and you run both in that order:

- **`cmake -S . -B build`** — *configure.* Reads `CMakeLists.txt` in the source tree (`-S .`,
  the current directory) and writes the generated build files into a fresh `build/` directory
  (`-B build`). It downloads nothing and compiles nothing; it just works out *how* to build.
  You rerun it only when `CMakeLists.txt` itself changes — and even then the build step reruns
  it for you.
- **`cmake --build build -j`** — *build.* Compiles what the configure step laid out under
  `build/`. `-j` runs the compiles in parallel across all CPU cores (drop it for a serial
  build, or `-j4` to cap the jobs). This is the command you repeat after editing code.
- **`ctest --test-dir build -LE slow`** — run the tests that the build produced. `--test-dir
  build` points at the same directory; `-LE` is *label-exclude*, so `-LE slow` runs everything
  **except** tests tagged `slow` (the ~10-minute 8080 exerciser). Drop it to run those too.

Everything lands in `build/`, which is disposable: `rm -rf build` and rerun the configure step
for a clean slate.

That is not a boast about minimalism for its own sake. It is a property worth defending: the
day this needs a package manager to build is the day it stops being something a person can
pick up in ten years and compile.

**After a `git pull`, the commands do not change.** `cmake -S . -B build && cmake --build build`
again — there is no separate incremental procedure and no need to delete `build/`. New `.cpp`
files can't be missed because sources are listed in `CMakeLists.txt` rather than globbed, so
adding one edits `CMakeLists.txt` and forces a reconfigure; `roms/` and `machines/` *are*
globbed but use `CONFIGURE_DEPENDS`, so they re-glob on every build. The build directory does
cache absolute paths, though, so **rename the checkout or switch compilers and you must
`rm -rf build`** — it fails with a loud `CMakeCache.txt directory is different` error, not a
silent wrong answer. If a build breaks — or a test that used to pass fails — immediately after
a pull, suspect a stale `build/` before suspecting the code: a `rm -rf build` and a clean
reconfigure is the first thing to rule out, because a half-rebuilt object can walk an
already-fixed bug back in. `docs/building-linux.md` §7 has the details.

## Warnings as errors before a PR

The build always compiles with `-Wall -Wextra -Wpedantic` (GCC/Clang) or `/W4 /permissive-`
(MSVC), but by default a warning is just noise and does not fail anything. **CI configures
every leg with `-DWERROR=on`**, which on GCC and Clang adds `-Werror` and turns a warning into
a build failure — so a PR that introduces one on either of those goes red before it can merge.
Reproduce that gate locally before you open the PR:

```sh
cmake -B build -DWERROR=on && cmake --build build -j
```

It is off by default (`option(WERROR … OFF)` in `CMakeLists.txt`) so a casual build, or one on
an unfamiliar toolchain whose newer diagnostics have not been chased down yet, still compiles.
GCC and Clang have slightly different warning sets, so a clean local build on one is not proof
the other is clean — that is what the separate CI legs exist to catch.

**MSVC is not in the gate yet.** `/W4` carries a pre-existing backlog (intentional integer
narrowing, which GCC/Clang do not flag, plus some variable shadowing) that must be cleared
before `/WX` can go on. That is tracked in issue #238; until it lands, `-DWERROR=on` is a no-op
on MSVC and its `/W4` warnings stay visible in the log but non-fatal.

## The optional video backend (SDL3)

**Still no *required* dependency.** The graphics boards — the [VDM-1](../boards/proctech-vdm1.md),
and the Dazzler to follow — draw into a host `Display` (`src/host/display.h`). That interface has
two backends: a headless `NullDisplay` (draws into memory, shows nothing) that is always
available, and an `SdlDisplay` (a real window) compiled **only when SDL3 is found**. So a plain
`cmake -S . -B build` on a machine with no SDL3 still configures, builds, and passes every test —
the video boards are simply headless, which is exactly what CI and a scripted run want.

To get a window, install SDL3 and reconfigure:

```sh
# macOS
brew install sdl3
# Debian/Ubuntu (needs a recent SDL3 package)
sudo apt install libsdl3-dev
# vcpkg (any OS)
vcpkg install sdl3

cmake -S . -B build          # reconfigure -- watch the status line
```

The configure step reports which backend it chose:

```
-- SDL3 found -- video boards enabled (windowed)
        …or…
-- SDL3 not found -- video boards build headless (null display). Install SDL3 (see docs/devguide) for a window.
```

CMake auto-detects with `find_package(SDL3 CONFIG)`; found → it compiles `src/host/display_sdl.cpp`,
links `SDL3::SDL3`, and defines `ALTAIRSIM_ENABLE_SDL`. Force a headless build even where SDL3 is
installed with **`-DALTAIRSIM_ENABLE_SDL=OFF`**. That flag is what a macOS *universal* build
needs, because a Homebrew SDL3 is single-arch and cannot link into an `x86_64;arm64` fat binary.
**CI passes it nowhere. One leg — macOS — installs SDL3 from Homebrew and builds native
`arm64`, so it is the only place `display_sdl.cpp` is compiled at all**, and the workflow fails
that leg if it comes up headless. Linux and Windows take the not-found path above and build
against the null display. Only
`display_sdl.cpp` and the composition root (`src/main.cpp`) are macro-gated; the boards themselves
`#include` no SDL and compile in every configuration. Then:

```sh
altairsim vdm1               # a VDM-1 with a banner-drawing demo (roms/VDM1DEMO)
```

## What actually got built

**Built and tested on Linux, macOS, and Windows.** The code is written to be portable — C++20,
no dependencies, and every OS difference confined to `src/platform/` behind a header with zero
conditionals — and that portability is now proven, not asserted: Linux (Ubuntu/GCC), macOS on
Apple Silicon (and on Intel, built natively there rather than in CI), and Windows on MSVC all
build and pass the suite. The Windows platform layer, once merely written, is field-proven.

**CI runs the suite on every push.** GitHub Actions builds and tests on all three platforms —
Linux, macOS, and Windows are each a required check — so a regression on any of them shows up
before it merges. The tests still run locally the same way, when someone types `ctest`.

Each of those jobs uploads the binary it built, so a green run leaves three executables on
GitHub — including the two you cannot produce on your own machine. To fetch them:

```sh
tools/fetch-ci-binaries.sh          # newest CI run on the current branch
tools/fetch-ci-binaries.sh 42       # ...on PR 42
```

It **waits** for the run if it is still going and refuses to download from a red one, which
makes it a reasonable last step before merging: three files means three platforms passed. They
land in `./artifacts` (git-ignored, replaced on every fetch) with the executable bit restored,
since the artifact zip does not carry POSIX modes. Nothing in the build or the tests reads from
there — these are CI's binaries, kept for running or handing to someone, not a build output of
this tree.

## The tests

```sh
ctest --test-dir build -LE slow     # unit + acceptance. About 30 seconds.
ctest --test-dir build              # ...plus 8080EXM, the full exerciser.
ctest --test-dir build -L hw        # modem control, against a real null-modem cable.
```

The acceptance tests are not unit tests. They **boot period software on the whole machine
through the real CLI** and check what lands on the terminal — 4K and 8K BASIC off a cassette,
MITS Programming System II polled and interrupt-driven, CP/M off a minidisk. Several ship with
a **negative control**: the same script against a machine that ought to *fail*, marked
`WILL_FAIL`. If a control ever passes, the test it guards was passing for the wrong reason and
is worthless. That is the only reason to believe any of them.

**The CPU exercisers are gated three ways, on purpose.** `cpu-8080exm` is ~2.9 billion
instructions and `cpu-zexdoc`/`cpu-zexall` ~5.8 billion each, so they are labelled `slow` and
excluded from the per-push matrix. `cpu-exerciser.yml` runs them on one Linux runner *only when
CPU/ISA code changes* — an opcode bug is a logic bug and shows up on any host, so one
architecture is enough to catch it, and paying for three would be paying three times for the
same answer. `cpu-exerciser-release.yml` then runs them on **all three platforms** at a release
tag (or on demand), because the one thing that is *not* architecture-independent is the
compiler: the cores are full of shifts, masks and half-carry arithmetic, and until a release
nothing has ever driven billions of instructions through the MSVC build.

The hardware tests (`-L hw`) run against an actual null-modem cable between two USB serial
ports, because a claim about a cable deserves a cable. They are opt-in, pointed at your ports
with `ALTAIR_SERIAL_A` / `ALTAIR_SERIAL_B`, and they **skip loudly** when the hardware is
absent — a hardware test that quietly passes with no hardware is a green tick that means
nothing. Unset, `serial-hw` exits 77 and ctest reports it `Skipped`; that is the expected
result on any machine without the cable, not a failure.

The two device paths are per-machine — list yours, then point the test at two ports joined by
the cable:

```sh
# macOS:   ls /dev/cu.usbserial-* /dev/tty.usbserial-*
# Linux:   ls /dev/ttyUSB* /dev/ttyACM*
# Windows: Device Manager -> Ports (COM & LPT), or run  mode

ALTAIR_SERIAL_A=/dev/ttyUSB0 ALTAIR_SERIAL_B=/dev/ttyUSB1 ctest --test-dir build -L hw
```

**The cable is not just any null-modem cable.** `serial-hw` drives the modem-control
lines, so the pins have to actually cross — and it needs the *full-handshake* wiring in
which **one end's DTR fans out to both DSR and DCD** on the other. A 3-wire cable
(TxD/RxD/GND) carries the bytes but leaves every control pin dead; a partial null-modem
that loops DTR/DSR/DCD back locally instead of crossing them looks identical to the eye
and fails the same checks. That is the usual cause of a `serial-hw` that fails rather than
skips — the byte check passes and the pin checks do not.

Wire it as (a DE-9 DTE pinout in parentheses, both ends):

| A end | | B end |
|---|---|---|
| TxD (3) | → | RxD (2) |
| RxD (2) | ← | TxD (3) |
| RTS (7) | → | CTS (8) |
| CTS (8) | ← | RTS (7) |
| DTR (4) | → | DSR (6) **and** DCD (1) |
| DSR (6) **and** DCD (1) | ← | DTR (4) |
| GND (5) | ↔ | GND (5) |

So B raising DTR is a *carrier appearing* at A — to a 6850 strapped `dcd=wired`,
indistinguishable from a modem, which is the whole point of the test. The wiring is
restated at the top of `tests/serialtest.cpp`.

## Catching lifetime bugs: `-DSANITIZE=on`

Twice now the `unit` suite has SEGFAULTed on **Windows CI only**, green on Mac and Linux, and
both times the cause was a use-after-scope the macOS allocator silently tolerated — a board/chip
test whose `Clock` was declared *after* the board, so the Clock died first and the board's
destructor cancelled its wake event on freed memory. No compiler warning fires for it.

`-DSANITIZE=on` is the local oracle. It threads AddressSanitizer + UndefinedBehaviorSanitizer
(just ASan on MSVC) through the whole binary, so the crash names its own `file:line` instead of
you guessing from a block-buffered stack trace. Configure it in its **own build directory** — an
instrumented binary is slower and is not what you ship:

```sh
cmake -S . -B build-asan -DSANITIZE=on
cmake --build build-asan --target altair_tests
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ./build-asan/altair_tests
```

It is off by default and not in CI — CI catches the Windows crash by running on Windows, and this
is the tool you reach for once it does. Run it before merging anything that changes board or chip
object lifetimes. **The rule it enforces:** in a board/chip test, declare `Clock c;` *before* the
board it drives (`tests/test_dcdd.cpp` is the precedent).

## The documentation is part of the build

Two documents come out of this tree:

| Target | Is |
|---|---|
| **User Manual** (`docs/manual/`) | Ships in the package. **Self-contained** — it may not name a single file the reader does not have. No `src/`, no CMake, no `DESIGN.md`. |
| **Developer Guide** (`docs/devguide/`) | This. Repo only. Free to talk about the source, because you cannot write a board without it. |

Some of the manual is **generated from the binary**, and this matters if you touch a board:

```sh
cmake --build build --target docs-reference
```

That rewrites `docs/manual/ref/*.md` — the command dictionary, the per-board parameter tables,
the machine list — by walking `Board::properties()` and the `CommandDef` table. Those files are
committed, and **a test (`docs-reference`) fails if they are stale.** So if you add a property,
change a default, or reword a `HELP` string, run it and commit the result alongside your change.

This is not a docs chore bolted on at the end. A board's properties **are** its TOML schema; a
hand-written parameter table would be a second schema, and the first draft of one in this very
project got three of the memory board's eight defaults wrong. The reference is printed rather
than retyped for the same reason the rest of the program has one source of truth for anything.

The PDFs need `pandoc` and any Chromium-based browser, and are built by `tools/build-docs.sh`.
Neither tool is a build dependency, and neither goes anywhere near the simulator.

### Why a wrapped paragraph pastes as several lines (and why we don't "fix" it)

A recurring report (issue #246): copy a paragraph out of one of our PDFs and it pastes as
several lines instead of one. This is a **reader** behavior, and the investigation is worth
recording so it is not re-run from scratch.

**There are no hard line breaks in the paragraphs.** In the PDF content stream each wrapped
line is painted at a new baseline position; there is no newline character anywhere. When a
paragraph pastes as three lines, the reader is *inventing* those breaks from the vertical gaps
at copy time. Nothing in the build put them there.

The only thing that tells a reader "the real text of this span is *this exact string*, ignore
the geometry" is `ActualText`. Hand-built PDFs extracted with poppler make the picture exact:

| PDF | Copies as |
|---|---|
| Wrapped paragraph, plain geometry | 2 lines (break invented) |
| Wrapped paragraph inside a real `<P>` **structure tag** | still 2 lines |
| Wrapped paragraph with paragraph-spanning **`ActualText`** | **1 line** |

The middle row is the one that closes off the easy fixes: the **structure/accessibility tree
does not join wrapped lines** — only `ActualText` does. Our PDFs are already tagged
(`pdfinfo` → `Tagged: yes`), and Chrome emits `ActualText` **only for ligatures** (the `fi`/`fl`
spans), never for paragraphs. Every mainstream HTML/Markdown→PDF engine (WeasyPrint, Prince,
wkhtmltopdf, Typst, LaTeX) conveys paragraphs through the structure tree, not through
paragraph-level `ActualText` — so **swapping the PDF engine changes the producer name and
nothing about the copy behavior.** Do not propose one as the fix.

Readers split on this: Word and Acrobat honor the structure tree, so the paragraph comes across
whole; Preview and Chrome's built-in viewer fall back to pure geometry and invent the breaks.
Worse, **macOS Sequoia's Preview/PDFKit regressed** — it stamps a paragraph break on *every*
visual line regardless of the document, surviving even a text-only paste — so it would ignore
`ActualText` too.

A custom `ActualText` post-pass *is* buildable — Chrome already brackets each paragraph in one
marked-content span — but it is a real tool, not a shell snippet: the paragraph text is emitted
as subsetted **hex glyph-IDs**, so each glyph has to be reverse-mapped through the font's
`ToUnicode` CMap, with TJ kerning arrays and nested ligature/italic spans handled, then the
stream recompressed. For a cosmetic gain that the reader in the original report (Sequoia Preview)
would not even honor, it has not been worth doing.

The Markdown can't fix it either: wrapping is decided at render time from the column width, and
there is no "soft wrap that copies as one line" a source file can encode. The only source-side
lever is keeping the things people copy short enough **not to wrap** — which the commands already
are (mono, ≤79 columns, in a column sized for 79). The case that wraps is prose. For
copy-a-command / copy-a-prompt workflows, copy from the **Markdown**, which ships beside every
PDF and is plain text; the PDF is the read-it version.
