# Contributing to altairsim

Contributions of all kinds to altairsim are welcomed and greatly
appreciated.

- Reporting bug(s) and suggesting new feature(s)
- Discussing the current state of the code
- Submitting fixes and enhancements

## The altairsim GitHub Repository

The [altairsim GitHub Repository](https://github.com/deltecent/altairsim)
is the primary location for developing, supporting, and distributing
altairsim.

- Use **Issues** to report bugs, request enhancements, or ask usage
  questions. Check the existing issues first — a declined idea may be
  recorded there with its reasoning so it does not get re-raised.
- Use **Discussions** to ask questions, share what you have built, and
  interact with others.
- Use **Pull Requests** to submit content (code, documentation, etc.)

## Submitting Content

altairsim uses the standard
[GitHub Flow](https://docs.github.com/en/get-started/quickstart/github-flow).
Submissions are ideally done via Pull Requests.

- For anything significant, opening an Issue first is encouraged. This
  simply ensures a submission is consistent with the overall goals of the
  project before you invest the work.
- Every change is developed on a branch off `master` and merged when
  done. Fork the repository, create your branch from `master`, and make
  and test your changes there.
- Please update the relevant documentation alongside your change. The
  User Manual lives in `docs/manual/`, the Developer Guide in
  `docs/devguide/`, and the architecture and its reasoning in
  `DESIGN.md`.
- You are encouraged to comment your submissions so your work is properly
  attributed.
- When ready, submit a Pull Request to merge your branch into `master`.

## Before You Start

Read the top of [`CLAUDE.md`](CLAUDE.md) and the documents it points to.
The most important:

| | |
|---|---|
| [`DESIGN.md`](DESIGN.md) | The architecture and the reasoning behind it. Read the relevant section before implementing — most surprises here are deliberate and explained. |
| [`DISTRIBUTION.md`](DISTRIBUTION.md) | How a release is built and shipped. |
| `docs/manual/` | The User Manual, for someone holding a release package. |
| `docs/devguide/` | The Developer Guide, for someone changing the source. |

## Building

altairsim is C++20 with no dependencies beyond a C++20 compiler and
CMake. SDL3 is optional and auto-detected — never required.

```sh
cmake -B build
cmake --build build -j
```

## Testing

Run the unit suites for the subsystem you touched, then rely on CI for
the full sweep:

```sh
./build/altair_tests <names>        # e.g. ./build/altair_tests pmmi modemline lines
./build/altair_tests --list         # list the suite names
ctest --test-dir build -LE slow     # full local run, minus the slow CPU gate
ctest --test-dir build              # full run including the slow CPU gate
```

Read the pass line — `100% tests passed out of N` — not merely the
absence of the word "error". CI runs the full suite on Linux, macOS, and
Windows on every push; all three must be green.

Warnings fail CI on GCC and Clang (`-DWERROR=on`). Reproduce that gate
locally before pushing a code change:

```sh
cmake -B build -DWERROR=on && cmake --build build -j
```

## Coding Style

Match the style of the surrounding code — its naming, indentation,
comment density, and idiom. No one likes mixed styles in one file.

A few project-specific conventions worth knowing:

- The word is **"board"**, not "card", for the object, the command, and
  the table (`DESIGN.md` §0.3). "Card" is reserved for a sentence that is
  genuinely about the physical 1970s artifact.
- Do not give hardware a behavior it never had to fix a software symptom.
  Check the host, filter, and monitor layers first.
- `docs/manual/ref/` is **generated** from the binary — edit the emitter
  and run `cmake --build build --target docs-reference`, never the `.md`
  files by hand.
- The PDFs under `docs/` are built and committed by CI. If you run the
  docs build locally, `git checkout --` the PDFs afterward.

## Reference Documentation

Every board and every piece of period software in altairsim is modeled
from primary sources — the original manuals, data sheets, and schematics.
The [`reference/`](reference/) directory is where that provenance lives.

- The **scanned manuals themselves are not tracked** (they are large and
  not ours to redistribute). What *is* tracked is a distilled, text-only
  `.md` for each source — register maps, port addresses, bit tables,
  geometry, and timing — written from the scan and citing it by filename.
  `reference/README.md` indexes them and `docs/sources.md` is the manifest
  naming each original source and where it came from.
- **Period manuals and first-hand artifacts only.** Never learn how the
  hardware works by reading another emulator's source — that explicitly
  includes AltairZ80 and z80pack. Second-hand facts inherit second-hand
  mistakes, and this project's value rests on the hardware model being
  *right*. If a spec is missing, ask rather than guess or reconstruct from
  memory.

**Any new board or new piece of software must be backed by an entry in
`reference/`.** When you add one:

1. Add the distilled `.md` for the source it was modeled from (or extend
   an existing one if the source already has a file).
2. Add its row to the manifest in `docs/sources.md`, naming the original
   scan and where it came from.

A change that models new hardware or new software without a corresponding
reference entry is incomplete — the entry is how a future reader knows the
model is grounded in a real artifact rather than a guess.

## Using Claude Code

Unlike some retro-computing projects, altairsim welcomes AI-assisted
contributions — the repository is deliberately set up to be worked with
[Claude Code](https://claude.com/claude-code). [`CLAUDE.md`](CLAUDE.md)
at the root is loaded automatically and points an agent at the rules that
bite; the Developer Guide under `docs/devguide/` carries the how-to
detail. Whatever the task, the same discipline applies: read the relevant
`DESIGN.md` section first, work on a branch off `master`, and let the test
suite — not a claim of success — be the judge.

- **Adding a new board.** Start from
  [`docs/devguide/adding-a-board.md`](docs/devguide/adding-a-board.md) —
  a tutorial plus the §6 cross-cutting wiring (name resolution, the serial
  seam, config load/save). [`docs/devguide/serial-io.md`](docs/devguide/serial-io.md)
  covers the three-layer serial architecture that most boards touch. Add
  a unit suite for the new board and an acceptance test that boots real
  period software on it, and back the model with a `reference/` entry (see
  above).

- **Fixing bugs.** Debug with facts, not guesses: drive a running guest
  with `BREAK`/`STEP`/`HISTORY`, or with `--mcp` for an interactive
  session, before forming a theory. Never give hardware a behavior it
  never had to paper over a software symptom — check the host, filter, and
  monitor layers first. Reproduce the fix under a scoped unit suite and,
  where it applies, an acceptance test.

- **Documentation.** Edit the Markdown sources — the User Manual in
  `docs/manual/`, the Developer Guide in `docs/devguide/`. Remember that
  `docs/manual/ref/` is generated from the binary (edit the emitter, run
  `cmake --build build --target docs-reference`) and the PDFs are built
  and committed by CI, so leave them to CI.

However Claude Code is used, review its output as your own before opening
a Pull Request. You are the author of what you submit.

## License

altairsim is licensed under the [MIT License](LICENSE). When you submit
changes, your submissions are understood to be under that same license.
