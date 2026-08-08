# North Star Micro-Disk System (MDS-A single-density / MDS-A-D double-density)

Source: [Single Density Controller (2nd version).pdf](#) (North Star *Micro-Disk System
MDS-A*, © 1977, **Revision 5**, for the MDS-A4 PC board; July 31 1978 errata),
[Double Density Controller.pdf](#) (North Star *Micro-Disk System MDS-A-D Double Density*,
© 1978, **Revision 1**; January 16 1979 errata)

Two generations of one North Star S-100 floppy-disk controller for **8080 or Z80** systems,
documented together because the double-density board is the single-density board's successor and
they share their whole personality: a 5¼″ **Shugart SA-400** hard-sectored minifloppy (10 sector
holes + 1 index hole per revolution), **35 tracks × 10 sectors**, an onboard **256-byte bootstrap
PROM**, a 4 MHz crystal ÷2 → **2 MHz** controller clock, and — the trait that makes this board
unlike every other floppy controller in the tree — a **memory-mapped command interface**: the
controller is not an I/O-port device at all. It occupies a **1 K byte block of CPU memory**
(standard origin **`E800H`**), and every command is a **memory *read*** whose address bits *are*
the command. Data transfer is by **CPU wait state** (the board stalls the processor via `PRDY`
until the write shift register empties or the read shift register fills), never DMA and never
interrupts.

The **MDS-A** (1977) is **single density only** (FM), 256-byte sectors, ~89.6 KB/diskette. The
**MDS-A-D** (1978) is a superset: it keeps single density and adds **double density** (MFM),
512-byte sectors, **179.2 KB/diskette** (up to 4 drives → 716.8 KB), plus a double-sided `SS`
select. In gaining double density the MDS-A-D **reorganized the command interface** — the four
address "cases" mean different things on the two boards, the status bytes differ, and drive/side/
density/step moved into a new "Controller Orders" case. That reorganization is the point of
grouping them; see §2 and the differences table in §1.

This is a distilled emulation reference. Kit assembly, soldering, parts lists, power-supply and
cabinet options, the PLL/data-separator analog circuitry, and the checkout waveform tables are
omitted except where they set a software-visible value. The FM/MFM bit-cell encoding is
described only at the byte/sector level a controller model needs; North Star's format is **not**
IBM 3740 — it is North Star's own hard-sectored layout (§4).

---

## 1. What differs between MDS-A and MDS-A-D

| Feature | MDS-A (single density, 1977) | MDS-A-D (double density, 1978) |
|---|---|---|
| Recording | FM (single density) only | FM **and** MFM (density chosen per operation) |
| Sector data size | 256 bytes | 256 (SD) **or** 512 (DD) |
| Capacity / diskette | 35 × 10 × 256 ≈ **89.6 KB** | **179.2 KB** (DD); up to 4 drives → 716.8 KB |
| Sides | 1 (SA-400) | `SS` bit selects side of a double-sided diskette |
| **Address case map** | 0/1 = PROM · 2 = write-data · 3 = command | 0 = PROM · **1 = write-data** · **2 = Controller Orders** · 3 = command |
| Drive/side/density/step | folded into Case-3 command codes + `M1M0` | separate **Case-2 "Controller Orders"** register (`DD SS DP ST DS`) |
| Drive-select encoding | `M1M0` (2-bit binary, drives 0–3) via `CC=0` | `DS` field **one-hot**: `1`/`2`/`4`/`8` = drive 1/2/3/4, `0` = none |
| Status bytes | **two** (A, B) | **three** (A, B, C) — different bit layouts (§3) |
| Case-3 fields | `MO RD BST CC(3) M1 M0` | `DM(3) CC(3)` (`DM` = which byte on the DI bus) |
| Motor-on | `MO` bit in every Case-3 command | Case-3 command **code 5** ("turn on drive motors") |
| Interrupt control | `CC=3` (arm/disarm via `M0`) | Case-3 **codes 2 (disarm) / 3 (arm)** |
| Sector sync char | `FB` ×1 | `FB` ×1 (SD) / ×2 (DD) |
| Standard PROM origin | `E800H` (PROMs `LE820-3`, `RE820-3`, `SE8-1`) | `E800H` (PROMs `DWE-1`, `DSEL-E8-1`, `DPGM-E8-1`) |
| Auto-motor-off | fixed timer | **9.6 s** default, jumper-selectable **3.2–38.4 s** |

