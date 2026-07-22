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
altairsim-changelog.pdf               the release history (docs/changelog/, built by CI)
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
Windows   tools\build-sdl3-static.bat   -- %USERPROFILE%\opt\sdl3-static
```

**Both scripts pin the same SDL3 version**, build it static, install to a fixed prefix, and are a no-op on a second run. That pin is the only thing making four independently-maintained machines agree — change it deliberately, and rerun the script everywhere when you do.

**The `.bat` is verified** (Windows 10 / MSVC 2022, from scratch: exit 0, ~3.5 min, valid `SDL3-static.lib`; see §8 and `docs/building-windows.md` §6). **It is a `.bat` and not a `.ps1` on purpose:** PowerShell's execution policy blocks unsigned scripts by default, so a freshly-cloned `.ps1` will not run until the user changes a machine setting. `curl.exe` and `tar.exe` have shipped in Windows since 10 1803, so it needs nothing installed but CMake.

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
every machine     a C++20 compiler and CMake >= 3.20
                  SDL3 for that platform (the static build, §3)
                  git

a WORKER only     git reaches GitHub over ANONYMOUS https -- no login, no token, ever
                  an ssh client, to scp the finished archive to the coordinator
                    (Windows: scp.exe ships in Windows 10)

the COORDINATOR   git authenticated for push, and gh authenticated -- it does ALL GitHub work
(this box only)   Remote Login (sshd) ON, so the three workers can scp their archives in
```

**A worker holds no credentials.** It clones the public repo over `https://` with no authentication, builds, and hands its one archive back over `scp`. It never logs in to GitHub, never holds a token, and never runs `gh`. **Everything that touches the repository with credentials — the tag, the push, the draft, every upload, the publish — happens on the coordinator, and only there.** A worker's sole outbound credential is the ssh key that lets it `scp` to this box; it is powerless against the repository. (Posture set 2026-07-21.)

**No pandoc, no Chrome, no poppler on any machine.** The manual and changelog PDFs are built once by CI and committed at the tag (§5 step 2), so every checkout already carries `docs/altairsim-manual.pdf` and `docs/altairsim-changelog.pdf`; `build-package.sh` is invoked with `--pdf` for the manual and copies the changelog straight from the tree, rather than rebuilding either. Without `--pdf` the script calls `build-docs.sh`, which needs **pandoc pinned at 3.6** (Homebrew ships 3.10, and a different pandoc is a different document), a Chromium browser as the paginator, and `pdffonts` for the font check — so four machines rebuilding it independently means four subtly different manuals. With `--pdf` none of that runs and the packaged PDF is byte-identical to CI's; **the script warns loudly on the rebuild path**, which only the coordinator should ever take.

**On Windows, add Git Bash** — `build-package.sh` is `/bin/sh` and is not being duplicated in PowerShell, because two parsers of `docs/package.map` would drift. Git for Windows supplies it, and the box needs Git to clone this anyway.

### 4.2 The seven steps, on every machine

**For these same steps filled in with each box's real paths, flags and target — the version to
run at release time, plus a diagram of the whole flow — see §4.5.**

```sh
# 1. THE SOURCE, AT THE TAG, NEVER A BRANCH.
#    A WORKER clones the PUBLIC repo over anonymous https -- no login, no token:
#        git clone https://github.com/deltecent/altairsim.git      # first time only
#    Its remote must be the https URL, so fetch needs no credential. The COORDINATOR
#    uses its own authenticated tree; only workers use anonymous https.
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

# 7. DELIVER the archive. A WORKER never touches GitHub -- it scps its one archive into the
#    coordinator's repo dist/. build-package.sh's cleanup is scoped to its OWN target, so a
#    sibling platform's already-delivered archive is not wiped (see the script, DISTRIBUTION.md 6):
scp dist/altairsim-X.Y.Z-<target>.<ext> patrick@dist.altairsim.com:~/src/altairsim/dist/
#    The COORDINATOR's own archive is already in that dist/ (it built there); it uploads all
#    four with gh (§5 step 6).
```

