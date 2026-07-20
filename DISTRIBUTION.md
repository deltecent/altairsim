# `altairsim` — Distribution

**Status:** design + runbook. The process it describes is **not automated** — every step here is run by hand, deliberately, and §8 says what would have to change for that to stop being true.
**Date:** 2026-07-20, written after cutting v0.2.0.

---

## 0. What this is, and who reads it

This document says **how a release is built and where it goes.** `DESIGN.md` has no packaging section, and until now the process lived in one person's shell history: `tools/build-package.sh` builds *one* archive for *one* platform, and the per-platform staging that actually produced v0.2.0's three archives — copy the tree, swap the binary, `tar.gz` for Unix and `zip` for Windows — was done by hand and written down nowhere.

**IT HAS TWO AUDIENCES, AND THE SECOND ONE SETS THE BAR.**

1. **A person**, deciding whether the process is sound.
2. **An assistant on a build machine that has never seen this repository before** — the Intel Mac, the Windows box, the Linux box. It has no memory of how the last release was cut. Everything it needs is in this file, or it is nowhere.

That is why the commands below are literal, why each one carries the **exact string to check** for, and why every check has a **STOP** attached. A step that says "make sure it worked" is useless to the second reader, and it is how v0.2.0 shipped broken: the configure line that says whether video is enabled was printed on every build, all along, and nobody read it.

**This document is not the package contents.** `docs/package.map` is the single source of truth for what goes in the archive, and `docs/manual/package.md` is the *reader's* view of the same thing. This is the *producer's* view. All three must agree; if they disagree, the map wins.

---

## 1. The four packages

```
altairsim-X.Y.Z-macos-arm64.tar.gz      Apple Silicon
altairsim-X.Y.Z-macos-x86_64.tar.gz     Intel Mac
altairsim-X.Y.Z-windows-x86_64.zip      Windows 10+, built with MSVC
altairsim-X.Y.Z-linux-x86_64.tar.gz     Linux, built against the oldest glibc available
```

**Four, and macOS is deliberately split.** A universal macOS build *is* possible — SDL upstream's `SDL3.xcframework` carries a real `macos-arm64_x86_64` slice, confirmed with `lipo -archs` — so this is a **choice**, not a limitation.

It is the right choice for two reasons. The download is half the size for everyone who takes it. And more importantly, **each slice is built and tested on the hardware it targets**, which closes a hole both previous releases admitted to in their own notes: the universal binary CI produced had its `x86_64` half exercised by nobody, because the macOS runner is arm64. Splitting turns that from a confession into a test.

---

## 2. What is inside one

**Shipping today**, as declared by `docs/package.map`:

```
altairsim[.exe]                       the program
altairsim-manual.pdf                  the User Manual
USING-ALTAIRSIM.md                    the briefing for an AI assistant driving it over MCP
LICENSE                               ours (MIT)
examples/cpm/                         CP/M 2.2 on an 8" floppy
examples/basic/                       4K BASIC on a cassette
examples/sol/                         a Sol-20 with TREK80
examples/diskbasic/                   Altair Disk BASIC 4.1
```

**Added by the SDL3 work** (2026-07-20):

```
LICENSE-SDL3                          SDL3's zlib licence
```

**One file, because SDL3 is linked statically into the binary (§3.2) rather than shipped beside it.** That is the whole benefit: no `SDL3.dll`, no `.framework`, no `libSDL3.so.0`, and nothing to copy or rewrite at release time. The licence still ships — the code is in there, so its licence belongs in the package.

**The contents come from `docs/package.map` and nowhere else.** Adding the licence means adding a `FILE` line *there* — not editing `tools/build-package.sh`. `docs/manual/package.md` must then name it too, because the manual may only name paths that actually ship.

The Developer Guide is **not** in the package. It is about the source, which is not in there either.

---

## 3. SDL3

