# Vector Graphic Bitstreamer II — three serial channels, two parallel channels and a 55 Hz clock

Source: [Bitstreamer II.pdf](#), [Bitstreamer_II_Schematic_Rev1.tif](#)

Vector Graphic Inc., *Bitstreamer II Board, Revision 1, User's Manual, Revision A*, January 1,
1980, 50 PDF pages, with drawing *Bitstreamer II Rev 1*, 9-1-79. This S-100 board has three 8251
serial channels, two parallel channels and a 55 Hz real-time clock. Interrupts are an option that
a technician wires with jumpers. This file omits the RS-232C theory, the cable lists for printers
and the kit of spare gates, except where a port or an interrupt depends on them.

Emulated as the `vgbsii` board (`docs/boards/vector-bitstreamer2.md`).

Citations: `n-m` is a printed page. PDF page = printed page + 12 for 1-1 to 1-9, and + 22 for 2-1
to 2-21. Pages 3-1, 3-2 and 3-3 are PDF 45, 46 and 49, and the board layout is PDF 47. The
schematic is the `.tif`, called *sheet* here. The 8251 is in
[Intel 8251 / 8251A USART](Intel%208251%20USART.md).

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Ports | Eight, x2 to x9, where x is 0 to 7. Base 02 is prewired (2-1) |
| x2, x3 | Serial channel A, data and status. Connector J3 |
| x4, x5 | Serial channel B, data and status. Connector J2 |
| x6, x7 | Serial channel C, data and status. Connector J1 |
| x8 | Parallel channel A. `OUT` = output latch, also the **interrupt mask**. `IN` = J4 lines |
| x9 | Parallel channel B. `OUT` = output latch. `IN` = J5 lines, and **clears the clock latch** |
| Interrupts | None prewired. Jumpers in area A connect the sources to `PINT` (pin 73) |
| Vector | FFH (`RST 7`), unless a jumper in area L pulls one data line low |
| Clock | 55 Hz by name, from a 2 MHz bus clock. The divider chain gives 54.6 Hz (§5) |

---

## 2. Port decode (2-1, 2-2, *sheet*)

Area W selects the upper digit of the base address. The lower digit is fixed, so the eight bases
are 02, 12, 22, 32, 42, 52, 62 and 72. A jumper set W1 to W6 selects one of them (Table 1). The
functions of the eight ports stay in the same order at each base (Table 2).

| Base | Ports | W1 | W2 | W3 | W4 | W5 | W6 |
|---|---|---|---|---|---|---|---|
| 02 | 02–09 | | x | | x | | x |
| 12 | 12–19 | | x | x | | | x |
| 22 | 22–29 | | x | | x | x | |
| 32 | 32–39 | | x | x | | x | |
| 42 | 42–49 | x | | | x | | x |
| 52 | 52–59 | x | | x | | | x |
| 62 | 62–69 | x | | | x | x | |
| 72 | 72–79 | x | | x | | x | |

Ports x0, x1 and xA to xF stay free for other boards (2-1).

---

## 3. Serial channels (1-2, 2-3, 3-1, *sheet*)

- **Chips.** U31 is channel A, U30 is channel B and U29 is channel C.
- **Baud rate.** Each channel has an eight-position switch: 9600, 4800, 2400, 1200, 600, 300, 150
  and 110. One position only must be set, or the channel does not run (2-3).
- **Clock factor.** The labels assume that the 8251 divides by 16. A guest that selects 64 gets one
  quarter of the labeled rate (2-3).
- **Clock chain.** U7, U2, U1, U11 and U3 are `'93` counters that divide the 2 MHz clock. U7 divides
  by 13 for 9600 baud. The 110 baud rate comes from the 2400 rate divided by 22 (3-1).
- **Modem lines.** RS-232C lines 5, 6 and 8 are held at +12 V. `CTS`, pin 17 of each 8251, is at
  ground as shipped (*sheet*, P/O N, O and P pad 1). A guest sees a channel that can transmit.
- **Current loop.** Channel C can use 20 mA loop with a jumper in area G (2-15).

---

## 4. Parallel channels (1-9, 2-20, 3-1, 3-2, *sheet*)

Each channel has eight output lines and eight input lines on a 34-pin connector.

- **Output is latched.** Four LS75 quad latches hold it: U5 and U9 for channel A, U16 and U20 for
  channel B. The latches take the data on the `OUT` cycle (3-2).
- **Input is not latched.** U12 and U24, LS244 buffers, put the connector lines on the bus during
  an `IN` (*sheet*). **No logic on the board drives these lines.** An `IN` reads what the cable
  brings. J4 pin 11 goes straight to U12 pin 2 (*sheet*). The manual does not say what an open
  input reads.

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | D7 OUT | 10 | D3 IN |
| 2 | D0 OUT | 11 | **D0 IN** |
| 3 | D1 OUT | 12 | D1 IN |
| 4 | D2 OUT | 13 | D2 IN |
| 5 | D3 OUT | 15 | D7 IN |
| 6 | D4 OUT | 32 | D4 IN |
| 7 | D5 OUT | 33 | D5 IN |
| 8 | D6 OUT | 34 | D6 IN |

The other pins are power and ground: 14, 16, 19–30 ground, 17 −12 V, 18 +12 V, 31 +5 V. Pin 9 has
no connection.

---

## 5. Interrupts (1-3, 1-7, 2-15 to 2-19, 3-2)

### Sources and the mask

An `OUT` to x8 writes the **interrupt masking register**. A 1 enables the source (2-16). Bit 7 is
not used. The register has no default in the text, so software must set it before it uses
interrupts (2-16).

| Source | Pad in area A | Mask bit |
|---|---|---|
| Channel A TxRDY | 3 | 0 |
| Channel A RxRDY | 6 | 1 |
| Channel B TxRDY | 7 | 2 |
| Channel B RxRDY | 4 | 3 |
| Channel C TxRDY | 2 | 4 |
| Channel C RxRDY | 5 | 5 |
| 55 Hz clock | 1 | 6 |
| `PINT` | 8 | |

Open-collector NAND gates in U6 and U10 join each 8251 line to its mask bit (3-2). Several of these
pads can be wired together to A-8.

Example from 2-16: `OUT x8,64H` enables channel B TxRDY, channel C RxRDY and the clock.

### The 55 Hz clock

The clock divides the 110 baud signal, which is 16 × 110 pulses each second. U3 divides it by 16,
and half of U4 divides it by 2 (3-2). The chain is not exact: 2 MHz / 13 / 4 is the 2400 rate, and
/ 22 gives 16 × 109.3. The clock is 2 MHz / 36608, or 54.6 Hz. The other half of U4 is the **clock latch** (*sheet*):

| U4 pin | Signal |
|---|---|
| 3, CK | 55 Hz square wave |
| 2, D and 4, PR | +5 V |
| 1, CLR | Low during an `IN` from x9, from U17-11 |
| 5, Q | No connection |
| 6, /Q | Latch output to pad A-1 |

A rising clock edge sets the latch, so pad A-1 **goes low and stays low**. An `IN` from x9 clears
the latch, and pad A-1 goes high. The data from that `IN` is not used for this (1-7, 2-19). An
interrupt routine must do this before `EI`, or the same latch interrupts again at once (1-7).

Pad A-1 is not open collector. It cannot share A-8 with the 8251 lines or with any other board
that drives `PINT`, such as the Flashwriter (2-15, 2-16, 3-2). The manual gives three ways to wire it:

| Purpose | Jumpers |
|---|---|
| Clock as the only source on the bus, no mask | A-1 to A-8 |
| Open collector, no mask | A-1 to B-1, B-1 to B-2, B-3 to A-8 |
| Open collector, with mask bit 6 | U9 pin 10 to B-2, A-1 to B-1, B-3 to A-8 |

The two lower rows go through a gate of U10. The manual says that this inverts the clock signal
and that this has no effect on the system (2-16). See ⚠ Q2.

### Vector (2-17 to 2-19)

A board with no jumpers leaves the bus at FFH during `SINTA`, which is `RST 7`. To put another byte
on the bus, jumper M to B-5, A-8 to U21-11, U21-10 to B-4, and L-1 to one pad of area L. That pad
pulls one data line low. Z80 mode 1 needs no byte at all.

| Byte | L-1 to | Byte | L-1 to |
|---|---|---|---|
| FF | Nothing | EF | L-2 |
| FE | L-9 | DF (`RST 3`) | L-4 |
| FD | L-8 | BF | L-6 |
| FB | L-3 | 7F | L-7 |
| F7 (`RST 6`) | L-5 | | |

### Finding the source (2-19)

The board has no interrupt status register. Software polls the 8251 status ports. If no 8251 is
active, the clock is the source (2-19). A Flashwriter on the same line also answers FFH, so the
guest must poll it too (2-18, 2-19).

To clear a source, read the data port of that 8251, or read x9 for the clock (2-19).

### VI lines

Area K has the eight S-100 `VI` lines. Jumper a pad of area A to a pad of area K in place of A-8.
Pads K-1 to K-8 are VI0 to VI7. No Vector Graphic board used them at this date (2-19).

---

## 6. Power-on

`POC*` drives the 8251 `RESET` pins through U21 (*sheet*). The manual does not give the power-on
state of the parallel latches, of the mask register or of the clock latch.

---

## 7. What Vector Graphic software uses (1-7)

| Channel | Use |
|---|---|
| A | Serial terminal, with Monitor option C. Some printers |
| B | Modem or time-share link, from the Version 4 Monitor |
| C | Printers, from MDOS, CP/M and the Word Management System |

---

## 8. Hardware identification (back of the manual, *sheet*)

| Part | Function |
|---|---|
| U29, U30, U31 | 8251 |
| U5, U9, U16, U20 | LS75, parallel output latches |
| U12, U24 | LS244, parallel input buffers |
| U4 | LS74, clock divider and clock latch |
| U6, U10 | 74LS38 open collector NAND |
| U1, U2, U3, U7, U11 | `'93` counters |
| U19 | `'42` decoder |
| U22, U26 | 1488 and 1489 line driver and receiver |
| U25, U28 | Empty sockets, for a 1488 or a 1489 |

---

## Quirks worth carrying

- **⚠ Q1. OASIS reads the clock through the parallel input. The board does not wire this.** OASIS
  5.5A reads `IN 28`, tests bit 0, and takes bit 0 = 1 to mean the clock interrupted. On a board at
  base 22, that is J4 pin 11, D0 IN. The schematic shows no connection from the clock latch to that
  input, and this manual does not describe one. The wire, if there is one, is outside this manual.
  **Unresolved.** The System B manual sends the reader to the *Time-Share Multi-User System B
  conversion package* for this class of change ([System B Overview](System%20B%20Overview.md),
  2-7). That package is not in hand. The older board's manual documents the same pattern
  ([Bit Streamer](Bitstreamer%20I.md) §4). There, U18 Q goes out on J2 pin 2, and a wire takes it to
  J2 pin 16, input A6. The program tests that bit on port A, and an `IN` of port B resets the latch.
  Status = 1 means set, which is the sense that `clock-latch` gives. OASIS does the same with
  `IN 28` bit 0 and `IN 29`. This is a precedent and an inference. It is not this wire, and the
  Bitstreamer II has no strobe pins on J4 or J5.
- **⚠ Q2. The sense of the clock.** Pad A-1 is low while the latch is set (*sheet*). OASIS wants
  bit 0 = 1 for the clock, so a plain wire from A-1 to J4 pin 11 reads backward. The gate of U10 in
  §5 gives the sense OASIS wants: high while the latch is set, low when it is clear and mask bit 6
  is 1. The same gate on `PINT` holds the line low while the latch is **clear**, which does not fit
  the text of 1-7. The manual's remark that the inversion has no effect is **unresolved**. U4 pin 5
  (Q) has the same sense with no mask, and the schematic leaves it with no connection. The gate is
  U10 pins 13 and 12 to pin 11, with pads B-1 and B-2 in and B-3 out, and nothing else on it
  (*sheet*). A NAND with A-1 at B-1 can only hold `PINT` low while the latch is clear. The same gate
  with U4-5 at B-1 would hold it low while the latch is set, as 1-7 says, but no document shows
  that wire. In that case B-3 reads backward for OASIS, and J4 pin 11 would have to come from U4-5.
- **⚠ Q3. OASIS and the mask.** OASIS writes `OUT 28,40`, which is mask bit 6 alone, so it enables
  the clock and no 8251. That agrees with Table 8.
- **⚠ Q4. OASIS and the 8251 bits.** The OASIS interrupt routine pairs its copy of the mask with
  the ports this way: bit 5 with `IN 23`, bit 3 with `IN 25`, bit 1 with `IN 27`. Each test is for
  status bit 1, RxRDY. Table 8 gives bit 5 to channel C, which is port 27, and bit 1 to channel A,
  which is port 23. The two orders are reversed. **Unresolved.** It has no effect while only bit 6
  is set.
- **⚠ Q5. "Bitstreamer I".** Page 2-4 says "the Bitstreamer I does not ground it" in a manual for
  the Bitstreamer II. It is a slip left from the older manual.
- **⚠ Q6. Schematic errata (3-3).** The pads of area K count 1 to 8 from the left. Below area K,
  "VIO" should read "VI". The dotted lines in area A and from A-8 to K-8 are not on a factory board.
