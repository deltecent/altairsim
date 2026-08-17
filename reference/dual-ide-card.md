# S100Computers IDE-AB (CompactFlash) Board

The **"IDE-AB CF+ESP32"** board (John Monahan, s100computers.com) is a combination card: an
**8255-based IDE/CompactFlash controller** (CP/M drives **A:/B:**) and the byte-identical
**Dual SD ESP32 engine** (drives **C:/D:**) on one physical board. This reference distills the
**IDE/CompactFlash half** — the part `altairsim` models as the `dualide` board. The SD half is
the [[dual-sd-card]] reference and the `dualsd` board; the two compose into the full card.

Sources (John Monahan's S-100 archive, and the author's distribution):
- **CP/M 3 BIOS source — the definitive authority.** `HIDE3.ASM` (the combination hard-disk
  driver: IDE/CF **and** SD in one BIOS), with `HBOOT3.ASM` (cold-boot loader) and
  `HDRVTBL3.ASM` (drive table), John Monahan, dated **1/24/2026**, from the author's
  "IDE-AB CF+ESP32-CD SD" distribution. This is the actual source for the shipping combination
  `CPM3.SYS`. It pins the 8255 port map, the IDE control-line encoding, the ATA register file,
  the command set, the status-polling sequences, the 16-bit sector transfer, the LBA math and
  the DPB below. (Not a fetchable page; obtained directly from the author. Nothing redistributed.)
- Boot monitor **MASTER Z80 (V6.6)** — the CPU board's ROM monitor. Its **`P` command**
  (`HBOOTCPM`, `roms/MASTER0/MASTER0.z80:1328`, menu line `roms/MASTER0/MASTER0.z80:2750`) boots
  CP/M 3 from the CF card; the sibling `I` command boots from SD (see [[dual-sd-card]] §5).
- Ready images: **CPM Card Images Store** (`.imgc` — HDD Raw Copy Tool containers, LZO-compressed;
  decode with `unimgc` before mounting, exactly as for the SD images — [[dual-sd-card]] §7):
  - **Image-1** `(Image-1) IDE-CF-AB Non-Banked CPM3.imgc` — A:/B: on CF only.
  - **Image-14** `(Image-14) IDE-CF + Dual SD Card Non-Banked CPM3.imgc` — A:/B: on CF + C:/D: on SD.

**Licensing:** no licence is stated. The board and images are John Monahan's hobbyist/educational
S-100 project; the CP/M 3 payload is Digital Research code. Nothing here is redistributed — this
is a text distillation citing the source. (Project rule: record the licence, never gate on it.)

---

## 1. Why a card is portable between `dualide` and `dualsd`

The whole point of the combination BIOS is that **the same card image works on either interface**.
`HIDE3.ASM` proves it:

- **DPB identical.** `IDEHD$DPB` and `SD$DPB` are both `DPB 512,61,256,2048,1024,1,8000H`
  (512 B/sector, 61 sectors/track, 256 tracks, 2 KB blocks, 1024 dir entries, 1 reserved track,
  permanent-drive flag). Same volume geometry, ~7.63 MB per drive.
- **LBA identical.** The CF `wrlba` builds `LBA = (@SECT+1) | @TRK_lo<<8 | @TRK_hi<<16`
  = `(@SECT+1) + @TRK·256`; the SD `SD$wrlba` builds `@TRK·256 + (@SECT+1)`. Same value.
- **Byte order identical.** The CF 16-bit transfer writes the **low** byte (8255 port A) at even
  offsets and the **high** byte (port B) at odd — the same on-disk order as the SD engine's byte
  stream. The byte offset is `LBA·512` on a raw medium in both.

So a `.img`/`.geo` card written through one half reads back byte-for-byte through the other. The
unit test `tests/test_dualide.cpp` pins exactly this round trip (`dualide` writes, `dualsd` reads).

## 2. The 8255 port map

Five contiguous I/O ports, strap-relocatable (defaults from `HIDE3.ASM`):

| Port | Default | Role |
|------|---------|------|
| IDEportA    | **30H** | 8255 A — IDE data **low** byte (out), register read-back (in) |
| IDEportB    | **31H** | 8255 B — IDE data **high** byte (16-bit sector transfers) |
| IDEportC    | **32H** | 8255 C — the IDE control lines, driven as a group (§3) |
| IDEportCtrl | **33H** | 8255 mode config: `READcfg8255=0x92` (C out, A/B in) / `WRITEcfg8255=0x80` (all out) |
| IDEDrive    | **34H** | bit 0 = drive select: `0` → A:, `1` → B: |

## 3. The control lines and the ATA register file

Port C carries eight control lines (`HIDE3.ASM`):

```
A0=01  A1=02  A2=04  CS0=08  CS1=10  WR=20  RD=40  RST=80
```

With **CS0** asserted the register address is `portC & 0x0F`:

| Register | Value | | Register | Value |
|----------|-------|-|----------|-------|
| REGdata      | `08` | | REGcylinderLSB | `0C` |
| REGerr       | `09` | | REGcylinderMSB | `0D` |
| REGseccnt    | `0A` | | REGshd         | `0E` |
| REGsector    | `0B` | | REGcommand / REGstatus | `0F` |

(`REGastatus = 17` uses CS1.) The CPU never sees the IDE bus directly — every register access is
a hand-built strobe on port C:

