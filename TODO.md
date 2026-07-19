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

### The SDL window cannot be closed — [#53]

`SdlDisplay` pumps the quit event and sets a flag (`src/host/display_sdl.cpp:81`,
exposed as `quitRequested()`), and nothing ever reads it — `grep -rn
"quitRequested" src/` finds the definition and no caller. Clicking the close box
does nothing. The open question is what closing the window should *mean* (back
to the monitor, quit the process, or detach the display); the issue lays out the
three readings.

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

- **A tape counter and a `WIND` verb** — [#48]. A mounted tape has a position
  (`TapeImage::pos()`, shown by `SHOW MOUNTS`) but the only control is `REWIND`,
  which sets it to zero. Put two programs on one tape and the second is
  reachable only in the session that recorded it. Filed 2026-07-18 with the
  design questions open (what number the counter is; what the operator types)
  and **deliberately not implemented** — waiting on comments.

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
[#53]: https://github.com/deltecent/altairsim/issues/53
