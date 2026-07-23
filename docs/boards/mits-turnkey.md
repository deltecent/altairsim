# MITS 8800b Turnkey Module

**Status:** done

The board type is `turnkey`. It is the one card that makes an 8800bt — the front-panel-less
"turnkey" Altair — out of a backplane: `src/boards/mits-turnkey.{h,cpp}`, with the serial
half in a reusable `Sio2Port` section (`src/chips/sio2port.{h,cpp}`).

## The real hardware

The **Systems Turnkey Module** (MITS/Pertec, PCBA `200372-01`, schematic `200374`) replaces
the classic Altair front-panel logic in the 8800bt and the MITS 300-series systems. One
S-100 card bundles six subsystems; four of them matter to software:

- **A boot PROM**, four 1702A sockets = 1 KB, normally at `FC00`–`FFFF`. Socket L1 (`FC00`)
  holds the hard-disk loader HDBL, K1 (`FD00`) the Turnkey Monitor, J1 (`FE00`) MBL, H1
  (`FF00`) the floppy loader DBL/MDBL/CDBL.
- **A 6850 ACIA serial channel** ("SIO") at `10h`, compatible with Port A of an 88-2SIO.
- **Sense switches** (SW6/SW7) read at port `FF`.
- **An Auto-Start circuit** that jams a `JMP` onto the bus at reset, booting the PROM page
  the START ADDR switches (SW8/SW9) select.

We model the newer `200372-01` revision. The older "REV 0" board carried 1 KB of onboard
SRAM at `F800`–`FBFF` and no phantom circuit; it and the 88-SYS-CLG/CLG2 field reworks are
out of scope (`reference/MITS Turn Key Board.md` §1, §11).

## Sources

| Source | Path | Authority |
|---|---|---|
| MITS *Systems Turnkey Module* Theory of Operation (200786A), Service Bulletin #007 | `reference/MITS Turn Key Board.md` §1–§11 | The subsystems, the register/bit tables, the phantom one-shot, the Auto-Start byte sequence |
| Martin Eberhard, *8800b Turnkey Module Notes* (2014) | `reference/MITS Turn Key Board.md` §2, §9 | The actual addresses (PROM `FC00`, SIO `10h`), the socket map, and the **post-SB007 input-only** phantom trigger — where the manual only describes the intent |

Where the two disagree, Eberhard's notes describe what the hardware *does*; the MITS manual
describes what it was *meant* to do. See `docs/sources.md`.

## Register reference

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `FC00`–`FFFF` | *(ROM — never answers a write; the write falls through to RAM)* | boot PROM, **while armed** (see Quirks) |
| `10h` (`sio_base`+0) | 6850 control register | 6850 status register |
| `11h` (`sio_base`+1) | 6850 transmit data | 6850 receive data |
| `FF` | *(not decoded — an `OUT FF` is discarded)* | sense switches (SA8..SA15) |

The 6850's control/status bits, baud table, and word-format encoding are the chip's, shared
with the 88-2SIO — see `reference/6850.md` and `docs/boards/mits-2sio.md`.

## How it is simulated

- **Decode.** I/O reads/writes at `sio_base`/`sio_base`+1 go to the SIO; an `IN FF` returns
  the sense byte; memory reads in the PROM window (while armed) return the PROM; the first
  three fetches after reset (addresses 0, 1, 2) return the Auto-Start `JMP`.
- **The SIO** is one `Mc6850` inside a `Sio2Port` — the card delegates every serial concern
  (decode, dispatch, the single `Clock` deadline, interrupts, connect, properties, snapshot)
  to it. The board forwards its `Clock` in `clockAttached()` and binds the section's
  interrupt-changed callback to its own `intChanged()`.
- **PHANTOM\*.** While the PROM is armed the board asserts `PHANTOM*` over its window, so a
  RAM board strapped `honors_phantom = read` defers reads there and still answers writes —
  the byte lands in the RAM under the shadow. This is the same mechanism as the Tarbell
  (`tests/test_phantom.cpp`).
