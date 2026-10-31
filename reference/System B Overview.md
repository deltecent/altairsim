# Vector Graphic System B — the machine, its boards and its disk drives

Source: [System B Overview.pdf](#)

Vector Graphic Inc., *Vector System B and Vector System B+S3, Installation, Use, and Maintenance,
Preliminary Manual, Revision A*, January 17, 1980, 21 PDF pages. This manual tells a dealer how to
unpack and start a System B. This file keeps the board list, the drive specification, the boot
checks and the Monitor commands that the checks show. It omits the printer setup, the site advice
and the unpacking steps.

**Not emulated** as a machine. `machines/vector.toml` has the screen and the Monitor.

Citations: `n-m` is a printed page. PDF page 7 is printed page 1-1.

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Processor | Z-80 at 4 MHz (1-2) |
| Memory | 64K Dynamic RAM board, 56K available (1-1, 1-2) |
| Monitor | Extended Systems Monitor on a 12K PROM/RAM board with 1K of scratch RAM (1-2) |
| Console | Flashwriter II and a Mindless Terminal keyboard (1-2, 2-1) |
| I/O | Bitstreamer II (1-2) |
| Disk | Floppy disk controller and two Micropolis 5¼-inch drives (1-1, 1-2) |
| Bus | 18-slot S-100 motherboard (1-2) |

---

## 2. Drive specification (1-3)

| Item | Value |
|---|---|
| Capacity | 315K bytes formatted for each drive. 1232 sectors of 256 bytes on each diskette (1-1) |
| Transfer rate | 250K bits/second |
| Rotational speed | **300 RPM** |
| Average latency | 100 ms |
| Track-to-track | 30 ms |
| Settling time | 10 ms |
| Head load time | 75 ms |
| Motor start time | 1 second |
| Recording | MFM, 5248 BPI |
| Track density | 100 TPI |
| Surfaces | 1 |
| Diskette | 16-sector (2-3) |

The [Micropolis 1115 manual](Floppy%20Drive%20%281115%29.md) gives a track-to-track time of 6 ms and
a settling time of 15 ms. See its ⚠ Q1.

---

## 3. Boot checks (2-2, 2-3)

1. Turn the power on. The screen shows the banner of the Vector Graphic Monitor.
2. Press `N` at `MON>`. The Monitor does a memory test. A good result is `E000 FF C3`.
3. Press `Y` at `MON>`. The Monitor echoes each key. `ESC` ends this mode.
4. Put the system disk in the right-hand drive, which is drive A.
5. Press `B` at `MON>`. The red light of the right-hand drive comes on, and the sign-on message
   shows in 5 seconds or less.

---

## 4. Optional hardware (1-2)

- Two more drives, with the MicroStor module.
- Up to four more consoles, for five in total. Each one runs its own task.
- The Sprint 3 and MP printers.
- A High-resolution Graphics Display Board and a Video Digitizer Board.

---

## 5. Printer lines on the Bitstreamer II (2-4, 2-5)

- Channel C is the serial printer line. A Sprint 3 runs at 1200 baud.
- For a TI 810 at 9600 baud, a wire goes from pin 11 of the RS-232C connector to U26 pin 1. A
  second wire goes from U26 pin 3 to pad 1 of area N, and the trace below pad N-1 is cut. This
  brings the printer busy line to `CTS` of channel C.

---

## Quirks worth carrying

- **⚠ Q1. Where the time-share wiring is.** Step 23 sends the reader to "the instructions which
  come with the Time-Share Multi-User System B conversion package" for more terminals (2-7). That
  package is not in hand. It is the first place to look for any wire that a multi-user or OASIS
  machine needs, such as the one in [Bitstreamer II](Bitstreamer%20II.md) ⚠ Q1.
- **⚠ Q2. Two surfaces.** This manual gives 1 surface and 315K for each drive (1-3). The
  [Dual-Mode Disk Controller](HD-FD%20documentation.md) manual gives 2 sides. `vg31.vgi` has 2 sides
  and `vg32.vgi` has 1. Both kinds were in use.
- **⚠ Q3. A preliminary manual.** The title page says the maintenance section will be added later.
  This scan has none.