- **Write a register (`IDEwr8D`):** config 8255 to all-out; put the byte on port A; drive port C
  to the register address; **pulse WR high** (`| 0x20`) then low. On the WR rising edge the board
  latches port A into that register. REGcommand dispatches a command; the geometry registers build
  the LBA.
- **Read a register (`IDErd8D`):** drive port C to the register address; **pulse RD high**
  (`| 0x40`); read port A. On the RD rising edge the board presents the register's value on port A.

**16-bit sector transfer** uses REGdata (`0x08`):
- **Write (`WRSEC1`)**, 256 times: low byte OUT port A, high byte OUT port B, then pulse WR on
  REGdata — one 16-bit word per pulse. The 256th word completes the 512-byte sector.
- **Read (`MoreRD16`)**, 256 times: pulse RD on REGdata, then IN port A (low), IN port B (high).

**LBA** = `REGsector | REGcylinderLSB<<8 | REGcylinderMSB<<16 | (REGshd & 0x0F)<<24`. `COMMON$INIT`
sets REGshd = `0xE0` (LBA mode, single drive, head 0), so its low nibble is 0.

Commands (written to REGcommand): `RECAL=10 READ=20 WRITE=30 INIT=91 SPINDOWN=E0 SPINUP=E1
IDENTIFY=EC`.

## 4. The status byte and the presence gate

The BIOS polls REGstatus (`IDEwaitnotbusy` / `IDEwaitdrq`):

- `IDEwaitnotbusy` waits for `(status & 0xC0) == 0x40` — **BSY clear, RDY set**.
- `IDEwaitdrq` waits for `(status & 0x88) == 0x08` — **DRQ set, BSY clear**.
- After a transfer, `CHECK$RW` reads REGstatus and tests **bit 0 = ERR**.

`altairsim` models an instant controller: **BSY is always 0**, **RDY is always 1** while a card is
present, **DRQ** is set during a transfer phase, and **ERR** reflects the last command. That
answers both waits with no timing.

**An empty socket floats the IDE bus to `0xFF`** — every register, status included, reads `0xFF`.
`COMMON$INIT` then spins on BSY (`0xFF & 0xC0 == 0xC0`, never `0x40`), times out, and reports the
drive **"not Present"** (`MSG$INIT$ERR`). This is the correct absent-drive behavior; a present
card with a valid sector answers `0x40`.

## 5. Boot path

The board has **no boot PROM**; the CPU board's **MASTER Z80 monitor** loads CP/M. The **`P`
command** (`HBOOTCPM`) does the CF boot:

1. `INITILIZE_IDE_BOARD` — 8255 config, hard-reset pulse on RST, select the MASTER device
   (REGshd = `0xE0`), poll status; abort ("no drive") if it never goes ready.
2. Load `SEC_COUNT = 12` sectors starting at **LBA 1** (`CPMLDR.COM`, "always TRK 0 SEC 2, LBA
   mode") into RAM at **100H** — for each: `SET_LBA` (writes the sector/cyl registers), then
   `READSECTOR` (issue READ, wait DRQ, shift 256 words).
3. Check `(100H) == 0x31` (a `LD SP,nn`), then `JP 100H` — the CP/M 3 five-stage cold boot
   (`CPMLDR` → `CPM3.SYS` → CCP).

`IDEDrive` (34H) is left at 0 by the monitor, so `P` boots CF drive 0 (A:).

## 6. Notes for the emulation

- Model the 8255 as three latches plus an edge-triggered engine on port C: the WR rising edge
  writes a register (or a data word), the RD rising edge presents one. No `tick()` work, nothing
  on the `EventQueue` — the controller is synchronous.
- Address the mounted medium **directly by byte offset** — `readAt/writeAt(lba·512, …)` on a raw
  `MediaFile` — never wrapping it in `DiskImage`. A `cardimg` card owns its own geometry, and it
  is the **same** `.img`/`.geo` medium the SD half uses (§1).
- Two `[[board.drive]]` sub-units = the two CF sockets (drive 0 → A:, drive 1 → B:).
- A never-written but **in-range** sector returns the medium's erased fill (`0xFF` from a
  `cardimg` card), ERR clear; a read **past the card** fails `readAt`, so DRQ never sets and the
  BIOS aborts. Filling `0xFF` is the card's job, not the board's.

## 7. Ready-made card images

Same `.imgc` container and `cardimg` storage as the SD side ([[dual-sd-card]] §7): decode the
`.imgc` to raw with `unimgc`, then pair a (possibly truncated) `.img` with a `.geo` sidecar
declaring the full card. `MOUNT … CREATE format=dualide` authors a blank card at the shared
geometry (512-byte sectors, 15616 sectors ≈ 7.99 MB); `format=dualsd` is the same template.

| Image | Drives | OS | Media |
|-------|--------|----|-------|
| **1**  | A:, B: (CF) | CP/M 3 (non-banked) | CompactFlash |
| **14** | A:, B: (CF) + C:, D: (SD) | CP/M 3 (non-banked) | CF + SD |

**Image-1** is the boot target for `examples/dualide` (CF only); **Image-14** for
`examples/dualidesd` (CF + SD). System tracks come from a real image; a bootable card cannot be
blank-created.
