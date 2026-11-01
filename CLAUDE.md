# Working on `altairsim`

A C++20 simulator of the MITS Altair 8800 and the S-100 bus. No dependencies beyond a C++20
compiler and CMake; SDL3 is optional and detected, never required.

## How work is done here

**Every task follows two skills: `work-task` (plan → build → review) and `ship-change`
(commit → PR → merge → issues).** Load the one that fits before acting. The rules hold
whether or not a skill is loaded:

1. **No work without a plan made in plan mode** and approved. No edits before approval.
   A feature plan first shows the feature is needed — not already documented, not a docs gap.
2. **Tests never work around a bug.** A test that exposes a bug gets the code fixed. Never
   loosen an assertion, skip the test, dodge the input, or add a retry.
3. **A bug related to the task is fixed in the same change. An unrelated bug gets a GitHub
   issue** at once (signed `--AltairSim Claude`) and is left alone.
4. **Docs are updated before the commit**, as part of the change under review.
5. **No commit until the maintainer has reviewed and approved it.** Stop and report the diff.
6. **No PR until the maintainer approves opening one.** More commits may come first.
7. **No merge until CI is green** on every leg it ran — all three platforms for a core change,
   Linux for anything else. Then merge without asking again. After a code merge, `master`
   builds all three; a red leg there is fixed before anything else merges.
8. **After the merge, comment on the related issues.** Who closes one depends on who opened
   it: an issue opened by `deltecent` is closed by `Fixes #N` in the PR. Anyone else's is never
   closed by us — `Refs #N`, no closing keyword — and the person who opened it closes it.

The three gates are **plan approved**, **commit approved** and **PR approved**. Each
approval covers that one step only; it never carries over to the next. **A PR from anyone but
`deltecent` goes through the `review-pr` skill: its merge needs green CI *and* the
maintainer's approval.** **An incoming issue goes through the `review-issue` skill**, which
reaches a verdict and stops there — it never builds anything.

**A report that hands the next move to the maintainer ends with it.** The summary comes first;
the last line says what is ready and what it is waiting on (*"Ready for your review on branch
`fix/x`. Nothing is committed or pushed."*). That holds at every gate, in every skill.

## If you are here to build or ship a release

**Use the release skills; they are the procedure.** Each is written to be executed step by
step on a machine that has never seen this repository — literal commands, the exact output to
check after each, and a STOP condition on every check. [`DISTRIBUTION.md`](DISTRIBUTION.md) is
the reasoning behind them.

| | |
|---|---|
| `release-worker` | **Any build machine.** Build, test, package, deliver one archive — nothing else. |
| `release-coordinator` | **The coordinator only.** Version, changelog, tag, draft, drive the builds, publish. |
| `release-verify` | Prove an archive works as downloaded. |

**A build machine never tags, never publishes, and never decides a version number.** If a
check fails, stop and report it — do not work around it and do not judge it probably fine.
Nothing gates a package after you deliver it. **This site's addresses, paths and serial ports
are in `distribution.conf`** (gitignored; `distribution.conf.example` is the template) — never
in a tracked file.

**Windows:** MSVC is the only supported toolchain, and no Developer shell is needed
(`docs/building-windows.md`). On a first visit to the Windows box, start at §6 of that file.

## Before changing anything else

| | |
|---|---|
| [`DESIGN.md`](DESIGN.md) | The architecture and the reasoning. **Read the relevant section before implementing** — most surprises here are deliberate and explained. |
| [GitHub issues](https://github.com/deltecent/altairsim/issues) | The public work list: bugs and features. Check before proposing something — a declined idea may be recorded here with its reason so it does not get re-raised. |
| [`DISTRIBUTION.md`](DISTRIBUTION.md) | How a release is built and shipped. |
| `docs/manual/` | The User Manual, for someone holding a release package. |
| `docs/devguide/` | The Developer Guide, for someone changing the source. |

## Rules that bite

- **Every change goes on a branch off `master`** — and through the gates above.
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
- **`<!-- @claude ... -->` in a Markdown file is a review comment for you.** Revise the master
  file in the repo, and never leave a marker in `docs/manual/`. See
  `docs/devguide/doc-review-comments.md`.

## Research economy

- **Before spawning Explore/Plan agents, check the memory index and run one targeted
  `grep`/`Read`.** Fan out only when the scope is unknown or spans many subsystems.
- **After tracing something non-obvious, write it back**: a memory note, or `docs/devguide/`
  when the fact is about how the code is built.

## Testing

```sh
./build/altair_tests <names>        # local loop: the suites for the subsystem you touched
                                    #   e.g. ./build/altair_tests pmmi modemline lines
                                    #   ./build/altair_tests --list  to see the names
ctest --test-dir build -LE slow -j 8   # optional full local run  (~60 tests, under a minute)
ctest --test-dir build -j 8            # optional; adds the slow CPU gate  (~4 minutes)
```

**Run the suites for what you changed, then commit. CI runs the full suite** and is the
backstop (`docs/devguide/building.md`).

**Match the pass line, not the absence of errors:** read `100% tests passed out of N`. A stray
`cd` can stop `ctest` from running, and that looks like success.

**Warnings fail CI on all three toolchains.** Reproduce the gate before you push a code change:
`cmake -B build -DWERROR=on && cmake --build build -j`.

## Driving a running guest

**To type at a running guest and read what it prints, use `altairsim <machine> --mcp`, and load
the `altairsim` skill for the recipe.** Never write a throwaway `expect` or pty script. `expect`
under `tests/acceptance/*.exp` is only for a committed acceptance test.
