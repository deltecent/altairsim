# Roadmap

## Step 0 — the paper CLI, reviewed before any code

Before implementation, `docs/attic/cli-transcripts.md` gets a set of **complete, annotated session transcripts**, written longhand as if the simulator already existed:

- Cold-boot CP/M from a floppy image to `A0>`.
- Build a machine from scratch interactively: add boards, hit a port collision, find it with `SHOW BUS IO`, fix it, `CONFIG SAVE`.
- Load a hand-assembled program at 0x0100, breakpoint, single-step, dump registers, patch a byte, continue.
- Load the DBL PROM into a ROM board (`RAW`), and inspect a phantomed-out board the CPU cannot see.
- ~~Pull a `.COM` out of a mounted disk image without booting the machine.~~ **Cut.** Host-side CP/M filesystem access is deferred (`DESIGN.md` §12.2): it needs the *controller's* sector layout **and** the *image's* DPB and skew, and no generic command can infer that pair. The `DISK` verbs are gone from the monitor.
- The same session driven from MCP, showing the JSON in and out.

**Why:** the board API can be validated on paper by hand-tracing. A CLI cannot — it is judged by whether it is pleasant to actually use, and you only learn that by using it. Reading these transcripts is how you discover that a verb is wrong, a default is annoying, or output is unreadable, at the cost of editing Markdown rather than refactoring a command dispatcher.

**These transcripts are reviewed and signed off before implementation starts, and they become the acceptance script.**

---

## Milestone 1a — the smallest thing that is really a simulator

**CLI + bus + the `memory` board + MCP. No CPU.**

> ### ✅ Built, 2026-07-11. It runs.
>
> `cmake -S . -B build && cmake --build build` → `altairsim` (three built-in machines, no config file needed), `altairsim -s script.cmd`, `altairsim --mcp`. 313 unit checks, all passing, no warnings.
>
> **MCP came in with it**, not after. That was Patrick's call and it was the right one: the CLI and the MCP server sit on the *same* `Machine` and the *same* `Board::properties()`, so building only one of them would have let the reflection layer be shaped by a single consumer — and the whole claim of §11 is that it has two. `board_get`'s schema is *generated* from `properties()`; nobody hand-wrote an enum list, and `CONFIG SAVE` round-trips `phantom`/`fill` through a serializer that was never written for them.
>
> **The acceptance tests earned their keep on day one.** The default configuration — a ROM shadowing RAM, `honors_phantom=all`, `phantom=all` — did not work, and *the bug was in this design document*, not just the code. `MemoryBoard::decodes()` read `if (c.phantom && honorsPhantom_) return false;`, so a ROM card pulled PHANTOM\*, **honored its own assertion, and switched itself off**; nobody drove FF00 and the PROM read back as `FF`. The fix is `&& !assertsPhantom(c)` — a card does not shut itself off with a signal it is itself driving. It stays board-local, the bus still arbitrates nothing, and §4.2 now carries the scar deliberately.

*Added 2026-07-11, at Patrick's direction: milestone 1 as originally written (below) was still too much to build before anything could be used. Start smaller.*

The insight that makes this a real milestone rather than a stub: **with no CPU in the machine, the monitor is the bus master.** `DEPOSIT` and `DUMP` originate genuine bus cycles against a genuine board — which is exactly what the Altair front panel did. So milestone 1a is not a mock; it exercises the bus, the decode tables, `Board`, `properties()`, both resets, contention detection, the floating bus, and most of the memory CLI, with **one board and no processor.**

The board is `memory` (`docs/boards/s100-memory.md`): a card holding a list of **regions**, each `ram` (writes stored) or `rom` (writes **not decoded**), at 256-byte page granularity. Everything a memory card can be — a partly-populated RAM board, a PROM card of four sockets with two of them empty, a combo card with both — is that one list. Plus **banking**, in the five real flavors that actually shipped.

**Two things make this milestone worth doing early, and neither needs a CPU.**

**Banking.** Five real cards bank in five different ways — three ports, two encodings, one card with seven banks instead of eight, and a Vector card that decodes `0x41`/`0x42` because OASIS writes them. If the board API can express all five with no bus special case and no monitor change, the central claim of the whole design is proven **before a single CPU instruction is executed.**

