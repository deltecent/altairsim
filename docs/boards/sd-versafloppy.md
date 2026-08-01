# SD Systems VersaFloppy I & II

**Status:** done (VF-II boots SDOS; VF-I selectable via `variant`; formats all ten SD Systems
formats via Write Track)

## The real hardware

The **VersaFloppy** was SD Systems' (S.D. Sales / S.D. Computer Products, Dallas/Garland TX)
S-100 soft-sector floppy-disk controller. Two generations, one card family:

- **VersaFloppy I** — a Western Digital **FD1771B-1**, single density (FM, IBM 3740).
- **VersaFloppy II** — a Western Digital **FD1791B-1**, single *and* double density (FM/MFM),
  including 256-byte double-density formats.

They share the port block, the board-level control/status latch, and the driver family (the
DDBIOS and the SD/MS monitors). They differ only in the FDC part and a few control-register
bits. Neither carries a boot PROM — the bootstrap BIOS (**DDBIOS**) lives on a separate PROM,
conventionally at `F000H`, which on the SBC-100/200 is the onboard socket. With the SBC-200 +
DDBIOS the monitor's `C`/`R`/`W`/`Z` commands boot and access **SDOS** (a CP/M work-alike).

## Sources

| Source | Path | Authority |
|---|---|---|
| SD Systems VersaFloppy I & II manuals | `reference/SD Systems VersaFloppy.md` | Port map, control-register bit layout, geometry, wait-state transfer. |
| DDBIOS driver `DDB200.ASM` (Rex Brown, v3.3) | `roms/DDB200/` | **The authority for what software actually does** — the runtime command bytes, the density tables, and the two facts the manuals omit (below). |
| WD FD1771 / FD179x data sheets | `reference/Western Digital FD1771 - Datasheet.md` | The chip: register file, command set, step rates, status. Modeled in `src/chips/wd17xx.h`. |

**Where a source disagreed with the manual, the driver won.** Two facts are in `DDB200.ASM`
and *not* in the VersaFloppy manuals, and both are load-bearing:

1. **The 63H control latch is negative-true** — `DRVSET` does `CPL ;HRDWRE REG IS INVERTED`
   before every `OUT (SELECT)`, and `SWEB`/`DWAIT` toggle the wait bit with neg-true logic.
2. **The data transfer is PRDY-wait-synchronized**, not DRQ-polled — the sector loop is a bare
   `INIR`/`OTIR` on port 67H with no software polling.

## Register reference

Eight ports (base 60H standard; the board decodes A0–A7 only). 61H/62H are unused.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| 60H | controller reset strobe (VF-II; no-op on VF-I) | — |
| 63H | board control latch — **negative-true** | status readback (VF-II: the latch; VF-I: latch low bits + INTRQ at D7) |
| 64H | FD177x Command | FD177x Status |
| 65H | FD177x Track | FD177x Track |
| 66H | FD177x Sector | FD177x Sector |
| 67H | FD177x Data (wait-synced) | FD177x Data (wait-synced) |

**63H control bits** (shown true-sense; the guest writes the complement):

| Bit | VF-I | VF-II |
|---|---|---|
| D0–D3 | drive select (one-hot) | drive select (one-hot) |
| D4 | side | side |
| D5 | restore | 5″/8″ select |
| D6 | wait-enable | density (double/single) |
| D7 | int-enable | wait-enable |

## How it is simulated

`VersaFloppyBoard` (`src/boards/sd-versafloppy.{h,cpp}`) owns one **`Wd17xx`** chip — a `Wd1791`
for `variant=vfii` (the default), a `Wd1771` for `vfi` — and up to four **`DiskImageDrive`**
adapters (`src/boards/floppy-drive.{h,cpp}`), each a `FloppyDrive` over a mounted `DiskImage`.

- **Decode:** IoRead/IoWrite of `port`..`port+7`. 64–67H go straight to the chip's register
  file; 63H is the board's own latch; 60H resets the chip (VF-II).
- **Media:** soft-sector, so `sectorSize` from the format, `startSector = 1`. All **ten**
  SD Systems formats — the `Z`-command codes 0–7, C, D (`reference/SD Systems Monitor.md` §3.4),
  five physical geometries × single/double sided (see the table below). An image's size selects
  its format on mount, or `media=NAME` forces one.
- **Interrupts:** the chip's INTRQ, gated by the `interrupt` jumper (and VF-I's D7). The
  standard software polls, so this is rarely used.
- **DMA:** none — an S-100 slave.
- **Properties:** `variant` (`vfi`/`vfii`), `port` (60H), `drives`, `interrupt`; `[[board.drive]]`
  for `unit`/`mount`/`writeprotect`/`media`.

### Media formats

Sectors number from 1; data fill `0xE5`. `media` names are the `Z`-command format codes.

| Code | `media` | Density | Sec/trk | B/sec | Tracks | Sides | Image bytes |
|---|---|---|---|---|---|---|---|
| 0 | `8sd`       | FM  | 26 | 128 | 77 | 1 | 256,256 |
| 1 | `8sd-ds`    | FM  | 26 | 128 | 77 | 2 | 512,512 ⚠ |
| 2 | `5sd`       | FM  | 18 | 128 | 35 | 1 | 80,640 |
| 3 | `5sd-ds`    | FM  | 18 | 128 | 35 | 2 | 161,280 |
| 4 | `8dd`       | MFM | 50 | 128 | 77 | 1 | 492,800 |
| 5 | `8dd-ds`    | MFM | 50 | 128 | 77 | 2 | 985,600 |
| 6 | `5dd`       | MFM | 29 | 128 | 35 | 1 | 129,920 |
| 7 | `5dd-ds`    | MFM | 29 | 128 | 35 | 2 | 259,840 |
| C | `8dd256`    | MFM | 26 | 256 | 77 | 1 | 512,512 ⚠ |
| D | `8dd256-ds` | MFM | 26 | 256 | 77 | 2 | 1,025,024 |

