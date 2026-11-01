# Vector Graphic Microcomputer Hardware 1979 — the board set of the Vector MZ

Source: [Vector_Graphic_Microcomputer_Hardware_1979.pdf](#)

Vector Graphic product brochure, 1979, 6 PDF pages, no text layer. The pages were read as images.
Only the machine and board entries that affect emulation are kept. The brochure gives no port and
no address.

**Emulated** as the `vgmz` machine, with a generic Z-80 and a stand-in memory board.

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Bus | 18-slot S-100 motherboard |
| Processor | Z-80 CPU board with a 4 MHz clock |
| Memory | 48K RAM, which can grow to 56K |
| Monitor | 12K 2708 PROM/RAM board with an extended Monitor |
| I/O | Bitstreamer I/O board, 1 serial and 2 parallel ports |
| Disk | Disk controller board and two quad-density Micropolis 5¼-inch drives, 630K in total |
| Software | Micropolis MDOS, MZOS and CP/M |

---

## 2. System B

A footnote on the page of the MZ says that a small business system called System B has a Mindless
Terminal, a Vector MZ and a Flashwriter II.

---

## Quirks worth carrying

- **⚠ The System B manual is different.** [System B Overview](System%20B%20Overview.md), January 1980,
  gives 64K RAM with 56K used, and a Bitstreamer II. The `vg3sb` machine follows that manual.
- **⚠ No console on the MZ.** The list has no video board. The Mindless Terminal needs a video board
  to drive it, so `vgmz` uses the serial port.
