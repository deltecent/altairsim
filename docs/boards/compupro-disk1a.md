# CompuPro Disk 1A floppy disk controller

**Status:** built (`disk1a`) for 8″ and 5.25″ drives. The board boots CompuPro CP/M 2.2LD from an
8″ disk through routine 0 of its own boot EPROM. `tests/test_disk1a.cpp` tests the board, and
`acceptance-disk1a` boots the tracked disk through the test machine `tests/media/compupro/disk1a.toml`. A 5.25″ CompuPro CP/M 2.2Q
disk, built from CompuPro's 1985 sources, boots through routine 3 from unit 2. That disk is not in
the tree (see *Limitations*).

## The real hardware

The **CompuPro Disk 1A** (board 203, 1984) is the second version of CompuPro's Disk 1 floppy
controller. It has three parts:

- an **NEC µPD765A** floppy disk controller (the Intel 8272 is the same part). It runs 8″ and
  5.25″ drives, in single and double density.
- a **DMA engine** with a 24-bit address counter. The board takes the bus and moves each byte
  between the 765 and memory. There is no byte count and no terminal count.
- a **boot EPROM** (a 2764 of 8 KB). Switches pick one 512-byte routine of the sixteen. The
  routine answers memory reads at `0000`–`01FF` over `PHANTOM*`.

The board decodes four ports at a base that is a multiple of four. The CompuPro base is `C0`.
The Disk 1 has a bit-banged serial port on its port 3. The Disk 1A has a Drive Select Register
on port 0 and a Motor Control Register on port 3 instead. The Disk 1 is the `disk1` board
(`docs/boards/compupro-disk1.md`). The two boards share one class, `CompuProDiskBoard`. The console is on a separate board,
usually a System Support 1.

## Sources

| Source | Path | Authority |
|---|---|---|
| CompuPro *Disk 1A Technical Manual* (1984) | `reference/CompuPro Disk 1 & 1A Floppy Controllers.md` | The ports, the register bits, the switches and jumpers, and the boot routine table. The section numbers in the source are this file's. |
| Viasyn *Disk 1A Technical Manual* (May 1985, board 203 Rev F) | the same file, cited "1985 manual" | The schematic. It gives what J11 controls, the motor timer, the reset of the two board registers, and the J1–J5 and J12 jumpers. |
| CompuPro *CP/M 2.2 Technical Manual* (October 1984; 1983 compared) | `reference/CompuPro CPM-22 Technical Manual.md` | The CP/M side: the four floppy formats, the system-disk layout, the boot chain, the CBIOS console ports, and the switch settings for a CP/M machine, with the Disk 1A standard settings. Its Disk 1A S1 table follows an earlier EPROM map, not `291B_1.HEX`. |
| NEC µPD765A data sheet (1982) | `reference/NEC uPD765A FDC.md` | The chip: commands, result bytes, ST0–ST3, the MSR handshake. |
| `291B_1.HEX` | `roms/DISK1A/` | The boot EPROM, all sixteen routines. |
| CompuPro `ROM1.ASM` (August 1984) | not tracked | Routine 0 of an earlier EPROM revision. It agrees with `291B_1.HEX` on the ports and the Read Data command. Its Specify bytes differ. |
| CompuPro `HMXBIOS.ASM`, `ACTIVE.LIB` | not tracked | The disk formats in the `MAKEDPB` table. The size probe uses them. |

`docs/sources.md` has the provenance of each one.

## Register reference

Ports at the base `B` (default `C0`):

| Port | OUT (write) | IN (read) |
|---|---|---|
| `B+0` | Drive Select Register | the 765 main status register |
| `B+1` | the 765 data register | the 765 data register |
| `B+2` | the DMA address: three bytes, the high byte first | board status |
| `B+3` | Motor Control Register. D0 = 0 switches the boot EPROM off | nothing (the board does not decode it) |

**Drive Select Register** (`B+0` OUT):

| Bit | Meaning |
|---|---|
| 0–1 | The unit, used when bit 2 is set |
| 2 | Alternate select: bits 0–1 go to the drive cable instead of the 765's US0/US1 |
| 3 | Force Two Sided: holds the 765's TS input high |
| 4 | Alternate head load (not modeled) |
| 5 | 5.25″ data rate: the 765 runs at 4 MHz instead of 8 MHz |

**Board status** (`B+2` IN):

| Bit | Meaning |
|---|---|
| 0 | The drive is ready |
| 1 | The index pulse. It is present only while the drive's motor runs |
| 2 | Switch S3-1 (`sense_switch`). On reads 0 |
| 7 | The 765's INT |

