# Vector Graphic Monitor 4.1 (`builtin:vgmon41`)

The Extended Systems Monitor. It is 2K of Z80 code at `E000h`. Its console is the
[FlashWriter II](../../docs/boards/vector-fwii.md). Vector's PROM sheet lists it for the 3030, the
2800 and the VIP.

- **Load address:** `E000h`. Entry at `E000h`.
- **Image:** `E000`–`E7FF`, 2048 bytes, CRC32 `80011EC9`.
- **Stack:** `FFD0h`, so RAM must be at `FC00`–`FFFF`.
- **Boot:** `B` jumps to `F800`, the boot PROM of a [`vgmdc`](../../docs/boards/vector-micropolis.md)
  board. `V` jumps to `E800` and `W` jumps to `E802`.

The commands are in `reference/Vector Monitor 4.1.md`.

## Source

deramp.com has the manual with a printed listing, but no source file. `source/MON41.ASM` is the
listing typed from the scan, using `MON43.ASM` as the starting point. It was assembled with M80 and
L80 in a guest. `source/MON41.PRN` and `source/MON41.HEX` are the output. `VGMON41.BIN` matches a
separate reading of the listing's hex column, byte for byte.

## Checksum

Vector's PROM sheet gives sum 16. The `Q` rule on this image gives 17 over `E000`–`E7FF`. The listing
does not print `E7FB`–`E7FF`, so these bytes are zero here. The cause of the difference of 1 is not
known. The maintainer accepted this image.