**Every binary this project shipped through v0.2.0 is headless.** No CI leg had SDL3, so `find_package(SDL3 CONFIG QUIET)` failed, `display_sdl.cpp` was compiled nowhere, and the archives carried a null display. Verified against the published v0.2.0 files: `otool -L` links no SDL, and `strings` finds neither `libSDL3` nor `SDL_CreateWindow` in any of the three. **The video window the manual documents at length could not be opened from anything released so far.** Fixing that is the whole reason packages are now built locally rather than from CI artifacts.

**Two things changed on 2026-07-20.** Packages are built on machines that have SDL3 (§4), and **the macOS CI leg now installs it** and builds native `arm64` — so `display_sdl.cpp` is compiled somewhere on every push, and that leg **fails if it comes up headless**. Linux and Windows still build against the null display; one leg is enough to stop a break going green everywhere, which is what it was doing.

### 3.1 Where the libraries come from — SETTLED

**Each build machine maintains its own SDL3, installed natively. Nothing is vendored into this repository.** (Patrick, 2026-07-20.)

```
macOS     tools/build-sdl3-static.sh    -- run ONCE, installs to ~/opt/sdl3-static
Linux     tools/build-sdl3-static.sh    -- same script, same prefix
Windows   tools\build-sdl3-static.bat   -- %USERPROFILE%\opt\sdl3-static   [UNVERIFIED]
```

**Both scripts pin the same SDL3 version**, build it static, install to a fixed prefix, and are a no-op on a second run. That pin is the only thing making four independently-maintained machines agree — change it deliberately, and rerun the script everywhere when you do.

**The `.bat` has never been run.** It was written from the working shell script and is here to be tested; `docs/building-windows.md` §6 says how to report what actually happens. **It is a `.bat` and not a `.ps1` on purpose:** PowerShell's execution policy blocks unsigned scripts by default, so a freshly-cloned `.ps1` will not run until the user changes a machine setting. `curl.exe` and `tar.exe` have shipped in Windows since 10 1803, so it needs nothing installed but CMake.

**THE WINDOWS TRAP IS THE C RUNTIME, and it has no Unix equivalent.** MSVC links the CRT dynamically by default (`/MD`), so the `.exe` then needs the VC++ redistributable — present on most Windows 10 machines, absent on a clean one, and a package that requires an install first is a broken package. Building with the **static** CRT (`/MT`, i.e. `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`) removes that requirement — **but SDL3 and `altairsim` must agree.** Mixing the two gives duplicate-symbol link errors at best, and two separate C runtime heaps at worst. Set it on both builds, or neither.

`brew install sdl3` is fine for *development* — it is what CI's macOS leg uses — but it ships a dylib only and cannot produce a package. See §3.2.

**SDL3 is a prerequisite, exactly like the compiler.** There is no `third_party/`, no fetch script, no Git LFS, and no committed binaries — options that were weighed and are not needed, because a machine that can build `altairsim` can install SDL3 the ordinary way. This also keeps ~11M per SDL3 version out of a 67M `.git` permanently.

**The consequence to keep in view: the version is per-machine and nothing enforces it.** Two build machines can hold different SDL3 releases and neither will complain. That is acceptable — SDL3 is ABI-stable within a major version and the packages are independent artifacts — but if a video bug ever appears on one platform and not another, **check the SDL3 versions first.** §4.2 asks each machine to record its version for exactly this reason.

### 3.2 The absolute-path trap, and why the answer is STATIC

A macOS build against Homebrew's SDL3 links **`/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib` by absolute path**. Zip that binary, hand it to anyone without that exact Homebrew prefix, and it does not start. The obvious fix is to bundle the dylib and rewrite the install name. **There is a better one.**

**LINK SDL3 STATICALLY. Measured on macOS/arm64 2026-07-20, and it is the recommended approach.** Homebrew ships no static library — only the dylib, and the one `.a` there is `libSDL3_test.a`, SDL's test harness. So this needs a source build, which is what the script is for:

