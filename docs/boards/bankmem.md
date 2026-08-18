# `bankmem` — S-100 bank-switched RAM

`bankmem` models the S-100 bank-switched memory cards: several planes of RAM at the same
addresses, with a write-only I/O port that says which plane the CPU sees. It is a **separate
board** from `memory` on purpose — see *Why not a `memory` strap* below — and it holds **one
class with four decoders**, one per real card, chosen by the `card` property.

- Source code: `src/boards/bankmem.{h,cpp}`
- Tests: `tests/test_bankmem.cpp`
- Runnable machine: `machines/bankmem.toml` (`altairsim bankmem`)
- Period references: `reference/Vector Graphic 64K Dynamic RAM.md`,
  `reference/Cromemco 64KZ RAM.md`, `reference/North Star HRAM.md`,
  `reference/SD Systems ExpandoRAM II.md`
- The audit that led here: `docs/devguide/banked-ram.md`

## The cards

Every card reduces to the same shape — a set of RAM **segments**, each with an address window
and an ENABLED flag; a memory cycle is answered by whichever enabled segment covers the address;
the write-only select port toggles those flags. Only the toggle rule differs, and `card` picks it:

| `card` | Real board | Port | The select byte does what? | Banks |
|---|---|---|---|---|
| `vector` | Vector Graphic 64K | `40` | **one-hot select-one**: `0x01`→bank 0, `0x02`→1, `0x04`→2 … `0x80`→7 | ≤ 8 |
| `cromemco64kz` | Cromemco 64KZ / 64KZ-II | `40` | **8-bit mask**: bit *N* enables bank *N*, **several at once** (`0x28`→banks 3 **and** 5) | ≤ 8 |
| `northstar` | North Star HRAM | `C0` | bit 0 = on(0)/off(1), bits 1–7 = a **one-hot address** of which bank to toggle | ≤ 6 |
| `expandoram2` | SD Systems ExpandoRAM II | `FF` | byte = **page number** (**approximation** — see the caveat) | ≤ 10 |

These are four genuinely different decoders, which is exactly why they are not one
parameterized table (`docs/devguide/banked-ram.md`, `DESIGN.md` §4.3). Note that `vector` and
`cromemco64kz` **share port 0x40 with incompatible encodings** — one-hot-select-one versus
mask-of-many — which is the sharpest possible statement of "the board owns its decode".

### `vector` — Vector Graphic 64K
One-hot: exactly one plane is live. Bank 0 is force-enabled at power/RESET (the manual's
theory-of-operation explains the latch-clear + DO0 inversion that makes reset electrically equal
to writing `0x01`). The card also decodes `0x41`/`0x42` as banks 0/1 — bit 6 ignored — because
**OASIS writes those values**; miss it and OASIS boots into the wrong plane and misbehaves later.
A select that is not a tolerated one-hot value is logged and leaves the live bank alone.

### `cromemco64kz` — Cromemco 64KZ / 64KZ-II
The select byte is a **bit-mask of active banks**: `OUT 40H,28H` turns banks 3 and 5 on together.
On real hardware each bank is realized by a 32K block strapped to a bank subset, so several banks
coexist at disjoint addresses; if two live segments cover one address that is a **bus fight**, and
`bankmem` reports it (it does not silently pick a winner behind the scenes). Reset comes up with a
single plane live so the machine has memory to run in.

### `northstar` — North Star HRAM
The byte is **not** a bank number: bit 0 is an on/off command and bits 1–7 are a one-hot address
of which bank the command targets. Banks are switched **individually** — the documented protocol is
switch the old bank off, *then* switch the new one on. One of bits 5/6/7 is spent on parity on the
real board, so ≤ 6 banks are usable. A byte with no single bank bit, or one targeting a bank this
board does not carry, is logged and changes nothing.

### `expandoram2` — SD Systems ExpandoRAM II (approximation)
> ⚠ **The page decode is a documented approximation.** The real board runs the port-FF page number
> through an on-board 82S130 PROM, against the board-select switches and the top address bits, into
> a 32K or 48K partition. That per-cell PROM map is **not transcribable** from the scanned manual
> (`reference/SD Systems ExpandoRAM II.md` says so explicitly), and `DESIGN.md` §0.1 forbids
> inventing hardware we cannot source. So `bankmem` models a plain **binary page-select** and states
> the gap here. If an 82S130 dump turns up, the faithful decode replaces this.

