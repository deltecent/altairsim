# IMSAI SIO-2 Serial I/O Board

Source: [IMSAI SIO 2 manual](#) (SIO 2 Board Rev. 3, Functional Description / Theory of
Operation / User's Guide, Edition 2; © 2002 IMSAI Division, Fischer-Freitas Company — a
reprint of the mid-1970s IMS Associates manual, assembly diagram dated 2/76).

The **IMSAI SIO-2** is a two-channel serial I/O board for the S-100 (IMSAI 8080) bus. It is
"structured around a pair of Intel 8251 USART" chips (p. 9-1) and provides "two complete
RS232 full duplex data lines with all control signals" plus TTL-level and 20/60 mA current-loop
formats. Async to 9600 baud, sync to 56,000 baud. The board can be jumpered to respond either
to **I/O port** instructions or to **memory-mapped** references.

This is a distilled emulation reference: the RS-232/TTL/current-loop driver wiring, the baud
divider chains, the PIC-8 interrupt plumbing and the connector pinouts are summarised, not
reproduced. What matters for an emulator is the port map, which register a poll reads, and the
fact that **the two ready bits an emulator must produce are defined by the 8251, not by this
board** — the manual defers programming of the USART itself to Intel:

> "For reference information on the programming and operation of the 8251 chip, the user should
> refer to the Intel 8080 Microcomputer Systems User's Manual." (p. 9-20)

See [`reference/Intel 8251 USART.md`](Intel%208251%20USART.md) for the chip's mode/command/status
model. This file covers only what the SIO-2 board adds around it.

---

## 1. Chip and board model

- **Two Intel 8251 USARTs** (marked `C8251`, 28-pin) — one per channel. Fully programmable:
  the manual notes "program control ... including the RS232 Control Line and sync character
  selection in the Synchronous mode and error condition sense and recovery" (p. 9-1).
- **Board variants in the same manual:** the SIO 2-**1** parts list populates **one** 8251
  (single channel built); the SIO 2-**2** parts list populates **two** 8251s (both channels).
  A full SIO-2 = both channels. Channel **A** is the natural console channel.
- **Data-ready polling is pure 8251.** The board does not interpose its own RxRDY/TxRDY logic;
  a driver reads the 8251 status register at the channel's control/status address (below).
- The board adds one extra register of its own — the **SIO control I/O port** (§4) — for
  interrupt-enable, carrier-detect and CTS, which the 8251 cannot report through its own status.

---

## 2. Address decode

Operation "requires 16 I/O port or address locations, which are selected by address bits 0
through 3. When the board is used with input and output instructions, address bits 4 through 7
form the remainder of the board address and are jumper selectable." (p. 9-1)

The low nibble (A0–A3) selects the function; the high nibble (A4–A7) is the jumpered board base.
From the **SIO BOARD ADDRESSING** table (p. 9-21):

| Address bit | Function | Asserted when |
|:-----------:|----------|---------------|
| **A0** | C/D̄ on the 8251s | `1` = CONTROL/STATUS, `0` = DATA |
| **A1** | Select **Channel A** | `1` = select |
| **A2** | Select **Channel B** | `1` = select |
| **A3** | Select the **board control I/O** port | `1` = select |
| **A4–A7** | **Card address** | jumperable to any one of 16 bases |

> "Address bits 1 and 2 select serial I/O channel A or channel B respectively. That is, when
> address bit 1 (A1) is high, serial I/O channel A is enabled. When address bit 2 (A2) is on,
> serial I/O channel B is enabled. Address bit 0 determines whether the I/O channel selected
> will respond ... as a control byte or a data byte. If address bit 0 is a 1, the control
> functions are selected, and if address bit 0 is a 0, the byte is assumed to be data."
> (p. 9-20)

**Memory-mapped mode:** the same A0–A7 layout becomes the low byte of the address and "the
upper byte of address is hex FE or octal 376" (p. 9-1, p. 9-20).

### Derived port offsets (base = A4–A7 nibble, 16-byte aligned)

The channel bit (A1/A2) and the C/D̄ bit (A0) combine to fixed offsets. The manual states them
in prose:

- Channel A control/status: "the lower 4 bits of address would normally contain **hex 3** or
  octal 03" (p. 9-20). Channel A data is therefore the same with A0=0 → **base+2**.
- Channel B control/status: "the normal address for channel B control bytes would be **hex 5**
  or octal 05" (p. 9-22). Channel B data → **base+4**.
- Board control I/O port: "the lower 4 bits of address would normally be **hex 8** or octal 10"
  (p. 9-22).

| Register | A3 A2 A1 A0 | Offset | Read | Write |
|----------|:-----------:|:------:|------|-------|
| **Channel A data** | `0 0 1 0` | base+`2` | RX data (8251 A) | TX data (8251 A) |
| **Channel A control/status** | `0 0 1 1` | base+`3` | **8251 A status** | 8251 A mode/command |
| **Channel B data** | `0 1 0 0` | base+`4` | RX data (8251 B) | TX data (8251 B) |
| **Channel B control/status** | `0 1 0 1` | base+`5` | **8251 B status** | 8251 B mode/command |
| **Board control I/O** | `1 0 0 0` | base+`8` | board input byte (§4) | board output byte (§4) |

Offsets 0, 1, 6, 7, 9–F within the 16-byte block are unused / decode to nothing meaningful
(A1 and A2 must not both be set; A0 alone selects nothing without a channel).

### Base address — no factory default in the manual

The card address (A4–A7) is "jumperable to any one of 16 addresses" (p. 9-21) — i.e. any
16-byte boundary `00, 10, 20, … F0`. **The manual specifies no factory-default base.** Selection
is by jumper/DIP at sockets C7 and D6 (pp. 9-33/9-34): a NAND over A4–A7 (inverted per bit as
jumpered) enables the board.

