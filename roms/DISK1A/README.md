# DISK1A — CompuPro Disk 1A boot EPROM (`builtin:disk1a`)

The boot EPROM of the **CompuPro Disk 1A** floppy disk controller. The part is a 2764 (8 KB).
It holds sixteen boot routines of 512 bytes each.

- **Run address:** `0000h`. The board shows one routine at `0000`–`01FF` until the guest
  turns the EPROM off.
- **Decoded image:** `0000`–`1FFF`, 8192 bytes, CRC32 `93DBAEF4`.
- **Part number:** the 1985 Viasyn *Disk 1A Technical Manual* (board 203 Rev F) gives EPROM
  Version 1.6 as Part #291B. The name of this dump is `291B_1.HEX`. The image holds no version
  string.

## The routines

Switch S1 positions 2–5 pick the routine. The `routine` property of the `disk1a` board is
that switch.

| Routine | Processor | Search order |
|---|---|---|
| 0 | 8080, 8085, Z80 | 8″ drive 0, then the DISK 3 hard disk |
| 1 | 8080, 8085, Z80 | The DISK 3 hard disk only |
| 2 | 8080, 8085, Z80 | 8″ drive 0, then a 5.25″ drive as drive 2 |
| 3 | 8080, 8085, Z80 | 5.25″ drive 0, then the DISK 3 hard disk |
| 4–7 | 8086 | The same four orders |
| 8–11 | 68000 | The same four orders |
| 12–15 | 32016 | The same four orders |

Only routines 0–3 run in altairsim. There is no 8086, 68000 or 32016 processor board.

## What routine 0 does

1. It switches on all the motors (`OUT C3,FF`) and waits.
2. It copies itself to `8000h` and runs from there.
3. It writes a DISK 3 vector at `005Dh` and zeros at `0050h` and `0052h`. The writes go to
   the RAM under the EPROM.
4. It switches the EPROM off (`OUT C3,FE`).
5. It sends Specify (`03 8F 24`) and Recalibrate (`07 00`) to the µPD765A. It waits for the
   interrupt bit (port `C2` bit 7) and then sends Sense Interrupt Status.
6. It sets the DMA address to `000100h`. It reads track 0, head 0, sectors 1–26 of 128
   bytes into `0100h` (`06 00 00 00 01 00 1A 07 80`).
7. It accepts the result when ST0 is `40h` and ST1 is `80h` (end of cylinder). This is a
   good read on the Disk 1A, because the board has no terminal count.
8. It reads the sense switch S3-1 (port `C2` bit 2). It puts 2 plus that bit in register C
   and jumps to `0100h`.

A failed read makes the routine try the DISK 3 at port `90h`. After that, it starts again.

## Use it

The `disk1a` board loads this EPROM itself. It is not a ROM that you mount. The Disk 1A machine
file in the *User Manual* (boards chapter, `disk1a`) starts it with `RUN 0`.
The board reference is [`docs/boards/compupro-disk1a.md`](../../docs/boards/compupro-disk1a.md).

## Files here

| File | What it is |
|---|---|
| `291B_1.HEX` | The 8 KB image in Intel HEX. The build embeds this file. |
| `DESC` | The line that `SHOW ROMS` prints. |

**Source:** `291B_1.HEX` from Herb Johnson's ROM collection,
<https://www.retrotechnology.com/herbs_stuff/roms/291B_1.HEX>, fetched 2026-10-14. CompuPro's
own source for an earlier revision of routine 0 is `ROM1.ASM` (PROM1A1, August 1984). That
source uses the same ports and the same Read Data command. Its Specify bytes are `03 DF 12`.
