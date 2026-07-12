# 88-DCDD — MITS Disk Controller (8" Pertec FD-400)

**Status:** not implemented (milestone 3). Register map recovered and cross-checked.

## The real hardware

The MITS 88-DCDD is the Altair's 8" floppy controller, driving up to 16 daisy-chained Pertec FD-400 drives (4 is typical). It is an unusually *raw* controller: it does not read or write sectors for you. Software steps the head, watches the sector counter go by, and shifts bytes one at a time through a data port in real time. Nearly all the disk logic lives in the BIOS.

## Sources

| Source | Path | Authority |
|---|---|---|
| `BOOT.ASM` | `disks/mits-88dcdd/cpm22/buffered/BOOT.ASM` | **Authoritative, and in the tree.** Mike Douglas's Altair CP/M 2.2b loader. Carries the complete equate block, and its cycle-count comments carry the *timing* (below). A period artifact written against real hardware — DESIGN.md §0.1's first-hand source. |
| `BIOS.ASM` | `disks/mits-88dcdd/cpm22/buffered/BIOS.ASM` | **Authoritative.** The same equates, plus the real read/write/seek loops. |
| `DBL.ASM` | `roms/DBL/DBL.ASM` | The 4.1 boot PROM, disassembled by Martin Eberhard. **Independently agrees** with the equates above — two sources, one answer. |
| MITS manual | `reference/Altair Floppy (88-DCDD) Manual.pdf` | The card's own documentation. A 52 MB scan with no text layer; read it as page images. |
| `mits_dsk.c` | `../AltairClaude/reference/mits_dsk.c` | **NOT a source. It is SIMH.** See the note below. |

> ### `mits_dsk.c` is not authoritative, and this doc used to say it was
>
> The row above used to read *"**Authoritative.** Patrick Linstruth's own SIMH module… a cleaner
> spec than the MITS manual."* It is a fine emulator and it is Patrick's own work, but **DESIGN.md
> §0.1 says hardware facts never come from another emulator's source, and it names SIMH.** The rule
> exists because a simulator is full of decisions that are *about simulating* and not about the
> hardware — and this card is where that bit.
>
> Specifically: `mits_dsk.c` advances the sector counter **on every second read of port 0x09**, and
> this doc faithfully copied that and called it *"the entire rotating-disk simulation. Subtle, and
> load-bearing."* **It is neither.** It is a way to fake rotation when you have no trustworthy cycle
> clock. No 88-DCDD ever advanced its sector counter because the CPU read a status port — the counter
> is driven by 32 sector holes going past a photodetector, and it turns whether the guest is looking
> or not. We have a T-state clock, so we model the disk (`Spindle`, DESIGN.md §7.5.1) and the
> question does not arise.
>
> Keep the file for cross-checking a bit map. Do not take behaviour from it.

> **RESOLVED 2026-07-12** (this note used to say the `I` and `Z` status bits were unresolved): the
> Python prototype was **wrong** and the table below is **right**. `BOOT.ASM` and `DBL.ASM` agree
> independently — `sINTEN equ 20h`, `sTRACK0 equ 40h`. Note also that **status bit 4 (0x10) is
> genuinely unassigned**: the `10h` in the equates is `cINTEN`, which lives on the *control* port.

## Register reference

Ports **0x08, 0x09, 0x0A** (octal 10/11/12).

| Port | OUT (write) | IN (read) |
|---|---|---|
| 0x08 | Select / enable drive | Drive status — **INVERTED** |
| 0x09 | Drive command (step / head / write-enable) | Current sector position |
| 0x0A | Write data byte | Read data byte |

### 0x08 OUT — drive select
`| C | x | x | x | Device[3:0] |` — C (0x80) = 1 deselects/clears the controller; 0 selects. Device 0–15.

### 0x08 IN — status. **Returned COMPLEMENTED: 0 = true, 1 = false.**
Keep internal flags true-sense and complement on read (`(~flags) & 0xFF`). Returns `0xFF` when no drive is selected.

| Bit | Name | Meaning (true sense) |
|---|---|---|
| 0x01 | `sENWD` W | Write circuit ready for another byte |
| 0x02 | `sMOVEOK` M | Head movement allowed |
| 0x04 | `sHDSTAT` H | Head loaded |
| 0x08 | `sDSKEN` | Disk selected / enabled |
| 0x20 | `sINTEN` I | Interrupts enabled (ignored by the sim) |
| 0x40 | `sTRACK0` Z | Head on track 0 |
| 0x80 | `sNRDA` R | New read data available |

