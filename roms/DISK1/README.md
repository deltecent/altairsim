# DISK1 — CompuPro Disk 1 boot EPROM (`builtin:disk1`)

The boot EPROM of the **CompuPro Disk 1** floppy disk controller. The part is a 2716 (2 KB).
It holds eight boot routines of 256 bytes each. This image is a dump of a factory EPROM.

- **Run address:** `0000h`. The board shows one routine at `0000`–`00FF` until the guest
  turns the EPROM off.
- **Decoded image:** `0000`–`07FF`, 2048 bytes, CRC32 `EBA8EE41`.

## The routines

Switches S2-1 and S2-2 and jumper J17 pick the routine. The `routine` property of the `disk1`
board is that selection. The default is 4.

| Routine | Processor | Console code in C | Console that CompuPro CP/M 2.2N then uses |
|---|---|---|---|
| 0, 2, 3 | 8086 | | |
| 1 | 8086 (a copy of routine 0) | | |
| 4 | 8080, 8085, Z80 | 0 | Interfacer at `00`/`01` |
| 5 | 8080, 8085, Z80 | 1 | See below |
| 6 | 8080, 8085, Z80 | 2 | System Support 1 serial channel at `5C`/`5D` |
| 7 | 8080, 8085, Z80 | 3 | Interfacer 3 or 4 at `10` |

The table in the *Disk 1 Technical Manual* (§3.1 of the reference) gives the same split.
Routines 4–7 are one routine. They differ in one byte only, at `00FD`, which is the console
code that the routine passes in register C.

The manual gives routine 5 to the Disk 1's own serial line. The CP/M 2.2N BIOS has no driver
for that line. With C = 1, it reads its console from an Interfacer at `10`/`11`.

Only routines 4–7 run in altairsim. There is no 8086 processor board.

## What routine 4 does

1. It waits in a delay loop.
2. It sets the DMA address to `000100h` (`OUT C2`, three bytes, the high byte first).
3. It sends Specify (`03 8F 24`) and Recalibrate (`07 00`) to the µPD765A. It waits for the
   interrupt bit (port `C2` bit 7).
4. It sends Sense Interrupt Status (`08`). It accepts ST0 = `20h` and cylinder 0. Any other
   result waits and starts again.
5. It reads track 0, head 0, sectors 1–4 of 128 bytes into `0100h`
   (`06 00 00 00 01 00 04 07 80`).
6. It accepts the result when ST0 is `40h` and ST1 is `80h` (end of cylinder). This is a
   good read on the Disk 1, because the board has no terminal count. Any other result waits
   and starts again.
7. At `00FC`, it puts the console code in register C and writes `00h` to port `C3`. D0 = 0
   switches the EPROM off, and D7 = 0 leaves the serial line at MARKING. The processor then
   runs the next instruction from RAM at `0100h`.

CompuPro's source `HMXFPROM.ASM` (May 1983), on the CP/M 2.2N disk, is a different boot
routine of the same design. It sends Specify `03 6F 24` and reads all 26 sectors.

## Use it

The `disk1` board loads this EPROM itself. It is not a ROM that you mount. The Disk 1 machine file
in the *User Manual* (boards chapter, `disk1`) starts it with `RUN 0`.
The board reference is [`docs/boards/compupro-disk1.md`](../../docs/boards/compupro-disk1.md).

## Files here

| File | What it is |
|---|---|
| `DISK1.HEX` | The 2 KB image in Intel HEX, 16 bytes to a record. The build embeds this file. |
| `DESC` | The line that `SHOW ROMS` prints. |

**Source:** `CompuPro_Disk1_80_86.BIN`, fetched 2026-10-15 from the deramp.com archive,
<https://deramp.com/downloads/mfe_archive/010-S100%20Computers%20and%20Boards/00-CompuPro-Godbout-Viasyn/10-CompuPro%20S100%20Boards/171%20Disk%201/>.
`DISK1.HEX` holds the same 2048 bytes.

| File | SHA-256 |
|---|---|
| `CompuPro_Disk1_80_86.BIN` | `0559d5cb5a797e733b393b36d4b4c791d8f44507cf1072f756fe8723cc33ed6d` |

The same folder has `CompuPro_Disk1_68_85_86.BIN` (SHA-256
`4da033813518192b62bbc42093c5fe6ebccce5564aff3d0f21ffadfd306f625c`). It is the same image
except routine 1, which holds other code, probably for a 68000. altairsim has no 68000
processor, so that image is not used.
