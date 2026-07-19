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

### The manual still names media beyond the three shipped examples

`examples/{cpm,basic,sol}` now ship and are booted by tests, so the quick start's
promise is finally true and `docs/package.map`'s tokens point at files a reader
has. What is **not** fixed is everything past those three: `disks.md`,
`file-transfer.md` and parts of `running.md` and `examples.md` still print paths
like the 8 MB `cpm-8mb.dsk`, the Burcon and Lifeboat CP/M variants and the
minidisk images as though you could type them. Those are correct in *form* and
impossible to run from a clone or from the archive.

Each such example wants a one-line statement that it needs an image you supply,
so a reader stops being told to type a path that cannot work. The alternative —
shipping the rest of the media — waits on the separate packages repository, which
does not exist yet.

### Wire `build-package.sh` into the release workflow

**Nothing runs `tools/build-package.sh`.** It assembles the archive the manual
describes, and no workflow, test or release step invokes it — `grep` over
`.github/workflows/` finds no reference to it. v0.1.0's three platform archives
were assembled without it.

The consequence is that two different archives exist and the documented one has
never been published:

| | Contents |
|---|---|
| What v0.1.0 shipped | `altairsim`, `README.md`, `LICENSE` |
| What `build-package.sh` builds, and the manual describes | `altairsim`, `altairsim-manual.pdf`, `USING-ALTAIRSIM.md`, `examples/{cpm,basic,sol}` **with media** |

The script works — run for real (it needs `pandoc`), the unpacked archive boots
CP/M from `examples/cpm/cpm22-buffered.toml` and mounts `examples/sol/TRK80.WAV`
to the byte-identical line the tapes chapter quotes. What is missing is only that
a release runs it.

Two smaller things to settle while doing it:

- **The built archive has no `LICENSE`.** `package.map`'s FILE table does not
  place one; the hand-built v0.1.0 archives did carry it. An MIT project's
  archive should — one `FILE` line.