### 0x09 OUT — control
| Bit | Name | Action |
|---|---|---|
| 0x01 | `cSTEPI` | Step head IN one track |
| 0x02 | `cSTEPO` | Step head OUT one track (sets track-0 flag at 0) |
| 0x04 | `cHDLOAD` | Load head. **Also `cRESTMR`** (restart motor-off timer) on the minidisk. |
| 0x08 | `cHDUNLD` | Unload head |
| 0x10 | `cINTEN` | Enable interrupts (ignored) |
| 0x20 | `cINTDIS` | Disable interrupts (ignored) |
| 0x40 | `cHCSON` | Lower head current (ignored) |
| 0x80 | `cWRTEN` | Start write-enable sequence |

Any step **invalidates the sector/byte position**.

### 0x09 IN — sector position
`| x | x | Sector[4:0] | T |`, built as:

```c
return ((sector << 1) & 0x3E) | 0xC0 | sector_true;
```

Bit 0 (`sNEWSEC` / T) is **Sector True, and it is LOW (0) when the sector is positioned** for read/write. Returns `0xFF` if the head is not loaded.

The layout looks arbitrary until you see the BIOS read it, and then it is beautiful:

```asm
dnLoop  in   DRVSEC   ;read sector position register
        rar           ;wait for sector true (0=true)
        jc   dnLoop
        ani  SECMASK  ;A=sector number found
```

One `RAR` drops T into carry **and** shifts the 5-bit sector number from bits 5..1 down into bits
4..0, where the mask is waiting for it. The sector sits in the middle of the byte *so that this
works*. Note too that the BIOS does **not** wait for a particular sector — it waits for *any*
sector boundary and takes whichever one it lands on, because it buffers the whole track.

### The rotation model — THE DISK TURNS ON ITS OWN

**The sector under the head is a reading taken off the clock, not a counter this card advances**
(`Spindle`, DESIGN.md §7.5.1):

```
sector = (now / tPerSector) % 32
```

> **This paragraph used to say the exact opposite**, in bold, and call it *"the entire rotating-disk
> simulation. Subtle, and load-bearing."* What it described — the counter advancing on every second
> read of this port — is **SIMH's**, not the card's. It makes the platter spin at the speed of the
> software polling it: a tight BIOS loop outruns a slow one, and a recorded session stops replaying
> identically. See the `mits_dsk.c` note at the top of this file.

### The timing — and it is the MANUAL's, not arithmetic

`BOOT.ASM`'s cycle-count comments give the byte rate directly:

```asm
; The sector transfer loop is 116 cycles for two bytes read (has to be less than 128)
        in   DRVDATA   ;(10) read first byte at 24-48 cycles
        in   DRVDATA   ;(10) read at 70-94 cycles (data at 64 and 128)
```

**A byte every 64 T-states** at 2 MHz — 32 µs, i.e. exactly **250,000 bits/sec**, the standard 8"
single-density rate, and the manual says the same thing in words: *"ENWD goes true every 32 µs."*
So the byte clock is the **medium's**, and like rotation it derives from `Clock::hz()`: a 4 MHz CPU
does not make the disk read faster.

Everything else is an **RC one-shot on the card** — a 74123 with a resistor and a capacitor — and the
MITS manual prints both the nominal value and the range a working board must calibrate to:

| Signal | Nominal | IC (74123) | Manual |
|---|---|---|---|
| **Sector True** | **30 µs** (= **60 T** @ 2 MHz) | F4, C8 = .01 µF, R11 = 10K | *"D0 – SR0 – Sector True – True when = 0, **and is 30 µs long**."* Calibrates 20–40 µs. |
| Read Clear | 140 µs | F1, R5 = 10K, C3 = .047 µF | *"Read data will be available 140 µs after SR0 goes true."* |
| Write Enable | 280 µs | F4 (other half) | *"Write data will be requested 280 µs after D0 goes true."* |
| Head settle | 40 ms | B1, Board #2 | *"HS – True 40 ms after head loaded."* |

So one sector looks like this (T-states at 2 MHz):

