# Soft-sector floppy controllers

Every soft-sector floppy card — the Tarbell #1011/#2022, the SD Systems VersaFloppy, and
whatever WD177x-based card turns up next — is built on the same three layers, and the whole
point of them is that a card only ever touches the top one and reuses the two below unchanged.

> **A board decodes its I/O ports and straps its chip. It does not know what a sector looks
> like, where a track's bytes live in the file, or how a format is parsed. Those belong to
> the chip and the drive, and they are shared.**

That one rule is why adding the Tarbell was mostly a port decode and a boot PROM, why the DD
card is the SD card plus four small overrides, and why this chapter exists once instead of a
copy in each board's source. (For the *serial* seam this mirrors, read `serial-io.md` first —
the shape is identical.)

The three layers, top to bottom:

| Layer | Who | Owns |
|---|---|---|
| The **card** | a `Board` (`src/core/board.h`, e.g. `boards/tarbell.h`) | port decode, register bits, the drive-select latch, the **density strap** (`DDEN`), the boot PROM, interrupt straps |
| The **chip** | `Wd17xx` (`src/chips/wd17xx.h`) | the register file and the Type I/II/III command FSM; `dataRateBits` **is** the `DDEN` pin |
| The **drive** | `DiskImageDrive` (`src/boards/floppy-drive.h`) over a `DiskImage` (`src/host/disk.h`) | CHS ↔ file offsets, synthesized ID fields, the **format parse** |

A card reaches down to the chip (`Wd17xx::attach`, the straps). It never reaches into the
`DiskImage`; the drive is the only thing that touches it.

## The chip: `Wd17xx` and the parts

`src/chips/wd17xx.h` is the FD177x family. Read its header — every comment is load-bearing, and
the chip/drive split is spelled out there at length. The essentials for a board author:

- **The class is the part, the file is the family.** `Wd1771` is single density (FM); `Wd1791`
  is single *and* double density (FM/MFM), with a side-select pin and a one-bit record type.
  The base `Wd17xx` is the whole register file and command FSM; a part supplies only the four
  things that genuinely differ (step-rate table, Read-Address side byte, record-type bits, and
  which data-address-mark a Write writes). Build the part the card has; do not `#ifdef`.
- **No drive select, no side select, no motor.** The chip talks to ONE `FloppyDrive`; the
  card's select latch points it with `attach()`. Side is a card latch too (`setSide`).
- **`dataRateBits` is the `DDEN` pin.** 250 kbit/s is 8″ single density; a double-density card
  writes 500 kbit/s when it decodes its density bit. The chip uses it for byte timing; the
  board sets it from its control-port density bit. This is the **single source of truth for
  density** — do not duplicate it onto the drive.
- **Wait-synced vs DRQ-polling.** A card whose data port stalls the CPU on a wait-state
  generator (PRDY) sets `setWaitSynced(true)`; then every command completes on the register
  access that would have stalled, one byte per access, and Lost Data is correctly unreachable.
  Both Tarbell boards are wait-synced. A DRQ-polling card leaves it off and gets byte timing.

## The drive: `DiskImageDrive` over `DiskImage`

`src/boards/floppy-drive.h` is the generic adapter between the chip's pins and a flat logical
`DiskImage`. A raw `.DSK` holds **sector payloads only** — no gaps, no address marks, no CRCs
(`src/host/disk.h`). So:

- **ID fields are synthesized** from the mounted image's declared per-track `TrackFormat`
  (`sectorIdAt`): the track is the physical head position, the sector counts from the format's
  `startSector` (1 on a soft-sector card), and the CRCs always check (an image carries no rot).
- **`DiskImage` is CHS with per-track geometry.** Each `(track, head)` slot carries a
  `TrackFormat{density, sectors, sectorSize, startSector}`; offsets are a **running sum** over
  the slots (`rebuild()`), so a disk whose tracks differ in size — the mixed-density disk — is
  expressible where one global geometry could not say it.
- **The head position lives in the drive, not the chip's Track Register.** They may disagree;
  that is what a verify catches (Seek Error).

## Geometry in real time — the FORMAT path

This is the reusable core. A soft-sector disk's geometry is **not** fixed at mount: sector
size, count and density can vary track to track, and none of it is in the `.DSK`. So:

> **`Write Track` is the only command that establishes or mutates a track's geometry.** Reads
> and writes never do; they *validate* against what a track records.