**Record the SDL3 version you built against** — in the release notes, or wherever the release
is being tracked. Nothing enforces that the four machines agree (§3.1), so this is the only
record there will be, and it is the first thing to check if a video bug shows on one platform
and not another.

`<target>` is one of `macos-arm64`, `macos-x86_64`, `windows-x86_64`, `linux-x86_64`.

**IF ANY CHECK FAILS, STOP AND REPORT IT.** Do not deliver it (no `scp`, no upload), do not work around it, and do not decide it is probably fine. A package that reaches altairsim.com is one somebody downloads; there is no later gate.

### 4.3 Per-platform divergence

**macOS (both machines).** Identical steps on each; the targets differ. **Neither passes `-DCMAKE_OSX_ARCHITECTURES`** — each builds native for the machine it is on, which is the whole point of splitting. Passing it would produce a fat binary that Homebrew's single-arch SDL3 cannot link, which is the trap that made the split look mandatory in the first place.

**Linux.** Build on the **oldest glibc you can reasonably get** — a container or an older VM. A binary and a `.so` built on current Ubuntu will refuse to start on an older distro, and this is the one target where the choice of build host decides who can run the result. Everywhere else the host is incidental.

**Windows.** Steps 1–5 run in PowerShell. `--config Release` is load-bearing on the multi-config MSVC generator. The binary lands at `build\Release\altairsim.exe`, not `build\altairsim.exe` — **the script probes for it**, so step 6 needs no extra flag. **Step 6 runs in Git Bash**, not PowerShell, because the script is `/bin/sh`. **Windows has no Info-ZIP `zip`** and Git Bash ships none — and its `tar` is GNU tar, which cannot write zip at all. The script now makes the `.zip` with the **Windows-native bsdtar** (`%SystemRoot%\System32\tar.exe --format zip`), which emits a conformant, forward-slash archive; Compress-Archive is a last resort only, because Windows-10 PowerShell 5.1 writes backslash separators that Unix `unzip` cannot extract (observed and fixed 2026-07-21). See §4.4.

### 4.4 Windows: MSVC, and only MSVC

**MSVC is the only supported Windows toolchain** — it is what the Windows CI leg builds on every push, and what `src/platform/win32/` is field-proven against: serial against two real FTDI ports, sockets against the real TCP stack, the terminal against a real console.

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

### 4.5 The four boxes, concretely — what you actually type

**One coordinator, three workers.** The coordinator (this box) is the only machine with
credentials — it tags, pushes, and does every `gh` operation. The three workers hold nothing:
they clone the **public** repo over **anonymous `https://`**, build, and `scp` their one archive
to the coordinator's repo **`dist/`** (`~/src/altairsim/dist`) — reached at
**`dist.altairsim.com`**, this box's public name (its LAN address is DHCP-assigned and floats, so
always use the name, never a raw IP). §4.2 is the same seven steps on all four; only the per-box
specifics differ — the **SDL3 prefix**, the **platform flags**, the **`--target`**, and, for a
worker, the **`scp` back**. Filled in with the project's real paths (verified 2026-07-21):

**THREE OF THE FOUR BOXES ARE ONE PHYSICAL MACHINE — build the x86 targets serially (2026-07-21).**
The Intel Mac is the physical host; the **Windows box and the Linux box are both VMware guests
running on that same Intel Mac.** So `macos-x86_64`, `windows-x86_64` and `linux-x86_64` share one
CPU and one pool of RAM, and running their builds at the same time only makes them contend — do
them **one at a time**. `macos-arm64` (the coordinator) is the only box that is genuinely separate
and can run in parallel with any of them. This is why §5 step 5 says "any order" but not "all at
once".

