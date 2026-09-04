# SSM IO-4 (2P + 2S) I/O Board

Source: [io4.pdf](#) (SSM Microcomputer Products, 2116 Walsh Avenue, Santa Clara CA 95050,
(408) 246-2707; *IO4 2 Parallel & 2 Serial I/O Board*; board © 1977, manual dated 3-19-79).
Read as page images — the scan has no text layer.

The **SSM IO-4** (the manual also calls it the **10-4, 2P + 2S**) is an S-100 board that puts
two independent serial ports and two independent parallel ports on one card. "Two" here means
**two in *and* two out** on each side: the serial half is a dual full-duplex UART pair, and the
parallel half is four latched 8-bit ports (two input, two output). SSM is the former **Solid
State Music** ("we still make the blue boards"); the same maker as the [PB1](SSM%20PB1%20EPROM%20Programmer.md).

The two halves are addressed **independently and exclusively**: each occupies its own port
block, set by its own DIP switch, and if the two blocks are ever set to overlap, **neither
section responds** in the contended region (a deliberate mutual-exclusion, not a bus fight).

This is a distilled emulation reference: the serial UART controls, the status/data strapping,
the baud generator, the current-loop/EIA options, the parallel handshake, the port-address
decodes, and the interrupt wiring. **The serial half is emulated** as the `io4` board
(`src/boards/io4.h`), a chip-backed card built on the real 1602-family UART
(`src/chips/uart1602.h`): two independent full-duplex channels with programmable word length,
parity and stop bits, AND the full status-word strap-up — the six status signals to any data
bits (headers W1/W2), either polarity (the U18/U16 74367/74368 buffer), and per-channel port
reversal (S1/S2-PR), presettable from named host personalities (`altair-rev1` — the default
and the SSM 8080 monitor console — plus `altair-rev0`, `i8251`, `proctech`, `imsai`, and
`custom`). (This is not the generic strap board — that is `gsio`, on the chip-less
`src/boards/strapserial.h`.) The **parallel section is also emulated**: four 8212 latched ports —
two in (J4/J6), two out (J3/J5) — each input carrying a service-request flip-flop, on their own
2-port block (switch S4), with the §3.2.2 status/data console flag strappable across ports. The
two halves are mutually exclusive on any address overlap. The **interrupts** (header W4) are
emulated too: each serial channel's receive and transmit and each parallel input are strapped to a
VI line, to pin 73, or to none — with no software enable, exactly as the card works. The
**current-loop/EIA electrical options are out of scope** — this reference documents them for
completeness.

---

## At a glance

| | |
|---|---|
| Bus | S-100, 8080-family host |
| Serial | **2 UART channels** (Serial A = U9, Serial B = U8; TMS6011 / AY5-1013 / TR-1602), full-duplex, async |
| Serial levels | 20/60 mA current loop (opto-isolated) **or** EIA RS-232 (DS1488 driver), per channel |
| Serial baud | 10 rates **55–9600**, RX and TX of each channel strapped independently on header **W3** |
| Serial address | a **4-port block** on any 4-port boundary, switch **S3** (decodes A7–A2) |
| Parallel | **4 latched 8-bit ports** — 2 in (J4, J6) + 2 out (J3, J5) — using **8212** latch-buffers (U10–U13) |
| Parallel address | a **2-port block** on any 2-port boundary, switch **S4** (decodes A7–A1) |
| Interrupts | serial-RX and parallel-in interrupts, strapped on header **W4**; to an 8-level priority card or polled |
| UART setup | switch **S2** = Serial A, **S1** = Serial B (parity, word length, stop bits, port reversal) |
| Status strap | header **W2** = Serial A, **W1** = Serial B (6 status bits → any data-bus bits, any polarity) |
| Power | **+8 V @ 0.95 A, +16 V @ 0.6 A, −16 V @ 80 mA** typical |

---

## Serial section

Two independent full-duplex asynchronous channels, each built on a UART (**U9 = Serial A**,
**U8 = Serial B**; a TMS6011 / AY5-1013 / TR-1602 in a 40-pin socket). Each channel has its own
control switch, its own status header, its own baud straps, and its own line interface, so the
two ports can run at different speeds, formats and electrical standards at once.

### UART control switches — S2 (Serial A), S1 (Serial B)

