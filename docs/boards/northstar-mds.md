# North Star MDS-A and MDS-A-D floppy controllers

**Status:** done. Board types `mdsa` (single density) and `mdsad` (double density). Both boot
CP/M and North Star DOS from the period images. What is not modeled is in
[Limitations](#limitations-and-deliberate-departures).

## The real hardware

The **North Star Micro-Disk System** was a 5¼″ floppy system for S-100 machines with an 8080
or a Z80: one controller board, and one to four Shugart SA-400 drives. The diskettes are
**hard-sectored**: ten sector holes and one index hole.

| | MDS-A (`mdsa`) | MDS-A-D (`mdsad`) |
|---|---|---|
| Year | 1977 | 1978 |
| Recording | single density (FM) | single (FM) and double density (MFM) |
| Sector data | 256 bytes | 256 or 512 bytes |
| Sides | 1 | 1 or 2 |
| Drives | 3 | 4 |
| Diskette | 35 tracks × 10 sectors | the same |
| Boot PROM runs at | `E900` | `E800` |

Both boards are TTL with no controller chip. A 4 MHz crystal, divided by two, clocks all of the
logic.

**The board has no I/O ports.** It decodes a 1 K block of memory (standard origin `E800`), and
a memory **read** is the command. The high six address bits select the board, the next two
bits select one of four *cases*, and the low eight bits are a PROM address, a data byte to
write, or a command. Data moves under **wait states**: the board holds `PRDY` until its shift
register is ready.

The MDS-A-D is the successor of the MDS-A, and North Star changed the whole command interface
for it. The two boards are documented together here, as in the reference, because everything
else is common.

## Sources

| Source | Path | Authority |
|---|---|---|
| *Micro-Disk System MDS-A*, Rev. 5, and its four schematic pages | [`reference/North Star MDS Floppy Controllers.md`](../../reference/North%20Star%20MDS%20Floppy%20Controllers.md) | The MDS-A command byte, status bytes and timing. **The schematics settle what the text leaves open.** |
| *Micro-Disk System MDS-A-D*, Rev. 1 | the same reference file | The MDS-A-D cases, orders, commands, status bytes and the checkout timing table. No schematic in the scan. |
| MDS-A boot PROM listing | [`roms/NSBOOT-SD/NSBOOTV2.PRN`](../../roms/NSBOOT-SD/NSBOOTV2.PRN) | Every address the PROM reads. It ran on the hardware. |
| MDS-A-D boot PROM listing | [`roms/NSBOOT-DD/NSBOOT.PRN`](../../roms/NSBOOT-DD/NSBOOT.PRN) | The same, for the double-density board. |
| North Star DOS 5.1 disk driver | on `tests/media/northstar/NSDOS-51-SSDD.NSI`, loaded at `2000` | The step sequence, the density check and the write stream of the MDS-A-D, as period software does them. |

Where the sources disagree:

- **Drives on the MDS-A.** The manual's command table gives `M1,M0` as a 2-bit drive number.
  The schematic (page 4) decodes three drive-select lines from the two flip-flops: `01`, `10`
  and `11`. `00` selects no drive. **The schematic won:** three drives.
- **The motor-off time of the MDS-A.** The checkout text says "about 5 seconds". The schematic
  (page 3) says "turns motors off after 16 revolutions", which is 3.2 seconds with a diskette
  turning. Both are right: the checkout step has no diskette in the drive, so the board counts
  its own 32.768 ms pulses, and 160 of those are 5.2 seconds. The board counts revolutions.
- **The spin-up wait.** The MDS-A manual says "wait 1 second (i.e. 5 sector times)". Five
  sector times are 100 ms. The PROM waits 50 sector times, which is 1 second. The board has no
  spin-up timer of its own, so this is the guest's wait and needs no model.

## Register reference

The block is four pages. `BASE` is `E800` by default.

### MDS-A

| Address | A read does this |
|---|---|
| `BASE+000`–`0FF` | returns a PROM byte (the same PROM as the next page: errata of July 31 1978) |
| `BASE+100`–`1FF` | returns a PROM byte |
| `BASE+200`–`2FF` | writes the low address byte to the disk |
| `BASE+300`–`3FF` | does a command, and returns a status byte or a disk data byte |

The command byte is `MO RD BST CC CC CC M1 M0`:

| Bits | Field | Meaning |
|---|---|---|
| 7 | `MO` | 1 = start the motors and restart the motor-off count |
| 6 | `RD` | 1 = return the next disk data byte. 0 = return a status byte |
| 5 | `BST` | 0 = A-status, 1 = B-status |
| 4–2 | `CC` | the command code, below |
| 1–0 | `M1 M0` | the argument of the command |

| `CC` | Command |
|---|---|
| 0 | select drive `M1M0` (1 to 3; 0 = none). Ignored while the motors are off |
| 1 | begin write. Taken only in the window |
| 2 | load the step flip-flop from `M0`. The head moves when the flip-flop goes from 1 to 0 |
| 3 | load the interrupt-arm flip-flop from `M0` |
| 4 | no operation |
| 5 | reset the sector flag |
| 6 | reset the controller: motors off, no drive selected, interrupt disarmed |
| 7 | load the step direction from `M0` (1 = in) |

```
A-status:  SF WN 0 MO | WRT BDY WP TR0
B-status:  SF WN 0 MO | sector position (0-9)
```

### MDS-A-D

| Address | A read does this |
|---|---|
| `BASE+000`–`0FF` | returns a PROM byte |
| `BASE+100`–`1FF` | writes the low address byte to the disk |
| `BASE+200`–`2FF` | loads the order register from the low address byte |
| `BASE+300`–`3FF` | does a command, and returns a status byte or a disk data byte |

The order register is `DD SS DP ST DS DS DS DS`:

| Bits | Field | Meaning |
|---|---|---|
| 7 | `DD` | density of the next write: 1 = double |
| 6 | `SS` | side: 0 = the first (or only) side, 1 = the second |
| 5 | `DP` | step direction (1 = in). On a write, precompensation |
| 4 | `ST` | the level of the step line. The head moves when it goes from 1 to 0 |
| 3–0 | `DS` | drive select, **one bit for each drive**: 1, 2, 4, 8. 0 = none |

The command byte is `. DM DM DM . CC CC CC`. `DM` selects what the read returns: 1 = A-status,
2 = B-status, 3 = C-status, 4 = disk data.

| `CC` | Command |
|---|---|
| 0 | no operation |
| 1 | reset the sector flag |
| 2 | disarm the interrupt |
| 3 | arm the interrupt |
| 4 | set body (a diagnostic) |
| 5 | start the motors |
| 6 | begin write. Taken only in the window |
| 7 | reset the controller: motors off, no drive selected, interrupt disarmed |

```
A-status:  SF IX DD MO | WI RE SP BD
B-status:  SF IX DD MO | WR SP WP T0
C-status:  SF IX DD MO | sector counter
```

### The status bits

| Bit | Meaning |
|---|---|
| `SF` | sector flag. A sector pulse sets it. Only the reset-sector-flag command clears it |
| `WN` / `WI` | this read is in the 96 µs window after a sector pulse |
| `MO` | the motors are on |
| `WRT` / `WR` | the write gate is open (from the end of the window to the next sector pulse) |
| `BDY` / `BD` | body: the sync character has passed, and data can be read |
| `WP` | the diskette in the selected drive is write-protected |
| `TR0` / `T0` | the selected drive is at track 0 |
| `IX` | the index hole passed in the sector before this one. True in sector 0 |
| `DD` | the diskette under the head is double density. True from 512 µs into the sector |
| `RE` | read enable. True from 480 µs into the sector |
| `SP` | spare. Reads 0 |

### Timing, from the sector pulse

| Time | Event | Source |
|---|---|---|
| 0 | sector pulse: `SF` set, the window opens, body and the write gate clear | both manuals |
| 96 µs | the window shuts. A write begins here | "12 bit times" (MDS-A schematic), "96us" (MDS-A-D) |
| 480 µs | `RE` (MDS-A-D) | MDS-A-D checkout step C3 |
| 512 µs | `DD` (MDS-A-D) | the same |
| 1184 µs | body. 96 µs + 16 zeros and 1 sync at 64 µs, or 32 zeros and 2 syncs at 32 µs | the sector format |
| 20 ms | the next sector pulse (300 RPM, 10 sectors) | both manuals |

With the motors off, no drive selected, or no diskette, the board makes its own sector pulse
every **32.768 ms** (2 MHz ÷ 2¹⁶). The sector counter then runs free: 0 to 9 on the MDS-A (a
decade counter), 0 to 15 on the MDS-A-D.

## How it is simulated

`src/boards/northstar-mds.{h,cpp}`. `NorthStarFdc` is the base for the two boards. It holds
what the boards share: the block, the PROM, the drives, rotation, the sector flag, the read and
write streams, the motor and the interrupt. Each board supplies one function, `access()`, which
is its command interface.

- **Bus cycles.** `Cycle::MemRead` in the 1 K block, and nothing else. A memory write to the
  block is not decoded, because the real board gates on `sMEMR`.
- **`peek()`** answers for the PROM pages only. `DISASM`, `HISTORY` and the debugger display
  use `peek()` and do not give the board commands. The monitor's `DUMP` is a real read by
  design, so `DUMP EB00` does give commands.
- **Rotation is a reading from the `Clock`**, not a counter. `where()` is the one place that
  computes it. A status read does not turn the disk.
- **The status is sampled before the command acts.** The MDS-A schematic clocks `MOTOR-SAMP`
  and `WINDOW-SAMP` on the leading edge of the read.
- **Wait states, and the `timing` property.** A read-data or write-data access holds `PRDY`
  until the shift register is ready. How long that takes depends on `timing`:
  - **`full`** (the default): the access moves the next byte at once, and the wait takes no
    emulated time.
  - **`real`**: the access holds until its byte's time on the disk, and the hold is charged to
    the CPU as wait states (`Board::holdReady()`). Read byte *p* (the data, then the check
    character) is ready at body + (*p*+1) byte times. A written byte is taken at the next byte
    slot after the window shuts. A byte time is 64 µs in single density and 32 µs in double.
    An access that is already late holds nothing.
- **Media: hard-sectored, but the image holds the data only.** A `.NSI` file is the sector
  data in order: track, then sector. The preamble, the sync character and the check character
  are not in the file. On a read, the board supplies the data and then the check character,
  which it computes. On a write, the board finds the sync character in the stream that the
  guest sends, and keeps the data that follows. The sector goes to the image, and to the host
  file, when its last data byte arrives.
- **The probe** is by file size: 89,600 (`sd`), 179,200 (`dd`), 358,400 (`quad`), with the
  usual XMODEM tolerance. The `mdsa` has one format, so a file of any size mounts as `sd`. On
  the `mdsad`, an empty file is a blank diskette, and its first write sets the density. A file
  of another size does not mount without `media`.
- **Side 2** is in the file after side 1, **from the innermost track out**: physical track 34
  of side 2 is track 35 of the file. North Star numbers the tracks of a two-sided diskette 0
  to 69 in that order, so that the head does not move when the side changes.
- **Interrupt.** The line is `SF` and the arm flip-flop. `interrupt` takes
  `none | int | vi0..vi7`, as the jumper on the board does. The board schedules a wake at
  each sector pulse only while it is armed and strapped. In any other state it holds no
  deadline, so an idle machine can stop its clock.
- **No DMA.**
- **Properties:** `base` (a multiple of `400`), `drives`, `interrupt`, `motor`. For each
  drive: `unit`, `mount`, `readonly` (`writeprotect`), `media`, `create`, `faults`.
- **Units** are `drive0` … . `drive0` is the drive that North Star software calls drive 1.
- **Debug flags:** `seek` (each head step), `sector` (each sector read or written), `fault`
  (each use of a bad sector).

### Reset

- `Reset::PowerOn` (POC*): the controller reset on both boards. The motors stop, no drive is
  selected, the interrupt is disarmed. The heads stay where they are, and the images stay
  mounted. The sector flag is not cleared.
- `Reset::Bus` (RESET*): **nothing on the `mdsa`.** Its `RST` line is POC or the reset command
  (schematic page 1). On the `mdsad` it does the controller reset. The MDS-A-D scan has no
  schematic. Its checkout procedure holds the reset switch down to keep the board in a known
  state, and that is the only evidence.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The status byte is sampled before the command of the same read acts | The MDS-A PROM starts the motors and asks "were they on?" in one read. It would always see "on", and skip the 1-second wait and the drive select |
| A select on the MDS-A is ignored while the motors are off | The select register is held clear by `MOTOR-ENB`. A model that selects anyway reports track 0 and write-protect for a drive that the board did not select |
| When the MDS-A motors stop, the drive is de-selected | The PROM and the DOS select the drive again after each motor start. A model that keeps the select hides a guest that does not |
| Begin-write is taken only in the 96 µs window | A write that starts late on the real board is refused. A model that takes it writes the sector that the guest was too late for |
| The head moves when the step pulse **ends** | One step for each low-high-low. A model that moves on both edges steps two tracks |
| MDS-A-D drive select is one bit for each drive | Drive 3 is `4`, not `3`. North Star DOS converts the number itself |
| `IX` is true in sector 0 only, and never without a diskette | The MDS-A-D PROM waits for `IX` to prove that a hard-sector diskette is present. With `IX` always false, no diskette boots. With `IX` always true, the PROM does not stop at its "no index" loop for an empty drive |
| `RE` comes at 480 µs with or without a diskette | The PROM's wait for `RE` has no timeout. Without `RE`, an empty drive stops the machine in that loop and not in the PROM's error loop |
| The check character is XOR, then rotate left | It is not a CRC. With any other sum, every sector fails its check |
| The sector counter runs free without a diskette | The MDS-A PROM waits 50 sector times for the motor before it selects a drive. With no pulses, it waits without end |
| Side 2 is recorded from the inside out | A program that is stored on side 2 loads as wrong data. `FINDBAD.COM` on the two-sided CP/M image is the test |

### Double-density North Star DOS reads `2000` before anything writes it

The MDS-A-D boot PROM uses no RAM. It loads the boot sector from byte 1 of its page, so byte 0
(`2000` for North Star DOS) is never written. Double-density North Star DOS keeps the track of
drive 1 at `2000`, and its first disk read uses that byte. With `80` or more there, the DOS
steps the head in from track 0, loads the wrong sectors, and runs them. The DOS stores the
correct track at `2000` before it steps, so a second boot works.

This is the software and not the board, and a real machine did the same on whatever its RAM
held at power-on. The `northstardd` machine sets `fill = "zero"` on its memory for this
reason. CP/M and single-density North Star DOS are not affected: the CP/M BIOS has its own
table, and the MDS-A PROM writes `59` (not initialized) to the table before it loads the DOS.

## Limitations and deliberate departures

- **No bits on the medium.** There is no FM or MFM stream, no phase-locked loop, and no
  precompensation. A sector is present with a valid check character, or it is absent.
- **One density in one image.** A `.NSI` file has one sector size. The `mdsad` does not write
  a single-density sector to a double-density image, or the reverse. It tells the host one
  time. A real diskette can hold both. Software that mixes densities on one diskette would
  notice.
- **A single-density diskette does not boot on the `mdsad`.** For a single-density diskette,
  the MDS-A-D PROM steps to track 1 and reads **512** bytes from sector 8. A single-density
  sector in an image is 256 bytes, and no image in the test media was made for this boot. The
  board gives the PROM the 256 data bytes, the check character, and then zeros. The PROM's
  check passes on that stream, and the PROM jumps into what it loaded, which is not a boot
  program. What the real board returns after the check character is not known: 512
  single-density bytes take 33 ms, and a sector is 20 ms. A single-density diskette works
  on the `mdsad` in every other way: North Star DOS lists, reads and writes one in a second
  drive.
- **A write that stops early is dropped.** If the next sector pulse comes before the last data
  byte, nothing is written. On real media the sector would be left with no valid check
  character. A payload-only image cannot hold such a sector.
- **A slow guest does not lose data.** Under `timing = full`, bytes come as fast as the guest
  reads them. Under `real`, they come no faster than the disk. But under either setting, a guest
  that is too slow gets the next byte, not a lost one; on the real board it loses data (the MDS-A
  manual gives a 64 µs loop limit).
- **No spin-up time and no head-settle time in the board.** The real board has neither. The
  drive does, and the period software waits for it with sector counts.
- **The motor-off jumper of the MDS-A-D** (3.2 to 38.4 seconds) is not a property. The time
  is the standard 9.6 seconds.
- **`DM` values 0 and 5–7 on the MDS-A-D return 0.** The manual gives them no meaning.
- **"Set body"** (the MDS-A-D diagnostic) makes `BD` true, and a data read then returns the
  sector data. On the real board it would return the bits from where the head is.
- **A drive with a one-sided image has no side 2.** A read there finds no body. On a real
  one-sided drive, the side select does nothing and side 1 answers.
- **The built-in PROM is the standard part.** If you move `base`, the block moves, but the
  PROM code still calls `E800`.
- **The MDS-A-D answers RESET\*.** See [Reset](#reset): this is from the checkout text, not a
  schematic.

## Bad sectors: `FAULT` and the `faults` key

A drive can have bad sectors on its disk, to test the retry code of a guest (#970). The faults
are a table in each drive (`DiskFaults`, `src/host/diskfaults.h`), and the image does not
change. Give them with `FAULT fd0:driveN`, with the `disk_fault_*` MCP tools, or with the
`faults` key of a `[[board.drive]]` table. The *User Manual* gives the syntax, in "Simulate a
bad sector" in the Disks chapter. The code is in `NorthStarFdc`, so the `mdsa` and the `mdsad`
have the same faults.

The board has no error bit, and it never checks the check character. The guest computes the
check character of the data and compares it with the one that it reads from the board. A fault
therefore changes what the guest reads:

| Kind | What the guest reads | When the fault is used |
|---|---|---|
| `data` | The data, then a wrong check character: the correct one XOR `FF` | When the guest reads the check character |
| `missing` | No sync character, so the body bit never comes for that sector. A data read gives 0. | Once a pass, when the guest reads the body bit after the body time |
| `write` | The board does not write the sector. The old bytes stay in the image, and the guest is not told, as on the real board. | At the last data byte, where the sector would go to the image |
| `id` | Refused | |

- **`id` is refused** with `a North Star sector has no ID field. Give data, missing or write`.
  The track is where the head is, and the sector is the hole count. `DiskFaults::refuse()` makes
  `FAULT ADD`, `disk_fault_add` and the `faults` key refuse the kind with that reason. The shared
  text of `HELP FAULT` and of the `faults` key still lists `id`, and it says that the controller
  reports a `write` fault. On this board, neither is true.
- **The address** is `<track>/<side>/<sector>`: the track under the head, the side, and the hole
  count, 0 to 9. The side is 0, or 1 on a two-sided `mdsad` diskette. The track is the track of
  the head, not the track in the image file. The second side is in the file from the inside out
  (see "Side 2" above), but this does not change the address of a fault.
- **A `data` hit is exact.** It counts one time in each pass where the guest reads the check
  character. The MDS-A-D boot PROM reads sector `0/0/0` but not its check character, so a fault
  there gets no hit from the PROM.
- **A `missing` hit can be one more than the guest's tries.** The body bit is in A-status with
  the sector flag. The CP/M BIOS reads A-status to wait for the sector flag, and to wait for the
  body. The board cannot tell the two reads apart. If a wait for the sector flag goes past the
  body time of the bad sector, that pass counts too. This depends on where the disk is in its
  turn when the guest starts. Because of this, a `count=N` `missing` fault can end one try
  early. The board does not count a `missing` fault at a data read. The BIOS times out on the
  body bit and reads no data.
- **The CP/M BIOS tries a bad sector again.** The Lifeboat CP/M 2.23 BIOS on the `mdsad` tries
  10 times. The CP/M 2.2b BIOS on the `mdsa` tries 16 times. The directory is at `2/0/0` on
  `CPM223DQ-48K-SSDD.NSI`, and at `3/0/7` on `CPM22b14-48K-SDC-ALTAIR.NSI`. A hard `data` fault
  on the directory gets one hit for each try, and `DIR` gives `Bdos Err On A: Bad Sector`. A
  hard `missing` fault there gives the same error. It got 10 or 11 hits on the `mdsad`, and 17
  on the `mdsa` under `--mcp`.
- **The acceptance test `acceptance-northstar-fault`** uses the `mdsad`. A `count=2` `data` fault
  on `0/0/0` gets 2 hits, and the BIOS gets past it at cold start. A hard `data` fault on `2/0/0`
  gets 10 hits. A hard `missing` fault there gives the BDOS error, and the test does not check
  its hit count.

`SET fd0 DEBUG=fault` writes a line each time that a fault is used, for example
`fault drive0 0/0/0 data hit 1, 1 left`. `MOUNT` and `UNMOUNT` clear the faults of the drive.
`CONFIG SAVE` writes the `faults` key. A snapshot keeps the faults with their counts, and a read
of a bad sector that is in progress.

## Verification

- **Unit suite `northstar`** (`tests/test_northstar.cpp`), through real bus cycles: the
  block and `base`; `peek()`; every command code and status bit of both boards, with the
  addresses from the PROM listings; the sector flag, the window, `RE`, `DD` and body, each to
  one T-state; a sector read with the check character computed as the PROM does; writes, a
  late begin-write, a write cut short, write-protect; step and track 0; drive select; side 2;
  the interrupt and its deadline; the motor timer; reset; the probe; snapshot and restore.
  The bad sectors on both boards: each kind, `count=N`, side 1, the `id` refusal, `MOUNT` and
  the `faults` key, and a snapshot taken during a bad read.
- **`acceptance-northstar`**: the `mdsa` boots CP/M 2.2b (`DIR`) and North Star DOS 5.1
  (`LI`, `GO BASIC`, `PRINT 6*7`).
- **`acceptance-northstardd`**: the `mdsad` boots Lifeboat CP/M 2.23 from a one-sided
  diskette (`DIR`) and from a two-sided diskette, where it runs `FINDBAD`, which is stored on
  side 2 and reads every block of both sides. It boots North Star DOS 5.1 (`LI`, `GO BASIC`).
- **`acceptance-northstar-fault`**: the `mdsad` boots CP/M past a `count=2` `data` fault, and
  `DIR` gives the BDOS error on a hard `data` fault and on a hard `missing` fault. The image
  does not change.
- **Mutations that the tests catch:** the check character rotated the wrong way; side 2 not
  reversed; the MDS-A-D case map on the MDS-A; one sync character at double density; the
  status sampled after the command. For the faults: the check character not changed, a
  `missing` sector still read, a `write` fault still written, `id` not refused, and the faults
  not saved or not restored.
- **By hand, over `--mcp`:** a file written with `PIP … [V]` under CP/M, and a BASIC program
  saved with `NSAVE` under North Star DOS, each read back after a cold boot in a new process,
  on both boards. A blank diskette formatted with `IN 2` (`mdsa`) and `IN 2 D` (`mdsad`). A
  single-density diskette listed in drive 3 of the `mdsad`.

## References

- [`reference/North Star MDS Floppy Controllers.md`](../../reference/North%20Star%20MDS%20Floppy%20Controllers.md)
- [`roms/NSBOOT-SD/README.md`](../../roms/NSBOOT-SD/README.md),
  [`roms/NSBOOT-DD/README.md`](../../roms/NSBOOT-DD/README.md)
- [`machines/northstar.toml`](../../machines/northstar.toml),
  [`machines/northstardd.toml`](../../machines/northstardd.toml)
- [`docs/sources.md`](../sources.md) for where the manuals, the PROMs and the disk images came
  from.
