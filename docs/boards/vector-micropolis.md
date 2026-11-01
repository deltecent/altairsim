# Vector Graphic Micropolis Disk Controller

**Status:** done.

## The real hardware

The **Micropolis Disk Controller Board** is the floppy controller of the Vector 3 VIP, the System B and the MZ. The schematic is a Micropolis drawing, and the manual is Vector Graphic's. This model follows the Vector manual, Rev A, November 1980, and the Rev. 1 jumpers of advisories VTA MZ-006 and MZ-007.

The board has no I/O ports and no DMA. It takes a 1K block of memory on any 1K boundary from C000 to FC00. Jumpers W1 to W4 set the block. A 56K Vector machine uses F800, and a 48K machine uses D800. The block holds a 256-byte boot PROM and four registers. The processor moves each byte of a sector, and the PRDY line holds the processor until the byte is ready.

The drives are 5.25-inch, 77-track, 16-hard-sector drives at 300 RPM. A sector takes 12.5 ms, and a byte takes 32 µs. The guest makes the checksum and checks it. The board does neither.

## Sources

| Source | Path | Authority |
|---|---|---|
| Distilled emulation reference | `reference/Vector Graphic Micropolis Controller.md` | The address map, the registers, the timing, the image format and the boot sequence |
| *Micropolis 5¼" Floppy Disk Controller Board*, Rev A, November 1980 | `Floppy Controller.pdf` | The text, Fig 1-7 and Fig 1-12 for the timing, and schematic 100087 |
| `MBOOT.ASM` and `MBOOT.BIN` | `roms/MBOOT/` | The boot ROM. It settles the polarity of SLTD/ and the boot contract |
| `Micropolis diskdrv.asm` | Not tracked | The driver of 1977, for the select, restore and read sequences |

Three conflicts are settled in the reference. The figures show base F400, and Vector's software and text use F800, so F800 won (Q1). The text says that status and data reads retrigger the 4-second timer, and the schematic drives it from DIG/, so any read of the block retriggers it (Q2). The Memory Maps sheet shows RAM at F900-FBFF, and the schematic has no RAM on this board, so the schematic won (Q3).

## Register reference

Offsets are from `base`. The default base is `F800`.

| Addr | Write | Read |
|---|---|---|
| `000`-`0FF` | Ignored | Boot PROM |
| `100`-`1FF` | Ignored | `FF`. No chip answers (see Limitations) |
| `200`, each +4 | Command | **Sector register.** Bits 0-3 sector, bit 4 W10 (reads 1), bit 5 W9 (1 = 2 MHz), bit 6 INT, bit 7 SF |
| `201`, each +4 | Command | **Status register.** Bits 0-1 unit, bit 2 SLTD/ (0 = selected), bit 3 track 0, bit 4 write protect, bit 5 RDY, bit 6 PINTE (reads 0), bit 7 TF |
| `202`, `203`, each +4 | Write data | Read data |

The command is in bits 7-5.

| Bits 7-5 | Command | Modifier |
|---|---|---|
| 1 | Select drive and head | Bits 0-1 unit, bit 4 head |
| 2 | Interrupt control | Bit 0: 1 enable, 0 disable and clear |
| 3 | Step one track | Bit 0: 1 in (track number goes up), 0 out |
| 4 | Set WRITE | None |
| 5 | Reset controller | None |

**Timing of a sector, from the sector hole.** SF is true for 30 µs. On a read, TF is true from 1268 µs, and the sync byte is complete at 1300 µs. On a write, TF is true from 1232 µs. TF and WRT end at the next hole, 12.5 ms after the last one.

## How it is simulated

- **Decodes** memory reads and writes in the 1K block. `peek()` shows only the PROM, so a monitor `DUMP` changes no state of the timer.
- **PRDY.** Each data access in a sector calls `holdReady()` up to the time the byte passes the head, and the run loop charges the hold to the processor. The position comes from the `Spindle`, a pure function of the Clock, so a read counts no bytes.
- **A read returns the byte that next finishes under the head.** A guest that falls behind loses bytes, and no flag says so.
- **A write** collects the slot in a buffer. The sector boundary stores it through `DiskImage`, with zeros after the last byte that the guest sent. A write that sent no byte stores a sector of zeros.
- **One Clock event, only when it has work.** It runs at the sector boundary while a write is on or interrupts are enabled. An idle board queues nothing, so the machine can stand down.
- **The 4-second timer** is an absolute T-state, read at each bus access. It needs no queued event.
- **Media.** Hard-sectored. A `DiskImage` with 275-byte sectors, `startSector` 0, 16 sectors on a track, 77 tracks. A file of 338800 bytes, or up to 127 bytes more, has one side. Any other size mounts as two sides, all of side 0 first, and grows when the guest writes. The extra bytes of a one-sided file stay as they are.
- **Boot PROM.** Fixed as `builtin:mboot`. The code runs at any base, so no socket or property changes it.
- **Interrupts.** The `interrupt` property takes `none`, `int` or `vi0` to `vi7`. The default is `none`.
- **DMA:** none. The real board has none.