```
                 ┌─────────────────────────────────────────────────┐
                 │  COORDINATOR — macOS Apple Silicon (this box)    │
                 │  dist.altairsim.com · only box with credentials  │
                 │  §5: bump·merge·wait for CI PDFs·tag·push        │
                 │      gh release create --draft                   │
                 │  builds macos-arm64 straight into repo dist/     │
                 └───────────────────────┬─────────────────────────┘
              the coordinator's push puts the source, at the tag, on github.com
     workers read it over ANONYMOUS https │ (public repo — no login, no token on a worker)
                                          ▼
   ┌───────────────────────┬──────────────────────────┬───────────────────────┐
   │ Intel Mac   ssh .22   │ Windows 10   VM on .22    │ Ubuntu Linux ssh .246 │
   │ build macos-x86_64    │ build windows-x86_64      │ build linux-x86_64    │
   │        .tar.gz        │        .zip               │        .tar.gz        │
   └───────────┬───────────┴─────────────┬────────────┴───────────┬───────────┘
               └─────────── scp → coordinator dist/ ───────────┘
                                          ▼
                 the repo's dist/ on the coordinator — all four collect here
                                          │  gh release upload   (coordinator only, §6)
                                          ▼
                    draft release vX.Y.Z  →  publish  →  GitHub + altairsim.com
```

**Two things are the same on every box.** (1) After `git checkout vX.Y.Z` the tree already holds
CI's manual at `docs/altairsim-manual.pdf` (§5 step 2 tags the commit CI built it in), so `--pdf
docs/altairsim-manual.pdf` uses the one correct PDF and pandoc never runs. (2) Every archive
lands in the repo's `dist/` — which is also the **collection point**: the three workers `scp`
their archive here and the coordinator builds `macos-arm64` here. `build-package.sh` used to wipe
the whole `dist/altairsim-*` each run; its cleanup is now **scoped to the target it builds**
(2026-07-21), so packaging one platform never deletes another's already-delivered archive.

**Box 1 — macOS Apple Silicon** *(the coordinator; also builds `macos-arm64`)*. Uses its own
authenticated tree.
```sh
git fetch --tags && git checkout vX.Y.Z
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$HOME/opt/sdl3-static" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -LE slow            # CHECK: 100% tests passed
./build/altairsim --version                           # CHECK: bare "AltairSim X.Y.Z"
tools/build-package.sh --pdf docs/altairsim-manual.pdf --target macos-arm64
# its archive is now in dist/ beside the workers' deliveries; all four upload in §5 step 6
```

> **Start from a clean `build/`.** This box's day-to-day `build/` is configured against
> Homebrew's *dynamic* SDL3. CMake caches that `SDL3_DIR`, and re-running the configure above
> does **not** override it — the build links `/opt/homebrew/.../libSDL3.0.dylib`, and
> `build-package.sh` refuses it (an absolute path that starts on no other Mac). So `rm -rf build`
> before the configure. A *clean* configure prefers the static prefix correctly — verified on the
> 2026-07-21 dry run, where the stale-cache build was caught by exactly that refusal.

**Box 2 — macOS Intel worker** *(`ssh patrick@192.168.94.22`)*. Same prefix and `11.0` floor as
Box 1 — only the target and the delivery differ.
```sh
git clone https://github.com/deltecent/altairsim.git   # first time only; no login, no token
git fetch --tags && git checkout vX.Y.Z
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$HOME/opt/sdl3-static" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -LE slow
./build/altairsim --version
tools/build-package.sh --pdf docs/altairsim-manual.pdf --target macos-x86_64
scp dist/altairsim-X.Y.Z-macos-x86_64.tar.gz patrick@dist.altairsim.com:~/src/altairsim/dist/
```

> **Driving this box over ssh? Put `cmake` on `PATH` yourself.** A non-login shell
> (`ssh … 'bash -s'`) does not read the profile that adds `/usr/local/bin`, so `cmake` is not
> found and the configure fails at line 1. Prefix the remote script with
> `export PATH="/usr/local/bin:$PATH"` (verified 2026-07-21). At the machine's own terminal this
> does not arise, and Linux is unaffected — its `cmake` is in `/usr/bin`, always on `PATH`.

**Box 3 — Windows 10 worker** *(VMware guest on the Intel Mac)*. Steps 1–5
in **PowerShell**; steps 6–7 in **Git Bash**. `MultiThreaded` and `--config Release` are
load-bearing (§4.4); the binary lands in `build\Release\`. `scp.exe` ships in Windows 10 and
reaches the coordinator over the LAN.

> **PROVEN end-to-end, 2026-07-21.** This leg now matches the other three: native MSVC build
> (static SDL3 + static `/MT` CRT), 17/17 tests, `dumpbin /dependents` showing system DLLs only
> (no `SDL3.dll`, no `VCRUNTIME140`), a conformant `.zip`, and `scp` outbound into the
> coordinator's `dist/`. Three findings worth keeping:
> **(1) The `.zip` archiver bit hard.** In Git Bash `zip` is absent and `tar` is GNU tar, which
> cannot write zip at all; PowerShell 5.1's Compress-Archive writes BACKSLASH separators that
> pass `unzip -t` but FAIL extraction on any Unix `unzip`. `build-package.sh` now emits the zip
> with the Windows-native bsdtar (`%SystemRoot%\System32\tar.exe --format zip`), so step 6 needs
> nothing extra — but do not "fix" a broken zip by reaching for Compress-Archive.
> **(2) Delivery is `scp.exe` outbound** and needs only a deploy key (`~/.ssh/altairsim_deploy`,
> its public half in the coordinator's `authorized_keys`) — no inbound `sshd`. The guest also
> accepts inbound ssh now (offline Win32-OpenSSH; an admin key lives in
> `%ProgramData%\ssh\administrators_authorized_keys` with an ACL of exactly {Administrators,
> SYSTEM}, or sshd silently ignores it), which lets the coordinator drive the whole leg — but a
> release needs only the outbound path.
> **(3) The walls, if you drive it by hand:** elevation loses mapped drives (use the
> `\\vmware-host\Shared Folders\…` UNC path); the firewall has three profiles, not one;
> `Expand-Archive` denies `_manifest`, so extract with `tar.exe`.
```powershell
# --- PowerShell ---   (first time only:  git clone https://github.com/deltecent/altairsim.git)
git fetch --tags; git checkout vX.Y.Z
cmake -B build -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH="$env:USERPROFILE\opt\sdl3-static" `
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -LE slow
.\build\Release\altairsim.exe --version
```
```sh
# --- Git Bash, from the repo root ---
tools/build-package.sh --pdf docs/altairsim-manual.pdf --target windows-x86_64
scp dist/altairsim-X.Y.Z-windows-x86_64.zip patrick@dist.altairsim.com:~/src/altairsim/dist/
```

