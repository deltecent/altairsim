# `altairsim` — Distribution

**How a release is built, and why it is built that way.** The process is run by hand on four
machines; nothing here is automated (§8).

---

## 0. What this is, and where the rest lives

This document is the **reasoning**. The **procedure**, as steps to run, is in three skills,
which an assistant loads when asked for the matching job:

| skill | who runs it | what it covers |
|---|---|---|
| `.claude/skills/release-worker/` | every build machine | build, test, package and deliver one archive (§4.2) |
| `.claude/skills/release-coordinator/` | the coordinator only | version, changelog, tag, draft, drive the builds, checksum, upload, publish (§5) |
| `.claude/skills/release-verify/` | anyone, on each archive | prove the archive works as downloaded, and the human window checklist (§7) |

**The facts of one site** (ssh addresses, checkout paths, which boxes share a physical machine,
serial port names) are in **`distribution.conf`** at the repo root. It is gitignored; copy
`distribution.conf.example` and fill it in. Nothing in this document or the skills names a
particular network.

The skills are written for **an assistant on a build machine that has never seen this
repository**. That is why every step has the exact string to check for and a **STOP**: a step
that says "make sure it worked" is useless to that reader. v0.2.0 shipped broken because the
configure line saying whether video was enabled was printed on every build and nobody read it.

**This is not the package contents.** `docs/package.map` is the single source of truth for what
goes in the archive, and `docs/manual/package.md` is the reader's view of it. If they disagree,
the map wins.

---

## 1. The four packages

```
altairsim-X.Y.Z-macos-arm64.tar.gz      Apple Silicon
altairsim-X.Y.Z-macos-x86_64.tar.gz     Intel Mac
altairsim-X.Y.Z-windows-x86_64.zip      Windows 10+, built with MSVC
altairsim-X.Y.Z-linux-x86_64.tar.gz     Linux, built against the oldest glibc available
```

**macOS is split on purpose.** A universal build is possible (SDL's `SDL3.xcframework` has a
real `macos-arm64_x86_64` slice), but splitting halves the download and means **each slice is
built and tested on the hardware it targets**. A universal binary from an arm64 runner has an
`x86_64` half that nobody ever ran.

---

## 2. What is inside one

Whatever carries a `DIR` or `FILE` line in `docs/package.map`. The shape:

```
altairsim[.exe]                       the program
QUICK-START.pdf                       CP/M in one command
altairsim-manual.pdf                  the User Manual
altairsim-changelog.pdf               the release history
altairsim-monitor.pdf                 the altairsim> prompt
altairsim-debugger.pdf                debugging from that prompt
migrating.pdf                         coming from AltairZ80/z80pack
DRIVING-WITH-AI.md                    the briefing for an AI assistant driving it over MCP
cheatsheet.md                         every monitor command on one page
LICENSE                               ours (MIT)
LICENSE-SDL3                          SDL3's zlib licence
hostbridge/                           file-transfer utilities: source, HEX, COM
tools/                                the mirror-watch scripts, the LBR tools, and their README.pdf
skills/altairsim/                     the MCP briefing as an Agent Skill
skills/altairsim-*/                   the rules for one guest program each, as Agent Skills
examples/...                          the shipped example machines, with their media
```

The PDFs are built by CI and committed; no build machine makes them. `LICENSE-SDL3` is there
because SDL3 is linked into the binary (§3.2): its code ships, so its licence does.

**The contents change in `docs/package.map`, never in `tools/build-package.sh`.** A new shipped
path must also be named in `docs/manual/package.md`; `docs-package` fails the build otherwise.

Only `altairsim` and the `altairsim-*` skills ship from `.claude/skills/`. The release skills
are for whoever builds the package, and the map names each shipped skill on its own line for
that reason.

The Developer Guide is not in the package. It is about the source, which is not in there either.

---

## 3. SDL3

**A binary built without SDL3 is headless, and it looks fine.** It starts, boots CP/M, answers
`--help`, and draws nothing. That is how v0.1.0 and v0.2.0 shipped on every platform. So every
build machine must have SDL3, the configure step checks for it, `build-package.sh` refuses a
headless binary, and the macOS CI leg fails if it comes up headless.

### 3.1 Where the libraries come from