The chip side is already done (`Wd17xx`, no per-board work): on `Write Track` the chip asks the
drive for `trackImageBytes()`, and if it is positive it enters the write phase, accumulates
every guest byte into an internal buffer, and hands the whole revolution to
`drive->writeTrackImage(buf)` at the end. When `trackImageBytes()` is `0` it sets **WRITE FAULT
(S5)** instead — the honest answer for an empty drive or a controller that does not format.

`DiskImageDrive::writeTrackImage` (in `floppy-drive.cpp`) is the format FSM, run over the whole
collected buffer:

```
gap … 0xFC(index) … gap … 0xFE track side sector N 0xF7 … gap … 0xFB <data…> 0xF7 … gap … (repeat)
        ^ID address mark  ^length code               ^data address mark   ^CRC-generate
```

Per sector: `sectorSize = 128 << N`, capture the first sector number as `startSector`, and
**accumulate the data field until `0xF7`** — the CRC-generate byte, which can never appear as
literal track data, so accumulate-until-`0xF7` is unambiguous (the `0xE5` fill and the `0xDD`
density signature are ordinary data, not special). Then:

1. Derive `TrackFormat{density = <chip density>, sectors, sectorSize, startSector}` and call
   `img->setTrackFormat(head, side, tf)`. That marks the slot valid and re-runs `rebuild()`, so
   the following tracks' offsets — and the growth cap — follow.
2. Write each sector's payload by a **sequential 1..N counter** from `startSector`, ignoring the
   header's possibly-skewed sector number, so the fill stays contiguous and the file grows in
   order. **Write exactly the bytes the guest streamed — never fabricate or pad the fill.** A
   data field shorter than the recorded `sectorSize` is a malformed track and `writeSector`
   rejects it → WRITE FAULT, which is honest.

Return `false` (→ WRITE FAULT) only if nothing parsed or a write could not land.

### The ascending-track-order invariant

Correctness rests on one fact: **FORMAT writes tracks 0→N in order.** So when a track is
(re)formatted to a *larger* geometry, `rebuild()` moving the following tracks' offsets clobbers
nothing valid — they have not been written yet, or are about to be overwritten. Every real
Tarbell format program formats ascending (`pd2/FORMAT.ASM`, `pd2/DFORMAT.ASM`). A fully-correct
out-of-order / cross-density reformat of an *already populated* disk would have to shift the
tail by the size delta first; that is **deferred** (see below).

### Growth: `setExtendsOnWrite` and the dynamic cap

`DiskImage::setExtendsOnWrite(true)` lets the backing file grow as sectors are written, capped
at `geometryBytes_`. For a soft-sector card that cap is **dynamic** — it rises as each track is
formatted (`setTrackFormat` → `rebuild`). A recognized full disk never grows (its writes stay
in bounds); a blank one grows track by track as it formats. The board turns it on at mount.

## The density model

Density is one value with three faces, and they must agree:

| Face | Where |
|---|---|
| The `DDEN` pin | `Wd17xx::dataRateBits` (250 kbit/s SD, 500 kbit/s DD) |
| The board I/O bit | e.g. Tarbell DD `OUT FC` bit 3 (`reference/Tarbell_Floppy_Disk_Interface_Manual.md`: "D3 = density") |
| The recorded per-track density | `TrackFormat.density`, written by `Write Track` |

Reads and writes **validate** the controller's current density against the addressed track's
recorded density and return **Record Not Found (S4)** on a mismatch (or an unformatted /
out-of-range track) — never WRITE FAULT. The **format path is never density-gated**: it
*records* density. The guest-side proof of the board bit driving the chip is `pd2/DFORMAT.ASM`
(`ORI 8` / `OUT FC` before a DD format); cross-reference the WD `WD177X-00` datasheet's `DDEN`
description.

> **Read-side density gate: implemented as of the double-density cut, not the single-density
> one.** On an SD-only card `dataRateBits` is always 250 kHz, so the gate is a no-op and reads
> validate through `locate()` alone (unformatted / out-of-range → RNF). It becomes live when a
> card carries both densities.

## Mount vs. format — where geometry starts

