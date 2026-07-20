# TODO

Everything outstanding, in one file. Items live here until they are done or
explicitly declined; a declined item stays, with its reason, so it does not get
re-raised.

**Conventions**

- **Bug** — shipped behavior is wrong, or a document describes something that
  does not exist. Doc drift is a bug: someone reaches for the knob and it is not
  there.
- **Feature** — designed and wanted, not built.
- **Deferred** — deliberately not being done now, with the reason recorded.
- **Declined** — settled as *no*. Do not re-open without new information.

An item with a `#nn` is a GitHub issue and that issue is the source of truth for
detail; this file is the index.

**Update this file before every commit.** If a commit closes an item, the same
commit removes it; if it half-closes one, the same commit says which half. A
commit that fixes the tree and leaves the entry standing is how this file rots —
`9c9c06c` did exactly that to three entries while pruning three others, and they
were re-picked as work two days later. The rule is cheap because the entry is
already in front of you when you do the work; reconstructing it later is not.

---

## Bugs

### `SET BUS UNCLAIMED` is documented but was never built — [#43]

`DESIGN.md` §4.6.1 (line 568) specifies `SET BUS UNCLAIMED=WARN|ERROR|SILENT`
(default `WARN`) to name the port and PC when a guest hits a port no board
decodes. `SET BUS` implements `CONTENTION` only (`src/cli/monitor.cpp:2164`), so
a script following the design doc fails. Nothing warns about an unclaimed port
during a run — the only feedback is the annotation on the interactive `IN`/`OUT`
commands (`src/cli/commands.cpp:229`), which cannot fire while a guest is
running.

The failure this was written to diagnose — a guest reading `0xFF` forever from a
board that isn't there, and hanging with no explanation — is still undiagnosed.
Either build it or cut the paragraph; leaving it reads as shipped.

**Deliberately held until after 0.1.0** (Patrick, 2026-07-18). It is the one
piece of `DESIGN.md` drift knowingly left standing.

### Wire `build-package.sh` into the release workflow

**No workflow runs `tools/build-package.sh`** — `grep` over `.github/workflows/`
still finds no reference to it. What has changed is that the archive it builds is
no longer hypothetical.

**v0.2.0 shipped it** (2026-07-20). The two-archive split below is closed: all
three platform archives are now `altairsim`, `altairsim-manual.pdf`,
`USING-ALTAIRSIM.md`, `LICENSE` and `examples/{cpm,basic,sol,diskbasic}` **with
their media**, which is what the manual has always described. v0.1.0's
binary-only archives were the last of that shape.

**What is still missing is only the automation.** The 0.2.0 archives were
assembled by hand: `build-package.sh` locally, then each platform's CI binary
swapped into a copy of the tree. So gap 2 of the release design — *the script has
never run on Linux or Windows* — is untouched, because it still has not; it ran
once, on macOS, driving three archives.

Two things that by-hand run learned, and that an automated one must handle:

- **The script rebuilds the PDF with whatever `pandoc` is local.** Homebrew gives
  3.10 and `docs.yml` pins 3.6, and a different pandoc is a different document. The
  0.2.0 archives ship CI's 3.6 PDF, copied over the script's output after the fact
  and checked byte-identical. A workflow should build the PDF once on Linux and
  hand it to the other legs rather than rebuilding per platform.
- **CI must build at the tag, not at the merge.** The first fetch produced binaries
  reporting `v0.1.0-86-g59dbba8`, because the run predated the tag and `git
  describe` found the older one. `gh workflow run ci.yml --ref v0.2.0` fixed it —
  a `workflow_dispatch` has no `event.before`, so it also takes the full matrix.
  The shipped binaries report a bare `AltairSim 0.2.0`.

The archive's missing `LICENSE` was the sub-item here and is fixed (2026-07-19):
one `FILE` line, verified byte-identical in a built zip.

### Every distributed binary is headless, and nothing compiles `display_sdl.cpp`

**Found 2026-07-20, cutting v0.2.0.** `ALTAIRSIM_ENABLE_SDL` defaults to `ON`, which
means *"use SDL3 if it is here"* — and on a CI runner it is not here. **No workflow
installs SDL3**; `grep -i sdl .github/workflows/` returns nothing. So
`find_package(SDL3 CONFIG QUIET)` fails on every leg and every CI binary is built
against the `NullDisplay`.

**Verified against the published v0.2.0 archives, not the build tree:** `otool -L` on
the macOS binary links no SDL, and `strings` finds neither `libSDL3` nor
`SDL_CreateWindow` in any of the three. So the video window — which the manual
documents at length, and which v0.2.0's own release notes lead on — **cannot be
opened from any binary this project has ever shipped.** `altairsim sol20` from the
release runs, and draws nowhere.

Two distinct problems live here, and they want different fixes:

1. **Nothing anywhere compiles `display_sdl.cpp`.** It is macro-gated out of all three
   CI legs, so a change that breaks it is green on every platform. That is a test-coverage
   hole as much as a packaging one, and it is the cheaper half to close: one leg with SDL3
   installed would do it.
2. **The shipped product has no video.** That is the packaging half — see *All distributed
   packages must ship with SDL3* under Features.

**The drift that hid this is fixed in the same commit.** `CMakeLists.txt` and
`docs/devguide/building.md` both stated that the CI macOS-universal leg passes
`-DALTAIRSIM_ENABLE_SDL=OFF`. **It never has.** The reasoning behind the flag is sound —
a Homebrew SDL3 is single-arch and will not link into an `x86_64;arm64` fat binary — but
attributing it to a guard that does not exist made the headless build look deliberate and
bounded to macOS, when it is in fact incidental and universal. Both comments now say what
is true, and name the consequence.

---

## Features

### CPU cores

- **8085** — the last unbuilt core (`docs/roadmap.md:248`). The 8080 and Z80
  both went through the same gate: the exercisers pass *before* any board is
  built on the core.

### Boards

