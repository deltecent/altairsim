# CompuPro Interfacer 1

Source: [CompuPro Interfacer 1.pdf](#) (Godbout Electronics / CompuPro, board 133H; cover dated
7/82, inner pages 11/80, schematic © 1981; 16 pp.)

The **CompuPro (Godbout) Interfacer 1** is an S-100 board with **two independent serial
channels**, A and B. Each channel is a MOS LSI UART ("1602, AY-5-1013 OR SIMILAR" on the
schematic; the parts list gives **TR1863-P**, U11 = A, U18 = B) with RS-232, 20 mA current-loop
and TTL line options. A **BR1941/2941** baud generator (U10, 5.0688 MHz crystal) clocks both
UARTs. Each channel is addressed on its own, anywhere in the 256-port space. As on the
Interfacer II, quad `74LS175` latches come up in a state set by jumper traces, and the CPU can
flip any of those levels later by writing a `1` to the matching control-port bit.

This is a distilled emulation reference: port map and addressing, the control and status bit
layouts **with their polarity**, the power-up strap scheme, the baud table, the interrupt
logic and the RS-232 pin mapping. The bring-up checklist, the troubleshooting guide, the parts
list and the current-loop wiring are summarized only where they confirm a bit's function.
Emulated as `interfacer1` (`docs/boards/compupro-interfacer1.md`).

---

## 1. Chips

| Ref | Part | Job |
|---|---|---|
| U11, U18 | TR1863 / TR1602 / AY-5-1013 | UART A, UART B |
| U10 | BR1941-L or BR2941-L, X1 5.0688 MHz | two baud clocks (16x), set by S1 |
| U12, U15 (A); U14, U17 (B) | 74LS175 | control latches, cleared by POC* and pRESET* |
| U27, U28 | 81LS95 (non-inverting, tri-state) | status buffers onto the internal bus |
| U19, U20 | 25LS2521 | port comparators for S2 (A) and S3 (B) |
| U22 | 74LS155 | I/O decoder: RDE, SWE, DS, CS strobes per channel |
| U1, U5 | 1488 (inverting) | RS-232 drivers: TxD, CA, CD |
| U2, U6 | 1489 (inverting) | RS-232 receivers: RxD, CB, CC, CF |
| U16, U29 | 74LS08, open-collector inverters | interrupt gates and VI drivers |

UART pin strapping on the schematic: **SWE (pin 16) grounded** (the status pins are always
driven), **CS (pin 34) tied high** (the format pins are read continuously), **NB2 (pin 37)
tied high** so NB1 alone selects 7 or 8 data bits, **RDAV (pin 18) tied to RDE (pin 4)** so
reading the data port clears DAV.

## 2. Addressing (page 4)

- **S2** addresses channel A, **S3** channel B. Positions 1-7 = A1-A7, `ON` = 0. Each channel
  is a 2-port block at an even address.
- Position 8: `ON` = channel **disabled** (removed from the I/O space), `OFF` = enabled.
- **J14:** factory trace **B-C** = **data at the switch address, status at +1**. Cut B-C and
  jumper **A-B** for status at the address and data at +1. One jumper for both channels.
- The CompuPro standard is channel A at `00`/`01` and channel B at `02`/`03`. The manual's
  example sets S2 positions 1-7 ON and 8 OFF for A at `00`.

| Address (factory J14) | IN | OUT |
|---|---|---|
| base | receive data (clears DAV) | transmit data |
| base+1 | status | control latch |

The manual does not give the control port its own address. Its test program uses
`CONTROL EQU 01H` and `STATUS EQU 01H`: the control port is an OUT to the status address.

## 3. Control port (pages 4-5)

| Bit | Name | Level 0 | Level 1 |
|---|---|---|---|
| D0 | RXINTE | receive interrupt disabled | enabled |
| D1 | TXINTE | transmit interrupt disabled | enabled |
| D2 | CD (RS-232 out) | "spacing" | "marking" |
| D3 | CA (RS-232 out) | "spacing" | "marking" |
| D4 | TSB | 1 stop bit | 2 stop bits |
| D5 | NP | parity | no parity |
| D6 | EPS | odd parity | even parity |
| D7 | NBI | 7 data bits | 8 data bits |

**The control port inverts; it does not set.** "To change the mask under software control,
simply output a '1' to the proper data bit of the Control Port corresponding to the signal
that you desire to change. This will flip the logic level of the particular signal to the
opposite state ... Outputting a '0' to a bit will return it to the power-up setting." So
**live level = strap XOR latch**. POC* and pRESET* clear the latches, which returns every line
to its strap. The manual's test program begins `MVI A,00H` / `OUT CONTROL` for exactly that
reason.

**Power-up straps.** Each latch output reaches a jumper socket both true and inverted; the owner
cuts one of the two traces per line. Channel A uses J7 (D7-D4) and J9 (D3-D0); channel B uses
J8 and J10. Never leave both traces of a line in (it ties a Q and a Q* together).