```
 0        60                   280                                        9,048
 |--------|---------------------|------------------------------------------|
 | SECTOR |                     | byte 0 | byte 1 | ... | byte 136          |  ~1,368 T
 | TRUE   |   (read clear)      |<------- 137 bytes x 64 T = 8,768 T ------>|  of slack
 | 30 us  |     140 us          |                                           |
 |<---------------------------- 10,416 T  (5.2 ms) ------------------------>|
```

> ### ⚠️ Sector True is a 30 µs ONE-SHOT. It is NOT the inter-sector gap.
>
> This section previously derived it: 10,416 − 8,768 = 1,648 T of gap, therefore sector-true is
> 1,648 T wide, *"and the three numbers add up because they are the same three numbers the drive
> had."* That was **wrong by a factor of twenty-seven**, and it was wrong in the most dangerous
> possible way — it was tidy, self-consistent, and it **booted CP/M perfectly**. A window that is too
> generous is *forgiving*: the guest never complains, so nothing ever tells you.
>
> The real window is **60 T-states against a 24-T-state poll loop — about two and a half spins of
> margin.** That tightness is deliberate. The manual instructs the programmer: *"The write mode
> should begin **as close as possible** to the time that D0 goes true."* The hardware expected
> software to be right on the edge, and a simulator with a luxurious window is not being kind to the
> guest, it is lying to it — it hides exactly the races a real card would punish.
>
> The manual was in `reference/` the whole time. DESIGN.md §0.1, one more time, and this one is mine.

`sNRDA` goes true once per byte-time through the data window, and `sENWD` does the same on the write
side, starting at 280 µs.

**One thing the manual contradicts itself about, and does not reconcile:** the prose says the first
byte is readable **140 µs** after sector-true, while the READ/WRITE timing diagram dimensions the
first NRDA at **280 µs**. We take the prose, because it matches the READ CLEAR one-shot the schematic
actually shows. Nothing observable rides on it — the BIOS polls NRDA rather than counting — and
either figure leaves the 137 bytes room to finish inside the sector.

### Write sequence
1. Step to the desired track.
2. Poll `IN 0x09` until the desired sector number appears with T = 0.
3. `OUT 0x09, 0x80` (`cWRTEN`).
4. The `W` bit in the 0x08 status goes active; the controller consumes **exactly 137 bytes** written to port 0x0A.
5. `W` drops; the sector is committed.

Read side reads 137 bytes from port 0x0A.

## Geometry

Every slot is **137 bytes**, on every format. Sectors are numbered **from 0** (`startSector = 0` —
the Tarbell numbers from 1, and DESIGN.md §7.3 calls that the off-by-one that silently corrupts a
disk). Byte offset is `137 * sectorsPerTrack * track + 137 * sector`.

| Format | Tracks | Sectors | `DATATRK` | Bytes | `media =` |
|---|---|---|---|---|---|
| **8"** | 77 | 32 | 6 | 77 × 32 × 137 = **337,568** | `8in` |
| **Minidisk** | **35** | **16** | **4** | 35 × 16 × 137 = **76,720** | `minidisk` |
| **FDC+ 8 MB** | 2048 | 32 | 6 | 2048 × 32 × 137 = **8,978,432** | `fdc8mb` |

> **The minidisk row is three facts, and this doc used to have only one of them** (the 16 sectors).
> Tracks are **35**, not 77, and **`DATATRK` is 4, not 6** — so the system/data boundary moves, and a
> minidisk read with the 8" `DATATRK` decodes data sectors with the system layout and returns
> garbage. Source: `BOOT.ASM`'s `MINIDSK` equates.

The minidisk also **ignores head-unload**, and its `cHDLOAD` bit doubles as `cRESTMR` — restart the
motor-off timer (see the control table).

> ### The size probe needs a tolerance, and without it BOTH of our 8" disks are rejected
>
> The two 8" images in the tree are **337,664 bytes, not 337,568** — XMODEM padded them up to a
> 128-byte block boundary. A strict `size == exact` probe rejects both. Match with **`sizeMatches()`**
> (`src/host/disk.h`): `exact <= size < exact + 128`. The pad is never data and a write never reaches
> it. The other two formats are already clean multiples of 128, so only the 8" disk shows the trap —
> which is exactly how a strict probe survives review and then fails on the only disks anyone has.