```sh
tools/build-sdl3-static.sh            # once per machine; ~/opt/sdl3-static
```

then point the simulator at it — `find_package` resolves `SDL3::SDL3` to the static target with **no change to `CMakeLists.txt`**:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$HOME/opt/sdl3-static" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
```

The script builds with `-DSDL_STATIC=ON -DSDL_SHARED=OFF`. **Both flags matter:** with a shared library also present in the prefix, `find_package` resolves to *that* and the static build is silently undone. The script refuses to finish if it finds one.

**What it buys, measured rather than asserted:**

| | dynamic (brew) | static |
|---|---|---|
| binary | 1.7M **+ a 2.4M dylib to bundle** | **4.4M, self-contained** |
| `otool -L` | `/opt/homebrew/...` — the trap | system frameworks only |
| packaging work | copy the dylib, `install_name_tool`, `@rpath`, and verify all three | **none** |

About **0.3M** on the package, and the entire bundling problem disappears — nothing to copy, no install names to rewrite, nothing to get wrong at release time. Verified: the resulting binary boots CP/M from an unpacked package and `otool -L` shows no SDL at all.

**`CMAKE_OSX_DEPLOYMENT_TARGET` is a separate requirement and is not optional.** Without it the binary targets whatever the build machine runs and will refuse to start on anything older. Set it on **both** the SDL3 build and the simulator build; `vtool -show-build` confirms it (`minos 11.0`).

*(SDL upstream generally prefers dynamic linking, so a user can update SDL independently of the app. That argument is weaker for a self-contained simulator handed to someone as a zip, and SDL3's zlib licence permits static linking outright.)*

**Linux and Windows should work the same way — `-DSDL_STATIC=ON` is not macOS-specific — but neither has been tried.** If a platform ends up dynamic after all, then bundling is the fallback and it is mandatory there:

| | fallback if dynamic |
|---|---|
| macOS | copy the dylib in; `install_name_tool -change … @executable_path/…` |
| Linux | copy `libSDL3.so.0` in; link with `-Wl,-rpath,'$ORIGIN'` |
| Windows | put `SDL3.dll` beside the `.exe` — the loader looks there first |

§7 has the check that proves you did one or the other.

---

## 4. The build machines

**Two roles, and one machine holds both.** Know which one you are before you start:

- **The coordinator** — bumps the version, merges, tags, creates the draft release, builds the manual PDF **once**, publishes, and mirrors to altairsim.com. This is §5, and it happens on one machine only.
- **A build machine** — §4.2, and nothing else. **A build machine never tags, never publishes, and never decides a version number.** All four machines are build machines, the coordinator included.

### 4.1 What each machine needs

```
a C++20 compiler and CMake >= 3.20
SDL3 for that platform
git, and gh authenticated against the repository
```

**That is the whole list. No pandoc, no Chrome, no poppler.** The manual PDF is built once by the coordinator and handed to the others, so `build-package.sh` is invoked with `--pdf` rather than being allowed to rebuild it. This is not a convenience: **without `--pdf` the script calls `build-docs.sh`**, which needs **pandoc pinned at 3.6** (Homebrew ships 3.10, and a different pandoc is a different document), a Chromium browser as the paginator, and `pdffonts` for the font check. Four machines rebuilding it independently means four subtly different manuals in four packages. With `--pdf` none of that runs, and the packaged PDF is byte-identical to the one handed in — **the script warns loudly on the rebuild path**, which only the coordinator should ever take.

**On Windows, add Git Bash** — `build-package.sh` is `/bin/sh` and is not being duplicated in PowerShell, because two parsers of `docs/package.map` would drift. Git for Windows supplies it, and the box needs Git to clone this anyway.

### 4.2 The seven steps, on every machine

```sh
# 1. THE TAG, NEVER A BRANCH.
git fetch --tags
git checkout vX.Y.Z

