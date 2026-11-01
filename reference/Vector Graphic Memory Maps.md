# Vector Graphic Memory Maps — the address space of a Vector Graphic machine

Source: [Memory Map.jpg](#) and [System RAM Map.jpg](#)

Two undated Vector Graphic single sheets. *Memory Map* draws the 64K address space of one machine,
board by board. *System RAM* lists which program uses each range of high RAM. The System RAM sheet
names Monitors 4.1, 4.2 and Timeshare Monitor 4.4. The
[PROM Reference](Vector%20Graphic%20PROM%20Reference.md) of December 1980 stops at 4.1, so the System
RAM sheet is likely later.

**Reference only.** `machines/vector.toml` has the same addresses, with a stand-in for the ZCB.

---

## 1. Quick reference for emulation

| Range | Holds |
|---|---|
| 0000–DFFF | User RAM, 56K |
| E000–EFFF | ROM on the [ZCB](Vector%20Graphic%20ZCB.md): Monitor, disk boot, printer driver |
| F000–F7FF | Flashwriter II video RAM |
| F800–FBFF | Disk controller |
| FC00–FFFF | RAM on the ZCB: printer data, Monitor stack and variables |

---

## 2. Memory Map sheet

| Range | Type | Size | Board | Use |
|---|---|---|---|---|
| 0000–DFFF | RAM | 56K | 64K Dynamic Memory | User available memory |
| E000–E7FF | ROM | 2K | ZCB | Monitor |
| E800–EBFF | ROM | 1K | ZCB | Disk PROM |
| EC00–EFFF | ROM | 1K | ZCB | Printer driver (optional) |
| F000–F7FF | RAM | 2K | Flashwriter II | Video display. The top of the range is "useable area", "not shown on video" |
| F800–F8FF | ROM | .25K | Micropolis disk controller | Disk controller boot strap |
| F900–FBFF | RAM | .75K | Micropolis disk controller | The sheet shades it and gives no use. See §3 |
| FC00–FFFF | RAM | 1K | ZCB | Stack area |

The sizes add up to 64K.

**The MITS boxes.** At the right edge the sheet prints *MITS Turnkey RAM* beside F800–FBFF and *MITS
Turnkey PROM* beside FC00–FFFF. A hand added the octal addresses of that PROM, and they convert
correctly:

| Hex | Octal | Note in the same hand |
|---|---|---|
| FC00 | 176000 | |
| FD00 | 176400 | Monitor |
| FE00 | 177000 | |
| FF00 | 177400 | M.S.FD. |

See [MITS Turn Key Board](MITS%20Turn%20Key%20Board.md).

---

## 3. System RAM sheet

| Range | Used by | For |
|---|---|---|
| F000–F7FF | All systems | Video memory |
| F800–FBFF | Micropolis | Disk controller |
| FC00–FC9A | MP Dot Matrix Printer, version 1.6 | Buffer and variable area |
| FC9B–FCAF | Open area | 21 bytes |
| FCB0–FCFF | Sprint 3 Printer, version 2.1 | Variable area |
| FD00–FF14 | Timeshare Monitor, version 4.4 | Stack and variable area |
| FF15–FF47 | Open area | 51 bytes |
| FF48–FF4B | 5¼" floppy drivers | Track tables |
| FF4C–FF53 | Dualstor floppy drivers | Track tables |
| FF54–FF5C | Dualmode controller | Track tables |
| FF54–FFCF | Vector Monitor, versions 4.1 and 4.2 | Stack area, 124 bytes, 62 levels |
| FFD0–FFDA | Reserved for Monitors | 11 bytes |
| FFDB–FFE0 | Timeshare and Vector Monitors, versions 4.1 to 4.4 | Cursor variables |
| FFE1–FFEA | Vector Monitor, versions 4.1 and 4.2 | Breakpoint variables |
| FFEB–FFFF | Reserved for Monitors | 21 bytes |

Each stated byte count agrees with its range.

---

## 4. Checked against other sources

- **Monitor 4.3 keeps the map.** `VGMON43.BIN` loads `SP` with FFD0 at E015 and E04C, the top of the
  stack area. A byte search of the image finds references to FFDB–FFDF, to FFE1–FFEA and to FF4C. A
  byte search is not a disassembly, so a single hit proves little. Several hits at each of these
  ranges fit the sheet.
- **The image is 4K at E000.** The Memory Map draws three PROMs at E000, E800 and EC00. Monitor 4.3
  fills all of E000–EFFF, and its boot code is at EB11, inside the Disk PROM range. This fits the one
  2732 at U20, "monitor-boot-printer", of the later ZCB boards
  ([Vector Graphic ZCB](Vector%20Graphic%20ZCB.md) §2).
- **The Dual-Mode manual agrees.** `TRACKADDR` is FF54 in the sample code of
  [HD-FD documentation](HD-FD%20documentation.md), the start of the Dualmode track tables here. The
  `BUSY` byte of that code is FF10, which is inside the Timeshare Monitor range here.

---

## Quirks worth carrying

- **⚠ F781.** The Memory Map prints the address F781 at the start of the useable area. The screen is
  80 × 24, which is 1920 bytes or 780H, so the first byte not on the screen is F780. The arithmetic
  wins: the screen is F000–F77F, as in the [Flashwriter II Manual](Flashwriter%20II%20Manual.md).
- **⚠ Two uses of FF54–FF5C.** The sheet gives this range to the Dualmode track tables and also to
  the Monitor stack. The stack grows down from FFD0, so the two meet only at its last levels. The
  sheet does not say which one wins.
- **⚠ The map is one configuration.** It shows a Micropolis controller at F800. A machine with the
  Dual-Mode controller has ports at C0–C3 and no memory there.