**ROM regions and PHANTOM\*.** A `rom` region doesn't decode writes, and a card can pull PHANTOM\* to shadow another. Those two facts are what §4.2 stakes its whole argument on — *the bus arbitrates nothing; boards switch themselves off.* They are testable with two boards, a hex file, and no processor. And they are worth testing **before** a CPU exists, because the three phantom straps differ *silently*: get one wrong and the symptom is a guest that misbehaves ten thousand instructions later.

### Acceptance

1. A 48K `ram` region; `DUMP C000-C0FF` returns **all `FF`**. The hole is real and reads float.
2. `LOAD`/`DUMP`/`SAVE` round-trips a file byte-for-byte (binary and Intel HEX).
3. `RESET` preserves memory and clears `bank` to 0; **only `POWER` loses it** — and `POWER` re-reads ROM images from their files.
4. `CONFIG SAVE` → `CONFIG LOAD` reproduces the regions, page map, and bank config exactly.
5. **A `rom` region at FF00 from a 256-byte file decodes FF00–FFFF and nothing else.** `DEPOSIT FF00 41` (a bus write) does not change it, and the monitor **says so** instead of silently succeeding. `LOAD other.hex ROM` **does** change it — the operator has a PROM burner; the guest does not.
6. **An empty socket between two ROM regions reads `FF`**, exactly like unpopulated RAM. One mechanism, not two.
7. **All three PHANTOM\* straps, because the difference is silent.** `phantom=all`: a write over the shadowed RAM **vanishes**. `phantom=read`: the same write **lands in the RAM beneath** while reads still come from ROM (shadow-RAM — read it back through the bus and you get the ROM byte; read it `RAW` and you get `41`). `honors_phantom=none` on the card beneath: **both drive → contention reported.** And a phantom shadow is **not** reported as contention — only the real collision is.
8. Two `memory` cards at overlapping bases → **contention reported**, naming both.
9. **Banking, all five types.** Binary vs one-hot select; Cromemco's 7-bank `& 0x7F` mask; the Vector/OASIS `0x41`/`0x42` quirk; an undecodable select leaves the bank unchanged and logs it.
10. **Two banked cards both claiming port 0x40** (three of the five real cards do) → **I/O contention reported**, naming both.
11. `fill=random` with a fixed `seed` is **byte-identical across runs**, and the seed survives `SNAPSHOT`/`RESTORE`.

**Then use it.** The CLI findings in `docs/attic/cli-transcripts.md` (F1–F12) get settled against something real before a CPU exists to distract from them.

---

## The 8080 — the CPU card, single-stepping, and breakpoints

**Built and VALIDATED, 2026-07-11.** *(Out of order: milestone 2 is the CPU
**validation gate**, and the chip itself was always going to be needed for 1b's
2SIO and 3's disk. The gate was closed the same day — see "the gate is passed",
below.)*

`src/isa/` (a stateless disassembler), `src/cpu/` (the core), `src/boards/mits-88cpu.cpp`
(the card), `src/core/debug.cpp` (the debugger). `docs/boards/mits-88cpu.md`.
449 unit checks, all passing, no warnings.

**The three-layer split earned its keep immediately.** `DISASM` runs against an
instruction set, not a CPU — so it worked on the DBL PROM in milestone 1a, with an
empty backplane, and the 8080 decode tables were exercised long before anything
executed them.

**The debugger is a bus observer, not a CPU feature** (DESIGN.md §3.0.3).
`BREAK MEM`/`BREAK IO` watch the cycle stream every board already sees, so they
cost the cores nothing and will work on a Z80 the day it lands. Only `BREAK <addr>`
is CPU-flavoured, and it is one comparison against a register the reflection layer
already exposes.

**Two bugs this found in the design document, not just in the code:**

- **§10.2 put `DISASM` in the "through the bus" group.** That is wrong, and quietly
  so: a disassembler built on real bus reads works perfectly against RAM and then
  **eats the console's input** the first time someone disassembles a page with a
  UART mapped into it. There is now a third access mode — `peek`, which runs the
  full decode but no cycle — and §10.2 carries the correction.