# 2. Configure. SDL3 MUST be found -- and for a PACKAGE it should be the static
#    build (3.2), not the system dylib. macOS also needs a deployment target,
#    or the binary will not start on anything older than this machine.
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<static SDL3 prefix> \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0        # macOS only
#    CHECK the configure output contains:
#        -- SDL3 found -- video boards enabled (windowed)
#    STOP if it instead says:
#        -- SDL3 not found -- video boards build headless (null display)
#    Install SDL3 and configure again. DO NOT CONTINUE -- a headless binary is
#    exactly what v0.2.0 shipped on all three platforms, and it looks fine.

# 3. Build.
cmake --build build --config Release --parallel

# 4. Prove the machine before it packages anything.
ctest --test-dir build -C Release -LE slow
#    CHECK: "100% tests passed"
#    STOP on any failure. Do not package a red tree.

# 5. Confirm the binary knows what it is.
./build/altairsim --version
#    CHECK: exactly   AltairSim X.Y.Z
#    STOP if you see a "-N-gsha" suffix (you are not on the tag) or
#    "(modified)" (the tree is dirty). Both mean the binary cannot be traced
#    to the release, which is the entire point of tagging first.

# 6. Package, with the manual handed in rather than rebuilt.
tools/build-package.sh --pdf <path to the coordinator's manual> --target <target>

