# SD Systems VersaFloppy I & II

**Status:** done (VF-II boots SDOS; VF-I selectable via `variant`)

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
- **Media:** soft-sector, so `sectorSize` from the format, `startSector = 1`. Formats are
  single-sided (their sizes are distinct, so a size probe is unambiguous): 8″ SD 26×77×128,
  8″ DD 50×77×128, **8″ DD-256 26×77×256** (the SDOS master), 5″ SD 18×35×128, 5″ DD 29×35×128.
  Double-sided variants collide by size and are a follow-up; `media` forces a format.
- **Interrupts:** the chip's INTRQ, gated by the `interrupt` jumper (and VF-I's D7). The
  standard software polls, so this is rarely used.
- **DMA:** none — an S-100 slave.
- **Properties:** `variant` (`vfi`/`vfii`), `port` (60H), `drives`, `interrupt`; `[[board.drive]]`
  for `unit`/`mount`/`writeprotect`/`media`.

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

- **Single-sided formats only** for now — the double-sided geometries double the image size and
  collide with single-sided ones (an 8″ SS-DD-256 and an 8″ DS-SD-128 are both 512,512 bytes),
  so they need side-aware probing. The SDOS master is single-sided.
- **No bit-level track image**, so `Read Track`/`Write Track` (formatting) sets WRITE FAULT — a
  raw `.DSK` has no gaps or address marks to write. `Z` (format) therefore cannot create a disk
  from nothing; it is honest about it (the chip says so). This is the same stance as every other
  soft-sector controller here.
- **The collapsed command time is invisible** under wait-synchronization — a seek does not spend
  its 20 ms/track in emulated time, because a PRDY-stalled CPU cannot observe it. Correct for
  this card; a DRQ-polling card (a future Tarbell) would use the chip's byte-timed path instead.

## Verification

- **`acceptance-sdos`** boots SDOS end-to-end: the SBC-200 + DDBIOS cold-boots the tracked 8″
  DD-256 master to its `[A]` prompt, on a pty (the SBC auto-bauds and the boot is a typed `C`).
  The disk is mounted write-protected and shasummed — the boot only reads.
- **`test_versafloppy`** pins the wait-synced read and write (a whole sector on `IN`/`OUT (67H)`),
  the negative-true select, a seek landing on the right sector, and write-protect refusal.
- **`test_wd17xx`** covers both parts — the FD1771 in full, and the FD1791's step rate, side
  byte, and one-bit record type.

## References

- `reference/SD Systems VersaFloppy.md`, `reference/SD Systems SDOS.md`,
  `reference/SD Systems Monitor.md`, `roms/DDB200/DDB200.ASM`.
- `docs/boards/tarbell-sd.md` — the other planned WD-chip soft-sector controller; it will reuse
  `Wd17xx` and `DiskImageDrive`.
