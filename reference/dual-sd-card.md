# S100Computers Dual SD Card Board

Sources (John Monahan's S-100 archive, s100computers.com):
- Board page: [Dual SD card Board.htm](https://www.s100computers.com/My%20System%20Pages/Dual%20SD%20card%20Board/Dual%20SD%20card%20Board.htm)
- **ESP32 firmware — the definitive authority** for the port protocol, the command set, the
  byte handshake, and the addressing:
  [S100_ESP32_Firmware_v1.5 (text).ino](https://www.s100computers.com/My%20System%20Pages/Dual%20SD%20card%20Board/S100_ESP32_Firmware_v1.5%20(text).ino)
  (by Wayne Warthen & Michael Petry, derived from John Monahan's work; board id 2 = "Dual SD",
  dated 7-Sep-2025). This is the code the board actually runs; §2–§3 below are distilled from
  its `loop()` + `runCmd()`.
- Boot monitor **MASTER Z80 (V6.6)** — the CPU board's ROM monitor that boots CP/M 3 off this
  board via its `I` command (added V6.52, 12/2025). Source
  [Master0.z80](https://www.s100computers.com/Software%20Folder/Master/Master0.z80) /
  [Master1.z80](https://www.s100computers.com/Software%20Folder/Master/Master1.z80)
  (assembled `MASTER0.HEX`/`MASTER1.HEX`, org F000H, ship in the V6.6 distribution). See §5.
- Board test driver `SD_CARD.Z80` / `SD_IO.Z80` (John Monahan, V0.5 1/22/2025), the Z80 test
  program shipped with the board. It agrees with the firmware on the protocol and is a second
  witness for §2–§3. Obtained from the author's SD_Card distribution; not fetchable from HTML.
- CP/M 3 hard-disk BIOS: [CPM3 HDISK BIOS Software.htm](https://www.s100computers.com/Software%20Folder/CPM3%20BIOS%20Installation/CPM3%20HDISK%20BIOS%20Software.htm)
- Ready images: [CPM Card Images Store.htm](https://www.s100computers.com/Software%20Folder/CPM%20Card%20Images/CPM%20Card%20Images%20Store.htm).
  Boot target: **Image-14** (IDE-CF + Dual SD, non-banked CP/M 3),
  [(Image-14) …CPM3.imgc](https://www.s100computers.com/Software%20Folder/CPM%20Card%20Images/CF_IMAGES/(Image-14)%20IDE-CF%20+%20Dual%20SD%20Card%20Non-Banked%20CPM3.imgc).

This reference distills what a software emulation of the Dual SD board needs: the two-port
command protocol, the byte handshake, the 512-byte sector model, the CP/M 3 hard-disk DPB the
card runs under, and the boot path. Circuit-level detail (the ESP32 firmware internals, the
GPIO/flip-flop wiring, level shifters) is omitted as not relevant to emulation.

**Licensing:** no licence is stated on any of these pages. The board, firmware and images are
John Monahan's hobbyist/educational S-100 project; the CP/M 3 payload is Digital Research code.
Nothing here is redistributed — this is a text distillation citing the public pages, and no
image or ROM is committed on the strength of it. (Project rule: record the licence, never gate
on it.)

**Open items:**
1. **RESOLVED (firmware).** The command port model, the trailing STATUS byte, the addressing,
   and the FORMAT semantics are all pinned from `S100_ESP32_Firmware_v1.5.ino` (§2–§3), with
   `SD_CARD.Z80` as a second witness.
2. **Blank/unwritten-sector fill.** A never-written *in-range* sector returns whatever the
   medium holds — a real card returns its erased-flash pattern; the directory-card medium
   returns `0xFF`. This is distinct from a *failed* read (offset past the card), where the
   firmware sends 512 `0x00` bytes and STATUS=ERR (§2). `0xE5` is written only by FORMAT.
3. **RESOLVED (MASTER V6.6).** The `I` command (`SDBOOTCPM` → `SDCPM`, added V6.52) boots CP/M 3
   directly from the Dual SD board — no CF/IDE board required. The v4.53 "SD boot not wired"
   note is obsolete. See §5.
4. **`.imgc` container form.** The store page calls them "sector image" files written with the
   HHD Raw Copy Tool, which strongly implies a plain raw byte-for-byte sector dump; a
   compressed/container form is not ruled out on the page (§7).

---

## 1. Board overview

- A modern S-100 board carrying **two microSD sockets** (drives 1/C: and 2/D:), driven by an
  onboard **ESP32-S3** module. "All access to the SD cards is done via a few one byte commands
  sent to the board. These commands are processed by the ESP32 and send/receive sector data to
  the S100 bus over two parallel ports."
- The S-100 CPU sees only **two 8-bit I/O ports** and a byte-at-a-time handshake — architecturally
  the same shape as the 88-HDSK / iCOM FD3712: a programmed-I/O command engine with a full-sector
  buffer, not a WD177x-style register file.
- **No boot PROM on the board.** CP/M is loaded by the CPU board's ROM monitor (MASTER, §5).
- Runs **CP/M 3** (banked and non-banked); the SD/CF family also carries CP/M 2.2, MS-DOS, CP/M-68K
  and CP/M-86 images.

## 2. Port map and command protocol

Two ports, DIP-selectable to any 8- or 16-bit address. Defaults:

| Port | Default | Read | Write |
|------|---------|------|-------|
| STATUS | **80H** | status byte (bit 7 = data ready, bit 0 = write-buffer busy) | flush pending read data |
| DATA   | **81H** | next byte the ESP32 is returning | next byte into the ESP32 (lead, command, or argument) |

**The port model (pinned from the firmware and both Z80 drivers).** There is **one host→board
input path**: the `33H` lead byte, the command code, *and* every argument/write byte are all
written OUT the **DATA port (81H)**. The STATUS port (80H) is a read-only status register; a
*write* to it merely flushes any pending read byte (the drivers' init housekeeping — "tell the
ESP32 no data is waiting"). There is no separate "command port". In the firmware this is the
`loop()` state machine: `33H` → command → the command's payload.

Both Z80 drivers confirm it — `MASTER.Z80`'s `SD_CMD`/`SD_PUTBYTE` and `SD_CARD.Z80`'s
`SEND_CMD`/`SEND_DATA` send the `33H` lead and the command code via `OUT (SD_DATA)` = 81H, and
read status via `IN (SD_STAT)` = 80H.

**Every in-range command returns a trailing STATUS byte** — the firmware runs `SendData(runCmd(cmd))`
so a STATUS value is pushed back after the command's own output (if any). The driver always
reads it back (its `GET_DATA`). `RESET` (88H) reboots the ESP32 and returns nothing; an
out-of-range code is ignored with no reply.

| Command | Code | Host sends after code | Board returns |
|---------|------|-----------------------|---------------|
| `INIT_DRIVE_1` C: | **80H** | — | STATUS |
| `INIT_DRIVE_2` D: | **81H** | — | STATUS |
| `SEL_DRIVE_1` C:  | **82H** | — | STATUS |
| `SEL_DRIVE_2` D:  | **83H** | — | STATUS |
| `SET_TRK_SEC`     | **84H** | Track (byte), Sector (byte) | STATUS |
| `READ_SECTOR`     | **85H** | — | 512 data bytes, then STATUS |
| `WRITE_SECTOR`    | **86H** | 512 data bytes | STATUS |
| `FORMAT_SECTOR`   | **87H** | sector **count** (16-bit, LSB then MSB) | STATUS |
| `RESET_ESP32`     | **88H** | — | (reboots; nothing) |
| `FWVER`           | **90H** | — | BoardID, Ver-Major, Ver-Minor, then STATUS |
| `SETLBA`          | **91H** | LBA (4 bytes, **MS first**) | STATUS |
| `TYPE`            | **92H** | — | card-type byte, then STATUS |
| `CAP`             | **93H** | — | sector count (4 bytes, MS first), then STATUS |
| `CID`             | **94H** | — | 16 bytes, then STATUS |
| `CSD`             | **95H** | — | 16 bytes, then STATUS |
| `DISP`            | **96H** | NUL-terminated string | STATUS |
| `ECHO`            | **97H** | length (MSB,LSB), then that many bytes | those bytes, then STATUS |

STATUS values: **`STAT_OK = 0x00`**, **`STAT_ERR = 0x1A`**.

**READ error path (firmware).** If the sector read fails (no card, or an offset past the media)
the firmware still sends **512 bytes of `0x00`** and then STATUS=`0x1A`. A never-written but
in-range sector is *not* an error — the medium returns its erased fill and STATUS is OK.

**FORMAT (firmware).** `FORMAT_SECTOR` takes a 16-bit sector **count**, then fills each of
`count` sectors with `0xE5`, **starting at the current sector and advancing it**, and **skips
sector 0** (it never overwrites the boot sector). The current sector is left advanced past the
formatted run.

## 3. The byte handshake and addressing

The DI7 / write-busy handshake the board model must reproduce (firmware `GetData`/`SendData`,
mirrored by the drivers' `GET_DATA`/`SEND_DATA`):

- **Status bit 7 (DI7) high = a byte is waiting to be read.** Reading the DATA port takes it and
  presents the next. A 512-byte read is 512 iterations; the trailing STATUS byte keeps DI7 high
  for one more read.
- **Status bit 0 high = the last written byte has not been consumed yet.** The host waits for it
  to fall before the next write. (In emulation the engine consumes each written byte at once, so
  bit 0 reads back 0.)

**Addressing (firmware `getTrkSec`).** `SET_TRK_SEC` reads the **track byte first, then the
sector byte**, and forms the current sector as:

```
sector = (Track << 8) + Sector;     // getTrkSec(): track high, sector low
```

so the card **LBA = `track*256 + sector`** (256 sectors per track), byte offset `LBA * 512`.
`SET_TRK_SEC` therefore addresses the first 32 MB; `SETLBA` (91H) sets a full 32-bit LBA,
**most-significant byte first**. The board model reads the two `SET_TRK_SEC` bytes as track
(high) then sector (low), and addresses the mounted medium directly by byte offset.

## 4. Geometry and the CP/M 3 DPB

- **Physical sector: 512 bytes.** Addressed by LBA (track + sector, or a 32-bit SETLBA).
- **CP/M 3 hard-disk DPB** (from the HDISK BIOS page): **512 B/physical sector, 61 sectors/track,
  256 tracks, 2048-byte allocation blocks (BLS = 2K), 1024 directory entries, 1 reserved/system
  track.** A 32 KB directory track holds 1024 × 32-byte entries.
  - Working volume size: 256 × 61 × 512 = **7,995,392 bytes (~7.63 MB)** per CP/M 3 drive.
- **On-card layout (LBA, 512-byte units):** boot loader at track 0 (§5); directory table and
  `CPM3.SYS` follow. (Reconfirm the exact LBAs against a real image in the machine build.)

## 5. Boot path

The board has no boot PROM. Booting is the CPU board's **MASTER Z80 monitor** (ROM at F000–FFFF,
a paged 8K EEPROM: the low 4K `MASTER0` and high 4K `MASTER1` both map to F000–FFFF and are
switched by `OUT D3H` bit 1; the SD boot loader lives in the **high** page). MASTER **V6.52+**
adds the **`I` command** — "boot CPM3 from Dual SD card Board. No IDE/CF board required."

`SDCPM` (the `I` handler, `Master1.z80`):

1. Read STATUS (80H); `0FFH` ⇒ no board present (the emulated board reads `0x00` idle, so it
   passes).
2. `SD_INIT` → `INIT_DRIVE_1` (80H), read STATUS.
3. Load **12 sectors** (`SEC_COUNT`) starting at **LBA 1** (`CPMLDR.COM`, "always TRK 0 SEC 2,
   LBA mode") into RAM at **100H** — for each: `SD_SEEK` = `SET_TRK_SEC` with `D`=track,`E`=sector
   of the incrementing `DE` LBA, then `SD_READBLK` = `READ_SECTOR` (512 data + STATUS).
4. Check `(100H) == 31H` (a `LD SP,nn`), then `JP 100H` — the standard CP/M 3 five-stage cold
   boot (`CPMLDR` → `CPM3.SYS` → CCP).

`WR_BOOT.COM` writes the loader to track 0 on a real card; a *bootable* volume therefore cannot
be blank-created — only data volumes can.

## 6. Notes for the emulation

- Model as a faithful port of the ESP32 firmware: two I/O ports, the single input path (§2),
  the eight core commands plus the report/utility set, the DI7/bit-0 handshake (§3), and a
  trailing STATUS byte on every in-range command. `FORMAT_SECTOR` fills `count` sectors with
  `0xE5`, skipping sector 0.
- The board addresses its mounted medium **directly by byte offset** — `readAt/writeAt(lba*512,…)`
  with `LBA = track*256 + sector` (§3). It does **not** wrap the medium in `DiskImage` (the
  directory-card medium owns its own geometry).
- Two `[[board.drive]]` sub-units = the two SD sockets.

## 7. Ready-made card images

The **CPM Card Images Store** offers raw sector card images (`.imgc`, "sector image", written to
media with the HHD Raw Copy Tool). Relevant to the Dual SD board:

| Image | Drives | OS | Banking | Media |
|-------|--------|----|---------|-------|
| **14** | A:, B: (CF) + C:, D: (SD) | CP/M 3 | non-banked | CF + SD |
| **16** | A: (SD only) | CP/M 3 | non-banked | 1 GB SD |
| **18** | A:, B: (SD)  | CP/M 3 | non-banked | 1 GB SD |

**Image-14** is the maintainer-supplied boot target for the acceptance test (a non-banked
CP/M 3 card with SD volumes). Image-16 (one SD volume) is the cleanest single-volume form,
usable as a single-partition directory card. System tracks come from `WR_BOOT.COM`, so a
bootable volume cannot be blank-created — only data volumes can.