# 7. Upload into the draft release.
gh release upload vX.Y.Z dist/altairsim-X.Y.Z-<target>.<ext>
```

**Record the SDL3 version you built against** — in the release notes, or wherever the release
is being tracked. Nothing enforces that the four machines agree (§3.1), so this is the only
record there will be, and it is the first thing to check if a video bug shows on one platform
and not another.

`<target>` is one of `macos-arm64`, `macos-x86_64`, `windows-x86_64`, `linux-x86_64`.

**IF ANY CHECK FAILS, STOP AND REPORT IT.** Do not upload, do not work around it, and do not decide it is probably fine. A package that reaches altairsim.com is one somebody downloads; there is no later gate.

### 4.3 Per-platform divergence

**macOS (both machines).** Identical steps on each; the targets differ. **Neither passes `-DCMAKE_OSX_ARCHITECTURES`** — each builds native for the machine it is on, which is the whole point of splitting. Passing it would produce a fat binary that Homebrew's single-arch SDL3 cannot link, which is the trap that made the split look mandatory in the first place.

**Linux.** Build on the **oldest glibc you can reasonably get** — a container or an older VM. A binary and a `.so` built on current Ubuntu will refuse to start on an older distro, and this is the one target where the choice of build host decides who can run the result. Everywhere else the host is incidental.

**Windows.** Steps 1–5 run in PowerShell. `--config Release` is load-bearing on the multi-config MSVC generator. The binary lands at `build\Release\altairsim.exe`, not `build\altairsim.exe` — **the script probes for it**, so step 6 needs no extra flag. **Step 6 runs in Git Bash**, not PowerShell, because the script is `/bin/sh`. **Windows has no `zip`** and Git Bash ships none; the script falls back to `Compress-Archive`, then to `bsdtar` (`tar.exe`, present since Windows 10 1803). **Which of the three actually fires here has never been observed** — report it. See §4.4, because the toolchain changes more than the paths.

### 4.4 Windows: MSVC, and only MSVC

**MSVC is the only supported Windows toolchain.** MinGW was considered and **declined** on 2026-07-20; `TODO.md` → *Declined* carries the reasoning so it does not get re-raised.

The short version: compiler diversity is already covered by Linux/GCC, macOS/Clang and Windows/MSVC, so MinGW-GCC would mostly duplicate Linux-GCC. Its one real advantage — easy fully-static linking — evaporated once `/MT` was shown to give MSVC a `.exe` with no redistributable requirement. And MSVC is the toolchain with evidence behind it: it is what the Windows CI leg builds on every push, and what `src/platform/win32/` is field-proven against — serial against two real FTDI ports, sockets against the real TCP stack, the terminal against a real console.

*Nothing in the source is knowingly MSVC-only (no `_MSC_VER`, lowercase Windows headers, a plain `main()`), so MinGW would very likely build. It is simply not tested, not documented and not shipped.*

#### No Developer shell is needed, and this matters for an assistant

**With the Visual Studio generator — CMake's default on Windows — nothing has to be set up
at all.** `cmake --build` invokes MSBuild, which locates the toolchain itself; `cl.exe`
never needs to be on `PATH`. **The proof is CI:** the Windows leg configures with
`shell: bash`, there is no `vcvars` step in any workflow, and it is a required check that
passes on every push.

*(`docs/building-windows.md` says to use a Developer shell. That is true for Ninja and
overstated for the default generator — see the note there.)*

**The real constraint is that an assistant's shell state does not persist between commands.**
Working directory carries over; environment variables do not. So "run `vcvarsall.bat`, then
build" is not a strategy — the second command starts with a clean environment. Plan for it:

| generator | setup needed | how to do it in one shot |
|---|---|---|
| **Visual Studio** (CMake's default) | **none** | `cmake -B build` then `cmake --build build --config Release` |
| Ninja | `vcvars` every time | `cmd /c "call vcvars64.bat && cmake --build build"` |

**Use the Visual Studio generator for the shipped build.** It requires no environment
manipulation, which is precisely what makes it safe to run unattended, and it is the
configuration CI already exercises.

**Neither row has been run interactively on the Windows box.** The first rests on CI rather
than on a person; the second rests on reading. `docs/building-windows.md` §6 is the job that
settles them, with the commands and a template for reporting back — **do that before the first
release build on Windows**, and correct this table with what it finds.

**THE RUNTIME DEPENDENCY IS WHAT BITES.** A package that requires the user to install something before it runs is a broken package. MSVC links the Visual C++ runtime dynamically by default, so the `.exe` wants `VCRUNTIME140.dll` and friends — present on most Windows 10 machines, absent on a clean one. **Set `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`** (static `/MT`) so the package carries no such requirement; static is the right default for something people download. **SDL3 must be built with the same setting** — see §3.1, because mixing `/MT` and `/MD` is a link error at best and two separate C runtime heaps at worst.

**The verification is not optional: run the packaged `.exe` on a Windows machine that has never had a compiler on it.** That is the only test separating "works on the build box" from "works". **Nothing has ever done this** — not CI, not the v0.2.0 process.

---

## 5. The release sequence

Steps 1–4 and 6 are the coordinator's. Step 5 is the four build machines.

**1. Bump the version.** `project(altairsim VERSION X.Y.Z …)` in `CMakeLists.txt` is the only place it lives; everything else derives from it or from `git describe`.

**2. Merge, then wait for the PDFs.** `docs.yml` rebuilds both PDFs on master and commits them as *"Rebuild the PDFs for `<sha>`"*. **Tag that commit**, not the merge — otherwise the tagged tree carries stale PDFs.

**3. Tag and push.** This fires `cpu-exerciser-release.yml`: 8080EXM, ZEXDOC and ZEXALL on all three CI platforms, roughly 15 billion instructions each. **Wait for it to go green before publishing anything.**

**4. Open the draft.** `gh release create vX.Y.Z --draft --notes-file <notes>`. The collection point has to exist before any machine starts building.

**5. Build, on all four machines.** §4.2. They are independent and can run in any order or at the same time.

**6. Publish.** When all four are uploaded and §7 passes: `gh release edit vX.Y.Z --draft=false`, then mirror the identical files to altairsim.com.

### The two traps that actually bit v0.2.0

**BUILD AT THE TAG, NOT AT THE MERGE.** v0.2.0's first set of binaries reported `AltairSim 0.2.0 (v0.1.0-86-g59dbba8)`. CI had built the merge commit — an ancestor of the tag — so `git describe` walked back and found the *previous* tag. They were rebuilt with `gh workflow run ci.yml --ref v0.2.0`. This is why step 5 of §4.2 exists: **run `--version` on the binary before packaging it, every time.** A correct one reads a bare `AltairSim X.Y.Z`, and `SHOW VERSION` says `tree clean`.

**THE MANUAL IN THE PACKAGE MUST BE THE ONE CI BUILT.** `build-package.sh` rebuilds the PDF with whatever pandoc is local. Homebrew ships 3.10; `docs.yml` pins 3.6; pandoc's HTML is the paginator's input, so *a different pandoc is a different document*. For v0.2.0 the fix was to restore CI's PDF over the script's output and confirm with `md5`. The `--pdf` flag in §4.2 exists so this cannot happen by accident.

---

## 6. Where the artifacts go

**A draft GitHub Release is the collection point.** No shared filesystem, no `scp`, no cloud folder — it works identically from all four operating systems, needs only `gh`, and nothing is visible to anyone until it is published.

```
   each machine
   dist/altairsim-X.Y.Z-<target>.<ext>        gitignored scratch
              |
              |  gh release upload
              v
   draft release vX.Y.Z                       the only place all four meet
              |
              |  gh release edit --draft=false
              v
     +------------------------+------------------------------+
     |                        |                              |
  GitHub Release          altairsim.com/download
  the permanent archive   the front door
