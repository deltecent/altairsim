# Roadmap

## Step 0 — the paper CLI, reviewed before any code

Before implementation, `docs/cli-transcripts.md` gets a set of **complete, annotated session transcripts**, written longhand as if the simulator already existed:

- Cold-boot CP/M from a floppy image to `A0>`.
- Build a machine from scratch interactively: add boards, hit a port collision, find it with `SHOW BUS IO`, fix it, `CONFIG SAVE`.
- Load a hand-assembled program at 0x0100, breakpoint, single-step, dump registers, patch a byte, continue.
- Load the DBL PROM into a ROM board (`RAW`), and inspect a phantomed-out board the CPU cannot see.
- Pull a `.COM` out of a mounted disk image without booting the machine.
- The same session driven from MCP, showing the JSON in and out.

**Why:** the board API can be validated on paper by hand-tracing. A CLI cannot — it is judged by whether it is pleasant to actually use, and you only learn that by using it. Reading these transcripts is how you discover that a verb is wrong, a default is annoying, or output is unreadable, at the cost of editing Markdown rather than refactoring a command dispatcher.

**These transcripts are reviewed and signed off before implementation starts, and they become the acceptance script.**

---

## Milestone 1 — the walking skeleton

**CLI + MCP + 8080 + bus (including interrupts) + RAM + one 88-2SIO. Nothing else.**

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
| **1 — skeleton** | CLI + MCP, 8080 (incl. interrupts), bus, RAM, 88-2SIO | BASIC answers `PRINT 2+2` via console, socket, and host serial; interrupt-driven echo; two 2SIO boards at once |
| 2 — CPU gate | (none) | TST8080 / 8080PRE / CPUTEST / **8080EXM** pass in CI on all four platforms; the no-`#ifdef` lint is green |
| 3 — disk | 88-DCDD | Cold-boot CP/M 2.2 from `CPM22-8MB-56K-SIM.DSK` to `A0>`; run `M80`/`L80` |
| 4 — memory model | ROM board, PHANTOM, banking | DBL PROM at 0xFF00 overlays RAM; `SHOW BUS MAP` shows it; bank switching works |
| 5 — rest of serial | 88-SIO, 88-ACR, 88-LPC | MITS BASIC from a cassette image; SIO's **inverted** status alongside 2SIO's true-sense |
| 6 — interrupt board & DMA | 88-VI, RTC | VI priority across *several* boards; a DMA card steals cycles and the clock notices |
| 7 — parallel, host bridge | 88-PIO, 88-4PIO, **Host Bridge** (our own design) | `HGET`/`HPUT`/`HDIR` move files to and from the host; sandbox escape attempts are refused |
| 8 — more cores | 8085, Z80 | ZEXALL / ZEXDOC; a Z80 CP/M binary runs |
| *later* | PMMI MM-103, VDM-1, Dazzler | see below |

**Two things about this ordering:**

- **The interrupt mechanism is in milestone 1**, not milestone 6. Milestone 6 adds the 88-VI *board* and multi-board priority, but `pINT`/`IntAck`/floating-bus-RST-7 exist and are proven from the start, because the 2SIO already needs them.
- **The CPU validation gate is milestone 2**, not milestone 1: get the interface in front of you first, then make the core provably correct — but both before a single disk sector is read, so no board is ever built on an unvalidated 8080.

Each milestone has a **written acceptance test that runs headless in CI on all four platforms** via `altairsim -c script.cmd`, so "it works" is never an opinion.

---

## Deferred, but designed for

**PMMI MM-103** — deferred out of the implementation plan: the 88-2SIO already exercises every interface it would (console, host serial, sockets, interrupts). The PMMI adds a modem's *semantics*, not new simulator plumbing.

**But it stays in the design as a paper exercise, and that is not a consolation prize.** It is the hardest board in the catalog to express — read/write asymmetry at one port, a read whose only purpose is a side effect, write-only registers needing shadow copies, one register shared between the baud divisor *and* the dialer *and* the interrupt mask, non-queueing interrupts, and both interrupt models. **Hand-tracing it against the board API costs nothing and is the single best test of whether that API is right.** The recovered register map lives in `docs/boards/pmmi-mm103.md` so the work isn't lost.

**VDM-1** (memory-mapped text video) and **Cromemco Dazzler** (DMA-driven bitmap graphics) are why the `Display`/`Audio` SDL services exist and why **DMA is in the bus model rather than being theoretical**. Hand-trace both against the board API before writing code. Nothing about them should require touching the bus, the monitor, or the MCP server when they're eventually built. That's the test.

---

## The API acceptance test

The design is "done" when it answers, without hand-waving: *how do I add a new S-100 board?*

Validate by hand-tracing four boards against the board API and host services on paper:

1. **88-2SIO** — simple I/O, one `ByteStream` per channel.
2. **88-DCDD** — I/O + rotation timing (`EventQueue`) + mounted media (`BlockDevice`).
3. **PMMI MM-103** — the hard one (see above).
4. **A DMA card (Dazzler-shaped)** — bus mastering, cycle stealing.

For each, check that **all** of the following fall out with no special-casing:

- The bus decodes it, and `SHOW BUS MAP` / `SHOW BUS IO` / `WHO` describe it correctly.
- Every setting is reachable via generic `SET`/`SHOW` through `properties()`, with no monitor changes — and the MCP schema is generated, not written.
- It honors both `Reset::PowerOn` and `Reset::Bus`, and the doc says *concretely* what each does to it.
- It reaches the host only through `ByteStream` / `BlockDevice` / `Display` / `EventQueue` — never a raw socket, file handle, or `chrono::now()`.
- It serializes and restores byte-exactly, and replays deterministically.

**If any of the four needs a special case inside the bus, needs the monitor to learn a new command, or needs to reach around the host-services layer, the API is wrong and needs another pass before any code is written.** That is the whole point of doing this as a document first.