Bits 3–6 read 0.

**Motor Control Register** (`B+3` OUT): bits 4–6 are the motors of 8″ drives 0–2. Bit 7 is
8″ drive 3 and also every 5.25″ drive. The motors stop about 15 seconds after the last access
to the board. With `mini_motor_control` off (jumper J11 removed), the 5.25″ motors run all the
time. The 8″ motors always follow their bits and the timer.

## How it is simulated

- **The chip** is `Upd765` (`src/chips/upd765.h`). The board reaches its drives through
  `Upd765::Wiring`. Because of this, the Drive Select Register can change the unit and the TS
  line between the chip and the cable.
- **The drives** are four `DiskImageDrive`s over raw `.DSK` images, units `drive0`–`drive3`.
  Mount an `.IMD` file, and `MOUNT` converts it to a `.DSK` next to it.
- **DMA.** The board has a `Mover`, a `BusMaster`, as the Tarbell DD does. The 765's DRQ asks
  for the bus. Each grant moves one byte in one three-state memory cycle, and the counter counts
  up.
- **The boot EPROM** answers memory reads at `0000`–`01FF` and asserts `PHANTOM*`. Memory
  writes go to the RAM below it. Because of this, the RAM of the machine must be set
  `honors_phantom = "read"`. Routine 0 writes a vector at `005D` while the EPROM is on, and
  reads it back after the EPROM is off.
- **The interrupt.** The 765's INT goes to the line that `interrupt` names (jumper J10). The
  default is `vi4`, as CompuPro shipped the board. Routine 0 polls status bit 7 and does not use
  the interrupt.

### The size probe

A raw image has no geometry, so the byte count gives the format. The formats come from the
`MAKEDPB` table of CompuPro's BIOS:

| Disk | Cylinders | Track 0, head 0 | The other tracks |
|---|---|---|---|
| 8″ | 77 | SD 26 × 128 | SD 26 × 128, or DD 26 × 256, 15 × 512, or 8 × 1024 |
| 5.25″ | 80 | the same as the other tracks | SD 16 × 128, or DD 16 × 256, 8 × 512, or 5 × 1024 |

Each format can have one side or two. Two sides are cylinder-interleaved (C0H0, C0H1, C1H0…),
the order of the CompuPro BIOS. The directory of the two-sided `CCPM22Q` disk is where this
order puts it.

