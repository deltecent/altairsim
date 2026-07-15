# FDC+ Enhanced Floppy Disk Controller for the Altair 8800 — User's Manual

Source: [FDC+ Manual.pdf](#)

> Manual version 1/26/26 v2.0. Board revisions B and C are covered; where they
> differ, the difference is called out. This file distills the parts relevant to
> emulating the controller in software; cabling part numbers, mechanical mounting
> and marketing text are omitted.

---

## 1. Overview

The Altair FDC+ (FarmTek, PC038) is a single-board, 100% compatible drop-in
replacement for the original **two-board** MITS Altair 8" floppy disk controller
(88-DCDD) and, in a different configuration, for the original two-board Altair
Minidisk controller (88-MDS). From the perspective of the Altair CPU and
software it is register- and timing-compatible with the originals.

Key facts for an emulator:

- The controller registers occupy **4 consecutive I/O ports** (default `08h`–`0Bh`).
- It emulates both the 8" hard-sectored drive (32 sectors) and the 5.25" Minidisk
  (16 sectors), selectable by a latched **Drive Type** setting.
- It can synthesize **virtual hard-sector pulses** from soft-sectored media by
  syncing to the disk's rotational rate — transparent to software.
- It carries up to **64K of on-board RAM** (1K increments) and **8K of PROM**
  (256-byte increments) that can be independently enabled and address-mapped.
- It adds a **high-speed serial port** that can act as the controller for a
  PC-hosted "serial drive" (no rotating media), and also hosts an on-board
  monitor for firmware/config.
- The internal interrupt fires at the **start of each sector** and can be routed
  to any Altair vectored interrupt line V0–V6 or to PINT.

### Enhancements beyond the original MITS controller

- Drop-in replacement for the two-board Minidisk controller (incl. 8800bt "Foley"
  configuration).
- Direct connection to Shugart SA-80x 8" drives made to look like Altair/Pertec
  8" drives (media interchangeable with aligned Altair/Pertec drives).
- Direct connection to Shugart SA-400 5.25" drives made to look like an Altair
  Minidisk (media interchangeable with a 48-TPI Altair Minidisk).
- Accepts **soft-sectored** media as a substitute for hard-sectored media.
- 5.25" high-density drive (e.g., Teac 55-GFR) usable as an Altair 8" drive.
- 1.5 Mb drive support (HD 5.25" or DSDD 8") under a patched CP/M 2.2.
- 8" IBM-3740 SSSD soft-sector support (iCOM/Pertec FD3712 emulation).
- PC-hosted serial drive, including an 8 Mb virtual 8" drive.

---

## 2. I/O Port Map (register interface)

By default the FDC+ responds to I/O addresses **08h–0Bh**. Cutting two traces and
adding two jumpers (cross-wire A7↔A3) relocates it to the alternate block
**80h–83h**. These are the SAME registers as the original 88-DCDD / 88-MDS.

| Port (default) | Port (alt) | Read           | Write          |
|----------------|-----------|----------------|----------------|
| `08h`          | `80h`     | Drive Status   | Drive Select   |
| `09h`          | `81h`     | Sector Position| Drive Command  |
| `0Ah`          | `82h`     | Read Data      | Write Data     |
| `0Bh`          | `83h`     | Reserved       | Reserved       |

> The FDC+ manual documents the port assignments but does NOT re-print the
> individual bit fields of the Drive Status / Drive Select / Sector Position /
> Drive Command / data registers — those are identical to the original MITS
> 88-DCDD (8") and 88-MDS (Minidisk) controllers and must be taken from those
> manuals / the existing `dcdd` and `mds` card implementations in this repo.
> The FDC+ additions relevant to those registers are:
> - The internal (sector) interrupt is **de-asserted as soon as the 8080 reads
>   any FDC+ register** (see §2.4.1), which differs from the original.
> - Sector pulses are synthesized on the fly for soft-sectored media, so the
>   Sector Position register always presents the expected 32 (8") or 16 (Minidisk)
>   sectors plus index regardless of the physical media.

---

## 2.1 Configuration switches (four DIP banks + jumpers)

Four DIP switch banks configure the board:

1. **Drive Type (S3)** — selects the emulated drive type and enables the monitor.
2. **RAM (S1)** — enable/disable on-board RAM and its 1K start address.
3. **PROM (S2)** — enable/disable PROM, sets RAM end address and PROM start.
4. **Interrupt Select (S4)** — routes the internal interrupt to a bus line.

Additionally, PCB traces can be cut/jumpered to change the I/O address block and
to disable RAM/PROM when *Phantom is asserted.

Switch convention: actuator toward the **top** of the PCB = **1**, toward the
**bottom** = **0**. On every bank the silkscreen labels (not the numbers printed
on the switch body) identify each switch.

### Drive Type switches (S3)

The right-most four switches (silkscreen "3 2 1 0") select the drive type; the
far-left switch is "Mon" (monitor). Switch positions between "Mon" and drive-type
bit 3 are unused. **The drive type is latched at power-on**; changing switches
afterward has no effect until the next power cycle.

**Table 2.1 — Drive Types (switch bits 3 2 1 0)**

| 3 2 1 0 | Value | Drive Type                                                  |
|---------|-------|-------------------------------------------------------------|
| 0 0 0 0 | 0     | Original Altair 8" drive                                     |
| 0 0 0 1 | 1     | Direct connect to Shugart 8" drive as Altair 8" drive       |
| 0 0 1 0 | 2     | Original Altair Minidisk                                     |
| 0 0 1 1 | 3     | Direct connect to 5.25" drive as Altair Minidisk            |
| 0 1 0 0 | 4     | 5.25" HD drive as Altair 8" drive                           |
| 0 1 0 1 | 5     | 1.5 Mb 5.25" HD drive or 1.5 Mb 8" DSDD drive               |
| 0 1 1 0 | 6     | Serial drive as Altair Minidisk                             |
| 0 1 1 1 | 7     | Serial drive as Altair 8" drive                             |
| 1 0 0 0 | 8     | 8" SSSD Soft-Sectored (iCOM/Pertec FD3712, IBM-3740)        |
| 1001–1111 | 9–15| Not assigned                                                |

**On-board monitor ("Mon" switch):** far-left switch on S3. Set to 1 at power-on
to enter the monitor (used for firmware updates and settings such as serial baud
rate). Accessed over the built-in serial port (J2, IDC-14 → DB-25), **9600 baud,
8N1**, DB-25 wired as DCE (TX/RX/GND only). If the FDC+ appears non-functional,
suspect the monitor switch being left on — turn it off and power-cycle.

### Interrupt Select switches (S4) and interrupt clearing

Like the original FDC, the FDC+ generates an interrupt **at the start of each
sector**. S4 routes the internal interrupt to exactly one of the bus vectored
interrupt lines **V0–V6** or to **PINT**. Actuate one switch (silkscreened
V0–V6 / PINT) toward the top; only one at a time.

**§2.4.1 Interrupt clearing (emulator-relevant behavior change):** On the FDC+,
the interrupt is **de-asserted as soon as the 8080 interrupt routine reads ANY
register on the FDC+**. On the original FDC, the interrupt was cleared only by
the 8080 interrupt-acknowledge cycle (which could belong to a different device).
This "bug fix" works with all known Altair disk software because the first thing
a disk interrupt routine typically does is read the sector position register.

### Phantom (§2.6)

The FDC+ can be write-through or full Phantom:

- **Write-through Phantom:** when *Phantom is asserted, the FDC+ ignores memory
  **reads** but still services memory **writes** to its RAM.
- **Full Phantom:** both memory reads and writes are ignored when *Phantom is
  asserted.

The FDC+ **cannot drive** the Phantom signal, only respond to it. (Revision B and
C enable these modes via different board mods; not emulation-relevant beyond the
read/write-ignore semantics above.)

---

## 2.2 On-board RAM and PROM address mapping

### RAM (S1)

- **`*EN` switch** (active-low): `0` = RAM enabled, `1` = RAM disabled.
  (Opposite polarity from the PROM enable switch.)
- Switches **"15"–"10"** map to address lines **A15–A10** and set the **1K**
  block at which RAM starts. E.g. A15–A10 = `000000` → RAM at `0000h`;
  `110000` → RAM at `C000h`.
- RAM's **end** address is set by the PROM start address (even if PROM is
  disabled). Max PROM start is `FF00h`, so RAM normally cannot extend above
  `FF00h` — the **top 256 bytes are inaccessible** by default.
  - Revision B: RAM can stop no lower than `E000h`.
  - Revision C: RAM can stop as low as `8000h`.
- The leftmost S1 switch is unused.

**Full 64K mod (§2.2.1):** cut the trace U9p13↔U8p19, connect U9p13↔U9p14. After
the mod the PROM-switch page value becomes (start page − 1) of EPROM and (last
page) of RAM. Setting PROM switches to page `FF` then gives RAM `FF00–FFFF`
(full 64K); EPROM becomes inaccessible and PROM enable should be off.

### PROM (S2)

- Socket holds a **27C64 (8K)** EPROM.
- **`EN` switch:** `1` = PROM enabled, `0` = disabled. (Opposite polarity from
  the RAM `*EN` switch.)
- Switches **"12"–"8"** map to address lines **A12–A8**, selecting the 256-byte
  page within the 8K PROM at which the PROM begins to respond.
- **Revision B:** 8K PROM fixed at `E000h`–`FFFFh`; A15–A13 permanently `1`. The
  two leftmost S2 switches are unused.
- **Revision C:** A15 permanently `1`; switches A14 and A13 (typically both `1`
  for standard Altair) allow the 8K PROM to start at `8000h`, `A000h`, `C000h`,
  or `E000h`. Below `E000h`, PROM content repeats through `FFFFh`.

Examples:
- Only the 256-byte **DBL** (disk boot loader) at `FF00h`: program DBL into the
  top 256 bytes of the 27C64 (`1F00h–1FFFh`); set A12–A8 = `11111` (page 1Fh);
  with A15–A13 = all ones → responds at `FF00h`.
- **MBL + TURMON + hex-loader + DBL**: burn to `1C00h–1FFFh` of the 27C64; set
  A12–A8 = `11100` (page 1Ch) → responds at `FC00h`.

---

## 3. Drive types (geometry, timing, media)

### 8" drive (types 0, 1) — Altair 88-DCDD compatible

- **32 hard sectors** + index (33 pulses total). The Altair FDC and FDC+ expect
  all 33 pulses and perform sector/index separation on the board.
- 77 tracks (standard Altair 8" geometry).
- 8" **disk image file** size: **330K**, `.dsk` extension.
- Shugart 800 (soft-sectored) and 801 (hard-sectored) both usable; an 801 must be
  jumpered to behave like an 800 (disable its sector separator) because the FDC+
  does the separation. A "soft-sectored" Shugart 800 still asserts all 32 sector
  holes plus the index hole when a hard-sectored floppy is inserted.
- **Index-alignment quirk:** a calibrated Shugart 80x and a calibrated
  Altair/Pertec drive differ in index alignment by **~360 µs**, enough to make
  media incompatible. The FDC+ **automatically compensates** for this 360 µs
  offset in firmware. Align Shugart 80x with a Shugart SA120 / Dysan 360A disk —
  NOT the Pertec/Dysan 500 disk.

### Minidisk (types 2, 3) — Altair 88-MDS compatible

- **16 hard sectors** (5.25" Altair Minidisk format).
- Minidisk **disk image file** size: **75K**, `.dsk` extension.
- Type 2 = original Altair Minidisk hardware (incl. 8800bt "Foley" internal
  cabling variant). Type 3 = direct-connect Shugart SA-400 (or any 48-TPI 5.25"
  drive) presented as an Altair Minidisk.
- 48-TPI media is interchangeable with an aligned Altair Minidisk. 96-TPI drives
  work and their media interchanges between 96-TPI drives, but 96-TPI-written
  media will NOT read in a 48-TPI drive and vice-versa.
- Use SD or DD media for Minidisk applications.

### 5.25" HD drive as 8" drive (type 4)

- A 96-TPI 5.25" HD drive (e.g., **Teac 55-GFR**) presents as an Altair 8" drive.
- Configured to spin at **360 RPM** to match 8" rotation.
- The drive has **80 tracks** (vs 77 on real 8"); firmware generates **32 virtual
  hard sectors** for soft-sectored HD media. Media is NOT interchangeable with a
  real 8" drive, but interchanges between Teac drives.
- Use soft-sectored **HD** floppies. The FDC+ creates virtual hard-sector pulses.

**Table 3.5.1 — Teac 55-GFR jumper settings for 8" compatibility**

| Jumper   | In/Out            | Function                                             |
|----------|-------------------|-----------------------------------------------------|
| LG       | Out               | HD mode (pin 2 high / not driven)                   |
| I        | Out               | 360 RPM                                             |
| E2       | In                | Index pulses continue while seeking                 |
| DS0–DS3  | Install for drive#| 0–2 = Shugart 1–3; DS3 on P6 → jumper to IDC-50 P32 |
| IU       | Out               | Ignore "In Use" on P4                               |
| U0,U1    | In                | Light on with drive select and motor spinning       |
| RY, DC   | Out               | Neither Ready nor Disk Change on drive P34          |

### 1.5 Mb drive (type 5)

- Introduced in FDC+ firmware **v1.3**. Provides **1.5 Mb** formatted on a 5.25"
  HD floppy or an 8" DSDD drive.
- Disk format and controller access differ from a normal Altair drive, so
  **original Altair software (Disk BASIC, Altair DOS, CP/M for Altair) is NOT
  compatible**. A special CP/M 2.2 (from the FDC+ "Altair Software" link)
  supports up to four 1.5 Mb floppies.
- Drive wiring: HD 5.25" as in §3.5.1 (Teac config); 8" DSDD as in §3.4.1
  (Shugart config).

### Serial drive (types 6, 7) — no rotating media

- The FDC+ high-speed serial port connects to a PC running a disk-image server;
  software sees a normal Altair drive. Performance ≈ real Altair floppy.
- Type 6 = serial drive as **Minidisk**; type 7 = serial drive as **8" drive**.
- Serial port = same J2 header/IDC-14→DB-25 cable used by the monitor; DCE,
  standard DB-25→DB-9 cable (no null modem).
- Baud rates (set in the monitor): **403.2K** (default/first choice),
  **460.8K** (460.K), or **230.4K**. At 230.4K, 8" operation is slightly slower
  than a real 8" drive; Minidisk still runs full speed. Fast rates usually
  require a USB-to-serial adapter rather than a native PC serial port.
- Server: "Altair Server.exe" (Windows), no install.

**8 Mb serial 8" drive (§3.7.4):** introduced with FDC+ firmware **v1.2** /
Serial Drive Server **v1.3**. Looks like an Altair 8" drive with **2048 tracks
instead of 77**. Set drive type to **7**. CP/M for the 8 Mb drive expects an 8 Mb
image on drives **A and B (0,1)** and normal 77-track Altair images on **C and D
(2,3)**. The 8 Mb CP/M is patched to show the current user number in the prompt
and will run a command from drive A user 0 if not found on the current
drive/user.

### iCOM/Pertec FD3712 (type 8) — 8" SSSD soft-sectored, IBM-3740

- Presents as an iCOM/Pertec FD3712 dual-drive cabinet/controller with its S-100
  interface card, implementing the **8" SSSD soft-sector IBM-3740** format common
  in the 1970s.
- Connects to up to four Shugart 800 drives (config as §3.2.1). 5.25" HD drives
  (e.g., 1.2 Mb Teac 55-GFR) also work, wired/configured as §3.5.
- Runs vintage FD3712 software (FDOS, CP/M) and can read/write 8" SSSD disks
  written by other systems.

### Soft-sectored media as substitute for hard-sectored (general)

The original Altair FDC required hard-sectored media (32-sector 8", 16-sector
5.25"). The FDC+ also accepts soft-sectored media: if it detects no sector holes
as the disk spins up, it **syncs to the rotational rate and generates virtual
sector pulses** on the fly, fully transparent to the Altair. Soft-sectored media
written on one aligned FDC+ drive works in another aligned FDC+ drive, but NOT in
an original two-board FDC. Belt-driven 8" drives (Shugart 80x) are unreliable
with soft-sector media due to intra-revolution speed variation; belt-driven
5.25" drives tolerate it better. Use SD/DD media for Minidisk, HD media when
running a 5.25" as an 8" drive.

---

## 4. PROM / boot (Appendix B) — as shipped

The 27C64 ships pre-programmed with (each entry is a 256-byte page):

| Address | Contents                                                            |
|---------|---------------------------------------------------------------------|
| `FF00`  | **CDBL** — Combined Disk Boot Loader (Martin Eberhard; boots either 8" or Minidisk) |
| `FE00`  | **MBL** — Altair Multi-Boot Loader (boots cassette tapes, paper tapes) |
| `FD00`  | **TURMON** — Altair Turnkey Monitor (stack at `F800`; needs RAM just below `F800`) |
| `FC00`  | **Intel hex file loader**                                           |
| `F800`  | **ALTMON** — Altair Monitor (full-featured)                         |

Notes:
- MITS shipped **separate** disk boot loaders for 8" and Minidisk, both at
  `FF00`, so both couldn't coexist. CDBL replaces both and boots either type.
- MBL and TURMON are original Altair PROMs.
- **Intel hex loader:** jump to `FC00`; prompts to send a hex file over the first
  port of a 2SIO board; displays load-progress addresses; idles in an endless
  loop when done. Stack at `8000h` → needs RAM just below `8000h`.
- ALTMON: memory display/modify/search/compare/move, Intel hex loader, I/O port
  in/out, disk boot, etc.
- Any subset of PROM content can be enabled (as little as the top 256-byte DBL).

---

## 5. Disk-image / file-transfer utilities (Appendix A)

- **Image files:** `.dsk` extension. 8" = **330K**, Minidisk = **75K**,
  8 Mb serial 8" = large (2048 tracks). Recognizable by file size.
- **PC2FLOP** — Altair-side utility that writes a full floppy from a disk image
  received over a **2SIO** serial port via **XMODEM**. Runs stand-alone or under
  CP/M; can write any disk type. Separate builds exist per drive type (8"/330K,
  Minidisk/75K, 1.5 Mb). Can be bootstrapped via the loader technique or the
  PROM's Intel hex loader.
- **PCGET / PCPUT** — per-file transfer to/from an Altair running CP/M over a
  2SIO port via XMODEM (PCGET = PC→Altair, PCPUT = Altair→PC). Pre-installed on
  most CP/M disk images.
- The 2SIO can be modified to run at **19,200 or 38,400 baud** to speed transfers.
- TeraTerm is the recommended PC terminal emulator.

---

## 6. Emulation checklist (quick reference)

- Default register block **08h–0Bh** (alt 80h–83h): Drive Status/Select (0),
  Sector Position/Command (1), Read/Write Data (2), Reserved (3).
- Register bit semantics = original 88-DCDD (8", 32 sectors) or 88-MDS (Minidisk,
  16 sectors) depending on latched drive type. See existing `dcdd`/`mds` cards.
- Drive type is **latched at power-on**; ignore changes afterward.
- Sector interrupt fires at the **start of each sector**; it clears on **any**
  FDC+ register read (not on interrupt-acknowledge). Route to V0–V6 or PINT.
- 8" = 32 sectors + index (33 pulses), 77 tracks, 330K image; Minidisk = 16
  sectors, 75K image. Virtual sector pulses are synthesized for soft-sector media.
- Optional on-board 64K RAM (1K granularity) and 8K PROM (256-byte granularity)
  with the address rules in §2.2. Shipped PROM map: CDBL/MBL/TURMON/hex/ALTMON
  at FF00/FE00/FD00/FC00/F800.
- Serial-drive variants (types 6/7) and iCOM FD3712 (type 8) are host/serial-side
  features rather than S-100 register behavior.
