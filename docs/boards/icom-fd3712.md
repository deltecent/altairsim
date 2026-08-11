# iCOM FD3712 / FD3812 — 8″ floppy disk controller

**Status:** done (2026-08-11). Boots CP/M 2.2 single density (FD3712, `CPM22v1.0-3712-48K.DSK`)
and CP/M 2.23 double density (FD3812, `CPM22-3812-48K.dsk`) to `A>`, and both of iCOM's own FDOS
revisions — FDOS-III and the original FDOS-I — to their `!` prompt, each off its own boot PROM.
`altairsim icom`; see `examples/icom/`.

## The real hardware

The **iCOM FD3712** (single density) and **FD3812** (double density) 8″ floppy disk systems,
from **iCOM / Pertec Microsystems** (1977–1978). Unlike the WD177x soft-sector cards (Tarbell,
VersaFloppy), the iCOM is a **programmed-I/O command/handshake controller**: the controller
buffers a whole sector, and the CPU shifts bytes through two I/O ports while a boot PROM in high
memory carries the operating system's disk driver. It is architecturally the 88-HDSK's cousin,
not a floppy-shift-register card — the Altair never sees a bit off the medium.

- **FD3712** — the single-density card: the two-board **CF3700** controller/formatter driving
  Pertec **FD511** drives, IBM 3740 format (77 × 26 × 128).
- **FD3812** — the double-density successor: a single S-100 board driving Pertec **FD514**
  drives. Same command engine plus the Load Configuration command; track 0 stays single density,
  tracks 1–76 are double density (256-byte MFM sectors).

Two things live on the interface board that the machine has nowhere else: the **boot PROM** (1 K,
at `F000` for CP/M or `C000` for FDOS) and a small **scratch RAM** (a 6810, at PROM base + `0x400`)
holding the driver's I/O vectors and file pointers. Because the disks are 48 K builds (main RAM
`0000`–`BFFF`), the PROM and scratch RAM sit above RAM with nothing else there — no `PHANTOM*`
needed.

## Sources

| Source | Path | Authority |
|---|---|---|
| iCOM FD3700 Interfacing Guide (Rev B, 1978) | `reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md` | The shared programmed-I/O model: command word, C0/C1 handshake, status byte, the 14 commands with hex codes, SD geometry. |
| iCOM FD3812 User's Guide (1978) | (same reference, grouped) | Double density: the Load Configuration command, the mixed SD/DD track format, DD geometry. |
| `FD3712-CPM.HEX` boot PROM | `roms/ICOM-FD3712-CPM/` | The CP/M CBIOS's actual C0/C1 sequences (`pREAD`, `pWRITE`, `pSELDSK`) — the read/write byte protocol as software drives it. |
| `FD3712-FDOS.HEX` boot PROM | `roms/ICOM-FD3712-FDOS/` | The FDOS resident driver and mini-monitor; the `cDRVSEC` unit/sector bit packing. |
| iCOM FDOS-II / FDOS-III manuals | `reference/FDOS-II Manual.md`, `reference/FDOS-III Manual.md` | The FDOS memory map, entry points, scratch-RAM layout, disk format, and directives. |

**Where the manual and the PROM disagree, the PROM won.** The FD3812 manual misprints Read as
`0B` and Write as `06`; the FD3700 manual and the bit columns both give `03`/`05`, and the boot
PROMs issue `03`/`05` — so that is what the engine decodes.

## The programming model

Two consecutive ports (default base **`C0h`**):

| Port | OUT (write) | IN (read) |
|---|---|---|
| `C0` | **command word** (`CMDOUT`) — bit 0 is the command strobe; bit 6 selects what the next `IN C0` returns | **data in** (`DATAIN`) — the **read-buffer** byte if the last `OUT C0` had **bit 6 set** (`cRDBUF`/`cSHIFT`), otherwise the **status byte** |
| `C1` | **data out** (`DATAOUT`) — the byte a following command consumes (track / unit-sector / write-buffer / config) | unused (floats `FF`) |

**Commands** (the `cXXX` codes the PROMs issue):

