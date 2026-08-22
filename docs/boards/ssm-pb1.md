# SSM PB1 — 2708/2716 EPROM programmer & 4K/8K EPROM board

**Status:** done — the programmer (both sockets) and the read-only on-board EPROM area.

## The real hardware

The **PB1** is an S-100 board from **SSM Microcomputer Products** (formerly **Solid State
Music**), software © 1978. It does two unrelated jobs on one card:

1. an **EPROM programmer** — two sockets, **U22** for a 2708 and **U23** for a 5-volt 2716,
   with the programming voltage (+26.5 V) generated on-board by a TL497 (no external supply);
   and
2. an **on-board read-only EPROM area** — four sockets (U11–U14) holding 4K of 2708 or 8K of
   2716, mappable to any boundary above `8000H`.

The programmer half is **software-driven**. An `OUT` to one control port arms a flip-flop and
latches D0/D1 to pick 2708-vs-2716 timing; the CPU then **writes each byte to the socket's
memory address**, and the board stretches that write into a multi-millisecond programming pulse
by holding the S-100 `READY` line. A **read** of the socket window resets the flip-flop (the
LED goes out). Configuration is by two DIP switches (SW2 = socket window + control-port
address; SW3 = on-board area) and jumpers (EPROM type, 0–4 read wait states, PRDY/XRDY); SW1
is a safety switch that gates the programming voltage.

## Sources

| Source | Path | Authority |
|---|---|---|
| SSM PB1 instruction manual (1978/1979), read as page images | `reference/SSM PB1 EPROM Programmer.md`, `docs/sources.md` (`pb1.pdf`) | Port map, control-port bits, programming handshake, address decode, the four 8080 driver routines |
| retrocmp.de SSM PB1 page | `docs/sources.md` | Cross-check of the control-port bits (`01`→2708, `02`→2716) and the D000/4000 addresses in the sample software |

The manual and the retrocmp page agree on the control-port bits and the sample software's
addresses. The manual's object listings (sections 4.2–4.5) are authoritative for the driver
code and are transcribed verbatim into `examples/pb1/*.{ASM,HEX}` — the two burners
(2708/2716) and the two verify routines (erase-check, copy-verify). They keep the manual's own
`JMP F021H` / `CALL F009H` to the **SSM 8080 monitor** (manual §3.3); altairsim does not ship
that monitor yet, so `examples/pb1/README.md` shows how to run them without it.

## Register reference

One output port (default `10H`), plus a 4K memory window (default `D000`) for the sockets and
an optional read-only window above `8000H` for the on-board area.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| control port (`x0H`; only A4–A7 decode) | arm the programming flip-flop; **D0=1 → 2708 (U22), D1=1 → 2716 (U23)** | — (write-only; an IN here is not the board's) |
| socket window + offset (armed) | program a byte into the selected socket (`buf &= data`) | read the socketed chip **and reset the flip-flop** |
| on-board EPROM area | ignored (read-only) | read the on-board chip |

## How it is simulated

- **Decodes** three things, dispatched on cycle type: an `IoWrite` to the control port; a
  `MemRead`/`MemWrite` in the 4K programming window; and a `MemRead` in each mounted on-board
  PROM socket's range. The decode does **not** depend on the arm flip-flop or the type latch —
  it is pure and cacheable — so whether a window write actually burns is decided in `write()`,
  not in `decodes()`.
- **`write()`**: an armed window write ANDs the byte into the selected socket buffer (2708 =
  1K, 2716 = 2K); an un-armed window write is dropped; a control-port write sets the flip-flop
  and latches the chip from D0/D1.
- **`read()`**: a window read returns the selected socket's byte and **resets the flip-flop**
  (the disarm the period routines rely on). `peek()` does the same read **without** the reset,
  so DISASM/TRACE and `SHOW` never disturb the board.
- **Making a hex file**: nothing on the board writes files. The burned window is ordinary
  readable memory, so the monitor's `SAVE <file> <window>` (which reads the range off the bus
  and emits Intel HEX) is the whole "save the chip" path. This is issue #382's answer.