**The 88-DCDD is a HARD-SECTOR controller, and that is the fact everything else follows from.** Its images contain the **entire 137-byte slot** — sync byte, track/sector header, 128-byte payload, checksum, stop byte, trailer — not just the payload. Soft-sector controllers (Tarbell, Disk 1A, North Star) store the **payload only**, because on real media their headers and checksums lived in the inter-sector gaps and never reached the image file. Anything that reads a `.DSK` without knowing which kind of controller wrote it reads garbage.

In the `DiskImage` service (`DESIGN.md` §7.3) this needs no special flag — it is just `sectorSize = 137` where a soft-sector board would say `128`:

```cpp
img.init(2048, 1, /*interleaved=*/false);                 // 8 MB FDC+
img.initFormat(0, 2047, 0, 0, Density::SD, 32, 137, 0);   // startSector = 0
```

Note `startSector = 0`: the DCDD numbers sectors **from zero**, where most soft-sector controllers number **from one**.

**Geometry probing belongs to THIS BOARD, not to the `DiskImage` service.** Image size alone is not enough — 337,568 bytes means a 77-track 8″ floppy *because it is a DCDD*, and the same byte count on another controller means something else. Only the board knows which formats are even candidates. So the board probes size, picks among *its* known formats (8″, minidisk, 8 MB FDC+), and declares the result. The service does offsets and I/O and nothing else.

The slot-internal offset math above (payload at 7 on a data track, 3 on a system track) also stays in the board — that is the controller's business.

## Sector slot layout (the raw 137 bytes)

**System tracks** — `(track & 0x7F) < 6` (`DATATRK`), `SSECLEN` = 133 used:
```
[0]        track | 0x80          (sync bit)
[1]        0x00
[2]        0x01
[3..130]   128-byte payload      DATA_OFF_SYS = 3
[131]      0xFF stop byte
[132]      checksum = sum(payload) & 0xFF
[133..136] 0x00 padding
```

**Data tracks** — `(track & 0x7F) >= 6`, `DSECLEN` = 136 used:
```
[0]        track | 0x80
[1]        Altair-skewed sector, 0-indexed
[2..3]     0x00 0x00
[4]        checksum = sum(payload) & 0xFF
[5..6]     0x00 0x00
[7..134]   128-byte payload      DATA_OFF_DATA = 7
[135]      0xFF stop byte
[136]      0x00
```

The 137th byte is the **track-buffer status byte** (0x00 = good, 0xFF = undefined), which is what makes the raw slot 137 rather than 136.

> **Quirk:** the BIOS does `ANI 7Fh; CPI DATATRK`, so the system/data distinction **wraps every 128 tracks**. On the 8 MB drive, system-format sectors recur every 128 tracks. Real, and easy to miss.

## Two independent sector skews — this trips everyone up

1. **Altair *hardware* skew** (`altSkew`), applied **first**: system tracks 0–5 none; data tracks 6+ — **odd sectors XOR 0x10**, even unchanged. Equivalent to `(sec * 17) mod 32`.
2. **BIOS *software* skew** (`tranTbl`, used by CP/M SECTRAN), logical 0–31 → physical 1–32:
   ```
   01 09 17 25 03 11 19 27 05 13 21 29 07 15 23 31
   02 10 18 26 04 12 20 28 06 14 22 30 08 16 24 32
   ```

They are **separate and both apply.** Document this loudly.

## CP/M DPB (8 MB drive)

`SPT=32, BSH=5 (4096-byte blocks), BLM=31, EXM=1, DSM=2045, DRM=511, AL0=0xF0, AL1=0x00, CKS=0 (non-removable), OFF=2 (RESTRK)`. 512 directory entries; ALV = 257 bytes/drive. EXM=1 with 16-bit block pointers → 8 pointers per dir entry, one extent = 256 records.

## How it is simulated

- Decodes `IoIn`/`IoOut` on 0x08–0x0A.
- Media via **`DiskImage`** (`MOUNT fdc:0 cpm.dsk`), one per drive unit, up to `drives` (property, 1–16). The board declares the format; see Geometry above.
- Rotation via **`EventQueue`**, not a per-instruction poll.
- `interrupt` property exists but the real controller's interrupt bits were ignored by period software and by `mits_dsk.c`; model the bits, don't wire them by default.

