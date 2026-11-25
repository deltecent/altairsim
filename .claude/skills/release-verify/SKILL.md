---
name: release-verify
description: Prove an altairsim release archive works as downloaded: run tools/verify-package.sh on it, on the platform it is for (version, video row, static SDL3, minos, Windows DLLs, macOS signature and notarization, and the manual's own commands), and hand a person the window checklist. Use when asked to verify, check or test a release package or archive before (or after) it is published, or to run DISTRIBUTION.md §7.
---

# Verify a release archive

**During a release, the workers have already done the machine part.** Step 7 of
`tools/release-worker.sh` runs `tools/verify-package.sh --release` on each archive, on its own
box, and the `verify` line of each report block in the `release-drive.sh wait` output gives
the result. Read those four lines. Do not run the checks again, and do not write a boot
script. Give the window checklist below to a person.

## The machine part: one command

To check an archive outside a release, such as a download, run this from a checkout of the
repository, on the platform that the archive is for:

```sh
tools/verify-package.sh --release [--require-notarized] altairsim-X.Y.Z-<target>.tar.gz
#   --require-notarized: macOS only.   Windows: run it in Git Bash, with the .zip.
```

**CHECK the last line, `verify-package: PASS -- N/N … (skipped 0)`.** **STOP on
`verify-package: FAIL`.** Each `---- FAIL` line above it names the problem.

The script unpacks the archive in a temporary folder outside any repository, and checks the
binary in it:

| Check | Pass |
|---|---|
| `--version` | `AltairSim X.Y.Z`, the version in the archive name. With `--release`, no suffix. |
| `SHOW VERSION` | a row `video  SDL3 -- windowed (…)`. A headless build has no window. |
| macOS `otool -L`, Linux `ldd` | no SDL library |
| Windows `dumpbin -dependents` | no `SDL3.dll`, no `VCRUNTIME140.dll` |
| macOS `vtool -show-build` | `minos 11.0` |
| macOS signature | Developer ID, notarized, and it starts with the quarantine mark |
| the manual's commands | each one reaches the prompt that the manual shows |

Notes:

- **Never give it `dist/altairsim-<ver>-<target>/`.** That folder is the staging folder of
  `build-package.sh`. Give it the archive.
- **Windows with no Visual Studio:** the script prints a NOTE that the DLL check did not run.
  Run `dumpbin /dependents altairsim.exe` by hand.
- **macOS:** `spctl` asks Apple online. A Mac with no internet gives a false FAIL.
- **No `expect`:** the pty commands are skipped, and the PASS line says how many. A skip is not
  a pass.

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

For each target: the `verify-package:` last line (from the worker report, or from your own run),
any `---- FAIL` or NOTE line, and the person's H0–H6 answers, or "not yet run".
