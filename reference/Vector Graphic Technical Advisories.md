# Vector Graphic Technical Advisories — dealer bulletins, 1979 to 1981

Source: [Technical Advisorys.pdf](#)

Vector Graphic Inc., hardware bulletins and Vector Technical Advisories (VTAs) to dealers, January
1979 to January 1981, 26 PDF pages. This file lists each bulletin and keeps what affects the
behavior of a board. The bulletins about power supplies and motors change nothing that a guest can
see, so this file names them and no more.

**Not emulated.** No bulletin here changes a port or a register.

Citations: `PDF n` is a PDF page. The Flashwriter II bulletins are in a separate scan and are
distilled in the [Flashwriter II Manual](Flashwriter%20II%20Manual.md).

---

## 1. List of bulletins

| Bulletin | Date | For | Topic | PDF |
|---|---|---|---|---|
| Hardware #80-1 | Jan 15, 1979 ⚠ | System B | Power supply upgrade, Sprint 3 option | 1–2 |
| Hardware #80-2 | Jan 16, 1980 | System B, MZ | Z80 clock circuit failure | 3 |
| Hardware #80-6 | Apr 4, 1980 | System B, MZ | Centronics printer | 4–5 |
| Hardware #80-3 | Feb 20, 1980 | Flashwriter II Rev 3 | APL character set | 6–8 |
| VTA #80-0 | Sep 1, 1980 | All | The VTA form | 9 |
| VTA #80-1 | Sep 1, 1980 | All | 64K memory board | 10 |
| VTA 80-05 | Oct 13, 1980 | System 2800, 230 V | Japan Servo drive motors | 11 |
| VTA 80-06 | Oct 23, 1980 | System 2800, Dualstore | 8-inch controller Rev 0, Qume head gap | 12 |
| VTA 80-7 | Dec 1, 1980 | All | ZCB PROM removal | 13 |
| VTA 80-8 | Dec 1, 1980 | System 3030 | Micropolis 1200 hard disk bulletins | 14–18 |
| VTA 80-9 | Dec 5, 1980 | Vector 3 | VG-4G transformer | 19–20 |
| VTA 80-10 | Dec 16, 1980 | All | DC power plug wiring | 21–22 |
| VTA 81-1 | Jan 15, 1981 | All | Rev 3 ZCB | 23–24 |
| VTA 81-2 | Jan 15, 1981 | Vector 3 | Wells-Gardner CRT | 25–26 |

---

## 2. What affects a board

**Z80 clock failure (#80-2).** A bad crystal on the Z80 board shows random characters on the screen
at power on, with no keyboard input. Replace a crystal with its data stamped in ink.

**Centronics wire list (#80-6).** The bulletin connects a printer to `I/O 2` J4 or J5. These pins
agree with the [Bitstreamer II](Bitstreamer%20II.md) connector table.

| J4, J5 pin | Signal |
|---|---|
| 2–8 | Data 0–6 out |
| 1 | Strobe out |
| 11 | Busy in |
| 20 | Ground |

**APL character set (#80-3).** Two switches and new PROMs at U22 and U23 select an APL character set
on a Flashwriter II Rev 3. Instructions differ for TMS 2716 and for Intel 2716 or TMS 2516 parts.

**64K memory (VTA #80-1).** A failed 4108 RAM in the last 8K can stop a 56K CP/M disk from booting.
Memory tests, the `worm` test too, do not find this fault.

**8-inch controller (VTA 80-06).** Rev 0 boards need a change at U30 and U29 to become Rev 1. A
technician removes the 470 pF capacitor between U30-8 and U30-9.

**Rev 3 ZCB (VTA 81-1).** Rev 3 delays `RD` to stop a data glitch. Sheet 2 of the ZCB schematic,
Rev 3, dated 7/16/80, is at PDF 24. Sheet 2 shows `MD0`–`MD7` and the 8T28 and 8T13 bus buffers.

---

## Quirks worth carrying

- **⚠ Q1. A date with the wrong year.** Bulletin #80-1 is dated January 15, 1979, and calls itself
  the first of 1980. The year must be 1980.
- **⚠ Q2. Gaps in the numbers.** VTA numbers 80-2 to 80-4 and hardware bulletins #80-4 and #80-5 are
  not in this scan.
- **⚠ Q3. Nothing about OASIS.** No bulletin here names OASIS, a clock wire or port 28.