Three 5.25″ formats have 327,680 bytes, and two have 655,360 bytes. Without `media`, the probe
refuses these two sizes, because a raw image cannot say which format it is. The error names the
`media` values that fit. An image that is smaller than the largest format and has no known size
is a blank disk. A blank has no size to probe, so it needs `media`, as on the VersaFloppy. Without
`media`, the probe refuses a blank, and the error names the values to set (issue #777). A guest
can format a blank track by track with Format a Track. A track that is never formatted takes no
bytes in the image. Because of this, a format of fewer tracks is written end to end, and the probe
reads it at the next mount.

The TS line to the 765 is high when Drive Select bit 3 (Force Two Sided) is set. Otherwise, it is
the sides of the diskette: from the size of the image, or from `media` for a blank. A format does
not change it.

An 8″ `FORMAT.COM` reads TS to decide how many sides to write. CompuPro's BIOS and the EPROM set
bit 3 for every 5.25″ drive.

### The `media` of a drive (issue #755)

The diskette is a physical part of the drive, so its size and sides belong to the drive and not
to the image. The `media` key of `[[board.drive]]` sets them. It takes two kinds of value:

| Kind | Values | Use |
|---|---|---|
| Size and sides | `8ss`, `8ds`, `5ss`, `5ds` | A blank disk. The probe looks only at the formats of that size and that number of sides |
| A whole format | the size and sides, then `-sd`, `-dd256`, `-dd512` or `-dd1024` (for example `5ds-dd512`) | An image size that more than one format has. The probe takes that one format |

`-sd` is the single-density row of the size: 26 × 128 on 8″, 16 × 128 on 5.25″. The `disk1` board
offers only the 8″ values. The schema takes any case, and the board stores the lower-case name.

With `media`, the probe does these things:

- An image of a format that fits mounts as that format.
- A smaller image mounts as a blank of the forced shape. The blank has 77 cylinders for 8″ or 80
  for 5.25″, and the forced number of sides. It has the rate and motor bit of the forced size.
  For example, a 5.25″
  blank turns at 5 rev/s, and Motor Control bit 7 runs it.
- A formatted image of a different shape, or a larger image, is refused. The old disk stays.
- `5ds` alone on 655,360 bytes is still refused, because two formats fit it.

The media stays with the drive across `MOUNT` and `UNMOUNT`, and `CONFIG SAVE` writes it, also for
an empty drive. When the mount of a `[[board.drive]]` table is refused, the drive keeps its old
media. `MOUNT` takes `media` too, and gives it to the board before the disk goes in:

```
MOUNT fd0:drive1 BLANK.DSK CREATE media=5ds
```

### Properties

| Property | Default | What it sets |
|---|---|---|
| `port` | `C0` | The base (switches S3-2 to S3-7). A multiple of 4 |
| `rom` | `builtin:DISK1A` | The boot EPROM, or a 2764 image file |
| `routine` | `0` | The 512-byte routine at `0000` (switches S1-2 to S1-5), 0–15 |
| `boot` | `on` | The boot EPROM (switch S3-8). Off: a plain disk controller |
| `sense_switch` | `on` | Switch S3-1. On: routine 0 tells the BIOS that the console is on a System Support 1 |
| `mini_motor_control` | `on` | Jumper J11, in the 5.25″ motor line. On is J11 installed: bit 7 runs the 5.25″ motors. Off: the 5.25″ motors run all the time. The 8″ motors always follow bits 4–7 |
| `interrupt` | `vi4` | Jumper J10 |

The `[[board.drive]]` table takes these keys:

| Key | What it sets |
|---|---|
| `unit` | The drive, 0–3 |
| `mount` | The disk image |
| `readonly` | Write-protect the disk (also `writeprotect`) |
| `media` | The diskette's size and sides, or a whole format. See *The `media` of a drive* |
| `create` | Make an empty file if it is not there, then mount it. It is not saved back |

### Reset

- `Reset::PowerOn` and `Reset::Bus` both switch the boot EPROM on again, as any reset does on
  the board. They also reset the 765, clear the Drive Select Register, and stop the motors. The
  disks stay mounted.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The EPROM shadows reads only. Writes under it reach the RAM | Routine 0 loses the vector it writes at `005D`, and a failed boot cannot try the DISK 3 |
| No terminal count. A DMA read runs to the EOT byte of the command, and the 765 ends it with ST0 = `40` and ST1 = EN | Routine 0 takes ST0 = `40` with ST1 = `80` as a good read. Other result bytes make it try the DISK 3 |
| The DMA address is a three-deep stack, written high byte first | The boot loads at the wrong address |
| Track 0 of an 8″ disk is single density, 26 × 128, on every format | Routine 0 reads track 0 at single density, and the boot fails on any other format |
| The 765 checks MF against the recorded density of the track | A read at the wrong density ends with Missing Address Mark (ST1 MA), as on the hardware |
| Status bit 1 (index) stops when the motor is off | A guest that reads the index pulse sees a drive with its motor stopped as a drive that does not turn |

## Bad sectors: `FAULT` and the `faults` key

A drive can have bad sectors on its disk, to test the retry code of a guest (#966). The faults
are an overlay in the drive (`DiskImageDrive`, `src/host/diskfaults.h`), and the image does not
change. Give them with `FAULT <id>:driveN`, with the `disk_fault_*` MCP tools, or with the
`faults` key of a `[[board.drive]]` table. The *User Manual* gives the syntax, in "Simulate a
bad sector" in the Disks chapter.

The µPD765 reports each fault with the status that it gives for a damaged disk:

| Kind | What the guest reads |
|---|---|
| `data` | ST0 AT, ST1 DE, ST2 DD. The data still transfers. |
| `id` | ST0 AT, ST1 DE |
| `missing` | ST0 AT, ST1 ND |
| `write` | ST0 AT and EC. The drive writes nothing. |

`SET <id> DEBUG=fault` writes a line each time that a fault occurs. The `fault` flag is off
until you turn it on. `CONFIG SAVE` writes the `faults` key, and a snapshot keeps the faults
with their counts.

## Limitations and deliberate departures

- **One 5.25″ format is tested.** A CP/M 2.2Q disk built from CompuPro's 1985 sources (two sides,
  80 cylinders, 5 × 1024) boots through routine 3 from unit 2. It formats a blank disk in unit 3
  and copies files to it, and its warm boot reads the CCP again. The disk and the record of how it
  was built are kept outside the tree, as the CompuPro master disk is. The other 5.25″ formats in
  the size probe come from the `MAKEDPB` table of `HMXBIOS.ASM`, and no real disk has checked
  them. That build of the BIOS sets 80 tracks (`TRK5 EQU 80` in `ACTIVE.LIB`). The BIOS
  can also be built for 40-track and 77-track 5.25″ drives, but the probe knows only 80 tracks.
  The rule that a 5.25″ disk has no single-density track 0 comes from the BIOS as well. Without
  `media`, the probe refuses 327,680 and 655,360 bytes, as *The size probe* says.
- **A16–A23.** The counter counts all 24 bits, but this is a 64 K bus (DESIGN.md §4.0). Only
  A0–A15 reach memory.
- **No wait states and no DMA priority** (switches S2). The machine has one bus master and no
  slow memory, so there is nothing to wait for and nothing to arbitrate.
- **Alternate head load** (Drive Select bit 4) does nothing. The drives have no head-load
  solenoid.
- **A motor that is off stops the index pulse but not READY.** An 8″ drive keeps READY with its
  motor stopped (the `SOFTRDY` comment of `HMXBIOS.ASM`). A 5.25″ drive would drop READY, and
  that is not modeled.
- **The data rate does not check the media.** On the board, a 5.25″ disk read at the 8″ rate
  fails. Here, Drive Select bit 5 changes only the Specify timing of the 765.
- **Only routines 0–3 can run.** Routines 4–7 are 8086 code, 8–11 are 68000 code and 12–15 are
  32016 code. The machine has no processor board for them. Routine 1 boots only the DISK 3 hard
  disk, which is not emulated. Routine 2 tries 8″ drive 0 and then the 5.25″ drive in unit 2.
  Routine 3 tries the 5.25″ drive in unit 2 and then the DISK 3. Routines 0, 2 and 3 are tested.
- **No Disk 2 or DISK 3.** CP/M 2.2NZ, which boots from a Disk 2 hard disk, does not boot.
- **The CompuPro CP/M 2.2Q master disk needs the 8088 of the CPU 8085/88 board.** Its BIOS, as
  shipped, moves records with the 8088 (`IN FD`). The machine has no 8088, so its `DIR` repeats
  one record. The same BIOS source, built with `I8088 FALSE`, runs on the Z80. The 5.25″ disk
  above is that build.
- **Jumpers J5 and J12 are not modeled.** Both are removed as shipped. J5 holds the 5.25″
  READY on for unit 3. J12 lets the motor timer clear the Drive Select Register after about
  2 seconds with no access to the board.

## Verification

- `tests/test_disk1a.cpp` (suite `disk1a`) uses real bus cycles. It tests these things:
  - the port map at a moved base, each status bit, and the DMA stack
  - a DMA read, a DMA write and the 24-bit counter
  - the size probe, the refusal of a blank with no `media`, a `5ds` and an `8ss` blank
    formatted, the TS line from the media, and a `5ds` blank run by motor bit 7
  - `media`: a 655,360-byte image mounts as `5ds-dd512` and reads side 1, 327,680 bytes mounts
    as `5ss-dd256` and as `5ds-sd`, a `5ds` blank reads two-sided, and an 8″ image is refused as
    5.25″ media
  - the read-only shadow of the EPROM, and the built-in EPROM
  - the 5.25″ boot: routines 2 and 3 of the built-in EPROM read track 0 of unit 2 to `0100` and
    jump to it, and routine 2 does this when unit 0 is empty
  - the motor timeout, and J11: with J11 removed, a 5.25″ motor runs and an 8″ motor still
    follows its bit and stops after 15 seconds
  - a snapshot round trip and the property checks
- `tests/test_upd765.cpp` (suite `upd765`) tests the chip against the data sheet.
- `acceptance-disk1a` boots `tests/media/compupro/CCPM22LD.DSK` through routine 0 to the
  `64K CP/M 2.2LD` sign-on. It runs `DIR` and `STAT DSK:`. It copies `STAT.COM` with `PIP`, runs
  the copy, and erases it. It then mounts a blank disk in drive 1, and CompuPro's `FORMAT.COM`
  (version 2.2B) formats it in mode 0, 8″ single density. The image grows to 256,256 bytes, and
  `STAT B:` reports 241k free.

## References

- `reference/CompuPro Disk 1 & 1A Floppy Controllers.md`
- `reference/CompuPro CPM-22 Technical Manual.md`
- `reference/NEC uPD765A FDC.md`
- `roms/DISK1A/README.md`
- `docs/devguide/soft-sector-floppy.md`