**Reset:**
- `Reset::PowerOn` and `Reset::Bus` both: deselect all drives, unload the head, invalidate the sector/byte counters.
- **Both keep images mounted**, and **neither seeks to track 0** — real drives don't.

## Quirks reproduced (and what breaks if you don't)

| Quirk | If you get it wrong |
|---|---|
| Status bits are **inverted** on read | Nothing works, immediately and confusingly. |
| The sector comes from the **clock**, and reading 0x09 does not advance it | Reading a port must never turn the disk. Bump a counter here and the platter spins at the speed of whatever loop is polling it, and a replay stops reproducing. (**This row used to say the opposite** — see the rotation section.) |
| Sector True is **low** when positioned, and is a **30 µs one-shot** — not the gap | Too narrow and software never sees it. **Too WIDE and everything works, which is worse** — you have built a card that forgives races the real one punished, and nothing will ever tell you. |
| The byte clock is **250 kbit/s (64 T @ 2 MHz)**, not the CPU's | Overclock the CPU and the disk reads faster, which is nonsense. `BOOT.ASM`'s transfer loop is cycle-counted against the real rate and breaks if you change it. |
| A **short write is padded with the LAST BYTE**, not with zeros | *"Write circuit will continue writing last byte outputted... to the end of that sector."* The trailing `00` software writes is not a terminator — **it is the fill pattern**, which is why a 133-byte system sector ends in zeros on real media. Get this backwards and it only shows on a disk written by something that did not write the zero. |
| Status **bit 4 reads 0** when the card is enabled | *"D4 – Not Used, = Ø."* An unused bit that reads 1 is a bit you have inverted by accident. |
| Write sequence consumes **exactly 137 bytes** | Partial sectors, corrupted disks. |
| Any step **invalidates** sector/byte position | Stale positions cause writes to the wrong place. |
| Dirty-buffer flush ordering: `out08` (select), `in09` (sector), `out09` (step) **all flush first**, in that order relative to invalidating position | **Silently corrupts disks.** |
| Partial-sector writes (133-byte system sectors that never reach 137) must not be lost | System tracks corrupt on write. |
| `(track & 0x7F)` system/data wrap every 128 tracks | 8 MB images read garbage past track 127. |
| Delay physical drive select until READ/WRITE — **don't seek on select alone** | Spurious seeks. |

## Limitations

- Interrupt bits (`cINTEN`/`cINTDIS`/`sINTEN`) are decoded but not wired to `pINT` by default — no period software used them.
- Head-current control (`cHCSON`) is decoded and ignored, as on real hardware from software's point of view.
- **Rotation and the byte clock are modeled; bit cells are not.** Sector position, the sector-true
  gap and the 250 kbit/s byte rate all derive from the clock (above), so software that *times* the
  disk sees the right answers. What is not modeled is anything below the byte: no write splice, no
  MFM/FM cell encoding, no CRC on the wire. A sector is transferred as bytes, and the 137-byte slot
  in the image is the whole of the medium. Nothing period looks below that line — the checksum the
  BIOS verifies is a byte *inside* the slot, and it is really there.
- **The head-load and step delays are not modeled**, so `sMOVEOK` and `sHDSTAT` come true at once.
  Period software polls them rather than counting, so it cannot tell — but a formatter that *timed*
  a seek would. If the MITS manual's numbers turn up, they belong here.

## The BIOS track-buffer trap (not a board bug, but it will bite you)

The 8 MB Altair CP/M BIOS does **not** write to the DCDD when CP/M closes a file. BIOS WRITE only copies into an in-memory `trkBuf` (32 × 137 = 4,384 bytes, each slot prefixed with a status byte) and marks it dirty. The actual port-0x0A write happens in `invFlush`, **called from the BIOS CONIN entry** — the BIOS uses console input as its flush trigger.

**Consequence: never flush the disk image right after a CP/M file operation.** The directory update from BDOS Close sits in `trkBuf` until the next BDOS function 1. Run back to the `A0>` prompt first.

## Verification

- Cold-boot CP/M 2.2 from `CPM22-8MB-56K-SIM.DSK` to the `A0>` prompt.
- Run `M80` / `L80` and compare output against a golden log.
- Write a file, return to the prompt, unmount, and verify the image on the host.
