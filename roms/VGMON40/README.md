# Vector Graphic Monitor 4.0 (`builtin:vgmon40`)

The monitor PROM for a Vector Graphic machine with a Micropolis floppy controller. It is 2K of
Z80 code at `E000h`. Its console is the [FlashWriter II](../../docs/boards/vector-fwii.md).

- **Load address:** `E000h`. Entry at `E000h`.
- **Image:** `E000`–`E7FF`, 2048 bytes, CRC32 `95D8858F`, checksum `F8`.
- **Boot:** `B` jumps to `F800`, the boot PROM of the
  [`vgmdc`](../../docs/boards/vector-micropolis.md) board.

`ROM Checksums.pdf` lists checksum `F8` for this Monitor, for a 4 MHz processor and a Micropolis
disk.

## Use it

On a [`vgzcb`](../../docs/boards/vector-zcb.md) board:

```toml
[[board.socket]]
at    = 0xE000
mount = "builtin:vgmon40"
```

## Source

`VGMON40.BIN` is made from `source/MON40.HEX`, from deramp.com. The HEX file starts at address
`0000` and has no holes, so the two hold the same 2048 bytes. The HEX file is in a subfolder
because the build embeds the first image file in a ROM folder. There is no source listing for
this image.
