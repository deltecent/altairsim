# Cromemco 64KZ / 64KZ-II RAM

Source: [Cromemco 64KZ RAM Manual.pdf](#) (© Cromemco 1979, 1981; this printing May 1981),
[Cromemco 64KZ-II RAM Manual.pdf](#) (© Cromemco 1982; November 1982)

The 64KZ and 64KZ-II are S-100 / IEEE-696 dynamic-RAM boards built around the TMS 4116-15
16K×1 DRAM (250 ns access, no wait states at 4 MHz). Each board is two independent 32 Kbyte
halves — **BLOCK A** and **BLOCK B** — and each half is switch-mapped into some subset of eight
software-selected **memory BANKs** (BANK 0–7) addressed through one S-100 OUT port. This file
is a distilled emulation reference written from **scanned** manuals (no text layer; read as page
images); it omits parts lists, installation photos, and the gate-level schematics. It covers
both boards as one family, with the deltas called out.

The payload for an emulator: **the BANK SELECT byte written to port 40H is a bit-mask, not a
bank number.** Bit *N* controls BANK *N*, a logic 1 activates that bank and a logic 0
deactivates it, and *several banks can be active at once*. This is a fundamentally different
mechanism from a one-hot "select bank *N*" or a binary "bank = *N*" scheme.

---

## 1. What differs between 64KZ and 64KZ-II

| | 64KZ | 64KZ-II |
|---|---|---|
| Manual / part no. | May 1981, P/N 023-0008 (© 1979, 1981) | Nov 1982, P/N 023-2020 (© 1982) |
| Capacity per board | 64 Kbytes (2 × 32K blocks) | 64 Kbytes (2 × 32K blocks) |
| Banks per port | 8 (BANK 0–7) | 8 (BANK 0–7) |
| Documented system max | 512 Kbytes (8 banks × 64K) on one port; a 16-board/1.024 MB example uses a *second* PROM-remapped port | "Two to seven 64KZ-IIs" → **max 448 Kbytes** for a Z-80 Cromix system (six users + the OS in bank 0) |
| Bank-select port | 40H (standard) | 40H (standard) |
| Alternate port (PROM) | 74S288 at IC60 permits **40H–4FH and C0H–CFH** (32 addresses) | PROM at IC7 permits **41H–4FH** only; ⚠ no C0H–CFH range is mentioned for the -II |
| DMA support | Explicit OVERRIDE / DMA-IN-OUT switches in SW1; full DMA + refresh theory-of-operation | ⚠ **Not mentioned anywhere** in the -II manual — no DMA/OVERRIDE switches, no refresh-during-DMA text (unconfirmed whether dropped or just undocumented) |
| Block-enable granularity | Whole 32K BLOCK A/B (via RESET IN/OUT) | SW3 adds independent **Lower-16K / Upper-16K** enables per block |
| A15 address-half switch | In SW1 ("A15 A", "A15 B") | In SW3 ("Block A/B Addresses") |
| MEMDSBL phantom jumper | Yes (S-100 pin 67) | Not mentioned |

---

## 2. Bank-select port and the BANK SELECT control byte (the payload)

Both boards decode an 8-bit OUT port (standard **40H**) whose data byte is a **bit-mask of
active banks**:

| Bit | Bank controlled |
|:---:|:---:|
| 0 (LSB) | BANK 0 |
| 1 | BANK 1 |
| 2 | BANK 2 |
| 3 | BANK 3 |
| 4 | BANK 4 |
| 5 | BANK 5 |
| 6 | BANK 6 |
| 7 (MSB) | BANK 7 |

Verbatim (64KZ p. 17): *"Each bit of the control word manages one memory BANK. BIT 0 (LSB)
controls BANK 0, BIT 1 controls BANK 1, and so on. Outputting a logic 1 control word bit
activates its corresponding memory BANK; outputting a logic 0 control word bit deactivates its
memory BANK."* The 64KZ-II states the same model in its own words (p. 11–12) but gives no
numeric example.

- The whole byte applies atomically: on `OUT 40H,A` every bank set in the new byte turns on and
  every bank clear turns off, "**in unison** during the last machine cycle of the OUT 40H,A
  instruction" (64KZ p. 20).
- A BLOCK is switch-mapped, at setup time, into a fixed subset of the eight banks (§4). The
  mask then turns whole *banks* on/off; a BLOCK becomes electrically enabled whenever a bank it
  belongs to is activated (and its BANK ACTIVE LED lights).

### Worked examples (64KZ pp. 18–20, verified against the scan)

| Instruction | Byte | Banks activated |
|---|:---:|---|
| `LD A,00101000B` / `OUT 40H,A` | 28H | BANK 3 **and** BANK 5 |
| `LD A,01001000B` / `OUT 40H,A` | 48H | BANK 3 **and** BANK 6 |
| `LD A,00000001B` / `OUT 40H,A` | 01H | BANK 0 only |
| `LD A,00100000B` / `OUT 40H,A` | 20H | BANK 5 only |

⚠ **Program-continuity trap** (64KZ p. 20): the CPU is ignorant of bank boundaries — after the
`OUT` it just fetches the next sequential address. The 20H example activates a bank that
contains *no* running block, so the CPU then reads the floating bus as `0FFH` and executes
`RST 38H`; "these instructions would not be executed in actual practice." The safe pattern is
to keep one read/write module common to all activated banks so the stack and code survive the
switch.

⚠ **Bus-fight trap** (64KZ Examples 5–6): two BLOCKs (or a BLOCK and another Cromemco bank-select
board) switch-mapped into the *same* bank and the *same* 16-bit address range will both drive
DI0–DI7 if that bank is activated — garbage reads, no damage. Even *disjoint* banks conflict if
both are activated while their address ranges overlap.

---

## 3. Bank-select port addressing

Standard **40H** on both boards; every board on that port responds to the same broadcast BANK
SELECT byte (64KZ p. 15). A PROM makes the port relocatable: on the 64KZ a 74S288 at IC60 yields
32 legal addresses (40H–4FH, C0H–CFH); on the 64KZ-II a PROM at IC7 yields 41H–4FH (no C0H–CFH
mentioned). "There is no PROM in standard boards." Multiple ports can coexist, each managing its
own eight banks — the 64KZ manual's 16-board example uses two ports for 1.024 MB (p. 20).

---

## 4. Switch groups

### 4.1 64KZ SW1 — "Addr/Control" (8 switches, both blocks)

| Switch | 0 (OUT) | 1 (IN) |
|---|---|---|
| DMA A | IN | OUT |
| OVERRIDE A | DISABLED | ENABLED |
| RESET A | OUT | IN |
| DMA B | IN | OUT |
| OVERRIDE B | DISABLED | ENABLED |
| RESET B | OUT | IN |
| A15 B | 0 | 1 |
| A15 A | 0 | 1 |

- **A15 = 0** maps the block into 0000H–7FFFH; **A15 = 1** into 8000H–FFFFH.
- **RESET = OUT** unconditionally *disables* the block after RESET / Power-On-Clear regardless of
  bank status; **RESET = IN** unconditionally *enables* it (§4.3).
- **OVERRIDE / DMA** govern DMA response only: OVERRIDE DISABLED → DMA obeys bank boundaries like
  normal access; OVERRIDE ENABLED → bank boundaries collapse during DMA, the block enabling for
  DMA when `DMA = IN` and disabling when `DMA = OUT`.

### 4.2 64KZ SW2 / SW3 — block bank membership

SW2 = BLOCK A, SW3 = BLOCK B; eight switches each (BANK 0–7), `IN` = member of that bank / `OUT`
= not a member. A block may be a member of none, one, several, or all eight banks. Only the
switch state at the instant `OUT 40H,A` executes matters.

### 4.3 64KZ power-on / reset

Immediately after POC / RESET **there are no active banks** — bank membership is irrelevant at
that instant. Block active/inactive is set **solely by the RESET A/B switches** for non-DMA
access, until the CPU executes an `OUT 40H,A` (64KZ Fig. 10, p. 13).

### 4.4 64KZ-II SW1 / SW2 / SW3

SW1 = BLOCK A bank select, SW2 = BLOCK B bank select (BANK 0–7, Out/In), same shape as the
64KZ's SW2/SW3. SW3 = "Address/Control":

| Switch | OFF | ON |
|---|---|---|
| Block A Lower 16K Array | Enabled | Disabled |
| Block A Upper 16K Array | Enabled | Disabled |
| Block A Reset | Disabled | Enabled |
| Block B Lower 16K Array | Enabled | Disabled |
| Block B Upper 16K Array | Enabled | Disabled |
| Block B Reset | Disabled | Enabled |
| Block B Addresses | 0000H–7FFFH | 8000H–FFFFH |
| Block A Addresses | 0000H–7FFFH | 8000H–FFFFH |

⚠ The -II adds independent Lower-16K / Upper-16K enables the 64KZ lacks. ⚠ The -II manual gives
no explicit "no banks active after POC" prose — infer reset behavior from the per-block Reset
switch (ON = enabled after reset).

---

## 5. DMA and refresh (64KZ only — undocumented for the -II)

A bus master takes control via pHOLD/pHLDA and the S-100 DSBL lines (see the 4FDC/16FDC/64FDC
reference for the general protocol). The 64KZ provides transparent refresh during M1 cycles and
autonomous refresh during extended WAIT states, but **no refresh during DMA** — a DMA burst
over ~1 ms (or bursts spaced under ~1 ms) requires the DMA controller to refresh itself, e.g. by
touching one 128-byte half-page every 2 ms (64KZ pp. 22–25). ⚠ The 64KZ-II manual contains no
DMA discussion at all; whether the feature was dropped or merely undocumented is unconfirmed.

---

## 6. Jumper / phantom (64KZ)

`MEMDSBL` on S-100 **pin 67**, when driven active-low, unconditionally disables the whole board
(hardware phantom-memory support). Factory-shipped closed; cut the solder-side trace to remove
it if pin 67 is used otherwise (64KZ p. 27). No equivalent is documented for the -II.