The UART's mode pins are held by a DIP switch: **S2 controls Serial A, S1 controls Serial B.**
`OFF` may be silk-screened "open"; `ON` may be called "closed".

| Function | Switch | Setting |
|---|---|---|
| **Parity enable** | S1/S2-**NPB** (No Parity Bit) | **OFF = no parity bit** (most common) |
| **Parity sense** | S1/S2-**POE** (Parity Odd/Even) | **ON = odd**, OFF = even |
| **Word length** | S1/S2-**NDB1** + **NDB2** | see table below |
| **Stop bits** | S1/S2-**NSB** | **ON = 1** (300 baud & up); OFF = 2 (or 1.5 if word = 5 bits) |
| **Port reversal** | S1/S2-**PR** | OFF = status port first / data last; ON = data first / status last |

**Word length (data bits):**

| Data bits | NDB1 | NDB2 |
|---|---|---|
| 5 | ON  | ON  |
| 6 | OFF | ON  |
| 7 | ON  | OFF |
| **8** | **OFF** | **OFF** *(most typical)* |

**Port reversal (PR):** the status and data port addresses of a channel can be exchanged so the
board matches whichever software expects which order.

- **PR = OFF** — status port first, data port last. *Most common with MITS, Processor Technology, etc.*
- **PR = ON** — data port first, status port last. *Most common with IMSAI software.*

Each channel's two ports (data and status) may be reversed independently of the other channel.

### Baud-rate generator — header W3

A common divider chain produces the ten rates below; the clock is **16 × baud**. To set a
channel, run a jumper on **W3** from the pin carrying the wanted frequency to the pin for that
channel's RX or TX. **RX and TX are strapped separately**, so a channel can send and receive at
different rates.

| Baud | Clock (16×) | W3 pin | % error |
|---|---|---|---|
| 55   | 874 Hz     | 10 | −0.68 |
| 75   | 1.202 kHz  | 1  | +0.16 |
| 110  | 1.748 kHz  | 8  | −0.68 |
| 150  | 2.404 kHz  | 7  | +0.16 |
| 300  | 4.807 kHz  | 4  | +0.16 |
| 600  | 9.615 kHz  | 3  | +0.16 |
| 1200 | 19.231 kHz | 2  | +0.16 |
| 2400 | 38.461 kHz | 5  | +0.16 |
| 4800 | 76.973 kHz | 6  | +0.16 |
| 9600 | 153.846 kHz| 9  | +0.16 |

**W3 destination pins:** RX A = 11, TX A = 12, RX B = 13, TX B = 14.

Clock chain (schematic): U29/U30 (74LS197) and U31 (÷11) / U32 (÷13) 74LS193 dividers off the
S-100 2 MHz Φ2 (bus pin 49); U33 (74LS74) final stage. (See **§4.11 PolyMorphic** for a ÷12
mod when the host clock is 1.8 MHz, and **§4.7 Selectric** for a 133.5-baud mod.)

### Status word strap-up — header W2 (Serial A), W1 (Serial B)

Six UART status signals are brought to a header and can be jumpered to **any** data-bus bits, in
any positive or negative pattern, so the status byte read by the CPU can be shaped to imitate
almost any other card's status port. The **logic sense** is set by the status-buffer chip:
**U18 (Serial A) / U16 (Serial B)** — a **74367** reads **active-high** (positive sense), a
**74368** reads **active-low** (negative sense); swap the chip to flip every status bit's polarity.

**Status signals (W1/W2 pin):**

| Status | Pin |
|---|---|
| Output Data Available (DAV / ODA) | 4 |
| Receiver Over Run (ROR) | 3 |
| Receiver Parity Error (RPE) | 2 |
| Receiver Framing Error (RFE) | 7 |
| Transmitter End of Character (TEOC) | 6 |
| Transmitter Buffer Empty (TBMT) | 5 |

**Data lines (W1/W2 pin):** D0 = 9, D1 = 10, D2 = 11, D3 = 12, D4 = 13, D5 = 14, D6 = 15, D7 = 16.

A jumper from a status pin to a data pin routes that status bit into that data-bus position.

### Serial addressing — switch S3