- **88-C700 interrupt structure** — [#26]. The card ships and the polled path is
  complete (write a byte, poll ACKNOWLEDGE/BUSY). The real card also has a
  single-level interrupt (per-character or per-CR/LF, SW2 #4) that is **not**
  modeled: the status byte carries the INTERRUPT ENABLE bit software wrote
  (`intEnabled_`, `src/boards/mits-88c700.h:85`) but no request is raised and no
  wire is pulled. A deliberately separate, deferred addition.
- **88-LPC** — line printer controller; milestone 5's one remaining board
  (`docs/roadmap.md:244`).
- **Cromemco Dazzler** — the DMA-driven color bitmap card. Both prerequisites
  ship and are proven: bus-mastering DMA (`src/core/bus.h` `BusMaster`,
  `Debugger::serviceDma`, `tests/test_dma.cpp`) and the `Display` seam
  (`src/host/display.h`, SDL3-backed, optional dependency). The Dazzler is the
  first board that would actually exercise DMA — it is built and proved but has
  never carried a shipping card. Register map distilled in
  `reference/Cromemco Dazzler.md`; provenance in `docs/sources.md`.
- **Sol-20 keyboard: `MODE SELECT` and `CLEAR` still have no host key** — [issue
  #59](https://github.com/deltecent/altairsim/issues/59). **Sourced 2026-07-19**
  from Sol Systems Manual Table 7-4, confirmed against the SOLOS 1.3 equates, and
  recorded in `reference/Sol-20.md`. The four arrows and `HOME CURSOR` now work in
  the video window (`Display::SpecialKey`, defaults `97`/`9A`/`81`/`93`/`8E`), and
  every one of the eight is reachable from any console as a control code, because
  SOLOS masks bit 7 — so this is now an ergonomics gap, not a missing capability.
  What is left: a key for `MODE SELECT` and `CLEAR`, which is a real design call
  (a PC keyboard has nothing obvious to borrow); arrows from a *terminal*, which
  needs escape-sequence matching and collides with `ESC`; and exposing the code
  table to the operator instead of leaving it a compile-time default.
- ~~**A sample-machine TOML for the Eberhard ROMs**~~ — **DONE 2026-07-19.**
  `altairsim amon`, `altairsim acuter` and `altairsim cdbl` are built-in machines
  now. The original entry: `cdbl`, `hdbl`, `amon` and `acuter` are built-in
  (PR #30) but no machine boots one by name the way `altairsim basic4k` does.

  **`hdbl` deliberately got none, and it is a missing BOARD, not a missing machine
  file.** HDBL boots an 88-HDSK and there is no 88-HDSK controller here — nothing
  decodes `A0`–`A7`, so the loader reads a floating bus and stops with `LOAD ERR`.
  A machine that cannot boot is not a sample; shipping one would have answered the
  letter of this entry and taught the reader something false. `roms/HDBL/README.md`
  says so out loud, and the day the controller exists the machine file is four
  lines. (The same loader is inside `amon` at its `FC00` entry, absent for the same
  reason.)

  Each machine is what its ROM requires and not a set of preferences, which is why
  the ACR is in two of them: AMON's default transfer port is 6 (`DTPORT`), and
  CUTER is a cassette operating system, so a machine without one has had the point
  removed. `cdbl` is written as `base = "default"` with one region changed, because
  that is exactly what it is.

  Backed by two acceptance tests, because `test_machines.cpp` proving a built-in
  loads cannot tell you the ROM is at the address its firmware expects or that the
  console is on the port it talks to. `acceptance-eberhard-roms` (a pipe) checks
  only what each machine says unprompted — AMON's `RAM: DF00`, which is the guest
  measuring the memory card, and CP/M booting off the tracked 8″ image.
  `acceptance-eberhard-typed` (expect, a pty) asks AMON and ACUTER to dump their
  own first bytes.

  **The split is the finding, and CI found it.** The first version piped the
  keystrokes and was FLAKY — 4 runs in 40 here once I went looking, and one red
  macOS leg. Keystrokes down a pipe are in the console buffer before the guest is
  switched on and clock into the 6850's one-byte receive register at 9600 baud
  whether or not it has read the last one, so a command typed at a monitor that is
  still doing its startup RAM scan arrives mangled and AMON answers `?`. No amount
  of padding fixes that; waiting for the prompt does, which is what a pty is for.
  Passing locally was luck, and the mutation test I did run (shrink the RAM,
  watch it fail) could not have caught it.

### Debugger and monitor

- **`STOP`** — reserved and answers "not implemented yet"
  (`src/cli/commands.cpp:472`). It waits on a monitor that runs *alongside* the
  machine, which needs a second thread or a multiplexed input loop. ATTN is
  today's escape.
- **`EDIT <addr>`** — reserved, answers "not implemented yet"
  (`src/cli/commands.cpp:165`). Interactive deposit where Enter advances;
  waiting on the line editor. It resolves today so its abbreviation cannot
  change under your fingers once it lands.
- **`SNAPSHOT` / `RESTORE` / `RECORD` / `REPLAY`** — all four resolve at the
  prompt and answer "not implemented yet" (`docs/cli-commands.md:358`). They
  need `Board::serialize()`/`deserialize()`, which are **designed and unbuilt**:
  `DESIGN.md` §4 draws the shape and states plainly that nothing in `src/`
  implements it and no board author writes either method. This is the single
  largest unbuilt piece of the design.
- **`ReplayStream`** — the last unimplemented `ByteStream` (`DESIGN.md`:716).
  Blocked on `RECORD`/`REPLAY` above.
- **Symbolic annotation of disassembly** — `JMP 0100` → `JMP START`. Symbols
  already resolve anywhere a true address is *typed*, and the address→name map
  is built. Display is deferred because an `Insn` melts its operand into text
  before anyone sees it, and `%W` is not always an address (`LXI B,%W` is a
  constant, `JMP %W` is not) — correct annotation needs an operand-kind split
  across both opcode tables (`DESIGN.md`:1473).
- **Phantom / bank annotation on `DUMP` and `DISASM`** — planned so a confusing
  read explains itself; neither handler does it. `SHOW BUS MAP` carries the only
  overlay annotation there is, which means the confusing read is still confusing
  at the point you meet it (`DESIGN.md`:1415). **May belong in Declined rather
  than here** — `src/cli/monitor.cpp:1933-1934` argues the other way, that
  banking and phantoming are properties and selecting what you want to look at
  is what the guest has to do too. Needs a call before anyone builds it.

### CLI and console

- **Tab completion** — `src/main.cpp:318`, `DESIGN.md` §10.4. Driven by the same
  `Board::properties()` reflection everything else uses: command names, then
  board ids, then that board's property names, then that property's legal enum
  values (`SET sio2a BA<Tab>` → `BAUD=`). No completion tables to maintain, and
  a board added later is completable the day it lands. There is no Tab handling
  in the line editor today.
- **Line editor gaps** — history persisted to `~/.altairsim_history`, Ctrl-R
  search, word motion, Ctrl-K, Home/End (`DESIGN.md`:1499).
- **Console properties** — `rows`, `cols`, `pace`, `ansi`, `tabs`, `log`
  (`DESIGN.md` §7.2). `docs/config.md:229` already flags them "not built yet" and
  `SHOW CONSOLE` does not offer them, so the documentation is honest — this is a
  feature gap, not drift. The four transforms that did land (`upper`,
  `strip7in`/`strip7out`, `crlf`, `bsdel`) settled the question the list was
  really asking, which is *where* a transform belongs.
- **Display properties** — `focus` is the only one. It landed with the video
  window's focus fix (2026-07-19): a static `Property` on the `Display` seam
  (`src/host/display.h`), reachable as `SET DISPLAY focus=on`, `SHOW DISPLAY`
  and a `[display]` table, and static on purpose so it is answerable before any
  window exists and in a build with no SDL at all. Everything else about the
  window is still a compile-time constant — the scale ceiling and the title-bar
  allowance (`kMaxScale`, `kChromeH`, `src/host/display_sdl.cpp`), the palette
  the board hands down, and the special-key table behind `Display::SpecialKey`
  that [#59] wants exposed. None is urgent; recorded so the one-knob table is a
  known shape rather than an oversight, and so the next one has somewhere to go.

### Configuration

- ~~**`writeprotect` as a TOML alias for `readonly`**~~ — **DONE 2026-07-19.**
  `writeprotect = true` is accepted in a `[[board.drive]]`; `readonly` is
  unchanged and is still the only spelling written back, so no existing machine
  file moves and none of the ones we ship gain a second vocabulary.

  Done as `Property::aliases` in the schema rather than another string compare in
  the board, which is what makes the rest of it free: the same validator (an alias
  cannot become a laxer door), the generated reference, the MCP schema and `SHOW`
  all say so without being told, and both floppy controllers got it off one line
  because they are one class. `Board::loadSubUnit` canonicalises at the door, so
  no board learns its key has two names.

  Two spellings of one key in the same table is an **error**, not a last-wins:
  TOML already refuses a repeated key, so it is the only way to write the setting
  twice, and either answer would be a file saying two things with one of them
  silently happening.

  Original entry: the CLI says WP and "write-protected" everywhere while
  `[[board.drive]]` said `readonly = true`. Deferred by Patrick 2026-07-16 when
  PR #12 merged.

### Media

- **Print to a real printer — a `printer:QUEUE` endpoint** — [#70]. Design note
  in `docs/printing.md`; the open questions are on the issue, for comment.
  Nothing built. Printed bytes can go to `file:PATH` or
  `null` today, which is a capture, not a printout. The platform research is
  settled: two implementations, not three — WinSpool with datatype `RAW` on
  Windows, the CUPS C API with a raw queue on macOS *and* Linux — selected by
  CMake so no call site carries an `#ifdef`.

  **It is an endpoint, not a board feature**, and that is the load-bearing
  call. The grammar lives only in `resolveEndpoint` (`src/host/endpoint.cpp`)
  and no board may parse an endpoint string, so one `ByteStream` serves the
  88-C700, the unbuilt 88-LPC, the Sol-20 printer unit, the 88-SIO and 88-2SIO
  (a serial printer was as ordinary as a parallel one) and any card written
  later — with not one line changed in any of them. Putting it on the C700
  would mean writing it four times and watching the four drift.

  **The open problem is the job boundary.** Centronics has no end-of-job signal
  and neither does a serial line; a real line printer settled it with continuous
  paper and an operator who tore it off. A host queue wants a finite job. The
  proposal is an idle timer — every byte restarts it, expiry submits the buffer
  — with `idle`, `onff` (form feed also ends a job) and a `max` byte ceiling as
  parameters, plus an operator verb to force one out. **The parameters live in
  the endpoint spec** (`printer:linewriter?idle=15`), settled 2026-07-19: the
  resolver owns the grammar, and a spec round-trips through `describe()` into
  `SHOW` and `CONFIG SAVE` without a board learning a printer-shaped property.
  Three findings constrain
  it: the timer is **wall** seconds, not the Clock, since the default clock is
  free-running and nothing here is guest-readable (same rule as the VDM-1 blink,
  PR #69); `pump()` only runs inside the run loop (`src/cli/monitor.cpp:1082`),
  so a halt, a breakpoint or ATTN right after a report would strand the buffer
  unless stop/reset/`DISCONNECT`/exit flush it too; and `flush()` cannot carry
  the boundary, because the C700 already calls it every pump
  (`src/boards/mits-88c700.cpp:132`) and every idle slice would submit an empty
  job. The C700's PRIME line is a *start*-of-job signal, not an end.

  Build it optional the way SDL3 is (`CMakeLists.txt:250`) — detect quietly,
  and where CUPS is absent leave `printer:` out of `endpointHelp()` rather than
  failing to configure a build that works today.

- **A tape counter and a `WIND` verb** — [#48]. A mounted tape has a position
  (`TapeImage::pos()`, shown by `SHOW MOUNTS`) but the only control is `REWIND`,
  which sets it to zero. Put two programs on one tape and the second is
  reachable only in the session that recorded it. Filed 2026-07-18 with the
  design questions open (what number the counter is; what the operator types)
  and **deliberately not implemented** — waiting on comments.

- **No way to create a blank tape** — found 2026-07-18 while correcting the
  manual's recording section. `MOUNT` refuses a file that does not exist
  (`openHostFile`, `src/host/media.cpp`), and nothing in the CLI creates a media
  file, so recording can only ever write back over a tape already in hand. Two
  parts to it:
  - **The trap, which is the sharper half.** The format sniff goes by magic
    (`openTapeMedia`, `src/host/tapecodec.cpp`), so an empty file — or any
    non-RIFF file named `.WAV` — falls through to `raw` and mounts as a *byte*
    tape. Recording then puts bytes in it and reports success. Nothing will play
    the result. Silent, and it looks like it worked.
  - **The design question, and why this is not just a missing `touch`.** An
    `AudioTapeMedia` inherits its format and sample rate from the decode at
    mount. A blank has neither, so creating one means both must be stated:
    `format` would have to mean something other than `auto`, and the sample rate
    has no property at all today.
  Documented as a limitation for now (`docs/manual/tapes.md`); not filed as an
  issue pending a call on whether creation belongs in `MOUNT` or a verb of its
  own.

- **A mount can only vouch for framing, never for data** — found 2026-07-19
  while documenting the CUTS format. `DemodResult::confidence()` is
  `bytes / (bytes + framingErrors)`: it reports that start and stop bits landed
  where they belonged, and cannot say the eight bits between them are the ones
  recorded. deramp.com's archived `TRK80.WAV` is the counterexample — it frames
  at 99.7% and 6,778 of its 7,840 payload bytes are wrong. The **wording** is
  fixed (the mount line now says "of frames intact", not "clean", and
  `docs/manual/tapes.md` says what the number does not cover), but the
  **capability gap** is open: a tape can clear `kTapeConfidenceFloor` comfortably
  and still be unloadable, and the operator finds out when the program crashes.
  A SOLOS tape carries its own answer — a header checksum and a checksum after
  every 256-byte block (see `examples/sol/make-trek80-tape.sh`), and the archived
  tape's header checksum `D9` verifies even though its payload does not. So the
  data *is* checkable for CUTS. The question is layering: `tapecodec.cpp`
  demodulates and knows nothing of SOLOS file structure, and teaching the codec
  one guest's format would be the wrong seam. Probably belongs in `tapetool` as
  a `verify` verb first, where being format-aware is the point.

### MCP

Built: `run`, `send`, `recv`, `regs`, `who`, `bus_map`, `bus_io`,
`bus_contention`. Unbuilt, and marked as such in `DESIGN.md` §11:

- `expect(pattern, max_steps, idle_threshold)`
- `screen() -> {rows, cols, grid}` — a VT100/ANSI screen emulator
- `step`, `breakpoints`, `snapshot`, `restore`, `bus_trace`
- `mem_save`, `mem_search`, `mem_fill`, `disasm`
- `mount`, `connect`
- `bus_irq()`

The snapshot/restore tools are blocked on board serialization, above.

### Documentation and packaging

#### Take the release notes out of the manual; ship `changelog.pdf` instead

**Patrick, 2026-07-20 — for the next release.** v0.2.0 added
`docs/manual/whats-new.md` as a manual chapter. That was the wrong home: the
manual describes *the program as it is*, and a chapter that describes *what
changed* ages differently from everything around it — by 0.4.0 the manual would
carry three of them, or one that has quietly become a changelog with a chapter
number.

So: **remove the chapter, and add a separate `changelog.pdf` to the
distribution.** The archive gains a document; the manual loses one.

What that touches, none of it hard but none of it optional:

- **`docs/manual/whats-new.md` and its `ORDER` line go together.**
  `tests/acceptance/docs-manual.cmake` fails a chapter file that is not in
  `ORDER`, so deleting one without the other is a red test either way round.
- **A changelog needs a source and a build step.** `tools/build-docs.sh` builds a
  document from a directory with an `ORDER` in it; the changelog is one file and
  wants either its own tiny source dir or a special case. Decide which before
  writing it.
- **`docs/package.map` needs a `FILE` line for it**, which is also what puts it in
  the archive — the map is the only source of truth for package contents.
- **`docs/manual/package.md` must name it.** That chapter lists what is in the
  archive, and the manual may only name paths that actually ship.
- **Whether `docs.yml` builds and commits it like the other two PDFs.** If it does
  not, the release inherits the pandoc-version trap v0.2.0 hit: `build-package.sh`
  rebuilds with the local pandoc (3.10 via brew) while `docs.yml` pins 3.6, and a
  different pandoc is a different document.

Open question worth settling first: **does the changelog cover 0.2.0 retroactively,
or start at 0.3.0?** The 0.2.0 content already exists as prose in `whats-new.md`
and in the v0.2.0 release notes, so retroactive is cheap; starting fresh is
cleaner but loses it.

#### The table of contents needs improving, or removing

**Patrick, 2026-07-20.** As it stands the contents page lists chapters only
(`tools/build-docs.sh`, `--toc-depth=1`), and it is the **only** navigation the
PDF has: `--print-to-pdf` emits no bookmark tree at all — verified, zero
`/Outlines` in the file. So a reader who wants a subsection reaches it through its
chapter or by text-searching, and a reader in a PDF viewer gets no sidebar.

Both directions are open, and they are genuinely different bets:

- **Improve it.** The real fix is probably not a deeper `--toc-depth` — depth 2
  was tried and listed all 146 subsection headings across five pages, which is
  what got it cut. It is to emit a **real PDF outline**, which `--print-to-pdf`
  cannot do; that means a different PDF path (a LaTeX/Typst engine, or a
  post-pass that injects an outline), and that is a build-chain change, not a flag.
- **Remove it.** If the document is navigated by search anyway, a chapter-only
  contents page costs pages and earns little.

**Note that `build-docs.sh`'s own comment currently forbids reopening this** — it
says that if a chapter grows big enough to need finding-by-subsection, the answer
is to split the chapter. Whichever way this goes, that comment is now stale and
gets rewritten with it, or the next person reads it as settled.

#### All distributed packages must ship with SDL3

**Patrick, 2026-07-20.** Every distributed package is to contain the SDL3 library, for
all three (or four) operating systems, so that the video boards open a real window out
of the box. Today none of them do — see *Every distributed binary is headless* under
Bugs.

**This does not have to happen on GitHub, and does not have to happen in CI.** The
packages carrying SDL3 are for distribution on **altairsim.com**; if the only way to get
them is to build each platform locally, that is acceptable and is the instruction. What
matters is that a package can be **built and packaged locally for every target**, not
that a workflow does it.

That reframes the release story rather than extending it. `build-package.sh` currently
assembles one tree and takes whatever `build/altairsim` happens to be; v0.2.0 then had
each platform's CI binary swapped in by hand. Shipping SDL3 means each platform's package
has to come from a machine that *has* SDL3 for that platform, so the binary and its
library travel together.

The mechanics that have to be decided, none of them settled:

- **Linking and layout.** Static SDL3, or dynamic with the library beside the binary? If
  dynamic: `SDL3.dll` next to the `.exe` on Windows; a bundled `.dylib` with an `@rpath`
  that survives being unzipped anywhere on macOS; a `.so` plus `$ORIGIN` on Linux. This is
  the part that most often ships "working on the machine that built it" — and this tree is
  already in that state. A local macOS build links
  `/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib` **by absolute path**, so zipping today's
  `build/altairsim` and handing it to anyone without that exact Homebrew prefix produces a
  binary that will not start. Bundling the dylib and rewriting the install name
  (`install_name_tool`, `@rpath`/`@executable_path`) is mandatory, not a refinement.
- **macOS universal is the hard one, and it is measured, not assumed.** Homebrew's SDL3 is
  `arm64` only — `lipo -archs /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib` says exactly
  that, which is the whole reason `-DALTAIRSIM_ENABLE_SDL=OFF` exists. So a windowed macOS
  build today is Apple Silicon only. Either build SDL3 from source as a fat binary and link
  that, or stop shipping one universal package and ship `x86_64` and `arm64` separately.
  Worth deciding early — it is the difference between three packages and four.
- **The fourth would be the macOS split** (Patrick, 2026-07-20): Intel and Apple Silicon as
  separate packages rather than one universal build — *if* it turned out to be forced.

  **IT IS NOT FORCED. Checked 2026-07-20, so there are three packages, not four.** What was
  ever measured is only that *Homebrew's* SDL3 is `arm64`-only, which is a fact about brew
  and not about SDL3 or about fat binaries. SDL upstream's own macOS release
  (`SDL3-<ver>.dmg`, from `libsdl-org/SDL`) ships an **`SDL3.xcframework` whose
  `macos-arm64_x86_64` slice is genuinely universal** — verified with
  `lipo -archs …/SDL3.framework/Versions/A/SDL3` → `x86_64 arm64`. Link that instead of
  brew's and a universal `altairsim` keeps its window on both architectures.

  So macOS stays **one** universal package. Building SDL3 from source with
  `-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"` is the fallback if the framework proves
  awkward to consume, but it should not be needed.

  Note the knock-on: consuming a `.framework` is not the same as consuming brew's
  `.dylib` — `find_package(SDL3 CONFIG)` wants the framework's config, and the packaged
  layout becomes an embedded framework rather than a loose dylib. That is a build-glue
  question, not an architecture one.
- **`package.map` gains the library**, since it is the only source of truth for package
  contents — and `docs/manual/package.md` then has to name it, because the manual may only
  name paths that ship.
- **SDL3 is zlib-licensed**, so bundling it is straightforward; record its licence in the
  package alongside `LICENSE` rather than treating it as a blocker.
- **Where SDL3 itself comes from — fetched, or built and tracked?** Patrick asked
  (2026-07-20) whether to build SDL3 from source into an untracked directory in the repo
  and track only the resulting libraries. Both halves are workable; the second is the one
  to think about.

  Measured sizes, 3.4.12: macOS universal framework binary **5.0M**, Windows x64
  `SDL3.dll` **2.7M**, Linux `.so` ~3M — call it **~11M per SDL3 version**, against a
  67M `.git` today. Tracking is *permanent and compounding*: every version bump adds
  another copy that cannot be reclaimed, and `.gitignore`'s own header records that
  vendor binaries were "burned into this repo's history twice already".

  **Cross-compiling is not required, because the hardware exists.** Patrick builds and
  tests natively on macOS Apple Silicon, macOS **Intel**, Windows 10 and Ubuntu, and can
  spin up a VM for any x86 Linux (2026-07-20). So each platform's SDL3 gets built on that
  platform, which is the straightforward path and sidesteps every cross-toolchain problem.
  The remaining care is Linux-specific: a `.so` built on a current Ubuntu will not load on
  older distros, so build that one against an old-glibc baseline (a container or an older
  VM) if the package is meant to travel.

  **The repo already has the other pattern**, twice: `tools/fetch-disk-images.sh` and
  `tools/fetch-ci-binaries.sh`. A `tools/fetch-sdl3.sh` pulling upstream's official
  prebuilts into an untracked `third_party/sdl3/`, checksum-verified, keeps git history
  flat and costs one command on a fresh clone. Upstream ships macOS universal and all
  three Windows arches already; **only Linux has no official prebuilt**, so that is the
  single platform actually needing a from-source build.

  **Or Git LFS, which is the third option and is cheaper than it looks.** Checked
  2026-07-20: GitHub's included allowance is now **10 GB of storage AND 10 GB/month of
  transfer** for Free *and* Pro (data packs are gone; overage is metered at $0.07/GiB
  stored, $0.0875/GiB transferred, billed to the **repo owner**). At ~11M of libraries and
  three CI legs per push that is roughly 300 pushes a month before the quota notices, so
  the "permanent history growth" objection above largely dissolves — LFS objects do not
  bloat a clone the way committed blobs do, and `actions/checkout` only pulls them with
  `lfs: true`.

  Note `deltecent` is a **User** account: a paid *personal* plan does not raise this.
  Free and Pro are both 10 GB; only Team/Enterprise get 250 GB.

  **The LFS trap is conversion, not cost.** `git lfs migrate import` rewrites history and
  changes every commit SHA from the conversion point forward — with `v0.1.0` and `v0.2.0`
  published on a public repo, that invalidates both tags and breaks existing clones. So if
  LFS is used, use it for **newly added** files only.

  **The trade to settle:** fetching needs the network once and keeps git clean; tracking
  (plain or LFS) works offline. If local packaging must work genuinely offline, LFS is the
  better of the two tracking routes. Whichever is picked, pin one SDL3 version and bump
  rarely.

  **Not a factor either way: where the finished packages are hosted.** Distribution is
  altairsim.com. That is independent of this decision, which is only about where the
  libraries live as *build inputs*. (For the record, a GitHub Release could also carry an
  SDL3-bearing package — Release assets are file hosting, and v0.2.0's archives were built
  locally and uploaded by hand. v0.2.0 shipped without SDL3 because its binaries came from
  CI, not because a Release cannot carry one.)

**Prerequisite, and it is the honest ordering:** nothing currently proves `display_sdl.cpp`
even compiles (Bugs, above), and the Windows+SDL3 recipe is not written down (next item).
Both of those are in the way of building these packages by hand repeatably.

#### Building on Windows with mingw is untried, and undocumented

**Raised by a user; recorded 2026-07-20.** Every Windows instruction we ship assumes
MSVC — `docs/building-windows.md` walks through installing Visual Studio or the Build
Tools, insists on a *Developer* shell, and describes the Visual Studio generator. A
reader with mingw-w64 has nothing to follow.

**Nothing in the source is knowingly MSVC-only**, which is the encouraging half. Checked
2026-07-20:

| | |
|---|---|
| `_MSC_VER` / `__declspec` in `src/` | **none** |
| Windows headers | already lowercase (`<windows.h>`, `<winsock2.h>`) |
| entry point | plain `int main(int, char**)` — no `wmain`, so no `-municode` |
| `if(MSVC)` in `CMakeLists.txt` | has a working `else()`; mingw gets `-Wall -Wextra -Wpedantic` |
| `ws2_32` | linked by CMake `target_link_libraries`, not only by a pragma |
| the old `strncasecmp` portability fix | **gone** — no case-compare intrinsic anywhere in `src/` |

**One real MSVC-ism:** `src/platform/win32/socket_win32.cpp:18` has
`#pragma comment(lib, "ws2_32.lib")`. GCC ignores it, but `-Wall` implies
`-Wunknown-pragmas`, so mingw warns on every build. It is redundant on *every*
toolchain — CMake already links `ws2_32` — so guard it with `#ifdef _MSC_VER` or delete
it.

**"Should build" is not "builds", and that is the whole item.** The work is to actually
run it on Windows with mingw-w64 (`cmake -B build -G Ninja`), fix whatever falls out —
`-Wpedantic` noise from Windows headers is the likeliest — and then write the recipe
into `docs/building-windows.md` *alongside* the MSVC path, not instead of it. Patrick
has a Windows 10 box, so this needs no CI leg to answer; a CI leg is only worth adding
afterwards, to keep the answer true.

**It also connects to the SDL3 work above:** upstream ships
`SDL3-devel-<ver>-mingw.tar.gz`, so mingw is a supported SDL3 configuration and may
well be the *easier* route to a windowed Windows package than vcpkg + MSVC.

#### The developer guide is thin on building under Windows with SDL3

**Patrick, 2026-07-20.** `docs/devguide/building.md` covers SDL3 detection well in
general — `find_package(SDL3 CONFIG)`, the `ALTAIRSIM_ENABLE_SDL` macro, the
headless fallback — but its entire Windows instruction is the one line
`vcpkg install sdl3`. That is the easy half. The half that stops people is wiring
vcpkg into the configure step (`-DCMAKE_TOOLCHAIN_FILE=...`, the triplet, and
where `SDL3Config.cmake` has to be for `find_package` to see it), and none of it
is written down.

`docs/building-windows.md` is worse: it mentions SDL **zero times**, so the
document specifically about building on Windows never tells you how to get a
window.

**This cannot be copied out of CI, which is the catch.** No workflow installs
SDL3 — `grep -i sdl .github/workflows/` finds nothing — so every CI leg,
Windows included, builds headless. There is no known-good Windows+SDL3 recipe in
the tree to transcribe. Somebody has to do it on a real Windows box, note what
actually worked, and write that down; and the obvious follow-on is whether the
Windows CI leg should then build *with* SDL3 so the path stays proved.

---

## Blocked on documentation

`DESIGN.md` §17 keeps the table; these cannot be built honestly from what is
known, and guessing would be inventing hardware.

- **88-PIO and 88-4PIO** — bit layouts and the CA1/CB2 handshake semantics are
  unknown (`docs/roadmap.md:247`, milestone 7).
- **88-HDSK** — ports, command protocol, geometry and image format all unknown;
  nothing in tree.
- **88-TURNKEY / PROM power-on jump** — undocumented. Modeling POJ as a board
  property would also require adding an opcode-fetch `Cycle` kind. Nice to have;
  blocks nothing.
- **PMMI E1–E7 → VI0–VI7 pad correspondence** — the manual says only to consult
  your CPU/VI card manual. Everything else about the PMMI is recovered.

---

## Deferred

- **"Card" still outnumbers "board" in comments and the developer docs.** The
  rule is now stated (`DESIGN.md` §0.3: *board* for the object, the command and
  the table; *card* only where the sentence is about the physical historical
  artifact), and the surfaces a user reads were brought in line with it — every
  user-visible string in `src/`, the whole of `docs/manual/`, and `ref/`
  regenerated from the binary. What was **deliberately not swept** (counts
  re-measured 2026-07-19): roughly 730 occurrences in code comments, 58 in the
  shipped machine-file comments, and the developer docs
  (`docs/devguide/theory.md` at 66, `adding-a-board.md` at 32). Those are internal prose, the cost is churn against every line reference
  in this file and in `DESIGN.md`, and the gain is nil for anyone who is not
  reading the source. New code and new docs follow §0.3; the back catalogue is
  left alone on purpose. **`CpuCard`/`Machine::cpuCard()` also stay** — 13
  tokens, the only `Card`-suffixed class among sixteen `Board`-suffixed ones,
  and renaming a class to settle a synonym is not worth a merge conflict.
- **UART 1602 flow control** — pushed at the stream rather than modeled in the
  chip (`src/chips/uart1602.cpp:43`).
- **Host-side CP/M filesystem access** — no `DISK` verbs in the monitor, no
  `disk_*` tools over MCP (`DESIGN.md` §12.2). Deferred, not cancelled. A
  listing needs the *controller's* sector layout **and** the *image's* DPB and
  skew, and no generic command can infer that pair; `DiskImage` already solves
  the first half in the right place (the controller declares the layout), and
  the second half — a named CP/M filesystem descriptor — is a later design task.
  `cpmtools`' `diskdefs` is the precedent, and it exists precisely because this
  cannot be inferred. A naive `DISK LS` would need a wrapper per controller ×
  per image format, and its apparent genericity would be a lie: it would produce
  a plausible-looking directory listing off a misparsed image.
- **Tarbell SD floppy controller** — not being built (Patrick, 2026-07-12); the
  spec is retained in `docs/boards/tarbell-sd.md`. The real card is a
  pre-IEEE-696 July 1977 design with **no PHANTOM\*** — it asserts STATUS
  DISABLE\* and drives the S-100 status lines itself, so modelling it honestly
  means building status lines in order to have something to disable. Kept as
  *deferred* rather than declined on purpose: `src/core/board.h` and
  `tests/test_phantom.cpp` both key off that distinction (see *Housekeeping*).
- **PMMI MM-103** — deferred out of the implementation plan: the 88-2SIO already
  exercises every interface it would. It stays in the design as a paper
  exercise, and that is not a consolation prize — it is the hardest board in the
  catalog to express (read/write asymmetry at one port, a read whose only
  purpose is a side effect, write-only registers needing shadow copies, one
  register shared between the baud divisor *and* the dialer *and* the interrupt
  mask, non-queueing interrupts, both interrupt models), which makes hand-tracing
  it the best available test of whether the board API is right. Recovered
  register map: `docs/boards/pmmi-mm103.md`.

---

## Declined

Settled. Recorded here so they are not proposed again.

- **`SET <board> ENABLED=OFF` as a runtime property** — `DESIGN.md`:387 argues
  for it as a *planned* operator switch and is marked unbuilt. `Board::enabled_`
  is a plain C++ field with a non-virtual setter, reachable from **no
  `Property`**, so `SET mem0 enabled=off` is refused and a TOML file carrying it
  will not load. Do not "restore" `enabled` to a properties table in any
  document.
- **A free-running (`baud = 0`) UART** — declined 2026-07-14. `baud` is not the
  analogue of `clock_hz`: it is a duration in T-states, the guest observes it,
  and it is the 6850's only flow control. MITS PS2's one-byte mailbox proves it.
- **Making all paths cwd-relative** — proposed twice and declined 2026-07-16. A
  path *inside* a machine file is relative to that file; a path *typed* is
  relative to the shell. The pains it would fix are already fixed.

---

## Test coverage lost or missing

### Per-drive geometry is no longer proved — `acceptance-dcdd-mixed` was removed

Removed 2026-07-19 with the 8 MB image it needed. It put an 8 MB disk in drive 0
and a 77-track floppy in drive 2 of one 88-DCDD (the FDC+ manual's own
arrangement, 3.7.4), installed the host-bridge utilities into scratch copies of
both, and weighed `R.COM` byte for byte coming back off the floppy — because the
failure mode when the format or the Spindle stops being **per drive** is
*corruption*, not an error.

It needed an 8.6 MB download, so it skipped on every fresh clone and in all of
CI: a test that reads as protection and is not. **Rebuilding that coverage
without the download is the open task** — two different geometries in one
controller does not inherently need a large image, only two images whose
geometries differ.

The three stale references this entry used to carry — `README.md`,
`tools/install-hostbridge-utils.sh` and `tools/fetch-disk-images.sh`, all citing
a test that no longer exists — were fixed by `9c9c06c`. `CMakeLists.txt:915`
carries the tombstone comment and is correct.

### `acceptance-hostbridge-build` is no longer registered

Same date, same reason. `tests/acceptance/hostbridge.cmake` keeps its
`MODE=build`, and it is now run **by hand** when one of `cpm/hostbridge/*.ASM`
changes:

```sh
tools/fetch-disk-images.sh
cmake -DSIM=build/altairsim -DSRC=. -DBIN=build -DMODE=build \
      -P tests/acceptance/hostbridge.cmake
```

It cannot move to the tracked floppy: PIPing the three `.ASM` in needs 78 KB of
free disk and that image arrives with 18K. So on a fresh clone the committed
`.HEX` is checked against the committed `.COM`, and the `.ASM` behind them is
checked by nobody unless you run the above.

### Nothing checks the manual against the package

**The false claim is fixed** (2026-07-19): `docs/package.map` said
`tests/acceptance/manual.cmake` builds the package somewhere with no repository
in sight and runs the manual's own commands against it. No such file has ever
existed. The comment now describes the two tests that are real —
`docs-manual.cmake` (greps the chapters for references outside the package,
checks `ORDER`) and `examples.cmake` (boots `examples/basic` and `examples/cpm`
from a scratch directory) — and says plainly what neither of them does.

**The gap is not fixed, and it was never a documentation bug.** Nothing
assembles the package layout and checks the manual against it, because
`tools/build-package.sh` is run by no workflow and no test — only by hand. That
is why the manual could promise "CP/M in one command" while the published
archive contained no media at all, and it is why v0.1.0's archives were
assembled by hand without it.

`acceptance-examples` covers the load-bearing half, the examples themselves —
though only two of the four: `examples/sol` and `examples/diskbasic` are not in
it. Note the shape of the original failure before writing the fix: a *claimed*
test is worse than a missing one, because it is the missing test plus the belief
that you have it. This entry has now been the evidence for that twice.

**And the manual's TRANSCRIPTS drift too, which is the half nobody is watching.**
The demonstrated case, found and **fixed** 2026-07-19 in `docs/manual/tapes.md`:
the `MOUNT ... TRK80.WAV` example printed `7932 bytes, 27 framing errors (99.7%
clean)` and the ACR refusal below it printed `2398 Hz / 1206 Hz`. Both had been
captured from deramp.com's *archived* recording, which has never been in the
tree, while the tape that actually ships is synthesized by
`examples/sol/make-trek80-tape.sh`. Every figure on both lines was wrong. They
now read `7939 bytes, 0 framing errors (100.0% of frames intact)` and `2390 Hz /
1205 Hz`; the surviving `99.7%` at `tapes.md:173` is deliberate, describing the
archived rip as a cautionary example.

**The gap that produced it is still open.** No test could tell — the repo's rule
is that transcripts are captured rather than composed, but nothing re-captures
them. A check that runs the manual's own fenced commands and diffs the output
against the fence is the missing piece; it would have caught this and the "CP/M
in one command" gap in the same pass.

**Two more instances, found 2026-07-19 and re-captured the same day**, which is
the point: the first one was not a one-off. (A third, the refused `MOUNT` at
`docs/manual/machines.md:265`, was fixed by `9c9c06c`.)

- `docs/manual/machines.md` — the `SHOW MOUNTS` transcript showed `drive0` and
  `drive1` only, omitting `drive2` and `drive3` (the CP/M machine derives from
  `machines/default.toml`, `drives = 4`, and `monitor.cpp:633-644` iterates
  every mountable unit), and dropped the trailing `Paths are AS WRITTEN.  SHOW
  PATHS says what they are relative to.` footer, which `monitor.cpp:662` emits
  unconditionally after every non-empty `SHOW MOUNTS`. The prose below it then
  referred to paths being printed *as written* — describing a footer the
  transcript did not show. Both fixed; the prose now points at the footer
  rather than paraphrasing it, and a line says why empty drives are listed.
- `docs/manual/package.md` — the `basic4k` `SHOW MOUNTS` transcript was missing
  the same footer. Fixed.

Both shared a shape worth naming: a transcript that was **trimmed** when
captured is indistinguishable, later, from one that **drifted**. Whatever
re-capture check gets built has to decide whether trimming is allowed at all —
if it is, it needs a marker in the fence, because otherwise every legitimately
abridged transcript is a permanent false positive and the check gets switched
off.

**Counted prose drifts the same way, and nothing checks that either** (found
while re-capturing the two transcripts above, 2026-07-19). Shipping
`examples/diskbasic` made a fourth example, and the manual went on saying three
in six places in `package.md` plus `introduction.md` and `README.md` — while
`examples.md` had already grown a `## 4.` section, so the manual contradicted
itself. `package.md` and `introduction.md` also both said **twelve** built-in
machines against `--list`'s thirteen. All corrected. The board count
(*fourteen*) was checked against `BOARDS TYPES` and is right. A count in prose
is a captured transcript with the fence taken off: same failure, less visible,
and the re-capture check above would not catch one.

### `SHOW PATHS` calls a machine file built-in when it is in the cwd

`monitor.cpp:697-706` prints `(none -- this machine is built in to the binary)`
whenever `m_.dir` is empty — but `m_.dir` is the *dirname of the path as given*,
so naming a machine file in the current directory makes it empty and the machine
is reported as built in:

```
$ cd examples/cpm && altairsim cpm22-buffered.toml -x "SHOW PATHS"
  machine file       (none -- this machine is built in to the binary)
```

Run the same machine as `examples/cpm/cpm22-buffered.toml` from the repo root
and the line is correct. This is the one case the manual calls out by name
(`machines.md`, "Boot a **built-in** machine and the middle line says so"), so
the command lies precisely where the manual sends you to look — and `cd` into
the example folder is how every example README tells you to start. Found
2026-07-19 while measuring the transcripts above; **not fixed**, being a source
change outside that task. Empty-means-absent is the bug: an empty dirname and no
machine file are different answers — the same shape as the `read() == 0` bug in
issue #25, where a quiet tty and an ended pipe shared one value.

### Eleven tests do not run on Windows

`CMakeLists.txt:687-688` gates eleven tests on `find_program(EXPECT_EXECUTABLE
expect)`, and the Windows runner has no `expect`. This is a **live** gate, not a
dead one, and it is deliberate — but it means four acceptance tests
(`acceptance-minidisk`, `acceptance-minidisk-control`,
`acceptance-dcdd-readonly`, `acceptance-ddt`) have Linux and macOS coverage
only. Surfaced 2026-07-19 when the dead `if(EXISTS ...)` gates came off (PR #73)
and the tests began running in CI for the first time: Linux ran 27, Windows 16.

Not a regression, and not urgent. Recorded because "runs in CI" now means two
platforms out of three for this group, and the next person to read the test list
should not have to rediscover why.

### ~~CI checks out at depth 1, so a CI binary cannot name its commit~~ — FIXED 2026-07-19

`fetch-depth: 0` added to the three workflows that build a binary — `ci.yml`,
`cpu-exerciser.yml`, `cpu-exerciser-release.yml`. Verified by configuring a
`--depth 1` clone and a full one side by side and reading the generated
`version_generated.h`: `"88527de"` against `"v0.1.0-74-g88527de"`.

**`docs.yml` was deliberately left at depth 1** and now says so in a comment. It
builds no binary, and its only stamp is `git rev-parse --short HEAD`, which needs
neither a tag nor ancestry.

`ci.yml` also gained a `Version` step that runs the built binary with `--version`
on every leg. Nothing else in the run would fail if the checkout regressed to the
default depth — the artifacts would build, the tests would pass, and the binaries
would quietly stop naming their tag. The log line is where that is visible.

The original entry follows.

`--version` now reports `git describe` output (PR #73), but `.github/workflows/`
checks out with `fetch-depth: 1`, so no tag is reachable and a CI-built binary
reports a bare short sha rather than `v0.1.0-N-gsha`. Still traceable, just less
legible. The fix is a full-history fetch in every job, which is why it was not
taken automatically.

### ~~The 16-drive path is untested~~ — FIXED 2026-07-19

Closed by the `sixteen drives on the daisy chain` section in `tests/test_dcdd.cpp`:
`drives = 16`, a disk in `drive15`, a select of `0x0F`, and the byte read back off
the data port. **The status byte was the wrong witness** — an empty drive is still
`online()`, so it reads back identically to a loaded one, and a first draft of this
test passed with the mask mutated to the MDS's `0x03`. Drive 3 therefore holds a
disk of its own, and the check is on which fill comes out. Mutation-verified: with
`selectMask()` set to `0x03` three checks fail. The original entry follows.

`dcdd` declares `maxDrives() = 16` and `selectMask() = 0x0F`
(`src/boards/mits-88dcdd.h:39-40`), which is a real hardware fact — the
88-DCDD's select register carries a 4-bit drive address, four jumper wires on
the Disk Buffer card, four `DA-A..DA-D` lines on the daisy chain
(`reference/Altair Floppy (88-DCDD) Manual.md:89`). But **no shipped machine
file sets `drives` to anything but 4, and no test sets the property at all**, so
nothing above drive 3 is ever selected and the top two address bits are never
exercised. `test_machines.cpp:616` and `test_units.cpp:137` prove only that an
out-of-range unit is *refused*, at the default of 4.

Cheap to close: a `dcdd` with `drives = 16`, a mount into `drive15`, and a
select of `0x0F`. Worth doing because the 88-MDS deliberately differs
(`maxDrives() = 4`, `selectMask() = 0x03`) and the two share `HardSectorFdc` —
the divergence is exactly the kind that a shared base quietly erases. Found
2026-07-19 while checking whether "sixteen drives" in the manual was our
invention; it is not.

---

## Housekeeping

- **Inspecting the `lineprinter` machine creates a file.** `altairsim -x "SHOW
  MACHINE" lineprinter` drops a `printout.txt` in the working directory without
  a byte having been printed — the C700's `file:` sink is opened when the
  machine loads, not when the card first writes. Defensible (a `CONNECT` target
  is opened at connect time) and it is the machine's whole purpose, but a
  read-only inspection command leaving a file behind is a surprise. Low
  priority; noted rather than filed.
- **The snoop hooks kept for the Tarbell stay.** `src/core/board.h:258` and
  `:290`: `wantsSnoop()` and `snoop(const BusCycle&)` exist for a consumer that is on the shelf, and
  they are the only executable description we have of how a card that shadows
  low memory behaves — deleting them means rediscovering the
  combinational-release trap (`DESIGN.md` §4.2.1), which cost a day the first
  time. They come out only if the Tarbell is ruled **out** rather than deferred,
  and `tests/test_phantom.cpp` goes with them. It is deferred. Leave them.
- **The four-board API acceptance hand-trace is half done**
  (`docs/roadmap.md:276-291`). Boards 1 and 2 (88-2SIO, 88-DCDD) are discharged;
  3 (PMMI) and 4 (a DMA/Dazzler-shaped card) are outstanding. The point is to
  find an API defect on paper before writing code.
- **This file drifts against the tree, and nothing checks it.** Of five items
  picked off it on 2026-07-19, **two had already been done** — the
  `build-package.sh:7` header comment (fixed by `d20018e`, the very commit that
  introduced `examples/`) and the roadmap's milestone 4 marker (present since
  `3706100`). A third was half stale: `running.md` and `examples.md` were named
  as needing media notes and needed none, because they use only the shipped
  `{{MACHINE_*}}` tokens. Both dead entries were removed without a code change.

  The cost is not the wasted pass — it is that an item resolved silently is
  indistinguishable from one nobody has started, so the file's authority decays
  from the bottom. No mechanism is proposed here; recorded so the next person to
  work from this list treats a line-referenced claim as **a claim to verify**,
  not as a finding.

  **A full sweep of the file against the tree followed, same day.** Four entries
  were removed as done: the commit-in-`--version` feature and the dead
  `if(EXISTS ...)` gates (both landed in PR #73); the Sol-20 CUTS tape, which the
  WAV cassette work built without the entry being closed
  (`src/boards/proctech-sol.cpp` — `tapeUart_`, ports FA/FB, two decks, record
  and commit); and the burcon/lifeboat machine files, whose READMEs were given
  the manual-download wording by `3706100`. Six more were corrected rather than
  removed: half-stale claims, drifted line citations, and two counts.

  **`3706100` had silently closed two entries, and PR #73 two more** — which is
  the drift this note is about, now measured. The burcon/lifeboat entry also
  offered a *wrong* remedy ("add them to the fetch script"): `disks/` is not a
  deliverable, it is the provenance and optional-download tree that `examples/`
  was carved out of (`d20018e`), so pulling non-shipping media into the fetch
  path would have been a regression dressed as a fix. **A stale entry is not
  merely noise — its proposed remedy rots too.**

  **Every entry in this file has now been checked against the tree on
  2026-07-19** — which resets the clock but does not change the rule above.

  **That claim did not survive the day.** `9c9c06c`, the commit that performed
  the sweep above, fixed three things in the tree — the refused `MOUNT` in
  `machines.md`, the "sixteen drives" sentence in `disks.md`, and the three
  dangling `dcdd-mixed` citations — and left all three entries standing. They
  were read as open work on 2026-07-19 and only caught because someone asked
  whether they had already been done. **The sweep and the rot came from the same
  commit**, which is the strongest argument available that a periodic sweep is
  not the mechanism: the fix and the bookkeeping have to travel together, which
  is why *update this file before every commit* is now stated at the top.

  **And a fresh entry can be wrong in the other direction.** The
  `build-package.sh` subshell bug, written the same day, claimed all four of that
  script's guards were defeated. Three were not: a pipeline's status is its last
  command's, the `while` *is* last, so `set -e` fired and the script did stop.
  Only the `missing` variable was really lost, because a variable cannot escape a
  subshell the way an exit status can. Running the pre-fix script against each
  failure — rather than reasoning about it — is what separated them, and the
  first attempt at that measured nothing, because the copy under test resolved
  its own `$root` to the scratch directory and bailed before reaching any guard.
  **An entry asserting a bug is as much a claim to verify as one asserting a
  fix.**

[#26]: https://github.com/deltecent/altairsim/issues/26
[#43]: https://github.com/deltecent/altairsim/issues/43
[#48]: https://github.com/deltecent/altairsim/issues/48
[#70]: https://github.com/deltecent/altairsim/issues/70
