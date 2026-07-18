# Building `altairsim` on Linux

**Status: verified building, testing and running on Linux.** Compiled and
smoke-tested on **Ubuntu 22.04.4 LTS (x86_64)** with **GCC 11.4.0** on
2026-07-14, and the **test suite was run there on 2026-07-16** — `ctest -LE slow`
passed 13/13. The simulator starts, lists its built-in machines, and runs monitor
commands.

> **The counts below are a dated record, not a running total.** The suite has grown
> since — `ctest -LE slow` registers 26 tests today — and CI now builds and tests
> Linux on every push, which is the live answer. What this document is for is the
> from-scratch procedure and the platform notes, and those have not changed.

There are **no third-party dependencies** — the produced binary links only
against the system C/C++ runtime (`libstdc++`, `libgcc_s`, `libc`, `libm`).
SDL3 is optional and detected: install it and the video boards get a window;
leave it out and they build headless, which is what the runs below did.

---

## 1. Prerequisites

| Need            | Minimum         | Verified with        |
|-----------------|-----------------|----------------------|
| C++20 compiler  | GCC 11+         | GCC 11.4.0           |
| CMake           | ≥ 3.20          | 3.22 (apt) / 3.28.3  |
| Make            | any             | GNU Make 4.3         |
| git             | any             | 2.34.1               |

On Debian/Ubuntu, the whole toolchain is one line:

```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake git
```

(`build-essential` provides `g++` and `make`.) Ubuntu 22.04's packaged CMake is
3.22, which already satisfies the `>= 3.20` requirement. On Fedora/RHEL the
equivalent is `sudo dnf install gcc-c++ cmake git make`.

> **No root?** You can drop a prebuilt CMake into your home directory instead of
> using the package manager:
> ```bash
> ver=3.28.3
> curl -fsSL -O https://github.com/Kitware/CMake/releases/download/v${ver}/cmake-${ver}-linux-x86_64.tar.gz
> tar xzf cmake-${ver}-linux-x86_64.tar.gz
> export PATH=$PWD/cmake-${ver}-linux-x86_64/bin:$PATH
> ```

---

## 2. Build

```bash
git clone https://github.com/deltecent/altairsim
cd altairsim
cmake -S . -B build
cmake --build build --target altairsim        # SERIAL — no -j. See the memory note below.
```

**Build serially — no `-j`.** The command above has no `-j`, and that is
deliberate: it compiles one file at a time. This is the recommended default,
especially on any machine without a lot of spare RAM. Do not reach for `-j` to
speed it up unless you have read the memory note below and know the box has the
headroom for it.

The result is `build/altairsim`.

At configure time you may see:

```
-- expect not found -- SKIPPING the interactive CLI test (acceptance-cli).
```

That only disables one optional *test*; it does not affect building the binary.
Install `expect` if you want that test. The build compiles with
`-Wall -Wextra -Wpedantic` and may print warnings under GCC, but there is no
`-Werror`, so warnings never fail the build.

> **A note on the source, for the record.** GCC's libstdc++ is stricter than
> macOS's libc++ about transitive includes: `src/util/json.cpp` used
> `std::strlen` without including `<cstring>`, which libc++ pulls in for free and
> libstdc++ does not. That include is now in the tree, so a fresh clone builds
> clean — but it is the shape of bug to expect if a *new* file reaches for a
> `<cstring>`/`<cstdint>`/`<algorithm>` name without saying so, since macOS will
> not catch it. Build on Linux before you trust a "portable" change.

### ⚠ Memory / parallelism — do not use a bare `-j`

`cmake --build build -j` (unbounded) launches **one compiler process per core at
once**. This is template-heavy C++20 code and each `cc1plus` can use several
hundred MB to over 1 GB. On a small machine this exhausts RAM and drives the box
into swap thrash — on the 3.8 GB host used here, a bare `-j` made it unresponsive
(SSH timed out) until the OOM killer intervened. If that happens, kill the build
from a console with:

```bash
killall -9 cc1plus cmake make
```

