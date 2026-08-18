# SD Systems ExpandoRAM

Source: [SD Systems ExpandoRAM I.pdf](#) (© SD Sales / SD Systems Company, 1977)

The ExpandoRAM is an S-100 dynamic-RAM expansion board, populated with Mostek MK4115 (8,192×1)
or MK4116 (16,384×1) DRAMs, sold as a bare-board kit interfacing to the IMSAI, Altair A/B, SOL-8,
Cromemco, and SD Sales Z80 CPU boards (Poly-88 needs a hardware mod). This file is a distilled
emulation reference written from a **scanned** manual with no text layer (read as page images).
Omitted: assembly/soldering, the BOM, and the ECN-1 rework. Added: an explicit statement of the
board's memory-mapping model, because "bank" here is easy to confuse with the *ExpandoRAM-II*'s
port-driven scheme — **they are unrelated designs**. This is the original ExpandoRAM ("I"); the
[ExpandoRAM-II](SD%20Systems%20ExpandoRAM%20II.md) is a separate board with its own reference.

---

## 1. Verdict: no bank-select I/O port exists

**The ExpandoRAM I has no bank-select port, and none of its RAM is bank-switched at runtime.**
There is no I/O port anywhere in the design — Table 1-2's full J1 pinout (p. 3) lists only
memory-bus signals (address/data, `SMEMR`/`MWRT`/`SM1`/`RFSH`, `PSYNC`/`PDBIN`, `PHOLDA`/`P WAIT`,
`RESET`, `HALTA`); there is no `sINP`/`sOUT` or device-select line, so the board cannot decode an
`OUT` instruction at all. The Introduction confirms the model: *"An eight position DIP Switch is
provided for positioning memory on any 8K or 16K boundary"* (p. 1).

What the manual calls a "bank" is a **fixed group of 8 DRAM chips** wired, once, to a fixed slice
of the 16-bit address space by an 8-position address DIP switch (U2) feeding a 74LS138 3-to-8
decoder (U7) that asserts one of four `RAS0–3` lines (pp. 12–13, 20). All four banks are present
in the map simultaneously, each at its own fixed range — nothing swaps in or out during
execution. There is **no on-board PROM** (unlike the II); the decode is pure combinational logic.

⚠ An implementer modeling this board must **not** reuse a bank-switching mechanism. It needs a
static address-decode model (like any switch-based S-100 memory board), plus per-bank
write-protect and output-disable as below.

---

## 2. Board identity and capacity

| Item | Value | Source |
|---|---|---|
| DRAM (32K board) | MK4115 (8,192×1); MK4115-40 or -41, cannot mix on one board | p. 1, 11 |
| DRAM (64K board) | MK4116 (16,384×1) | p. 1, 12 |
| Chips per bank | 8 (one bit each) | p. 4 |
| Number of banks | 4 | p. 4 |
| Max capacity, MK4115 | 32,768 bytes (partial: 8K/16K/24K/32K) | p. 1–2 |
| Max capacity, MK4116 | 65,536 bytes (partial: 16K/32K/48K/64K) | p. 1–2 |
| Access / cycle | 375 ns access, 500 ns cycle | p. 2 |

Partial population is bank-by-bank (8 chips at a time) (p. 9).

---

## 3. Address-mapping DIP switch (U2) — the static substitute for a bank port

An 8-position DIP switch (U2) sets which fixed address range each bank occupies, decoded through
the 74LS138 into `RAS0`–`RAS3`. Two layouts, per DRAM type:

**32K board (MK4115), Fig. 4-3 (p. 12):**

| Switch | Range | Bank |
|:---:|---|:---:|
| 1 | 0000–1FFF | 0 |
| 2 | 2000–3FFF | 1 |
| 3 | 4000–5FFF | 2 |
| 4 | 6000–7FFF | 3 |
| 5 | 8000–9FFF | 0 |
| 6 | A000–BFFF | 1 |
| 7 | C000–DFFF | 2 |
| 8 | E000–FFFF | 3 |

**64K board (MK4116), Fig. 4-4 (p. 13):** only switches 1–4 used (5–8 off):

| Switch | Range | Bank |
|:---:|---|:---:|
| 1 | 0000–3FFF | 0 |
| 2 | 4000–7FFF | 1 |
| 3 | 8000–BFFF | 2 |
| 4 | C000–FFFF | 3 |

This is a static, configuration-time address decode — analogous to any S-100 memory board's
base-address strap, not a software-driven bank register.

---

## 4. Bank-selectable write protect / output disable

A 5-position DIP switch (U1) controls per-bank write protection (Fig. 4-5, p. 13):

| Position | Function | OFF | ON |
|:---:|---|---|---|
| 1–4 | Bank 0–3 write protect | Protected | Unprotected |
| 5 | Manual output disable (only if PHANTOM jumper not selected) | Enable | Disable |

PHANTOM OUTPUT DISABLE is jumper-selectable (Fig. 4-7, p. 14): the board's output is gated either
by the S-100 `PHANTOM` line (J1-67) or by switch position 5 — never both.

---

## 5. DMA and reset

- DMA is jumper-enabled (E13–E14), 8080/Z80 only (not 8085); bursts limited to ≤ 1 ms for refresh
  (p. 14). With DMA disabled the board still refreshes during other boards' DMA cycles.
- `RESET` (J1-75) only feeds an RC debounce into the control logic (p. 20). **Because bank mapping
  is static DIP-switch hardware, there is nothing for RESET to affect regarding bank selection,
  and no power-on default bank state exists** — none is described because none is needed.
