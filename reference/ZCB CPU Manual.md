# Vector Graphic ZCB — memory, ports, reset and jumpers of the single board computer

Source: [ZCB CPU Manual.pdf](#) and [ZCB CPU Manual (another).pdf](#)

Vector Graphic *ZCB Single Board Computer, Revision 1, User's Manual*, Revision B, June 11, 1980,
part 7200-0203-03-02, with a Field Manual Update of 2/27/81. The two scans are the same edition, and
each one has pages that the other one does not have. See §12. The file also uses `ReadMe.pdf` and
`mon40c (ZCB serial, IH).mac` from the deramp folder `using ZCB serial port`.

The ZCB is one S-100 board with a Z-80, 1K of RAM, three PROM sockets, an 8251 and an 8255. The
[Vector Graphic ZCB](Vector%20Graphic%20ZCB.md) file has the board revisions and the bulletins, and
this file does not repeat them. Chip detail of the 8251 is in
[Intel 8251 USART](Intel%208251%20USART.md).

**Emulated** as `zcb` (`src/boards/vector-zcb.h`, `docs/boards/vector-zcb.md`). The `vector` machine does not use it yet: `machines/vector.toml` has a generic Z-80 and a stand-in memory board.

---

## 1. Quick reference for emulation

| Item | Value | Source |
|---|---|---|
| Processor | Z-80A, 4 MHz. Later boards have a Z-80-B at 6 MHz | p. 2-1, sheet 1 |
| Reset | PHANTOM* on, and all of memory decodes as the E000 block, until the first memory cycle in E000–FFFF | §3 |
| PROM | E000–EFFF on boards with one 2732 at U20 | §4 |
| RAM | 1K at FC00–FFFF, two 2114s | p. 2-3 |
| On-board read | The board ignores the S-100 data-in lines. It does not need PHANTOM* | §5 |
| Serial | One 8251: 04 data, 05 status and command. A1 is not decoded, so 06 and 07 are the same chip | §6 |
| Parallel | One 8255: 08 port A, 09 port B, 0A port C, 0B control | §7 |
| 8251 clock | 2 MHz at CLK. S1 gives 16 times the baud rate at TxC and RxC | §6.2 |
| On-board interrupts | None. TxRDY and RxRDY of the 8251 have no connection | §8 |
| Wait state | One after each M1, as shipped | §9 |

---

## 2. Factory configuration (p. 2-1)

| Item | Setting |
|---|---|
| PROM | Two sockets for 2708, one socket for 2716. See ⚠ Q5 |
| PROM base | E000 |
| RAM | 1K at FC00 |
| Serial | 04 and 05, and the same chip at 06 and 07 |
| Parallel | 08, 09, 0A and 0B |
| Clock | 4 MHz |
| MWRITE | The board generates it |
| Wait state | One on each M1 |
| Boot | On-board PROM enabled on reset |
| PHANTOM* | Enabled on line 67 |

---

## 3. Reset and the jump to the Monitor (sheet 4)

The manual says that the first three bytes of the PROM are addressed after a reset, and that they
usually hold a jump to the fourth byte (p. 2-6, p. 3-1). The schematic shows how the board does this.
This section is a reading of sheet 4, and both scans draw that sheet the same way.

Half of U27 holds the state. Its D input is at ground.

1. **RESET* presets the flip-flop.** Q goes high and Q* goes low.
2. **Q high drives PHANTOM*.** It goes through U28 and the 8097 at U35 to jumper C and line 67.
3. **Q* low, through jumper B, enables U5 for every address.** U5 is the 74156 that selects the
   PROMs and the RAM. Its address inputs are A12, A11 and A10 only. Because of this, each 8K of
   memory reads as the E000–FFFF block. Address 0000 reads as E000.
4. **The first memory cycle in the block clocks the flip-flop.** MREQ and the area H match, through
   U13 and U39, clock D into U27. Q goes low and Q* goes high. PHANTOM* stops, and U5 needs the area
   H match again.

The processor starts at 0000 and reads `JP E0xx` from the PROM. The fetch at E0xx ends the override.
The manual's "first three bytes" is the normal case of this logic and not a counter. A longer program
could run in the mirror at 0000 for as long as it stays out of E000–FFFF. A read or a write in the
block ends the override. Nothing on the board writes the program counter.

| Jumper | Pads | As shipped | Function |
|---|---|---|---|
| B | Both | Joined | Step 3 above. Cut it, and the processor reads off-board memory at 0000 |
| C | Both | Joined | PHANTOM* to line 67. Cut it, and the board never drives the line |

**Cut B and leave C joined, and PHANTOM* holds until the first cycle in E000–FFFF.** The manual does
not give this case. To boot from off-board memory, cut both jumpers.

**Reset sources.** U28 drives RESET* and POC* together. Power-on drives them through C15. PRESET*,
line 75, lets the first half of U27 clock on M1, and that fires the 74121 at U18. The reset of a
running machine therefore starts at the next M1. RESET* goes to the processor, to U27, and through
U34 to RESET of the 8251 and the 8255.

---

## 4. PROM and RAM decode (p. 2-3 to 2-6, sheet 4)

U5 divides one block into eight lines. Area F joins each line to a chip. Several lines can go to one
chip, and that is how a 2K or a 4K PROM is mapped on the 1K lines.

| Area F pad | U5 output | 1K, in the E000 block | 2K | 4K |
|---|---|---|---|---|
| 14 | 2Y0 | E000–E3FF | 0000–07FF | 0000–0FFF |
| 13 | 2Y1 | E400–E7FF | 0800–0FFF | 1000–1FFF |
| 12 | 2Y2 | E800–EBFF | 1000–17FF | 2000–2FFF |
| 11 | 2Y3 | EC00–EFFF | 1800–1FFF | 3000–3FFF |
| 10 | 1Y0 | F000–F3FF | 2000–27FF | 4000–4FFF |
| 9 | 1Y1 | F400–F7FF | 2800–2FFF | 5000–5FFF |
| 8 | 1Y2 | F800–FBFF | 3000–37FF | 6000–6FFF |
| 7 | 1Y3 | FC00–FFFF | 3800–3FFF | 7000–7FFF |

The 2K and 4K columns are offsets from the base of the chart below.

| Area F pad | Connects |
|---|---|
| 1 | U20, PROM 0 |
| 6 | U21, PROM 1 |
| 2 | U22, PROM 2 |
| 3 | U24 and U25, the RAM |
| 4, 5 | Memory disable, §4.1 |

**Base address.** Areas H and I set the block. The factory block is E000–FFFF.

| PROM type | Block | Base addresses | Area H | Area I | Area J |
|---|---|---|---|---|---|
| 2708 | 8K | Each 8K | E000: 1-6, 2-4, 3-8 | 3-6, 4-7, 5-8 | 2-4, 5-7 |
| 2716 | 16K | 0000, 4000, 8000, C000 | C000: 1-2, 2-4, 3-8 | 8-4, 7-3, 6-2 | 3-4, 6-7 |
| 2732 | 32K | 0000, 8000 | 8000: 1-2-3, 3-8 | 8-3, 7-2, 6-1 | 3-4, 1-7 |

With a 2732 setup, the 1K of RAM takes up 4K of address space. The first 1K holds the RAM and the
other 3K is blank (p. 2-6).

Areas J, L and M take A10, A11 and the program voltages to pins 19 and 21 of the three sockets. The
Field Manual Update gives this caution: changing the PROM type means rewiring these power lines, and
a chip in a socket that is wired for another type can be destroyed. The update also says that the
schematic has pads 6 and 7 of areas L and M reversed, and that pad 7 of area J joins pad 5 by a trace
outside the jumper area.

### 4.1 Memory disable (p. 2-4, sheet 4)

Pads 4 and 5 of area F go to the NAND at U17, and its output goes to U28, the same gate that drives
PHANTOM* after a reset. A jumper from pad 4 or pad 5 to an address pad makes the board assert
PHANTOM* for that range. The range is 1K with 2708 decode, 2K with 2716 decode and 4K with 2732
decode. The manual gives this as a way to let an auxiliary disk controller or a video board answer
in place of the main memory. It uses the pad as a select for the phantom line. It does not switch
off a chip on the ZCB.

---

## 5. On-board memory and the S-100 bus (sheet 2)

U3 is an eight-input NAND of ROM0*, ROM1*, ROM2*, RAM*, PAR* and SER*. Through U4 and U42 it shuts off
the data-in buffers during a read of anything on the board. The address and the status of the cycle
still go out on the bus, so a memory board at the same address can respond. The ZCB does not look at
its data.

| Cycle | On-board chip selected | S-100 bus |
|---|---|---|
| Memory read | The chip supplies the byte | Data-in ignored |
| Memory write, RAM | The RAM takes the byte | The write goes out too, so a memory board at that address takes it |
| Memory write, PROM | Nothing. ROMn* is gated with RD at U4 | The write goes out too |
| I/O read of a board port | The chip supplies the byte | Data-in ignored |

This is why the board runs with a 64K board under it and does not assert PHANTOM* in normal use. The
[memory map](Vector%20Graphic%20Memory%20Maps.md) calls that board's range 0000–DFFF "user
available", and it does not say what the 64K board does above DFFF.

---

## 6. Serial port (p. 2-7, p. 2-11, sheets 6 and 7)

| Port | Read | Write |
|---|---|---|
| 04 | Data | Data |
| 05 | Status | Mode and command |
| 06 | Same as 04 | Same as 04 |
| 07 | Same as 05 | Same as 05 |

MA0 is C/D of the 8251. MA1 goes only to the 8255, so A1 is ignored and there is one chip. The
manual calls the upper pair an echo that duplicates the first pair (p. 2-7). `ReadMe.pdf` says the
same. Status uses the normal 8251 bits: `mon40c` tests D1 for receive and D0 for transmit.

**Monitor 4.3 programs this chip twice.** `INIT` sends 00, 00, 00, 40, CE and 27 to port 3, then to
port 5, then to port 7, and only the last two reach the ZCB. The first pass starts from the reset
state. In that state 00 is a mode word for synchronous mode, and the next two bytes are its sync
characters. The 40 is then the internal reset command. The second pass starts in command state. The
three 00 bytes are commands, and the 40 is again the internal reset. Both passes end with mode CE
(8 bits, no parity, 2 stop bits, clock factor 16) and command 27.

Ports 3, 5 and 7 are also the three 8251 status ports of a Bitstreamer II. On that board, each pass
of `INIT` reaches a different chip.

### 6.1 Port address (p. 2-8, sheet 6)

Area G sets a group of 32 ports. Area E sets the base of each device in that group, in steps of 4.
The two devices must be in the same group, because A7 to A5 feed one decoder. Pad 1 of area E is the
serial select and pad 2 is the parallel select.

| Area E pad | Group I | Others |
|---|---|---|
| 3 | 00 | Add the group base |
| 4 | 04, serial as shipped | |
| 5 | 08, parallel as shipped | |
| 6 | 0C | |
| 7 | 10 | |
| 8 | 14 | |
| 9 | 18 | |
| 10 | 1C | |

**Beside a Bitstreamer II.** A Bitstreamer II at 02 decodes 04-07 and 08-09, so it conflicts with both
devices at their shipped ports. Real machines resolved this at U2. Sheet 6 shows U2 as the 74LS42 port
decoder, and it is in a socket. Its pin 2 (output 1) feeds SER* through area E, and its pin 3 (output 2) feeds
PAR*. SER* and PAR* have 1K pull-ups. Lifting pin 2 stops the 8251. Pulling U2 stops the 8251 and the
8255. Pads 6 and 9 of area E give 0C and 18. The table in the Port Map lists neither, but its note puts the
time share system at 08-0F.

| Group | Ports | Area G |
|---|---|---|
| I | 00–1F | 9-6, 8-4, 7-2 |
| II | 20–3F | 9-5, 8-4, 7-2 |
| III | 40–5F | 9-6, 8-3, 7-2 |
| IV | 60–7F | 9-5, 8-3, 7-2 |
| V | 80–9F | 9-6, 8-4, 7-1 |
| VI | A0–BF | 9-5, 8-4, 7-1 |
| VII | C0–DF | 9-6, 8-3, 7-1 |
| VIII | E0–FF | 9-5, 8-3, 7-1 |

The decode uses MREQ and A7 to A2. It does not use IORQ. RD and WR of the two chips come from the
INP and OUT status lines (sheet 7), so a memory cycle does not reach them.

`ReadMe.pdf` says that the port can also be jumpered to 00 and 01, with a copy at 02 and 03, after a
trace is cut. It adds that this conflicts with the Flashwriter keyboard port at 00 and 01.

### 6.2 Baud rate (p. 2-11, p. 3-2, sheet 6)

S1 has eight rockers, and one rocker must be on. CLK of the 8251 is the 2 MHz clock. S1 feeds TxC and
RxC. The labels assume a clock factor of 16. With a factor of 64 the rate is a quarter of the label.

| Rocker | Label | Rate at TxC and RxC | Baud with factor 16 |
|---|---|---|---|
| 1 | 9600 | 2 MHz ÷ 13 = 153.85 kHz | 9615 |
| 2 | 4800 | ÷ 2 | 4808 |
| 3 | 2400 | ÷ 4 | 2404 |
| 4 | 1200 | ÷ 8 | 1202 |
| 5 | 600 | ÷ 16 | 601 |
| 6 | 300 | ÷ 32 | 300 |
| 7 | 150 | ÷ 64 | 150 |
| 8 | 110 | The 1200 rate ÷ 11 | 109 |

With all rockers open the serial port has no clock and does not work. The versions marked A/M in the
chart of §11 have a wire to 9600 and no switch.

### 6.3 RS-232 lines (p. 2-10, p. 2-16, p. 2-17, sheet 7)

The port is wired as Data Communications Equipment, for a terminal or a printer. A modem needs a
cable that crosses lines 2 and 3. Connector J2 is a 16-pin socket.

This table follows sheet 7. See ⚠ Q8 for the table on p. 2-16.

| J2 pin | DB-25 | RS-232 name | Direction | 8251 pin |
|---|---|---|---|---|
| 15 | 2 | Transmitted Data | Into the board | RxD, through U16 |
| 14 | 3 | Received Data | Out of the board | TxD, through U15 |
| 13 | 4 | Request to Send | Into the board | CTS*, through U16 |
| 12 | 5 | Clear to Send | Out of the board | RTS*, through U15 |
| 11 | 6 | Data Set Ready | Out of the board | DTR*, through U15 |
| 7 | 20 | Data Terminal Ready | Into the board | DSR*, through U16 |
| 10 | 7 | Signal Ground | | |

The board does not need a handshake line from the peripheral (p. 2-12). R22 and R23 pull lines 4 and
20 up to +5 V, so with the line open CTS* and DSR* are both active. Status D7 then reads 1, and the
8251 can send. If line 4 goes off, the 8251 does not send (p. 2-17). Command 27 from the Monitor turns
on DTR and RTS, and these are the +12 V that the manual says standard software puts on lines 5 and 6.
The schematic of 6-18-81 adds R10, 4.7K from line 8 at J2 pin 9 to +5 V. See MZ-0034 in the
[Vector Graphic ZCB](Vector%20Graphic%20ZCB.md) file.

---

## 7. Parallel ports (p. 2-7, p. 2-18, p. 2-19, sheet 7)

| Port | Register |
|---|---|
| 08 | Port A |
| 09 | Port B |
| 0A | Port C |
| 0B | Control |

U11 is an 8255. Outputs are latched and inputs are not latched. The levels are TTL. MA0 and MA1 go
to A0 and A1. RESET goes to pin 35. Connector J1 has 34 pins, and the manual lists them on p. 2-19.
The manual says that the 8255 has three modes and leaves the detail to Intel.

| J1 pins | Lines |
|---|---|
| 13, 16, 15, 18, 17, 20, 19, 22 | PA0 to PA7 |
| 26, 25, 28, 27, 30, 29, 32, 31 | PB0 to PB7 |
| 5, 6, 3, 4, 8, 7, 10, 9 | PC0 to PC7 |
| 2 | Ground |
| 34 | +5 V |

Port C is not wired to the processor. The 8255 has no interrupt path on this board.

**The Sprint 3 and the MP printer use J1** through an interface board in the connector: ZCB-Sprint 3
(P/N 42-3200) and ZCB-MP (P/N 12-3400). In the MZ the interface board aims toward the rear, and in the
Vector 3 toward the front (Field Manual Update).

**The real software.** The printer driver in the top of Monitor 4.3 is the oracle for this chip. It
is in the ROM and not in the listing, so these lines come from a disassembly of `VGMON43.BIN`:

| Address | Code | Meaning |
|---|---|---|
| EC3C | `IN 09`, `AND F2`, `CP E2` | Before it writes the mode, the driver reads port B to look for one pattern |
| EC44 | `LD A,89` then `OUT 0B` | If the pattern is absent, it sets mode 0, A output, B output, C input |
| EEFC | `OUT 09`, `AND 8F`, `OUT 09` | Strobes the high bits of port B |
| EF76, EF81 | `OUT 08`, `OUT 09` | Data |
| EF07 | `IN 09`, `IN 0A` | Status |
| EFF2 | `BIT 7,(FCCE)` | The flag from EC49. Set: status comes from port B and the data is complemented. Clear: status comes from port C and the data is true |

Only mode 0 and whole-byte writes appear. A byte search of the CP/M 2.22 H disk `VGBOOT.vgi` finds the
same `LD A,89` and `OUT 0B`. A byte search is not a disassembly. No source says what the pattern E2
at EC40 detects.

---

## 8. Interrupts (specifications, sheets 1, 2 and 7)

| Line | Path |
|---|---|
| INT*, line 73 | U36 to INT of the processor |
| NMI*, line 12 | U36 to NMI of the processor |
| HOLD*, line 74 | U35 to BUSRQ of the processor |
| INTA | From IORQ and M1, opens the data-in buffers so that a board can supply a vector |

The processor runs modes 0, 1 and 2. **The board has no interrupt source of its own.** Sheet 7 marks
TxRDY, RxRDY, TxEMPTY and SYNDET of the 8251 as no connection. See ⚠ Q4.

---

## 9. Other jumpers

| Area | As shipped | Function | Page |
|---|---|---|---|
| A | 1 to 2 | Clock. 4 MHz. For 2 MHz, cut it and join 1 to 3. The 6 MHz boards differ. See [Vector Graphic ZCB](Vector%20Graphic%20ZCB.md) | 2-1 |
| D | Joined | The board generates MWRITE, line 68. Cut it if a front panel generates MWRITE | 2-2 |
| N | 1 to 2 | Wait states. 1 to 2: one after M1. 1 to 3: none. Open: one on each bus cycle | 2-2 |
| O | 3 to 2 | I/O address mirroring. The port number also goes on A8 to A15, as on an 8080. With 1 to 2 the processor's own high byte goes out | 2-9 |

PRDY and XRDY stop the processor through the same logic as the automatic wait state (sheet 8).

---

## 10. Power and bus

The board takes +8 V at 970 mA typical, +16 V at 120 mA and −16 V at 80 mA. It has its own
regulators. Inputs are one TTL load. It supplies RFSH on line 66 for dynamic memory. The specifications
call the reset a "fast reset/power on clear".

---

## 11. Board versions (chart in the first scan, schematic 3502-XXXX-XX-XX)

This chart lists changes from schematic 3502 0001 01 00 of 6-18-81. A dash means no change.

| Assembly version | Version | S1 | U20, U21, U22 | Area A | Area F | Area J | Processor |
|---|---|---|---|---|---|---|---|
| 3501-0000-07-00 | 4 MHz with 3 sockets | Switch, socket | 2716, 2708, 2708 | Cut 1-2, join 3-2 | Cut 13-12-11, join 11-2, 12-6 | Cut 7-2, join 7-5 | A or B |
| -0001-05-01 | 4 MHz (A/M) with 3 sockets | Wire 9600 to 0 | 2716, 2708, 2708 | Cut 1-2, join 3-2 | Cut 13-12-11, join 11-2, 12-6 | Cut 7-2, join 7-5 | A or B |
| -0002-03-00 | 6 MHz | Switch, socket | 2732 | — | — | — | B |
| -0003-04-01 | 6 MHz (A/M) | Wire 9600 to 0 | 2732 | — | — | — | B |
| -0004-02-00 | 4 MHz single socket | Switch, socket | 2732 | Cut 1-2, join 3-2 | — | — | A or B |
| -0005-00-00 | 4 MHz X-CPM | Switch, socket | 2732 | Cut 1-2, join 3-2 | Cut 11-12, join 9-10-5 and 3-4 | — | A or B |
| -0006-00-00 | 6 MHz X-CPM | Switch, socket | 2732 | — | Cut 11-12, join 9-10-5 and 3-4 | — | B |
| -0007-00-01 | 4 MHz (A/M) single socket | Wire 9600 to 0 | 2732 | Cut 1-2, join 3-2 | — | — | A or B |

**Reading the X-CPM row.** With the pad table of §4, cut 11-12 takes EC00–EFFF off U20. Join 9-10-5
asserts PHANTOM* for F000–F7FF. Join 3-4 asserts it when the RAM is selected. Neither scan explains
"X-CPM" or "A/M".

The later schematic has the same reset logic as sheet 4. Its other additions are the Y1÷3 clock for
12 MHz and the read delay at U26, in the [Vector Graphic ZCB](Vector%20Graphic%20ZCB.md) file.

---

## 12. The two scans

| | `ZCB CPU Manual.pdf` | `ZCB CPU Manual (another).pdf` |
|---|---|---|
| Pages | 52 | 73, with blank pages |
| Text | None, images only | OCR, poor |
| Pages 2-8 and 2-14 | **Missing** | Present |
| Sheets 1 to 9 and the assembly drawing | Present | Present, and cleaner |
| Schematic of 6-18-81, 3 PDF pages | Present, at the front | Absent |
| Version chart of §11 | Present, at the end | Absent |
| Field Manual Update | Present | Present |

Sheets 1 to 9 are Revision 2, 7/16/80, and sheet 5 also has the date 2/27/81. The manual text is for
the Revision 1 board. Both facts matter, because the text and the sheets do not describe one board.

---

## Quirks worth carrying

- **⚠ Q1. Which of 04 and 05 is data.** P. 2-1 prints "04(control) and 05(data)". The specifications
  and p. 2-7 give data 04, control 05. **Data 04 won.** On sheet 7 MA0 is C/D, so the odd port is
  control. `mon40c` has `COND EQU 04H` and `CONS EQU 05H`, and Monitor 4.3 `E` reads status at 5.
- **⚠ Q2. No second serial port.** The [Port Map](Vector%20Graphic%20Port%20Map.md) lists "Serial
  port A1" at 06 and 07. The manual, the ReadMe and sheet 7 show one 8251 with A1 not decoded. **One
  chip won.** Words about "each serial port" and "the ZCB II board" on p. 2-11 and p. 3-3 are likely
  text from the Bitstreamer II manual.
- **⚠ Q3. Any increment of four.** The specifications say that a base can be any increment of four
  from 00 to FF. P. 2-7 and p. 2-8 add that both bases must be in one group of 32. **The group rule
  won**, because sheet 6 feeds both selects from one decoder.
- **⚠ Q4. Interrupt from the 8251.** P. 2-18 and p. 2-14 say that software can be interrupted by the
  TxRDY pin. Sheet 7 marks the pin as no connection, and the schematic of 6-18-81 shows no wire. **No
  interrupt won.** A user would have to add a wire and a path to the bus.
- **⚠ Q5. The PROM table.** The manual gives four layouts. P. 2-1 and p. 2-3 give 2708s at E000, E400
  and EC00. The Appendix gives U22 as an 8316E302 at E000, with U20 at E800 and U21 at EC00. The update
  of 2/27/81 gives U20 as a 2716 at E000, U21 at E800 and U22 at EC00. The chart of §11 and V3-0022 add
  the single 2732. These are different boards, and no one layout won. The
  [Vector Graphic ZCB](Vector%20Graphic%20ZCB.md) file identifies each one. Monitor 4.3 needs 4K in one
  range, so it fits a 2732, or a set of PROMs that fill E000–EFFF.
- **⚠ Q6. A Monitor that alternates ports fails on this board.** `mon40c` writes each byte once to
  port 5, and it keeps `;OUT (05H),A` and `;OUT (07H),A` as comments. If software sent each byte to 05
  and to 07 in turn, the one chip would get each byte twice. The second 40 would then be a mode word
  for synchronous mode, and CE would be a sync character. Monitor 4.3 sends all six bytes to one port
  before it moves on, so the mirror does no harm (§6).
- **⚠ Q7. An unconnected 8255 input.** Neither the manual nor the schematic says what port A, B or C
  reads with nothing on J1. No pull-up is drawn. The driver at EC3C reads port B before it sets a mode,
  so this value decides which of the two paths of §7 the driver takes.
- **⚠ Q8. The handshake table.** P. 2-16 joins J2 pin 13 to U15 pin 11 and calls it RTS, and it joins
  J2 pin 12 to U16 pin 1 and calls it CTS. J2 pins 11 and 7 have the same fault. Sheet 7 joins J2 pin
  13 to U16 pin 1, the input to CTS*, and J2 pin 12 to U15 pin 11, the output of RTS*. **Sheet 7 won.**
  P. 2-14 agrees with it, because it says that incoming line 4 reaches the CTS input. P. 2-12 agrees
  too, because software holds lines 5 and 6 on. The later schematic draws the same pins.
- **⚠ Q9. U22 pin 3.** P. 2-18 gives the output of the spare RS-232 driver as U22 pin 3. U22 is a
  PROM socket. The spare driver is in U15, so this is a slip.
- **⚠ Q10. The serial clock is not exact.** The label 9600 is 9615 baud, because 2 MHz ÷ 13 ÷ 16 is
  9615. The manual gives the same divide and calls 13.02 "rounded to 13".
- **⚠ Q11. U number of the 8251.** The manual and the sheets use U6, the 19,200 baud note uses IC6, and
  the schematic of 6-18-81 prints AM9551. The note asks for an 8251A or a 9551. These are one socket.