**Guidance — default to a serial build.** Unless you know the machine has plenty
of free RAM, build **serially**: just `cmake --build build --target altairsim`
with **no `-j` at all**. It is slower in wall-clock but it will not fall over,
and for a build this size the difference is minutes, not hours. The serial build
is exactly how this port was verified on `bart` (section 5).

Only if the box genuinely has the memory headroom, cap jobs to roughly one per
1.5–2 GB of RAM rather than turning `-j` fully loose:

```bash
cmake --build build --target altairsim -j2      # ~2 jobs for a 4 GB box
```

A full `-j$(nproc)` is fine *only* on a machine with ample RAM.

---

## 3. Smoke test

```bash
./build/altairsim --version      # prints "altairsim 0.1.0 ..."
./build/altairsim --list         # lists built-in machines (4k, default, minidisk, ...)
./build/altairsim -x "help" default   # boots the default machine, runs one monitor command, exits
./build/altairsim                # interactive: the default machine's front panel + monitor
```

All of the non-interactive commands above exit 0 on Linux. `--list` succeeding
confirms the machines and ROMs embedded into `.rodata` load correctly under
libstdc++, and `-x "help" default` confirms a machine actually initializes.

---

## 4. What was verified

- **Host:** Ubuntu 22.04.4 LTS, kernel 5.15, x86_64, 3.8 GiB RAM.
- **Toolchain:** GCC/g++ 11.4.0, GNU Make 4.3, CMake 3.28.3, git 2.34.1.
- **Platform layer:** the `platform_lint` target (DESIGN.md §2.1, "the OS has not
  escaped `src/platform/`") **passes** on Linux, and CMake selected
  `src/platform/posix/*` — the same POSIX implementation already proven on macOS.
- **Binary:** `build/altairsim`, a dynamically-linked ELF x86-64 executable
  depending only on `libstdc++`, `libgcc_s`, `libc`, `libm`.
- **Tests (2026-07-16):** `ctest --test-dir build -LE slow` → **100% tests
  passed, 0 failed out of 13**, in 29 s. That is the unit suite, three of the
  four 8080 exercisers (TST8080, 8080PRE, CPUTEST) and the acceptance tests —
  4K/8K BASIC off a cassette, MITS PS2 polled and interrupt-driven, the config
  and docs checks. `serial-hw` skipped cleanly (no serial hardware, exit 77) and
  `socket-hw` passed.
- **Not covered on this host:** the `slow` label (`cpu-8080exm`, `cpu-zexdoc`,
  `cpu-zexall`) was excluded by `-LE slow`; `acceptance-cli` was skipped because
  `expect` was not installed; the Host Bridge acceptance tests were skipped
  because the 8 MB CP/M image was absent. All three are gated by CI regardless —
  see `.github/workflows/`.

---

## 5. How this Linux build was tested

The build was developed on macOS and validated on a real Linux box over SSH.
The exact procedure, so it can be reproduced:

1. **Remote host.** `ssh bart.deltecent.com` — an Ubuntu 22.04.4 LTS x86_64
   machine with GCC 11.4.0, GNU Make, and git already installed. Nothing was
   built on the Mac; every compile ran on Linux.

2. **CMake without root.** At the time, `bart` had no CMake installed and the
   account had no passwordless `sudo`, so instead of `apt` a prebuilt CMake was
   unpacked into the home directory and put on `PATH` (the "No root?" box in
   section 1):
   ```bash
   mkdir -p ~/altairsim-linux-build && cd ~/altairsim-linux-build
   curl -fsSL -O https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz
   tar xzf cmake-3.28.3-linux-x86_64.tar.gz
   export PATH=$PWD/cmake-3.28.3-linux-x86_64/bin:$PATH
   ```
   **This step is no longer needed on `bart`** — it now has a system CMake 3.22.1
   at `/usr/bin/cmake`, which clears the `>= 3.20` floor. The box above is kept
   because it is still the answer on *any* host without root. On a normal machine
   with sudo, `sudo apt-get install -y cmake` is simpler.

3. **Throwaway clone.** The repo was cloned fresh on `bart` into
   `~/altairsim-linux-build/altairsim` — a scratch copy, kept entirely separate
   from the macOS working tree. The one build error encountered — `std::strlen`
   in `src/util/json.cpp` without `<cstring>` — was fixed there to get a clean
   build; that include has since been committed upstream, so a fresh clone no
   longer needs it.

4. **Configure + build.** `cmake -S . -B build`, then
   `cmake --build build --target altairsim`. The first attempt used a bare `-j`
   and drove the 3.8 GB host into swap thrash until it stopped responding to SSH
   (see the memory warning in section 2); the successful build was **serial**,
   and completed with exit 0, producing `build/altairsim`.

5. **Smoke test.** The section-3 commands (`--version`, `--list`,
   `-x "help" default`) were run over the same SSH session and all exited 0,
   confirming the binary not only links but initializes a machine and loads its
   embedded ROMs on Linux.

6. **Test suite (added 2026-07-16).** The whole procedure was repeated from a
   fresh clone of `master`, this time building **all** targets rather than just
   `--target altairsim` — ctest needs the test executables too. Serial build,
   exit 0, no errors. Then `ctest --test-dir build -LE slow` →
   **100% tests passed, 0 failed out of 13** (29 s). See section 4.

To reproduce on any Linux host, do the same: get a C++20 toolchain + CMake ≥
3.20, clone, and build serially (or with a bounded `-j`) per section 2.

---

## 6. Rebuilding after a `git pull`

**The commands are the same as for a fresh clone.** There is no separate
procedure, and no need to delete `build/`:

```bash
git pull
cmake -S . -B build          # no-op if nothing about the configuration changed
cmake --build build          # rebuilds only what the pull touched
```

You can even skip the `cmake -S . -B build` line: the generated Makefile re-runs
CMake by itself when `CMakeLists.txt` changes. Running it costs a second and is
the safer habit.

This works because of two deliberate properties of the tree, and it is worth
knowing *why* rather than cargo-culting a `rm -rf build`:

- **The C++ sources are listed explicitly in `CMakeLists.txt`, not globbed.** So
  a pull that adds a `.cpp` necessarily changes `CMakeLists.txt`, which triggers
  the automatic reconfigure. A new source file cannot be silently missed.
- **The things that *are* globbed — `roms/`, `machines/` — all use
  `CONFIGURE_DEPENDS`** (`CMakeLists.txt` §"ROMs"/§"Built-in machines"), which
  re-globs on every *build*, not just on configure. A pull that adds or deletes
  a ROM or a machine `.toml` is picked up with no manual step.

### When you *do* need to delete `build/`

The build directory caches **absolute paths**, so it is not portable. Delete and
re-create it if you:

- **moved or renamed the source directory** (this project has been renamed once
  already), or
- **changed compilers** (e.g. switched GCC versions or to Clang).

The rename case fails loudly rather than silently, which is the good outcome:

```
CMake Error: The current CMakeCache.txt directory .../build/CMakeCache.txt is
different than the directory .../build where CMakeCache.txt was created.
CMake Error: The source ".../CMakeLists.txt" does not match the source
".../CMakeLists.txt" used to generate cache.  Re-run cmake with a different
source directory.
```

The fix is `rm -rf build` and configure again. That error means the build
directory is stale, **not** that the pull broke anything.

> **A different CMake, same build directory.** Reconfiguring an existing
> `build/` with a *different CMake version* than the one that generated it (say,
> a `$HOME` 3.28 one time and `/usr/bin/cmake` 3.22 the next) is accepted without
> an error — but it regenerates the build system and you get a **full rebuild**,
> not an incremental one. Harmless, just slow, and confusing if you expected
> "nothing changed" to be instant. Keep `PATH` consistent between invocations,
> or `rm -rf build` and stop thinking about it.

**If a build fails right after a pull, suspect the build directory before
suspecting the code.** `rm -rf build && cmake -S . -B build && cmake --build
build` is the one-line answer, and it distinguishes the two cases: if a clean
build works, the tree was fine and the cache was stale.
