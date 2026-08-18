# Vector Graphic 64K Dynamic RAM

Source: [64K DRAM Manual.pdf](#) (© Vector Graphic, 1979)

The Vector Graphic 64K Dynamic Memory Board is an S-100 dynamic-RAM board for **Z-80 systems
only** (it is explicitly not usable in an 8080-based S-100 system — it relies on the Z-80's
built-in refresh-address generation). It provides up to 65,536 bytes of RAM per board, and up
to **eight** boards may be bank-switched into one system for up to 512K total. This file is a
distilled emulation reference written from a **scanned** copy of the manual (no text layer;
read as page images) and keeps only what an emulator needs to model the board's
software-visible behavior: the bank-select I/O port, the exact byte encoding written to it,
the address-space jumper options, and power-on/reset behavior. Omitted: kit/board assembly,
the parts list, the schematic itself, and the DRAM refresh timing/circuitry (RAS/CAS
sequencing, the refresh-interval electronics) — none of that is software-visible.

---

## 1. Bank-select port (the payload)

Software selects which one of up to eight installed 64K boards is currently mapped into the
address space by writing a byte to **port 40H** (p. 11, p. 20). This port must not be used for
any other purpose in a system that has a 64K board installed (p. 13, p. 20).

The encoding is **one-hot**: bit *N* of the byte corresponds to bank number *N*.

| Bank number | Byte written to port 40H |
|:---:|:---:|
| 0 | 01H |
| 1 | 02H |
| 2 | 04H |
| 3 | 08H |
| 4 | 10H |
| 5 | 20H |
| 6 | 40H |
| 7 | 80H |

Worked example from the manual: to select the board set to bank 5, `MVI A,20H` / `OUT 40H`
(p. 21).

Each board is configured to answer to exactly one bank number via an on-board DIP switch (§4).
The hardware compares the latched port-40H byte against that switch setting; a board asserts
itself onto the bus only when the bit corresponding to its own switch position is a 1 in the
byte just written (p. 25, theory of operation §3.3). Only one board drives the bus at a time,
but **all installed boards are refreshed regardless of which is selected** — switching banks
does not lose data in the unselected boards (p. 13, p. 20).

---

## 2. Bank count, board count, total RAM

- Bank numbers run **0–7** (3 bits), so up to **8** boards may coexist in one system (p. 13).
- Each board contributes up to 65,536 bytes, so a fully populated system has up to **512K**
  (8 × 64K) (p. 13, p. 20).
- A "bank" is a full board's worth of address space — up to 64K per bank, not a smaller fixed
  granule (§3 covers how much of that 64K is actually mapped on a given board).
- The Vector Graphic 16K Static RAM board "has exactly the same bank selecting capability," so
  16K static and 64K dynamic boards can be mixed as long as every board present has a distinct
  bank number (p. 14, p. 21).

---

## 3. Address-space trimming (jumper area D)

A single board can be jumpered to occupy less than the full 64K, to leave room at the top of
memory for ROM/monitor and other boards. The board always starts at 0000H; jumper area D trims
from the top in 8K increments (p. 11, p. 19):

| Option | Addresses enabled | Jumper (area D) |
|---|---|---|
| 64K | 0000H–FFFFH | 1 to 3 |
| 56K | 0000H–DFFFH | 5 to 3 |
| 48K + 8K at E000 | 0000H–BFFFH and E000H–FFFFH | 4 to 3 |
| 48K | 0000H–BFFFH | 2 to 3 |

⚠ **As shipped, the board is jumpered for the 56K option** ("E000H to FFFFH disabled; thus
0000H to DFFFH enabled", p. 11), not full 64K. A board named "64K Dynamic RAM" does not map a
full 64K out of the box.

---

## 4. Bank-select switch (S1) and board identity

Each board carries an 8-position DIP switch, **S1**, used to assign that board's bank number
(p. 20). Ignore the numbers printed on the switch package; use the numbers silkscreened on the
board. All positions start OPEN; to assign bank *N*, close only the rocker for position *N* and
leave the other seven OPEN — exactly one rocker closed per board. As shipped, a board answers
**bank 0** (position-0 rocker closed) (p. 11). The scan does not describe the behavior of two
boards set to the same bank, or of more than one rocker closed on one board.

---

## 5. Power-on / RESET behavior

⚠ Load-bearing for correctness: **the board set to bank 0 is always force-enabled by POC
(Power-On-Clear), independent of the last byte written to port 40H** (p. 11, p. 21):

> "The 64K board with bank number 0 is always enabled when the system is turned on or the RESET
> switch (or other source of Power-On-Clear) is depressed."

Mechanism (theory of operation, p. 25 §3.3): the port-40H byte is latched into two quad
latches whose CLEAR pins are wired to POC, so reset zeroes the latch outputs. Data line DO0 is
inverted ahead of one latch specifically so that the cleared state is electrically equivalent
to a 1 in bit 0 — i.e. to the byte **01H** (bank 0's one-hot code) having been written. That is
why bank 0, and only bank 0, is the reset-selected bank. Practical consequence stated in the
spec table: in a single-board system whose board is left at bank 0, no `OUT 40H` is needed at
all (p. 11, p. 21).

---

## 6. Compatibility notes relevant to emulation

- Z-80 S-100 only; not 8080-compatible (relies on Z-80 M1-cycle refresh) (p. 13).
- Rated for Z-80 at 4 MHz, no wait states; 65,536 bytes per board (p. 11).
- ⚠ Holding the CPU (DMA/long wait states) for ≥2 ms without refresh loses memory contents
  (pp. 18–19, 24) — a real hazard for an emulator that halts refresh bookkeeping, though the
  refresh electronics themselves are out of scope here. Board-level RFSH/MWRITE/RESET-pulse
  jumpers (areas A/B/C, pp. 15–18) are not software-visible and are omitted.
