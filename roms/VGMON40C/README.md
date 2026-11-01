# Vector Graphic Monitor 4.0c (`builtin:vgmon40c`)

Vector's Monitor for an MZ with a serial terminal. It is Z80 code at `E000h`. Its console is
port A of a [Bitstreamer II](../../docs/boards/vector-bitstreamer.md) at port `02`: `03h` is
status and `02h` is data.

- **Load address:** `E000h`. Entry at `E000h`.
- **Image:** `E000`–`E64F`, 1616 bytes, CRC32 `6662335C`, checksum `78`.
- **Boot:** `B` jumps to `F800`, the boot PROM of the
  [`micropolis`](../../docs/boards/vector-micropolis.md) board.

`ROM Checksums.pdf` lists checksum `78` for this Monitor. The image holds only the bytes that
assemble. A socket on the [`zcb`](../../docs/boards/vector-zcb.md) board treats the image as a
2K PROM, and `E650`–`E7FF` reads `FF`.

## Use it

A `bitstreamer` at `02` decodes the ports of the `zcb` serial and parallel blocks. Move those
blocks to free ports:

```toml
[[board]]
type          = "zcb"
serial_port   = 0x0C
parallel_port = 0x18

  [[board.socket]]
  at    = 0xE000
  mount = "builtin:vgmon40c"
```

## Source

`VGMON40C.BIN` is made from `source/MON40C.HEX`, from deramp.com. `source/MON40C.MAC` is the M80
source from the same folder, headed *Vector MZ Monitor, Version 4.0c, R. S. Harp, 7/16/79,
System MZ version, only for serial console*. The source was not assembled here. These files are
in a subfolder because the build embeds the first image file in a ROM folder.