The serial section answers as a **4-port block** starting on any 4-port boundary. **S3** is a
6-bit address code decoding **A7–A2** (`ON` = that address bit is 1). All positions `ON` puts the
block at **ports 0–3**; the status ports land at 0 & 2 and the data ports at 1 & 3 for channels
A & B (subject to each channel's PR reversal).

| Ports | A7 | A6 | A5 | A4 | A3 | A2 |
|---|---|---|---|---|---|---|
| 0–3   | on | on | on | on | on | on |
| 4–7   | on | on | on | on | on | off |
| 8–B   | on | on | on | on | off | on |
| C–F   | on | on | on | on | off | off |
| 20–23 | on | on | off | on | on | on |
| … | | | | | | |
| F0–F3 | off | off | off | off | on | on |
| F4–F7 | off | off | off | off | on | off |
| F8–FB | off | off | off | off | off | on |
| FC–FF | off | off | off | off | off | off |

(In the default 0–3 layout, ports 0/1 = Serial A, ports 2/3 = Serial B.)

### 20 mA / 60 mA current loop — §3.1.5

The board carries **optical isolators** for both send and receive of a **20 or 60 mA** current
loop. The isolators respond to **baud rates up to 4800**. Connections are made on **J1** (a
channel) / **J2** (the other channel); pin 7 = input ground, pin 8 = output ground.

**Serial input (to the card):**
- **20 mA:** connect pin 3 → pin 6 (uses the internal −12 V); remove **R6** (Serial B) or **R12**
  (Serial A); pins 2 & 7 are the loop signal (pin 7 = gnd).
- **60 mA:** external **180 Ω 1 W** resistor between pins 3 & 4; **install R6** (Serial B) or **R12**
  (Serial A); pins 2 & 7 are the loop signal.

**Serial output (from the card):**
- **20 mA:** connect pin 5 → pin 12 (uses the internal +12 V); pins 10 & 8 are the loop signal (pin 8 = gnd).
- **60 mA:** connect pin 5 through an external **180 Ω 1 W** resistor to pin 9; pins 10 & 8 are the loop signal.

### EIA (RS-232) interface — §3.1.6

Each channel also has an EIA interface: input impedance ≈ **2.7 kΩ**, output drive ≈ **±10 V**
from **U4 (DS1488)**. On J1/J2:

- **Pin 1** = EIA input pin (input receiver U5 with a 4.7 V zener + 2.7 kΩ), **pin 7** = input ground.
- **Pin 11** = EIA output pin (from U4/DS1488), **pin 8** = output ground.

---

## Parallel section

Two input and two output 8-bit ports built on **8212** (74S412) latch-buffers with a service-
request flip-flop:

| Connector | Direction | 8212 | Strobe/ACK |
|---|---|---|---|
| **J3** | output | U10 | — |
| **J4** | input  | U11 | strobe J4-1 |
| **J5** | output | U12 | — |
| **J6** | input  | U13 | strobe J6-1 |

### Parallel input (J4, J6) — §3.2.1

The input ports latch on the **strobe line** (J4-1 / J6-1): while strobe is high, data passes
into the 8212; on the strobe's **falling edge** the byte is held. Pulsing strobe positive is the
data-available strobe. The falling edge also sets the **service-request flip-flop**, driving the
`INT` line low (J4-2 / J6-2); the CPU reading the port raises it again (acknowledge).

### Parallel output (J3, J5)

Same as the inputs but with the data direction reversed: the service-request flip-flop is still
worked by the strobe line and the CPU, but the byte is **latched under CPU control** instead of
by the external strobe.

### Status/Data configuration — §3.2.2

The two parallel ports can be arranged as a **status/control + data** pair (the "8080 console"
idiom): e.g. **Parallel A = status port, Parallel B = data port, data-available flag = D0 going
low**. Typical connections: J4-2 → J6-9 (`DAV` bit), J6-1 → J6-14 (strobe = 1), and data strobed
into J4 with a positive pulse on pin 1.

### Parallel addressing — switch S4

The parallel section answers as a **2-port block** on any 2-port boundary. **S4** is a **7-bit**
address code decoding **A7–A1** (`ON` = 1). All `ON` → ports 0 & 1.

| Ports | A7 | A6 | A5 | A4 | A3 | A2 | A1 |
|---|---|---|---|---|---|---|---|
| 0 & 1 | on | on | on | on | on | on | on |
| 2 & 3 | on | on | on | on | on | on | off |
| 4 & 5 | on | on | on | on | on | off | on |
| 6 & 7 | on | on | on | on | on | off | off |
| 8 & 9 | on | on | on | on | off | on | on |
| … | | | | | | | |
| FC & FD | off | off | off | off | off | off | on |
| FE & FF | off | off | off | off | off | off | off |

**Port J5/J6 is the first port** addressed (S4's setting) and **J3/J4 is the second** (S4 + 1).
So Parallel A = J5 (out) / J6 (in) at the base address, Parallel B = J3 (out) / J4 (in) at base+1.

---

## Interrupts — header W4, §3.3

Both the serial and parallel sections can raise interrupts, routed to header **W4**.

- **Parallel input interrupt:** an external strobe into the 8212 (pin 1) of parallel input A or B
  raises `INT` — **even if that port is not currently addressed** by the system.
- **Serial input interrupt:** raised when Serial A or B receives a new character (DAV → high); the
  signal is inverted to the correct polarity by **U28** and sent to W4.

The interrupts can go to an **8-level priority card** or be **polled** through one of the board's
parallel input ports. Default 8-level priority order (highest first): **parallel-in B, parallel-in
A, serial-in B, serial-in A.**

**W4 pin map:** VI0 = Serial B RX, VI1 = Serial A RX, VI2 = Serial A TX, VI3 = Serial B TX,
VI5 = Parallel in B, VI6 = Parallel in A, VI7 = PINT.

**Polling example:** connect J6-1 → J6-14 (strobe = 1) so only serial A & B are polled, and wire
the DAV lines from W4 into the inputs of J6.

---

## Applications (§4)

Short strapping recipes the manual gives for imitating specific hosts. All strap the status
header **W2 (Serial A) / W1 (Serial B)**, mapping UART status pins to data-bus bits:

| §   | Target | Key strap (UART status pin → data bit) |
|---|---|---|
| 4.1 | **8251 emulation** (async) | TBMT→D0, DAV→D1, TEOC→D2, RPE→D3, ROR→D4, RFE→D5 |
| 4.2 | **Altair Rev 1** serial | DAV→D0, TBMT→D7; **change U18/U16 to 74LS368** to invert status; parallel side simulated as status/data |
| 4.3 | **Altair Rev 0** serial | DAV→D5, TBMT→D1 |
| 4.4 | **Processor Technology** | DAV→D6, TBMT→D7 |
| 4.5 | **IMSAI** | DAV→D1, TBMT→D0; set S2 (Serial A) / S1-PR (Serial B) **ON** to reverse status/data addresses |
| 4.6 | **EIA RS-232** cabling | J1/J2 header ↔ EIA socket wiring (Cochran) — see below |
| 4.7 | **Selectric 133.5 baud** | cut U31 pin 1↔15, jumper pin 1↔4 (110 → 133.5 baud, ~1 % err) |
| 4.8 | **Serial-TTL in / EIA out** | jumper across R13 (Serial A) or R7 (Serial B) for a TTL-level input (e.g. Xitex video card) |
| 4.9 | **RS-232 busy (pin 4)** printer | add a 74LS00 (UX) in a spare socket + zener as a busy detector (Heath WH14) |
| 4.10| **RS-232 clear-to-send (pin 5)** | as §4.9 but CTS on J2 pin 4 (Integral Data Systems printer) |
| 4.11| **PolyMorphic** (1.8 MHz clock) | cut U32 pin 14↔15, rejumper so U32 **divides by 12 not 13** → baud error back within 2 % |

**§4.6 EIA RS-232 interconnection** (16-pin header J1/J2 → EIA socket, per Lynn E. Cochran):
EIA-in = header pin 1 → socket 2 (Transmit Data); EIA-out = header pin 11 → socket 3 (Received
Data); header pin 9 (with pull-up **) → socket 5/6/8 (CTS/DSR/DCD); +5 V on header pin 13; header
pin 8 → socket 7 (Signal Ground); header pin 7 → socket 1 (Frame Ground). ** the pull-up borrows
the current-loop circuitry (pin 9), so the current loop can't be used at the same time unless you
instead drive pins 5/6/8 with three 1 kΩ resistors off pin 12 (+5 V).

---

## Parts / chip complement

| Ref | Part | Role |
|---|---|---|
| U1, U37 | 7805 / LM340T-5 | +5 V regulators (two rails: **+5VA** analog/isolator side, **+5VB** logic) |
| U27 | 7812 / LM340T-12 | +12 V regulator |
| U14 | 7912 / LM320-12 | −12 V regulator |
| U2, U3, U6, U7 | MCT2 / MCT200 opto-isolators | current-loop isolation |
| U4 | DS1488 / MC1488 | EIA (RS-232) line driver |
| U5, U21, U34 | 74LS00 | line-receiver gating / control glue |
| U8, U9 | **TMS6011 / AY5-1013 / TR-1602 UART** | **U9 = Serial A, U8 = Serial B** |
| U10–U13 | 74S412 / **8212** | parallel latch-buffers (U10 J3-out, U11 J4-in, U12 J5-out, U13 J6-in) |
| U16, U18 | **74367 / 74368** | serial status buffers (U18 = Serial A, U16 = Serial B); 74367 = positive sense, 74368 = negative |
| U19 | 74LS42 / 7442 | BCD/decimal decode (address) |
| U20, U26 | 74LS04 | inverters |
| U22, U24, U25 | 74LS367 | address / data buffers |
| U15, U17, U33 | 74LS32 / 74LS74 | glue / final baud stage |
| U23 | 74LS86 | address compare (XOR) |
| U28 | 74367/74368 | serial interrupt polarity inversion |
| U29, U30 | 74LS197 / 74197 / 8291 | baud divider |
| U31, U32 | 74LS193 / 74193 | baud dividers (÷11 / ÷13) |
| U35, U36 | **DM8131** | 6-bit address comparators (S3 serial / S4 parallel decode) |
| S1, S2 | 7-position DIP | UART control (S1 = Serial B, S2 = Serial A) |
| S3 | DIP | serial address (A7–A2) |
| S4 | DIP | parallel address (A7–A1) |
| W1, W2 | 16-pin header | status word strap (W1 = Serial B, W2 = Serial A) |
| W3 | 16-pin header | baud-rate select |
| W4 | 16-pin header | interrupt vectors |
| J1, J2 | 16-pin ribbon header | serial line I/O (current loop / EIA) |
| J3, J5 | 16-pin ribbon header | parallel **output** |
| J4, J6 | 16-pin ribbon header | parallel **input** |

**Power requirements:** +8 V @ 0.95 A, +16 V @ 0.6 A, −16 V @ 80 mA (typical).

---

## Header pinouts (from the schematic, sheet 2 of 2)

**J1, J2 — Serial:** 1 EIA-in, 2 −RL, 3 −12V, 4 −RL, 5 +12V, 6 −−RL, 7 GND / 8 GND, 9 +TL,
10 −TL, 11 EIA-out, 12 ++TL, 13 +5V, 14 +5V.

**J3, J5 — Output:** 1 STROBE, 2 ACK, 3 DO7, 4 DO5, 5 DO3, 6 DO1, 7 GND / 8 DO0, 9 DO2, 10 DO4,
11 DO6, 12 −12V, 13 +5V, 14 GND.

**J4, J6 — Input:** 1 STROBE, 2 ACK, 3 DI7, 4 DI5, 5 DI3, 6 DI1, 7 GND / 8 DI0, 9 DI2, 10 DI4,
11 DI6, 12 −12V, 13 +5V, 14 GND.

**W1, W2 — Status:** 2 RPE, 3 ROR, 4 ODA(DAV), 5 TBMT, 6 TEOC, 7 RFE / 8 D0, 9 D1, 10 D2, 11 D3,
12 D4, 13 D5, 14 D6, 15 D7 (pins 1 & 16 unused).

**W3 — Baud rate:** 1 = 75, 2 = 1.2K, 3 = 600, 4 = 300, 5 = 2.4K, 6 = 4.8K, 7 = 150 / 8 = 110,
9 = 9.6K, 10 = 55, 11 = RX A, 12 = TX A, 13 = RX B, 14 = TX B.

**W4 — Interrupt:** 1 VI0, 2 VI1, 3 VI2, 4 VI3, 5 VI4, 6 VI5, 7 VI6, 8 VI7 / 9 PINT, 11 Parallel
in A, 12 Parallel in B, 13 Serial B TX, 14 Serial A TX, 15 Serial A RX, 16 Serial B RX.
