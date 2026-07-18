# MITS 88-C700 Centronics Printer Controller

Source: [88-C700.pdf](#)

MITS, Inc. "Altair C700 Printer User's Guide", first printing March 1977. The
88-C700 is the S-100 interface board that couples an Altair 8800-series computer
to the Altair C700 line printer (a Centronics-parallel dot-matrix unit). The
board occupies **two consecutive I/O ports** — a control/status port and a data
port — and offers a switch-selectable single-level interrupt after each
character or after each CR/LF.

This file captures everything needed to *emulate* the board and the
printer-visible behavior of the C700. Assembly, cable orientation, parts lists,
and marketing text from the manual are omitted.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| I/O ports | **Two consecutive** — an even Control/Status port and the next odd Data port |
| Base select | 128 even bases, **000 … 376 octal**, via DIP switches SW2/SW3 |
| Address bit | **A0 = 0 → Control/Status; A0 = 1 → Data Transfer** |
| MITS software default | **Base 002** → `IN 2`=status, `OUT 2`=control, `OUT 3`=data |
| `IN` even | Read **status byte** (Table 1) |
| `OUT` even | Write **control byte** (Table 2): D0=PRIME, D1=INTERRUPT CONTROL |
| `OUT` odd | Send **one ASCII char** to the printer; clears ACK latch + interrupt |
| Interrupt | Single line, switch-selected: per-character **or** per-CR/LF (SW2 #4) |
| Interrupt enable | `OUT` control with **D1=1** |
| Printer | Bidirectional 5×7 matrix, 64-char USASCII, 132 col @ 10 cpi, 6 lpi |

The board is *not* self-vectoring — unlike the [88-VI/RTC](88-VI-RTC.md) it does
not jam an `RST` onto the bus. It drives the single S-100 interrupt line; the
status byte's bit 7 tells software the printer is the requester.

---

## 2. Address selection

The Altair provides 128 starting addresses selected by DIP switches SW2 (zone C7)
and SW3 (zone B7). Address bit **A0** picks one of the two consecutive ports the
board needs:

- **Control/Status** — always **even** (A0=0). `IN` reads printer status;
  `OUT` writes control (printer reset + interrupt enable/disable).
- **Data Transfer** — always **odd** (A0=1), the next address above the even
  base. `OUT` transmits an ASCII character to the printer.

> **Note 1 (manual):** MITS software requires that address **002** be selected.
> With base 002: `INPUT 2 = STATUS`, `OUTPUT 2 = CONTROL`, `OUTPUT 3 = DATA`.

Switch decode: SW2/SW3 are wired single-pole double-throw; a switch **ON**
(toward the number marked on it) = 1 and passes the true address, **OFF** = 0 and
passes the inverted address. When the incoming address matches the strapped
value, the board's compare (NAND gate J pin 8) enables the port. The base is any
even octal value 000–376.

---

## 3. Status channel (`IN`, even address)

An `INPUT` to the even address latches the status byte into the accumulator.
Table 1 defines each bit. **Bit 5 is unused.**

| Bit | Name | LOW (0) means | HIGH (1) means |
|-----|------|---------------|----------------|
| 0 | ACKNOWLEDGE | Not ready | Printer will accept new data |
| 1 | BUSY | Not busy | Print, return, or line feed is occurring |
| 2 | PAPER EMPTY | Printer has paper | Printer is out of paper |
| 3 | SELECT (SEL) | Printer is selected | Printer not selected, won't respond to input |
| 4 | FAULT | No fault | Paper out **or** printer not selected |
| 6 | INTERRUPT ENABLE | Interrupts disabled | Printer can interrupt (after BUSY or after ACKNOWLEDGE) |
| 7 | INTERRUPT REQUEST | No interrupt has occurred | Causes a system interrupt if the hardware interrupt is selected |

**ACKNOWLEDGE latch:** the printer's ACKNOWLEDGE pulse clocks the Acknowledge
Latch (IC D, zone A2) HIGH at pin 5 — this is status bit 0. It stays HIGH until
the CPU **outputs new data** to the data port, which clears it. So bit 0 is the
"last character taken, ready for the next" flag, re-armed by every data write.

---

## 4. Control channel (`OUT`, even address)

An `OUTPUT` to the even address drives the control byte. Data out lines are valid
for the ~500 ns duration of the internal `PWR` pulse. Only **D0** and **D1** are
control lines (Table 2); the rest are ignored.

| Bit | Name | LOW (0) | HIGH (1) |
|-----|------|---------|----------|
| 0 | PRIME | Reset printer buffer counter; print head goes to home position | No function |
| 1 | INTERRUPT CONTROL | Disable interrupt structure | Enable interrupt structure |

- **Reset / prime the printer:** `OUT` the control port with **D0 = 0**. This
  pulses the printer's `PRIME` line low (~500 ns), clearing its buffer and
  homing the carriage. (D0 = 1 = no operation.)
- **Enable interrupts:** `OUT` the control port with **D1 = 1**. A HIGH here
  (interrupt-enable condition, IC C6) lets the printer's BUSY/ACKNOWLEDGE events
  raise the CPU interrupt. D1 = 0 disables the interrupt structure.

The two bits are independent — a typical init writes D0=0 (prime), then later
D1=1 (arm interrupts) if interrupt-driven output is wanted.

---

## 5. Data transfer channel (`OUT`, odd address)

An `OUTPUT` to the odd (data) address:

1. Latches the 8-bit character to the printer data lines.
2. Fires the strobe `STB` HIGH for **~1.5 µs** to clock the byte into the
   printer. *(Manual margin note: the strobe may have to be widened to ≈2 µs to
   drive an Anadex printer.)*
3. **Clears the Acknowledge Latch (D5)** and the **INTERRUPT signal (IC C pin 9)**.

So writing a data byte is what dismisses a pending printer interrupt and re-arms
the ACKNOWLEDGE handshake. The printer answers with its own ACKNOWLEDGE pulse
when it has taken the byte, which re-sets bit 0 / bit 7 per the interrupt mode.

Internal timing (Figure 1): `TP4` (the OUT decode) → `PWR` 500 ns → after a
~1.5 µs delay `TP1` latches data → `TP2` raises `STB` for ~1.5 µs.

---

## 6. Interrupt structure

The board offers a **single-level** interrupt (it does not vector). Two things
gate it:

- **Granularity — SW2 position #4:**
  - **ON** (toward the # marking): interrupt after **each character**.
  - **OFF**: interrupt after **each Carriage Return or Line Feed** operation.
- **Enable — software:** the interrupt completes only after an `OUT` to the
  control channel with **D1 = 1**. Once the CPU's own interrupt is enabled, the
  printer then generates interrupts automatically.

The status byte reports the state: bit 6 (INTERRUPT ENABLE) and bit 7
(INTERRUPT REQUEST). A data-port write clears the request (see §5).

---

## 7. Program control codes (printer-side ASCII)

These codes are interpreted by the C700 itself when received on the data channel.
Octal values as printed in the manual (hex in parentheses):

| Code | Octal | Hex | Effect (printer must be Selected unless noted) |
|------|-------|-----|-----|
| **LF** | 012 | 0x0A | Advance one line immediately. |
| **CR** | 015 | 0x0D | Print the buffer, then a single line feed. Data is accepted until a CR **or** 132 printable chars; the line is then auto-printed with a trailing LF. *(A CR is **not** acknowledged in Deselect mode.)* |
| **DC1** | 021 | 0x11 | **Select** the printer, independent of the operator panel. |
| **DC3** | 023 | 0x13 | **Deselect** the printer, independent of the operator panel. |
| **DEL** | 177 | 0x7F | Reset the print buffer to zero, terminate paper motion, return carriage to the left margin. |
| **SO** | 016 | 0x0E | Print up to **66** buffer chars **expanded** (double-width, 5 cpi). Cancelled by DEL, an END-OF-PRINT, or a PRIME. Chars beyond 66 are nulled at end of print. |

---

## 8. C700 printer characteristics (context)

- Bidirectional 5×7 dot-matrix line printer; head prints both directions,
  always seeking the nearest margin of the next line (no wasted carriage return).
- **64-character** subset of USASCII.
- Original plus up to **four copies**.
- **132 columns** at 10 characters/inch; up to **66 columns** double-width
  (software-selected via SO, above).
- **6 lines/inch** vertical spacing.

For emulation, a faithful console model can treat the data port as a byte sink,
honor the control codes above (LF/CR/DC1/DC3/DEL/SO), keep a 132-column line
buffer, and expose the Table 1 status bits — with ACKNOWLEDGE/BUSY toggling and
the interrupt request following the SW2 #4 mode.