**Common memory (`partition=`).** What the PROM's two stock variants *do* — carve a fixed common
region shared by every page — is well documented (EX-48 = SD# 7010393, EX-32 = SD# 7010392), so that
part is modelled directly:

| `partition` | Banked window | Common region | Total per bank |
|---|---|---|---|
| `none` (default) | `0000-FFFF` (whole 64K plane) | — | 64K |
| `ex48` | `0000-BFFF` (48K) | `C000-FFFF` (16K, shared) | 48K + 16K common |
| `ex32` | `0000-7FFF` (32K) | `8000-FFFF` (32K, shared) | 32K + 32K common |

The common region is **one always-live segment every bank sees**: writing the page port swaps only
the banked window below it. That is exactly what a banked CP/M needs — the resident OS and the
bank-switch routine itself live in common, so they must survive the `OUT` that changes banks.
**SD Systems banked CP/M 3** does precisely this: its BIOS writes the bank number straight to port
FF and puts common at `C000` (`COMBAS=C0` in its GENCPM), i.e. `partition=ex48`
(`reference/SD Systems COSMOS.md`). With `partition=none` the board is the plain whole-plane model
and byte-for-byte unchanged from before.

**Size (`ram=`).** `ram` is total board RAM in KB, and it derives the bank count from the current
partition — the real board is 64K (16K chips) or 256K (64K chips) in four physical banks. With
`ex48`, `ram=256` → 5 banks (16K common + 5×48K); with `none`, `ram=256` → the 4 physical 64K
planes. `banks` still works as the direct count knob; the two are two views of the same geometry.

## Properties

| Property | Kind | Notes |
|---|---|---|
| `card` | enum | `vector \| cromemco64kz \| northstar \| expandoram2`. Sets the decode, the default port, and the bank cap. |
| `port` | int (hex) | The write-only select port. Card default (40/40/C0/FF), overridable — the real boards relocate it via PROM/straps. |
| `banks` | int | How many switchable planes this subsystem carries (one per real board). Card-capped: vector/cromemco 8, northstar 6, expandoram2 10. The common region is not a bank. |
| `partition` | enum | **expandoram2 only** — `none \| ex48 \| ex32`. Carves a shared common region (see *Common memory* above). Refused on the other cards, which are whole-64K planes. |
| `ram` | int | Total board RAM in KB (≤ 256K on expandoram2). Derives `banks` from the partition; the other view of the same geometry. |
| `active` | str | **Read-only** — the live bank(s), and `+ common` when a partition is set. The guest sets this by writing the select port. |
| `fill` | enum | RAM contents at power-on: `zero \| random` (real RAM is not zeroed). |
| `seed` | int | Seed for `fill=random`, so a run is repeatable across POWER. |

There is **no ROM region and no PHANTOM\* role**: none of these cards carried ROM, so what a bank
select would do to a ROM plane is unknown, and we do not guess. Use the `memory` board for ROM.

## Reset and snapshot

POC* and RESET* do the same thing: the select latch clears and every segment returns to its
per-card reset-enable default. **Neither touches one byte of RAM** — only POWER loses memory. A
snapshot carries the RAM store and the live enables; the card geometry is config and is already
correct in a matching machine (`DESIGN.md` §13), exactly as the `memory` board does it.

## Driving it

There is no banked operating system shipped to boot, so `bankmem` is proven by unit tests of each
decode (`tests/test_bankmem.cpp`) and driven by hand from the monitor. The select port is
write-only and the guest owns it, but the monitor's `OUT` writes it too:

```
altairsim bankmem
OUT 40 01            ; Vector: select bank 0
DEPOSIT 1000 A0
OUT 40 08            ; select bank 3 (one-hot 0x08, NOT bank 8)
DEPOSIT 1000 B3
OUT 40 01
DUMP 1000-1000       ; reads A0
OUT 40 08
DUMP 1000-1000       ; reads B3 — the plane really swapped
SHOW mem0            ; the port, the live bank, every plane's window
```

## Why not a `memory` strap?

Banking used to be a `bank_type=` strap on `memory`, with one `{port, banks, one-hot?, mask}`
encoding for all five "cards". `docs/devguide/banked-ram.md` audited that against the period
manuals and found it wrong for four of five: the encoding had been taken from another emulator's
source rather than the manuals (the project's cardinal sourcing rule, `docs/sources.md`), and the
real cards do not share one parameterization — a one-hot select, a bit-mask of many, an
on/off-plus-one-hot toggle, and a PROM page-select are four different decoders. `DESIGN.md` §4.3
argues a board must own its decode; making banking its own board, where each card owns its rule, is
the literal form of that argument. The plain `memory` board is left doing exactly one thing — plain
RAM/ROM — which is also the honest model of the SD Systems ExpandoRAM I, a static-strapped board
with no I/O port at all.