- **Sockets** are sub-units. `[[board.socket]]` (`chip` = 2708|2716, `mount`) preloads the two
  programming sockets; `[[board.prom]]` (`at`, `mount`) adds read-only on-board chips. Images
  load through the same Intel HEX / S-record / binary auto-detect the memory card's ROM regions
  use, and are re-read on power.
- **Properties:** `port` (radix 16, must be an `x0H` address) and `window` (radix 16, 4K
  boundary). No interrupts, no bus mastering, no `Clock` use.

### Reset

- `Reset::PowerOn` (POC*, cold): re-read every socket/PROM image from its mount (an unmounted
  socket becomes an erased, all-`FF` chip), clear the arm flip-flop, and default the type latch
  to 2708.
- `Reset::Bus` (RESET*, warm): clear the arm flip-flop only — the S-100 power-on-clear line
  resets it on the real board too. The socket buffers (a burn in progress) survive.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Programming ANDs into the socket (`buf &= data`) — a cell can only go 1→0 | A "re-burn" over existing data would appear to set bits back to 1; software that relies on erase-then-program would pass on a bug that fails on real silicon |
| A **read** of the window disarms the board | The SSM routines finish with `LDAX D`; without the reset the board would stay armed and a later stray write would corrupt the chip |
| A window write only burns while **armed** | A monitor `DEPOSIT` or a bus probe into the window would silently alter a chip nobody meant to program |
| The 2708 socket is 1K, the 2716 is 2K | Burning past a 2708's 1K would "succeed" into space that a real chip does not have |
| The on-board area ignores writes | Treating it as RAM would let a guest scribble a "ROM" |

## Limitations and deliberate departures

- **No programming-pulse timing, no wait states, no READY handshake.** On the real card a
  window write stretches into a ~0.6 ms (2708) or ~50 ms (2716) pulse held by the S-100
  `READY` line, and **wait states are mandatory** or the pulse is too short to program a cell.
  In the simulator a bus write simply lands. This carries no guest-visible state — the byte is
  programmed either way — so software that burns correctly on hardware burns correctly here,
  and the ~160 s / ~100 s a real burn takes is instant. Software that *measures* the pulse (a
  scope check) has nothing to see.
- **No SW1 safety gate and no +26.5 V rail.** There is no voltage to switch on, so SW1 and the
  TL497 supply are not modeled; you cannot "forget to turn on the programmer".
- **A burn is not non-volatile across a power-cycle** unless it is backed by a socket `mount`.
  Power re-reads the sockets from their images (the ROM idiom), so `SAVE` the burn first, or
  `MOUNT` it back, to continue. A `SNAPSHOT` *does* carry a burn.
- **The on-board area is modeled as independent read-only sockets**, not the exact SW3 boundary
  decode; each `[[board.prom]]` places one image at an `at` address, which is enough to hold
  firmware or act as a copy source.

## Verification

- `test_pb1` (unit): drives a real bus for the decode, the arm/type/AND/disarm mechanics, the
  socket selection and chip sizes, the read-only on-board area, and a SNAPSHOT round-trip — and
  then **runs the actual SSM 2708 programmer** (the manual's object code) through an 8080 to its
  HLT and checks that all 1024 source bytes landed in the socket, and that the result round-trips
  through Intel HEX.
- `acceptance-pb1`: boots the `examples/pb1/pb1.toml` machine, `LOAD`s `examples/pb1/PROG2708.HEX`,
  runs it (breaking at `F021`, where the burner returns to the monitor), and `SAVE`s the socket
  to a host hex file — asserting the burned chip equals the source and that the file is valid
  Intel HEX. **If it fails, the board is wrong, not the software.**

## References

- `reference/SSM PB1 EPROM Programmer.md` — the distilled hardware reference.
- `examples/pb1/` — the four SSM driver routines (§4.2–4.5), transcribed and runnable, with a
  machine and a burn-to-hex walkthrough.
- GitHub issues #397 (add the board) and #382 (why a burner board, not just `LOAD … ROM`).
