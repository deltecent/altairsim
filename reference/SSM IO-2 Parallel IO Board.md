# SSM IO-2 Parallel I/O Board

Source: [IO2_Parallel_IO_Board.pdf](#) (SSM Microcomputer Products, 2116 Walsh Avenue, Santa
Clara CA 95050, (408) 246-2707; *IO2 Parallel I/O Board*; © Solid State Music 1977).
Read as page images — the scan has no text layer.

The **SSM IO-2** (silk-screened **10-2**) is the earlier, simpler sibling of the
[IO-4](SSM%20IO-4%202P%2B2S%20IO%20Board.md). It is an S-100 **prototyping card** with a small
block of *committed* circuitry: **one parallel input port and one parallel output port**, both
8212-buffered, plus a DIP-switch address decoder that answers a **block of eight consecutive port
addresses**. The rest of the board is uncommitted trace and socket area, so the same card can be
built up three different ways:

- a **parallel I/O card** (the committed ports, as shipped);
- a **serial interface card** — add a UART (AY-5-1013), the baud-rate divider and a TTY/EIA line
  interface, using the supplied supplemental drawing (a MITS Rev 0 / Rev 1 SIO clone); or
- a **1702 PROM card** — the same address decoder becomes a memory-mapped chip-select for up to
  eight 1702 (256 × 8) PROMs = a 2 K window anywhere in the 64 K space.

SSM is the former **Solid State Music** ("we still make the blue boards"); the same maker as the
[IO-4](SSM%20IO-4%202P%2B2S%20IO%20Board.md) and [PB1](SSM%20PB1%20EPROM%20Programmer.md). The
manual explicitly points anyone wanting *flexible* or *multiple* serial ports at the IO-4 instead,
and anyone wanting a real PROM board at the SSM **MB-3**. The concept and tape master are credited
to **Malcolm Wright**; the UART and TTY interface circuits to **Lynn Cochran**.

This is a distilled emulation reference: the address decoder, the two committed 8212 ports and
their control jumpers, and the two optional personalities (serial, PROM). **This board is not
currently emulated** — no `io2` board exists in `src/boards/` (the unrelated `sio2port` chip is
the 2SIO, not this card); this file is period documentation captured for a possible future board.

---

## At a glance

| | |
|---|---|
| Bus | S-100, 8080-family host |
| Committed I/O | **one parallel input port** (8212 **U8**) + **one parallel output port** (8212 **U10**) |
| Buffering | 8212 (74S412) tri-state latches on both ports |
| Address decode | one **8-position DIP switch (U7)** picks a **block of 8 consecutive** port (or PROM) addresses; `U1` 7485 compare + `U3` 7442 1-of-8 decode |
| Expandable to | two more parallel ports, **or** one serial port (UART + baud generator + TTY/EIA), **or** a 1702-PROM memory-mapped card |
| Prototyping | uncommitted area for 2×24-pin, 1×40-pin, 9×16-pin, 1×14-pin ICs + a spare regulator |
| Serial (option) | UART **AY-5-1013**, MITS Rev 0/Rev 1 layout (status port even, data odd); 74163/7493 baud divider; 20 mA TTY loop or EIA RS-232 headers |
| PROM (option) | up to eight **1702** (256×8) = 2 K memory-mapped; needs an added **−9 V** regulator; optional wait-state circuit |
| Power | **+8 V @ 0.35 A** typical (parallel build); +5 V from a 7805 (−9 V added for PROM, ±16 V for serial) |

---

## Address decoding — DIP switch U7

One 8-position DIP switch (**U7**) sets the card's base address. Only the **five left-hand poles**
are used; the leftmost pole is the **most significant** address bit. The switch selects a
contiguous **block of eight** addresses; which of the eight a given access hits is decoded from the
low three address bits by **U3** (a 7442 / 74L42), whose eight outputs (pins 9, 7, 6, 5, 4, 3, 2, 1
— pin 9 = least-significant / first, pin 1 = most-significant / last) are the individual
port/chip selects. Each committed device's select pin is **jumpered** to the U3 output for the
port it should answer. `OFF` may be silk-screened "open"; `ON` = "closed".

### As an I/O card (§3.1.1)

The five poles decode **A7–A3**; the block covers eight consecutive I/O ports.

| Ports | A7 | A6 | A5 | A4 | A3 |
|---|---|---|---|---|---|
| 00–07 | open | open | open | open | open |
| 08–0F | open | open | open | open | closed |
| A0–A7 | closed | open | closed | open | open |
| F8–FF | closed | closed | closed | closed | closed |