- **`clock_hz` existed in two places**: on `[machine]` and on the CPU card. While
  there was no CPU that was harmless; the moment there was one it became two
  places to say one thing, and the day they disagreed the machine would run at
  whichever was written last. The machine-level key is **gone**, and a config that
  still uses it is **refused with a message**, not silently ignored.

### It runs the boot PROM

    altairsim> BREAK IO R 08
    altairsim> RUN FF00
    breakpoint 1 (io r   08) -- stopped at 2C21
    1433 instructions, 9344 T-states.

DBL copies itself out of the PROM into RAM at 2C00, jumps there, and starts polling
a disk controller that is not in the backplane — reading a floating `FF` every time,
because nobody is driving those ports. **That is not a bug; it is what an Altair
with no disk controller in it does**, and you can watch it happen. The 88-DCDD is
milestone 3.

The self-relocation loop is **1413 instructions and 9192 T-states**, and the
datasheet predicts exactly that: three setup instructions (27) plus 235 iterations
of a six-instruction, 39-T-state loop (9165). The T-states are not approximately
right — they are right, which matters because they will drive the baud rate and the
disk rotation.

### The gate is PASSED (2026-07-11)

Patrick supplied the source: **altairclone.com/downloads/cpu_tests/**. The four
suites are committed under `tests/cpu/` *with their period assembler source*, the
harness is `tests/cputest.cpp`, and **all four pass** — including all 25 CRC
groups of 8080EXM, which checks every instruction against every interesting
operand and CRCs the result *including all five flags* against values captured
from real silicon.

That matters more than the three suites before it, because the core was
previously validated only by tests its own author wrote — precisely the tests
that share its blind spots. The three flag rules called out in
`docs/boards/mits-88cpu.md` as most likely to be wrong (`ANA`'s half-carry,
subtraction through the one adder, even parity) are exactly what 8080EXM's
`aluop` groups check, and `<daa,cma,stc,cmc>` is exactly what the Python
prototype admitted it had never tested. All green, no changes to the core.

**The T-states got corroborated for free.** SuperSoft's docs say CPUTEST's timing
section takes ~2 minutes on a real 2 MHz 8080; our run of it costs 255,660,114
T-states = **127.8 seconds** at 2 MHz. No CRC constrains that number. It is right
because the cycle counts are right.

**How the CP/M programs run with no CP/M and no console card:** a BDOS stub at
`F000` written in *real 8080 machine code*, reached through the real `JMP` at
`0005`, doing its output with a real `OUT` to a real board in the backplane. It
would have been less code to trap `PC == 0005` in C++ — and that was rejected,
because it would mean the `CALL`, the `RET`, the stack it unwinds, the `LDAX D`
and the `OUT` were all being faked by the harness, in the one program whose whole
job is to decide whether we implement them correctly.

---

## The 88-2SIO — the console, and the first machine you can talk to

**Built, 2026-07-11.** `src/host/` (the host services layer), `src/boards/mits-2sio.cpp`, `src/core/clock.h`, `tests/test_sio2.cpp`. `docs/boards/mits-2sio.md`.

```
$ altairsim altmon
startup> RUN F800

ALTMON 1.3
*DUMP F800 F80F
F800 3E 03 D3 10 D3 12 3E 11 D3 10 D3 12 31 00 C0 CD  >.....>.....1...
```

**That is a real 1K monitor PROM, written for real hardware, driving a real 6850 over the real bus** — and the bytes it dumped are its own first sixteen, which are `MVI A,3 / OUT 10 / OUT 12 / MVI A,11 / OUT 10 / OUT 12`: the ROM reading its own 2SIO initialization back to us *through the card it is initializing*.

What landed, in dependency order:

- **`Clock`** (`src/core/clock.h`) — emulated time in T-states, advanced only by the run loop, by exactly what the CPU reported. The crystal is on the CPU card, so the card publishes its `clock_hz` here.
- **`ByteStream`** (`src/host/stream.h`) — the generic serial endpoint. `NullStream`, `LoopbackStream`, `ScriptedStream`, `Console`.
- **`FilterStream`** (`src/host/filter.cpp`) — the transform chain, put **on the line rather than on the console** so `SET sio0:a UPPER=ON` would work identically on a socket. **That was reversed later, and the reversal is the rule now:** a transform is a *console* property (`SET CONSOLE UPPER=ON`), because a card's line has to stay **8-bit clean** — a filter sitting on it corrupts XMODEM, silently. What a card has instead is line *coding* (`baud`, `data_bits`), which is a frame, never a mask. `SET sio0:a UPPER=ON` is an error today, and says so.
- **`Console`** (`src/host/console.cpp`) — raw mode, and the **ATTN** key, intercepted by the host before the guest is ever offered the byte.
- **The 88-2SIO** — two independent 6850s that share nothing.
- **The monitor**: `CONNECT`, `DISCONNECT`, `CONSOLE [addr]`, `SET <id>:<unit> k=v`, `SHOW CONSOLE`, and `[board.unit.a]` in a config file, round-tripped by `CONFIG SAVE`.

### Three things this got wrong first, and what they taught

**1. A card's receiver fills on its own clock, and the interface has to let it.** The interrupt-driven echo test failed because the ACIA only ingested a character when the guest *read a register* — and **an interrupt-driven driver never reads the status port; that is the entire point of it.** So RDRF never set, IRQ never rose, nothing ever happened. Every polled test passed throughout.

The first fix made `assertsInt()` non-`const` and advanced the receiver inside it, on the strength of the bus polling every board once per instruction. **That was half a fix, and it was reversed on 2026-07-12** — the free-running work was real, but a *query* was the wrong place for it, and the poll was the wrong shape for a bus (§4.4.1). The work moved to a `Clock` deadline the card sets itself, plus `pump()`; `assertsInt()` went back to being `const` and pure; and the card now **pulls pin 73** rather than being asked about it. See lesson 3.

**2. A `ByteStream` is not a serial line.** The first ACIA synthesized OVRN by pulling a byte every character-time whether or not the guest had read the last one — and **immediately lost data**, because while ALTMON was echoing `DUMP ` it was not reading the receiver, so the address you typed went on the floor. A stream is *buffered and flow-controlled*; inventing an overrun from it manufactures data loss the host does not have. (`docs/boards/mits-2sio.md`.)

**3. The `EventQueue` was "never needed" — and the argument was circular.** DESIGN §7.5 specified callbacks; the first pass deleted them, reasoning that a board is already **polled** for everything the bus can see (`decodes()` every cycle, `assertsInt()` every instruction), so it never needs *waking* — it only needs to answer *"what time is it?"* when asked.

**The board did not need waking because we were polling it sixty million times a second.** The poll was not evidence the queue was unnecessary; **the poll *was* the queue**, run at enormous cost under another name. Take it away — and it had to go, because no backplane interrogates a card for its interrupt status — and the hole is obvious: a 6850 whose transmit register drains **while the guest sits in a `HLT` waiting for exactly that interrupt** has no way to be present for its own deadline. If the only thing that would ask is the CPU that is halted waiting for it, the machine is dead.

**Restored 2026-07-12**, with that board as the proof it was needed, and `tests/test_sio2.cpp` runs the machine that fails without it. The old argument's *correct* half survives: TDRE really is a deadline, not an event, and on a quiet line the card schedules **nothing at all**.

**And the answer to "event queue, or periodic timer?" is both** — they answer different questions. A deadline is something emulated time can predict (a character finishing transmission). A keystroke is not in emulated time at all, so it arrives through `pump()`. The second one already existed.

### Still open in 1b

**Nearly all of this list has since closed.** `socket:` and `serial:` endpoints are built (`src/host/endpoint.cpp`, with `socket-hw` and `serial-hw` exercising them against a real port and a real cable); MCP grew `send`/`recv`; 4K BASIC boots off a cassette as `acceptance-basic4k`; and idle detection shipped as the `idle` property on the CPU card (§8) — a machine sitting at a prompt stands down instead of burning a core, and it is on by default.

**What remains is the `pace`, `ansi`, `tabs` and `log` console transforms.** The four that did land — `upper`, `strip7in`/`strip7out`, `crlf`, `bsdel` — settled the question the list was really asking, which is *where* a transform belongs: on the console, never on the line, because a card's line has to stay 8-bit clean or XMODEM corrupts silently.

---

## Milestone 1b — the walking skeleton

**MCP + 8080 + interrupts + one 88-2SIO, on top of 1a.**

Deliberately lopsided: nearly the entire interface, and the smallest machine that can prove it.

- **8080 only.** 8085 and Z80 are later cores behind the same `CpuCore` interface. Designing for three is worth it now; implementing three is not.
- **RAM only, no ROM board.** Programs get into memory with `LOAD file AT addr` (binary and Intel HEX). One fewer board, *and* it defers PHANTOM entirely — the overlay semantics are the subtlest part of the memory model and none of it needs to be right to prove the CLI is right.
- **The 88-2SIO, complete** — deliberately the only peripheral, because a fully-modeled 2SIO exercises **every** interface in the design at once. It is the proof vehicle, not a placeholder.

| What the 2SIO proves | How |
|---|---|
| **Multi-unit boards** | Two independent 6850 ACIAs. Forces `sio2a:a` / `sio2a:b` unit addressing and per-unit properties to be real from day one, instead of retrofitted the first time a board has more than one of something. |
| **Multiple instances** | Two 88-2SIO boards at different base ports, simultaneously. |
| **`Console`** | Channel A → console, with the transform chain (`UPPER` matters at once; BASIC wants caps). |
| **Sockets** | Channel B → `socket:2323`. Listening, accept, client disconnect and reconnect. |
| **Host serial** | Channel B → `serial:/dev/cu.usbserial-X`. Most likely to be subtly broken on Windows, so exercise it in milestone 1, not milestone 8. |
| **Interrupts, end to end** | The 6850's own enables (RIE on receive, TIE in the transmit-control field), the board's `interrupt` jumper property, `pINT`, the CPU's `IntAck` cycle, and the floating-bus `RST 7`. |

**The full interrupt path lands in milestone 1** — specifically the **un-vectored `pINT` path, with no VI board in the machine.** The 2SIO is jumpered `interrupt = int`, pulls pin 73, and the `IntAck` cycle finds nobody driving the data bus, so the CPU reads a floating `0xFF` and executes `RST 7`. That is the real Altair behavior, and getting it right on day one makes the vectored path later purely *additive*: the 88-VI board arrives in milestone 6 and simply claims the `IntAck` cycle that already exists.

This is the single biggest gap in the Python prototype — it has **no** interrupt mechanism at all (`EI`/`DI` set a flag nothing reads), which is what happens when a simulator is built polled and interrupts are bolted on later. Not deferring it is the point.

### Acceptance

1. 4K MITS BASIC, `LOAD`ed into RAM, answering `PRINT 2+2` **on the console** — and the same, driven headless **over MCP** with `send`/`expect`.
2. The same session over a **TCP socket** instead of the console, with a terminal emulator attached.
3. The same over a **host serial port**, on macOS/Linux and on Windows.
4. An **interrupt-driven** console echo program (not polled) running correctly **with no VI board in the machine** — proving RIE/TIE, `pINT`, the `IntAck` cycle, and the floating-bus `RST 7`.
5. **Two 2SIO boards at once**, four channels live, each independently configured, on different base ports.

That one milestone exercises the CPU core (including interrupts), the bus decode tables, memory, a multi-unit board, all three `ByteStream` back-ends, the console transform chain, `LOAD`/`DUMP`/`DISASM`/`GO`/`STEP`/breakpoints, generic `SET`/`SHOW`, `SHOW BUS MAP`/`IO`/`IRQ`, `CONFIG SAVE`, both reset kinds, snapshots, and the MCP loop. **The entire interface, end to end, on one peripheral.**

**Then you use it for a while and say what's wrong before board three exists.** Every board after that is a comparatively mechanical exercise against an interface you have already lived with — which is the whole point of sequencing it this way.

---

## Subsequent milestones

Each board lands with its `.md`, its properties, its reset behavior, its tests, and a demonstrated period-software workload.

| Milestone | Adds | Proven by |
|---|---|---|
| **1a — memory bench** | CLI, bus, `memory` board (ram + rom regions, banking, PHANTOM\*). **No CPU.** | Monitor acts as bus master: unpopulated pages read `FF`; a ROM region doesn't decode writes; all three phantom straps behave; contention detected; `RESET` keeps RAM, only `POWER` loses it |
| **1b — skeleton** | MCP, 8080 (incl. interrupts), 88-2SIO, **88-SIO** | BASIC answers `PRINT 2+2` via console, socket, and host serial; interrupt-driven echo; two 2SIO boards at once |
| 2 — CPU gate | (none) | **DONE 2026-07-11** — TST8080 / 8080PRE / CPUTEST / **8080EXM** all pass (`ctest`, plus `ctest -L slow` for 8080EXM). **They run in CI too now**, on all three legs (Linux, a universal macOS binary, Windows/MSVC), on their own workflow rather than every push. The no-`#ifdef` lint is green. |
| 3 — disk | ~~88-DCDD~~ (**built** — `docs/boards/mits-dcdd.md`), ~~**88-MDS**~~ (**built 2026-07-13** — `docs/boards/mits-88mds.md`) | Cold-boot CP/M 2.2 from `CPM22-8MB-56K.DSK` to `A0>`; run `M80`/`L80`. **The 88-MDS was not on this roadmap** — it was a *bug*, in the same family as the front panel one below. The 5.25″ minidisk was a row in the 88-DCDD's format table plus `if (d->fmt.sectors != 16)`, i.e. an 8″ controller inferring which controller it was from the disk in the drive, and it turned the platter at 360 RPM instead of 300 and clocked bytes at twice the real rate. **It never failed, because no minidisk image was ever in the tree.** The two cards now share a register model (`HardSectorFdc`) because MITS built that compatibility on purpose, and nothing else. `acceptance-minidisk` boots CP/M 2.2b off a real 5.25″ disk. |
| 4 — memory model | ~~ROM board, PHANTOM, banking~~ (**built** — `docs/boards/s100-memory.md`) | DBL PROM at 0xFF00 overlays RAM; `SHOW BUS MAP` shows it; bank switching works. **Done:** all three live on the `memory` board — `PhantomAssert`/`PhantomHonor` and `BankType`/`BankSpec` in `src/boards/s100-memory.h`, covered by `tests/test_roms.cpp`, `tests/test_phantom.cpp` and `tests/test_memory.cpp` (five real banking schemes, no two alike) |
| 5 — rest of serial | ~~88-SIO~~ (**built 2026-07-12, in 1b**), ~~88-ACR~~ (**built 2026-07-12** — `docs/boards/mits-88acr.md`; it *is* an 88-SIO B, plus an FSK modem the guest cannot observe), 88-LPC | MITS BASIC from a cassette image; SIO's **inverted** status alongside 2SIO's true-sense |
| 5½ — the front panel | **`fp`** (**built 2026-07-12** — `docs/boards/mits-frontpanel.md`) | `IN 0FFH` reads the SENSE switches, and **DBL reads a switch instead of a floating wire for the first time.** Not a planned milestone: it was a *bug*. `[machine] sense` parsed into a byte nothing put on the bus (see the F6 note in `docs/attic/cli-transcripts.md`), and the fix was to put the switches on the card that carries them |
| 5¾ — the parallel printer | ~~**88-C700**~~ (**built** — `docs/boards/mits-88c700.md`; the `c700` board) | Printed bytes reach an endpoint: `file:` captures them, `null` discards them. Not a planned milestone — it is recorded here because it was built and the table did not say so, which is the same gap the 88-MDS and the front panel rows above were filled to close. **The polled path only.** The real card also carries a single-level interrupt (per-character or per-CR/LF, SW2 #4) that is *not* modelled: the status byte reports the INTERRUPT ENABLE bit software wrote (`intEnabled_`), but no request is raised and no wire is pulled — a deliberately deferred addition ([#26](https://github.com/deltecent/altairsim/issues/26)). It is **not** the 88-LPC, which is a separate and genuinely unbuilt board still owed by milestone 5 |
| 6 — interrupt board & DMA | ~~88-VI, RTC~~ (**built 2026-07-13** — `docs/boards/mits-88virtc.md`; one card, `virtc`), ~~DMA~~ (**mechanism built 2026-07-16**) | VI priority across *several* boards; a DMA card steals cycles and the clock notices. **The VI half is done:** MITS Programming System II runs with interrupts enabled, its console vectored through VI7 → `RST 7`, and Ctrl-C breaks a runaway program back to the monitor (`acceptance-ps2-int`). **The DMA half is now done too — the *mechanism*.** `Board::requestsBus()`/`busMaster()` and the run loop's `serviceDma()` grant `pHLDA` at an instruction boundary and charge the stolen T-states to the clock (DESIGN §4.5). It is proved by a real memory-to-memory master in `tests/test_dma.cpp` — genuine RAM→RAM cycles, the 128-T-state theft read back out of `clock.now()`, cycle-steal interleaving with a running CPU, and a `BREAK MEM W` that catches the DMA's own write. No fictional shipping DMA board was invented (§0.1); the concrete one is the **Dazzler**, which is blocked on the SDL `Display` service (§7.3), not on the bus. It will be additive, the way the 88-VI was additive onto the `IntAck` cycle. |
| 7 — parallel, host bridge | 88-PIO, 88-4PIO, ~~**Host Bridge**~~ (**built 2026-07-13** — `docs/boards/hostbridge.md`; our own card, `hostbridge`, at 0xB0) | `R`/`W`/`HDIR` move files to and from the host; sandbox escape attempts are refused. **The Host Bridge is done.** It is the project's first genuinely new piece of hardware, and the board API needed nothing to take it. Its guest utilities are 8080 assembly **assembled inside the machine** by `ASM.COM`/`LOAD.COM` — `acceptance-hostbridge-build` `PIP`s the sources in through the console and builds them, so the `.ASM` and the card cannot drift, while `acceptance-hostbridge` runs the committed `.HEX` on the tracked floppy every time. It then round-trips 256 bytes containing every value from `00` to `FF` and compares them **on the host's disk**, and does the whole thing again with the *same binary* on an 88-MDS minidisk behind a different controller — which is how the "every disk call is a BDOS call" claim gets proved instead of asserted. The 88-PIO and 88-4PIO remain, and are still blocked on documentation (§17). |
| 8 — more cores | 8085, ~~Z80~~ (**built 2026-07-15** — `docs/boards/mits-z80cpu.md`; a `z80` CPU card and a minimal `z80` machine) | ZEXALL / ZEXDOC; a Z80 CP/M binary runs. **The Z80 half is done and validated:** ZEXDOC and ZEXALL both pass (`ctest` targets `cpu-zexdoc` / `cpu-zexall`, `-L slow`), and they passed before any board was built on the core — the same gate the 8080 went through. The **8085 remains.** |
| *later* | PMMI MM-103, ~~VDM-1~~ (**built 2026-07-18** — `docs/boards/proctech-vdm1.md`, and the **Sol-20** with it), ~~Dazzler~~ (**built 2026-07-23** — `docs/boards/cromemco-dazzler.md`; `dazzler`, KSCOPE in `examples/dazzler/`) | see below |

**Two things about this ordering:**

- **The interrupt mechanism is in milestone 1**, not milestone 6. Milestone 6 adds the 88-VI *board* and multi-board priority, but `pINT`/`IntAck`/floating-bus-RST-7 exist and are proven from the start, because the 2SIO already needs them. **This paid off exactly as predicted** (2026-07-13): the 88-VI needed *no new bus concept* — it claims the `IntAck` cycle that had been there since milestone 1, and the only thing the bus had to grow was the eight VI wires themselves. The card was additive, as promised.
- **The CPU validation gate is milestone 2**, not milestone 1: get the interface in front of you first, then make the core provably correct — but both before a single disk sector is read, so no board is ever built on an unvalidated 8080.

Each milestone has a **written acceptance test that runs headless in CI on all three platforms** — Linux, macOS (one universal `x86_64`+`arm64` binary) and Windows — via `altairsim -s script.cmd`, so "it works" is never an opinion.

---

## Deferred, but designed for

**PMMI MM-103** — deferred out of the implementation plan: the 88-2SIO already exercises every interface it would (console, host serial, sockets, interrupts). The PMMI adds a modem's *semantics*, not new simulator plumbing.

**But it stays in the design as a paper exercise, and that is not a consolation prize.** It is the hardest board in the catalog to express — read/write asymmetry at one port, a read whose only purpose is a side effect, write-only registers needing shadow copies, one register shared between the baud divisor *and* the dialer *and* the interrupt mask, non-queueing interrupts, and both interrupt models. **Hand-tracing it against the board API costs nothing and is the single best test of whether that API is right.** The recovered register map lives in `docs/boards/pmmi-mm103.md` so the work isn't lost.

**VDM-1** (memory-mapped text video) and **Cromemco Dazzler** (DMA-driven bitmap graphics) are why the `Display`/`Audio` SDL services exist and why **DMA is in the bus model rather than being theoretical**. Hand-trace both against the board API before writing code. Nothing about them should require touching the bus, the monitor, or the MCP server when they're eventually built. That's the test.

**The VDM-1 has since been built (2026-07-18), and it passed that test** — `docs/boards/proctech-vdm1.md`, the `vdm1` machine, and an SDL3-backed `Display` seam that is an *optional* dependency (no SDL, no video boards, everything else still builds). The bus, the monitor and the MCP server were untouched. The **Sol-20** came with it (`docs/boards/proctech-sol.md`, the `sol20` machine): the integrated Sol-PC as one composite board, reusing that same VDM-1. **The Dazzler has since been built too (2026-07-23)** — `docs/boards/cromemco-dazzler.md`, the `dazzler` machine, and KSCOPE in `examples/dazzler/` — reusing that same `Display` seam and, again, touching neither the bus nor the monitor nor the MCP server. It renders from its framebuffer in main RAM the way the VDM-1 renders from its screen RAM; it deliberately does **not** bus-master (a correct picture only needs to read the current frame, which `Bus::peek()` does headlessly and deterministically), so the DMA mechanism is still built and proved but has yet to carry a shipping board.

---

## The API acceptance test

The design is "done" when it answers, without hand-waving: *how do I add a new S-100 board?*

Validate by hand-tracing four boards against the board API and host services on paper:

1. **88-2SIO** — simple I/O, one `ByteStream` per channel.
2. **88-DCDD** — I/O + rotation timing (`EventQueue`) + mounted media (`DiskImage`). Specifically: can the board declare a **hard-sector** 137-byte-slot format, and could a *soft-sector* controller with a single-density track 0 declare its own, without either one special-casing the service?
3. **PMMI MM-103** — the hard one (see above).
4. **A DMA card** — bus mastering, cycle stealing. (The Dazzler was the intended example, but it shipped rendering from RAM without mastering the bus — so the DMA mechanism, though built and proved in `tests/test_dma.cpp`, still awaits its first shipping board.)

For each, check that **all** of the following fall out with no special-casing:

- The bus decodes it, and `SHOW BUS MAP` / `SHOW BUS IO` / `WHO` describe it correctly.
- Every setting is reachable via generic `SET`/`SHOW` through `properties()`, with no monitor changes — and the MCP schema is generated, not written.
- It honors both `Reset::PowerOn` and `Reset::Bus`, and the doc says *concretely* what each does to it.
- It reaches the host only through `ByteStream` / `DiskImage` / `Display` / `EventQueue` — never a raw socket, file handle, or `chrono::now()`.
- It serializes and restores byte-exactly, and replays deterministically.

**If any of the four needs a special case inside the bus, needs the monitor to learn a new command, or needs to reach around the host-services layer, the API is wrong and needs another pass before any code is written.** That is the whole point of doing this as a document first.