```

**altairsim.com is the front door; the GitHub Release is the archive.** They serve the **identical files** — one build, uploaded twice. **They must never diverge.** Two artifacts both calling themselves `0.3.0` and differing by a byte is the worst outcome available here, because every bug report against either becomes untraceable.

Publish `SHA256SUMS` alongside the four so the two locations can be checked against each other, and so anyone downloading can check what they got. **Nothing produces checksums today** — see §8.

---

## 7. Verifying a package

**A PACKAGE IS PROVED BY UNPACKING IT SOMEWHERE `git rev-parse` FAILS, AND RUNNING IT THERE.** Not in `dist/`, not anywhere under the repository. A package that only works next to its own source tree is the failure this catches, and it is not hypothetical — see §3.2.

> **Test the ARCHIVE, never `dist/altairsim-<ver>/`.** That directory is `build-package.sh`'s staging area and holds whatever local `build/altairsim` existed when it ran — not the binary that ships. On 2026-07-20 it was a pre-tag, arm64-only, dynamically-linked build reporting `AltairSim 0.2.0 (v0.1.0-82-gb634269) (modified)` while the shipped archives correctly reported a bare `AltairSim 0.2.0`. It looks exactly like a package and the script's closing message points at it. Unpack the `.tar.gz`/`.zip` you are actually going to publish.

For each of the four:

```sh
# somewhere with no repository in sight
tar xzf altairsim-X.Y.Z-<target>.tar.gz && cd altairsim-X.Y.Z

