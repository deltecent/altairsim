# CompuPro M-Drive/H — 512 K solid-state disk emulator

Source: [CompuPro M-Drive Manual.pdf](#)

CompuPro (a Godbout company) *M-DRIVE/H Technical Manual*, board **197B**, Document #11978,
© 1982, First Printing December 1982, Second Printing March 1983 (cover dated 3/83). The scan
has 21 pages and no text layer. PDF page N is printed page N+1 from PDF page 2 on; printed
pages 2, 22 and 23 are not in the scan (the contents page puts the warranty on p.24; it is the
last PDF page, with no number). The board is 512 K bytes of dynamic RAM behind **two I/O
ports**. It takes no memory space. The software uses it as a RAM disk, drive `M:`. Up to eight
boards share the same two ports and form one 4 MB space. This file omits the warranty, the
upgrade address and the power-supply detail. **Not emulated.**

## 1. Quick reference for emulation

| Item | Value | Page |
|---|---|---|
| Ports | Two, `base` (A0=0) and `base+1` (A0=1). Standard `C6H`/`C7H`. | 5, 8, 11 |
| `base` (`C6`) — data | `IN` reads the byte at the address counter, `OUT` writes it. Each access then counts the address up by one. | 7, 15 |
| `base+1` (`C7`) — address | `OUT` only. Three writes load the 22-bit address, **high byte first**. | 7, 15, 17 |
| Address width | 22 bits (MA0–MA21) = 4 MB. MA19–MA21 pick the board (cascade), MA0–MA18 the byte in the board's 512 K. | 7, 17 |
| Address decode | A1–A7 against S1 positions 1–7 (8-bit port address; A8–A15 not decoded). | 8, 17 |
| Board select | S1 positions 8, 9, 10 against MA21, MA20, MA19. | 6, 8, 17 |
| Memory | 64 × 4864 (64 K × 1) DRAM, 8 rows of 8 bits; Intel 8203-3 controller does the refresh. | 8, 18, 20 |
| Wait states | RDY held low from pSYNC until the 8203's XACK\*, only when this board is addressed. | 19 |
| Interrupts, DMA | None. | 17–19 |
| Power-up contents | Not defined (DRAM). The software formats the drive at cold boot. | 6, 9 |
| CP/M layout (shipped software) | Record (128 B) for track *t*, sector *s* (0-based) at `s<<16 | t<<7`; its parity byte at `s<<16 | t`. | 14–16 |

## 2. Ports and switch S1

The board has one 10-position DIP switch, S1. **The switch markings, not the legend, number the
positions** (pp.5, 6, 8).

Positions 1–7 select the port pair. Position *n* compares one address line (p.8; U18 25LS2521
on the schematic, p.17):

| S1 position | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Address bit | A7 | A6 | A5 | A4 | A3 | A2 | A1 |

**OFF = 1, ON = 0.** A0 is not switched; it selects the port in the pair: A0=0 is the data
port, A0=1 the address port (driver equates `HDATA EQU HBASE`, `HADDR EQU HBASE+1`, p.11).

Standard setting, `C6H`/`C7H` (pp.5, 8):

| Position | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Setting | OFF | OFF | ON | ON | ON | OFF | OFF |

That is `1100 011x` = `C6`/`C7`. The CompuPro CP/M 2.2 manual gives the same setting (see
[CompuPro CP/M 2.2 Technical Manual](CompuPro%20CPM-22%20Technical%20Manual.md) §5.4).

Positions 8–10 give the **cascade address**, a different value on each board (pp.6, 8–9). The
schematic (p.17) compares position 8 with MA21, 9 with MA20, 10 with MA19 (U17 74LS266
exclusive-NOR, open collector, wired together into BSEL). OFF = 1 here too (pulled up by SR1):

| Board | 8 | 9 | 10 | MA21..MA19 | Byte range in the 4 MB space |
|---|---|---|---|---|---|
| 1st | ON | ON | ON | 000 | `000000`–`07FFFF` |
| 2nd | ON | ON | OFF | 001 | `080000`–`0FFFFF` |
| 3rd | ON | OFF | ON | 010 | `100000`–`17FFFF` |
| 4th | ON | OFF | OFF | 011 | `180000`–`1FFFFF` |
| 5th | OFF | ON | ON | 100 | `200000`–`27FFFF` |
| 6th | OFF | ON | OFF | 101 | `280000`–`2FFFFF` |
| 7th | OFF | OFF | ON | 110 | `300000`–`37FFFF` |
| 8th | OFF | OFF | OFF | 111 | `380000`–`3FFFFF` |

"Up to eight M-Drive/H boards can exist at the same port addresses, only the cascade address
needs to be different for each board" (p.5).

## 3. The address counter

The address is held in six 74LS161/163 4-bit counters (U16, U15, U9, U8, U5, U4; p.17). They
are wired as a **shift register on load and a counter on count**:

| Counter | Load inputs | Outputs |
|---|---|---|
| U16 | DB0–DB3 | MA0–MA3 |
| U15 | DB4–DB7 | MA4–MA7 |
| U9 | MA0–MA3 | MA8–MA11 |
| U8 | MA4–MA7 | MA12–MA15 |
| U5 | MA8–MA11 | MA16–MA19 |
| U4 | MA12–MA13 | MA20–MA21 (outputs 11, 12 not connected) |

All six share LOAD\* and COUNT\*. So **each `OUT` to the address port shifts the address up by
one byte and puts the new byte in MA0–MA7**:

```
MA[21:16] <- MA[13:8]      MA[15:8] <- MA[7:0]      MA[7:0] <- data
```

After three writes: MA16–MA21 = bits 0–5 of the **first** byte (bits 6–7 are lost), MA8–MA15 =
the second byte, MA0–MA7 = the third. Fewer than three writes leave old bits in the high part.

COUNT\* adds one after each data-port access, read or write; the carry runs through all 22
bits, and the address wraps from `3FFFFF` to `000000` (U4's carry out is not used). The counters
are reached only through the data bus and the PAL, so the address port has no read path on the
schematic.

**Every board decodes the same two ports, so every board loads every address byte.** It must:
BSEL comes from the counter's own MA19–MA21, so a board that loaded only while selected could
never become selected. BSEL is an input of the PAL (U6), and the equations are not printed, so
whether an unselected board also **counts** is not shown. It makes no difference to software that
reloads the address for each record, as all shipped code does; a model that counts on every board
keeps all boards at the same 22-bit address. Only the board whose S1 8–10 match
MA21–MA19 (BSEL true) drives the data bus, writes its RAM and pulls RDY (pp.7, 17, 19). An
emulator can keep one 22-bit counter for the whole set and a 512 K array for each board present.

## 4. The data port

- **Write** (`OUT base`): the byte goes from the bus (DO0–DO7) through U13 74LS244 (enabled by
  IE\*) onto the board's DB bus, to the RAM data inputs and to the counter load inputs (p.19).
  The selected board writes it at the current address.
- **Read** (`IN base`): the 8203 reads the row; XACK\* clocks the byte into U19 74LS374, and
  OE\* drives it onto DI0–DI7 (p.19).
- **No board at the address** (no BSEL match): nothing drives DI0–DI7, so the CPU reads the
  bus's idle value — `FFH` on an S-100 bus. The shipped BIOS sizes the drive by this (§7).

The decode PAL U6 (197P-1) takes sINP, sOUT, A0, pWR\*, pDBIN, BSEL and PWRFAIL\* (through
jumper J1) and makes OE\*, LOAD\*, COUNT\*, IE\*, PCS\*, RD\* and WR\* (p.17). **The PAL equations
are not in the manual.** PWRFAIL\* into the PAL suggests writes are blocked during power fail;
the manual does not say so.

## 5. Memory array and timing

- 512 K × 8: 64 Mostek **4864** (or equivalent, 64 K × 1) in 8 rows of 8 bits, U21–U84
  (pp.20, 21). Row *n* is the 64 K block MA16–MA18 = *n*: PAL U10 (197P-2) turns MA16–MA18 and
  the 8203's two RAS outputs into RAS0\*–RAS7\* (p.18).
- MA0–MA15 go to the **Intel 8203-3** DRAM controller (64 K mode, pin 35 grounded), which does
  the row/column multiplexing, refresh and arbitration from a **24 MHz** crystal (X1, 74F04
  oscillator U12; pp.8, 18).
- RDY (bus pin 72): U14 74LS74 is cleared at pSYNC and set by XACK\*; its Q drives RDY through
  U20 74LS125, enabled by PCS\*, so the board adds wait states only while it is selected (p.19).
  The manual gives no wait-state count. In an emulator the data port needs no delay.
- "Since the DRAM is isolated from the IEEE 696/S-100 bus, problems normally associated with
  DRAM and the S-100 bus are avoided" (p.8).

## 6. The sample driver (pp.11–16)

A "skeletal" CP/M 2.2 BIOS by L. Ott, 12/17/82, "taken out of the standard CompuPro CPM 2.2
BIOS" (pp.10–11). Equates: `HBASE EQU 0C6H`, `HDATA EQU HBASE`, `HADDR EQU HBASE+1`,
`MEMDRV EQU 'M'`, `F$RDAT EQU 6`, `F$WRT EQU 5`, `MRTRY EQU 10`.

### 6.1 Disk layout

- **8 sectors (records) per track for each board**, 128 bytes each. `SETSEC` adds 1 to the
  sector ("to make like floppies"); the transfer routine takes 1 off again.
- **512 tracks.** The record for track *t*, sector *s* (0-based) is at
  `(s << 16) | (t × 128)`: the sector picks a 64 K block, the track a 128-byte slot in it.
  More boards add sectors: *s* runs `0..8n−1` for *n* boards, so `s<<16` reaches MA19–MA21.
- **4 reserved tracks** "for parity check, this is always 4". Tracks 0–3 of every block hold
  the parity bytes.
- **Parity byte** for (*t*, *s*) at `(s << 16) | t` — byte *t* of tracks 0–3 of the same
  block. Its value: **`80H` + the sum of the 128 data bytes, modulo 256** (`MVI C,128` /
  `MOV B,C` seeds B with 128).

Address sequence (`SETMDRV`, p.15–16): `OUT HADDR` with A = *s*, then H, then L. For a record,
HL = *t* × 128; for its parity, HL = *t* ("H is always 0 or 1"; "Upper bit of L picks track
0–3, remainder of byte picks sector and byte").

### 6.2 Read and write

```
read:  set address (s, t*128); 128 x IN HDATA, B += byte (B starts at 80H);
       set address (s, t);     IN HDATA, SUB B -> 0 is good, else retry (10 times), A=1
write: set address (s, t*128); 128 x OUT HDATA, B += byte;
       set address (s, t);     OUT HDATA with B; A=0
```

### 6.3 Disk parameter block (one board)

```
HMDSIB  EQU 2048/128              ; 16 records per block
HMDDSM  EQU ((512-4)*8)/HMDSIB    ; = 254
        DB  128                   ; MDRIVE type for MFORM, do not change
DPBHMD+1:
        DW  8                     ; SPT (8 for each board)
        DB  4,15,1                ; BSH, BLM, EXM  (2 K blocks)
        DW  HMDDSM-1, 128-1       ; DSM = 253, DRM = 127
        DB  11000000B,00000000B   ; AL0, AL1
        DW  0                     ; CKS (no checksum vector)
        DW  4                     ; OFF (4 reserved tracks)
```

The DPH points at `DPBHMD+1`; the byte before the DPB (`128`) is the type code MFORM reads.

## 7. Shipped software

- **MDRIVE.COM** installs the drivers in another CP/M 2.2 at a given address (1 K under the
  top of memory, Table 1, p.10): `MDRIVE -M FC00` for a 63 K system on 64 K. It sizes the drive,
  patches the BIOS, installs the drivers and formats the drive.
- **MFORM.COM** formats the drive. `MFORM M` asks before it reformats (p.9). The CompuPro CBIOS
  runs MFORM at cold boot; MFORM formats only an unformatted drive, so a reset keeps the data
  (CP/M 2.2 Technical Manual, §3.2).
- **CompuPro operating systems with built-in support** (p.5): CP/M 80 2.2MDB or later,
  CP/M 86 1.1PAA or later, MP/M 816 2.1D or later. The sign-on prints the size in K.

**Checked against CompuPro's own BIOS** — the CP/M-86 BIOS source on `TMXBIOS.IMD` in the
maintainer's CompuPro collection (`READHMD`/`WRITHMD`, and the cold-boot sizing):

- Same order: `OUT` control port with the sector, then the high and low bytes of track × 128;
  then the sector, the track's high and low bytes for the parity byte. The control port is
  `HM_DATA + 1`.
- Same parity seed, `MOV BL,80h ;Start parity`.
- **Sizing:** at track 4, sector 8*k* (*k* = 0…7, the first record of board *k*), it reads,
  writes back and reads again; a parity error ends the count. An absent board reads `FFH` for
  all 129 bytes, and 128 × `FF` + `80` = `00` ≠ `FF`, so it fails. SPT becomes the last sector
  tried (8 × boards); with 32 sectors (4 boards, 2 MB) or more it uses 4 K blocks, below that
  2 K blocks, 128 directory entries, and EXM 1 for one board only.

## Quirks worth carrying

- ⚠ **The address port is write-only and shifts.** Three `OUT`s, **high byte first**; only
  bits 0–5 of the first byte are kept. A driver that writes low byte first gets a wrong but
  plausible address.
- ⚠ **All boards load together.** Do not model the address as a per-board register that only
  the selected board loads: every board on the ports sees every address write.
- ⚠ **An absent board must read `FFH`.** The shipped BIOS counts the boards by a parity
  failure on the empty bus (§7). A model that answers for a board that is not there — for
  example by wrapping one 512 K array over the whole 4 MB — reports boards that do not exist.
- ⚠ **Contents at power-up are not defined, and a reset keeps them.** MFORM formats only a drive
  it finds unformatted, so the data must survive a reset.
- ⚠ **Component designations conflict.** The parts list (p.21) gives the DRAMs as **U21–U84**
  and U20 as a 74LS125; the board silkscreen in the component layout (p.21) reads "U20 THROUGH
  U83 — 4864 OR EQUIVALENT". The schematic (p.19) has U20 = 74LS125, so the parts list wins.
  It does not matter to an emulator.
- ⚠ **The PAL equations (197P-1, 197P-2) are not given.** What an `IN` from the address port
  returns, and what PWRFAIL\* does, are not documented.
- The sample `WRITE` routine has its `JMP HMDFNL` commented out (p.14); it falls through into
  `HMDFNL` on the next line, so it works.