Within the block, U3 fans out to ports 0…7: **pin 9 = port 0**, pin 7 = 1, pin 6 = 2, pin 5 = 3,
pin 4 = 4, pin 3 = 5, pin 2 = 6, **pin 1 = port 7**. The committed input port **U8** and output
port **U10** are placed by tying **their pin 1 (DS1)** to the wanted U3 output.

### As a PROM card (§3.2.1)

The same five poles instead decode **A15–A11**, selecting a **2 K block** (eight 1702s of 256
bytes) anywhere in the 64 K range; the leftmost pole = A15.

| Decode | A15 | A14 | A13 | A12 | A11 |
|---|---|---|---|---|---|
| 0000–07FF | open | open | open | open | open |
| 0800–0FFF | open | open | open | open | closed |
| A800–AFFF | closed | open | closed | open | closed |
| F000–F7FF | closed | closed | closed | closed | open |

Within the 2 K block U3 fans out on 256-byte boundaries: pin 9 = base+0000, pin 7 = +0100, pin 6 =
+0200, … pin 1 = +0700. Each 1702's **chip-select (pin 14)** is tied to the wanted U3 output.

---

## Parallel I/O — the committed ports (§3.1.2)

Two 8212 latches are committed: **U8 = the input port**, **U10 = the output port**. Several key
8212 control lines are deliberately left **unstrapped** on the bare board so the ports can be
repurposed; to bring the plain parallel-in/parallel-out ports to life the manual lists these
jumpers:

| # | Jumper | Purpose |
|---|---|---|
| 1 | **SM** (U5 pin 12) → logic 1 (the 1 kΩ pull-up pad) | disable the memory-read option → the card responds as **I/O**, not memory |
| 2 | **sOUT** status (edge pin **45**, `SO`) → **SOUT** (U6 pin 5) | qualify output cycles with the S-100 output-status line |
| 3 | **sINP** status (edge pin **46**, `SI`) → **SINP** (U6 pin 9) | qualify input cycles with the S-100 input-status line |
| 4 | **OUT STB** → U10 pin 13 (**DS2**) | the output strobe that latches data into the output port |
| 5 | **INP STB** → U8 pin 13 (**DS2**); also **U8 pin 2 (MD) → ground** | the input strobe that enables the input port onto the bus |
| 6 | **CLR** (pin 14) of U8 / U10 → 1 kΩ pull-up (if unused) | park the unused 8212 clear line high |

The 8212 note on the schematic: **pin 24 = +5 V, pin 12 = ground**.

---

## Serial interface option (§3.3, supplemental drawing)

The board can instead be wired as a **single serial port**, and the manual supplies the schematics
and assembly detail for a **MITS Rev 0 / Rev 1** SIO clone. Its layout: the **status port at the
even address, the data port at odd** (base+0 / base+1). The status bits are strapped to match MITS:

- **Data-available:** D0 (negative-going) and D5 (positive-going).
- **Data-acknowledge / transmit-ready:** D7 (negative-going) and D1 (positive-going).

The serial build adds:

- **UART** — an **AY-5-1013** (40-pin) in the committed UART socket, fed by a 16 × baud clock.
- **Baud-rate generator** — a programmable divider off the S-100 2 MHz clock (bus pin 49), using
  two **74163** counters + a **7493**, strapped on a 16-pin header for **110 / 150 / 300 / 458.7
  ("Suding") / 600 / 1200** baud. (The strap value = 256 − 15625⁄baud; an **8097** buffer is
  *required by the Rev 1 version* so an output port could drive the straps in software.)
- **Line interface** — a **20 mA TTY current-loop** header (2N2907 drivers, teletype send/receive)
  **or** an **EIA RS-232** header (2N2222/2N2907 level shifters to a DB-25: TxD pin 2, RxD pin 3,
  ground pin 7).
- **Reset** — POC (power-on clear) through a 7404 to the UART reset.

---

## PROM card option (§3.2.2–3.2.3)

Configured as memory, the card carries up to **eight 1702** (256 × 8) PROMs, their address pins
A0–A7 tied to the bus address lines and data outputs DI0–DI7 to the bus, chip-selects from U3 (per
addressing above). Two extra requirements:

- **−9 V supply.** The 1702 needs −9 V; add an **LM320T-8 / 7908** regulator on the spare heat-sink
  pad left of the +5 V regulator, fed from the **−16 V** bus (with 56 Ω / 470 Ω trim + bypass caps).