| Code | Name | Effect |
|---|---|---|
| `00` | cSTATUS | no-op (status is read via `IN C0` when bit 6 is clear) |
| `03` | cREAD | read the addressed sector into the read buffer |
| `05` | cWRITE | write the write buffer to the addressed sector, then sync |
| `07` | cRDCRC | read + CRC check (modeled as cREAD) |
| `09` | cSEEK | seek (synchronous — no-op; the image has no head) |
| `0B` | cCLRERR | clear the latched CRC / DDAM error bits |
| `0D` | cRESTOR | restore to track 0 |
| `0F` | cWRITE-DDAM | write with a deleted-data address mark (modeled as cWRITE) |
| `11` | cSETTRK | latch the last `OUT C1` byte as the track register |
| `15` | cLDCFG | latch the density/format config byte (FD3812) |
| `21` | cDRVSEC | latch unit (bits 7:6) and sector (bits 5:0) from the last `OUT C1` byte |
| `30`/`31` | cWRTBUF | arm write-buffer streaming; `31` (FD3712) also pushes the latched byte immediately |
| `40` | cRDBUF | select read-buffer mode (next `IN C0` returns buffer, not status) |
| `41` | cSHIFT | (FD3712) shift to the next read-buffer byte — modeled as a mode-set no-op; see Quirks |
| `81` | cCLEAR | "Clear": abort and clear the CRC / DDAM status latches. It is **not** a controller reset — the read/write buffers keep their pointers and the track/unit/sector registers are untouched |

The **read sequence** (PROM `readXfr`): `OUT C0=40; IN C0` (byte 0); then for the 3712, N×
`OUT C0=41; IN C0`; for the 3812, N× back-to-back `IN C0`. The **write sequence** (PROM
`wrtLoop`): `OUT C1=byte; OUT C0=31; OUT C0=00`, ×128 (3712), or stream `OUT C1` after one
`OUT C0=30` (3812).

### One engine, both densities

The board is **one superset command engine**; density is carried by the mounted disk's geometry,
not a separate board type. `rom=` picks the PROM (`builtin:icom-fd3712-cpm`,
`builtin:icom-fd3712-fdos`, or `builtin:icom-fd3812-cpm`) and the mounted image's size picks the
geometry. The unified read model works for both generations because **`IN C0` in read-buffer mode
always advances the read pointer**: the 3712 does one `IN` per byte (advance) and the 3812 does
back-to-back `IN`s (advance), and the 3712's `cSHIFT` (`41`) commands are absorbed as no-op
mode-sets. `cREAD` refills the buffer and resets the pointer; `cRDBUF`/`cSHIFT` never touch it.

## Disk geometry

Single head, `startSector = 1`, probed from image size:

| Density | Layout | Bytes |
|---|---|---|
| **SD** (FD3712) | 77 × 26 × 128 | **256,256** |
| **DD** (FD3812) | track 0 SD (26 × 128) **+** tracks 1–76 DD (26 × 256) | **509,184** |

The DD image is the mixed-density shape `DiskImage` already supports (an SD track 0 then DD data
tracks) — track 0 is mandatorily single density on real iCOM DD media.

## How it is simulated

- Decodes `IoRead`/`IoWrite` for `port_` and `port_+1`, and `MemRead` in the PROM+scratch-RAM
  window (`MemWrite` lands in the scratch RAM only). The window **base is derived from the decoded
  PROM's address range** — `F000` for the CP/M PROMs, `C000` for the FDOS PROM — never hardcoded.
- No `tick()` work and nothing on the `EventQueue`: the controller is **synchronous**, BUSY reads
  back 0 immediately. The PROMs poll BUSY, so this is faithful to how period software waits.
- Uses `DiskImage::readSector`/`writeSector` directly (whole-sector, like the 88-HDSK) — `n` out
  is the addressed track's real sector size (128 on track 0, 256 on a DD track).
- Does not master the bus; no interrupts.
- `properties()`: `port` (radix 16, default `C0`), `rom` (a `builtin:` / file selector),
  `drives` (radix 10, default 2). Drives are a `[[board.drive]]` sub-unit table (`unit` / `mount`
  / `readonly`), the same shape as the 88-HDSK.

### Reset

