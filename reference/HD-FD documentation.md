# Vector Graphic Dual-Mode Disk Controller — floppy and Winchester controller on one S-100 board

Source: [HD-FD documentation.pdf](#)

Vector Graphic Inc., *Dual-Mode Disk Controller Board, Revision 2.0, Engineering Documentation,
Revision A*, February 1, 1981, 62 PDF pages. The file holds the manual, the Z-80 sample driver
(dated 01/26/81, written by Jay Rasmussen) and two schematics. This board drives one hard disk
and up to three floppies, or four floppies, through four I/O ports and a 512-byte buffer RAM.

**Not emulated.** This file holds what an implementer needs: ports, bits, sector images,
sequences, geometry, the ECC polynomial and the schematic facts that the text leaves out. It omits
the BIOS and timeshare glue, and the full listing of the ECC correction routine.

Citations: `1-n` is a printed page. The listing has no page numbers, so `PDF n` is a PDF page.
PDF page = printed page + 4 for 1-1 to 1-15, and + 3 for 1-17 to 1-21. **Printed page 1-16 is not
in the scan.**

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Ports (default) | **C0H–C3H**. Jumper block D sets A7–A4. A3 and A2 must be 0. |
| C0H | OUT drive, head, step, direction, low current. IN status 0. |
| C1H | OUT sector, read/write, ECC enable, write pre-comp. IN status 1. |
| C2H | IN or OUT one byte of buffer RAM. The address counter increments. |
| C3H | IN resets the RAM address counter and reads **FFH**. OUT starts the transfer. |
| Buffer RAM | 512 bytes (two 2114s, A9 grounded). 9-bit address. |
| Hard disk | 4 heads, 153 tracks, 32 sectors, 256 bytes, MFM. 5,013,504 data bytes. |
| Floppy | 2 sides, 77 tracks, 16 hard sectors, 256 bytes, MFM. 630,784 data bytes. |
| Interrupts, DMA | None. No such line is on either schematic. The driver polls. |
| Busy | **While busy, the board decodes no port.** See §4. |
| Clocks | 10 MHz crystal for the hard disk. S-100 `CLK` (pin 49) ÷ 4 for the floppy. |
| Power-on | `POC*` clears both output latches and the motor one-shot. Not busy. |

The board has no command set. It reads or writes one whole sector between RAM and the disk. It
does not check an ID, it does not seek, and it does not make a CRC. The guest loads every byte of
the sector image, and the guest checks every header byte. The only compare in hardware is sector
number against sector counter.

---

## 2. Port map (1-4, 1-8, 1-10, 1-12, PDF 55)

| Port | Listing names | OUT | IN |
|---|---|---|---|
| C0H | `CNTLPORT0`, `STATUS0` | Control 0 latch `U9` | Status 0 |
| C1H | `CNTLPORT1`, `STATUS1` | Control 1 latch `U18` | Status 1 |
| C2H | `DATAPORT` | Write RAM | Read RAM |
| C3H | `STARTPORT`, `RESETPORT` | START | RESET counter |

Decode, from the schematic (PDF 58): jumper block D gives each of A7–A4 in true or inverted form
to `U48`. As drawn, the block selects 1100, which is C0H. A second gate needs A3 = 0, A2 = 0 and
`BUSY*` high. `A1` and `A0` drive `74LS139` decoders `U7`, which make `RD0*`–`RD3*` and
`WR0*`–`WR3*`.

Because A3 and A2 must be 0, C4H–CFH are not decoded.

### Control bits (1-8, 1-9)

| Port | Bit | Name | Meaning |
|---|---|---|---|
| C0H | 0–1 | DRIVE SELECT 0–1 | Drive 0–3. The hard disk is always drive 0. |
| C0H | 2–3 | HEAD SELECT 0–1 | Hard disk head 0–3. On a floppy, bit 2 is the side. |
| C0H | 4 | HEAD SELECT 2 | "Reserved for 10 M/Byte" |
| C0H | 5 | STEP A TRACK | One step for 0 → 1 → 0 |
| C0H | 6 | DIRECTION | 1 = in, track number goes up. 0 = out. |
| C0H | 7 | LOW CURRENT | Hard disk write at track 128 or more. See ⚠ Q6. |
| C1H | 0–4 | SECTOR 0–4 | Hard disk 0–31. Floppy 0–15, bits 0–3. |
| C1H | 5 | READ/WRITE* | 1 = read, 0 = write |
| C1H | 6 | ECC ENABLE | Insert ECC on a write. Check it on a read. |
| C1H | 7 | WRITE PRE-COMP | Hard disk write at track 64 or more. See ⚠ Q6 and ⚠ Q12. |

Set the direction bit 100 ns before the step bit, in a separate `OUT` (1-15). The driver does
this in `STEPIT` (PDF 42).

### Status bits (1-10, 1-11, PDF 54)

| Port | Bit | Name | Active | Drive | Note |
|---|---|---|---|---|---|
| C0H | 0 | WRITE PROTECT | 1 | Floppy | |
| C0H | 1 | READY | 1 | Hard | About 15 s after power on |
| C0H | 2 | TRACK 0 | 1 | Both | |
| C0H | 3 | WRITE FAULT | 1 | Hard | |
| C0H | 4 | SEEK COMPLETE | 1 | Hard | Low during the power-on recalibrate |
| C0H | 5 | LOSS OF SYNC | **0** | Hard | 1 = the sector PLL is locked |
| C0H | 6–7 | Reserved | | | Pulled up, read 1 |
| C1H | 0 | FLOPPY SELECTED | 1 | | High when the drive select decodes a floppy |
| C1H | 1 | CONTROLLER BUSY | 1 | | See §4 |
| C1H | 2 | MOTOR ON | 1 | Floppy | High until the motor timer ends |
| C1H | 3 | TYPE OF HARD DISK | | | 0 = 5 MB, 1 = 10 MB. A jumper. |
| C1H | 4–7 | Reserved | | | Pulled up, read 1 |

The schematic adds these facts:

- `Z2` is a 4.7 K pull-up on the internal data bus. Bits with no driver read 1. An `IN` from C3H
  has no driver at all, so it reads FFH.
- Status 0 bits 0–4 come from the drive lines through an inverting `74LS368`. `Z1` holds these
  lines high when nothing drives them. A bit with no drive behind it reads 0.
- Status 0 bit 5 reads 1 with a floppy selected, because `FD*/HD` clears the one-shot `U60`.
- Status 1 bit 1 has its gate input tied to **ground**. See §4.

---

## 3. Sector images

### Hard disk (1-5, 1-6, PDF 36–37)

| Field | Bytes | Value |
|---|---|---|
| Preamble | 30 | 00H. Never appears in RAM after a read. |
| Sync | 1 | FFH |
| Head | 1 | 0–3 |
| Track | 1 | 0–152 |
| Sector | 1 | 0–31 |
| Data | 256 | |
| ECC | 4 | Load 00H. The hardware inserts the ECC. |
| Postamble | 128 or more | 00H. "To end of sector." |

### Floppy (1-6, 1-7, PDF 36–37)

| Field | Bytes | Value |
|---|---|---|
| Preamble | 40 | 00H |
| Sync | 1 | FFH |
| Track | 1 | 0–76 |
| Sector | 1 | 0–15 |
| Filler | 10 | 00H |
| Data | 256 | |
| Checksum | 1 | See below. Software makes it. |
| ECC | 4 | Load 00H |
| ECC valid | 1 | AAH = ECC valid. Any other value = ECC not used. |
| Postamble | 128 or more | 00H |

**Checksum:** an add with carry over each byte from the track byte to the last data byte. The
start value is 0 with carry clear, and the last carry is dropped (`SENDITOUT`, PDF 37). The hard
disk format has no checksum on the disk. The driver still computes one over head, track, sector
and data, for its own compare of what it wrote against what it read.

### RAM offsets

These offsets come from the driver's byte counts (PDF 36–39).

| Field | HD write | HD read | FD write | FD read |
|---|---|---|---|---|
| Preamble | 0–29 | | 0–39 | |
| Sync | 30 | 0 | 40 | 0 |
| Head | 31 | 1 | | |
| Track | 32 | 2 | 41 | 1 |
| Sector | 33 | 3 | 42 | 2 |
| Filler | | | 43–52 | 3–12 |
| Data | 34–289 | 4–259 | 53–308 | 13–268 |
| Checksum | | | 309 | 269 |
| ECC | 290–293 | 260–263 | 310–313 | 270–273 |
| ECC valid | 294 ⚠ | | 314 | 274 |
| Postamble | 295–422 | | 315–442 | |

The driver loads 423 bytes for a hard disk write and 443 for a floppy write. A read is complete
from the sync byte on. RAM holds the sync byte at offset 0, because the preamble is never stored.

⚠ `PHYSWRITE` sends AAH for both types. On a hard disk that byte falls in the postamble, and no
read looks at it (PDF 37).

---

## 4. Busy, start and the sector counter

This section comes from the schematics. The manual says only that the bit is high during a read
or a write (1-11).

**Busy gates the address decode.** `BUSY*` drives two inputs of the `U48` gate that decodes
A3 = A2 = 0 (PDF 58, pins 9 and 10). While busy, the board responds to no port. Each `IN` floats
and each `OUT` is lost. Status 1 bit 1 is wired to ground, so when the board does respond, that
bit is **0**. The driver reads bit 1 = 1 only because a floating S-100 bus reads FFH. The 8″ sheet
has the same gate (`U50`, PDF 61) and the same grounded input.

An implementation that reads a busy-time value must return FFH from all four ports, and must
drop writes.

**Sequence (`U24`, `U17`, PDF 58):**

1. `OUT C3H` (`WR3*`) clears the first flip-flop of `U24`. The board is busy.
2. `U17`, an 8131, compares latched sector bits against the sector counter.
3. At a sector pulse with a compare, the second flip-flop sets `SECTOR MATCH`. The transfer runs.
4. At the next sector pulse the compare fails, `SECTOR MATCH` drops, and that edge ends busy.

Busy therefore lasts until the wanted sector comes around, plus one full sector time.
`MOT` is the preset input of the first flip-flop, so a START with the motor timer ended does not
stay busy.

**RAM counter.** `U59`, a 4040, is the address counter. Bits M0–M8 reach the RAM, so the address
wraps at 512. Only `RD3*` clears it, which is an `IN` from C3H. An `OUT` to C3H does not clear it
(1-12). The counter steps on `RD2*` and `WR2*` when idle, and on the byte clock when busy.

**Floppy sector counter.** A floppy has 16 sector holes and one index hole between two of them.
`U33` is a 74LS161, and `U25` is a pair of one-shots. The first one-shot (15 K, 100 pF) clocks
`U33` on each hole. The second (61.9 K, 0.22 µF, about 9.5 ms) drives `LOAD*`, and the load inputs
are all 1. A hole that arrives within that time is the index hole, and it loads 15. The next
sector hole counts to 0. Sector 0 is the first sector hole after the index hole.

**Hard disk sector counter.** The hard disk gives one `INDEX` pulse per turn and no sector pulses.
`U20`, a 4046 PLL, and `U41`, a 74LS393, make the sector count from `INDEX`. Five bits of that
count are the sector number. The block diagram (1-2) shows the same PLL. When the PLL is out of lock, the one-shot `U60` (82 K, 1 µF)
pulls status 0 bit 5 low. Its other output blocks `WRITE GATE`. This is the "sector counter"
that status 0 bit 5 reports (1-11).

**Motor and select timer.** `U60` (100 K, 100 µF) triggers on `WR0*`, which is each `OUT` to C0H.
The listing calls it a 4-second timer (`TIMER`, PDF 44). Its output enables `U6`, the drive select
decoder, so **no drive stays selected after the timer ends**, hard disk included. MOTOR ON is high
only while the timer runs and a floppy is selected. `TIMER` writes C0H in each delay to retrigger
it.

---

## 5. Operation sequences

### Calibrate (1-13, `RESTORE`, PDF 43)

1. Select the drive.
2. Step in 4 tracks, so that the head is not on track 0.
3. Step out 1 track, and test TRACK 0. Repeat up to 87 times for a floppy, 163 for a hard disk.
4. Report error 14H if TRACK 0 never comes high.

### Select (1-14, `SLCT`, PDF 40–42)

1. `OUT C1H` with sector, read/write and ECC.
2. Read MOTOR ON. The next step retriggers the timer, so read it first.
3. `OUT C0H` with drive and head.
4. Hard disk: wait for bit 5 high, then check WRITE FAULT low and READY high.
5. Floppy, drive 0: check that status 1 bit 0 is high. If it is low, report error 15H.
6. Floppy: wait 250 ms if the motor was off or the drive changed.

### Seek (1-15, `SEEK`, PDF 42)

1. Wait 1 ms if the last operation was a write.
2. If the table entry is FFH, calibrate.
3. Step once for each track of difference.
4. Floppy: wait 15 ms. Hard disk: poll SEEK COMPLETE, with a limit of 4000H polls.

### Write (`PHYSWRITE`, PDF 35–37)

Printed page 1-16 is missing, so this sequence comes from the listing.

1. `IN C3H` to reset the counter.
2. `OUT C2H` the whole image: preamble, sync, header, data, ECC as 00H, then postamble.
3. `OUT C1H` and `OUT C0H` with the write bit clear.
4. `IN C3H`, then `OUT C3H`.
5. Poll status 1 bit 1 until 0, with a limit of 4000H polls. Report error 21H at the limit.

### Read (1-17, `PHYSREAD`, PDF 37–39)

1. `OUT C1H` and `OUT C0H` with the read bit set.
2. `IN C3H`, then `OUT C3H`.
3. Poll status 1 bit 1 until 0.
4. `IN C3H` to reset the counter.
5. `IN C2H` the image, from the sync byte on.
6. Check sync = FFH, then sector, track and head, then ECC, then the floppy checksum.

### Retries (1-18, 1-19, PDF 32–33)

| Level | Action | Count |
|---|---|---|
| Read | Read the sector again | 6 |
| 1 | Do the failed operation again | 5 |
| 2 | Step off the track and back, then level 1 | 4 |
| 3 | Delay 200 ms, calibrate, seek, then levels 1 and 2 | 3 |

Level 3 does not run for an error below 30H, which is a drive or timeout error.

Read-after-write and the read before a write are both on by default. DCB+1 bit 7 turns both off.

---

## 6. Units, heads and tracks

**Drive select.** The hard disk and a fourth floppy use the same address, drive 0 (1-3, 1-8).

| Build | Logical unit → drive | `NUMUNITS` |
|---|---|---|
| Floppy only | 0 → 1, 1 → 0, 2 → 2, 3 → 3 ⚠ | 4 |
| Hard disk, one logical drive | 0–3 → 0–3 | 4 |
| Hard disk, two logical drives | 0, 1 → 0; 2 → 1; 3 → 2; 4 → 3 | 5 |

**Logical tracks.** A DCB track is a logical number, and the driver divides it by tracks per head
(`VALIDATE`, PDF 29–31). The quotient is the head and the remainder is the physical track.
A two-sided floppy therefore uses 0–76 on side 0 and 77–153 on side 1, with track numbers that
run the same way on both sides. `INFOTBL` holds the table (PDF 56–57).

| Entry | Heads, two logical drives | Heads, one logical drive | Tracks per head |
|---|---|---|---|
| `FS.DISK` | 1 | 1 | 77 |
| `FD.DISK` | 2 | 2 | 77 |
| `H5.DISK` | 2 | 4 | 153 |
| `H10.DISK` | 3 | 6 | 153 |

With two logical drives, unit 1 adds a head offset. `H5.DISK` unit 1 uses heads 2–3. `H10.DISK`
unit 1 uses heads 3–5.

`DBL_SNG` is a configuration byte with one bit for each unit. A 1 means one-sided (`GETFLAGS`,
PDF 45–46). The floppy-only build treats unit 0 as two-sided in all cases. Bit 7 turns on the
`SOFT ERROR-` message (PDF 33).

---

## 7. ECC

The ECC is 32 bits. The manual gives no polynomial. The schematic and the listing give it, from
two independent directions, and they agree.

**Hardware (PDF 59):** four `74LS164` shift registers, `U52`, `U44`, `U36` and `U28`, with the
feedback from `U19`, a 74LS280 parity chip. The taps are X0, X7, X8, X11, X14, X15, X18 and X24.

```
g(x) = x^32 + x^24 + x^18 + x^15 + x^14 + x^11 + x^8 + x^7 + 1
```

**Software (`SHIFT`, PDF 49):** `ECCFIX` shifts the syndrome right and XORs `80H 82H 64H C0H` into
`B C D E`. With X0 at bit 7 of B (PDF 48), these are bits 0, 8, 14, 17, 18, 21, 24 and 25. That
set is the reciprocal of g(x), and the reciprocal runs the register in the other direction.

The two sets were compared by program, and they match in each bit.

- **Coverage:** from the sync byte to the last data byte, and on a floppy to the checksum. The
  offset of the ECC is `HECCOFF` = 260 for a hard disk and `FECCOFF` = 270 for a floppy (PDF 47).
  The correction code handles errors in sync, head, track and sector (PDF 50–51).
- **Read:** the hardware makes a new ECC and returns **four 00H bytes** on a match. Any other
  value is a syndrome (1-6).
- **Correction:** `ECCFIX` shifts until only the top 13 bits are set, and it aligns to a byte.
  The bit displacement starts from `K1` = offset × 8 + 7 (PDF 48). It credits "a report by Neal
  Glover", page 12.
- **Retry:** the driver reads again and corrects only when two reads give the same ECC (1-18,
  `CMPECC`, PDF 33, 53).

`U51`, an 82S131 PROM, marks the ECC time from the RAM address, `FD/HD*` and `RD/WR*` (PDF 59).
Its contents are not in the scan. The ECC field therefore works only at the offsets of §3.

An emulator that stores good sectors can return 00H for the four ECC bytes on each read. It needs
g(x) only to produce real syndromes.

---

## 8. Disk Control Block and error codes

The driver takes a DCB pointer in `IX` (1-20, 1-21, PDF 25, 54).

| Offset | Name | Meaning |
|---|---|---|
| +0 | `DCBCOM` | 0 seek, 1 read, 2 write, 3 verify, 4 init, 5 write-protect test |
| +1 | `DCBUNT` | Bits 0–3 unit. Bit 4 interrupt save. Bit 5 write-protect detect (1 = do). |
| | | Bit 6 compare verify and write checksum (1 = do). Bit 7 pre-write ID check (1 = skip). |
| +2 | `DCBSEC` | Sector |
| +3, +4 | `DCBTRKL`, `DCBTRKH` | Logical track, low and high |
| +5, +6 | `DCBBUFL`, `DCBBUFH` | Address of a 268-byte buffer |

**Buffer** (PDF 25–26). Floppy: track 0, sector 1, filler 2–11, data 12–267. Hard disk: head 0,
track 1, sector 2, data 3–258, 259–267 unused. The driver fills the ID bytes.

**Return:** `Z` on success, `NZ` and a code in `A` on failure (PDF 26).

| Code | Meaning | Code | Meaning |
|---|---|---|---|
| 11 | Drive not ready | 43 | Heads don't match |
| 12 | Write protected | 44 | Data doesn't match |
| 13 | Write fault | 51 | Read checksums don't match |
| 14 | Track 0 not found | 52 | Read/write checksums don't match |
| 15 | Controller not jumpered for floppy-only system | 53 | ECC error |
| 21 | Controller busy timeout | 54 | Uncorrectable ECC error |
| 22 | Seek complete timeout | 55 | Uncorrectable spasm error |
| 23 | Loss of sync timeout | 56 | Sync byte error |
| 31 | Bad command code | 91 | No room left in table |
| 32 | Bad unit value | E1 | Reset disk function attempt |
| 33 | Bad sector value | E2 | Disabled and protected |
| 34 | Bad track value | E3 | Disabled and busy |
| 41 | Tracks don't match | F1 | Not much room left in table |
| 42 | Sectors don't match | | |

The first digit maps to a BDOS error: 1x and 2x → 05, 3x–7x → 01, 9x → 06, Ex → 80, F1 → 07.

`TRACKADDR` (FF54H) and `BUSY` (FF10H) are memory locations in Vector's BIOS. They are not
controller registers (PDF 55).

---

## 9. Hardware identification (PDF 58–59, 61–62)

The file holds two sheets. Both are later than the manual, and the scans cut off the right edge.
PDF 60 is blank.

| | 5¼″ FD/HD Controller | 8 Inch H.D. Controller |
|---|---|---|
| Pages | PDF 58–59 | PDF 61–62 |
| Dated | 1-3-81 | 1-3-81 |
| Latest change | Rev 11, ECR 694, 4-11-82, "update to suit 8088" | Rev 03, ECR 637, 4-11-82, "update to suit 8088" |
| C1H bit 7 | `WPC` | **`RECAL`** (J3 pin 6) |

Jumpers, as drawn:

| Function | 5¼″ block | 8″ block | Drawn setting |
|---|---|---|---|
| Base address A7–A4 | D | D | 1100 = C0H |
| Hard disk on drive 0 (`FD/HD*`) | B | E | In. Out = floppy only. |
| DS0 to the hard disk or floppy connector | A | | Hard disk. 8″: J3 only. |
| MOTOR ON to J2 | C | D | 5¼″ closed. 8″ open. |
| DS3 to J2 | | C | Open |
| Type of hard disk, status 1 bit 3 | E | H | Ground, 0 = 5 MB |
| ECC flip-flop preset | F | I | `RD3*`. Other position: `ECC ENABLE`. |
| Sector compare enable | | F | To `U33` |
| Sector count chain | | A, B | Not traced |

**Clocks.** `U21` selects the write clock, `TX CLOCK X2`. A hard disk uses the 10 MHz crystal `Y1`.
A floppy uses S-100 `CLK` (pin 49) divided by 4 in `U53`. The floppy data rate therefore depends
on the bus clock. See ⚠ Q11.

**Connectors.** J3 is the hard disk control connector and J2 is the floppy connector. `J1` has the
differential data lines of `U3` and `U4`. The 8″ sheet prints no pin number for DS0 on J3.

| Line | 5¼″ J3 | 5¼″ J2 | 8″ J3 | 8″ J2 |
|---|---|---|---|---|
| HEAD SEL 0 / SIDE 0 | 14 | 32 | 14 | 32 |
| HEAD SEL 1, 2 | 18, 4 | | 18, 4 | |
| STEP* | 24 | 20 | 36 | 20 |
| DIR* | 34 | 18 | 34 | 18 |
| T>127* | 2 | | 2 | |
| WRITE PROT* | | 28 | | 28 |
| READY* | 22 | | 22 | |
| TRACK 0* | 10 | 26 | 40 | 26 |
| WRITE FLT* | 12 | | 44 | |
| SEEK COMPL* | 8 | | 8 | |
| INDEX/SEC* | | 8 | | 8 |
| INDEX* | 20 | | 20 | |
| WRITE GATE* | 6 | 24 | 40 ⚠ | 24 |
| DS0 | 26 | 10 | Yes | |
| DS1, DS2, DS3 | | 12, 14, 6 | | 12, 14, 6 |
| MOTOR ON* | | 16 | | 16 |

⚠ The 8″ sheet prints pin 40 for both TRACK 0 and WRITE GATE. The conflict is not resolved.

**Parts.** RAM `2114` × 2. Write pre-comp `82S131`, `U30` (5¼″), `U32` (8″). ECC timing `82S131`,
`U51` (5¼″), `U53` (8″). Latches `74LS273`, `U9`, `U18`. Shift register `74LS299`, `U27`. Bus
transceivers `8T28`. Data separators `74LS629`, `74S629`. Regulators `7805` × 2, `7905`.

**Software.** The listing is for 5¼″ floppies, Vector's controller and "CP/M 2.21". It needs a
Z-80: 8080 mnemonics with extensions such as `JRZ`, `DJNZ`, `BIT`, `SET` and `DSBC`.

---

## Quirks worth carrying

Each entry gives the readings and the one that won. The listing is the shipped software, so it
wins over the prose unless the entry says otherwise.

- **⚠ Q1. Page 1-16 is missing.** §1.8.4 Write is not in the scan. The contents page lists it,
  and PDF 19 and PDF 20 are pages 1-15 and 1-17. §5 uses `PHYSWRITE` instead.
- **⚠ Q2. Busy is not a status bit.** The manual calls it a status bit. The schematic shows that
  the board stops responding, and the bit is high only on a floating bus. See §4. The schematic
  settles it, and the driver's `ACTIVATE10` works with either reading.
- **⚠ Q3. `IN C3H` is data.** The manual says this port has no data lines (1-12). `PHYSWRITE`
  stores the value in `OPFLAG`, and its comment says `OPFLAG = 0FFH ON WRITES` (PDF 35). `Z2`
  makes the value FFH. `SEEK` then waits 1 ms after a write. Return FFH.
- **⚠ Q4. The driver writes first and tests write protect after.** `WRITEFUNC10` calls
  `PHYSWRITE`, then `CHKWRTPROT` (PDF 35). A write to a protected floppy must finish without a
  controller error. It must not change the disk. The controller has no write-protect logic, so
  the drive has to refuse the write.
- **⚠ Q5. Floppy-only units 0 and 1 swap.** `VALIDATE` has `XRI 1`, with the comment `ELSE SWAP
  A & B`, for drives below 2 (PDF 29). Logical unit 0 is drive 1.
- **⚠ Q6. Pre-comp and low current on all operations.** The manual says to set them only for a
  hard disk write, and 0 at all other times (1-9). `SLCT10` sets them from the track alone, on a
  read too, and in a hard disk build on a floppy too (PDF 40–41). Both writes are harmless to an
  emulator. Do not treat either bit as an error.
- **⚠ Q7. Read before write.** The manual says the sector "immediately preceding" (1-19).
  `WRITEFUNC` subtracts 2, with the comment `STEP BACK TWO SECTORS` (PDF 34). The listing wins.
- **⚠ Q8. Step rate.** The manual says 3 ms (1-15). `STEPIT` uses 3 ms only if the `STEPRATE`
  entry is non-zero, and 30 ms otherwise, and the table ships as zeros (PDF 43, 47).
- **⚠ Q9. Calibrate limits.** The flowchart counts from 80 (floppy) or 0 (hard disk) to 160, which
  is 80 or 160 steps (1-13). `RESTORE` uses `F.STEPMAX+10` = 87 and `W.STEPMAX+10` = 163 (PDF 43).
- **⚠ Q10. "10 megabyte".** `H10.DISK` has 6 heads × 153 tracks × 32 sectors × 256 bytes =
  7,520,256 bytes. Only 8 heads gives 10,027,008. The listing wins, and the "10 MB" name stays
  **unresolved**.
- **⚠ Q11. The floppy format and the rate.** The fields before the postamble total 315 bytes
  (§3). The manual gives no rotation speed or data rate. If `TX CLOCK X2` is twice the bit rate,
  a 2 MHz `CLK` gives 250 kbit/s. At 300 RPM, a 12.5 ms sector then holds 312.5 bytes, which is
  less than 315. A 4 MHz `CLK` gives 625 bytes. The one-shot in §4 fits 300 RPM, but does not
  prove it. **Unresolved.**
- **⚠ Q12. `RECAL` against `WPC`.** On the 8″ sheet, C1H bit 7 goes to `RECAL`, but the pre-comp
  PROM `U32` still has a `WPC` input. The sheet shows no other source for `WPC`. **Unresolved.**
  No listing for the 8″ board is in the scan.
- **⚠ Q13. Hard disk tracks in the DCB.** The manual gives "0 to 153" (1-20). That is the floppy
  range. `VALIDATE` accepts heads × tracks for a hard disk, which is 0–611 for `H5.DISK` with one
  logical drive (PDF 30–31).
- **⚠ Q14. Function 5.** The manual says write protect detect test (1-20). The listing's header
  says drive ready test (PDF 25). The code calls `CHKWRTPROT`, so write protect wins.
- **⚠ Q15. Column heading.** The table on 1-20 heads the function codes "BIT". They are values
  (`SEEKCOM EQU 0` to `WPDCOM EQU 5`, PDF 53).
- **⚠ Q16. `ECCTEST`.** The comment says it returns 52H. The code loads 53H, which is the table's
  ECC error (PDF 52).
- **⚠ Q17. Buffer offsets.** `HARDDATA` = 4, `FLOPDATA` = 0DH and `FLOPCS` = 10DH are labelled
  buffer offsets (PDF 54). They are RAM offsets from the sync byte. In the 268-byte host buffer,
  data starts at 3 and 12.
- **⚠ Q18. Wrong section numbers.** 1-18 and 1-19 cite Sections 1.6.5 and 1.6.4. Neither exists.
  The text means 1.8.5 and 1.8.4.
- **⚠ Q19. Retry comment.** `WRITEFUNC25` says `UP TO 3 TIMES`, but `RETRY1` starts at 5 (PDF 32,
  35). The code wins.
- **⚠ Q20. Deselect.** The retry text says the unit is deselected for 200 ms (1-19, PDF 32).
  `RESLCT` calls `TIMER`, and `TIMER` writes C0H with the drive still selected (PDF 44). The code
  does not deselect.
- **⚠ Q21. The delay loop.** `TIMER` loops 192 times for 1 ms, at 23 T-states a pass for `SUI`,
  `ORA` and `JRNZ` (PDF 44–45). That is 4,416 T-states, or 1.1 ms at 4 MHz. The driver expects a
  processor near 4 MHz.