- **`tools/build-package.sh:7`** still summarises the contents as `disks/
  tapes/`, which `examples/` replaced. The DIR loop below it is correct; only
  the header comment lies, and it is the file that defines the package.

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
- **Sol-20 CUTS tape** — the Sol-PC composite board ships without its cassette
  interface. Deferred when the board landed (PR #32).
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
- **A sample-machine TOML for the Eberhard ROMs** — `cdbl`, `hdbl`, `amon` and
  `acuter` are built-in (PR #30) but no machine boots one by name the way
  `altairsim basic4k` does.

### Debugger and monitor

- **`STOP`** — reserved and answers "not implemented yet"
  (`src/cli/commands.cpp:458`). It waits on a monitor that runs *alongside* the
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
  at the point you meet it (`DESIGN.md`:1415).

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
- **Open the video window without stealing the terminal's focus** — today the
  first frame from a video board opens an SDL window that becomes the active,
  key window, so a person typing at the `altairsim>` prompt (or at a guest's
  console) has their keystrokes go somewhere else mid-sentence. The window
  should come up unfocused and leave the terminal active; clicking it should
  still focus it, because for the Sol-20 that window *is* the keyboard.

  SDL3 can do this. In `SdlDisplay::ensureWindow`
  (`src/host/display_sdl.cpp:21`): set
  `SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0")` before creating the
  window, add `SDL_WINDOW_HIDDEN` to the `SDL_CreateWindowAndRenderer` flags,
  and call `SDL_ShowWindow(window_)` at the end of the function once the
  renderer, logical presentation and text input are set up. Creating hidden and
  showing explicitly is what makes the hint bite: it is consulted on the show
  path, and it also means the window never appears half-configured. Set
  `SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED` to `"0"` alongside it if we ever start
  calling `SDL_RaiseWindow`; nothing does today.

  **Do not reach for `SDL_WINDOW_NOT_FOCUSABLE`.** It looks like the same
  feature and is not: it makes the window permanently unable to take focus, and
  the video window is a real input device — the key sink wired in `main.cpp`
  feeds the Console, and the Sol-20's keyboard has no other home. A window you
  can never click into cannot be typed into.

  Two things to be honest about before building it. *Unfocused is not behind*:
  the hint guarantees the terminal stays the active window, but the video window
  may still be ordered visually in front of it, and SDL exposes no "order back"
  — on macOS the non-activating show is `orderFront:`. If the requirement is
  literally *behind*, that needs a platform call SDL does not wrap. And it is a
  hint on every backend other than macOS: a window manager is free to ignore it,
  so this is a best-effort behavior and cannot be asserted in a test. Whether it
  is unconditional or a knob is an open design call — someone driving a Sol-20
  wants the focus — but there is no `[display]` configuration section to hang a
  property on yet, so unconditional-with-a-flag is the smaller change.
- **Show the commit the binary was built from** — `--version` and the banner say
  `altairsim 0.1.0` and nothing more, so a binary in hand cannot be traced to a
  commit. Between releases that is most of them: every CI artifact, every local
  build, and anything a person was handed. `SHOW VERSION` should name the commit
  (and whether the tree was dirty when it was built), and `--version` should
  carry it too. Wanted for the ordinary case of someone reporting a bug against a
  build nobody can identify. The value has to come from the build — a CMake
  `git describe` at configure time, with an honest fallback when there is no git
  (a release tarball has none), because a version string that guesses is worse
  than one that says "unknown".

### Configuration

- **`writeprotect` as a TOML alias for `readonly`** — the CLI says WP and
  "write-protected" everywhere; `[[board.drive]]` still says `readonly = true`.
  Add the alias; **never break `readonly`**. Deferred by Patrick 2026-07-16 when
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

### `docs/package.map` claims a test that does not exist

`docs/package.map:20` says `tests/acceptance/manual.cmake` builds the package in
a directory with no repository in sight and runs the manual's own commands
against it. **That file does not exist.** Nothing assembles the package layout
and checks the manual against it, which is exactly why the manual could promise
"CP/M in one command" while the published archive contained no media at all.

`acceptance-examples` now covers the *examples* (it boots `examples/cpm` and
`examples/basic` from a scratch directory), which is the load-bearing half. What
is still missing is the manual-versus-package check.

**And the manual's TRANSCRIPTS drift too, which is the half nobody is watching.**
Found 2026-07-19 in `docs/manual/tapes.md`: the `MOUNT ... TRK80.WAV` example
printed `7932 bytes, 27 framing errors (99.7% clean)` and the ACR refusal below
it printed `2398 Hz / 1206 Hz`. Both were captured from deramp.com's *archived*
recording, which has never been in the tree. The tape that actually ships is
synthesized by `examples/sol/make-trek80-tape.sh` and mounts at `7939 bytes, 0
framing errors`, carrying `2390 / 1205 Hz`. Every figure on both lines was
wrong, and no test could tell — the repo's rule is that transcripts are captured
rather than composed, but nothing re-captures them. A check that runs the
manual's own fenced commands and diffs the output against the fence is the
missing piece; it would have caught this and the "CP/M in one command" gap in
the same pass.

**`tools/build-package.sh:7` still describes the contents as `disks/ tapes/`**
in its header comment, which `examples/` replaced. One line, and the script's
own DIR loop is correct — but it is the file that defines the package, so a
stale summary at the top of it is the worst place for one. Not fixed here
because it is a tool, not documentation.

### Dead `if(EXISTS ...)` gates on files that are tracked

`acceptance-minidisk`, `acceptance-dcdd-readonly`, `acceptance-ddt` and the MDS
phase in `hostbridge.cmake` still guard on images that have been in git since
2026-07-18, and `acceptance-hostbridge` opens the same image unguarded
(`hostbridge.cmake:204`) while its siblings guard it. Dead code that reads as
live protection; the inconsistency is the tell.

### Two machine files mount images that exist nowhere

`disks/mits-88dcdd/cpm22/burcon/cpm22-burcon.toml` mounts `cpm56k.dsk` and
`.../lifeboat/cpm22-lifeboat.toml` mounts `LIFEBOAT-CPM22-48K.DSK`. Neither file
is in the tree, and neither is in `tools/fetch-disk-images.sh` — so neither can
ever have worked from a clone. Either add them to the fetch script or say in
each README that the image is a manual download.

---

## Housekeeping

- **Inspecting the `lineprinter` machine creates a file.** `altairsim -x "SHOW
  MACHINE" lineprinter` drops a `printout.txt` in the working directory without
  a byte having been printed — the C700's `file:` sink is opened when the
  machine loads, not when the card first writes. Defensible (a `CONNECT` target
  is opened at connect time) and it is the machine's whole purpose, but a
  read-only inspection command leaving a file behind is a surprise. Low
  priority; noted rather than filed.
- **The roadmap's milestone 4 row is unmarked but the milestone is built.**
  `docs/roadmap.md:243` still lists "ROM board, PHANTOM, banking" with no built
  marker. `PhantomAssert`/`PhantomHonor` and `BankType`/`BankSpec` are all in
  `src/boards/s100-memory.h`. Mark the row.
- **The snoop hooks kept for the Tarbell stay.** `src/core/board.h:248-257`:
  `wantsSnoop()` and its partner exist for a consumer that is on the shelf, and
  they are the only executable description we have of how a card that shadows
  low memory behaves — deleting them means rediscovering the
  combinational-release trap (`DESIGN.md` §4.2.1), which cost a day the first
  time. They come out only if the Tarbell is ruled **out** rather than deferred,
  and `tests/test_phantom.cpp` goes with them. It is deferred. Leave them.
- **The four-board API acceptance hand-trace is half done**
  (`docs/roadmap.md:276-291`). Boards 1 and 2 (88-2SIO, 88-DCDD) are discharged;
  3 (PMMI) and 4 (a DMA/Dazzler-shaped card) are outstanding. The point is to
  find an API defect on paper before writing code.

[#26]: https://github.com/deltecent/altairsim/issues/26
[#43]: https://github.com/deltecent/altairsim/issues/43
[#48]: https://github.com/deltecent/altairsim/issues/48
[#70]: https://github.com/deltecent/altairsim/issues/70
