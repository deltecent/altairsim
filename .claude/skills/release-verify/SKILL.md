---
name: release-verify
description: Prove an altairsim release archive works as downloaded — unpack it outside any repository, check the version, check that SDL3 is linked in statically (not headless, not by absolute path), check that a macOS program is signed and notarized, and hand a person the window checklist. Use when asked to verify, check or test a release package or archive before (or after) it is published, or to run DISTRIBUTION.md §7.
---

# Verify a release archive

**Test the ARCHIVE, unpacked somewhere `git rev-parse` fails.** Not in `dist/`, not under the
repository. A package that only works next to its source tree is exactly what this catches.

**Never test `dist/altairsim-<ver>-<target>/`.** That is `build-package.sh`'s staging
directory: it holds whatever `build/altairsim` existed when it ran, not the binary that ships,
and it looks exactly like a package.

On each of the four archives, on the platform it is for:

```sh
d=$(mktemp -d) && tar xzf altairsim-X.Y.Z-<target>.tar.gz -C "$d" && cd "$d"/altairsim-X.Y.Z*
#   Windows: extract with tar.exe (Expand-Archive refuses some files)

./altairsim --version                            # CHECK: exactly "AltairSim X.Y.Z"
./altairsim -n -x 'SHOW VERSION'                 # CHECK: video row "SDL3 -- windowed"
./altairsim -l                                   # the built-in machines list
```

**STOP** on any version suffix, `(modified)`, or a video row of `none -- headless (null
display)`. Ask the binary first — it is the simplest check and needs no toolchain.

Then boot two machines and see them reach their prompt. Drive them with `altairsim <machine>
--mcp` (the `altairsim` skill), not a hand-rolled pty script:

| machine | reaches |
|---|---|
| `examples/cpm/cpm22-buffered.toml` | `A>` |
| `examples/basic4k/basic4k.toml` | `MEMORY SIZE?` |

## Is SDL3 inside the binary?

| | run | PASS (static) | FAIL |
|---|---|---|---|
| macOS | `otool -L altairsim` | no SDL line | `/opt/homebrew/…` or any absolute SDL path |
| Linux | `ldd altairsim` | no SDL line | a distro `libSDL3.so` |
| Windows | `dumpbin /dependents altairsim.exe` | only system DLLs: no `SDL3.dll`, no `VCRUNTIME140.dll` | either of those |

**"No SDL line" also describes a headless build**, so add a second check where `nm` exists:

```sh
nm altairsim | grep -c SDL_       # static: thousands.   headless: 0.
```

Use `nm`, not `strings`: `SDL_CreateWindow` is a symbol, not a string, and `strings` reports
0 on a correct static build.

**macOS only:** `vtool -show-build altairsim | grep minos` must report `11.0`, not the build
machine's own OS version.

## macOS: is it signed and notarized?

A macOS package must start with no `xattr` step. Check it:

```sh
codesign --verify --strict --verbose=2 altairsim
codesign -dv --verbose=2 altairsim 2>&1 | grep -E '^(Authority|Timestamp)='
spctl --assess --type open --context context:primary-signature --verbose=2 altairsim
```

PASS is `valid on disk`, a first `Authority=Developer ID Application: …`, and from `spctl`
`accepted` with `source=Notarized Developer ID`. FAIL is an ad hoc signature (no `Authority`
line) or `rejected`. Use the `spctl` form above. `--type execute` judges only an app bundle,
and rejects every plain program as "not an app".

`spctl` asks Apple online, so a Mac with no internet gives a false FAIL.

## The window checklist — for a PERSON

The `video` row says SDL3 is compiled in; it does not say a window reached a screen. **An
assistant cannot do this part** — it cannot click a window or see pixels. Give this table to a
person, per platform, and record their yes/no for each row.

The window only redraws while a program runs, so these checks use `sol20`, whose SOLOS loops.

| | do this | pass is |
|---|---|---|
| **H0** | macOS: download the archive from the release page **with Safari**, open it in Finder, and in Terminal run `./altairsim --version` in the folder | the version prints, and macOS shows **no** *"altairsim" Not Opened* dialog. A dialog means the package is not notarized |
| **H2** | `altairsim sol20`; wait for the SOLOS `>` in the window | a cursor blinking about once a second, steadily, in a blocky font, **sharp-edged** (blurred means the scaler is wrong) |
| **H3** | `altairsim sol20`; at the `>` in the window, click it and type `HELLO` | the characters appear **in the window** — the only proof the window gets the keyboard |
| **H5** | with `sol20` running: `^E`, `SET vdm0 video=reverse`, `R`; then `^E`, `SET vdm0 video=normal`, `R` | the window flips dark-on-light and back (the value is `reverse`; `R` matters — a stopped machine does not redraw) |
| **H6** | with `sol20` running, click the window's close button | the monitor prints `window closed -- the machine is still at <PC>. RUN resumes; QUIT exits.` and the window **stays open**. That is the design: closing the window stops the guest, it does not quit |

On `sol20`, press `^E` before typing a monitor command — it boots straight into SOLOS, and
typed text otherwise goes to the guest and echoes on the video screen.

Things that look like bugs and are not:

- `SHOW DISPLAY`'s `focus` is a launch **policy** (take the keyboard when the window opens),
  not a live "has the keyboard" report. Nothing reports live focus; H3 is the check.
- With the machine **stopped** (e.g. at a `HLT`), the close button does nothing and the OS may
  call the window unresponsive — the SDL event queue is only drained while a program runs.
  Known limitation, not a release blocker.

## Report

Per target: the `--version` line, the `video` row, the dependency check result, `nm` count,
`minos` (macOS), the signature and `spctl` lines (macOS), the two boots, and the person's
H0–H6 answers (or "not yet run").