**Box 4 — Ubuntu Linux worker** *(`ssh patrick@192.168.94.246` when up; the 22.04 VM — oldest
glibc here, §4.3)*. Needs the X11 dev headers installed once (`libxtst-dev` and friends) or SDL3
will not configure.
```sh
git clone https://github.com/deltecent/altairsim.git   # first time only; no login, no token
git fetch --tags && git checkout vX.Y.Z
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOME/opt/sdl3-static"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -LE slow
./build/altairsim --version
tools/build-package.sh --pdf docs/altairsim-manual.pdf --target linux-x86_64
scp dist/altairsim-X.Y.Z-linux-x86_64.tar.gz patrick@dist.altairsim.com:~/src/altairsim/dist/
```

> **One-time setup for the scp path (proven coordinator↔Intel-Mac↔Linux 2026-07-21; Windows
> still pending).**
> **On the coordinator:** turn **Remote Login** ON (System Settings → General → Sharing), and add
> each worker's delivery public key to `~/.ssh/authorized_keys`. The collection dir needs nothing:
> `dist/` is **tracked** (via `dist/README.md`), so every checkout already has it — a worker can
> deliver before the coordinator has built — and `build-package.sh`'s cleanup is scoped to the
> target it builds, so a delivered sibling archive is never wiped.
> **On each worker:** point `origin` at the https URL (`git remote set-url origin
> https://github.com/deltecent/altairsim.git`) so fetches need no credential, and give it a
> **passphrase-less** ssh key dedicated to this — the scp runs unattended, so a
> passphrase-protected key will not do. All three workers — Mac, Linux and Windows — use a
> dedicated `altairsim_deploy` ed25519 key; the shared passphrase-protected key only worked on
> the Intel Mac by accident of macOS Keychain unlocking it, and not on Linux at all — so a
> dedicated key is the uniform rule. Point `~/.ssh/config` at it — `Host dist.altairsim.com` /
> `IdentityFile ~/.ssh/altairsim_deploy` / `IdentitiesOnly yes` — so the plain `scp` above uses
> it and only it. That key is a worker's ONLY credential: it writes to `dist/` and nothing else,
> and grants no GitHub access at all.
> **The full build→package→deliver→collect dry run passed on ALL FOUR boxes (2026-07-21): each
> built native, the three workers `scp`'d their archive into the coordinator's `dist/`, and all
> four collected there with no sibling wiped.** The Windows leg is proven to the same bar as the
> others (§4.5 Box 3) — self-contained `.exe`, conformant `.zip` — so v0.3.0 ships four platforms.

