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
- **CP/M 3 BIOS source — obtained.** `SD3.ASM` (the SD disk driver) and `HBOOT3.ASM` (the
  cold-boot loader), John Monahan, dated **1/24/2026**, from the author's "AB-SD Disk"
  distribution. **This is the actual source for the shipping SD `CPM3.SYS` BIOS3 that §5.1 was
  reverse-engineered from** — its sign-on banner (`64K CP/M VERSION (Non banked) (John Monahan
  1/24/2026)` / `A: & B: = SD cards (61 Sec/Track)`) and presence messages match the
  disassembly verbatim. It **confirms** the port map, the command set, the byte handshake, the
  card-detect bits, the 512-byte sector, and the DPB below; the earlier reverse-engineering
  stands. (Not a fetchable page; obtained directly from the author. Same licensing note as
  below — nothing redistributed.)
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
4. **RESOLVED (`.imgc` is a container).** The store's `.imgc` files are **not** raw sector dumps
   — they are HDD Raw Copy Tool containers (plaintext header + LZO-compressed, zero-padded
   blocks). Decode to raw with `unimgc` (github.com/shizmob/unimgc) before mounting; never mount
   the `.imgc` directly (§7).
5. **CONFIRMED BY BIOS SOURCE (§5.1).** The CP/M 3 cold-boot **presence check** — the reason a
   naïve STATUS=0x00 board boots CPMLDR yet dies at `NO CCP.COM FILE` — was first
   reverse-engineered from the shipping `CPM3.SYS` BIOS3, and is now **confirmed against the
   BIOS source** (`HBOOT3.ASM` `?INIT`, `SD3.ASM` `SD$RD`). STATUS **bit 1 / bit 2** are the two
   sockets' card-detect lines; a card in each socket must read high for the cold boot to reach
   the `A>` prompt, and STATUS must never be 0xFF while the board is present. With that modeled,
   the SD `CPM3.SYS` boots to `A>`.

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
| STATUS | **80H** | status byte (bit 7 = data ready, bits 2/1 = card-detect D:/C:, bit 0 = write-buffer busy; 0xFF = no board) | flush pending read data |
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
formatted run. Note the CP/M 3 BIOS (`SD3.ASM`) only *defines* `CMD$FORMAT$SECTOR` (87H) — it
never issues it, and its EQU comment mischaracterises it as "format the CURRENT sector" (one
sector). The **firmware's** count semantics are authoritative; a `WR_BOOT`/format utility, not
the BIOS, drives this command.

## 3. The byte handshake and addressing

