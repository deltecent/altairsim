# CompuPro Disk 1 floppy disk controller

**Status:** built (`disk1`). The board boots CompuPro CP/M 2.2N from an 8″ disk through routine
4 of `builtin:DISK1`, a dump of a factory EPROM. `tests/test_disk1.cpp` tests the board, and
`acceptance-disk1` boots the tracked disk through the test machine `tests/media/compupro/disk1.toml`. The serial line on port 3
is built and tested with a guest program in the unit test. No period program that uses the line
has run on it (see *Limitations*).

## The real hardware

The **CompuPro Disk 1** (board 171F, 1981) is CompuPro's first floppy controller. The Disk 1A
(1984) replaced it. It has four parts:

- an **NEC µPD765A** floppy disk controller (the Intel 8272 is the same part). It runs 8″
  drives, in single and double density. A changed board runs 5.25″ drives instead (§5).
- a **DMA engine** with a 24-bit address counter, the same design as on the Disk 1A. There is
  no byte count and no terminal count.
- a **boot EPROM**, a 2716 of 2 KB. Switches and a jumper pick one 256-byte routine of the
  eight. The routine answers memory reads at `0000`–`00FF` over `PHANTOM*`.
- a **bit-banged serial line** on port 3, D7. There is no UART. The guest makes each bit with
  an `OUT` and times it with a delay loop. The manual says that the line is for the first start
  of a new machine only.

The board decodes four ports at a base that is a multiple of four. The CompuPro base is `C0`.
The console is usually on a separate board, an Interfacer or a System Support 1.

## Sources

| Source | Path | Authority |
|---|---|---|
| CompuPro *Disk 1 Technical Manual* (1981, fourth printing 1983) | `reference/CompuPro Disk 1 & 1A Floppy Controllers.md` | The ports, the status bit, the serial line and its polarity, the switches and jumpers, and the routine table. The section numbers in this file are the ones in that file. |
| CompuPro *Disk 1 Technical Manual* (1982 printing) | the same file, cited "1982 manual" | The polarity of the motor bit, and the 5.25″ conversion. |
| A factory Disk 1 EPROM, `CompuPro_Disk1_80_86.BIN` | `roms/DISK1/README.md` | `builtin:DISK1`. |
| CompuPro *CP/M 2.2 Technical Manual* (1983 and 1984) | `reference/CompuPro CPM-22 Technical Manual.md` | The floppy formats, the system-disk layout and the CompuPro port map. |
| NEC µPD765A data sheet (1982) | `reference/NEC uPD765A FDC.md` | The chip. |
| `COMPUPRO.LIB`, `ACTIVE.LIB` (CP/M 2.2N, 1983) | not tracked | The console ports of the Disk 1 machine file in the *User Manual*. |

`docs/sources.md` has the provenance of each one.

## Register reference

Ports at the base `B` (default `C0`):

| Port | OUT (write) | IN (read) |
|---|---|---|
| `B+0` | nothing (the board does not decode it) | the 765 main status register |
| `B+1` | the 765 data register | the 765 data register |
| `B+2` | the DMA address: three bytes, the high byte first | board status |
| `B+3` | D7: the serial output, or the motor (`port3`). D0 = 0 switches the boot EPROM off | D7: the serial input |

**Board status** (`B+2` IN): bit 7 is the 765's INT. The manual calls bits 0–6 "not
significant". They read 0 here.

**Port 3, D7.** The level is inverted between D7 and the line (§2.2):

| D7 | Line | Bit |
|---|---|---|
| 1 | SPACING | 0, and the start bit |
| 0 | MARKING | 1, and the idle line |

A reset clears the output latch to MARKING. With `port3 = motor` (switch S1-3 ON), the latch
drives the MOTOR ON line of the minifloppy cable. D7 = 0 turns the motors on, and D7 = 1 turns
them off (1982 manual). With `port3 = serial`, the minifloppy motors are on all the time.

## How it is simulated

- **The shared base.** The Disk 1 and the Disk 1A are one class, `CompuProDiskBoard`
  (`src/boards/compupro-disk.h`). The class holds the `Upd765`, the DMA engine, the boot EPROM,
  the four drives and the size probe. `CompuProDisk1Board` adds port 3, the status bit and the
  straps. `docs/boards/compupro-disk1a.md` describes the shared parts.