**Each build machine keeps its own SDL3, built static from source. Nothing is vendored.**

```
macOS, Linux   tools/build-sdl3-static.sh    -- once; installs to ~/opt/sdl3-static
Windows        tools\build-sdl3-static.bat   -- once; %USERPROFILE%\opt\sdl3-static
```

Both scripts pin the same SDL3 version, build it static, and do nothing on a second run. **That
pin is the only thing making the machines agree** — change it on purpose, and rerun the scripts
everywhere when you do. Nothing checks that they agree, so every worker reports its SDL3 version
with its archive; if a video bug shows on one platform only, compare those first.

The Windows script is a `.bat`, not a `.ps1`: PowerShell's default policy blocks an unsigned
script, and a freshly cloned `.ps1` would not run. `curl.exe` and `tar.exe` ship with Windows 10,
so it needs only CMake.

`brew install sdl3` is fine for development (CI's macOS leg uses it), but it ships only a dylib
and cannot make a package — see §3.2.

SDL3 is a prerequisite like the compiler: no `third_party/`, no fetch script, no committed
binaries. That keeps ~11 MB per SDL3 version out of `.git` for good.

### 3.2 Why static

A macOS build against Homebrew's SDL3 links `/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib` **by
absolute path**. Zip it and it starts on no other Mac. Bundling the dylib and rewriting install
names would fix that; **linking SDL3 statically removes the problem**:

| | dynamic (brew) | static |
|---|---|---|
| binary | 1.7 MB **+ a 2.4 MB dylib to bundle** | **4.4 MB, self-contained** |
| `otool -L` | `/opt/homebrew/...` | system frameworks only |
| packaging work | copy the dylib, `install_name_tool`, `@rpath`, verify | **none** |

`find_package` picks up the static target with no change to `CMakeLists.txt`; the configure
only points `CMAKE_PREFIX_PATH` at the static prefix. The SDL3 script builds with
`-DSDL_STATIC=ON -DSDL_SHARED=OFF`, and **both matter**: with a shared library also in the
prefix, `find_package` takes that one and the static build is silently undone. The script
refuses to finish if it finds one.

**macOS also needs `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`**, on both the SDL3 build and ours.
Without it the binary targets the build machine's own macOS and will not start on anything
older.

**Windows has the same problem one level down: the C runtime.** MSVC links it dynamically by
default (`/MD`), so the `.exe` needs the VC++ redistributable, which a clean Windows 10 machine
does not have. `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (static `/MT`) removes that need.
**SDL3 and altairsim must agree on it**: mixing the two gives duplicate-symbol link errors at
best and two C runtime heaps at worst.

SDL prefers dynamic linking so users can update SDL on their own. That matters less for a
self-contained simulator handed over as a zip, and SDL3's zlib licence permits static linking.

If a platform ever has to go dynamic, bundling becomes mandatory there:

| | fallback if dynamic |
|---|---|
| macOS | copy the dylib in; `install_name_tool -change … @executable_path/…` |
| Linux | copy `libSDL3.so.0` in; link with `-Wl,-rpath,'$ORIGIN'` |
| Windows | put `SDL3.dll` beside the `.exe` |

---

## 4. The build machines

**Two roles:**

- **The coordinator** bumps the version, tags, opens the draft, collects the archives, and
  publishes. It is the only machine with GitHub credentials. It is also a build machine for its
  own target.
- **A build machine** builds, tests, packages and delivers **one** archive. It never tags, never
  publishes, and never decides a version number.

### 4.1 What each machine needs

```
every machine     a C++20 compiler, CMake >= 3.20, git
                  the static SDL3 for that platform (§3.1)

a WORKER          git reaching GitHub over ANONYMOUS https -- no login, no token
                  an ssh client, and a delivery key for the coordinator (§4.5)

the COORDINATOR   git and gh authenticated -- it does ALL the GitHub work
                  sshd on, so the workers can scp their archives in
```

**A worker holds no GitHub credentials.** It clones the public repo, builds, and hands one
archive back over `scp`. Its credential for the coordinator is the delivery key, which can write
to the coordinator's `dist/` and nothing else. Everything that touches the repository with
credentials happens on the coordinator. A Mac also holds the signing credentials (below), which
can sign and notarize a program and do nothing on GitHub.

**Each Mac, the coordinator too, needs a signing keychain.** A macOS package is signed with the
Developer ID Application certificate and notarized by Apple, so that macOS starts it with no
`xattr` step (§4.3). Set it up one time on each Mac:

1. Get the Developer ID certificate with its private key as a `.p12` (Keychain Access: right-click
   the identity, Export). Get the App Store Connect API key (`AuthKey_XXXXXXXXXX.p8`), its key
   ID and its issuer ID.
2. Copy the two files to the Mac, into a folder that only you can read.
3. On that Mac, in a terminal (it asks for the `.p12` password), run:
   `tools/macos-signing-setup.sh --p12 <file> --p8 <file> --key-id <id> --issuer <uuid>`.
   Over ssh, use `ssh -t` so that it can ask.
4. CHECK the last line, `macos-signing-setup: OK`. STOP on a FAIL: the script names what Apple
   or the keychain refused.
5. Copy the four `<TARGET>_…` lines that it prints into `distribution.conf`.
6. Delete the two copied files.

The script makes `~/Library/Keychains/altairsim-signing.keychain-db` and its password file
`~/.altairsim-signing-pass` (mode 600). The keychain is separate from the login keychain for
one reason. Over ssh, the login keychain can be locked, and `codesign` can wait for an "allow
access" dialog that nobody sees. `tools/macos-sign.sh` unlocks the signing keychain from the
password file before each use. `release-drive.sh preflight` checks the keychain on each Mac.

**No pandoc, no Chrome, no poppler on any machine.** CI builds the PDFs and commits them, so
the tagged tree already has them. `build-package.sh --pdf docs/altairsim-manual.pdf` uses that
copy, so the packaged manual is byte-identical to CI's. A local pandoc is a different pandoc,
and a different pandoc is a different document.

**Windows also needs Git Bash**, because `build-package.sh` is `/bin/sh`, and one parser of
`docs/package.map` is enough. `tools\windows\RUN-ME-setup-windows-worker.bat` sets up a Windows
worker in one go: Visual Studio 2026 Build Tools with CMake and Ninja, Git and Git Bash, OpenSSH
Server, and the static SDL3. It ends by proving a configure finds SDL3. It makes no delivery key.
The macOS and Linux workers are set up by hand.

### 4.2 The eight steps, on every machine

`tools/release-worker.sh` does them, on all four targets (on Windows, in Git Bash), with every
CHECK and STOP. Each check reads the full output of its command. The script ends with a report
block and one line, `release-worker: OK` or `release-worker: FAIL at step N`. The
`release-worker` skill gives the reason for each check. In outline:

1. **Fetch the tag, never a branch** — `git fetch --tags --force; git checkout -f vX.Y.Z`. A
   re-spin can force-move the tag, and a plain fetch keeps the old one.
2. **Configure from a clean `build/`** against the static SDL3. CHECK
   `-- SDL3 found -- video boards enabled (windowed)`; STOP on `headless`.
3. **Build** — `--parallel`, except on a machine short of RAM.
4. **Test** — `ctest -LE slow`, minus the hardware tests. CHECK `100% tests passed`. The
   hardware tests (`serial-hw` with a null-modem pair, `tnfs-hw` with `de-tnfsd` on `PATH`) run
   separately and **warn** rather than stop. Then **the CPU exercisers**, `ctest -L slow`
   (8080EXM, 8085EXM, ZEXDOC, ZEXALL), through the binary that this machine ships. CHECK the
   same pass line. They run here and not in CI on the tag: the four build machines are the four
   compilers that ship, and the Intel Mac has no CI runner.
5. **`altairsim --version`** — CHECK exactly `AltairSim X.Y.Z`; STOP on a `-N-gsha` suffix or
   `(modified)`.
6. **Package** — `tools/build-package.sh --pdf docs/altairsim-manual.pdf --target <target>`.
   On a Mac, with the four signing options, which sign and notarize the package (§4.3). CHECK
   the `macos-sign: SIGNED` line. A Mac with no signing values in `distribution.conf` STOPs
   before step 1.
7. **Prove the manual against the archive** — `tools/verify-package.sh <archive>`, which
   extracts it outside the repo and runs the manual's own `$ altairsim …` commands. On a Mac,
   with `--require-notarized`, which also proves that Gatekeeper accepts the program and that
   it starts with the quarantine mark on it.
8. **Deliver** — `scp` the archive to the coordinator's `dist/`.

**A worker's checkout is disposable.** `tools/release-drive.sh` resets it
(`git reset --hard`, `git clean -fdx`) before a build, and prints each path that it removed.
The coordinator's checkout is a development tree and is never reset.

**If any check fails, stop and report it.** No delivery, no workaround. A package that reaches
the download page has no later gate.

### 4.3 Per-platform differences

**macOS.** Both Macs run identical steps and differ only in target. **Neither passes
`-DCMAKE_OSX_ARCHITECTURES`**: each builds native, which is the point of splitting.

**Each Mac signs and notarizes its own package**, in step 6. `tools/macos-sign.sh` signs every
Mach-O file in the package with the Developer ID identity, the hardened runtime and a secure
timestamp. It zips the package folder, sends the zip to Apple's notary service, and waits for
`Accepted`. It usually takes a few minutes, and the Mac needs the internet to reach Apple. The
archive stays a tar.gz.

**A plain executable cannot be stapled.** Only an app bundle, a `.dmg` or a `.pkg` can hold the
notary ticket. For a bare `altairsim`, macOS gets the ticket from Apple the first time a user
starts a new copy. The manual's Troubleshooting chapter says what to do when the Mac is offline.

**A double-click on a notarized bare executable is still refused.** Finder starts a bare
executable as a document. macOS checks documents separately from programs, and the check
refuses a notarized copy, online or offline. A start from a terminal is checked as a program.
That check passes when the Mac is online. The start also clears the record of the refused
double-click. The manual tells users to start `altairsim` from a terminal. A `.dmg` does not
change this, because its payload is still a bare executable. A `.pkg` would, and that is a
release-process change, not a documentation fix.

**The signing is done on the worker, not on the coordinator.** The coordinator could sign the
Intel binary, but it would then change an archive after delivery. Step 7 proves the archive
that ships, on the hardware that it is for. That holds only if the worker signs.

`--unsigned` on `release-drive.sh start` (or `release-worker.sh`) gives an ad hoc signature
instead. Use it for a dry run only: a user of that package must clear the quarantine mark by
hand. The CI package leg (`package.yml`) has no certificate, and signs ad hoc.

**Linux.** Build on the **oldest glibc you can get** — a container or an old VM. A binary built
on a current distro refuses to start on an older one. This is the one target where the build
host decides who can run the result.

**Windows.** Steps 1–5 in PowerShell, 6–8 in Git Bash. `--config Release` is required on the
multi-config generator, and the binary lands in `build\Release\`, where `build-package.sh` looks
for it. Git Bash has no `zip` and its GNU `tar` cannot write one, so the script makes the `.zip`
with Windows' own `tar.exe --format zip`. Do not fall back to `Compress-Archive`: PowerShell 5.1
writes backslash separators that Unix `unzip` cannot extract.

### 4.4 Windows: MSVC, and only MSVC

**MSVC is the only supported Windows toolchain.** It is what the Windows CI leg builds on every
push, and what `src/platform/win32/` is proven against. **Specifically Visual Studio 2026**, the
one on CI's image, so a release is built by the compiler every PR is checked with. Name the
generator (`-G "Visual Studio 18 2026"`): without it CMake picks the newest Visual Studio *it*
knows, and an old CMake builds with 2022 without a word.

#### No Developer shell is needed, and this matters for an assistant

With the Visual Studio generator, `cmake --build` runs MSBuild, which finds the toolchain itself;
`cl.exe` never needs to be on `PATH`. CI's Windows leg configures from `shell: bash` with no
`vcvars` step.

An assistant's shell **does not keep environment variables between commands**, so "run
`vcvarsall.bat`, then build" does nothing:

| generator | setup needed | in one command |
|---|---|---|
| **Visual Studio** (use this) | none | `cmake -B build -G "Visual Studio 18 2026"`, then `cmake --build build --config Release` |
| Ninja | `vcvars`, every time | `cmd /c "call vcvars64.bat && cmake --build build"` |

**Static C runtime** (`MultiThreaded`), for the reason in §3.2. A packaged `.exe` must show only
Windows system DLLs in `dumpbin /dependents`: no `SDL3.dll`, no `VCRUNTIME140.dll`.

### 4.5 The site: `distribution.conf`, and the delivery key

**Which machines, where, and how to reach them** is in `distribution.conf`
(`distribution.conf.example` names every key): the coordinator's name and `dist/`, each
worker's ssh address and checkout, whether it can build `--parallel`, a `PATH` prefix for
non-login ssh shells, its null-modem serial ports, and which targets are **one physical machine**
(a VM and its host). Those must not build at the same time: they only fight over one CPU and
one pool of RAM.

Reach the coordinator by a **name that does not change**, never a DHCP address.

**Every worker's `dist/` delivery works the same way:**

- **On the coordinator:** sshd on, and each worker's delivery public key in
  `~/.ssh/authorized_keys`. `dist/` itself needs nothing: it is tracked (via `dist/README.md`),
  so it exists in every checkout, and `build-package.sh` clears only its own target's files
  there, so an archive a worker delivered is never wiped.
- **On each worker:** `origin` is the https URL, so fetches need no credential. A dedicated,
  **passphrase-less** ed25519 key, `~/.ssh/altairsim_deploy` — the scp runs unattended, so a key
  with a passphrase will not do — and in `~/.ssh/config`:

  ```
  Host <coordinator name>
      IdentityFile ~/.ssh/altairsim_deploy
      IdentitiesOnly yes
  ```

---

## 5. The release sequence

The `release-coordinator` skill has the commands. Steps 1–4 can be done ahead of build day;
5–6 need the machines.

1. **Bump the version, and write the changelog section.** `project(altairsim VERSION X.Y.Z …)`
   in `CMakeLists.txt` is the only place the number lives. The changelog section is
   **curated**: a short themed narrative built from `git log --merges <prevtag>..HEAD`, with
   anything already shipped trimmed out. It is not the `Unreleased` block renamed. The same
   commit removes the `Unreleased` block, header included: a release leaves no empty
   `## Unreleased` header, and the next change that needs an entry adds it again.
2. **Merge, then wait for the PDFs.** `docs.yml` rebuilds every document on master and commits
   them as *"Rebuild the PDFs for `<sha>`"*. **Tag that commit**, not the merge, or the tagged
   tree carries a stale manual. The manual goes to `build-package.sh` with `--pdf`; the
   changelog, monitor and debugger PDFs are copied straight from the tagged tree.
3. **Preflight, then tag and push.** Run `tools/release-drive.sh preflight` at the commit
   that you will tag. It builds a package of HEAD in a temporary clone on the coordinator and
   runs §7 on it. Next, it resets each worker's checkout and proves that the worker can `scp` a
   file to the coordinator's `dist/`. Tag only after its last line, `preflight: OK`. The tag
   starts no CI run: the CPU exercisers run on the build machines in
   step 5 (§4.2 step 4). `cpu-exerciser-release.yml` is manual only.
4. **Open the draft** — `gh release create vX.Y.Z --draft`.
5. **Build on all four machines** (§4.2): `tools/release-drive.sh start <target> X.Y.Z` for
   each, in any order. The driver copies the worker script to the box, starts it detached, and
   **proves that it started** (the checkout is at the tag, and the worker is a running
   process). On a worker, it first resets the checkout. It refuses two targets that share a
   physical machine at once.
   `tools/release-drive.sh wait <target>...` checks every worker once a minute until all have
   ended. Then it prints each report block and one `wait OK` or `wait FAIL` line.
   `status <target>` reads one log once. The workers `scp` their archives
   into the coordinator's `dist/`; the coordinator builds its own straight into it.
6. **Verify, checksum, upload, publish.** §7 on each archive; then `tools/build-checksums.sh`
   (it refuses unless all four of one version are in `dist/`, and writes `dist/SHA256SUMS`);
   then upload the four archives and `SHA256SUMS` and publish the draft.

**While a release is still a draft**, a doc fix after tagging is shipped by force-moving the
tag to the fix and rebuilding everywhere, so every archive still reports a clean `X.Y.Z`. Never
move a published tag.

### The two traps that bit v0.2.0

**Build at the tag, not at the merge.** v0.2.0's first binaries reported
`AltairSim 0.2.0 (v0.1.0-86-g59dbba8)`: CI had built the merge commit, an ancestor of the tag,
so `git describe` found the previous tag. That is why every machine checks `--version` before
packaging.

**The manual in the package must be the one CI built.** `build-package.sh` without `--pdf`
rebuilds it with whatever pandoc is local. CI pins pandoc 3.6, and pandoc's HTML is the
paginator's input, so a different pandoc is a different document.

---

## 6. Where the artifacts go

**The coordinator's `dist/` is the single collection point; the GitHub Release is where the
archives become public.**

```
   worker (x3)                               coordinator
   packages its target into dist/            packages its own target into dist/
              |                                        |
              |  scp -> coordinator                    | (already local)
              +------------------+---------------------+
                                 v
                 the repo's dist/  —  all four collect here
                                 |
                                 |  gh release upload      <- coordinator only
                                 v
                       draft release vX.Y.Z  →  published
                                 |
                                 |  pulled by the web server, on its own
                                 v
                        altairsim.com/downloads
```

**altairsim.com is the front door; the GitHub Release is the archive.** They serve the
**identical files**, and must never differ: two artifacts both calling themselves the same
version and differing by a byte make every bug report against either untraceable. The web
server refreshes its copy from the published release itself, and checks what it serves over
HTTPS against `SHA256SUMS`. Nothing on the build side pushes to it.

`SHA256SUMS` is published beside the four so the two locations can be checked against each
other, and so anyone can check a download. `tools/build-checksums.sh` writes it in the standard
bare-name format, so `shasum -a 256 -c SHA256SUMS` works from `dist/` or from a download
folder. It refuses to write a partial file: three lines that verify OK and a fourth archive that
ships unchecked is worse than no file at all.

---

## 7. Verifying a package

The `release-verify` skill has the commands. Why it is shaped the way it is:

**Unpack the archive somewhere `git rev-parse` fails, and run it there.** A package that only
works next to its source tree is exactly what this catches.

**Test the archive, never `dist/altairsim-<ver>-<target>/`.** That is `build-package.sh`'s
staging directory. It holds whatever local `build/altairsim` existed when the script ran, and
it looks exactly like a package.

**Ask the binary first:** `altairsim -n -x 'SHOW VERSION'` has a `video` row that reads
`SDL3 -- windowed` or `none -- headless (null display)`. `build-package.sh` refuses a headless
binary on the same row.

**"No SDL in the dependency list" means static or headless**, and `otool -L` / `ldd` /
`dumpbin` cannot tell the two apart. So a second check counts SDL symbols with `nm` (thousands,
or zero). Use `nm`, not `strings`: `SDL_CreateWindow` is a symbol, not a string, and `strings`
reports zero on a correct static build.

**A window reaching a screen can only be checked by a person.** The `video` row says SDL3 is
compiled in, not that a window opened and takes the keyboard. The skill ends with a short
checklist (open a window, see the cursor blink, type into it, flip the video, close it) for a
person to answer yes or no per platform. An assistant driving a pipe cannot click a window, and
`SHOW DISPLAY`'s `focus` is a launch policy, not a report of where the keyboard is.

---

## 8. What is not automated

- **No workflow builds a release package.** The coordinator starts `tools/release-worker.sh` on
  each of the four machines with `tools/release-drive.sh`. The upload is by hand.
  `.github/workflows/package.yml` builds a `macos-arm64` package and runs `verify-package.sh` on
  it, but only as a check of the manual. Nothing ships from that run.
- **Setting up a worker is scripted on Windows only.** The macOS and Linux workers, and every
  worker's delivery key, are set up by hand (§4.5).
- **Nothing makes the machines' SDL3 versions agree** other than the pin in the two scripts.
  Each worker reports its version (§3.1).
- **The packaged Windows `.exe` has not been run on a machine that never had a compiler.** It
  imports only Windows system DLLs, which is what such a run would test, but the run itself has
  not been done.
- **The window checklist (§7) is a person's job** and cannot be automated.

The GitHub issue tracker is the live list of open work.