- **Auto-Start.** For the first three fetches after any reset the board drives `C3 00 <hi>`
  (JMP to `start`) and asserts `PHANTOM*` so no other memory board contends. Then a latch
  clears and low memory reverts to RAM. It follows the 88-VI/RTC precedent of a card jamming
  an opcode onto the bus.
- **Interrupts:** the SIO's, via its `tty` unit's `interrupt` strap (`none | int | vi0..vi7`).
- It does not master the bus.
- **Sockets** are a `[[board.socket]]` sub-unit table (`at` + `mount`), loaded with the same
  Intel-HEX path as a memory card's ROM region (`builtin:` or a file).
- `properties()`: `prom` (window base), `start` (START ADDR), `sense`, `sio_base`.

### Reset

- `Reset::PowerOn` (POC*, cold): re-read the socket ROMs; power-on the 6850; **re-arm** the
  phantom PROM and the Auto-Start jam.
- `Reset::Bus` (RESET*, warm): the 6850 is untouched (it has no reset pin), but **both
  latches re-arm** — a front-panel reset re-enables the boot PROM (`reference` §9). Memory
  and mounted media survive.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The boot PROM **phantoms out** on the first `IN` from port `FE`/`FF`, and stays out until a reset | A program that reads the sense switches (Altair BASIC, and DBL for its stop bits) never gets its top 1 KB of RAM back — or, modeled as a plain ROM, the PROM shadows RAM forever and a 64K program crashes |
| The trigger is an **input only** (post-Service-Bulletin-007); an `OUT FE/FF` does not disable it | Model it as any-I/O and the PROM vanishes on the first `OUT` a boot loader does, before it has finished |
| The **sense-switch read at FF is the same event** as the phantom disable | Two separate mechanisms drift apart; the machine works until the one program that reads sense from PROM |
| Auto-Start jams `C3 00 <hi>` — the low byte is **always 0**; SW8/SW9 are the high eight bits | A non-page-aligned boot address; the JMP lands in the wrong place |
| A write into the PROM window **falls through** to the RAM beneath (ROM never answers a write; the RAM is `honors_phantom = read`) | The bootstrap cannot deposit into the RAM it is running over, and 64K is unreachable |

## Limitations and deliberate departures

- **Newer board only.** No REV 0 onboard SRAM at `F800`–`FBFF`, and none of the 88-SYS-CLG /
  CLG2 reworks. A machine that needs RAM there adds a `memory` region for it.
- **Post-SB007 timing only.** The as-built "any I/O disables the PROM" bug is not offered as
  a mode; if it is ever wanted it becomes a `phantom_trigger` strap.
- **This card owns port `FF`.** Because it answers the sense switches there, a machine with a
  Turnkey Module must not also carry an `fp` board — they would contend for the port. (The
  8800b-with-full-front-panel configuration, where the panel drives FF and the Turnkey Module
  only snoops it, is not modeled; it would need a `sense_enabled = off` strap.)
- Only DBL (`FF00`) and HDBL (`FC00`) exist as built-in ROMs; TURMON and MBL are not in the
  tree, but the socket table accepts any `builtin:` name or file.

## Verification

- `tests/test_turnkey.cpp` drives a real bus (`setVerify(true)`) and pins the phantom
  one-shot (armed at reset; `IN FF`/`IN FE` disable it; `OUT FF` does not; both resets
  re-arm), the Auto-Start `JMP` jam and its START ADDR strap, the sense switches, and the SIO
  at `10h`.
- `tests/test_sio2.cpp` (unchanged) proves the shared `Sio2Port` did not disturb the 88-2SIO.
- The acceptance suite boots CP/M on the 8800bt both ways — `examples/turnkey/floppy.toml`
  (DBL → 56K CP/M) and `examples/turnkey/hdsk.toml` (HDBL → 48K CP/M off an 88-HDSK) — through
  the shipped binary (`tests/acceptance/examples.cmake`).

## References

- `reference/MITS Turn Key Board.md` — the distilled hardware reference (this board's spec).
- `docs/boards/mits-2sio.md`, `reference/6850.md` — the 6850 the SIO section reuses.
- `docs/boards/tarbell-sd.md`, `tests/test_phantom.cpp` — the PHANTOM* pattern.
