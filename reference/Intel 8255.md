# Intel 8255A — programmable peripheral interface

Source: [1979_Intel_Peripheral_Design_Handbook.pdf](#)

The 8255A/8255A-5 data sheet, © Intel 1979, printed pp. 1-52 to 1-72 of the *Peripheral Design
Handbook*. The chip has 24 I/O pins in three ports and three modes of operation. This file holds the
programmer's model. It leaves out the electrical tables, the timing diagrams, the handshake timing of
modes 1 and 2 and the application figures.

**Emulated** as `Intel8255` (`src/chips/intel8255.h`), mode 0 only, on the `vgzcb` board.

---

## 1. Quick reference for emulation

| Item | Value | Source |
|---|---|---|
| Registers | A1 A0: 00 port A, 01 port B, 10 port C, 11 control | p. 1-53 |
| Control register | Write only. A read at 11 is an "illegal condition" | p. 1-53, p. 1-54 |
| Control write, D7 = 1 | Mode set | p. 1-55 |
| Control write, D7 = 0 | Bit set or reset on port C | p. 1-56 |
| Reset | Clears the control register. All ports are inputs, and the 24 lines are high impedance | p. 1-54, p. 1-55 |
| Mode set | All output registers reset, with the status flip-flops | p. 1-55 |
| Mode 0 outputs | Latched | p. 1-56 |
| Mode 0 inputs | Not latched | p. 1-56 |

---

## 2. Basic operation (p. 1-53)

| A1 | A0 | RD* | WR* | CS* | Operation |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 1 | 0 | Port A to data bus |
| 0 | 1 | 0 | 1 | 0 | Port B to data bus |
| 1 | 0 | 0 | 1 | 0 | Port C to data bus |
| 0 | 0 | 1 | 0 | 0 | Data bus to port A |
| 0 | 1 | 1 | 0 | 0 | Data bus to port B |
| 1 | 0 | 1 | 0 | 0 | Data bus to port C |
| 1 | 1 | 1 | 0 | 0 | Data bus to control |
| X | X | X | X | 1 | Data bus 3-state |
| 1 | 1 | 0 | 1 | 0 | Illegal condition |
| X | X | 1 | 1 | 0 | Data bus 3-state |

---

## 3. Mode set word (Figure 4, p. 1-55)

| Bit | Field | Value |
|---|---|---|
| D7 | Mode set flag | 1 = active |
| D6, D5 | Group A mode | 00 = mode 0, 01 = mode 1, 1X = mode 2 |
| D4 | Port A | 1 = input, 0 = output |
| D3 | Port C upper, PC7 to PC4 | 1 = input, 0 = output |
| D2 | Group B mode | 0 = mode 0, 1 = mode 1 |
| D1 | Port B | 1 = input, 0 = output |
| D0 | Port C lower, PC3 to PC0 | 1 = input, 0 = output |

Group A is port A and port C upper. Group B is port B and port C lower (p. 1-54).

**Mode 0 has 16 configurations (p. 1-57).** D4, D3, D1 and D0 give each port its direction, with no
restriction. Word 9B makes all four inputs. Word 80 makes all four outputs. Word 89, which the Vector
Graphic printer driver writes, gives A output, B output and C input.

---

## 4. Bit set and reset word (Figure 5, p. 1-56)

| Bit | Field | Value |
|---|---|---|
| D7 | Bit set/reset flag | 0 = active |
| D6 to D4 | Unused | Don't care |
| D3, D2, D1 | Bit select, B2 B1 B0 | 0 to 7 |
| D0 | Set or reset | 1 = set, 0 = reset |

The word is written to the control register, and it changes one bit of port C. Bits of C upper that are
outputs must be accessed this way in the special mode combinations (p. 1-65).

---

## 5. Ports (p. 1-54)

| Port | Output | Input |
|---|---|---|
| A | 8-bit latch and buffer | 8-bit latch |
| B | 8-bit latch and buffer | 8-bit buffer |
| C | 8-bit latch and buffer | Buffer, no latch |

Port C divides into two 4-bit ports, under the mode control. Each 4-bit port has a 4-bit latch, and it
can carry the control and status signals of ports A and B.

---

## 6. Modes 1 and 2 (p. 1-55, p. 1-65)

Mode 1 is strobed input or output on group A, group B or both. Mode 2 is a bidirectional bus on port A
only, and it takes five lines of port C. In these modes port C carries handshake signals, and a normal
read of port C gives their status.

| Line | Mode 1 input | Mode 1 output | Mode 2 |
|---|---|---|---|
| PC0 | INTR B | INTR B | I/O, or group B |
| PC1 | IBF B | OBF* B | I/O, or group B |
| PC2 | STB* B | ACK* B | I/O, or group B |
| PC3 | INTR A | INTR A | INTR A |
| PC4 | STB* A | I/O | STB* A |
| PC5 | IBF A | I/O | IBF A |
| PC6 | I/O | ACK* A | ACK* A |
| PC7 | I/O | OBF* A | OBF* A |

The interrupt requests can be enabled and disabled with the bit set and reset function, through the
INTE flip-flops. Mode selection and a device reset clear all of the mask flip-flops (p. 1-56).

---

## Quirks worth carrying

- **⚠ Q1. A read of an output port.** The sheet does not say what a read of a port in output mode
  returns. Port A, port B and port C each have an input buffer on the same pins, and the output latch
  drives those pins, so `Intel8255` returns the latch value. A heavy load on a real pin can change the
  value.
- **⚠ Q2. A read of the control register.** P. 1-53 calls it an illegal condition, and p. 1-54 says that
  no read is allowed. The sheet does not give a value. `Intel8255` returns FF, the value of a 3-state
  data bus on S-100.
- **⚠ Q3. Reset clears the control register, but the ports are inputs.** A control register of 00 would
  be four outputs in the table of §3. The sheet states both facts on p. 1-54. **The ports as inputs
  won**, because p. 1-55 repeats that all 24 lines are high impedance after a reset. Software cannot read
  this register, so nothing shows the difference.
- **⚠ Q4. Output registers after a reset.** P. 1-55 says that a mode change resets the output registers,
  and it does not say the same of the reset pin. `Intel8255` clears them on both, because the ports are
  inputs after a reset and a program must then write a mode word.
