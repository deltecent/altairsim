# Roadmap

## Step 0 — the paper CLI, reviewed before any code

Before implementation, `docs/cli-transcripts.md` gets a set of **complete, annotated session transcripts**, written longhand as if the simulator already existed:

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
> `cmake -S . -B build && cmake --build build` → `altairsim`, `altairsim -c script.cmd`, `altairsim --mcp`. 77 unit checks, all passing, no warnings.
>
> **MCP came in with it**, not after. That was Patrick's call and it was the right one: the CLI and the MCP server sit on the *same* `Machine` and the *same* `Board::properties()`, so building only one of them would have let the reflection layer be shaped by a single consumer — and the whole claim of §11 is that it has two. `board_get`'s schema is *generated* from `properties()`; nobody hand-wrote an enum list, and `CONFIG SAVE` round-trips `phantom`/`fill` through a serializer that was never written for them.
>
> **The acceptance tests earned their keep on day one.** The default configuration — a ROM shadowing RAM, `honors_phantom=true`, `phantom=all` — did not work, and *the bug was in this design document*, not just the code. `MemoryBoard::decodes()` read `if (c.phantom && honorsPhantom_) return false;`, so a ROM card pulled PHANTOM\*, **honored its own assertion, and switched itself off**; nobody drove FF00 and the PROM read back as `FF`. The fix is `&& !assertsPhantom(c)` — a card does not shut itself off with a signal it is itself driving. It stays board-local, the bus still arbitrates nothing, and §4.2 now carries the scar deliberately.

*Added 2026-07-11, at Patrick's direction: milestone 1 as originally written (below) was still too much to build before anything could be used. Start smaller.*

The insight that makes this a real milestone rather than a stub: **with no CPU in the machine, the monitor is the bus master.** `DEPOSIT` and `DUMP` originate genuine bus cycles against a genuine board — which is exactly what the Altair front panel did. So milestone 1a is not a mock; it exercises the bus, the decode tables, `Board`, `properties()`, both resets, contention detection, the floating bus, and most of the memory CLI, with **one board and no processor.**

The board is `memory` (`docs/boards/memory.md`): a card holding a list of **regions**, each `ram` (writes stored) or `rom` (writes **not decoded**), at 256-byte page granularity. Everything a memory card can be — a partly-populated RAM board, a PROM card of four sockets with two of them empty, a combo card with both — is that one list. Plus **banking**, in the five real flavors that actually shipped.

**Two things make this milestone worth doing early, and neither needs a CPU.**

**Banking.** Five real cards bank in five different ways — three ports, two encodings, one card with seven banks instead of eight, and a Vector card that decodes `0x41`/`0x42` because OASIS writes them. If the board API can express all five with no bus special case and no monitor change, the central claim of the whole design is proven **before a single CPU instruction is executed.**

**ROM regions and PHANTOM\*.** A `rom` region doesn't decode writes, and a card can pull PHANTOM\* to shadow another. Those two facts are what §4.2 stakes its whole argument on — *the bus arbitrates nothing; boards switch themselves off.* They are testable with two boards, a hex file, and no processor. And they are worth testing **before** a CPU exists, because the three phantom straps differ *silently*: get one wrong and the symptom is a guest that misbehaves ten thousand instructions later.

### Acceptance

1. A 48K `ram` region; `DUMP C000-C0FF` returns **all `FF`**. The hole is real and reads float.
2. `LOAD`/`DUMP`/`SAVE` round-trips a file byte-for-byte (binary and Intel HEX).
3. `RESET` preserves memory and clears `bank` to 0; **only `POWER` loses it** — and `POWER` re-reads ROM images from their files.
4. `CONFIG SAVE` → `CONFIG LOAD` reproduces the regions, page map, and bank config exactly.
5. **A `rom` region at FF00 from a 256-byte file decodes FF00–FFFF and nothing else.** `DEPOSIT FF00 41` (a bus write) does not change it, and the monitor **says so** instead of silently succeeding. `LOAD other.hex RAW mem0` **does** change it — the operator has a PROM burner; the guest does not.
6. **An empty socket between two ROM regions reads `FF`**, exactly like unpopulated RAM. One mechanism, not two.
7. **All three PHANTOM\* straps, because the difference is silent.** `phantom=all`: a write over the shadowed RAM **vanishes**. `phantom=read`: the same write **lands in the RAM beneath** while reads still come from ROM (shadow-RAM — read it back through the bus and you get the ROM byte; read it `RAW` and you get `41`). `honors_phantom=false` on the card beneath: **both drive → contention reported.** And a phantom shadow is **not** reported as contention — only the real collision is.
8. Two `memory` cards at overlapping bases → **contention reported**, naming both.
9. **Banking, all five types.** Binary vs one-hot select; Cromemco's 7-bank `& 0x7F` mask; the Vector/OASIS `0x41`/`0x42` quirk; an undecodable select leaves the bank unchanged and logs it.
10. **Two banked cards both claiming port 0x40** (three of the five real cards do) → **I/O contention reported**, naming both.
11. `fill=random` with a fixed `seed` is **byte-identical across runs**, and the seed survives `SNAPSHOT`/`RESTORE`.

**Then use it.** The CLI findings in `docs/cli-transcripts.md` (F1–F12) get settled against something real before a CPU exists to distract from them.

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
| **1b — skeleton** | MCP, 8080 (incl. interrupts), 88-2SIO | BASIC answers `PRINT 2+2` via console, socket, and host serial; interrupt-driven echo; two 2SIO boards at once |
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
2. **88-DCDD** — I/O + rotation timing (`EventQueue`) + mounted media (`DiskImage`). Specifically: can the board declare a **hard-sector** 137-byte-slot format, and could a *soft-sector* controller with a single-density track 0 declare its own, without either one special-casing the service?
3. **PMMI MM-103** — the hard one (see above).
4. **A DMA card (Dazzler-shaped)** — bus mastering, cycle stealing.

For each, check that **all** of the following fall out with no special-casing:

- The bus decodes it, and `SHOW BUS MAP` / `SHOW BUS IO` / `WHO` describe it correctly.
- Every setting is reachable via generic `SET`/`SHOW` through `properties()`, with no monitor changes — and the MCP schema is generated, not written.
- It honors both `Reset::PowerOn` and `Reset::Bus`, and the doc says *concretely* what each does to it.
- It reaches the host only through `ByteStream` / `DiskImage` / `Display` / `EventQueue` — never a raw socket, file handle, or `chrono::now()`.
- It serializes and restores byte-exactly, and replays deterministically.

**If any of the four needs a special case inside the bus, needs the monitor to learn a new command, or needs to reach around the host-services layer, the API is wrong and needs another pass before any code is written.** That is the whole point of doing this as a document first.