Everything in §2–§5 is common to both boards except where a heading calls out SD or DD.

---

## 2. The memory-mapped command interface

The controller responds to references anywhere in a **1 K block** of CPU address space. Commands
are issued as **memory read cycles**; the address decodes as three fields:

```
 A15 .................. A0
| BS (high 6 bits)  | CASE (2) | low 8 bits = data / command / PROM address |
```

- **BS** — high 6 address bits, matched by the board-select PROM. When they match, the board is
  selected (its 1 K window). Standard window is **`E800H`–`EBFFH`**.
- **CASE** — the next two bits (address bits 9,8) pick the subcase.
- **low 8 bits** — meaning depends on the case: a PROM offset, the data byte to write, an orders
  byte, or a command byte.

Because a "write a byte to disk" is performed by *reading* an address whose low 8 bits hold the
data, an emulator must decode this board on the **memory-read path over its 1 K region**, not on
`IN`/`OUT`. A read that lands in the write-data case with the shift register still busy **hangs
the CPU** (wait state) until it drains.

### Case map — MDS-A (single density)

| Case | Meaning | Low 8 bits |
|---|---|---|
| 0 | Optional PROM addressing | PROM offset (**errata: now identical to Case 1** — both read the standard 256-byte PROM) |
| 1 | PROM addressing | PROM offset 0–255 |
| 2 | Write byte of data | data byte (hangs CPU until write shift register empty) |
| 3 | Controller command | `MO RD BST CC CC CC M1 M0` (see §3) |

### Case map — MDS-A-D (double density)

| Case | Meaning | Low 8 bits |
|---|---|---|
| 0 | PROM addressing | PROM offset 0–255 |
| 1 | Write byte of data | data byte (hangs CPU until write shift register empty) |
| 2 | Controller **Orders** | `DD SS DP ST | DS DS DS DS` — load the 8-bit order register |
| 3 | Controller **Commands** | `DM DM DM | CC CC CC` (top bit unused) |

**MDS-A-D Case-2 order fields:** `DD` density (1 = double, 0 = single, on write); `SS` side
(0 = bottom/only side, 1 = top); `DP` shared — step direction on a step (1 = in, 0 = out) *and*
write-precompensation enable (precomp iff `DP=1`) during a write; `ST` head-step signal level;
`DS` drive-select, **one-hot** (0 none, 1 drive 1, 2 drive 2, 4 drive 3, 8 drive 4).

---

## 3. Commands and status

### MDS-A (single density) — Case 3 command byte

`MO RD BST CC(3 bits) M1 M0`

- **MO** — 1 = turn drive motors on (if off) and reset the auto-motor-off timer; 0 = no action.
- **RD** — 1 = read a data byte from the read shift register onto the DI bus, hanging the CPU
  until the register is full; 0 = gate a status byte onto the DI bus instead.
- **BST** — 1 = gate **B-status**; 0 = gate **A-status** (only meaningful when `RD=0`).
- **CC** — command code:
  | CC | Action |
  |---|---|
  | 0 | Load drive-select register from `M1,M0`; lower head on selected drive |
  | 1 | Write record — start a write-sector sequence |
  | 2 | Load track-step flip-flop from `M0` |
  | 3 | Load interrupt-armed flip-flop from `M0` |
  | 4 | No operation |
  | 5 | Reset sector flag |
  | 6 | Reset controller, raise heads, stop motors |
  | 7 | Load step direction from `M0` (1 = step in, 0 = step out) |

**MDS-A status bytes** (two):

```
A-Status:  SF WN 0 MO WRT BDY WP TR0
B-Status:  SF WN 0 MO | SP SP SP SP        (SP = sector position, 4 bits)
```