- `Reset::PowerOn` (POC*, cold): reload the PROM, clear the scratch RAM, reset the engine.
- `Reset::Bus` (RESET*, warm): reset the engine (buffers, pointers, latched errors). Mounted
  disks and the scratch RAM survive.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| `IN C0` returns the read buffer only when the **last `OUT C0` had bit 6 set**; otherwise the status byte | The CBIOS reads status as data (or data as status) and the boot never completes |
| `IN C0` in read-buffer mode **advances** the pointer; `cRDBUF`/`cSHIFT` only set the mode | The 3712 (one `IN` + `cSHIFT` per byte) and the 3812 (back-to-back `IN`) can't both read a sector from one engine |
| `cDRVSEC` byte is `unit(7:6) | sector(5:0)`, sector 1–26 | Wrong drive/sector selected; CP/M reads garbage (confirmed from the CP/M `rrc rrc` and the FDOS `ani 0C0h`) |
| cWRTBUF `31` pushes the latched byte immediately **and** arms streaming; `30` only arms it; any other command disarms | Every written sector is shifted by one byte, or the whole buffer is zeros |
| BUSY (status bit 0) always reads **0** — synchronous | A BIOS that polls BUSY before the first status read waits forever if it ever reads 1 |
| Write to a write-protected disk **fails cleanly** (status bit 4), does not hang | A wedged guest (the 88-DCDD had exactly this bug) |
| `IN C1` floats `FF` | A driver that reads C1 for status gets a plausible-looking wrong answer |

## Limitations and deliberate departures

- **Blank-disk FORMAT is not modeled.** The iCOM format-track command and `cLDCFG` bit 5
  (format mode) are accepted but do nothing; all shipped images are pre-formatted. `MOUNT … CREATE`
  is not wired for this card.
- **No interrupts or DMA.** The real card can raise `IDONE` and do DMA (`IRSTR`/`IWSTR`); the PROMs
  poll BUSY instead, so none of it is modeled.
- **CRC is never wrong.** `cRDCRC` and the CRC-error status bit exist, but a faithful image always
  reads back clean, so the error path is structural, not exercised.
- **FDOS-I needs more memory than the 48 K machines.** The `FDOS-I (2SIO)` disk from deramp loads
  its resident executive into **high memory** (it calls into the `D000` region), so unlike the 48 K
  CP/M and FDOS-III setups it needs RAM above the boot PROM. `examples/icom/fdos-i.toml` supplies it
  by adding a RAM board over `C500-FFFF` — everything above the controller's PROM (`C000-C3FF`) and
  6810 scratch (`C400-C4FF`) — matching the near-full-64 K map SIMH boots this disk under. With only
  48 K it stalls in the `CALL` into the unmapped high region. FDOS-I's Executive also uses a
  different command language from FDOS-III: **single-letter directives** at the `!` prompt (`L` lists
  the directory, `A` assembles, `P` prints), not FDOS-III's word commands.

## Verification

- **Unit** (`tests/test_icom.cpp`): drives C0/C1 directly over `MemoryMedia` fixtures — SD read
  and write-then-read through **both** buffer protocols, the write-protect and drive-not-ready
  status bits, the DD 128-byte track 0 / 256-byte data-track geometry with the past-sector `FF`
  clamp, and the PROM + scratch-RAM memory windows (including the FDOS relocation to `C000`).
- **Acceptance** (`tests/acceptance/icom*.exp`, label `acceptance`): boots the real period disks
  on a whole machine through the CLI — `acceptance-icom` (FD3712 CP/M `A>` + `DIR STAT`),
  `acceptance-icom-dd` (FD3812 double-density `A>` + `DIR PIP` — the mixed-density read proof),
  and `acceptance-icom-fdos` (FDOS-III `!` + `LIST EXEC`). Each mounts the tracked master
  write-protected and shasums it before and after.
- **End to end**: booting CP/M read/write, `PIP`-ing a new file, and cold-rebooting shows the
  file persisted — the write path lands on the image.

## References

- `reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md` — the distilled hardware reference.
- `reference/FDOS-II Manual.md`, `reference/FDOS-III Manual.md` — iCOM's own disk OS.
- `roms/ICOM-FD3712-CPM/`, `roms/ICOM-FD3712-FDOS/`, `roms/ICOM-FD3812-CPM/` — the boot PROMs.
- `docs/roms.md` — the PROM provenance and CRCs.