- **The drives** are four `DiskImageDrive`s over raw `.DSK` images, units `drive0`–`drive3`.
- **DMA.** The board takes the bus for each byte, as the Disk 1A does.
- **The boot EPROM** answers memory reads at `0000`–`00FF` and asserts `PHANTOM*`. Memory
  writes go to the RAM below it, so the RAM of the machine must be set
  `honors_phantom = "read"`.
- **The serial line** is a `BitSerialLine` (`src/chips/bitserial.h`), on the unit `serial`.
  The far end is a UART at `baud`, 8 data bits, no parity, 1 stop bit. It samples the middle of
  each bit that the guest writes, and sends each good byte to the endpoint. A byte from the
  endpoint starts its frame when the guest next reads an idle line. The bit time is counted in
  T-states of the processor clock, so the guest's delay loop and the line agree at
  `clock_hz = 0` too.
- **8″ drives only.** The 765 runs at 8 MHz. The size probe takes 8″ images only and refuses
  an image the size of a 5.25″ disk. The board has no software rate select.
- **The TS line** is the sides of the diskette: from the size of the image, or from `media` for
  a blank. A blank needs `media` (`8ss` or `8ds`), and without it the mount is refused (issue
  #777). The Disk 1 has no Force Two Sided bit.
- **`media` and `create`** on `[[board.drive]]` work as on the Disk 1A
  (`docs/boards/compupro-disk1a.md`, *The `media` of a drive*), with the 8″ values only. CompuPro
  `FORMAT.COM` 3.1c on the 2.2N disk formats a blank drive B only with `media = "8ds"`. The BIOS
  reads no ID from a blank disk, so it keeps the drive's two-sided type and formats head 1. With
  TS low, the 765 reports Not Ready, and FORMAT stops with `Drive Format Error`.
- **The interrupt.** The 765's INT goes to the line that `interrupt` names (jumpers J0–J8). The
  default is `none`. The manual does not give a shipped jumper, and the EPROM and the 2.2N
  BIOS poll status bit 7.

### Properties

| Property | Default | What it sets |
|---|---|---|
| `port` | `C0` | The base (switches S2-3 to S2-8). A multiple of 4 |
| `rom` | `builtin:DISK1` | The boot EPROM, or a 2716 image file |
| `routine` | `4` | The 256-byte routine at `0000` (switches S2-1 and S2-2, jumper J17), 0–7. In `builtin:DISK1`, 4–7 are the 8080 routines |
| `boot` | `on` | The boot EPROM (switch S1-4 OFF). Off: a plain disk controller |
| `port3` | `serial` | Switch S1-3. `serial`: D7 is the serial line. `motor`: D7 is the MOTOR ON line, D7 = 0 on |
| `interrupt` | `none` | Jumpers J0–J7 (`vi0`–`vi7`) and J8 (`int`) |

The `serial` unit has `baud` (default 9600) and `connect`.

The `[[board.drive]]` table takes `unit`, `mount`, `readonly` (also `writeprotect`), `media`
and `create`. `media` takes `8ss`, `8ds` and the whole 8″ formats, such as `8ds-dd1024`.

### The routines of `builtin:DISK1`

Routines 4–7 are one 8080 routine. Each passes a different console code in register C, and the
loaded system picks its console from it. CP/M 2.2N, as on the tracked disk, does this:

| Routine | C | Console of CP/M 2.2N |
|---|---|---|
| 4 | 0 | Interfacer at `00`/`01` (the console of the Disk 1 machine file) |
| 5 | 1 | An Interfacer at `10`/`11`. The machine has none, so nothing prints |
| 6 | 2 | System Support 1 serial channel. Connect `ss1:serial` to see it |
| 7 | 3 | Interfacer 3 or 4 at `10`. The machine has none, so nothing prints |

Routines 0–3 are 8086 code, and altairsim has no 8086.

### Connect the serial line

The line is a serial unit, as on any serial board:

```
altairsim> CONNECT fd0:serial telnet:2323
altairsim> SET fd0:serial baud=1200
```

Set `baud` to the rate that the guest's delay loop makes. The manual gives the highest rates
for the processor clock: 600 at 2 MHz, 1200 at 4 MHz and 2400 at 6 MHz.

### Reset

- `Reset::PowerOn` and `Reset::Bus` both switch the boot EPROM on again, reset the 765, and
  clear the port 3 latch to MARKING. The disks stay mounted, and the endpoint stays connected.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| D7 = 1 is SPACING, and a reset leaves the line at MARKING | A guest that bit-bangs the line sends inverted bytes, and the far end sees a break after each reset |
| The EPROM shadows reads only. Writes under it reach the RAM | Code that the guest writes under the EPROM is lost |
| No terminal count. A DMA read runs to the EOT byte of the command, and the 765 ends it with ST0 = `40` and ST1 = EN | The EPROM takes ST0 = `40` with ST1 = `80` as a good read, and starts again on other result bytes |
| The DMA address is a three-deep stack, written high byte first | The boot loads at the wrong address |
| Port 0 OUT is not decoded | A Disk 1A program that writes the Drive Select Register sees no change |
| Status bits 0–6 read 0 | A Disk 1A program that waits for READY in bit 0 runs here and fails on the real board |

## Limitations and deliberate departures

- **The serial line is held at SPACING while CP/M 2.2N runs.** The 2.2N BIOS writes `80h` to
  port `C3`, which sets D7 = 1, and does not write it again. D7 = 1 is SPACING, so the far end
  sees a break. With `port3 = serial`, the board logs one line on each boot: `fd0: serial: a
  break on the line (no stop bit); nothing sent`. The real board holds its line at SPACING in
  the same way.
- **No period program has used the serial line here.** The unit test runs a small 8080 program
  that sends `HELLO` at 1200 baud and echoes three bytes. Routine 5 passes C = 1, but the 2.2N
  BIOS has no driver for the line.
- **No 5.25″ drives.** A Disk 1 runs 5.25″ drives only after a hardware change: other parts,
  three cut traces and new jumpers (1982 manual). The change is not modeled.
- **The motor (`port3 = motor`) has no effect that a guest can read.** The Disk 1 has no ready
  or index bit. The latch is stored and shown, and nothing else.
- **No wait states and no DMA priority** (switches S1-1 and S1-5 to S1-8). The machine has one
  bus master and no slow memory.
- **A16–A23.** The counter counts all 24 bits, but this is a 64 K bus (DESIGN.md §4.0).
- **No M-Drive/H.** The CP/M 2.2N BIOS looks for an M-Drive/H RAM disk, finds 0 boards, and
  continues. The cold-boot program `MFORM` prints `DRIVE SPECIFIED IS NOT A MEMORY DRIVE`.

## Verification

- `tests/test_disk1.cpp` (suite `disk1`) uses real bus cycles. It tests these things:
  - the port map at a moved base, port 0 OUT not decoded, and status bit 7 only
  - the 256-byte EPROM window, `routine` paging, and D0 = 0 and a reset
  - a DMA read of track 0 to `0100`, the 765 at 8 MHz, and the refusal of a 5.25″ image
  - `media`: TS on a blank with `8ds` and `8SS`, the refusal of a blank with no media, of 5.25″ media and of a
    formatted image of another shape, `CONFIG SAVE` of the media, and `create`
  - `port3 = motor`, the snapshot round trip and the property checks
  - an 8080 guest that bit-bangs `HELLO` at 1200 baud and echoes bytes from a stream
  - `builtin:DISK1`: the default routine 4, routines 5–7 differ only in the console code, and
    each of routines 4–7 boots an 8″ disk with C = 0–3, the EPROM off and the line at MARKING
- `tests/test_bitserial.cpp` (suite `bitserial`) tests the line: one byte, a burst, a break and
  a framing error, at two clock rates and at `clock_hz = 0`.
- `acceptance-disk1` boots `tests/media/compupro/CPM22N.DSK` through routine 4 to the
  `CompuPro Systems 64K CP/M 2.2N` sign-on. It runs `DIR` and `STAT DSK:`. It copies
  `STAT.COM` with `PIP`, runs the copy, and erases it. It then boots the disk through routine 6
  (`tests/media/compupro/cpm22n-ss1.toml`), with the System Support 1 serial channel as the
  console, and runs `DIR`. Last, `tests/media/compupro/cpm22n-format.toml` mounts a blank `8ds`
  disk in drive 1, and `FORMAT.COM` 3.1c formats it as 8 × 1024 with two sides. The image grows
  to 1,256,704 bytes, and `STAT B:DSK:` reports a 1200K drive.

## References

- `reference/CompuPro Disk 1 & 1A Floppy Controllers.md`
- `reference/CompuPro CPM-22 Technical Manual.md`
- `reference/NEC uPD765A FDC.md`
- `roms/DISK1/README.md`
- `docs/boards/compupro-disk1a.md`
- `docs/devguide/soft-sector-floppy.md`
