# Cromemco PRI Printer Interface

Source: [PRI Printer Interface.pdf](#) (© Cromemco 1978, 1980)

The **Cromemco PRI** ("Printer Interface") is an S-100 board that drives **two independent
printers at once**: a Cromemco **3703/3779** dot-matrix (Centronics-style parallel) printer
through connector J1, and a Cromemco **3355A** fully-formed-character (daisy-wheel) printer
through connector J2. Each printer has its own port block, its own data-strobe/handshake
protocol, and its own interrupt source; the two halves of the board otherwise share nothing
but power and the interrupt-priority chain. The board also automatically raises and lowers the
3355A's print ribbon around a burst of characters, and supports Z-80/8080 vectored interrupts
via the same TU-ART-style INTA gating used elsewhere in the Cromemco line.

This is a distilled emulation reference: it keeps the programmer-visible port map, the two
status/control byte layouts (with polarity), the data-strobe handshakes, the interrupt-enable
bits and vector-byte straps, and the automatic ribbon-lift timing. Connector pin numbers are
kept only insofar as the manual's own port tables key off them; the schematic-level theory of
IC-by-IC operation, the parts list, and the mechanical/cable drawings are summarized only where
they explain a signal's behavior.

---

## 1. Overview — two printer channels, one board

| | Dot-matrix channel (3703/3779) | Fully-formed channel (3355A) |
|---|---|---|
| Connector | J1 | J2 |
| Cable | Cromemco TU-ART cable (TRT-CBL), EIA DB25 | same TU-ART cable, EIA DB25 |
| Data path | 7-bit ASCII (no parity) + a strobe | full data byte + an 11-bit carriage/paper-feed value + direction, all strobed |
| Handshake | printer-driven BUSY / ACKNLG | printer-driven IN BUFFER READY / PRINTER READY / CHECK / PAPER OUT / RIBBON OUT |
| Signal polarity | **active HIGH**, except DATA STROBE and ACKNLG STROBE (active LOW pulses) | **all lines active LOW** |
| Default I/O base | 54H (data+status), 53H (interrupt enable) | 5AH/5BH (data), 5CH (control), 5AH (status), 5DH (interrupt enable) |
| Ribbon control | n/a | automatic (board-timed) |

(Introduction, p.1; Port Assignments, pp.2–4.)

The two channels are otherwise unrelated — a system with only one printer uses only the
matching half of the board and can ignore the other port block entirely.

---

## 2. Port map

| Port | Direction | Channel | Contents |
|:---:|:---:|---|---|
| **53H** | OUT | dot-matrix | Interrupt enable (bit 2) |
| **54H** | OUT | dot-matrix | Data 0–6 (bits 0–6) + DATA STROBE (bit 7) |
| **54H** | IN | dot-matrix | Status: BUSY (bit 5), ACKNLG (bit 7) |
| **5AH** | OUT | fully-formed | Data 0–7 (low byte of the 12-bit carriage/paper-feed/character word) |
| **5AH** | IN | fully-formed | Status: IN BUFFER READY (bit 0), CHECK (bit 1), PAPER OUT (bit 2), RIBBON OUT (bit 3), PRINTER READY (bit 4) |
| **5BH** | OUT | fully-formed | Data 8–11 (high nibble, bits 0–3) |
| **5CH** | OUT | fully-formed | Control/strobe byte (§5) |
| **5DH** | OUT | fully-formed | Interrupt enable (bit 2) |

(Technical Specifications, p.1: "Input Ports 54,5A / Output Ports 53,54,5A,5B,5C,5D"; Port
Assignments tables, pp.2–4.)

**Base address.** The factory default upper nibble (A7–A4) is **05H** — giving the 53/54/5A–5D
block above. It is changed by cutting two PC traces (the A4 and A6 runs) and soldering a 4-pole
DIP switch across the four holes marked A7/A6/A5/A4 near IC5; the lower nibble (A0–A3) is fixed
by the on-board decode (IC16/IC28) to 3H, 4H, AH, BH, CH, DH and cannot be relocated. Example:
all four switches ON → base F0H gives ports F3H/F4H/FAH/FBH/FCH/FDH; only switch A4 ON → base
10H gives 13H/14H/1AH/1BH/1CH/1DH. (Features — "Changing the Port Assignments", p.5.)

---

## 3. Dot-matrix channel (3703/3779) — status, port 54H IN

Reads active-HIGH except where the manual's own overline marks a bit active-LOW; the manual
states the general rule directly: **"ALL LINES ARE TTL LEVEL ACTIVE HIGH except DATA STROBE
and ACKNLG STROBE."**

| Bit | Flag | Polarity | Meaning |
|:---:|------|----------|---------|
| 5 | **BUSY** | active-high | Printer is not ready to accept input (the print head or carriage is moving). |
| 7 | **ACKNLG** (strobe) | **active-low** pulse | The printer pulses this line low to signal that BUSY has gone low and it is ready for the next character. On the PRI this also clocks the interrupt flip-flop when interrupts are enabled. |

Bits not listed are unused/undefined on this channel. (Port Assignments — Model 3779/3703, p.2,
notes 4–5.)

---

## 4. Dot-matrix channel — data + strobe, port 54H OUT

| Bit | Meaning |
|:---:|---------|
| 0–6 | 7-bit ASCII character (no parity bit) |
| 7 | **DATA STROBE** — active-low pulse |

Writing this port latches the 7-bit ASCII code onto the data lines and pulses DATA STROBE low
to tell the printer the byte is ready. (Notes 2–3, p.2.)

**Interrupt enable — port 53H OUT, bit 2.** Set (1) to let the PRI raise pINT when ACKNLG goes
low; clear (0) to disable. (Note 6, p.2; confirmed in Features/Installation, p.5: "bit 2 of
output port 53H must be set" — and the port table itself places "Interrupt Enable" in the Bit 2
row of the Output Port 53H column, p.2.)

---

## 5. Fully-formed channel (3355A) — data registers, ports 5AH/5BH OUT

The manual states **"ALL LINES ARE TTL ACTIVE LOW"** for this channel (note 2, p.3) — a set bit
in the tables below is the electrically-asserted (low) condition on the physical connector, but
the *data value* itself (character code, carriage position, paper-feed count) is an ordinary
positive binary number; only the *strobes and status flags* are active-low as signals.

| Port | Bits | Meaning |
|:---:|:---:|---|
| 5AH OUT | 0–7 | Data 0–7 — low byte of whichever 12-bit word is being sent (character code, or carriage/paper-feed position) |
| 5BH OUT | 0–3 | Data 8–11 — high nibble of the same 12-bit word |

A **12-bit** value spans the two ports (Data 0–7 at 5AH, Data 8–11 at 5BH):

- **Character strobe:** Data 0–7 is the ASCII code being printed (no parity); Data 8–11 unused.
- **Carriage strobe:** Data 0–10 is the carriage position; **Data 11** is direction — 0 = right,
  1 = left.
- **Paper feed strobe:** Data 0–10 is the feed increment; **Data 11** is direction — 0 = up
  (i.e., away from the operator / toward top-of-form), 1 = down.

(Port Assignments — Model 3355A, p.3, notes 4–6.)

---

## 6. Fully-formed channel — control register, port 5CH OUT

| Bit | Name | Effect |
|:---:|------|--------|
| 0 | **RESTORE** | Moves the carriage to the leftmost position, synchronizes the printwheel, and resets the printer's internal logic. |
| 1 | **CHAR STROBE** | The ASCII code on Data 0–7 (7 bits, no parity — bit 7 of the data word is not used for the character) is ready; triggers the print of that character. Also arms the automatic ribbon lift (§9). |
| 2 | **CARR STROBE** | The carriage-position value + direction on Data 0–10/11 is ready; moves the carriage. |
| 3 | **PAPER FEED** | The feed-increment value + direction on Data 0–10/11 is ready; feeds the paper. |
| 4 | **TOP OF FORM** | Advances the paper to its top-of-form starting point. |
| 5 | **PRINTER SELECT** | Selects the printer for operation. |
| 6–7 | — | Not used |

All are active-low strobes/commands on the physical connector per the channel-wide convention
(note 2, p.3); a driver simply sets the corresponding output-port bit and the board pulses the
matching connector pin low. (Port Assignments — Model 3355A, p.3, notes 3, 7–8.)

**Interrupt enable — port 5DH OUT, bit 2.** Set (1) to let the PRI raise pINT when IN BUFFER
READY goes low; clear (0) to disable. (Note 15, p.4; Features/Installation, p.5: "bit 2 of
output port 5DH must be set" — matching the table's placement of "Interrupt Enable" in the Bit
2 row of the Output Port 5DH column, p.4.)

---

## 7. Fully-formed channel — status, port 5AH IN

| Bit | Flag | Meaning |
|:---:|------|---------|
| 0 | **IN BUFFER READY** | Printer is ready to accept an input command. Drives the interrupt when enabled. |
| 1 | **CHECK** | Printer has malfunctioned. |
| 2 | **PAPER OUT** | Printer is out of paper. |
| 3 | **RIBBON OUT** | Printer is out of ribbon. |
| 4 | **PRINTER READY** | Printer is ready to accept data and control inputs. |
| 5–7 | — | Not used |

(Port Assignments — Model 3355A, p.4, notes 9–13.) Note 14 also documents connector pin 22 as
"ribbon position (0=up, 1=down)" but this is a hardware test point, not a bit visible at either
status port — "this function is automatically controlled by the PRI board" (§9).

---

## 8. Interrupt vector and priority chain

The PRI does not jam a fixed RST; like the Cromemco TU-ART family it gates the **low byte of
the interrupt vector address onto the bus during an INTA cycle**, and expects the CPU/monitor
software to have set up the corresponding vector-table entry (Z-80 mode 2) or RST opcode (8080
mode 0) at that address.

- **Fully-formed channel vector byte** — set by an 8-position DIP switch **"switch 1"**.
  Recommended value **5CH** for Cromemco's own interrupt software: positions 1, 3, 7, 8 **ON**;
  positions 2, 4, 5, 6 **OFF** (switch convention: **ON = 0, OFF = 1**, position 1 = MSB).
- **Dot-matrix channel vector byte** — set by DIP switch **"switch 2"**. Recommended value
  **34H**: positions 1, 2, 5, 7, 8 **ON**; positions 3, 4, 6 **OFF**.

The interrupt request line is pINT (S-100 pin 73), driven through an interrupt-enable flip-flop
(set by the port 53H/5DH bit-2 writes above) and a request flip-flop clocked by ACKNLG (dot
matrix, J1 pin 15) or IN BUFFER READY (fully-formed, J2 pin 18). **Device A/B-style priority**:
the PRI can be chained after other TU-ART-family boards using the same priority-jumper cable —
connect the *last* board's PRIORITY OUT to the PRI's **J3 IN** — and a PRIORITY IN gone low
blocks the PRI's own vector-gating flip-flop from answering the INTA, exactly like the TU-ART's
device-priority chain. (Features — "Interrupts", pp.5–6; Theory of Operation, pp.6–8.)

**An emulator that only needs a polled printer console can ignore all of the above** and drive
each channel from its status-register bits (BUSY/ACKNLG for dot-matrix; IN BUFFER READY /
PRINTER READY / CHECK / PAPER OUT / RIBBON OUT for fully-formed).

---

## 9. Automatic ribbon control (3355A only)

The PRI raises the 3355A's ribbon into printing position on its own, ahead of the character
actually reaching the print head: the ribbon-lift command is generated as soon as the board
sees a **CHAR STROBE** (port 5CH bit 1), and — to give the ribbon time to fully rise before the
printer accepts the character — the board **delays the CHARACTER STROBE signal it forwards to
the printer by 8 cycles of its onboard 2 MHz clock** (4 µs). If no further character strobe
arrives for **approximately one second**, the board automatically **lowers the ribbon** again.
This is entirely a board-side timer; no register exposes ribbon-lift state to software beyond
the RIBBON OUT status bit (out of ribbon, not lift position). (Features — "Automatic Ribbon
Control", p.5; Theory of Operation, p.7.)

---

## 10. Printer characteristics (context)

- **3703/3779** — Cromemco's dot-matrix printer family, driven Centronics-parallel-style: 7-bit
  ASCII data + strobe, BUSY/ACKNLG handshake. The manual does not describe the printer's own
  character set or mechanical specifics (that lives in the printer's own manual, not this
  interface manual) — *not in the manual*.
- **3355A** — a fully-formed (daisy-wheel) character printer with independently addressable
  carriage position and paper-feed position (not just "next column" / "next line" — the PRI
  sends an absolute 11-bit position + direction for each), a manual ribbon it otherwise does not
  raise/lower on its own, and its own PAPER OUT / RIBBON OUT / CHECK sensors reported back
  through the status port. Font/character-shape and daisywheel details are — *not in the
  manual*.

---

## 11. Emulation checklist

- **Two independent channels; implement whichever printer(s) the machine config wants.** They
  do not interact except by sharing the board's base-address strap and interrupt-priority chain.
- **Dot-matrix (54H/53H):** 7-bit ASCII + strobe out; BUSY (bit 5, active-high) / ACKNLG (bit 7,
  active-low pulse) in; DATA STROBE (bit 7 of the OUT byte) is itself active-low. Interrupt
  enable is port 53H bit 2.
- **Fully-formed (5AH/5BH/5CH/5DH):** all connector signals active-low; register *values*
  (character code, 11-bit carriage/feed position + direction bit) are ordinary positive binary.
  CHAR STROBE / CARR STROBE / PAPER FEED / RESTORE / TOP OF FORM / PRINTER SELECT are all
  distinct command bits in the **same** OUT byte (port 5CH) — a driver can, in principle, set
  more than one at once, though the manual's own examples strobe one command per write.
  Interrupt enable is port 5DH bit 2.
- **Default base is 05H** (ports 53/54/5A/5B/5C/5D); relocatable in 16-port-aligned steps by a
  DIP-switch hardware mod cutting two PCB traces — not runtime-configurable on real hardware,
  so an emulator can safely treat it as a fixed default with an optional override.
  Lower nibble (3/4/A/B/C/D) is fixed by the board and never moves.
  Interrupt vector low bytes default to **5CH** (fully-formed) and **34H** (dot-matrix) — 8-position
  switches, **ON = 0 / OFF = 1**, MSB first.
- **Ribbon lift (3355A) is a board-owned timer, not a register:** raise on CHAR STROBE (with an
  effective ~4 µs/8-clock forwarding delay to the printer), lower after ~1 second of no further
  CHAR STROBE. A file/console sink can model this as cosmetic-only, or ignore it entirely, since
  nothing reads the lift state back.
  **Polled operation only needs**: dot-matrix BUSY/ACKNLG, fully-formed IN BUFFER READY /
  PRINTER READY (+ optionally PAPER OUT / RIBBON OUT / CHECK for a "declare fault" console
  model). Interrupts (§8) can be skipped by a polled driver exactly as with the other MITS/
  Cromemco printer boards in this reference set.
- **No erratum found.** The port tables (pp.2–4) and the prose in Features/Installation (p.5)
  independently agree on both interrupt-enable bit positions (port 53H bit 2 for dot-matrix,
  port 5DH bit 2 for fully-formed) and on the two default vector bytes (5CH/34H) — unlike some
  other manuals in this reference set, this one is internally consistent everywhere checked.
