# 88-DCDD — MITS Disk Controller (8" Pertec FD-400)

**Status:** not implemented (milestone 3). Register map recovered and cross-checked.

## The real hardware

The MITS 88-DCDD is the Altair's 8" floppy controller, driving up to 16 daisy-chained Pertec FD-400 drives (4 is typical). It is an unusually *raw* controller: it does not read or write sectors for you. Software steps the head, watches the sector counter go by, and shifts bytes one at a time through a data port in real time. Nearly all the disk logic lives in the BIOS.

## Sources

| Source | Path | Authority |
|---|---|---|
| `mits_dsk.c` | `../AltairClaude/reference/mits_dsk.c` | **Authoritative.** Patrick Linstruth's own SIMH module (2025), from Owen (1997) / Schorn (2002-23), minidisk by Mike Douglas. Its header comment is a cleaner spec than the MITS manual. |
| `BIOS.ASM` | `../AltairClaude/reference/BIOS.ASM` | Mike Douglas 48K/8Mb Altair CP/M 2.2b BIOS — authoritative equates. |
| `boot.asm`, `dbl.prn` | `../AltairClaude/reference/` | 2nd-stage loader; DBL 4.1 boot PROM disassembled by Martin Eberhard. |
| `CLAUDE.md` | `../AltairClaude/CLAUDE.md` | 628 lines, binary-verified. Disk geometry, skew, DPB/DPH. |

> **Unresolved:** the Python prototype and `BIOS.ASM` disagree on the positions of the `I` (interrupts-enabled) and `Z` (track 0) status bits. **Reconcile against `mits_dsk.c`.** The table below follows `BIOS.ASM`/`mits_dsk.c`.

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

**The rotation model:** `sector_true` toggles on each read, and the sector counter advances (mod 32) only on **every second read**. That is the entire rotating-disk simulation. Subtle, and load-bearing.

### Write sequence
1. Step to the desired track.
2. Poll `IN 0x09` until the desired sector number appears with T = 0.
3. `OUT 0x09, 0x80` (`cWRTEN`).
4. The `W` bit in the 0x08 status goes active; the controller consumes **exactly 137 bytes** written to port 0x0A.
5. `W` drops; the sector is committed.

Read side reads 137 bytes from port 0x0A.

## Geometry

| | |
|---|---|
| Bytes per sector slot | **137** (`DSK_SECTSIZE` / `TSECLEN`) |
| Sectors per track | **32** (16 on minidisk) |
| Tracks | **77** (standard 8"); **2048** on the 8 MB FDC+ serial drive |
| Full 8" image | 77 × 32 × 137 = **337,568 bytes** |
| 8 MB FDC+ image | 2048 × 32 × 137 = **8,978,432 bytes** |
| Byte offset | `137 * sectors_per_track * track + 137 * sector` |

**Minidisk is auto-detected purely by image size** (`MINI_DISK_SIZE ± MINI_DISK_DELTA` → 16 sectors/track, else 32) and **ignores head-unload**.

Geometry probing belongs in the `BlockDevice` service (once), not in this board. The offset math above stays in the board — that is the controller's business.

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
- Media via **`BlockDevice`** (`MOUNT fdc:0 cpm.dsk`), one per drive unit, up to `drives` (property, 1–16).
- Rotation via **`EventQueue`**, not a per-instruction poll.
- `interrupt` property exists but the real controller's interrupt bits were ignored by period software and by `mits_dsk.c`; model the bits, don't wire them by default.

**Reset:**
- `Reset::PowerOn` and `Reset::Bus` both: deselect all drives, unload the head, invalidate the sector/byte counters.
- **Both keep images mounted**, and **neither seeks to track 0** — real drives don't.

## Quirks reproduced (and what breaks if you don't)

| Quirk | If you get it wrong |
|---|---|
| Status bits are **inverted** on read | Nothing works, immediately and confusingly. |
| Sector advances every **second** read of 0x09 | Software spins forever waiting for a sector that never arrives — or the disk appears to spin at double speed. |
| Sector True is **low** when positioned | Reads/writes land on the wrong sector. |
| Write sequence consumes **exactly 137 bytes** | Partial sectors, corrupted disks. |
| Any step **invalidates** sector/byte position | Stale positions cause writes to the wrong place. |
| Dirty-buffer flush ordering: `out08` (select), `in09` (sector), `out09` (step) **all flush first**, in that order relative to invalidating position | **Silently corrupts disks.** |
| Partial-sector writes (133-byte system sectors that never reach 137) must not be lost | System tracks corrupt on write. |
| `(track & 0x7F)` system/data wrap every 128 tracks | 8 MB images read garbage past track 127. |
| Delay physical drive select until READ/WRITE — **don't seek on select alone** | Spurious seeks. |

## Limitations

- Interrupt bits (`cINTEN`/`cINTDIS`/`sINTEN`) are decoded but not wired to `pINT` by default — no period software used them.
- Head-current control (`cHCSON`) is decoded and ignored, as on real hardware from software's point of view.
- No modeling of index pulses, write splice, or actual bit-cell timing. Sector timing is the two-reads-per-sector model above. Software that measures rotation with a real-time clock would notice; nothing period does.

## The BIOS track-buffer trap (not a board bug, but it will bite you)

The 8 MB Altair CP/M BIOS does **not** write to the DCDD when CP/M closes a file. BIOS WRITE only copies into an in-memory `trkBuf` (32 × 137 = 4,384 bytes, each slot prefixed with a status byte) and marks it dirty. The actual port-0x0A write happens in `invFlush`, **called from the BIOS CONIN entry** — the BIOS uses console input as its flush trigger.

**Consequence: never flush the disk image right after a CP/M file operation.** The directory update from BDOS Close sits in `trkBuf` until the next BDOS function 1. Run back to the `A0>` prompt first.

## Verification

- Cold-boot CP/M 2.2 from `CPM22-8MB-56K-SIM.DSK` to the `A0>` prompt.
- Run `M80` / `L80` and compare output against a golden log.
- Write a file, return to the prompt, dismount, and verify the image on the host.