For an emulated console the natural choice is **base 0** (all address-bit jumpers → the byte
appears when A4–A7 are low), which reproduces the widely used IMSAI serial-console ports:

| Register | Port at base 0 |
|----------|:--------------:|
| Channel A data | `02h` |
| Channel A status/control | `03h` |
| Channel B data | `04h` |
| Channel B status/control | `05h` |
| Board control I/O | `08h` |

This base-0 map is a convention, not a manual-stated default — expose the base as a strap.

---

## 3. Status polling — the two bits an emulator must produce (8251-defined)

A driver waits for a received character or a free transmitter by reading the **8251 status
register** at the channel's control/status port (Channel A = base+3, Channel B = base+5). The
SIO-2 manual does not redefine these; they are the standard 8251 status bits:

| Bit | Flag | Meaning | Polarity |
|:---:|------|---------|----------|
| **D0** | **TxRDY** | Transmitter buffer empty — ready to accept a byte to send | **active-high** (`1` = ready) |
| **D1** | **RxRDY** | A received character is assembled and waiting | **active-high** (`1` = char available) |

Both are **active-high** (1 = asserted); neither is active-low. Reading the data port clears
RxRDY; writing the data port clears TxRDY. (D2 = TxEMPTY, D3 = PE, D4 = OE, D5 = FE, D6 =
SYNDET/BD, D7 = DSR — see the 8251 reference.) With a byte-clean transport, model PE/OE/FE as 0.

**Console poll idiom** (Channel A at base 0): `IN 03h` then test bit — `RRC`/`ANI 02h` for RxRDY,
`ANI 01h` for TxRDY.

---

## 4. Board control I/O port (base+8) — *not* the 8251 status

A separate board register, read and written at base+8, distinct from the 8251 status. It exists
because carrier-detect and CTS are board-level RS-232 signals the 8251 cannot report, and because
per-channel interrupt enables live here. From **SIO CONTROL I/O BIT DEFINITIONS** (p. 9-21):

| Bit | Input byte (read) | Output byte (write) |
|:---:|-------------------|---------------------|
| 0 | always 1 | Interrupt Enable, Channel A |
| 1 | always 1 | Carrier Detect, Channel A (originate) |
| 2 | Carrier Detect, Channel A | non-functional |
| 3 | Clear To Send, Channel A | non-functional |
| 4 | always 1 | Interrupt Enable, Channel B |
| 5 | always 1 | Carrier Detect, Channel B (originate) |
| 6 | Carrier Detect, Channel B | non-functional |
| 7 | Clear To Send, Channel B | non-functional |

Notes (p. 9-22):

- On **read**, bits 0, 1, 4, 5 are non-functional and "will always be read as a 1."
- Bits 2 and 6 read carrier-detect for A and B; "operative only when jumper socket BJ is
  jumpered to read the condition of the carrier detect line."
- Bits 3 and 7 read CTS for A and B: "provided because it is not possible to read the condition
  of CTS through programmed input from the 8251."
- On **write**, bits 0/4 enable interrupts for A/B; bits 1/5 output carrier-detect (originate
  mode, jumper BJ); bits 2, 3, 6, 7 are non-functional.

An emulated console that ignores modem control can leave this port's read as `0xFF`-ish
(the four "always 1" bits, plus whatever CD/CTS strap you model as ready) and ignore writes.

---

## 5. Interrupts (present; not implemented here)

The board "provides interrupt generation for received characters, empty transmitter buffers,
and sync characters" (p. 9-1). Interrupts fire on **TxRDY, TxEMPTY, RxRDY, and SYNDET**;
per-channel enables are output-byte bits 0 (A) and 4 (B) of the control port (§4). The channel
interrupt lines appear at the **interrupt select socket D3** and jumper to any of the eight
IMSAI 8080 priority-interrupt lines, working "in conjunction with the Priority Interrupt/Clock
board (PIC-8)" (p. 9-1, p. 9-31). **All functions also work fully polled without interrupts**
(p. 9-1). We do not model interrupts; a polled console needs none of §5.

---

## 6. Baud (summary)

Baud is jumper-selected at the data-rate-select socket B11 (jumper RJ), fed from a divide chain
off the 2 MHz Φ2 system clock (÷13 + 7493 stages give ≈16×9600). "The baud rates ... are correct
when the 8251 is programmed for a 16X asynchronous clock rate and a 1X synchronous clock rate"
(p. 9-27). An external SIOC divider board can supply non-standard rates. For emulation, baud is a
strap; framing/pacing follow the 8251 model.

---

## 7. Emulation checklist

- **Two 8251s**, Channel A (base+2 data / base+3 status·control) and Channel B (base+4 / base+5).
  Base = A4–A7 jumper, any 16-byte boundary; **no factory default** — expose it, default to `0`
  (→ A data 02/03, B data 04/05, control port 08).
- **Poll the 8251 status**, not a board register: **TxRDY = D0, RxRDY = D1, both active-high.**
  Reading data clears RxRDY; writing data clears TxRDY. Model PE/OE/FE = 0 on a clean transport.
- **A0 = C/D̄** (1 = control/status, 0 = data); **A1 = select A, A2 = select B, A3 = board control
  port.** Both A1 and A2 set is illegal.
- **Board control I/O port at base+8** carries interrupt-enable / carrier-detect / CTS — separate
  from the 8251 status; its read "always 1" bits are 0,1,4,5. Safe to stub for a bare console.
- **Interrupts** exist (RxRDY/TxRDY/TxEMPTY/SYNDET → PIC-8 via socket D3) but all I/O also works
  polled — no interrupt model is needed for a console.
- **Memory-mapped mode** repeats the port map as the low address byte with high byte `FEh`
  (`376` octal).
