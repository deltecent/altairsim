# `bankmem` — S-100 bank-switched RAM

`bankmem` models the S-100 bank-switched memory cards: several planes of RAM at the same
addresses, with a write-only I/O port that says which plane the CPU sees. It is a **separate
board** from `memory` on purpose — see *Why not a `memory` strap* below — and it holds **one
class with five decoders**, one per real card, chosen by the `card` property.

- Source code: `src/boards/bankmem.{h,cpp}`
- Tests: `tests/test_bankmem.cpp`
- Runnable machine: `machines/bankmem.toml` (`altairsim bankmem`)
- Period references: `reference/Vector Graphic 64K Dynamic RAM.md`,
  `reference/Cromemco 64KZ RAM.md`, `reference/North Star HRAM.md`,
  `reference/SD Systems ExpandoRAM II.md`, `reference/SD Systems ExpandoRAM III.md`
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
| `expandoram2` | SD Systems ExpandoRAM II | `FF` | **low nibble** = page; one segment per page for the whole subsystem | ≤ 10 (11 with a partition) |
| `expandoram3` | SD Systems ExpandoRAM III | `FF` | **low nibble** = page; the PROM map picks a 16K cell per CPU quarter | 5 or 10 pages |

These are five different decoders, which is exactly why they are not one
parameterized table (`docs/devguide/banked-ram.md`, `DESIGN.md` §4.3). Note that `vector` and
`cromemco64kz` **share port 0x40 with incompatible encodings** — one-hot-select-one versus
mask-of-many — which is the sharpest possible statement of "the board owns its decode".

### `vector` — Vector Graphic 64K
One-hot: exactly one plane is live. Bank 0 is force-enabled at power/RESET (the manual's
theory-of-operation explains the latch-clear + DO0 inversion that makes reset electrically equal
to writing `0x01`). The card also decodes `0x41`/`0x42` as banks 0/1 — bit 6 ignored — because
**OASIS writes those values**; miss it and OASIS boots into the wrong plane and misbehaves later.
A select that is not a tolerated one-hot value is logged and leaves the live bank alone.

**A bank with no board answers nothing.** Each real board compares the latch with its own switch.
A select of a bank that this subsystem does not carry takes the live bank off the bus. OASIS finds
its banks with this: it complements a byte in a bank and reads it back. If the old bank stayed live,
every bank would look like RAM, and the test would change OASIS's own code.

**`window_lo` and `window_hi`** set the addresses of each plane. A 64K board starts at `0000`, and
jumper area D sets its last address: `FFFF`, `DFFF` as shipped, or `BFFF`. The option of 48K plus 8K
at `E000` has two windows and is not built. A window that starts higher stands for the 16K static
boards, which the 64K RAM manual says carry the same bank select.

The `vector` machine uses `4000-DFFF`, and a `memory` board answers `0000-3FFF` in every bank. OASIS
needs that split, because its copy routine and its stack stay in low memory across a switch. The
*OASIS System Reference Manual* (`SET BANK`) gives 16K, 32K or 48K for this common memory. The
`oasis` map of the ExpandoRAM III is also `0000-3FFF`. OASIS sets bit 6 in each byte it writes to
port 40, which fits a board strapped to bank 6. No Vector Graphic document shows which board held
the common memory.

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

### `expandoram2` — SD Systems ExpandoRAM II
The real board latches only the low four bits of the port-FF byte (U13 74LS174; BDO4–5 go to
unconnected outputs) and runs them through an on-board 82S130 PROM, with the board number (S3)
and bus A15–A14, to pick one 16K bank on one board (`reference/SD Systems ExpandoRAM II.md` §4,
Fig 2-4). `bankmem` keeps **one segment per page for the whole subsystem**, not one per board.
The guest sees the same pages at the same addresses. What it does not model is which board
holds which 16K of a page, and the II's S3 bank enables.

- **Select.** The page is `data & 0x0F`, so `OUT FF,13` is page 3.
- **A page with no RAM.** If the subsystem carries no page by that number, the PROM drives no
  bank enable and nothing answers the switched window. The common region still answers, and
  the board logs one line. (Before, the whole byte was the page, and a page it did not carry
  was logged and left the old page live.)
- **Page A.** Fig 2-4 maps a page A that the manual's text ("0-9") leaves out. On `ex32` it is
  a full 32K page (board 5), so the cap is 11 pages. On `ex48` it is a single 16K cell (board 7,
  bank 3) at A=2, `8000-BFFF`. ⚠ The text (p. 12) puts it at `C000H`. The figure is the PROM
  program, so the model follows the figure.