- **Wait states** for slow PROMs, via an optional circuit (a **74L03** + **74L74** off PSYNC and
  bus pins 24/25, driving A.READY / bus pin 72; change CPU-board R18 to 2.2 K):

  | PROM speed | RA | RB | Wait states added |
  |---|---|---|---|
  | 0.5 µs | GND (or RC) | GND | **none** — circuit not required |
  | 1.0 µs | GND | RC | **one** (0.5 µs per byte) |
  | 1.5 µs | RC | RC | **two** (1.0 µs per byte) |

---

## Applications — parallel-port host clones (§4)

Short strapping recipes for making the committed **input** port (U8 at port `01`) plus a
one-bit **status** port (built from spare gates U4/U6 for port `00`, with an added **8097 / 74367**
buffer = **U11**) imitate specific hosts. The data-available bit differs per host:

| §   | Target | Key connection |
|---|---|---|
| 4.1 | **Altair Rev 0** | insert **A** (an added inverter) between U8 and U11; U11 pin 13 → **DI5** (edge pin 92) |
| 4.1 | **Altair Rev 1** | insert **B** (a direct connection) between U8 and U11; U11 pin 13 → **DI0** (edge pin 95) |
| 4.2 | **Processor Technology** | as Altair Rev 0, but U11 pin 13 → **DI6** (edge pin 93) |
| 4.3 | **IMSAI** | as Altair Rev 0, but: U4 pin 4 → U3 pin 5; U8 pin 1 → U3 pin 6; U11 pin 13 → **DI6** (edge pin 93) |

The keyboard/input device supplies a **positive DAV pulse** into U8 (pin 11, STB) to load a byte.

---

## Parts / chip complement

| Ref | Part | Role |
|---|---|---|
| U1 | 7485 / 74LS85 | 4-bit magnitude comparator — address block compare |
| U2 | 74LS04 | inverter |
| U3 | 7442 / 74L42 | BCD-to-decimal decoder — 1-of-8 port/chip select |
| U4 | 7486 | XOR — address compare glue |
| U5, U6 | 74L00 / 74LS00 | NAND glue — SM (memory-read disable), SINP/SOUT status qualify |
| U7 | 8-position DIP switch | base-address select (5 poles used) |
| U8 | 74S412 / **8212** | **committed parallel INPUT port** (also the serial UART's input 8212 in that build) |
| U10 | 74S412 / **8212** | **committed parallel OUTPUT port** |
| U11 | 8097 / 74367 *(user-added)* | one-bit status buffer in the §4 host-clone recipes |
| — | AY-5-1013 UART *(serial build)* | 40-pin UART; 16× baud clock |
| — | 2×74163 + 7493 *(serial build)* | programmable baud-rate divider |
| — | 8097 *(serial Rev 1)* | baud-strap buffer, "required by revision 1 version" |
| — | 1702A ×8 *(PROM build)* | 256×8 PROMs, memory-mapped |
| — | LM320T-8 / 7908 *(PROM build)* | −9 V regulator on the spare heat-sink pad |
| — | 74L03 + 74L74 *(PROM build)* | optional wait-state circuit |
| — | 7805 / 340T-5 | +5 V regulator |

**Sockets (parts list):** four 14-pin, four 16-pin, two 24-pin, two 16-pin headers; a 40-pin
socket for the UART. Passives: 6 × 0.1 µF ceramic, 9 × 2.2 K–4.7 K ¼ W, 1 × 1 K ¼ W, 2 filter
caps (10–39 µF).

**Power requirements:** +8 V @ 0.35 A typical (parallel build). High-grade glass-epoxy board,
gold-plated edge fingers, low-profile sockets throughout. **⚠ SSM shipped only the bare PC board**
— the warranty covers the board alone, not the parts a kit builder or assembler added.

---

## Header / edge references (from the schematics)

**Committed 8212 ports:** U8 = input, U10 = output. STB (data strobe) = pin 11; DS2 (enable) = pin
13; MD (mode) = pin 2; CLR = pin 14; DS1 (select) = pin 1; +5 V = pin 24; GND = pin 12.

**Status control (edge connector):** SO (output status) = pin 45; SI (input status) = pin 46.
Data-in bus lines used in §4: DI0 = pin 95, DI5 = pin 92, DI6 = pin 93.

**Address-decode fan-out (U3, 7442):** pin 9 = block+0, pin 7 = +1, pin 6 = +2, pin 5 = +3, pin 4 =
+4, pin 3 = +5, pin 2 = +6, pin 1 = +7.

**Serial baud strap:** header value = 256 − (15625 ⁄ baud rate); MITS clock at bus pin 49 (2 MHz),
UART fed the 16× clock.
