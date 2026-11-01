# Vector Graphic Micropolis Disk Controller Board — memory-mapped 5¼-inch floppy controller

Source: [Floppy Controller.pdf](#)

Vector Graphic, *Micropolis 5¼" Floppy Disk Controller Board, Technical Information Manual*, Revision A,
November 21, 1980, P/N 7200-1400-01-01, 56 PDF pages. The file holds the manual, the Micropolis
schematic "F.D. Controller B", drawing 100087, sheets 1 to 9, and advisories VTA MZ-007 and MZ-006 of
February 6, 1981. Page numbers below are the printed numbers. This file also uses the two listings
`MBOOT.ASM` and `Micropolis diskdrv.asm`, the boot ROM `MBOOT.BIN`, the 56K disks and the Vector
*ROM Checksums* sheet. It omits the phase-locked loop, the bit encoding and the repair procedures,
because a model at the byte level does not need them.

Emulated as the `micropolis` board (`docs/boards/vector-micropolis.md`).

## 1. Quick reference for emulation

| Item | Value | Source |
|---|---|---|
| Interface | Memory mapped, 1K block, no I/O ports | p. 1-3 |
| Base address | Any 1K boundary, C000 to FC00. Vector's 56K machines use F800, 48K machines D800 | Table, `ReadMe.pdf` |
| Boot PROM | base + 000 to base + 0FF | p. 1-3, sheet 5 |
| Spare PROM space | base + 100 to base + 1FF. No chip. See §2 | Fig 1-2 |
| Registers | Four, at base + 200, repeated up to base + 3FF | p. 1-3 |
| Disk | 16 hard sectors, 77 tracks (0-76), or 35 on a Mod I (0-34) | p. 1-1, p. 1-3 |
| Rotation | 300 RPM. 200 ms for a turn, 12.5 ms for a sector | Fig 1-7 |
| Data rate | 4 µs a bit, 32 µs a byte | p. 1-7, p. 1-17 |
| User data | 266 bytes. With track and sector, 268 | p. 1-3 |
| Checksum | Add with carry, by the guest. The controller does not check it | p. 1-3 |
| DMA | None. The guest moves each byte, held by PRDY | p. 1-12 |
| Interrupt | Optional, on each sector. Vector and Micropolis software does not use it | p. 1-13 |
| Idle timeout | 4 seconds with no read of the board. The controller then resets | p. 1-11 |
| Boot ROM sum | BC, for F800-F8FF. `MBOOT.BIN` sums to BC | p. 3-1, checked |

## 2. Address map

Offsets are from the base. The standard base is F800.

| Offset | Read | Write |
|---|---|---|
| 000-0FF | Boot PROM | Ignored |
| 100-1FF | ⚠ Open. See below | Ignored |
| 200, and each +4 | Sector register, status byte 0 | Command |
| 201, and each +4 | Status register, status byte 1 | Command |
| 202, 203, and each +4 | Read data | Write data |

Sheet 2 decodes the block with A10 to A15. A9 selects the registers, and A1 selects data. A0 selects
status byte 0 or 1 on a read, and does not matter on a write. A2 to A8 do not reach the register decode.

A8 reaches a chip enable of both PROMs on sheet 5, so a read of 100-1FF enables no PROM. DIG/ still
drives the data-in lines. The sources do not give the value of that read.

Jumpers W1 to W4 set A13 to A10. Each jumper in place compares the line with 0, so:

| Base | W1 | W2 | W3 | W4 |
|---|---|---|---|---|
| C000 | Y | Y | Y | Y |
| D800 | Y | N | N | Y |
| F800 | N | N | N | Y |
| FC00 | N | N | N | N |

The table on the sheet gives all 16 lines, and each follows this rule. For example, E400 has W2 and W3.
To move a 48K board to 56K, remove W1.

## 3. Registers

### Command, write at base + 200 or 201

| Bits 7-5 | Command | Modifier |
|---|---|---|
| 0 | None | |
| 1 (20) | Select drive and head | Bits 0-1 unit, bit 4 head (0 lower, 1 upper) |
| 2 (40) | Interrupt control | Bit 0: 1 enable, 0 disable and clear |
| 3 (60) | Step one track | Bit 0: 0 out, track number goes down. 1 in, track number goes up |
| 4 (80) | Set WRITE | Not used |
| 5 (A0) | Reset controller | Not used |
| 6, 7 | Not used | Not used |

### Sector register, status byte 0, read at base + 200

| Bit | Name | Meaning |
|---|---|---|
| 0-3 | Sector | The sector under the head of the selected drive |
| 4 | W10 | Jumper W10. Reads 1 with no jumper (pull-up R7, sheet 5). Reserved |
| 5 | W9 | Processor speed. 1 = 2 MHz, W9 out. 0 = 4 MHz, W9 in |
| 6 | INT | Interrupt request. Cleared by a reset or by interrupt control with bit 0 = 0 |
| 7 | SF | Sector flag. True for 30 µs at the start of each sector |

### Status register, status byte 1, read at base + 201

| Bit | Name | Meaning |
|---|---|---|
| 0-1 | Unit | The selected unit. Valid only if bit 2 is 0 |
| 2 | SLTD/ | **Low true.** 0 = a drive is selected. Non-existent memory reads FF, so a guest can find a missing board |
| 3 | TK0 | The selected drive is at track 0 |
| 4 | WPT | The selected drive is write protected |
| 5 | RDY | The selected drive is ready. See §7 |
| 6 | PINTE | The state of the S-100 PINTE line |
| 7 | TF | Transfer flag. True from one byte time before the sync byte to the next sector pulse |

## 4. Timing of a sector

| Time from the sector hole | Event | Source |
|---|---|---|
| 0 | SF true, sector number valid. The counter is stable about 200 ns before SF | p. 1-15 |
| 30 µs | SF false | Fig 1-7 |
| 70 µs after SF | Last time to issue Set WRITE. The manual gives 100 µs from the hole | p. 1-17 |
| 700 µs | ROS ends, the read logic looks for the sync byte | Fig 1-7, p. 1-14 |
| 1200 µs | WOS ends after a write, the byte counter starts | p. 1-14, p. 1-18 |
| 1232 µs | TF true on a write, after 8 bits | p. 1-18 |
| 1268 µs | TF true on a read, one byte time before the sync byte | Fig 1-12 |
| About 1300 µs | Sync byte on a read, after about 40 bytes of zero | Fig 1-12, p. 1-1 |
| 12.5 ms | Next hole. WRT and TF reset | p. 1-10, Fig 1-5 |

**Sector layout.** About 40 bytes of zero, sync FF, track, sector, 266 data bytes, checksum, then zeros to
the next hole. In Fig 1-8 the example bytes are FF, 3E, 05 and a check of 27.

**Sector counter.** The index hole is halfway between the holes of sectors 15 and 0. The counter loads 15
at the index hole and counts up at each sector hole, so the next hole makes sector 0 (p. 1-15). No SP/ pulse
comes from the index hole. With no index seen, the number is not valid. The drivers wait 250 ms after a
select for this reason (`SLCT`, and `ramEnt` in the boot ROM).

**No drive, no sector.** SECP comes from the selected drive. With no drive selected, or no disk turning,
the guest sees no SF. `wtSec` in the boot ROM then waits forever.

## 5. Read and write sequences

**Read (p. 1-19).** No read command exists, because the controller reads whenever it does not write.

1. Select the drive, and step to the track.
2. Read status byte 0 until SF is true with the wanted sector.
3. Read status byte 1 until TF is true.
4. Read the data register within 20 µs. PRDY stays low until the sync byte is complete.
5. Read each next byte in less than one byte time. A read holds PRDY low until the byte is ready. PRDY
   is then high for one bit time, 4 µs, and the data is valid only for that time.

If TF is false, a read of the data register does not hold PRDY (p. 1-12). No flag reports a late read.

**Write (p. 1-17).**

1. Issue Set WRITE after SF, with the wanted sector. The controller writes the preamble by itself.
2. Read status byte 1 until TF is true.
3. Write FF within 20 µs. PRDY stays low for one byte time, until the controller takes the byte.
4. Write the track, the sector, 266 data bytes and the checksum. Each write holds PRDY.
5. Stop writing. The controller fills the sector with zeros. The next sector pulse resets WRT.

WRT also resets on RST/ low and when RDY is false (p. 1-10).

`WSECT` checks WPT and does not start a write to a protected disk. No source says what the controller
does if a guest writes anyway.

## 6. Step, select, reset, interrupt

- **Step (p. 1-10).** Pulses in one direction must be 30 ms apart. A change of direction needs 40 ms,
  and a read or write needs 40 ms after the last step. `diskdrv` waits 30 ms and then 10 ms to settle.
- **Restore (`RESTR1`).** If the drive is at track 0, the driver steps in 8 tracks first. It then steps out
  until TK0, for 85 steps at most. The boot ROM does the same, with no limit.
- **Select (p. 1-9).** B3 holds D0, D1 and D4. US stays set until RST/ goes low. Only one drive is selected.
- **Reset (p. 1-11, sheet 7).** RST/ goes low on the reset command, on POC, and when the one-shot D1
  times out. RST/ clears US, the interrupt logic and the write latch.
- **4-second timer.** D1 is retriggered by DIG/. A select or a step does not retrigger it. Only a read does.
- **Interrupt (p. 1-13).** Enable sets D2-5, and the next sector boundary sets INT. A jumper connects INT to
  PINT/ or to one of VI0/ to VI7/. A command with D0 = 0 clears both flip-flops.

## 7. Revisions and jumpers

| Jumper | Function | Standard |
|---|---|---|
| W1-W4 | Base address | W4 only, F800 |
| W5-W8, W13-W16 | MFM or single density. Vector software is MFM only | MFM |
| W9 | Status bit 5, processor speed | In for 4 MHz, as the photo in `Micropolis FDC Jumpers.jpg` shows |
| W10 | Status bit 4, reserved | Out |
| W11, W12 | Write clock from the crystal, or from the S-100 2 MHz clock | Crystal |

**VTA MZ-007 and MZ-006, Rev. 1.** On a Rev. 0 board, a factory update makes the drive motor run only
when the drive is selected. Rev. 1 has this as two jumper areas, both standard at 2 to 3:

| Area | Function |
|---|---|
| A | Motor enable only when selected |
| B | Drive Ready set true. CP/M needs this with the motor option. The drawing ties the input of B7 to ground |

On a standard Rev. 1 board, RDY therefore reads 1 with a drive selected, with or without a disk.

## 8. The disk image

A `.VGI` file holds 275-byte slots, 16 to a track, 77 tracks, 338800 bytes, side 0 only.

| Slot offset | Bytes | Content |
|---|---|---|
| 0 | 1 | Sync, FF |
| 1 | 1 | Track |
| 2 | 1 | Sector |
| 3 | 266 | Data |
| 269 | 1 | Checksum |
| 270 | 5 | Not read by a guest. Mostly zero. Some files hold other bytes |

Checked on five disks of `56K Bootable Disk Images.zip`: the add-with-carry sum of slot bytes 1 to 268 equals
byte 269 on 1229 sectors of VG00.VGI, and on 1232 of the 1232 sectors in each of the other four. This is
the same 275-byte slot as the [Dual-Mode controller](HD-FD%20documentation.md), but the field after the
sector number is data here.

Fifteen files in the archive have 338800 bytes. Five have 338816, which is 16 bytes more than the slots. The
16 bytes differ from file to file, and no source explains them.

## 9. Boot

The boot ROM enters at its base. It copies itself to RAM at 00A2, using RAM at 00A0-00A1 to find
its own address, so it runs at any base and needs RAM there. It then:

1. Issues Reset, and selects drive 0.
2. Waits for RDY true and SLTD/ 0, then 250 ms, then checks again.
3. Restores to track 0.
4. Reads track 0, sector 0, and takes a load address from offset 10A of the 268 bytes.
5. Reads the sector again to that address.
6. Jumps to the load address + 0C, with DE holding the ROM base.

The disk's loader uses the copy in RAM: 00A2 holds the register address, and `rdSec` is at 00A6. On VG00.VGI the
load address is 015F, so the loader starts at 016B. The ROM is 8080 code.

**Which Monitor boots this board.**

| Monitor | `B` jumps to | Boot, from *ROM Checksums* |
|---|---|---|
| 4.0 (`MON40.HEX`, sum F8) | F800, read from the image | 4 MHz, Micropolis |
| 4.0c (`MON40C.MAC`, sum 78) | PR + 1800, which is F800 | 4 MHz, serial terminal |
| 4.3 (`roms/VGMON43`, sum 43) | E80C, in the ZCB PROM space | 6 MHz, Tandon |

**Monitor 4.3 does not boot this board with `B`.** A guest can type `GF800`. This booted a 56K disk on the
`micropolis` board. The 4.x Monitors take console
I/O entry points that VG CP/M calls, so CP/M needs a 4.x Monitor at E000 (`ReadMe.pdf`).

## Quirks worth carrying

- **⚠ Q1. The base in the figures is not Vector's base.** Fig 1-2 shows F400, and Table II on sheet 1 says
  "Address F4". The text, the jumper rule, the ReadMe, MBOOT's users and the Memory Maps sheet say F800.
  F800 won for a Vector machine. The manual kept a Micropolis figure.
- **⚠ Q2. What resets the 4-second timer.** P. 1-11 says reads of status or data. P. 1-12 says that
  DIG/ goes low on any read of the board, and sheet 7 drives the one-shot from DIG/. The schematic won: a read of
  the PROM also retriggers it.
- **⚠ Q3. The Memory Maps sheet shows F900-FBFF as RAM.** The schematic has no RAM on this board. FA00-FBFF
  holds the registers. The sheet shades the range and gives no use.
- **⚠ Q4. A read at base + 100 to 1FF.** No PROM answers, and no source gives the value.
- **⚠ Q5. Slips in the text.** P. 1-14 says "three quarters of a second" for C2-5. Fig 1-7 gives 10 ms, which
  is three quarters of a sector. P. 1-24 says RP/ is delayed 200 µs, longer than a byte. The value must be
  shorter than a 4 µs bit, and p. 1-15 uses 200 ns for a like delay. Neither reaches a guest.
- **⚠ Q6. The listing's "spare" bit 5.** The 1977 driver lists bits 4 and 5 of the sector register as spare.
  The 1980 manual gives bit 5 to W9. The 2013 and 2016 edits of the listings read it, and they ran on hardware.
- **⚠ Q7. Bit 4 of the command in the listings.** `diskdrv` passes only bits 0-1. Bit 4, the head, is unused by
  Vector's single-sided 330K disks.
- **⚠ Q8. RDY with no disk.** On a standard Rev. 1 board, RDY reads 1 with an empty drive, and the boot ROM
  waits for a sector flag that never comes. A board with RDY from the drive retries from `ramEnt`.
- **⚠ Q9. The 16 extra bytes** of five image files. See §8.
- **⚠ Q10. A write to a protected disk,** and the value of a read with TF false. No source gives either.
- **The checksum is the guest's.** A model must not fix or check it. A sector with a bad checksum reads as
  it is, and the guest retries.
- **SLTD/ is low true, and the test is `ANI RDY+USLT` / `XRI RDY`.** A board that sets the bit when selected
  makes the boot ROM loop at `ramEnt` forever.
- **The driver depends on timing.** `READCK` holds NOPs to pace its loop, and the boot ROM has two NOPs "for FDC
  stall logic". Each read of data must wait for the next byte, and must not return at once.