The DI7 / write-busy handshake the board model must reproduce (firmware `GetData`/`SendData`,
mirrored by the drivers' `GET_DATA`/`SEND_DATA`):

- **Status bit 7 (DI7) high = a byte is waiting to be read.** Reading the DATA port takes it and
  presents the next. A 512-byte read is 512 iterations; the trailing STATUS byte keeps DI7 high
  for one more read.
- **Status bit 0 high = the last written byte has not been consumed yet.** The host waits for it
  to fall before the next write. (In emulation the engine consumes each written byte at once, so
  bit 0 reads back 0.)
- **Status bits 1 and 2 = the two sockets' CARD-DETECT lines.** **bit 1 (0x02) = SD card 1
  (drive C:) inserted; bit 2 (0x04) = SD card 2 (drive D:) inserted.** These are static presence
  signals, readable at any time, that the CP/M 3 BIOS keys its cold-boot presence check off (§5).
  A board that is installed but has *no* card in a socket reads that socket's bit **low** (0); an
  all-ones read (**0xFF**) means the slot is empty — no board driving the bus at all. The board
  model ORs bit 1 / bit 2 into the STATUS byte whenever drive 0 / drive 1 has media mounted, and
  never returns 0xFF while it is present. (The data-transfer masks — bit 7 for DI7, bit 0 for
  write-busy — are unaffected, so these bits are safe to leave asserted during a transfer.)

**Addressing (firmware `getTrkSec`).** `SET_TRK_SEC` reads the **track byte first, then the
sector byte**, and forms the current sector as:

```
sector = (Track << 8) + Sector;     // getTrkSec(): track high, sector low
```

so the card **LBA = `track*256 + sector`** (256 sectors per track), byte offset `LBA * 512`.
`SET_TRK_SEC` therefore addresses the first 32 MB; `SETLBA` (91H) sets a full 32-bit LBA,
**most-significant byte first**. The board model reads the two `SET_TRK_SEC` bytes as track
(high) then sector (low), and addresses the mounted medium directly by byte offset.

**The BIOS sends a 1-based sector on the wire** (confirmed in `SD3.ASM` `SD$wrlba`: `LDA @SECT`
then `INR A`, "Disk sectors are numbered 1...MAXSEC"). So the CP/M 3 driver's first sector of a
track lands at card LBA `track*256 + 1`, and **card LBA 0 (track 0, sector 0) is never addressed
by `SET_TRK_SEC`** — it is the reserved boot sector (the DPB's one system track, §4; the monitor
loads `CPMLDR` starting at LBA 1, §5; `FORMAT` skips sector 0, §2). This is transparent to the
board — it just multiplies whatever track/sector bytes arrive into a byte offset — but it
explains why nothing is stored at offset 0 of a live card.

## 4. Geometry and the CP/M 3 DPB

- **Physical sector: 512 bytes.** Addressed by LBA (track + sector, or a 32-bit SETLBA).
- **CP/M 3 hard-disk DPB** — confirmed from the BIOS source (`SD3.ASM`):
  `DPB 512,61,256,2048,1024,1,8000H`, i.e. **512 B/physical sector, 61 sectors/track, 256 tracks,
  2048-byte allocation blocks (BLS = 2K), 1024 directory entries, 1 reserved/system track**, and
  a checksum field of `8000H` — the CP/M 3 "permanent (non-removable) drive" flag (high bit set,
  no removable-media directory checksumming). (`CPM3.LIB` `dpb` macro args:
  `psize,pspt,trks,bls,ndirs,off,ncks`.) A 32 KB directory track holds 1024 × 32-byte entries.
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

### 5.1 The CP/M 3 BIOS cold-boot presence check (confirmed by source)

**The BIOS source is now in hand** (`SD3.ASM` + `HBOOT3.ASM`, John Monahan 1/24/2026 — see the
Sources list). It confirms the presence check exactly. The check below was originally
**disassembled from the shipping BIOS3 binary in the emulator** (loads at BA00; `SAVE bios3.prn
BA00-C600` after the `I` command reaches its error, then read the SD routines around BC55 and
C284) before the source surfaced; the two agree byte-for-byte. Both are kept here — the
disassembly recipe is still useful for any BIOS whose source is not available.

The cold-boot presence gate lives in **`HBOOT3.ASM` `?INIT`** (the `BC55` region): read STATUS,
`CP FF` → no board, send `RESET` (88H), then test bit 1 (card 1 / C:) and bit 2 (card 2 / D:),
printing a "…is not Present" warning for each socket that reads low. The disk-access gate is in
**`SD3.ASM` `SD$RD`** (the `C284` region): `IN 80` / `BIT 1,A` (or `BIT 2,A`) before touching a
drive, failing the read with the error flag if the card-detect bit is clear — which is what
turns an absent card into `CP/M Error On A: Disk I/O` → `NO CCP.COM FILE`.

After the monitor's `I` command loads and jumps to CPMLDR → `CPM3.SYS`, the BIOS cold-boot code
runs a **STATUS-only presence gate** on each socket — it makes the decision from the STATUS port
(80H) alone, with **no** disk read and (initially) no `INIT`/`SELECT`:

```
BC55  IN A,(80)        ; read STATUS
BC57  CP  FF           ; 0xFF  → "No Dual SD Card Board Detected" (empty slot / floating bus)
BC59  JP  Z,noBoard
BC5C  LD  C,88         ; send RESET (88H) to the ESP32
BC5E  CALL sendCmd
BC61  IN  A,(80)       ; read STATUS after reset
BC63  AND 02           ; bit1 = SD card 1 (C:) present?  clear → "SD Drive 1 … is not Present"
BC6E  IN  A,(80)
BC70  AND 04           ; bit2 = SD card 2 (D:) present?  clear → "SD Drive 2 … is not Present"
BC72  RET NZ           ; present → proceed
```

The disk-access path repeats the same test (`C284: IN A,(80) / CP FF … C28D: BIT 2,A`). So a
Dual SD board that answers STATUS = 0x00 (as a naïve model does) is read as **both cards
absent** → `CP/M Error On A: Disk I/O` → `BIOS ERR ON A: NO CCP.COM FILE`. The board model must
therefore assert the card-detect bits of §3 (bit 1 for a card in socket 1, bit 2 for socket 2);
with both asserted the same `CPM3.SYS` boots straight through to the `A>` prompt. The `CP FF`
guard is why an installed-but-empty board must read 0x00, never 0xFF.

> **Drive lettering caveat (confirmed inconsistent in the source).** The SD-only build is
> internally inconsistent about drive letters, and the source proves it: `HBOOT3.ASM`'s banner
> prints `A: & B: = SD cards`, while the very same module's presence messages say `SD Drive 1:
> (CPM C:)` / `SD Drive 2: (CPM D:)`, and `SD3.ASM`'s DPH comments label the two drives `C:` and
> `D:`. The drive table itself (`HDRVTBL3.ASM` `@DTBL <DPH0,DPH1,…>`) installs them as the first
> two CP/M drives (A:/B:) on an SD-only card. The presence *mechanism* above is invariant; the
> CP/M drive-letter mapping on a given image is a property of that image, not the board.

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
CP/M 3 card with SD volumes). Image-16 (one SD volume) is the cleanest single-card form.
System tracks come from `WR_BOOT.COM`, so a bootable card cannot be blank-created — only a
blank data card can.

### How altairsim stores a card

Each SD card is one CP/M drive, and the BIOS assigns the drive letter from the **socket** it
sits in (socket 1 → A:, socket 2 → B:), not from anything on the card. altairsim therefore
models a card as a single raw image file, `foo.img`, paired with a sibling **geometry
sidecar** `foo.geo` of the same base name:

```
sector_size 512
sectors     769920      # the card's real size; the .img may be shorter
```

The board sees one flat LBA space of `sectors × sector_size`; the image may be **truncated**
to just its live filesystem, and every sector past the image's end reads back the erased-card
fill byte (0xFF). Mounting a `.img` with no `.geo` beside it is an error — the geometry is
required. `MOUNT … CREATE format=dualsd` (or `sectors=<n>`) authors a blank, unformatted card
(an empty `.img` + its `.geo`). See `src/host/cardimg.{h,cpp}`.
