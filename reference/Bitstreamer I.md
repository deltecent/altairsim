# Vector Graphic Bit Streamer — one 8251 and two parallel ports

Source: [Bitstreamer_I.pdf](#)

Vector Graphic *Bit Streamer I/O Board* users guide, undated, 20 PDF pages, with the schematic. This
is the board before the [Bitstreamer II](Bitstreamer%20II.md), which has three 8251s and a clock.
The Bit Streamer has no clock and no interrupt logic. The board also appears in the
[Early Products](Vector%20Graphic%20Early%20Products.md) sheets.

**Emulated as `vgbs`.** This file omits the repair agreement and the general trouble shooting guide.

---

## 1. Quick reference for emulation

| Port | `IN` | `OUT` |
|---|---|---|
| 00 | Parallel port A | Parallel port A |
| 01 | Parallel port B. **Also resets the status latch** | Parallel port B |
| 02 | USART data | USART data |
| 03 | USART status | USART command |

| Item | Value |
|---|---|
| Serial | 8251 at U9, RS-232 or 20 mA current loop |
| Baud | 110 to 9600 on an eight-position switch. One switch only must be on. 9600 at the left |
| Baud clock | The 2 MHz clock divided by 13 for 9600, halved for each lower rate. 110 is the 2400 clock divided by 22 |
| Output ports | Latched in LS75s U14 to U17, on J3 |
| Input ports | Not latched. Buffered by 8097s U4 to U6, on J2 |
| Address | Six pairs of pads beside U7 set A2 to A7. Base 00 as shipped |

---

## 2. Serial software (p. 6, p. 7)

```
INIT    MVI A,0CEH      ; asynchronous, x16, 8 bits, no parity, 2 stop bits
        OUT 3
        MVI A,27H       ; enable the transmitter and the receiver
        OUT 3

INPUT   IN  3           ; status
        XRI 00
        ANI 2           ; D1 = RxRDY
        JZ  INPUT
        IN  2

OUTPT   IN  3
        XRI 00
        ANI 1           ; D0 = TxRDY
        JZ  OUTPT
        POP PSW
        OUT 2
```

`CTS` must be high, and a pull-up holds it high with no connection. Bit 7 of the status port is
`DSR`. Software sends the two commands once after each power-on or reset. The Vector Graphic
processor board generates `POC` on both. Monitor option C uses this setup.

---

## 3. Connectors (Figure 1, Figure 2)

| J3 pin | DB25 pin | Signal |
|---|---|---|
| 24 | 1 | Protective ground |
| 23 | 2 | Transmitted data |
| 22 | 3 | Received data |
| 21 | 4 | RTS |
| 20 | 5 | CTS |
| 19 | 6 | DSR |
| 18 | 7 | Signal ground |
| 7 | 20 | DTR |

J2 pins 1 to 24, from the figure: 1 GND, 2 STROBE B OUTPUT, 3 STROBE A OUTPUT, 4 −V REG, 5 B6, 6 B5,
7 B3, 8 B2, 9 B7, 10 B4, 11 NC, 12 VCC, 13 STROBE A INPUT, 14 STROBE B INPUT, 15 A7, 16 A6, 17 B0,
18 B1, 19 A0, 20 A1, 21 A3, 22 A5, 23 A4, 24 A2.

J3 carries the output lines: 1 B3, 2 B2, 3 B1, 4 B0, 5 A0, 6 A1, 8 A7, 9 A2, 10 A3, 11 B4, 12 B5,
13 B6, 14 B7, 15 A4, 16 A5, 17 A6, 18 GND. The table above names the serial pins from the terminal
side, and Figure 1 names them from the board side.

---

## 4. The keyboard wire (p. 10, p. 11)

This wire is a precedent for [Bitstreamer II](Bitstreamer%20II.md) Q1. U18 is an LS107 J-K
flip-flop, the status bit latch. The strobe from the keyboard sets it, by J2 pin 14 and U25.

**"The Q output of U18 available at J2 pin 2 should be connected to J2 pin 16 (A6 input)."**

The status latch reaches port A only through that wire on the connector. The manual then gives this
routine:

```
INPUT   IN  0           ; KBD status, port A
        ANI 40H         ; status bit
        JZ  INPUT
        IN  1           ; KBD data, port B
        ANI 7FH
        RET
```

"The status bit is reset when port B is input." The manual warns not to reset the latch until the
data has been read, because software may test the status bit many times while it looks for a
Control-C. The latch holds the status. The data is not latched, so a break key must stay down until
the program polls the keyboard.

The manual uses the plain Q output. Status bit = 1 means a key waits.

---

## 5. Other notes

- **Keyboard data** goes to input port B, bits 0 to 6 (p. 10).
- **Negative supply.** Pads at the lower left take a 78L12 or a zener for a keyboard that needs one.
- **Two serial outputs (p. 14).** A 74LS00 in the patch area gates a second output with `RTS`.
  `MVI A,27H` / `OUT 3` turns the second output off, and `MVI A,07H` / `OUT 3` turns it on. Monitor
  option C sets `RTS` low, so the second output starts off.
- **Patch area.** Two 16-pin pad arrays right of the 8251. A buffered `CLK` is at U24-6.
- **Pin 49.** Undershoot on the clock line can give a wrong baud rate. The cure is 220 Ω from the
  eyelet above pin 49 to the ground strip of pin 50 (p. 11).
- **Reset key.** A spare key wired to pin 75 lets a user jump to the Monitor (p. 11).

---

## Quirks worth carrying

- **⚠ Pages out of order.** The printed page 11 comes before pages 9 and 10 in the scan.
- **⚠ The manual disagrees on J2 and J3.** Page 6 says first that the outputs are on J3 and the
  inputs on J2, and Figure 1 agrees. The same page then says that `IN 1` reads "PORT B ON J3". The
  figure and the schematic win: the inputs are on J2.
- **⚠ Not a Bitstreamer II fact.** The Bitstreamer II has no strobe pins on J4 or J5. Its clock latch
  has a different job. The pattern is common to both boards, and the wire is documented only here.