⚠ **`8sd-ds` (f1) and `8dd256` (fC) are both 512,512 bytes** — the only size collision. An
unforced probe of 512,512 picks **`8dd256`** (the SDOS master); `media=8sd-ds` forces the FM
double-sided reading. The VF-I (`vfi`, FD1771) supports the four FM codes (0–3); the VF-II
(`vfii`, FD1791) all ten.

### Formatting a blank / reformatting

The `Z` command formats a diskette by streaming a whole track image through the FD177x **Write
Track** command; the emulated controller lays each track's geometry down as it arrives
(`DiskImage::setTrackFormat`), and the recorded density comes from the chip's data rate at that
moment (the D6 density bit). A **blank** disk — `MOUNT drive0 blank.dsk CREATE media=NAME` — mounts
unformatted (every read is Record Not Found) and **grows** into a valid image as the guest formats
it, one track at a time, capped at the named format's size. A full disk **reformats** in place at
the same size. Double-sided formats record both sides of a cylinder before stepping (the DDBIOS
`FMAT`), which is ascending image-slot order under the controller's cylinder-major layout — so a
double-sided blank grows contiguously (the ascending-slot-order requirement of `host/disk.h`).

The per-track byte budget is one disk revolution at the chip's data rate: **`rate / (8 × rev/s)`**,
with 8″ drives at 360 RPM (6 rev/s) and 5.25″ minis at 300 RPM (5 rev/s) — so 5208/10416 bytes for
8″ SD/DD and 6250/12500 for 5.25″ SD/DD. The drive's RPM is set per mount from the diskette's size
(`DiskImageDrive::setRevsPerSecond`); it is a physical property of the drive, not a control bit.

### Reset

- `Reset::PowerOn`: the chip is powered to a known-good idle (`Wd17xx::powerOn`).
- `Reset::Bus`: nothing beyond re-driving the interrupt wire — the guest/DDBIOS re-initializes
  the controller (60H reset, or a fresh command). Mounted media stay mounted.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **63H is negative-true** — the guest writes `~control` | Every drive-select picks the wrong drive → the disk reads NOT READY → **the boot hangs**. This is the bug that cost the first boot attempt. |
| **PRDY wait-synchronization** — the data port and command completion stall the CPU; there is no DRQ polling | The `INIR`/`OTIR` transfer loop reads a byte before it is ready, or a status read returns BUSY forever. Modeled by `Wd17xx::setWaitSynced(true)`: any register access resolves all pending time-based progress, serving the next byte on demand. |
| **DD-256 uses double-density 256-byte sectors** | The DDBIOS format probe reads the ID field's length code (`IDSV+3`); a wrong sector size fails detection and the boot falls through to the monitor. |
| **FD1771 vs FD1791 differ** (step rates, side byte, record-type bits) | Wrong step-rate table seeks at the wrong speed; wrong record-type width misreports deleted sectors. Split into `Wd1771`/`Wd1791` parts, not a flag. |

## Limitations and deliberate departures

- **Write Track parses the streamed track, it does not model a bit-level image.** The controller
  reconstructs each track's geometry from the address marks and length code the guest streams
  (density from the chip's data rate), then stores sector payloads in the raw `.DSK` — the gaps,
  sync fields, and CRCs are consumed and discarded, exactly as they never appear in the image on
  read. This is enough to format every SD Systems geometry and read it back; it is not a
  flux-level model, and `Read Track` returns nothing.
- **`Read Address`/`Read Track` are not the format path.** Formatting is `Write Track`;
  `Read Track` has no bit image to return.
- **The collapsed command time is invisible** under wait-synchronization — a seek does not spend
  its 20 ms/track in emulated time, because a PRDY-stalled CPU cannot observe it. Correct for
  this card; a DRQ-polling card (a future Tarbell) would use the chip's byte-timed path instead.

## Verification

- **`acceptance-sdos`** boots SDOS end-to-end: the SBC-200 + DDBIOS cold-boots the tracked 8″
  DD-256 master to its `[A]` prompt, on a pty (the SBC auto-bauds and the boot is a typed `C`).
  The disk is mounted write-protected and shasummed — the boot only reads.
- **`test_versafloppy`** pins the wait-synced read and write (a whole sector on `IN`/`OUT (67H)`),
  the negative-true select, a seek landing on the right sector, and write-protect refusal — and
  **formats a blank in each of the ten formats** through the real ports, confirming the file grows
  to the exact image size and reads back `0xE5` (including the last sector of the last track and,
  for double-sided disks, a head-1 sector), a reformat-in-place, and the 512,512 collision default.
- **`test_wd17xx`** covers both parts — the FD1771 in full, and the FD1791's step rate, side
  byte, and one-bit record type.

## References

- `reference/SD Systems VersaFloppy.md`, `reference/SD Systems SDOS.md`,
  `reference/SD Systems Monitor.md`, `roms/DDB200/DDB200.ASM`.
- `docs/boards/tarbell-sd.md` — the other planned WD-chip soft-sector controller; it will reuse
  `Wd17xx` and `DiskImageDrive`.
