# iCOM FD3712 & FD3812 Floppy Disk Systems (Pertec CF3700 / Double Density Controller)

Source: [FD3700 Interfacing Guide.pdf](#) (Pertec/iCOM *Interfacing Guide for iCOM Model
FD3700 Series Floppy Disk Systems*, Rev B, April 1978) and
[FD3812 and FF38-XX Frugal Floppy.pdf](#) (Pertec/iCOM *FD3812 Double Density Flexible Disk
System and FF38-XX "Frugal Floppy" Series User's Guide*, doc 250297A, May 1978).

Two 8-inch floppy subsystems from **iCOM** (by 1978 the *Microsystems Division of Pertec
Computer Corporation*). Both are sold as complete systems — a controller board (or boards), one
to four AC-driven Pertec drives, cables, and (on the boxed versions) a power supply and
cabinet. The **"Frugal Floppy"** name is the bare, cabinet-less form of each: **FF37** for the
single-density FD3700 family, **FF38-XX** for the double-density FD3812. Neither is emulated by
altairsim today; this reference is kept because the **FD3812 is an S-100 board that interfaces
to a MITS 8800** (the double-density guide says so outright), and because both share one clean
programmed-I/O controller model that is worth capturing once.

This is a distilled emulation reference for both systems **documented together**, because they
are the same controller family: identical command mnemonics, the same eight-line command-word /
data-out / data-in handshake, the same seek/read/write/CRC/deleted-mark sequences, and the same
status-byte layout. The single-density FD3700 came first; the FD3812 is its double-density,
S-100/single-board successor and is a **superset** — everything the FD3700 does, plus double
density and a configuration command. Where they differ, the difference is called out inline and
summarised in §1. **Out of scope** (recorded in the scans, not here): the gate-level schematics,
PROM/parts lists, drive servo internals, cable part numbers, and the troubleshooting guide.

## 1. The two systems at a glance

| | **FD3700 family** (FD3712 / FF37 / **CF3700**) | **FD3812** (FF38-XX / **Double Density Controller**) |
|---|---|---|
| Density | **Single only** (IBM 3540 / 3740 FM) | **Single *and* double** (IBM 3740 FM + IBM 2D MFM) |
| Controller board | **Two** boards, Z1 + Z2 (127 ICs), general-purpose | **One** board, S-100 form factor |
| Host interface | General µP — worked examples for **8080, 6800, RCA 1800** (iCOM FDOS-II OEM software) | **S-100 for the MITS 8800**; Multibus via an adapter board |
| Drive | Pertec **FD511** | Pertec **FD514** (modified, P/N 250254) |
| Bytes/sector | 128 (all tracks) | 128 (single) · **256** (double, except track 0 = 128) |
| Sectors/track | 26 | 26 |
| Tracks | 77 | 77 |
| Bytes/diskette | 256,256 | 256,256 (SD) · **509,184** (DD) |
| Disk transfer rate | 31,250 B/s (250 kHz cell) | 31,250 B/s (SD) · **62,500 B/s** (DD) |
| Extra command | — | **Load Configuration (15)** — density + format mode |
| Signal drivers | earlier TTL (7404/8096/8098) | 74LS240 throughout; open-collector `IRSTR`/`IWSTR`/`IDONE` |
| DMA / strobes | programmed I/O or DMA (independent I/O buffers) | programmed I/O, **or** DMA via `IRSTR`/`IWSTR` (non-buffered controller option) |

Everything in §2–§6 is **common to both** unless a row or a ⚠ says otherwise.

## 2. Programming model — the command/handshake interface

The controller is a **programmed-I/O peripheral**, not a memory-mapped one. The host exchanges
three eight-bit groups with it, all carried on the I/O cable:

- **CPU 0–7** — the **command word**, written by the host. **Negative (zero) true**: a command
  bit is *asserted* when its line is **low**. CPU 0 doubles as the **command strobe** (the
  leading edge latches the command).
- **CDO 0–7** — **data out** from host to controller (track/unit/sector/write-buffer bytes).
- **DI 0–7** — **data in** to the host. These lines are **multiplexed**: when command bit
  **CPU 6 is true (low)** they carry **read-buffer data**; when **CPU 6 is false (high)** they
  carry the **status byte** (§4).
- **DONE** (`IDONE`, active-low pulse) — ~100 ns pulse when a read/write/seek completes, errors,
  is cleared mid-flight, or the controller loses power while busy. Optional to use; software can
  poll BUSY instead.
- **IRSTR / IWSTR** (FD3812, DMA option only) — read strobe / write strobe for non-buffered DMA
  transfer. Not present in the buffered/programmed-I/O path.

⚠ **Every command except EXAMINE STATUS and EXAMINE READ BUFFER must be preceded by returning
CPU bit 0 to the zero (idle) state** — done by issuing a READ STATUS or READ DATA BUFFER first.
Data on CDO must be stable before the leading edge of CPU 0 and hold ≥2.0 µs after it.

The controller carries **two 128-byte buffers** (one read, one write), each holding a full
sector. The write buffer **recirculates**, so a sector can be rewritten without reloading it.

## 3. Command set

Codes are the eight command-word bits read as a hex byte. **BUSY = Yes** means the command sets
the BUSY status and loads the head (a mechanical operation); the host then loops on status until
BUSY clears.

| Command | Hex | BUSY | Both? | Notes |
|---|---|---|---|---|
| Examine Status | `00` | No | ✓ | Places status byte on DI (CPU 6 high) |
| Read | `03` | Yes | ✓ | Read sector → read buffer |
| Write | `05` | Yes | ✓ | Write buffer → sector |
| Read CRC | `07` | Yes | ✓ | Verify CRC without disturbing the read buffer |
| Seek | `09` | Yes | ✓ | Seek to the loaded track address |
| Clear Error Flags | `0B` | No | ✓ | Clears CRC-error + deleted-data-mark status bits |
| Seek Track 0 | `0D` | Yes | ✓ | Recalibrate to track 0 (use at power-up) |
| Write w/ Deleted Data Mark | `0F` | Yes | ✓ | Like Write, but writes a DDAM ahead of the data field |
| Load Track Address | `11` | No | ✓ | Latch CDO → seek-track register |
| Load Unit/Sector | `21` | No | ✓ | Latch CDO → unit (0–3) + sector (1–26) register |
| Load Write Buffer | `31` | No | ✓ | Push one byte into the write buffer (128×) |
| Examine Read Buffer | `40` | No | ✓ | Place read-buffer front byte on DI |
| Shift Read Buffer | `41` | No | ✓ | Advance the read buffer (127 shifts read all 128 bytes) |
| Clear | `81` | No | ✓ | Halt any operation, clear BUSY, pulse DONE, unload head |
| **Load Configuration** | `15` | No | **FD3812 only** | Set density + format mode (§6) |

⚠ **FD3812 command-table misprint.** The double-density guide's command table prints the hex
for **Read as `0B`** and **Write as `06`**, but its own bit columns read `00000011` (=`03`) and
`00000101` (=`05`) — matching the FD3700 exactly. The bit patterns and the older manual agree, so
`03`/`05` are used here; treat the FD3812 printed hex as a typo and confirm against the PROM
(U119) before trusting it [[altairsim-plausible-but-wrong-timing]].

## 4. Status byte

Read on DI 0–7 when **CPU 6 is high (false)**. Same bit positions on both systems:

| Bit | Signal | Meaning |
|---|---|---|
| 7 | Found Deleted Data Mark | Set if a DDAM preceded the last sector read; cleared by Clear / Clear Error Flags |
| 6 | Media Status | FD3712: media-or-CRC error line. **FD3812: always 1** (reserved; DI 6 of EXAMINE STATUS is reserved for two-sided systems) |
| 5 | Drive Fail | 1 = selected drive not ready (not up to speed, door open, no diskette, unplugged) |
| 4 | (Selected Unit) Write Protect | 1 = the selected drive holds a write-protected diskette |
| 3 | CRC / Media Error | 1 = data error on the last seek/read; must be cleared before further commands |
| 2 | Unit Select code bit 1 | The address (0–3) of the currently selected drive |
| 1 | Unit Select code bit 0 | " |
| 0 | Busy | 1 = a read/write/seek is in progress. Only EXAMINE STATUS (and Clear) may be issued while busy |

Unit-select decode: `UN1 UN0` = `01`→0, `01`→1, `10`→2, `11`→3 (per the FD3700 table, which
distinguishes units 0/1 by a separate line pair — treat unit as the 2-bit field here).

## 5. Operational sequences

Common to both systems (double-density adds only the Load Configuration step in §6):

- **Seek** — Load Unit/Sector (`21`), Load Track Address (`11`), Seek (`09`), then loop on
  BUSY/CRC in status. The controller reads the ID field, compares actual vs desired track, steps
  the head, and re-reads to verify (so a defective/renumbered track is chased until found).
- **Seek Track 0** (`0D`) — recalibrate at power-up/restart; no track address needed.
- **Read** — seek to unit/track/sector, Read (`03`), check CRC (re-read if set), then Examine
  Read Buffer (`40`) + Shift Read Buffer (`41`) 127× to pull all 128 bytes. Read data is clocked
  into the buffer at 250 kHz.
- **Write** — Load Write Buffer (`31`) 128×, seek to unit/track/sector, Write (`05`), then Read
  CRC (`07`) and retry if CRC set. Six bytes of zeros + the (deleted) data address mark are
  written ahead of the data field; the controller appends a two-byte CRC.
- **Write with Deleted Data Mark** (`0F`) — as Write, but tags the sector with a DDAM (later
  reads set the Found-DDAM status bit; useful to mark end-of-field or bad sectors).
- **Read CRC** (`07`) — re-checks a just-written sector without disturbing the read buffer.
- **Clear** (`81`) — aborts, clears BUSY, pulses DONE, unloads the head.
- **Clear Error Flags** (`0B`) — resets the CRC-error and deleted-data-mark status bits.

Timing (FD3700 figures; FD3812 drive is the faster FD514): track-to-track **10 ms**, head
load/settle **≤40 ms**, full 77-track seek **≤820 ms**; read/write **~6 ms/sector**, latency
(½ rev) **83 ms**, buffer shift **≤500 kHz**. FD3812 rotation is **360 rpm** (83.3 ms latency,
166.67 ms/rev).

## 6. Disk geometry and formats

Both format their own blank media (soft-sectored; **hard-sectored diskettes are not allowed**).
77 tracks (0 outermost … 76), 26 sectors/track, index hole between sector 26 and sector 1.

| | Single density (FM) | Double density (MFM) |
|---|---|---|
| Encoding | Frequency modulation (clock bit before every data bit) | Modified FM (clock only for a 0 preceded by a 0) |
| Data bytes/sector | 128 (all tracks) | **256** (track 0 stays **128, single density**) |
| Bytes/diskette | 256,256 | 509,184 |
| Address mark | data `FE`, clock `C7` | 3× `A1` (missing clock) + `FE` |
| Data mark | data `FB`, clock `C7` (DDAM = `F8`-family) | 3× `A1` + `FB` |
| Index mark | data `FC`, clock `D7` | 3× `C2` + `FC` |
| Gap bytes | G1 `FF`, G2 `FF`, G3 `FF`, G4 `FF` (+ `00` preambles) | G1/G2/G3/G4 `4E` (+ `00` preambles) |
| CRC | 2 bytes, poly **x¹⁶+x¹²+x⁵+1**, preset all-ones | same |

⚠ **On a double-density diskette, track 0 is recorded in *single* density.** So before
seeking/reading/writing track 0 of a DD disk you must set the density bit **back to single**;
before a DD seek *from* track 0 you must set it to **double**. This is the whole reason the
Load Configuration command exists.

**Load Configuration (`15`)** — CDO byte: **bit 4 = Double Density** (0 = single, 1 = double),
**bit 5 = Format mode** (0 = normal, 1 = format). Bits 0–3, 6, 7 = 0 (reserved for two-sided
systems). Does **not** set BUSY. In **format mode** a Write is reinterpreted as *format one
track* and a Seek steps without reading the header, so a Seek/Write loop formats the whole disk;
**bit 4 must be 0 when formatting track 0**.

## 7. Physical and electrical differences

- **Controller form.** FD3700 = two edge-connectored boards **Z1** (command/data input, P5) and
  **Z2** (data/status output, P4), joined by ribbon cables; 184×381 mm each, no card cage.
  FD3812 = **one** S-100 board (connectors J1 I/O, J2 drive, J3 power, J4 optional LED status).
- **Host cable.** FD3700 I/O cable = 50-cond ribbon → P9 (50-pin, to CPU) splitting to P5
  (26-pin) + P4 (20-pin). FD3812 I/O cable = 50-cond ribbon → J1 (same CPU-0..7 / CDO-0..7 /
  DI-0..7 / DONE assignment; DMA `IRSTR`/`IWSTR` on pins 21/22).
- **Drive cable.** 50-conductor daisy chain; **drive 0 is the terminus** and carries the line
  terminator. Up to four drives, master/slave, one accessed at a time. All controller/drive
  signals are **low-true**.
- **Signal levels.** Standard TTL, **negative (zero) true** on the host interface; positive-true
  is a factory option. FD3700 inputs = 7404 + 680 Ω pull-up (Options A/B for special loads),
  outputs = DM8096 tri-state, 32 mA sink. FD3812 inputs/outputs = 74LS240, 24 mA sink;
  `IRSTR`/`IWSTR`/`IDONE` are open-collector.
- **Power.** Controller +5 V and −12 V (FD3812 also routes +24 V through J3 to the drive; FD3700
  drive power via P8). FD511/FD514 need +5, +24, and −5/−12; systems run 90–130 VAC 60 Hz (or a
  190–250 VAC 50 Hz option). FD3812 boxed system: 7.75×19.16×20.5 in, 70 lb, <300 W.
- **Options.** FD3812 ships a non-buffered/DMA controller variant (no read/write buffers — uses
  `IRSTR`/`IWSTR`), a 19-inch rack cabinet, and a controller-less "SLAVE" chassis for 3/4-drive
  master-slave strings.

## 8. Emulation notes / gotchas

- **One controller model, two densities.** If altairsim ever grows an iCOM floppy board, model
  the shared programmed-I/O engine once (command word + two 128-byte buffers + status byte) and
  make double density a mode of it, exactly as the hardware did. The FD3812 is a strict superset
  of the FD3700.
- **The FD3812 is the S-100 / Altair-relevant one.** It plugs into a MITS 8800 bus; the FD3700
  is a general-µP OEM controller (its own guide shows 8080, 6800, and RCA 1800 hookups). An
  Altair 8800 emulation target should follow the FD3812.
- **DI lines are multiplexed by CPU 6.** Reading "the data lines" is meaningless without the
  command context — CPU 6 low = read-buffer byte, CPU 6 high = status byte. A model that exposes
  a single DI register must switch on the last command word.
- **Track 0 density split is mandatory, not cosmetic.** A DD emulation that records track 0 in
  double density will produce media no real drive can read, and will mis-handle the
  configuration bit dance around track 0. Honor the single-density track 0.
- **Don't trust the FD3812 printed command hex.** Read=`03`, Write=`05` (from the bit columns and
  the FD3700), *not* the table's `0B`/`06`. Verify against the controller PROM before committing
  a number that merely "looks reasonable" [[altairsim-plausible-but-wrong-timing]].
