# S100Computers Dual SD Card Board

Source (fetched 2026-08-16, John Monahan's S-100 archive):
- Board page: [Dual SD card Board.htm](https://www.s100computers.com/My%20System%20Pages/Dual%20SD%20card%20Board/Dual%20SD%20card%20Board.htm)
- CP/M 3 hard-disk BIOS: [CPM3 HDISK BIOS Software.htm](https://www.s100computers.com/Software%20Folder/CPM3%20BIOS%20Installation/CPM3%20HDISK%20BIOS%20Software.htm)
  and the build note [Z80 CP_M 3.0 build_Rev01.pdf](https://www.s100computers.com/Software%20Folder/CPM3%20BIOS%20Installation/Z80%20CP_M%203.0%20build_Rev01.pdf) (page images, no text layer)
- Boot monitor: [MASTER.Z80 (V4.53)](http://www.s100computers.com/Software%20Folder/CPM3%20BIOS%20Installation/MASTER%20(V4.53).pdf),
  [Master.htm](http://www.s100computers.com/Software%20Folder/Master/Master.htm)
- Ready images: [CPM Card Images Store.htm](https://www.s100computers.com/Software%20Folder/CPM%20Card%20Images/CPM%20Card%20Images%20Store.htm)
- Board test driver: `SD_CARD.Z80` / `SD_IO.Z80` (John Monahan, V0.5 1/22/2025), the Z80 test
  program shipped with the board. It is the authoritative statement of the 80/81 port protocol,
  the 33H command lead, the byte handshake, and the SET_TRK_SEC argument order and LBA mapping
  (§3). Obtained from the author's SD_Card distribution; not fetchable from an HTML page.

This reference distills what a software emulation of the Dual SD board needs: the two-port
command protocol, the byte handshake, the 512-byte sector model, the CP/M 3 hard-disk DPB the
card runs under, and the boot path. Circuit-level detail (the ESP32 firmware internals, the
GPIO/flip-flop wiring, level shifters) is omitted as not relevant to emulation.

**Licensing:** no licence is stated on any of these pages. The board, firmware and images are
John Monahan's hobbyist/educational S-100 project; the CP/M 3 payload is Digital Research code.
Nothing here is redistributed — this is a text distillation citing the public pages, and no
image or ROM is committed on the strength of it. (Project rule: record the licence, never gate
on it.)

**⚠ Open items:**
1. **`SET_TRK_SEC` (84H) byte order and the (track,sector)→LBA formula — RESOLVED from
   `SD_CARD.Z80`.** `SET_SECTOR` sends the **track byte first, then the sector byte**; the card
   LBA is **`track*256 + sector`** (the driver's "next sector" step loads `H=track, L=sector`
   and does a single `INC HL`, so the pair is one big-endian 16-bit number rolling sector into
   track at 256 — i.e. 256 sectors per track). Byte offset = `LBA * 512`. See §3.
2. **Blank/unwritten-sector fill.** What a never-written SD sector reads is *not stated*; a real
   CF/SD returns the erased-flash pattern (typically `0xFF`), **not** `0xE5`. `0xE5` on this
   board comes only from the FORMAT command explicitly writing it (§2). The directory-card medium
   uses the erased value for its EOF/gap fill; treat `0xFF` as the working value until confirmed.
3. **SD-vs-CF boot at MASTER v4.53.** The monitor's `P` boot reads the CF card; the author notes
   a direct-SD CP/M 3 boot was not yet wired into the (already-full) monitor (§5). The boot path
   for an SD-only machine is the primary open risk for the machine build.
4. **`.imgc` container form.** The store page calls them "sector image" files written with the
   HHD Raw Copy Tool, which strongly implies a plain raw byte-for-byte sector dump; a
   compressed/container form is not ruled out on the page (§7).

---

## 1. Board overview

- A modern S-100 board carrying **two microSD sockets** (drives A: and B:), driven by an
  onboard **ESP32-S3** module. "All access to the SD cards is done via a few one byte commands
  sent to the board. These commands are processed by the ESP32 and send/receive sector data to
  the S100 bus over two parallel ports."
- The S-100 CPU sees only **two 8-bit I/O ports** and a byte-at-a-time handshake — architecturally
  the same shape as the 88-HDSK / iCOM FD3712: a programmed-I/O command engine with a full-sector
  buffer, not a WD177x-style register file.
- **No boot PROM on the board.** CP/M is loaded by the CPU board's ROM monitor from a boot loader
  written to the first sectors of the card (§5).
- Runs **CP/M 3** (banked and non-banked); the SD/CF family also carries CP/M 2.2, MS-DOS, CP/M-68K
  and CP/M-86 images.

## 2. Port map and command protocol

Two ports, DIP-selectable to any 8- or 16-bit address. Defaults:

| Port | Default | Read | Write |
|------|---------|------|-------|
| STATUS | **80H** | status byte (bit 7 = data ready, bit 0 = write-buffer busy) | command byte |
| DATA   | **81H** | next data byte from the ESP32 | next data byte to the ESP32 |

Every command is **two bytes: a lead `33H` (a safety sync), then the command code.** For a
sector read/write the 16-bit sector number (`0-FFFFH`) is also part of the transaction.

| Command | Code | Meaning |
|---------|------|---------|
| `INIT_DRIVE_A`   | **80H** | initialize / mount SD drive A: |
| `INIT_DRIVE_B`   | **81H** | initialize / mount SD drive B: |
| `SEL_DRIVE_A`    | **82H** | (re)select already-initialized drive A: |
| `SEL_DRIVE_B`    | **83H** | (re)select already-initialized drive B: |
| `SET_TRK_SEC`    | **84H** | set current track + sector on current drive (byte order ⚠ open, §3) |
| `READ_SECTOR`    | **85H** | read the current 512-byte sector into the data port |
| `WRITE_SECTOR`   | **86H** | write the current 512-byte sector from the data port |
| `FORMAT_SECTOR`  | **87H** | **fill the current sector with `E5`** |
| `RESET_ESP32`    | **88H** | reset the ESP32 controller |

`FORMAT_SECTOR` writing `0xE5` is a firmware fact (the page: "Format the CURRENT sector with
E5's") — this is the *board command's* fill, distinct from what an untouched sector reads (§ open
item 2).

## 3. The byte handshake and addressing (verbatim driver fragments)

`SD_CARD.Z80` is the authoritative statement of the DI7 / write-busy handshake, and the board
model must reproduce exactly this observable behavior:

```
; Read one byte: wait for DI7 (bit 7) high, then read the data port.
GET_DATA:  IN   A,(SD_CARD_STATUS)   ; wait for character (GPIO_3 and GPIO_21)
           BIT  7,A
           JR   Z,GET_DATA
           IN   A,(SD_CARD_DATA)     ; S100 read-enable resets the ready flip-flop

; Send one byte: wait until the previous byte has been consumed (bit 0 low), then write.
SEND_DATA: IN   A,(SD_CARD_STATUS)   ; wait until any previous character has been read
           BIT  0,A
           JR   NZ,SEND_DATA
           LD   A,C
           OUT  (SD_CARD_DATA),A
```

So: **status bit 7 (DI7) high = a byte is waiting to be read**; reading the data port clears it.
**Status bit 0 high = the last written byte has not been taken yet**; it must fall before the
next write. A 512-byte sector transfer is 512 iterations of the matching loop.

**`SET_TRK_SEC` (84H) addressing** — `SD_CARD.Z80`'s `SET_SECTOR` sends the command, then the
track byte, then the sector byte:

```
SET_SECTOR: ...                      ; TRACK and SECTOR each entered as one byte (0-FFH)
            LD   C,CMD$SET$TRK$SEC   ; 84H
            CALL SEND_CMD            ; 33H lead + 84H
            LD   A,(CURRENT_TRACK)
            LD   C,A
            CALL SEND_DATA           ; track byte FIRST
            LD   A,(CURRENT_SECTOR)
            LD   C,A
            CALL SEND_DATA           ; sector byte SECOND
```

and its "advance to the next sector" step shows the two bytes are one big-endian LBA:

```
            LD   A,(CURRENT_SECTOR)
            LD   L,A                 ; L = sector (low byte)
            LD   A,(CURRENT_TRACK)
            LD   H,A                 ; H = track  (high byte)
            INC  HL                  ; one sector forward: rolls sector->track at 256
```

So the card **LBA = `track*256 + sector`** (256 sectors per track), and the byte offset is
`LBA * 512`. The board model reads the two DATA bytes as track (high) then sector (low).

## 4. Geometry and the CP/M 3 DPB

- **Physical sector: 512 bytes.** Addressed by track + sector; default disk shape assumed by the
  firmware is `0FFH` (255) tracks × `0FFH` (255) sectors/track.
- **CP/M 3 hard-disk DPB** (from the HDISK BIOS page): **512 B/physical sector, 61 sectors/track,
  256 tracks, 2048-byte allocation blocks (BLS = 2K), 1024 directory entries, 1 reserved/system
  track.** A 32 KB directory track holds 1024 × 32-byte entries.
  - Working volume size: 256 × 61 × 512 = **7,995,392 bytes (~7.63 MB)** per CP/M 3 drive.
  - ⚠ Confirm whether "61 sectors/track" is physical 512-byte sectors (making CP/M SPT = 61×4 =
    244 128-byte records) against a real image size before fixing the geometry-file sector count.
- **On-card layout (LBA, 512-byte units):** boot loader at track 0 (§5); **directory table at
  LBA 48 (30H)**; **`CPM3.SYS` from ~LBA 112** onward. (The exact CPM3.SYS LBA and the raw DPB
  bytes are on the build PDF's page images — reconfirm against a real image in the machine build.)

## 5. Boot path

The board has no boot PROM. Booting is the CPU board's **MASTER Z80 monitor** (ROM at
F000–FFFF), via its **`P` command**:

> The monitor reads at least **12 contiguous sectors from track 0, sector 1** into RAM at
> **100H**, then **jumps to 100H** to start CP/M.

`WR_BOOT.COM` writes the CP/M 3 boot loader to track 0; the loader pulls `CPMLDR` → `CPM3.SYS`
→ CCP (the standard CP/M 3 five-stage cold boot).

⚠ At **v4.53** the `P` boot targets the **CF** card; the author notes a direct-SD CP/M 3 boot
was not yet added to the (full) monitor. For an SD-only machine the boot routine that speaks the
80/81 protocol to the Dual SD board must be obtained or derived — **stop and consult before
fabricating a loader.**

## 6. Notes for the emulation

- Model as a two-port programmed-I/O board (template: `IcomFdBoard`), with a 512-byte sector
  buffer, current-drive/track/sector state, the DI7/bit-0 handshake of §3, and the eight commands
  of §2. `FORMAT_SECTOR` writes a 512-byte `0xE5` sector.
- The board addresses its mounted medium **directly by byte offset** — `readAt/writeAt(lba*512,…)`
  with `LBA = track*256 + sector` (§3). It does **not** wrap the medium in `DiskImage` (the
  directory-card medium owns its own geometry).
- Two `[[board.drive]]` sub-units = the two SD sockets.

## 7. Ready-made card images

The **CPM Card Images Store** offers raw sector card images (`.imgc`, "sector image", written to
media with the HHD Raw Copy Tool v1.10). Relevant to the Dual SD board:

| Image | Drives | OS | Banking | Media |
|-------|--------|----|---------|-------|
| **16** | A: (SD only) | CP/M 3 | non-banked | 1 GB SD |
| **18** | A:, B: (SD)  | CP/M 3 | non-banked | 1 GB SD |
| **14** | A:, B: (CF) + C:, D: (SD) | CP/M 3 | non-banked | CF + SD |

**Image-16** — one non-banked CP/M 3 volume on drive A: — is the cleanest single-volume boot
target: usable as a single-partition directory card (one backing file spanning the whole card,
no carving). The image occupies the CP/M volume region (~7.63 MB, §4) of a 1 GB card; the rest is
unused. System tracks come from `WR_BOOT.COM`, so a *bootable* volume cannot be blank-created —
only data volumes can (consistent with the directory-card CREATE caveat).
