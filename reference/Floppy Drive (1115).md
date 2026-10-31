# Micropolis Model 1115 — 5¼-inch hard-sector or soft-sector floppy drive

Source: [Floppy Drive (1115).pdf](#)

Micropolis Corporation, *Model 1115 OEM Flexible Disk Drives Maintenance Manual*, document 102076
Rev A1, April 1984, 179 PDF pages, with a text layer. This file keeps the mechanics and the sector
timing that a controller model needs. It omits the service procedures, the alignment steps and the
parts lists. It covers **only** the pages cited here, and the rest of the manual has not been read.

**Not emulated** as a drive. A board model takes the rotation and the sector timing from here.

Citations: `PDF n` is a PDF page. PDF 47 is printed page 3-23.

---

## 1. Specification (Table 1-1, PDF 12)

| Parameter | 1115-5 | 1115-6 | 1115-2 | 1115-4 |
|---|---|---|---|---|
| Heads for each drive | 1 | 2 | 1 | 2 |
| Tracks for each surface | 80 | 80 | **77** | **77** |
| Track density (TPI) | 96 | 96 | **100** | **100** |
| At speed after MOTOR ON (ms) | 200 | 200 | 200 | 200 |
| Spindle speed (RPM) | **300** | **300** | **300** | **300** |
| Speed variation (%) | ±1.5 | ±1.5 | ±1.5 | ±1.5 |
| MFM data density (BPI) | 5577 | 5921 | 5248 | 5552 |
| MFM transfer rate (Kbits/sec) | 250 | 250 | 250 | 250 |
| Unformatted KBytes for each track | 6.250 | 6.250 | 6.250 | 6.250 |
| Unformatted KBytes for each diskette | 500 | 1000 | 480 | 960 |
| Average latency (ms) | 100 | 100 | 100 | 100 |
| Head load time (ms), with the option | 75 | | 75 | |
| Track to track (ms) | 6 | 6 | 6 | 6 |
| Settling time (ms) | 15 | 15 | 15 | 15 |
| Average access (ms) | 173 | 173 | 167 | 167 |

Average access = ((tracks for each side − 1) / 3 × 6 ms) + 15 ms.

The 1115-2 and 1115-4 have the 77 tracks of a `.vgi` image.

---

## 2. Sector and index timing (§3.3.2.3, Figure 3-5, PDF 47)

| Item | Value |
|---|---|
| Sector and index pulse, `SECP/` | True for 4 ms (±1) at each hole |
| Time reference | The leading edge of the pulse |
| Sector hole to sector hole, 16 sectors at 300 RPM | 12.5 ms (±0.65) |
| Sector hole to index hole, and index hole to sector hole | 6.25 ms (±0.325) |
| Valid | Only when the diskette is at speed |

In Figure 3-5 the index hole is in the middle of sector 15. The next sector hole starts sector 0.

## 3. Step timing (§3.3.2.4, PDF 47)

| Period of the step pulses | Mode |
|---|---|
| 6 ms or more | Conventional |
| 3 to 5 ms | Burst |
| Between 5 and 6 ms | Not valid |

---

## Quirks worth carrying

- **⚠ Q1. A later manual.** This is the 1984 manual, for a drive sold after the System B. The
  [System B Overview](System%20B%20Overview.md) gives the same 300 RPM, 100 TPI and 5248 BPI, but a
  track-to-track time of 30 ms and a settling time of 10 ms. The 30 ms agrees with the step delay
  that the [Dual-Mode Disk Controller](HD-FD%20documentation.md) driver uses (⚠ Q8 there). This file
  does not settle which drive a Dual-Mode machine had.
- **⚠ Q2. The index hole agrees with the Dual-Mode counter.** The index hole comes 6.25 ms after the
  sector hole of sector 15. The one-shot in [Dual-Mode Disk Controller](HD-FD%20documentation.md) §4
  lasts about 9.5 ms, so it sees the index hole as a second hole inside that time and loads 15. The
  next hole counts to 0, which is sector 0 in Figure 3-5.
