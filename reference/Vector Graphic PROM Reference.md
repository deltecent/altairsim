# Vector Graphic PROM Reference — every factory PROM, with its checksum

Source: [PROM Reference Dec80.pdf](#)

Vector Graphic *PROM Reference*, Rev 12/01/80, 3 PDF pages. The sheet lists each factory PROM with
its part number, chip, checksum and function. The text layer is OCR, so each value here was read
from the page image.

**Reference only.** The checksums can identify a PROM dump. A note in a person's hand above the
checksum column says "most of ROMs", so do not expect every row to match.

---

## 1. Quick reference for emulation

- The `Q` command of the Monitor computes a checksum
  ([Vector Graphic PROM-RAM III](Vector%20Graphic%20PROM-RAM%20III.md) §6).
  This sheet does not define the checksum. `ROM Checksums.pdf`, a later sheet, gives the low byte of
  the sum (`docs/sources.md`).
- Two PROMs of one set have two checksums, `#0` and `#1`.
- **Monitor 4.3 is not on this sheet.** The sheet ends at 4.1. The listing of 4.3 is dated 1/12/81,
  which is after this sheet.

---

## 2. Monitors and boot PROMs

| Part number | Name | Chip | Checksum | Function |
|---|---|---|---|---|
| ????-1520 00 30 | MZOS 1.52 | 2708 | 87 | 48K MZOS boot |
| 6081-3000 00 30 | 2800/3030 Boot #3 | 2708 | 1A | Disk boot for 2800/3030 |
| 6020-2100 00 30 | 2.1C | 2708 | 50 | 48K monitor, serial terminal |
| 6203-2100 00 30 | 2.1CV | 2708 | 26 | 48K monitor with Diablo keyboard, serial terminal |
| 6200-3100 00 30 | 3.1 EVII | 2708 | 70 | 48K monitor, standard |
| 6202-4000 00 30 | 4.0C | 2708, 2 per set | #0: 36, #1: 42 | 56K monitor, serial terminal |
| 6202-4000 00 50 | 4.0C | 2716 | 78 | 56K monitor, serial terminal |
| 6200-4000 00 30 | 4.0 (EVII) | 2708, 2 per set | #0: A6, #1: 52 | 56K monitor, standard |
| 6200-4000 00 50 | 4.0 (EVII) | 2716 | F8 | 56K monitor, standard |
| 6200-4000 00 60 | 4.0 (EVII) | ROM | F8 | 56K monitor, standard |
| 6200-4100 00 50 | 4.1 (EVII) | 2716 | 16 | 56K monitor for 3030, 2800, VIP's |
| 6200-4003 00 30 | 4.0 (EVII) Swedish | 2708, 2 per set | #0: CB, #1: 52 | 56K monitor, Swedish |
| 6205-3300 00 30 | VTS 3.3 | 2708, 2 per set | #0: B6, #1: 6B | Timeshare monitor, standard |
| 6205-3300 00 50 | VTS 3.3 | 2716 | 21 | Timeshare monitor, standard |
| 6205-3303 00 30 | VTS 3.3 Swedish | 2708, 2 per set | #0: C3, #1: 96 | Timeshare monitor, Swedish |
| 6205-3302 00 30 | VTS 3.3 German | 2708, 2 per set | #0: B6, #1: 00 | Timeshare monitor, German |
| 6220-3000 00 30 | Univid III 48K | 2708 | 8D | Emulates serial terminal |
| 6??1-3000 00 30 | Univid III 56K | 2708 | BF | Emulates serial terminal |

---

## 3. Graphics and printer drivers

| Part number | Name | Chip | Checksum | Function |
|---|---|---|---|---|
| 6230-1000 00 30 | GRAPHIT 48K | 2708, 2 per set | FC, C©: EF | Processes commands for high-res board |
| 6231-1000 00 30 | GRAPHIT 56K | 2708, 2 per set | 16, C©: EF | Processes commands for high-res board |
| 6???-1300 00 30 | MPD I/O 1 1.3 | 2708, 2 per set | D: 80, G: 8A | Dot matrix driver, I/O 1 |
| 6241-1400 00 30 | MPD I/O 2 1.4 | 2708, 2 per set | D: B4, G: B6 | Dot matrix driver, I/O 2, 80 column |
| 6241-1600 00 30 | MPD I/O 2 1.6 | 2708 | 98 | Dot matrix driver, I/O 2, 132 column (graphics on disk) |
| 6242-1400 00 30 | MPD-Z 1.4 | 2708 | F2 | Dot matrix driver for ZCB, 80 column (graphics on disk) |
| 6242-1600 00 30 | MPD-Z 1.6 | 2708 | CA | Dot matrix driver for ZCB, 132 column (graphics on disk) |
| 6244-1300 00 30 | S3D 1.3 | 2708 | 6C | Qume Sprint-3 driver for I/O II, no TOF button |
| 6245-1300 00 30 | S3D-Z 1.3 | 2708 | 0E | Qume Sprint-3 driver for ZCB, no TOF button |
| 6245-2000 00 30 | S3D 2.0-I/O II | 2708 | 80 | Qume Sprint-3 driver for I/O II, MEM III |
| 6245-2000 00 30 | S3D 2.0-Z | 2708 | E3 | Qume Sprint-3 driver for ZCB, MEM III |
| 6245-2100 00 30 | S3D 2.1 | 2708 | 4C | Qume Sprint-3 driver for ZCB and I/O II, MEM III and up |
| 6680-2000 00 32 | Config D1 | 2708 | B1 | 48K Qume 5 driver at port 6,7 |
| 6???-1000 00 33 | Config F1 | 2708 | F9 | 48K Qume 5/Diablo at port 6,7 |

---

## 4. Flashwriter II character generators

The first digit of the `6300`-series numbers is at the margin of the scan, and on some rows it reads
as 5. The digit is 6 on the rows where the margin is clear.

| Part number | Name | Chip | Checksum | Function |
|---|---|---|---|---|
| 6300-1000 00 30 | FW2 (PLOT) | 2708, 2 per set | ©36, 33 | Plot character generator for FW2 |
| 6301-1000 00 30 | FW2 C (CNTL) | 2708, 2 per set | ©EF, FB | Control character generator for FW2, used with MEM II |
| 6300-1000 00 60 | FW2 (PLOT) | ROM 8316E181 | 69 | Plot character generator |
| 6301-1000 00 60 | FW2 C (CNTL) | ROM 8316E182 | EA | Control character generator, used with MEM II |
| 6302-1001 00 30 | FW2 (CNTL) French | 2708, 2 per set | ©3C, 17 | Control, French |
| 6300-1003 00 30 | FW2 (PLOT) Swedish | 2708, 2 per set | ©EE, 2B | Plot, Swedish |
| 6301-1001 00 30 | FW2 (CNTL) Swedish | 2708, 2 per set | ©B8, 23 | Control, Swedish |
| 6300-1002 00 30 | FW2 (PLOT) German | 2708, 2 per set | ©DC, 27 | Plot, German |
| 6???-1002 00 30 | FW2 (CNTL) German | 2708, 2 per set | ©9B, 07 | Control, German |
| 6300-1004 00 30 | FW2 (PLOT) Spanish | 2708, 2 per set | ©C4, 86 | Plot, Spanish |
| 6300-1005 00 30 | FW2 (PLOT) Norwegian | 2708, 2 per set | ©63, 2A | Plot, Norwegian |
| 6301-1005 00 30 | FW2 (CNTL) Norwegian | 2708, 2 per set | ©EA, 0A | Control, Norwegian |
| 6300-1006 00 30 | FW2 (PLOT) Greek | 2708, 2 per set | ©66, BF | Plot, Greek |
| 6300-1007 00 30 | FW2 (PLOT) Katakana | 2708, 2 per set | ©60, 10 | Plot, Japan |
| 6302-1000 00 50 | APL character set | 1 2708 and 1 2716 | APL1 (2716): 70, APL2 (2708): 87 | Goes with APL disk |
| 6303-1000 00 50 | FW2 plot gen 50 Hz | | F3 | Plot, 50 Hz |
| 6303-1003 00 50 | FW2 plot 50 Hz Swedish | | none printed | Swedish plot, 50 Hz |

---

## Quirks worth carrying

- **⚠ Marks the scan does not explain.** `©` stands before the first checksum of many sets, and the
  sheet does not say what it means. It also does not say what `D:`, `G:` and `C©:` mean.
- **⚠ Unreadable digits.** A `?` here marks a digit that the scan lost. The sheet gives no other
  source for those part numbers.
- **⚠ One part number twice.** 6245-2000 00 30 is printed for both S3D 2.0-I/O II and S3D 2.0-Z, with
  different checksums.
- **⚠ VTS 3.3 and Timeshare Monitor 4.4.** This sheet lists VTS 3.3, and the
  [System RAM sheet](Vector%20Graphic%20Memory%20Maps.md) names Timeshare Monitor 4.4. The sheets do
  not link the two.