./altairsim --version          # a bare "AltairSim X.Y.Z"
./altairsim -l                 # the built-in machines
./altairsim examples/cpm/cpm22-buffered.toml       # CP/M reaches A>
./altairsim examples/diskbasic/diskbasic.toml      # reaches MEMORY SIZE?
```

**And the check that would have caught the headless releases**, which is the one most likely to be skipped because everything above passes without it:

| | run | a STATIC build shows | a BUNDLED build shows | FAIL if it shows |
|---|---|---|---|---|
| macOS | `otool -L altairsim` | no SDL line at all | `@executable_path/…` | `/opt/homebrew/…` |
| Linux | `ldd altairsim` | no SDL line at all | a path resolving beside the binary | a distro path |
| Windows | `dumpbin /dependents altairsim.exe` | no `SDL3.dll` line | `SDL3.dll`, present beside the `.exe` | `SDL3.dll` absent from the package |

**`build-package.sh` now refuses to package a binary that fails either of these** (2026-07-20). It asks the binary — `SHOW VERSION` carries a `video` row — so the check is the same on all four machines and needs no toolchain, which matters because Git Bash on Windows has no `nm`. It also rejects an SDL path that is absolute rather than relative to the binary, where `otool`/`ldd` exist; **on Windows it says it could not check** rather than passing quietly. The checks below are still worth running on the unpacked archive, because they test the artifact rather than the staging input.

**"No SDL line at all" is the pass condition for a static build and the failure condition for a headless one** — they are indistinguishable in the table above, so a second check is required:

```sh
nm altairsim | grep -c SDL_        # static: thousands.  headless: 0.
```

**Use `nm`, not `strings`.** `SDL_CreateWindow` is a symbol, not a string literal, so
`strings altairsim | grep SDL_CreateWindow` reports **0 on a correct static build** — a check
that fails the thing it is meant to pass. (This document said `strings` until 2026-07-20, when
running it against a known-good static binary produced the false negative.)

**Also confirm the deployment target** on macOS: `vtool -show-build altairsim | grep minos` should report the floor you set, not the build machine's OS.

Simpler than any of the above, and the one to reach for first — **ask the binary**:

```sh
./altairsim -n -x 'SHOW VERSION'      # the `video` row: "SDL3 -- windowed", or
                                      # "none -- headless (null display)"
```

**Nothing said this before 2026-07-20.** `--version`, `--help`, `-l`, `SHOW VERSION` and `SHOW DISPLAY` were byte-identical between a windowed build and a headless one, so the only way to tell was `nm`/`otool`/`dumpbin` on the file — and `SHOW DISPLAY`'s *"no video service in this build"* was dead code that never fired, because `main.cpp` hands a headless build a non-null `NullDisplay`. A string that claims to be the discriminator and is not is worse than none, which is the same lesson as the phantom `manual.cmake`.

Then **open a window**: run `altairsim sol20` or `altairsim vdm1` and confirm one actually appears. `SHOW DISPLAY` reports whether the window has the keyboard. **This is still an eyeball check and cannot be automated** — the `video` row says SDL3 is compiled in, not that a window reached a screen.

---

## 8. What is not automated

Stated plainly, because a design document that describes a process nobody has automated should not read as though it has been.

- **No workflow runs `tools/build-package.sh`.** It is invoked by hand, by a person, on four machines.
- ~~**`build-package.sh` does not yet take `--target` or `--pdf`.**~~ **Both exist now** (2026-07-20). `--target` names the archive and picks its format (`.tar.gz`, `.zip` for Windows), defaulting to the detected host; `--pdf` takes the coordinator's manual and skips `build-docs.sh` entirely, so pandoc never runs on a secondary machine. Staging moved to `dist/altairsim-<ver>-<target>/`, and the script clears stale siblings — the un-suffixed directory was the footgun §7 warns about. It is still `/bin/sh`: **on Windows it runs under Git Bash**, and the `.zip` branch falls back from `zip` to `Compress-Archive` to `bsdtar`, because Git Bash ships no `zip`. **None of this has run on Windows or Linux yet** — only macOS/arm64.
- **Nothing copies SDL3 into the package or fixes the install names — and under §3.2 there is nothing to copy.** Static linking is the answer precisely because it deletes this step: no dylib, no `install_name_tool`, no `@rpath`. This bullet stands only for the dynamic fallback in §3.2's table, which no machine uses today.
- **Nothing produces `SHA256SUMS`.**
- **Nothing assembles the package and runs the manual's own commands against it.** `docs/package.map`'s own header says so, and names a `tests/acceptance/manual.cmake` that has never existed — a claimed test being worse than a missing one.
- **Nothing has ever run a shipped `.exe` on a clean Windows box.**
- **`tools\build-sdl3-static.bat` has never been run.** It was written from the working shell script. `docs/building-windows.md` §6 is the job that settles it.

`TODO.md` is the live index of these; this list is a snapshot of why they matter, not a substitute for it.