---

## 5. The release sequence

Steps 1–4 and 6 are the coordinator's. Step 5 is the four build machines — the three workers `scp` their archives to the coordinator, which alone talks to GitHub.

**1. Bump the version.** `project(altairsim VERSION X.Y.Z …)` in `CMakeLists.txt` is the only place it lives; everything else derives from it or from `git describe`.

**2. Merge, then wait for the PDFs.** `docs.yml` rebuilds all three PDFs on master — the manual, the **changelog** (`docs/changelog/`), and the developer guide — and commits the ones that changed as *"Rebuild the PDFs for `<sha>`"*. **Tag that commit**, not the merge — otherwise the tagged tree carries a stale manual, or no changelog at all. The manual is handed to `build-package.sh` with `--pdf`; the changelog needs no flag — the script copies `docs/altairsim-changelog.pdf` straight from the tagged tree (it is a committed artifact, same as the manual, and it must not go through the token-substitution loop).

**3. Tag and push.** This fires `cpu-exerciser-release.yml`: 8080EXM, ZEXDOC and ZEXALL on all three CI platforms, roughly 15 billion instructions each. **Wait for it to go green before publishing anything.**

**4. Open the draft.** `gh release create vX.Y.Z --draft --notes-file <notes>`. The collection point has to exist before any machine starts building.

**5. Build, on all four machines.** §4.2 / §4.5. Independent, and any order — but **build the three x86 targets one at a time, not all at once.** The Intel Mac, the Windows box and the Linux box are **one physical machine** — the Intel Mac is the host, and Windows and Linux are VMware guests on it (§4.5) — so building `macos-x86_64`, `windows-x86_64` and `linux-x86_64` concurrently just makes them fight for one CPU and one pool of RAM. Only `macos-arm64` (the M4 coordinator, a genuinely separate machine) can build in parallel with them. **The three workers `scp` their archive into the coordinator's repo `dist/` (over `dist.altairsim.com`); the coordinator builds `macos-arm64` straight into the same `dist/`.** No worker authenticates to GitHub.

**6. Upload all four, then publish — all on the coordinator.** When `dist/` holds all four and §7 passes: `gh release upload vX.Y.Z dist/altairsim-X.Y.Z-*.tar.gz dist/altairsim-X.Y.Z-*.zip`, then `gh release edit vX.Y.Z --draft=false`, then mirror the identical files to altairsim.com. **Only the coordinator has GitHub credentials.**

### The two traps that actually bit v0.2.0