- **These are soft-sectored 8″ IBM-format drives** — unlike MITS's own hard-sectored
  [88-DCDD](Altair%20Floppy%20%2888-DCDD%29%20Manual.md). Their IBM 3740 SD geometry is the same
  128-byte/26-sector/77-track layout the [Tarbell](Tarbell_Floppy_Disk_Interface_Manual.md) and
  [VersaFloppy](SD%20Systems%20VersaFloppy.md) controllers also read, so a `.dsk` image is
  interchangeable at the SD level. Do not invent geometry the manuals don't state
  [[altairsim-no-invented-hardware]].

## 9. Key facts at a glance

| | |
|---|---|
| Family | iCOM (Pertec Microsystems) 8″ floppy; **FD3700/FF37/CF3700** (SD) and **FD3812/FF38-XX** (SD+DD) |
| Interface style | Programmed I/O (or DMA on FD3812): CPU 0–7 command word (**zero-true**, CPU 0 = strobe), CDO 0–7 data out, DI 0–7 data in / status |
| Status vs data | **CPU 6 high → status byte** on DI; **CPU 6 low → read-buffer byte** on DI |
| Buffers | two 128-byte sector buffers (read + recirculating write) |
| Commands | 14 shared (`00`,`03`,`05`,`07`,`09`,`0B`,`0D`,`0F`,`11`,`21`,`31`,`40`,`41`,`81`) + FD3812 **Load Configuration `15`** |
| Status bits | 7 DDAM · 6 Media/(FD3812 always 1) · 5 Drive Fail · 4 Write Protect · 3 CRC Error · 2–1 Unit · 0 Busy |
| Geometry | 77 tracks × 26 sectors; **128 B/sec SD**, **256 B/sec DD** (track 0 always SD); 256,256 B (SD) / 509,184 B (DD) |
| CRC | x¹⁶+x¹²+x⁵+1, preset all-ones, 2 bytes |
| Density switch | FD3812 **Load Configuration** bit 4 (DD) / bit 5 (format mode); track 0 = SD |
| Drives | Pertec FD511 (FD3700) / FD514 (FD3812), up to 4, drive 0 = cable terminus, all signals low-true |
| Host targets | FD3700: 8080 / 6800 / RCA 1800 OEM. **FD3812: S-100 MITS 8800** (Multibus optional) |
| Altair status | **not emulated**; reference for possible future S-100 double-density floppy support |
| ⚠ Errata | FD3812 command table misprints Read (`0B`) and Write (`06`); use `03`/`05` |