`describeGeometry` (the board's size probe) establishes the *initial* geometry so a recognized
disk is usable immediately with no FORMAT:

- A recognized size → that format (padding-tolerant via `sizeMatches`, for the XMODEM pad).
- **A blank / short image → an *unformatted* disk**, mounted at the card's track count with
  **empty** per-track geometry (no ranges): READY and steppable, every access RNFs until
  `Write Track` lays a track down. This is what makes `MOUNT … CREATE` (a 0-byte file)
  formattable. Empty-pending is preferred over fabricating slots, since `Write Track` is the
  sole source of geometry. The default rule matches SIMH's `tarbell_attach`: *anything that is
  not a recognized size is single density.*
- Oversized / garbage is still an error — a real track is never larger than one revolution.

## The `trackImageBytes()` budget — the load-bearing number

Under wait-synced operation there is no index-pulse timeout: `Write Track` completes **exactly**
when the collected buffer reaches `trackImageBytes()`. So that value **is** the per-track raw
byte budget, and it is the one number most likely to bite:

- **Too large → the command hangs**, waiting for bytes the guest will never send.
- **Too small → the last sectors truncate**, because the command commits before the guest has
  streamed them.

It must be `≥` everything the format program streams before its trailing gap. The 8″
single-density value is **5208** bytes (250 kbit/s FM at 360 RPM, one 166.67 ms revolution);
`pd2/FORMAT.ASM` streams ~4882 structured bytes then pads with `0xFF` (its `ENDTRK` loop) until
the controller signals INTRQ — which is exactly when the buffer hits the budget. Validate this
number against the format program's gap tables, not by "it booted."

## The flat-`.DSK` limitation

A raw `.DSK` cannot record heterogeneous per-track geometry — it is payload bytes only. So the
geometry is **re-derived from the file size on every remount** (`describeGeometry`), which is
fine for a uniform SSSD disk and the one standard mixed disk, but a `.DSK` that had been
formatted to some *bespoke* per-track layout would lose it across a remount. An IMD/TD0-style
container that carries its own sector map would fix this — and is explicitly never coming
(DESIGN.md §7.3): such files are converted to raw beforehand.

## Deferred

- **Shift-tail resize** — on a `Write Track` that changes a *populated* track's total size,
  `memmove` the following data by the size delta before `setTrackFormat`, so a partial /
  out-of-order / cross-density reformat stays correct. Not needed for blank format or a
  whole-disk ascending reformat.
- **Double-density / mixed-density blank format** — the DD card's rate-doubled
  `trackImageBytes()` and a blank fallback for the DD probe, plus a size/format argument to
  `CREATE` (or first-format-defines-it) to choose SD vs DD for a fresh disk. The FSM and the
  density plumbing are already density-agnostic.
- **A real-CP/M FORMAT.COM acceptance test.** The multi-drive period formatter (`pd2/FORMAT.COM`)
  is assembled for the **double-density** interface's bitmap drive-select and formats a
  *non-boot* drive; the single-density `#1011`'s function-decoder select cannot be driven by it,
  and the SD-only formatters (`FORMAT91`, the CP/M 1.4 `FORMAT`) only ever format drive A — the
  boot disk. So an end-to-end CP/M format test belongs with the DD cut, which is where that
  software actually runs. The single-density mechanism is covered end-to-end at the board level
  in `tests/test_tarbell.cpp` (the real `FC`/`FB` port discipline, all 77 tracks, the file
  growing to 256,256 bytes, and the `0xE5` fill reading back).

## Adding another soft-sector controller — the checklist

1. **Build the right WD part** in the board's `buildChip()` — `Wd1771` for a single-density
   card, `Wd1791` for one that does double density — and `setWaitSynced(true)` if the data port
   stalls the CPU.
2. **Strap density** from the control-port bit into `chip_->dataRateBits` (leave it at 250 kHz
   for an SD-only card).
3. **Size-probe with a blank fallback** in `describeGeometry`: recognized sizes → their format;
   anything smaller → empty per-track geometry (unformatted); oversized → error.
4. **`setExtendsOnWrite(true)`** on the image at mount, and grant the drive its format budget
   (`setTrackCapacity(bytesPerRevolution)`) — leave it `0` on a card that does not format, and
   `Write Track` keeps faulting. (Density is not passed: it is the chip's, and the format path
   records it from there.)
5. **Reuse `DiskImageDrive`'s format path** unchanged — the parse, the sequential fill and the
   `setTrackFormat`/`rebuild` are controller-agnostic.
