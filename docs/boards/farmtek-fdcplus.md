# FarmTek FDC+ — serial drive and 1.5 MB floppy (`fdcplus`)

**Status:** partial. Drive types **5** (the 1.5 MB floppy), **6** (serial drive as an Altair
Minidisk) and **7** (serial drive as an Altair 8" drive, including the 8 MB drive) are done. The
other drive types, the on-board RAM and PROM, and the sector interrupt are not emulated — see
Limitations. Type 5 is a different machine from the serial drive, and has
[its own section](#drive-type-5-the-15-mb-floppy) below.

## The real hardware

The FDC+ (FarmTek, PC038, Mike Douglas) is a modern one-board replacement for the two-board MITS
88-DCDD and 88-MDS floppy controllers. A PIC24 microcontroller answers the 8080's port accesses,
and a Drive Type switch bank (S3, **latched at power-on**) picks what it drives: real 8" or
Minidisk drives, 5.25" HD drives, the iCOM FD3712 format — or **no drive at all**.

In drive types 6 and 7 the card has no rotating media. Its high-speed serial port (J2) talks to
a **drive server** on a PC, which holds the disk images. The card keeps one track in RAM and
fetches it from the server, and writes it back, a whole track at a time. To the 8080 it is an
88-DCDD (type 7) or an 88-MDS (type 6); period software runs unchanged.

Why a person wants this in a simulator (issue #560): the images can live on another computer,
the simulator and a real FDC+ Altair can use the same images through the same server, and a new
server implementation can be tested without a real FDC+.

## Sources

| Source | Path | Authority |
|---|---|---|
| FDC+ firmware v1.8, `serialDrive.s` | `reference/FDC+ Serial Drive Firmware.md` | **The oracle.** The firmware is the card in types 6 and 7: every register behavior, timer and link rule. |
| FDC+ Serial Drive Protocol v1.0 | `reference/FDC_Serial_Drive_Protocol.md` | The wire format. |
| FDC+ firmware v1.8, `hdfloppy.s`, and the 1.5 MB CP/M (`BIOS.ASM`, `HDFBL.ASM`) | `reference/FDC+ HD Floppy Firmware.md` | **The oracle for type 5.** |
| FDC+ Manual v2.0 | `reference/FDC+ Manual.md` | The port map, the drive types, the address jumpers, the 8 MB drive (§3.7.4). |

The firmware and the protocol text disagree on one point: the firmware times a receive from its
start, the protocol says "one second after the last byte". The board follows the protocol — see
Quirks.

## Register reference

Four ports at `08`–`0B`, or `80`–`83` with the address jumpers moved. Status bits are asserted
**low**.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| base+0 | drive select: bit 7 = deselect, bits 2–0 = drive (8 drives) | status: 0 ENWD, 1 MOVE HEAD, 2 HEAD STATUS, 3 drive ready (the firmware's own), 5 INTE, 6 TRACK 0, 7 NRDA |
| base+1 | command: 0 step in, 1 step out, 2 head load (Minidisk: timer reset), 3 head unload, 4 int enable, 5 int disable, 7 write enable | sector position: bits 5–1 sector, bit 0 sector true (low); `FF` when there is no track |
| base+2 | write data | read data |
| base+3 | ignored | `00` |

## How it is simulated

**Not a `HardSectorFdc`.** The 88-DCDD and 88-MDS share a register model built around a
spinning disk: the sector under the head is a reading off the clock, and a byte comes off the
medium every 32 or 64 µs. The serial drive has no medium, and its firmware behaves differently
where the guest can see it:

- The sector number moves on only when the 8080 **reads** the sector port, a sector time
  (5.208 ms / 12.5 ms) has passed, **and** that sector has arrived from the server. Sector true
  lasts for **one read**.
- NRDA is asserted as soon as the 8080 reads sector true, and a new byte is ready each time it
  takes one. There is no byte clock. ENWD is asserted from WRITE ENABLE to the next sector read.
- A sector-port read on a drive:track that is not in the buffer asks for it, and the port reads
  `FF` until the track starts to arrive. Sectors are readable while the rest of the track is
  still on the wire.

So `FdcPlusBoard` is its own `Board`, modeled on `serialDrive.s` and nothing else.

**The link runs in `pump()`, never in a bus cycle** (DESIGN.md §7.5). A port access only sets
flags — "this track is wanted", "the buffer is dirty". `pump()` runs a small state machine,
without waiting: STAT every 0.1 s, READ when a track is wanted (after writing a dirty one back),
WRIT → track → WSTA with three tries. That is the same split the firmware makes between its port
ISR and its idle loop. `rxBytes()` counts the bytes the server sends, so the run loop does not
nap during a transfer.

**Two clocks.** A duration the guest can read back is emulated time; one it cannot is wall
time.

| Timer | Clock | Why |
|---|---|---|
| step settle (10.5 ms, 50 ms, 260 µs) | emulated | the guest polls MOVE HEAD |
| sector time | emulated | the guest sees the sector number move |
| Minidisk 6.4 s turn-off | emulated | the guest sees status go to `FF` |
| 1.3 s / 1.6 s idle write-back | **wall** | only the link sees it |
| STAT interval, 1 s timeout | **wall** | the link |

The idle write-back was on emulated time at first. On a machine running flat out, 1.3 emulated
seconds pass long before a track arrives, and the board threw away every track in flight.

**Properties:** `port` (`08` or `80`), `drivetype` (5, 6 or 7, read at power-on), `baud` (one of
the serial drive's eight rates, default 403200 — see below), `connect` (the drive server;
`CONNECT fdc0:line` sets it). One serial unit, `line`, which takes no `MOUNT`: the server mounts
the images. The four disk units `drive0`–`drive3` are type 5's.

**Debug flags:** `seek` (every step) and `link` (every READ/WRIT sent, every track received,
and a change in the server's mount map).

### Reset

- `Reset::PowerOn` (POWER): the dirty track is written back first, then the drive type is
  latched, every track number goes to 0, no drive is ready until the next STAT answers, and the
  buffer is empty.
- `Reset::Bus` (RESET): **nothing.** The firmware has no bus-RESET handler; the PIC restarts at
  power-on only.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Sector true lasts one read, and the sector moves on only when the data is there | A BIOS reads a sector the server has not sent yet |
| NRDA at once, no byte clock | A BIOS written for the real card's speed is slowed for no reason (harmless, but not the card) |
| Select of a drive the server has not mounted: status `FF` | CP/M's select timeout never fires; a missing disk looks present |
| STAT drops the selected drive: `FF` until the next select | A disk unmounted on the server keeps answering |
| The track is written back before the next one is read, and after 1.3 s (8", head unloaded) / 1.6 s (Minidisk) idle | Writes wait forever for a track change; a disk swapped on the server is never noticed |
| A sector read on a drive that is not ready still requests a track | Nothing breaks either way; it is the firmware, and it is why the server's silence for an empty drive matters |
| Step in has no upper limit; past track 76 the 8" steps in 260 µs | The 8 MB drive (2048 tracks) cannot be reached, or seeks take minutes |
| Minidisk: the head is loaded on select; bit 2 only restarts the timer; 6.4 s idle turns the drive off | The period Minidisk BIOS, which restarts the timer before every access, sees the wrong status |
| Three select bits: `OUT 08, 0A` selects drive 2 | — |
| **The receive timeout runs from the last byte**, not from the start of the transfer | At the slow rates (38,400 baud: 1.14 s a track) every track times out |
| A failed write-back (3 tries) is reported on the host | The firmware says nothing; the guest cannot be told, but the operator can |
| The dirty track is written back on DISCONNECT, POWER and quit | The last writes are lost if you quit inside the 1.3 s idle window. (The real card would lose them if you switched off; an operator closing a window should not.) |

## Limitations and deliberate departures

- **Drive types 0–4 and 8 are not emulated.** Types 0–3 are what `dcdd` and `mds` already do.
- **The on-board RAM (64K) and PROM (8K) are not emulated.** Use a `memory` board with
  `builtin:dbl` or `builtin:cdbl` for the boot PROM.
- **The sector interrupt is not wired.** Interrupt enable/disable are stored, but the card never
  asserts an interrupt, and the INTE status bit (the bus PINTE line) always reads de-asserted.
  Period CP/M does not use the disk interrupt.
- **The machine needs a crystal, not full speed** — the same rule as any guest that times
  something outside the machine. The period BIOS gives up on a sector hunt after a 65,536-pass
  loop (`dNxtSec` in BIOS.ASM): 1.4 s at 2 MHz, 0.28 s at 10 MHz. Flat out, that loop takes a
  few milliseconds, and every track transfer is longer. The crystal can be faster than 2 MHz when
  the line is fast too: the shipped example runs 10 MHz with 230400 baud (a track in 0.19 s).
  The FDC+ note *Operation with a Z80 at 4MHz* confirms a 4 MHz Z80 works with the serial
  drive (`reference/FDC+ Manual.md` §7).
- **`baud` takes the serial drive's rates and no others:** 9600, 19200, 38400, 57600, 76800,
  230400, 403200 (preferred) and 460800. The v1.8 firmware's monitor offered only the last three;
  the slow rates came later, with Serial Drive Server v1.4 (9.6K–76.8K) and v1.41 (57.6K), and
  current FDC+ firmware has the same list. The simulator does not clock the line, so the rate
  only has to match the server. A slow rate makes a slow disk: at 38,400 baud a track takes
  1.14 s, close to the BIOS's 1.4 s limit for one sector hunt.

## Verification

- `tests/test_fdcplus.cpp` drives real bus cycles against an in-test drive server: the port
  map, the latched drive type, STAT contents and timing, ready and dropped drives, the track
  request, the one-read sector true, NRDA without a byte clock, partial tracks, timeouts and bad
  checksums, the write-back order and its three tries, both idle write-backs (and that the
  8" one ignores emulated time), the Minidisk turn-off, the step timers and the fast step, drain
  on POWER and DISCONNECT, and a snapshot round trip. The tests were checked by breaking the code
  they cover.
- **Real hardware** (2026-09-24): an ESP32 FDC+ Serial Drive Server at 38,400 baud, with
  `cpm22b23-56k.dsk`, `games.dsk` and `zork1.dsk` in drives 0–2. `default` machine, `dsk0`
  replaced by `fdcplus`, `clock_hz = 2000000`, DBL at `FF00`: CP/M 2.2b booted to `A>`, and
  `DIR B:` and `DIR C:` listed the games and Zork disks. The shipped example
  `examples/cpm/cpm22-fdcplus.toml` is that machine as a file; it boots the same way.

## Drive type 5: the 1.5 MB floppy

With the switches at 5, the card runs a 96 tpi high-density drive (a Teac 55-GFR, or an 8" DSDD
drive) in Mike Douglas's own format: **one 10,240-byte sector per track**, both sides, MFM, 149
tracks, **1,525,760 bytes**. Nothing about it is an Altair disk, and nothing on this board's
serial side runs: the `line` stays connected and quiet. The disks are images in `drive0`–`drive3`,
put there with `MOUNT` or `[[board.drive]]`.

### Registers (type 5)

| Addr | OUT (write) | IN (read) |
|---|---|---|
| base+0 | drive select: bit 7, or a drive of 4 or more, deselects; bits 3–0 = drive | status (low true): 0 ENWD, 1 MOVE HEAD, 2 HEAD STATUS, 3 drive ready, **4 WRITE PROTECT**, 6 TRACK 0, 7 NRDA |
| base+1 | command: 0 step in, 1 step out, 2 head load, 3 head unload, **4 read enable**, 6 low write current, 7 write enable | sector position: sector 0 or 15, bit 0 sector true (low) |
| base+2 | write data, into the track buffer | read data, from the track buffer |
| base+3 | **the track number** — also picks the side | **I/O status** (high true): 1 track error, 2 end of sector too soon, 7 done |

### How it is simulated (type 5)

**Its own engine, `FdcPlusHdf`** (`src/boards/farmtek-fdcplus-hdf.{h,cpp}`), which the board
owns and hands the bus to when it was powered up at type 5. It is `hdfloppy.s` and nothing else.

**Everything is emulated time, worked out when the 8080 looks.** The disk turns whether anyone
is reading it, as a `Spindle` does: the index holes are every 166.67 ms from the moment the motor
came on. Before every port access, the engine runs the card forward to now — the step and settle
timers, the end of sector true, the read-clear window, the end of a transfer, each index hole —
and then copies in the disk bytes that have arrived (a read) or copies out the buffer bytes the
disk has taken (a write). A long quiet stretch with the head loaded is done in one step.

**The transfer has no handshake, so the byte timing is the whole behavior.** A byte arrives every
16 µs after the sync byte, and the data port gives the next byte in the buffer whether it has
arrived or not. So a guest that reads faster than the disk gets **old bytes**, and a guest that
writes slower than the disk has old bytes written in its place — which is what the real card does,
and why the BIOS needs a 2 MHz 8080 (below).

**Writes reach the image when the track is done**, and are synced. A write-protected disk is not
written: the drive blocks the write gate, and the card still says done. A track past the end of a
shorter file makes the file bigger. `MOUNT` takes a file of whole 10,240-byte tracks, 149 at most;
an empty file is a blank disk.

**The fake boot sector.** At every index hole where no read has been asked for, the firmware
points the data port at a 137-byte Altair sector in its flash (`HDFBL.ASM`) and asserts NRDA. The
stock DBL reads it as sector 0 of track 0, loads its 128 bytes to 0000 and jumps there; that
loader reads the real track 0 to 4000h and jumps to it. So a disk Altair boots a 1.5 MB disk with
the PROM it already has. The bytes are copied from the firmware, and a test checks them against
DBL's checksum.

### Quirks reproduced (type 5)

| Quirk | If you get it wrong |
|---|---|
| A READ ENABLE sent before the index hole is cleared by it; the read starts at 400 µs only if the flag was set after the hole | A BIOS that asks too early gets a track, where the card gives it nothing |
| The data port has no handshake: a byte every 16 µs, and a faster reader gets old buffer bytes | A 4 MHz machine reads the disk, where the real card gives garbage |
| A write takes each byte as the buffer holds it when the disk reaches it | A slow writer's track is written whole |
| The sector register shows 0 and 15 in turn, only with the head loaded, sector true for 32 µs | DBL (sector 0) or CDBL (0 and 15) never finds a sector |
| A select starts the motor and throws the first hole away; the motor stops after 32 turns with the head unloaded, and a head load starts it | A select shows a sector at once; a disk left alone keeps spinning |
| A step ignores a head load in the same command; MOVE after 3 ms, HEAD after 18 ms | Seeks settle at the wrong time |
| The side comes from the track number (`OUT base+3`), the cylinder from the steps | Odd tracks read from the bottom side |
| A track the disk does not have gives end of sector at the next index, and NRDA | The 8080 waits for NRDA forever |
| The drive's write-protect line is status bit 4 | The BIOS, which reads it before every write, writes to a protected disk (and the drive throws the bytes away) |

### Limitations (type 5)

- **No checksum errors.** The sync byte and the checksum are on the disk, not in an image: every
  read of an image track is good, and its sync byte is the track's own number, so the track error
  bit means "the head or side is not on the track the 8080 named".
- **The real drive's MFM, precompensation and write current** change only the flux, which an
  image does not have.
- **A drive with no disk has no index holes**, so the sector register stays `FF` and the BIOS's
  1.4 s timeout reports it. A real drive's spin-up time is not in the firmware and is not
  modeled: the first hole comes a turn after the motor starts.
- **Step in stops at cylinder 79**, the Teac 55-GFR's last. The firmware has no limit; the drive
  has an end stop.
- **The machine needs 2 MHz of emulated time**, which flat out gives: the card times itself by
  the same clock as the guest. The BIOS's read loop takes 34 T-states a byte and must be slower
  than the disk's 16 µs; its write loop takes 28 and must be faster. So the 8080 must run between
  1.75 and 2.1 MHz. At `clock_hz = 4000000` it cannot read the disk, as on a real 4 MHz Altair; the FDC+ note
  *Operation with a Z80 at 4MHz* says the same (`reference/FDC+ Manual.md` §7).

### Verification (type 5)

- `tests/test_fdcplus.cpp` (the type 5 sections): the select and status bits, write protect,
  sector 0/15 and its 32 µs, the skipped first hole, the boot sector byte for byte with DBL's
  checksum, a track read at 17 µs a byte, **a read at 8 µs a byte that gets old bytes**, the read
  enable lost to the hole, the side from the track number and the track error, end of sector on a
  missing track, a write at 14 µs that lands, **a write at 18 µs that falls behind**, write
  protect, the step and settle times, the motor timer, the size check, `[[board.drive]]` and a
  snapshot. The timing tests were checked by breaking the byte timing.
- `acceptance-examples` boots `examples/cpm/cpm22-fdcplus-hdf.toml` from a copy of the folder:
  DBL, the fake boot sector, HDFBL and the BIOS, to `48K CP/M 2.2b v1.2`, `For Altair 1.5Mb
  Floppy`, `A>` and a directory line.
- By hand, over `--mcp`, on a copy of `CPM22-48K-HDF.dsk`: `DIR`, `STAT` (`920k` free), `SAVE 4
  X.COM`, a warm boot, `DIR X.COM`, `ERA X.COM`. At `clock_hz = 2000000` it boots in 2.3 s; at
  `clock_hz = 4000000` it does not boot.

## References

- `reference/FDC+ Serial Drive Firmware.md`
- `reference/FDC_Serial_Drive_Protocol.md`
- `reference/FDC+ HD Floppy Firmware.md`
- `reference/FDC+ Manual.md`
- `docs/boards/mits-dcdd.md`, `docs/boards/mits-88mds.md` — the controllers it replaces