### MDS-A-D (double density) — Case 3 command byte

`DM(3 bits) CC(3 bits)`

- **DM** — what gets multiplexed onto the DI bus during the command: `1` = A-status, `2` =
  B-status, `3` = C-status, `4` = read data (may enter wait state until the read register fills).
- **CC** — command code:
  | CC | Action |
  |---|---|
  | 0 | No operation |
  | 1 | Reset sector flag |
  | 2 | Disarm interrupt |
  | 3 | Arm interrupt |
  | 4 | Set body (diagnostic) |
  | 5 | Turn on drive motors |
  | 6 | Begin write |
  | 7 | Reset controller, de-select drives, stop motor |

**MDS-A-D status bytes** (three):

```
A-Status:  SF IX DD MO WI RE SP BD
B-Status:  SF IX DD MO WR SP WP T0
C-Status:  SF IX DD MO | SC SC SC SC       (SC = sector counter, 4 bits)
```

### Status bit glossary (union of both boards)

| Bit | Meaning |
|---|---|
| SF | Sector Flag — a sector hole was detected (set by hardware, reset by software command) |
| WN / WI | Window — status/byte was read during the ~96 µs post-sector-pulse window |
| MO | Motor On |
| WRT / WR | Write — controller ready to receive a data byte to write (SD) / valid write in progress (DD) |
| BDY / BD | Body — sync character found; data bytes can now be read |
| WP | Write Protect — the selected drive's diskette is write-protected |
| TR0 / T0 | Track 0 — the selected drive is at track 0 |
| SP (single bit) | Spare (DD A/B-status) |
| SP (field) | Sector Position / Sector Counter — current sector (SD B-status / DD C-status) |
| IX | Index Detect — index hole seen during previous sector (DD) |
| DD | Double-Density Indicator — data being read is double-density encoded (DD) |
| RE | Read Enable — phase-locked loop enabled (DD) |

---

## 4. Disk data format

35 tracks, 10 hard-sectored sectors/track. Each sector's data is recorded starting **~96 µs after
its sector hole** is detected (the "window"). A read or write command must be issued within that
96 µs window.

| Field | Single density | Double density |
|---|---|---|
| Zeros (preamble) | 16 bytes | 32 bytes |
| Sync char (`FB`) | 1 byte | 2 bytes |
| Data | 256 bytes | 512 bytes |
| Check char | 1 byte | 1 byte |
| **Sector total** | **274 bytes** | **547 bytes** |

**Check character** — *not* a CRC. It is computed iteratively: start at zero, then for each data
byte, **XOR** it into the running value and **rotate the result left by one bit** (left-cycle).
The final value is the stored check byte. A verify/read compares the recomputed value against the
stored one.

**Write sequence** (software, per sector, after positioning): issue begin-write → wait for the
write-status bit → write 15 more bytes of zeros (the hardware writes the first zero byte itself) →
write the sync char(s) `FB` → write the data bytes while accumulating the check char → write the
check char → stop at the next sector pulse. **Read sequence:** wait for sync detection (body
mode; MDS-A reports an error if no sync within 16 byte-times) → read the data bytes while
accumulating the check char → read and compare the stored check char. **Verify** is the read
path but compares each byte against RAM instead of storing it.

---

## 5. Timing, motor, and interrupts

- **Rotation:** 300 RPM ⇒ **20 ms/sector** (10 sectors/rev). Confirmed by the drivers' "wait 2
  sector times (40 ms)" after a head step. ⚠ The MDS-A manual's spin-up step reads *"wait 1
  second (i.e. 5 sector times)"* — but 5 sector-times is only 100 ms; 1 s ≈ **5 revolutions**.
  Treat the parenthetical as a manual slip and honor the ~1 s spin-up if timing matters (see
  [[altairsim-plausible-but-wrong-timing]]).
- **Head step:** set step flip-flop → wait ≥10 µs → reset it → wait 2 sector times (40 ms) per
  track stepped.
- **Read loop deadline (MDS-A):** the per-byte read loop must complete in **< 64 µs** or data is
  lost.
