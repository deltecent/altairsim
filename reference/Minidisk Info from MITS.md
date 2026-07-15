# Altair Minidisk System — Technical Information (88-MDS / 680b-MDS)

Source: [Minidisk Info from MITS.pdf](#)

> This is a transcription of the single-page MITS "Technical Information"
> sheet for the Altair Minidisk System. It is a product/technical overview,
> not a full hardware manual — it does **not** contain per-port register or
> bit-level definitions. Where such detail is needed for emulation, consult
> the 88-DCDD / 88-MDS manuals and the existing `mds`/`HardSectorFdc`
> implementation notes. Everything below is stated on the sheet itself; no
> figures have been inferred.

## Overview

The Altair Minidisk System is a mass-storage peripheral for Altair
microcomputers.

- **Storage capacity:** 71,680 formatted bytes per minidiskette.
- **Data transfer rate:** 125,000 bits per second.
- **Access time:** less than three seconds.
- **Model designations:** `88-MDS` (Altair 8800) and `680b-MDS` (Altair 680b).

### System contents

The Minidisk System as shipped includes:

- Two controller boards for the `88-MDS` (one controller board for the
  `680b-MDS`).
- An Interconnect Cable.
- One Minidisk Drive.
- Altair Minidisk BASIC software.
- Additional Minidisk Drives may be purchased separately.

## Drive geometry and timing

| Parameter | Value |
|---|---|
| Formatted capacity | 71,680 bytes per minidiskette |
| Data transfer rate | 125,000 bits/second |
| Access time | < 3 seconds |
| Sectoring | Hard-sectored (see note below) |
| Motor auto-off | Motor turns off after the Minidisk remains unaccessed for 5 seconds |

> Note: This sheet gives the capacity, transfer rate, and access time above,
> but does **not** break down tracks-per-disk, sectors-per-track,
> bytes-per-sector, RPM, or hard-sector index timing. Those numbers must come
> from the detailed controller documentation, not this document.

## System design

The Minidisk System's Disk Drive consists of the Drive itself, an
Interconnect cable, and one blank minidiskette. Inside the Minidisk Drive
cabinet are:

- The Disk Drive.
- Power supply.
- Line buffers and addressing circuitry.

**Drive Address:** switch-selectable, and the selected address is displayed
on the front panel for easy identification.

**WRITE PROTECT:** the Disk Drive offers WRITE PROTECT as a standard feature.

## Minidisk Controller

The Minidisk Controller consists of an Interconnect Cable and two Controller
Boards that plug into the Altair computer bus. It provides interaction
between the computer and the Minidisk Drive.

- **I/O ports:** All control, status, and data input/output for the Minidisk
  System are handled through **three I/O ports** dedicated to the Minidisk
  Controller.
- **Motor timer:** To ensure maximum life of the Drive motor, a timer in the
  Controller turns the system off if the Minidisk remains unaccessed for
  **five seconds**.
- **Hard-sectoring:** The Software Driver for the Minidisk READ/WRITE
  function simplifies Controller design by implementing the hard-sectoring
  technique in software.

## Software

### Minidisk BASIC

The software most commonly used with the Minidisk System is Altair Minidisk
BASIC.

- Resides in the **lower 20K** of memory.
- Provides the disk utilization routines.
- Includes the standard features of BASIC plus additional functions.
- The Software Driver for the Minidisk READ/WRITE function implements the
  hard-sectoring technique (simplifying the Controller design).

## Options and Accessories

| # | Item | Description |
|---|---|---|
| 1 | **Software** | Altair Minidisk BASIC, available on a WRITE-Protected Minidiskette. Virtually identical in operation and features to Altair Disk BASIC. Documentation includes Bootstrap Listings and READ/WRITE Driver Codes. Specify cassette tape or paper tape for the Bootstrap Loader if required. |
| 2 | **MDBL PROM** | Minidisk Bootstrap Loader on a Programmable Read Only Memory IC. Designed for use with the 88-PMC Memory Card or the 680b PROM socket, at the **highest 256-byte block address**. |
| 3 | **DRWT PROM** | Minidisk READ/WRITE Test PROM. Designed for use with the 88-PMC at the **third-highest 256-byte block address**. Contains fundamental diagnostic tests for checking hardware operation. |

## Emulation-relevant summary

- Controller occupies **three consecutive I/O ports** on the bus (this sheet
  does not give the base port address; the 88-MDS uses hard-sectored ports —
  see the `mds` card / `HardSectorFdc` base in the codebase).
- Motor is **opt-in / auto-off**: it spins down after 5 seconds of inactivity
  (matches the "motor is opt-in" note for the `mds` card).
- Hard-sectoring is performed **in the software driver**, not fully in
  hardware — the controller is deliberately simple.
- Drive address is set by a **physical switch** and shown on the drive's front
  panel.
- **WRITE PROTECT** is a per-drive hardware feature.
- Boot path: **MDBL** bootstrap-loader PROM at the top 256-byte block; **DRWT**
  read/write diagnostic PROM at the third-highest 256-byte block.
- Nominal figures: **71,680** formatted bytes/diskette, **125,000** bits/s,
  **< 3 s** access time.
