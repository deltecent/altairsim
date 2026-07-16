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
silent wrong answer. If a build breaks immediately after a pull, suspect a stale `build/`
before suspecting the code. `docs/building-linux.md` §6 has the details.

## What actually got built

**Built and tested on Linux, macOS, and Windows.** The code is written to be portable — C++20,
no dependencies, and every OS difference confined to `src/platform/` behind a header with zero
conditionals — and that portability is now proven, not asserted: Linux (Ubuntu/GCC), macOS as a
universal `x86_64`+`arm64` binary (Intel and Apple Silicon both), and Windows on MSVC all build
and pass the suite. The Windows platform layer, once merely written, is field-proven.

**CI runs the suite on every push.** GitHub Actions builds and tests on all three platforms —
Linux, macOS, and Windows are each a required check — so a regression on any of them shows up
before it merges. The tests still run locally the same way, when someone types `ctest`.

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

The hardware tests (`-L hw`) run against an actual null-modem cable between two USB serial
ports, because a claim about a cable deserves a cable. They are opt-in, pointed at your ports
with `ALTAIR_SERIAL_A` / `ALTAIR_SERIAL_B`, and they **skip loudly** when the hardware is
absent — a hardware test that quietly passes with no hardware is a green tick that means
nothing.

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
project got three of the memory card's eight defaults wrong. The reference is printed rather
than retyped for the same reason the rest of the program has one source of truth for anything.

The PDFs need `pandoc` and any Chromium-based browser, and are built by `tools/build-docs.sh`.
Neither tool is a build dependency, and neither goes anywhere near the simulator.