**BUILD AT THE TAG, NOT AT THE MERGE.** v0.2.0's first set of binaries reported `AltairSim 0.2.0 (v0.1.0-86-g59dbba8)`. CI had built the merge commit — an ancestor of the tag — so `git describe` walked back and found the *previous* tag. They were rebuilt with `gh workflow run ci.yml --ref v0.2.0`. This is why step 5 of §4.2 exists: **run `--version` on the binary before packaging it, every time.** A correct one reads a bare `AltairSim X.Y.Z`, and `SHOW VERSION` says `tree clean`.

**THE MANUAL IN THE PACKAGE MUST BE THE ONE CI BUILT.** `build-package.sh` rebuilds the PDF with whatever pandoc is local. Homebrew ships 3.10; `docs.yml` pins 3.6; pandoc's HTML is the paginator's input, so *a different pandoc is a different document*. For v0.2.0 the fix was to restore CI's PDF over the script's output and confirm with `md5`. The `--pdf` flag in §4.2 exists so this cannot happen by accident.

---

## 6. Where the artifacts go

**The coordinator is the single collection point; the draft GitHub Release is where the artifacts become public.** The workers have no GitHub credentials, so they do not upload — each `scp`s its one archive into the coordinator's repo `dist/` (over `dist.altairsim.com`), the coordinator builds `macos-arm64` into the same `dist/`, and then the coordinator alone pushes all four to the draft release. That `dist/` is the repo's own gitignored scratch dir; `build-package.sh`'s cleanup is scoped to the target it builds, so collecting siblings there is safe across re-runs.