| Bit | Line | Socket (A / B) | "0" trace | "1" trace |
|---|---|---|---|---|
| D7 | NBI | J7 / J8 | 1-16 | 2-15 |
| D6 | EPS | J7 / J8 | 3-14 | 4-13 |
| D5 | NP | J7 / J8 | 5-12 | 6-11 |
| D4 | TSB | J7 / J8 | 7-10 | 8-9 |
| D3 | CA | J9 / J10 | 1-16 | 2-15 |
| D2 | CD | J9 / J10 | 3-14 | 4-13 |
| D1 | TXINTE | J9 / J10 | 5-12 | 6-11 |
| D0 | RXINTE | J9 / J10 | 7-10 | 8-9 |

## 4. Status port (page 4)

| Bit | Name | Source | Reads 1 when |
|---|---|---|---|
| D0 | TBMT | UART pin 22 | the transmit buffer is empty |
| D1 | DAV | UART pin 19 | a character is waiting |
| D2 | OPT | J12 (A) / J13 (B) | see below |
| D3 | PE | UART pin 13 | parity error |
| D4 | OR | UART pin 15 | overrun |
| D5 | FE | UART pin 14 | framing error |
| D6 | CC | RS-232 input via 1489 | the line is **OFF** |
| D7 | CB | RS-232 input via 1489 | the line is **OFF** |

**Polarity, from the schematic (pages 9-10); the manual's tables do not state it.** The
UART's status pins reach the bus through the non-inverting 81LS95s, so they read true. This
matches the test program (`ANI 02H` / `JZ` waits for DAV = 1, `ANI 01H` / `JZ` for TBMT = 1).
Checked against a printed copy of the manual: U27/U28 are listed as 81LS95/97 (both non-inverting types), and the only bubbles on them are the active-low enables at pins 1 and 19 (G1/G2). CB, CC and CF pass through a 1489 receiver first, which inverts: an asserted RS-232 control
line (positive, ON) reads **0**, and an unconnected input (the 1489 output floats high) reads 1.

**OPT (D2):** jumper **B-A** at J12/J13 puts the RS-232 **CF** input (received line signal
detect) on D2, through the 1489 (ON reads 0). Jumper **B-C** puts the UART's **EOC** (End Of
Character, pin 24) on D2, true sense.

## 5. RS-232 lines (page 6)

The outputs pass through a 1488, which inverts: a level of **1 = "marking" = negative = OFF**,
and **0 = "spacing" = positive = ON** (asserted).

| Bit | Line | DB25 pin, master (DTE) | DB25 pin, slave (DCE) |
|---|---|---|---|
| D2 (out) | CD (DTR) | 20 | 6 |
| D3 (out) | CA (RTS) | 4 | 5 |
| D6 (in) | CC (DSR) | 6 | 20 |
| D7 (in) | CB (CTS) | 5 | 4 |
| D2 (in, J12/J13 B-A) | CF (DCD) | 8 | -- |

Master/slave is set by the J3/J5 sockets: straight across for master, with 2/3, 4/5 and 6/20
crossed for slave (a terminal or printer is a DTE, so the board is set as a slave for it).
Current loop and TTL use J4/J6. These are wiring choices on the far side of the UART and do not
change any register.

## 6. Baud rate, switch S1 (page 4)

Positions 1-4 select channel A, 5-8 channel B. `ON` = 0, and the left-most position is the
least significant bit.

| Code (pos 1-4 or 5-8) | Baud | Code | Baud |
|---|---|---|---|
| 0000 | 50 | 0001 | 1800 |
| 1000 | 75 | 1001 | 2000 |
| 0100 | 110 | 0101 | 2400 |
| 1100 | 134.5 | 1101 | 3600 |
| 0010 | 150 | 0011 | 4800 |
| 1010 | 300 | 1011 | 7200 |
| 0110 | 600 | 0111 | 9600 |
| 1110 | 1200 | 1111 | 19200 |

The UART runs from a 16x clock (troubleshooting, page 11: "1200 Baud x 16 = 19.2 KHz").

## 7. Interrupts (pages 5, 13)

Four sources, each an AND gate (U16) into an open-collector inverter (U29):

| Output | Logic | J11 pins |
|---|---|---|
| RxINT A | DAV A AND RXINTE A | 16, 15 |
| TxINT A | TBMT A AND TXINTE A | 14, 13 |
| RxINT B | DAV B AND RXINTE B | 12, 11 |
| TxINT B | TBMT B AND TXINTE B | 10, 9 |

J11 pins 1-8 go to **VI0-VI7** (S-100 pins 4-11). A jumper from an output pin to a VI pin
routes that interrupt. There is **no pin-73 (pINT) position**. `sINTA` (pin 96) is not used.
An RxINT drops when the character is read; a TxINT drops when a character is written and the
buffer is full. TxINT is a **level**: it is held as long as TBMT and TXINTE are both set.

⚠ **Manual erratum:** the J11 figure labels pins 10/9 "Rx INT B" a second time. The logic
diagram (page 10) shows TxINT B on those pins.

## 8. Notes for an emulator

- The status byte has no inverted bits apart from the three RS-232 inputs. A CompuPro BIOS that
  uses `TBMT = D0` and `DAV = D1`, active high, runs unchanged.
- Writing `00` to the control port is a "return to the jumpers", not "set everything to 0".
- Reset clears only the latches and the UARTs. The straps, addresses and baud rates are switch
  and jumper settings.
- The interrupt enables are part of the same latch byte as the word format. A driver that
  changes the format with a read-modify-write of a remembered byte keeps its enables; one that
  writes a constant can turn them off or on by accident.
