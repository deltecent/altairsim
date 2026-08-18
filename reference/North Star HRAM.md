# North Star HRAM

Source: [North Star 64k HRAM.pdf](#) (© North Star Computers, 1981)

The HRAM (HORIZON Random Access Memory) is North Star Computers' bank-switched dynamic-RAM
board for the HORIZON S-100 computer, shipped in three capacities — HRAM-32 (32K), HRAM-48
(48K), and HRAM-64 (64K) — all sharing one bank-switching and parity-checking design. This
file is a distilled emulation reference produced from a **scanned** manual (no text layer; read
as page images). Installation mechanics, static-handling warnings, the RAM-chip location
charts, the parts list, and the schematics are omitted as not software-visible. **What this
file adds is the exact bit-level encoding of the bank-control byte written to I/O port C0H** —
the polarity of the on/off bit, the one-hot bank-select mapping, and the trap where that same
bit means opposite things for bank-switching vs. parity-arming.

The most important fact for an emulator: **the HRAM byte is not a bank number.** Bit 0 is an
on/off command and bits 1–7 are a one-hot address of *which* bank the command acts on. Banks
are toggled individually — software must switch the old bank OFF before switching the new bank
ON — not selected by writing a number.

---

## 1. Board identity and capacity

| Item | Value | Source |
|---|---|---|
| Versions | HRAM-32 (32K), HRAM-48 (48K), HRAM-64 (64K) | p. 1-1 |
| Bits per byte | 8 data + 1 odd-parity bit | p. 1-3, p. 5-1 |
| Access time | 300 ns typical | p. 1-3 |
| RAM device | 4116 (16K×1 DRAM) | p. 5-1 |
| Board revisions | B, D, E (D/E add OCCLUDE-gated S2 behavior; E adds JP4) | p. 3-1, p. 3-22, p. 5-4 |

The manual states no maximum number of boards or fixed total-system-RAM figure in the pages
read; the worked examples go up to four boards / four banks (§3.1).

---

## 2. Two independently-switchable 32K sections

For bank switching each board is logically split into two 32K halves (p. 3-7):

| Section | Address range |
|---|---|
| Lower half | 0000H–7FFFH |
| Upper half | 8000H–FFFFH |

Jumper area **JP2** configures how the halves participate (Table 3-1, p. 3-8):

| JP2 configuration | Effect |
|---|---|
| Both halves "always on" | Bank switching disabled on this board |
| Both halves "switchable" | Both 32K halves switch together as one bank |
| Lower switchable, upper always-on | Only 0000H–7FFFH is bank switched |
| Upper switchable, lower always-on | Only 8000H–FFFFH is bank switched |

A single 64K board's two halves cannot be assigned to two *different* banks — "A 64K cannot be
divided into two different 32K banks" (p. 3-10). Two 32K boards can be combined into one bank
by jumpering both to the same I/O bit (§3).

---

## 3. Bank-control I/O port and byte encoding — the payload

**Port C0H** (out; the Port-C0 Detector matches address bits A0–A7 == C0H, p. 5-4).

### 3.1 Bit layout of the byte written to port C0H

| Bit | Role |
|:---:|---|
| 0 | On/off command: **0 = turn the addressed bank ON, 1 = turn it OFF** (p. 3-10) |
| 1–7 | **One-hot** address of which bank the command targets — exactly one of bits 1–7 is set, chosen per board by its JP1 jumper (Figure 3-6, p. 3-9) |

Each board's JP1 jumper connects a per-board "B" pin to **one** of pins 1–7, selecting which
I/O bit controls *that board's* bank state (p. 3-9). Boards forming different banks use
different bits; boards meant to combine into one bank use the *same* bit (p. 3-10).

⚠ **Only six of the seven bit positions are usable for banks.** One of bits 5, 6, or 7 is
reserved (a separate JP1 "P" jumper) to arm/disarm parity interrupts (§5), "leaving a total of
six bits that can be used for bank switching … a maximum of six memory banks" (p. 3-9).

### 3.2 Software protocol (verified against the scan, p. 3-10)

```
MVI A,08H   ; Turn ON  bank 3   (bit3=1, bit0=0)
OUT 0C0H

MVI A,09H   ; Turn OFF bank 3   (bit3=1, bit0=1)
OUT 0C0H
```

"Note that bit 0 is used to specify turning the bank on or off." For bank 5 the operands become
20H (on) / 21H (off) — the addressed bit stays set, only bit 0 toggles.

⚠ **Software must switch the OLD bank off before switching the NEW bank on** (p. 3-11): "Take
care to allow only one bank to be on at a time … the previous bank must be switched off before
the next bank is switched on." A bank that is off keeps being refreshed but neither accepts nor
supplies data (p. 3-7).

### 3.3 Underlying flip-flop semantics (Theory of Operation, p. 5-5)

Bit 0 is uniformly a SET(1)/RESET(0) pulse to whichever bits-1–7 flip-flop it addressed, but
"SET" means opposite real-world states for the two functions: on a *bank* flip-flop, `OCCLUDE`
high = bank OFF (so bit0=1 = off); on the *parity* flip-flop, `PARITY ARM` high = parity ON
(so bit0=1 = armed). ⚠ An implementer routing both functions through one "bit 0" helper must
not assume one polarity serves both — see §5.

---

## 4. Bank status on reset/power-up (JP1)

JP1 also selects a board's bank state when the POC-derived reset signal fires (pp. 3-11–3-12):

| JP1 reset position | Effect | Figure |
|---|---|---|
| **As-shipped default** | Bank always remains ON | Fig. 3-7 |
| "Enable bank on reset" | This bank turns ON at power-up/reset | Fig. 3-8 |
| "Disable bank on reset" | This bank turns OFF at power-up/reset | Fig. 3-9 |

To use multi-bank operation, exactly one bank's board(s) are set "enable on reset" and all
others "disable on reset" (p. 3-12) — only one bank active at reset.

---

## 5. Parity checking

- **I/O control bit** (JP1): parity arm/disarm goes through port C0H on a bit chosen from 5, 6,
  or 7. Shipped default **bit 6** (p. 3-20).
- **Interrupt routing** (JP3): VI0–VI7 / PINT / NMI, or disabled. Shipped default **VI5** ("the
  North Star standard required by DOS", p. 3-21).
- **Software (bit 6 example, p. 3-22):** `MVI A,41H` (bit6=1, bit0=1) = clear errors + **ARM**;
  `MVI A,40H` (bit6=1, bit0=0) = clear errors + **DISARM**. ⚠ Bit 0 = 1 arms, 0 disarms — the
  reverse polarity of the bank on/off convention (see §3.3).

---

## 6. Memory-address switches (S1/S2)

Independent of bank switching, the address decoder is told which of the 64K space this board
occupies:

- **S1** (8 switches): one per 8K block (0000/2000/4000/6000/8000/A000/C000/E000H). A block
  responds only if its S1 switch is on **and** its bank is currently on (pp. 3-13, 3-15).
- **S2** (8 switches): governs the **last 8K block** (E000H–FFFFH) as an alternative to S1 (if
  S2 controls it, S1's "E" switch must be off, and vice-versa) (pp. 3-14, 3-16).
  - **Rev B**: each S2 switch independently gates a 1K sub-block, on/off, **not** tied to the
    bank flip-flop (p. 3-14).
  - **Rev D/E**: an `OCCLUDE` signal lets S2 switches work in **pairs** per 2K sub-block with
    three states — always off / always on / on-only-when-bank-on (Table 3-2, p. 3-17).
- ⚠ **HRAM-32 alias trap**: the 32K board does not decode A15, so each RAM row answers two
  address ranges; do not enable both aliasing S1 switches at once (0&8, 2&A, 4&C, 6&E)
  (p. 3-18).

---

## 7. First Quadrant option (revision E, 48K boards)

JP4 (rev E only) relocates a 48K board's window (p. 3-19): standard = 0000H–BFFFH, alternate =
4000H–FFFFH. It does not interact with the bank-select or parity encodings above.