```
   worker (x3)                               coordinator (this box)
   packages its target into dist/            packages macos-arm64 into dist/
              |                                        |
              |  scp -> dist.altairsim.com             | (already local)
              +------------------+---------------------+
                                 v
                 the repo's dist/  —  all four collect here
                                 |
                                 |  gh release upload      <- coordinator only
                                 v
                       draft release vX.Y.Z
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

Then **open a window**: run `altairsim vdm1`, or `altairsim sol20` — **on `sol20`, press `^E` first**, because it boots into SOLOS with `startup = ["RUN C000"]` and typing a monitor command at it just sends the text to the guest, which echoes it onto the video screen. That looks exactly like a layering bug and is not one (observed and chased down on the Intel Mac, 2026-07-20; the tell is that the monitor prints *no* output at all).

`SHOW DISPLAY` reports one property, **`focus`** — and it is **not** the "does the window have the keyboard" answer, despite reading like one. It is `Display::focusPolicy_` (`src/host/display.h`), a session *policy*, default `false`, set by `SET DISPLAY focus=on` or `[display]` in a machine file, governing whether the window takes the keyboard **when it opens**. `src/cli/commands.cpp` states it: *"the video window takes the keyboard, not the terminal"*. It does not track where the keyboard is now, and clicking between applications never changes it. **Nothing reports live window focus.**

Reading it as a live report is a mistake that has now been made twice — once by the Intel Mac over a pipe, and once in the job sheet that corrected it. The keyboard question is answered by **H3**, by typing into the window and watching characters appear; there is no property that answers it. (Established at the machine, 2026-07-20.)

**This is an eyeball check and cannot be automated.** The `video` row says SDL3 is compiled in; it does not say a window reached a screen. **Give it to a human, with a stated pass/fail per item** — an assistant driving a pipe cannot click a window, and will otherwise report a `focus` value it has no way to interpret.

#### The human checklist

Written so somebody who does not know the codebase can execute it cold and answer yes or no. Proposed from the Intel Mac, 2026-07-20, after that machine verified an SDL window over a pipe and reported a `focus` value it had no business trusting.

**Corrected 2026-07-20, at the machine, on the first run of this table.** Four of the six checks were wrong: H2 named a machine that halts before you can look, H4 tested a value that cannot behave as described, H5 named a value that does not exist and omitted the resume, and H6 had no observations. The code was right in every case the table was wrong. **A checklist nobody has executed is a draft**, and this one shipped into a job sheet as though it were verified.

| | do this | pass is |
|---|---|---|
| **H1** | `altairsim vdm1` | a window opens showing `PROCESSOR TECHNOLOGY VDM-1 READY` in a blocky font, **sharp-edged** — it scales nearest-neighbour, so blurring means the scaler is wrong |
| **H2** | `altairsim sol20` — **not `vdm1`**; wait for the SOLOS `>` in the window | a visible cursor, blinking at roughly one cycle a second, steady rather than stuttering |
| **H3** | `altairsim sol20`, wait for the SOLOS `>` **in the window**, click it, type `HELLO` **into the window** | the characters appear in the window. **A pipe cannot do this one** — it is the whole reason this table exists. This is also the only evidence that the window receives the keyboard; no property reports it |
| **H4** | — | **STRUCK.** It asked for `focus` to read `true` then `false` as you clicked between applications. `focus` is a launch policy and cannot do that; see the paragraph above. Whether `SET DISPLAY focus=on` actually causes a window to open focused is **untested** — write a check for it before trusting it |
| **H5** | `sol20` or `vdm1` **running**, `^E`, `SET vdm0 video=reverse`, `R`; then `^E`, `SET vdm0 video=normal`, `R` | the window flips dark-on-light and back. The value is **`reverse`** — there is no `inverse`. The `R` matters: a stopped machine does not redraw |
| **H6** | with `sol20` **running**, click the window's close button | the monitor prints `window closed -- the machine is still at <PC>. RUN resumes; QUIT exits.` and gives you the prompt, **with the window still open**. That is the design, not a bug — see below |

**H2 and H5 need a RUNNING machine, and `vdm1` is not one.** Its demo `HLT`s after drawing — `machines/vdm1.toml` says so — and *"the window is live while a program RUNs"*. Point anything that needs live pixels at `sol20`, whose SOLOS loops. Three of the six checks originally tripped over this.

**H6 was the open question. It is now answered — the running case is deliberate, and the stopped case is a real limitation.** Observed at the machine on the Intel Mac, 2026-07-20, then read back to the source:

- **`sol20`, running:** close prints `window closed …` and returns to the monitor, **and the window stays open.** This is **intended**. `src/cli/monitor.cpp` says so at the `StopReason::WindowClosed` arm: *closing the window is not quitting* — it stops the guest and hands back the prompt, leaving the machine untouched and the window there to `RUN` back into. The message names the **event**, not a state of the window, and its own second half says the machine is intact and how to actually leave. **Reading only the first two words makes it look like output asserting something that did not happen; the whole line does not.** Quote it in full before judging it.
- **Machine not running:** the close button is **disabled**, and at a `HLT` that is permanent — the window can never be closed. **This one is a genuine limitation**, and it is the same root cause as H2 and H5: `SdlDisplay::pollEvents()` is called *by the run loop*, so with the CPU stopped nothing drains the SDL queue and the OS sees an unresponsive window.

**The Intel Mac's inference was right, and it explains three checks at once.** *"The window is live while a program RUNs"* (`machines/vdm1.toml`) is not a remark about the demo — it is the event pump's actual scope. H2's absent blink, H5's un-rendered flip and H6's dead close button are one fact, not three.

---

## 8. What is not automated

Stated plainly, because a design document that describes a process nobody has automated should not read as though it has been.

- **No workflow runs `tools/build-package.sh`.** It is invoked by hand, by a person, on four machines.
- ~~**`build-package.sh` does not yet take `--target` or `--pdf`.**~~ **Both exist now** (2026-07-20). `--target` names the archive and picks its format (`.tar.gz`, `.zip` for Windows), defaulting to the detected host; `--pdf` takes the coordinator's manual and skips `build-docs.sh` entirely, so pandoc never runs on a secondary machine. Staging moved to `dist/altairsim-<ver>-<target>/`, and the script clears stale siblings — the un-suffixed directory was the footgun §7 warns about. It is still `/bin/sh`: **on Windows it runs under Git Bash**, and the `.zip` branch falls back from `zip` to `Compress-Archive` to `bsdtar`, because Git Bash ships no `zip`. **This has now run on all three non-arm64 hosts** (2026-07-20): Linux produced a `.tar.gz` and Windows a `.zip` — on Windows via `powershell.exe Compress-Archive`, since Git Bash has no `zip` and its `tar` is not bsdtar. Both packages are self-contained (Linux `ldd`/Windows `dumpbin` show no SDL beside the binary, and the Windows `.exe` needs no VC++ redistributable), and CP/M boots from each unpacked outside its repo. **The human video check (§7) has now been run on all four platforms** — both Macs, Linux (XWayland), and a Windows 10 VMware guest — each opening a real VDM-1 window; H1–H6 pass everywhere.
- **Nothing copies SDL3 into the package or fixes the install names — and under §3.2 there is nothing to copy.** Static linking is the answer precisely because it deletes this step: no dylib, no `install_name_tool`, no `@rpath`. This bullet stands only for the dynamic fallback in §3.2's table, which no machine uses today.
- **Nothing produces `SHA256SUMS`.**
- **Nothing assembles the package and runs the manual's own commands against it.** `docs/package.map`'s own header says so, and names a `tests/acceptance/manual.cmake` that has never existed — a claimed test being worse than a missing one.
- ~~**Nothing has ever run a shipped `.exe` on a clean Windows box.**~~ **The shipped v0.3.0 `.exe` is proven self-contained (2026-07-22, on the Windows box).** `altairsim-0.3.0-windows-x86_64.zip` was extracted to a fresh directory and run: `--version` → `AltairSim 0.3.0`, `SHOW VERSION` → `video SDL3 -- windowed`. The archive bundles no `SDL3.dll` and no `VCRUNTIME140`, and `dumpbin /dependents` on the `.exe` lists only base Windows 10 system DLLs (KERNEL32, USER32, GDI32, WINMM, IMM32, ole32, OLEAUT32, VERSION, ADVAPI32, SETUPAPI, SHELL32, WS2_32). **One caveat keeps this from being *fully* closed:** the check ran on the **build** box, which has the toolchain — a run on a genuinely pristine machine (no Visual Studio, no VC++ redistributable) is still nominally unproven. But self-containedness — static SDL3, static `/MT` CRT, zero non-system imports — is exactly what a clean-box run tests, and it holds.
- **The workers deliver by `scp`.** By design (2026-07-21) only the coordinator holds GitHub credentials; the workers clone the public repo over anonymous `https://` and `scp` their archive into the coordinator's repo `dist/` (`dist.altairsim.com`), which then uploads all four. Nothing automates the scp, the collection, or the four-at-once upload — that is the manual part of §4.5. The scp path — anonymous https, `dist.altairsim.com` resolution, a passphrase-less delivery key, and `dist/` existing at checkout — is now **proven on all four boxes** (2026-07-21), Windows included: the full build→package→deliver→collect dry run landed four conformant archives in the coordinator's `dist/` with no sibling wiped. See §4.5.
- ~~**`tools\build-sdl3-static.bat` has never been run.**~~ **Run and verified end-to-end.** This bullet contradicted `docs/building-windows.md` §6, which recorded a first successful run on 2026-07-20; that run is confirmed and repeated. A fresh-from-scratch run on 2026-07-22 completed **exit 0 in ~3.5 min** — fetch SDL3 3.4.12, MSVC static build, install into a throwaway prefix — producing `SDL3-static.lib` (13 MB), the SDL3 headers, `cmake/SDL3Config.cmake`, and the version marker; the idempotent path (reads the `.altairsim-sdl3-version` marker, reports 3.4.12 already installed) works too. The `.bat`'s own `*** UNVERIFIED … never run on Windows` banner is corrected, and §6's "written, never run" leftover with it.

The GitHub issue tracker is the live index of open work; this list is a snapshot of why these gaps matter, not a substitute for it.