### Properties and units

| Name | Meaning |
|---|---|
| `base` | The 1K block, default `F800` |
| `speed` | W9: `4mhz` or `2mhz`, reported in sector register bit 5 |
| `ready` | `forced`, the Rev. 1 standard, or `drive`, true only with a disk in the drive |
| `interrupt` | The interrupt jumper |
| `drive0` to `drive3` | Units for `MOUNT`. The boot ROM reads `drive0` |
| `[[board.drive]]` | In a machine file: `unit`, `mount`, `readonly`, `create` |
| `DEBUG=sector`, `DEBUG=seek` | Trace each write, or each head step |

### Reset

- `Reset::PowerOn` (POC*, cold): RST/ goes low. The drive is deselected, and interrupts and the write latch clear. The timer starts as timed out. A head stays on its track.
- `Reset::Bus` (RESET*, warm): nothing. Only POC reaches the board. The guest uses the reset command.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| SLTD/ is low true | The boot ROM loops at `ramEnt` forever |
| A data access waits for the byte | `READCK` and the boot ROM pace their loops for this. A read that returns at once gives the wrong bytes |
| SECP comes from the selected drive | With no disk turning, the guest sees no SF, and `wtSec` waits forever. This is the real behavior of an empty drive |
| `ready = forced` reads RDY true on an empty drive | The boot ROM goes past the RDY test and waits for a sector. With `ready = drive` it waits at the RDY test |
| Any read of the block retriggers the 4-second timer, and a write or a select does not | A guest that only writes loses the select after 4 seconds |
| The timed-out timer holds RST/ low, so a command is lost until the next read | This is the level the manual gives (1-11). Power starts in this state. `SLCT` reads status first, so it selects the drive |
| The checksum is the guest's | A sector with a bad checksum reads as it is, and the guest retries. A board that fixes it hides a bad disk |
| TF stays false on a sector with no sync byte (slot byte 0 is 00) | An unformatted sector must not start a read |

## Limitations and deliberate departures

- **PINTE reads 0.** The Board interface does not carry the state of the line. Vector and Micropolis software does not use interrupts.
- **A read of `100`-`1FF` gives `FF`, and a read of data with TF false gives `00`.** No source gives either value (reference Q4, Q10).
- **A write to a protected disk is dropped, with one log line.** No source says what the controller does (reference Q10). `WSECT` tests the bit and does not try.
- **A late Set WRITE is not refused.** The manual gives 100 µs from the hole as the last time. Here a later command stores a whole slot.
- **The sector number is always valid.** The model has no index logic, so a guest does not need the 250 ms wait after a select. The drivers do the wait.
- **The disk always turns,** from `MOUNT` on. The Rev. 1 motor option is not modeled, because no status bit shows it.
- **Step rate and settle time are not checked.** A guest that steps too fast still moves the head.
- **The head stops at tracks 0 and 76.** The 35-track Mod I drive is not modeled.
- **`speed` only sets the status bit,** as W9 does.
- **The other jumpers have no property.** The model is MFM with the crystal write clock, and W10 is out.

## Verification

`./build/altair_tests micropolis` checks the decode, each status bit, select, step, write and read back, write protect, the timer, the interrupt, PRDY, snapshot and each property, with real bus cycles on made-up sectors. The real MBOOT ROM also boots a made-up sector on an 8080. A mutation check broke the sector timing, TF, the sync time, the byte time, the sector number, SLTD/, the timer, the step, the stored bytes, the end of a write and the checksum. The suite failed on each one.

Vector's 56K CP/M 2.20 A was checked once by hand, on a `zcb` at 4 MHz with `fwii` and 56K of RAM. It boots to `A>` with `B` on Monitor 4.0, and with `GF800` on Monitor 4.3. `DIR`, `STAT`, `SAVE`, `PIP` and `ERA` work, and the disk image changed. The tests do not hold a Vector disk.

All 20 disks of `56K Bootable Disk Images.zip` boot, each checked once (reference section 9). Six use serial port A of a Bitstreamer II at port 02, and these were run with a `bitstreamer` beside the `zcb`. Both boards then drive ports 05 and 07, and the bus reports `CONTENTION`.

## References

- `docs/boards/vector-zcb.md`, the processor board, and its Monitor.
- `docs/boards/vector-dualmode.md`, the other Vector floppy controller, with the same 275-byte slot.
- `docs/boards/vector-fwii.md`, the screen and the keyboard.