- **Reset.** The manual does not describe reset. Page 0 at reset is an approximation.
- ⚠ **Partial pages are not built.** The model counts whole pages. On the real board, two
  `ex48` boards also hold one 16K cell of page 4 (board 1 bank 3). With `ram=128`, page 4 is
  empty here.
- ⚠ **`partition=none` is an approximation.** It is one board of 4164s ("4-64K banks"), and
  page = bank. The manual gives no map for that board.

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
partition. A real board is 64K (16K chips) or 256K (64K chips) in four physical banks. The 32K and
48K maps in the manual are drawn for **16K chips**, so with `ex48` or `ex32` the value counts
64K boards:

| `partition` | `ram` | Boards | Pages |
|---|---|---|---|
| `ex48` | `256` | 4 | 0–4 (16K common + 5×48K) |
| `ex48` | `512` | 8 (the most) | 0–9 + the 16K page A |
| `ex32` | `256` | 4 | 0–6 (32K common + 7×32K) |
| `ex32` | `384` | 6 (the most) | 0–A |
| `none` | `256` | 1 board of 4164s | 4 whole 64K planes ("4-64K banks"), the most |

A value that is not a whole number of pages snaps down, with a log line. `banks` still works as
the direct count knob; the two are two views of the same geometry. `partition` comes before
`banks` in the property list, so a CONFIG SAVE file with `banks = 11` loads back.

### `expandoram3` — SD Systems ExpandoRAM III
The III is modelled from its PROM map (Fig 2-2, printed 2-9 of the manual; see
`reference/SD Systems ExpandoRAM III.md` §4). Only the parts that work on a 16-bit bus are
in: port FF, the page map, the S3 configuration switch and the S3 bank enables.

**The cell model.** Each board has 4 banks of 4164s, and each bank is 64K: four 16K **RAM
quarters**. The PROM's outputs are a bank enable and the two RAM address lines A14–A15. For
each page and each CPU quarter (bus A15–A14), the PROM therefore picks one 16K cell: a bank
and a RAM quarter. `bankmem` builds each cell of the figure as one `Segment`, with a 16K
window at the CPU quarter the cell answers, `key` = its page, and `bank` = board × 4 + bank.
The common cell is a `common` segment.

**Select.** `OUT FF` latches the byte, and the page is `data & 0x0F`. The schematic shows
U13 (74LS174) passing D0–D3 to the PROM, and D4–D5 to unconnected outputs. A cell is live
when its page is the latched page, or it is the common cell, **and** its bit in
`bank_enable` is set. If no cell carries the page (5–9 on one board, A–F always), nothing
answers the switched window, and the board logs one line. The PROM drives no bank enable
for such a page, so the board does not drive the bus (manual 2-4). The common cell still
answers. POC and RESET clear the latch to page 0.

**The two maps (`config`, S3 switch 5).**

| `config` | Switched window | Common | Board 0 | Board 1 |
|---|---|---|---|---|
| `cpm` (switch ON, default) | `0000-BFFF` | `C000-FFFF`, board 0 bank 3 | pages 0–4 | pages 5–9 |
| `oasis` (switch OFF) | `4000-FFFF` | `0000-3FFF`, board 0 bank 0 | pages 0–4 | pages 5–9 |

**Fixes to the printed map.** Fig 2-2 prints some quarter labels wrong. Each fix is in a
code comment in `bankmem.cpp` beside the table:

- **Page 4** (board 0, bank 3 in `cpm`, bank 0 in `oasis`): printed with the common quarter
  three times (A=3 or A=0). Built as the three quarters of the switched window.
- **Page 8** (board 1, RAM quarter 3 of banks 0–2): printed A=3 (or A=0) three times. Built
  with the quarter of each bank, as pages 5–7 in the same bank are printed.
- **Board 1's leftover cell** (bank 3 quarter 0 in `cpm`, printed "A=3 P=0"; bank 0 quarter
  3 in `oasis`, printed "A=2 A=1 P=A") is **not built**. Two boards cannot both answer the
  common quarter, and the common cell must be on board 0 for a one-board system. The II's
  Fig 2-4 has a 16K page A in the same place, which is the likely answer.

The II's Fig 2-4 draws its pages 4 and 8 with three different quarters, which supports the
first two fixes. Which RAM quarter holds which CPU quarter is invisible to software, unless a
bank is switched off. Every fix keeps the cell in the bank the figure prints. A PROM dump
(COSMOS-48K, SD 7010479) would replace the table and nothing else.

**Out of scope.** The IEEE-696 items (E22–E24 φ pin, E25–E27 RESET or POC, pSTVAL),
wait states and refresh (E14–E15), the strap options E16–E18 and E19–E21 (the manual says
they need new PROM firmware), and boards 2 and 3 (Fig 2-2 shows S=0 and S=1 only). PHANTOM
is the existing `honors_phantom` jumper.

