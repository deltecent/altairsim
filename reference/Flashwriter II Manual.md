# Vector Graphic Flashwriter II — 80 × 24 memory-mapped video board with keyboard port

Source: [Flashwriter II Manual.pdf](#)

Vector Graphic Inc., *Flashwriter II Board*, Manual Revision 3, March 29, 1979, 34 PDF pages. This
S-100 board shows 24 rows of 80 characters from 2K of on-board RAM. It also holds the parallel
keyboard port, so the screen and the keyboard of a Vector Graphic machine are one board.

**Not emulated.** This file holds what an implementer needs. It gives the addresses, status bits,
keyboard flag, interrupt, character codes, raster timing, wait states, jumpers and board revisions.
It omits monitor adjustment, video cabling and the blank character forms. **The dot patterns of the
character set are not transcribed** (Q15).

Other sources in this file:

| Citation | Scan | What it is |
|---|---|---|
| *sheet* | [Flashwriter II Schematic Rev 4.tif](#) | *80 x 24 Video Display Board, Rev 4*, drawing 3515-0000-04-00, 3 Nov 80 |
| `VTA`, `PIB` | [Flashwriter II - Misc.pdf](#) | Technical Advisories and Product Improvement Bulletins, 1979–1981 |
| *Port Map* | [Port Map.pdf](#) | *Port Map*, Revision A, July 10, 1981 |
| *Monitor 4.3* | `mon43.bin` | Vector Graphic Monitor 4.3, a 4K image for E000H. Read by disassembly. It is a first-hand artifact and is not in the tree |

Citations with no name are printed manual pages, such as `3-10`. **Schematic page 4-7 is not in the
manual scan.** The sheet above replaces it, but it is later than the manual (§10). Facts from the
sheet are marked *sheet*.

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Screen | 24 rows of 80 characters, 8 × 10 dots (1-1) |
| Video RAM | 2K, read and write, at any 2K block from C000H to F800H. 1920 bytes show, and the last 128 bytes do not (3-2) |
| Address with Monitor 4.3 | **F000H–F7FFH.** The screen is F000H–F77FH. F800H–FFFFH is not decoded by this board (*sheet*) |
| Address in the manual | D000H–D7FFH, for Monitor 3 (3-2) |
| Keyboard status | **Port 00H, input** |
| Keyboard data | **Port 01H, input.** The status port is always the lower address (2-1) |
| Key waiting | Bit 6 = 1 and bit 0 = 0 (3-10) |
| Vertical blanking | Bit 5 = 0 during lines 240–261 (3-10, *sheet*) |
| `OUT` to 00H or 01H | Ignored. The port decode needs `SINP` (*sheet*) |
| Clear the key flag | `IN 01H` (§4) |
| Interrupt | The key flag drives `PINT` (pin 73) when area L is fitted. No vector byte (§4) |
| Character | Bits 0–6 select one of 128 characters. Bit 7 = 1 is reverse video (3-9) |
| Cursor | None in hardware. Monitor 4.3 flips bit 7 of the cursor cell (§5) |
| Wait states | A processor access to video RAM holds `PRDY` low until retrace (§7) |
| Reset | `POC*` (pin 99) drives the 8212 `CLR` pin. Video RAM has no reset (*sheet*) |

---

## 2. Memory map

The decode is U26, a 7442, and half of U27 (*sheet*). A15 and A14 must be 1, and `SINP` and `SOUT`
must both be 0. A13, A12 and A11 then select one of eight outputs. A wire in area F takes one output
to the RAM (pad 2). A second wire can take one output to the on-board PROM (pad 1).

| Area F pad | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|
| Base (3-2) | C000 | C800 | **D000** | D800 | E000 | E800 | **F000** | F800 |

The manual ships pad 5. The Rev 4 sheet draws pad 9. The RAM decodes one 2K block with no mirror, and
it does not look at `PHANTOM*` (*sheet*).

The RAM is four 2114 chips, U17, U18, U31 and U32. Each pair holds 1K × 8, and A10 selects the pair
(*sheet*).

---

## 3. Port map and status bits

The decode is U39, a 74LS42, with half of U27 and U38 (*sheet*). A7–A4 must be 0, and `SINP` and
`PDBIN` must be 1. A3, A2 and A1 select one of eight pairs. A wire in area J picks the pair. A0 then
selects `INPS` (even) or `INPD` (odd). The decode is **input only**.

| Area J pad | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|
| Ports (3-4) | **00, 01** | 02, 03 | 04, 05 | 06, 07 | 08, 09 | 0A, 0B | 0C, 0D | 0E, 0F |

**Status port.** U21, an 8097, drives three bits only (*sheet*):

| Bit | Source | Reads |
|---|---|---|
| 6 | U46 `INT*`, inverted by U36 | 1 = a key waits |
| 5 | `V BLANK` | 0 = lines 240–261, 1 = at all other times |
| 0 | U46 `INT*` | 0 = a key waits |
| 7, 4, 3, 2, 1 | Nothing | Not driven. Software must mask them |

altairsim's bus floats to FFH, so the undriven bits would read 1. The status byte is then:

| | Active lines | Lines 240–261 |
|---|---|---|
| No key | BFH | 9FH |
| Key waits | FEH | DEH |

The real value of the undriven bits depends on the bus. The table is for the simulator.

**Data port.** U45, an LS244, puts the U46 latch on the bus (*sheet*). The eight key bits go through
with no inversion, and bit 7 is not masked.

**The rest of the machine** (*Port Map*, 1981):

| Port | Board |
|---|---|
| 00–01 | Flashwriter I or II keyboard. Also the Bitstreamer I parallel port and the Analog Interface |
| 02–07 | Bitstreamer II serial ports A, B and C (data at even, status at odd). The ZCB has serial ports at 04–07 |
| 08–0B | Bitstreamer II and ZCB parallel ports |
| 40 | Memory bank select, [64K Dynamic RAM](Vector%20Graphic%2064K%20Dynamic%20RAM.md) and 16K Static RAM |
| 82–87 | Clock/Calendar |
| C0–C3 | [Dual-Mode Disk Controller](HD-FD%20documentation.md) |
| E0–E7 | The separate Floppy Disk Controller |

---

## 4. Keyboard and interrupt

U46 is an 8212 latch with a service-request flip-flop (*sheet*). `MD` is grounded and `DS1*` is
grounded. `INPD` drives `DS2`.

1. The keyboard strobe latches the eight data bits and sets the flag. `INT*` goes low (2-1).
2. Status bits 0 and 6 change.
3. If area L is fitted, `INT*` also drives `PINT` (pin 73) low through U36 and U10, a 7406.
4. `IN` from the data port selects U46. The processor read clears the flag.

The latch behavior in step 4 follows the 8212 notes in
[SSM IO-4](SSM%20IO-4%202P%2B2S%20IO%20Board.md) §3.2.1. **`IN` from the status port does not clear
the flag**, because `INPS` does not reach U46. A new key overwrites the latch. The board has no
buffer past one byte.

`PINT` is a level. It stays low until `IN 01H`. The board has no decode for `SINTA`, so it does not
put a vector on the bus (*sheet*). The acknowledge reads the floating bus, which is FFH, or `RST 7`.
Monitor 4.3 writes `C3` to 0038H at reset. The target address at 0039H is left to the guest.

The strobe edge is set by area H (3-5). The keyboard on the Mindless Terminal has the rising edge,
which is the edge as shipped. The keyboard itself is a plain parallel source, with no scan logic
on this board.

**J1 pins** (3-13):

| DATA 1 | DATA 2 | DATA 3 | DATA 4 | DATA 5 | DATA 6 | DATA 7 | DATA 8 | `PRESET*` | STROBE | +5 V | GND | −V (option) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 7 | 17 | 8 | 14 | 20 | 15 | 16 | 19 | 11 | 12 | 24 | 13 | 9 |

**Monitor 4.3 and bit 7.** Monitor 4.3 maps five key codes before it masks bit 7:
`E1`→`5D`, `F1`→`5B`, `A1`→`7E`, `B1`→`5C`, `60`→`15` (table at E15B). Codes with bit 7 set therefore
reach the port on the keyboard of that machine.

---

## 5. Video RAM and character codes

The first byte is the top left cell. Bytes go left to right, then to the left edge of the next row
(3-8). The row stride is 80 bytes. The address of a cell is:

```
base + 80 × row + column      row 0–23, column 0–79
```

The display reads the RAM through U16, a 74S471 "position ROM". U16 maps H4–H6 and V0–V4 so that the
screen is linear in memory (2-2). Its contents are not in any scan, and a simulator does not need
them.

As shipped, bit 7 is reverse video and bits 0–6 select the character (3-1, 3-9):

| Code | Shown |
|---|---|
| 00–1F | 32 graphics characters |
| 20–7F | 96 ASCII characters |
| 80–9F | 00–1F in reverse video |
| A0–FF | 20–7F in reverse video |

With the 256-character jumpers, all 8 bits select the character and reverse video is lost (3-2).
Reverse video is U7, an XOR of the dot stream with the delayed bit 7 (*sheet*).

There is no hardware cursor, and the board has no control codes. Every byte is a character. Codes
such as Control-T are in the Monitor's driver, not on this board (3-7, 3-9).

**Monitor 4.3 cursor.** Monitor 4.3 flips bit 7 of the cursor cell (E460–E479). It holds the column
at FFDB, the row at FFDC and the reverse-video state at FFDD. It scrolls by a block move of F050–F77F
to F000.

---

## 6. Raster timing

| Item | Value | Source |
|---|---|---|
| Dot clock | 14.318 MHz or 14.3347 MHz | 2-1, *sheet*, PIB 0054 |
| Character clock | Dot clock ÷ 8, 558 ns | 2-1, 2-3 |
| Horizontal counter | U29 and U3, loads 94, ends at 207 | 2-1, *sheet* |
| Line | 114 characters, 63.69 µs | 2-1 |
| Displayed | Counts 128–207, 80 characters. H7 = 1 | Q2 |
| Horizontal retrace | Counts 94–127, 34 characters, 19.0 µs | Arithmetic |
| Scan lines per row | 10 at 60 Hz | 2-2 |
| Vertical counter | 0 to 261, 262 lines | 2-2 |
| Displayed lines | 0–239 | 2-2 |
| Vertical sync | Lines 240–242 | 2-2 |
| Vertical blanking | Lines 240–261, 22 lines | *sheet* |

The arithmetic closes: 114 × 8 ÷ 14.318 MHz = 63.70 µs, and 8 ÷ 14.318 MHz = 558.7 ns.

| Crystal | Line | Field at 262 lines |
|---|---|---|
| 14.318 MHz | 63.70 µs | 59.92 Hz |
| 14.3347 MHz | 63.62 µs | 59.99 Hz |

Bit 5 of the status port gives the guest a 60 Hz clock (2-1).

**Area R, 50 Hz.** The manual does not describe area R. On the sheet, the pad sets when U6 reloads.
The 60 Hz wire takes L0 and U6 counts 0–9, which is 10 lines. The 50 Hz wire takes L1 and U6 counts
0–11, which is 12 lines. The total line count at 50 Hz was not traced. The sheet labels the crystals
14.318 MHz (50 Hz) and 14.3347 MHz (60 Hz).

---

## 7. Wait states

The processor and the display share the RAM. The board lets the processor in during horizontal
retrace only, which stops flashes on the screen (2-2).

The gates are in U12, with R13, R14, C17 and C18 as delays (*sheet*). `PRDY` is low while H7 = 1 or
H1 = 1. During retrace H7 = 0, so `PRDY` can rise only while H1 = 0. U25 drives `PRDY` only while the
board is selected by the RAM decode, so **only video RAM accesses wait**. The keyboard ports do not.

The longest wait is about the 44.7 µs of the displayed part of the line, plus the delay for H1. A
block move in video RAM is therefore slower than in plain RAM. A simulator that ignores the wait is
still correct in function, because the guest sees no flag for the wait.

`V BLANK` does not open the RAM. The wait follows H7 and H1 on all 262 lines.

---

## 8. Jumper areas

| Area | Function | As shipped | Change | Page |
|---|---|---|---|---|
| A | Vertical sync polarity | 1–2, inverted | 1–3, not inverted | 3-4 |
| B, E, I | Character set | 128 characters | 256 characters, with the cuts on 3-2 | 3-2 |
| C, K | Jump on reset | Open | C 1–2 and K 1–2 | 3-3 |
| D, M | PROM type | 2708: 1–4, 2–3 | 2716: 1–3, and 4 to M | 3-3 |
| F | RAM and PROM address | Pad 2 to pad 5, D000 | §2 | 3-2, 3-3 |
| G | Make `MWRITE` | Open | 1–2 if no other board makes it | 3-4 |
| H | Keyboard strobe edge | Fitted, rising edge | Cut for the falling edge | 3-5 |
| J | Keyboard ports | Pad 1 to pad 2, 00/01 | §3 | 3-4 |
| L | Keyboard interrupt | Open in the manual, **fitted from 1980** | Q3 | 3-5, PIB 0055 |
| N | Keyboard negative supply | Empty | Resistor and zener | 3-5 |
| O, P | Low intensity for bit 7 | Open | Wire O to P, *sheet* only | *sheet* |
| R | 50 or 60 Hz | 60 Hz | §6, *sheet* only | *sheet* |
| S | Board enable for multi-user | 1–2 by trace | Cut, wire to U39 pin 15 | VTA V3-0026 |

---

## 9. On-board PROM and jump on reset

Socket U42 takes a 1K 2708 or a 2K 2716 (3-3). The board ships with the socket empty. In a Vector
Graphic machine the Monitor is on the PROM/RAM board, not here (3-3).

The jump-on-reset flip-flop is two gates of U9 (*sheet*). `POC*` sets it. With C and K fitted, the
flip-flop forces the PROM on at every address and pulls `PHANTOM*` (pin 67) low. The first access to
the real PROM address clears it. The first three bytes of the PROM must be a jump to the fourth byte
(2-4, 3-3).

The board does not need this circuit in a machine with the Monitor on another board.

---

## 10. Board revisions

The manual describes the board of early 1979. The sheet is Rev 4 of Nov 1980. The bulletins go to
Rev 7. None of these changes touches the address, the port or the bit map.

| Reference | Date | Change |
|---|---|---|
| PIB 0024 | 1-18-79 | 100 pF between U19-7 and U19-8, video glitches |
| PIB 0038 | 8-7-79 | R13 and R14 become 560 Ω. The board did not work at 2 MHz. The sheet shows 560 |
| PIB 0039 | 8-9-79 | 150 pF on U2, decode glitch |
| PIB 0043 | 9-7-79 | 18 or 20 gauge wire to J2 |
| PIB 0054 | 1-29-80 | 14.3347 MHz crystal and resistor changes, for boards in the Vector 3 chassis. Reason: the MTI board is not needed, and "swimming video" |
| PIB 0055 | 2-1-80 | **Area L jumper on all boards**, effective February 4, 1980, so that keys answer under WMS software |
| VTA V3-0028 | Feb 20, 1980 | APL character PROMs on Rev 3 |
| PIB 0061 | 4/8/80 | 20-pin socket at U43, to test the PROM socket. U43 is the PROM's LS244 bus buffer (*sheet*) |
| PIB 0063 | 4-21-80 | 470 pF between U30-8 and U30-7, memory errors in the retrace wait circuit |
| PIB 0065 | 4-24-80 | Video resistor changes for the Vector 3 |
| VTA V3-014 | 4/22/81 | Rev 7. Removes C31, adds a 1K resistor and rewires `WRT` for 2114 timing |
| VTA V3-0026 | 8 Dec 1981 | Identifies #3514-0000 (4 MHz, or 4/6 MHz with 2114-2 RAM), #3514-0006 (6 MHz multi-user) and #3514-0008 (4/6 MHz) |

---

## 11. Hardware identification

| Item | Part (*sheet*) |
|---|---|
| Video RAM | U17, U18, U31, U32, 2114 |
| Character generator | U22, 6316E, or 2708/2716 as an option, top 8 lines |
| Descender PROM | U23, 2708 (option), bottom 2 lines |
| Position ROM | U16, 74S471 |
| Keyboard latch | U46, 8212 |
| Status drivers | U21, 8097 |
| Data bus buffers | U33, U44 (RAM), U43, U45 (PROM and keyboard), LS244 |
| Shift register | U8, 74LS166 |
| Horizontal counters | U15, U29, U3, 74LS161 |
| Vertical counters | U6, U4, 74LS161, and U5, 74LS109 |
| Crystal | Y1 |
| Connectors | J1 keyboard, 24-pin DIP. J2 video, 6-pin Molex |

J2 pins (3-12): 1 composite video, 2 ground, 3 TTL video, 4 horizontal sync, 5 vertical sync.

---

## Quirks worth carrying

Each ⚠ item is a conflict or a gap. The item says which reading won and what settled it.

- **Q1. Only three status bits are real.** Bits 0, 5 and 6 are driven. Software that compares the
  whole byte depends on the bus. Bit 0 and bit 6 carry one fact with two polarities (3-10, *sheet*).
- **⚠ Q2. H7 in the text is backwards.** 2-1 says H7 goes high at the end of the displayed line and
  acts as blanking. The sheet shows H7 as the top bit of a counter that loads 94 and ends at 207.
  U2 decodes the end from H7, H6 and the low `TC`. Only counts 128–207 give 80 characters with
  H0–H6 = 0–79. The sheet won: **H7 = 1 is the displayed part.** `H BLANK DLD` is H7 delayed, and it
  enables the video in U19. `PRDY` is low while H7 = 1. Both agree with the sheet.
- **⚠ Q3. The interrupt jumper.** 3-5 says area L is an option, and the manual ships it open.
  PIB 0055 fits it on every board from February 4, 1980. For a late machine the bulletin wins, because
  it is later. With area L fitted, `PINT` stays low until the guest reads port 01H.
- **⚠ Q4. Two default addresses.** The manual says D000H and warns that the Monitor assumes it (3-2).
  Monitor 4.3 uses F000H, and the Rev 4 sheet draws pad 9, which is F000H. Both are right for their
  period. The address is a jumper setting, so a board model needs a property for it.
- **⚠ Q5. The graphics count.** 1-1 says 16 special graphics characters, and 3-1 says 32. 3-1 won:
  128 − 96 = 32, and the chart on 4-1 has 32 cells in rows 0 and 1.
- **⚠ Q6. Reverse range.** 3-7 says reverse video characters run "from 8A to FF". 3-8 and 3-9 say 80
  to FF, and the chart on 4-2 starts at 80. 3-8 won. "8A" is a typing error.
- **⚠ Q7. The crystal.** 2-1 says 14.318 MHz. PIB 0054 changes the part for the Vector 3 chassis only.
  The sheet labels 14.318 MHz as 50 Hz and 14.3347 MHz as 60 Hz. At 262 lines the fields are 59.92 Hz
  and 59.99 Hz, and neither is near 50 Hz. The sheet's 50 Hz label fits a 12-line row, which the
  manual does not describe (§6). The 50 Hz total was not traced. ⚠ Not resolved.
- **Q8. No write decode.** An `OUT` to 00H or 01H reaches nothing here. Monitor 4.3 writes 8251 setup
  bytes to 03H, 05H and 07H, and those ports belong to the Bitstreamer II, not to this board.
- **⚠ Q9. U19 pin 12.** 2-2 says U19 pin 12 goes low at count 240. On the sheet that pin is the
  scan-line decode, and U2 pin 12 is `V BLANK`. The sheet won, because the drawing shows the
  240–262 waveform at U2 pin 12.
- **⚠ Q10. U6 is not a decade counter.** 2-2 calls it one. The sheet shows a 74LS161 that reloads after
  9 or 11, set by area R. The count is 10 at 60 Hz, as 2-2 says.
- **⚠ Q11. The character PROM.** 3-1 says two 2708 PROMs. The sheet shows U22 as a 6316E, with
  2708/2716 as options. The sheet is later, so both can be true. The dot patterns are the same chart.
- **Q12. Typing errors in the manual.** The contents list prints `6.8` for section 2.8, and it numbers
  two parts `I.`. Section 3.2.4 cites 3.1.6 for the port jumper and means 3.1.7.
- **Q13. The processor can read the screen.** Monitor 4.3 reads the cell to flip the cursor bit, and
  scrolls with a block move inside video RAM. The RAM is ordinary memory to a guest, apart from the
  wait.
- **Q14. The last 128 bytes.** F780–F7FF hold plain RAM (3-2). Monitor 4.3 stops its scroll at F780H,
  and its code has no absolute address in F781–F7FF. These bytes must still behave as RAM, because
  the manual offers them for any purpose.
- **⚠ Q15. The dot patterns are not in text form.** Pages 4-1 and 4-2 show them as drawings. No scan
  holds a PROM dump. U22 and U23 hold the pattern as 128 entries of 8 lines and 128 entries of 2 lines
  (3-1). The chart labels bits D7 to D0 across and R0 to R9 down. A board model needs either a
  dump or a transcription of the chart.
- **Q16. Flashwriter I software fails here.** The manual says the EVIOS driver does not work on this
  board (1-2). The Flashwriter I had a separate attribute block, and this board does not.
