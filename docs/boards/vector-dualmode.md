# Vector Graphic Dual-Mode Disk Controller

**Status:** done for floppy disks. The hard disk half is not built (issue #872).

## The real hardware

The **Dual-Mode Disk Controller** is a Vector Graphic S-100 board for one hard disk and floppy drives. This model is Rev 2.0, February 1981. A jumper removes the hard disk, and the board then runs four floppy drives. `vgdmdc` is the board in that setup.

The board has no command set. It has four ports, a 512-byte RAM and a 32-bit ECC circuit. It does not seek and it does not look for a sector header. The guest loads every byte of a sector into the RAM, and the guest checks each header byte. The only compare in the hardware is the sector number against the sector counter.

The drives are 5.25-inch, 77-track, 16-hard-sector drives with one or two sides. The drive runs at 300 RPM. A timer of 4 seconds starts on each `OUT` to the first port, and the drive motor runs until the timer ends.

## Sources

| Source | Path | Authority |
|---|---|---|
| Distilled emulation reference | `reference/HD-FD documentation.md` | The ports, the status bits, busy, the ECC and the sector layout |
| *Dual-Mode Disk Controller Board*, Rev 2.0, Rev A, February 1981 | `HD-FD documentation.pdf` | The schematic, for the ECC polynomial and for the gate on the decode |
| System B manual and the Micropolis 1115 manual | `reference/System B Overview.md`, `reference/Floppy Drive (1115).md` | 300 RPM |
| Monitor 4.3 | `roms/VGMON43/VGMON43.BIN` | The check of the read path: it boots from drive 1 |

The ECC polynomial from the schematic matches the stored ECC on 2446 sectors of a period OASIS disk.

## Register reference

The ports are `port` to `port` + 3. The default base is `C0`. Ports `C4` to `CF` are not decoded.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `C0` | Bits 0-1 drive, bit 2 side, bit 5 step on a rising edge, bit 6 direction (1 = in). Starts the motor timer | **Status 0.** Bit 0 write protect, bit 2 track 0. Bits 5 to 7 read 1 |
| `C1` | Bits 0-3 sector, bit 5 read (1) or write (0), bit 6 ECC enable | **Status 1.** Bit 0 floppy selected, bit 1 reads 0, bit 2 motor on. Other bits read 1 |
| `C2` | One byte of RAM. The address counter goes up and wraps at 512 | One byte of RAM. The counter goes up and wraps at 512 |
| `C3` | **START**: read or write one sector | Clears the address counter. Reads `FF`, because nothing drives the bus |

**Busy.** While busy, the board answers no port. Each `IN` reads `FF` and each `OUT` is lost. A driver reads status 1 bit 1 as 1 for this reason alone. The bit is wired to ground.

**The sector.** A slot is 275 bytes: `FF`, track, sector, 10 zeros, 256 data bytes, a checksum, 4 ECC bytes and a valid byte. On a read, the RAM gets the slot from the sync byte on. The rest of the 512 bytes read 0. With ECC enable, bytes 270 to 273 hold the computed ECC XOR the stored ECC, so 00 means that the sector is good.

**Write.** The sync byte is the first byte that is not 0, from the address counter on. With ECC enable, the board puts the ECC in RAM bytes 310 to 313, which is where the circuit counts to.

## How it is simulated

- **Decodes** an `IN` and an `OUT` at the four ports. `decodes()` stays true during busy, and `read()` and `write()` act as an open bus. The bus keeps the result of `decodes()`, and busy ends with no bus cycle.
- **The transfer happens at START, and only the wait is lazy.** The guest cannot see the RAM or the disk until busy ends, so the bytes move at once. `busyEnd_` and `motorEnd_` are absolute T-states. No deadline stays in the Clock queue, so an idle board lets the machine stand down. A write reaches the image even if the guest stops during the wait.
- **Rotation.** Each drive has a `Spindle`, a pure function of the Clock. Busy ends at the end of the wanted sector, one sector time after the board finds its start.
- **Media.** A `DiskImage` with 275-byte sectors, 16 sectors on a track, 77 tracks. A file of 677600 bytes has two sides and a file of 338800 bytes has one. All of side 0 comes first. Any other size mounts as two sides and grows when the guest writes. The file holds each slot in full, with no file header.
- **Interrupts and DMA:** none. The real board has neither.
- **Host I/O.** `DiskImage` writes through. A write to a write-protected drive is dropped with one log line. The real drive gives the guest no error for this (reference Q4).

## Properties and units

| Name | Meaning |
|---|---|
| `port` | Base of the four ports, default `C0`. It must end in 0 |
| `drive0` to `drive3` | Units for `MOUNT`. Monitor 4.3 boots from `drive1` |
| `[[board.drive]]` | In a machine file: `unit`, `mount`, `readonly`, `create` |
| `DEBUG=sector`, `DEBUG=seek` | Trace each transfer, or each head step |

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The board answers no port while busy | A driver that polls status 1 bit 1 for 1 waits forever, or never waits |
| Only `IN C3` clears the address counter | A driver reads the wrong bytes of a sector after a transfer |
| The head starts wherever it stopped, and only the guest moves it | The Monitor must step to track 0 first. It reads the track byte of the header to find where it is |
| Status 1 bit 1 is ground when the board answers | A floating read and a real read give different values, and a driver relies on that |
| A sector with a bad ECC still reads, with a syndrome that is not 0 | 18 of the 2464 sectors of the OASIS disk do not match their stored ECC. A board that gives 00 for each sector hides that |
| ECC is optional on a write | OASIS writes with ECC off, and leaves bytes 270 to 274 at 0. One of the disks is all like this |
| The motor timer ends the select | With no `OUT C0` for 4 seconds, no drive is selected and a START does nothing |

## Limitations and deliberate departures

- **The hard disk is not built** (issue #872). Status 1 bit 0 reads as the floppy-only jumper, so all four drives are floppies.
- **Pre-compensation and low write current are ignored.** A byte stream has no bit timing.
- **A sector with no sync stores nothing.** The RAM keeps what it held. The controller manual says that the preamble is not stored, so this is an inference.
- **The address counter stays where it was after a transfer.** On the real board the transfer moves it. Each driver that was checked clears it first with `IN C3`.
- **The ECC at a write** goes in at the fixed RAM offset, and the board does not model the counter that times it.

## Verification

`./build/altair_tests vgdmdc` checks every port with real bus cycles, on made-up sectors. The suite also boots the real Monitor ROM from a made-up disk, with the head on track 3 at the start. The tests do not hold an OASIS byte.

OASIS 5.5A was checked once by hand. It boots from `drive1`, and a second disk reads from `drive2`. A `RENAME` on a copy of the disk survives a reboot.

## References

- `docs/boards/vector-bitstreamer2.md`, the board that gives OASIS its clock.
- `docs/boards/vector-fwii.md`, the screen and the keyboard.
- `docs/boards/bankmem.md`, the banked RAM that OASIS needs.