**Compatibility with the II.** The `cpm` map gives the same 48K pages under a 16K common at
C000 as the II's `partition=ex48`, with the same page numbers. A BIOS written for the II with
the 48K PROM runs unchanged. The SD Systems CP/M 3 banked disk proves it
(`acceptance-cpm3-banked-eram3`). A BIOS for the II's 32K PROM, or one that uses the II's
16K page A, has no equivalent map on the III.

## Properties

| Property | Kind | Notes |
|---|---|---|
| `card` | enum | `vector \| cromemco64kz \| northstar \| expandoram2 \| expandoram3`. Sets the decode, the default port, and the bank cap. |
| `port` | int (hex) | The write-only select port. Card default (40/40/C0/FF/FF), overridable — the real boards relocate it via PROM/straps. |
| `banks` | int | How many switchable planes this subsystem carries (one per real board). Card-capped: vector/cromemco 8, northstar 6, expandoram2 10 (11 with a partition: page A). On expandoram3 it reads the pages carried, and takes only 5 or 10 (one or two boards), so a CONFIG SAVE file loads back. The common region is not a bank. |
| `window_lo`, `window_hi` | int (hex) | **vector only**: the first and last address of each plane, on page boundaries. Default `0000` and `FFFF`. Other cards accept only those values, so a CONFIG SAVE file loads back. |
| `partition` | enum | **expandoram2 only** — `none \| ex48 \| ex32`. Carves a shared common region (see *Common memory* above). Refused on the other cards: vector, cromemco64kz and northstar are whole-64K planes, and the expandoram3 PROM fixes its own map. |
| `ram` | int | Total board RAM in KB (on expandoram2: ≤ 256K with `partition=none`; with a partition it counts 64K boards, ≤ 512K on ex48 and ≤ 384K on ex32). Derives `banks` from the partition; the other view of the same geometry. On expandoram3: `256` (one board) or `512` (two boards). |
| `config` | enum | **expandoram3** — `cpm \| oasis`, S3 switch 5 (see the maps above). Default `cpm`. Other cards accept only `cpm`, the factory value, so a CONFIG SAVE file loads back. |
| `bank_enable` | int (hex) | **expandoram3** — S3 switches 1–4 of each board: bit = board × 4 + bank, set = enabled. Default `FF`. A switched-off bank answers nothing. Other cards accept only `FF`. |
| `active` | str | **Read-only** — the live bank(s), and `+ common` when a partition is set. On expandoram3: `page N + common`, or `page N (no RAM) + common`. The guest sets this by writing the select port. |
| `honors_phantom` | enum | A jumper — `none \| read \| all`. When another board pulls PHANTOM\* (a boot PROM shadowing the RAM this card sits under), do I step off the bus? `read` steps aside for reads but keeps taking writes, so a cold-boot loader falls through to this RAM (the ExpandoRAM II under the SBC-200 boot PROM). Default `none`. |
| `fill` | enum | RAM contents at power-on: `zero \| random` (real RAM is not zeroed). |
| `seed` | int | Seed for `fill=random`, so a run is repeatable across POWER. |

There is **no ROM region**: none of these cards carried ROM, so what a bank select would do to a
ROM plane is unknown, and we do not guess. Use the `memory` board for ROM. The card therefore never
*asserts* PHANTOM\* (that is a ROM card's job); it only *honors* it (`honors_phantom`), for the case
where the banked RAM sits under someone else's boot PROM.

## Reset and snapshot

POC* and RESET* do the same thing: the select latch clears and every segment returns to its
per-card reset-enable default. **Neither touches one byte of RAM** — only POWER loses memory. A
snapshot carries the RAM store and the live enables; the card geometry is config and is already
correct in a matching machine (`DESIGN.md` §13), exactly as the `memory` board does it.

## Driving it

Each decode is proven by unit tests (`tests/test_bankmem.cpp`), and the `expandoram2` card boots a
real banked operating system: `tests/media/sdsys/cpm3-b.toml` runs **SD Systems CP/M Plus 3.0
(banked)** on an SBC-200 + VersaFloppy II with this board as 256K of ExpandoRAM II
(four 48K-PROM boards of 16K chips: `partition=ex48`, `honors_phantom=read`). The CP/M 3 BIOS bank-switches port FF with its resident
half in the common region — `SHOW mem0` at the `A>` prompt reads `active = bank N + common`.
The same disk boots unchanged on `card=expandoram3` (`tests/acceptance/cpm3-banked-eram3.toml`,
test `acceptance-cpm3-banked-eram3`), where `active` reads `page N + common`.

The select port is also write-only and the guest owns it, but the monitor's `OUT` writes it too, so
you can watch a plane swap by hand with no guest software:

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
