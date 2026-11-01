# Vector Graphic VIP — the board set of a Vector 3

Source: [Vector 3 (VIP) Brochure.pdf](#)

Vector Graphic *VIP* sales brochure, © 1980, printed 9-80, 4 PDF pages. The photograph on the second
page shows a console with the label `VECTOR 3`. The brochure lists the boards and the software of the
entry machine. It gives no port, no address and no jumper.

**Not emulated** as a machine. `machines/vector.toml` has these boards, except that a generic Z-80
and a stand-in memory board take the place of the ZCB.

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Processor | Z-80 at 4 MHz, on the Vector single board computer |
| Board memory | 1K of RAM, and up to 12K of PROM in three slots |
| Board I/O | Programmable parallel ports and one serial port |
| RAM | 64K Dynamic RAM board with bank select, 56K available |
| Video | Flashwriter II, memory mapped |
| Disk | A floppy disk drive controller board, and one quad-density 5¼-inch drive of 315,000 characters |
| Bus | S-100, 6-slot motherboard |
| Monitor | Extended Systems Monitor in PROM |
| Operating system | CP/M 2 |
| Price | $3,695, suggested retail |

---

## 2. What the brochure lists

**Standard.** A video console with a keyboard and a 10-key number pad. A printer interface. "Dumb"
terminal emulation at 30 characters each second, for a direct line or a modem. Microsoft BASIC-80
release 5, SCOPE, RAID and the ZSM Z-80 assembler.

**Optional.** Up to 3 more 5¼-inch floppy drives. The Sprint 3 or the MP matrix printer, on a parallel
interface. Memorite word processing, CCA data management and Peachtree accounting. Peachtree needs a
second drive.

The brochure says that the only difference between the VIP and the larger Vector systems is the disk
drive.

---

## Quirks worth carrying

- **⚠ The brochure does not name the controller.** It says "floppy disk drive controller board". The
  [CP/M sheet](Vector%20Graphic%20CP-M%20Versions.md) of 10/16/81 puts the VIP on the Micropolis
  controller (version A), and the VIP-T on the Dualmode controller (version H). The deramp.com Vector
  Graphic page, a restorer's notes, shows a Vector 3 in the VIP-T configuration with a ZCB, the 64K
  DRAM, the Flashwriter II and the floppy and hard disk controller, and two Tandon drives.
- **⚠ No Bitstreamer II in the list.** The serial and parallel ports of this machine are on the
  [ZCB](Vector%20Graphic%20ZCB.md).