- **Auto-motor-off:** MDS-A-D turns motors off **9.6 s** after the last activity by default
  (jumper table: 3.2 / 6.4 / 9.6 / 12.8 / 16.0 / 19.2 / 25.6 / 28.8 / 32.0 / 38.4 s).
- **Interrupts:** the stock North Star DOS is **not** interrupt-driven. The board can raise an
  interrupt on **any** S-100 vectored-interrupt / `PINT` line (jumper at the lower-left corner)
  on **every sector pulse** while armed. **⚠ Interrupts during a transfer corrupt data** — the
  stock software runs with interrupts disabled, and both errata sheets warn that a POLY-88 with
  the 4.0 monitor's continuous RTC interrupt must have that interrupt disconnected to use the
  disk at all.

---

## 6. Emulation notes and gotchas

- **This is a memory device, not a port device.** Unlike the 88-DCDD, 88-MDS, VersaFloppy,
  Cromemco FDC, and CompuPro Disk 1 — all `IN`/`OUT` port controllers — the North Star MDS
  decodes on the **CPU memory-read path** across a 1 K window (standard `E800H`), and the low
  address bits carry the data/command. Model it as a memory-mapped region that side-effects on
  read. This also means the on-board bootstrap PROM is simply the low 256 bytes of that window.
- **Hard-sectored media** (10 sector holes + index), same family as the 88-MDS minidisk and the
  88-DCDD — see [[altairsim-88mds-minidisk]] and [[hard-sector-blank-disks]] for the sim's
  hard-sector drive base and growable-image handling.
- **CPU wait-state (`PRDY`) transfer**, not DRQ polling and not DMA — the same stall-the-CPU
  pattern the VersaFloppy uses on its data port; see [[altairsim-versafloppy-board]]. A read of
  the write-data case while the shift register is busy, or of the read case before the register
  fills, must hold the CPU until ready.
- **The check byte is a rotate-left XOR, not a CRC** — anyone porting a generic FDC verifier will
  compute the wrong value. Re-derive it exactly: `chk = 0; for b in data: chk = rol8(chk ^ b)`.
- **The two boards' command interfaces are not interchangeable.** The address-case meanings, the
  status-byte layouts, drive-select encoding (binary `M1M0` vs one-hot `DS`), and where motor-on
  and interrupt-arm live all differ (§1). A single decoder cannot serve both without branching on
  board type.
- **Standard origin `E800H`.** The bootstrap is entered by jumping to `E800H`; non-standard PROM
  origins move both the PROM and the whole 1 K window (`xx` in a PROM label is the two high hex
  digits of the origin).
- **CPU-agnostic bootstrap.** The on-board PROM holds 8080/Z80 machine code and the board is
  explicitly specified for both; nothing here assumes one CPU — relevant when driven from the
  sim's shared 8080/Z80 core ([[altairsim-z80-isa-next]]).

---

## 7. Key facts at a glance

| | MDS-A | MDS-A-D |
|---|---|---|
| Density | FM (single) | FM + MFM (single/double) |
| Geometry | 35 trk × 10 sec × 256 B | 35 trk × 10 sec × 256/512 B |
| Capacity/diskette | ≈ 89.6 KB | 179.2 KB (DD) |
| Drives | up to 4 | up to 4 |
| Interface | 1 K memory window, command-by-read | same |
| Standard origin | `E800H` | `E800H` |
| Controller clock | 4 MHz xtal ÷2 = 2 MHz | 4 MHz xtal ÷2 = 2 MHz |
| Transfer | CPU wait-state (`PRDY`) | CPU wait-state (`PRDY`) |
| Sync char | `FB` ×1 | `FB` ×1 (SD) / ×2 (DD) |
| Check | rotate-left XOR | rotate-left XOR |
| Rotation | 300 RPM, 20 ms/sector | 300 RPM, 20 ms/sector |
| Status bytes | A, B | A, B, C |
| Interrupts | optional per-sector-pulse (any VI/PINT line), off by default | same, + arm/disarm command codes |
