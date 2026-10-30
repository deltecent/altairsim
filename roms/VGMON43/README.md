# Vector Graphic Monitor 4.3 (`builtin:vgmon43`)

The monitor PROM of a Vector Graphic machine. It is 4K of Z80 code at `E000h`. Its console is
the [FlashWriter II](../../docs/boards/vector-fwii.md): it writes each character into the
board's memory at `F000h` and reads the keyboard at ports `00h` and `01h`.

- **Load address:** `E000h`. Entry at `E000h`.
- **Image:** `E000`–`EFFF`, 4096 bytes, CRC32 `29A0FCEE`.
- **Stack:** `FFD0h`. The Monitor also keeps its cursor and its scratch data in high RAM, so RAM
  must be at `F800`–`FFFF`.

## What it does

- It prints a banner in block graphics, then the prompt `Mon>`.
- It takes one letter for each command and prints the name of the command. `R` shows the
  registers. `DE000E03F` shows the memory from `E000` to `E03F`.
- `B` boots from a floppy disk. It needs the Dual-Mode controller, which altairsim does not
  have yet.

These facts come from running the image, from its disassembly and from the listing.

## Use it

```toml
[[board.region]]
type  = "rom"
at    = 0xE000
mount = "builtin:vgmon43"
```

`altairsim vector` runs this ROM on a FlashWriter II ([`machines/vector.toml`](../../machines/vector.toml)).

## Source

`VGMON43.BIN` is a byte-for-byte copy of `mon43.bin` from `oasis.zip` on schorn.ch. The image
has no check against a real PROM. See [`docs/roms.md`](../../docs/roms.md).

The `source/` folder holds source made from the printed listing, *Vector MZ Monitor, Version
4.3* (R. S. Harp, 7/16/79, modified 1/12/81). These files are in a subfolder because the build
embeds the first image file in a ROM folder. `MON43.HEX` must not replace `VGMON43.BIN`.

| File | What it is |
|---|---|
| `MON43.ASM` | The source for M80 and L80. Its first lines list each change from the printed text. |
| `MON43.PRN` | The M80 listing. Its addresses are the printed addresses. |
| `MON43.HEX` | The assembled output, `E000`–`E7FF`, 2048 bytes. |

`MON43.HEX` is the same as the first 2048 bytes of `VGMON43.BIN`. Their checksum is `43`, the
value that `ROM Checksums.pdf` gives for the 4.3 Monitor.

- The listing ends at `E7EE`. The source adds `E7EF`–`E7FF` from the image: 16 zero bytes, then
  `66` at `E7FF`.
- The listing does not cover `E800`–`EFFF`, so that part of the image has no source.
- The comments and the label names were read from the scan. The assembly does not check them.

To assemble the source in a CP/M machine:

1. Type `M80 MON43,MON43=MON43.ASM`.
2. Type `L80 /P:E000,MON43,MON43/N/X/E`.
3. Answer `Y` when L80 asks to move above loader memory. L80 writes `MON43.HEX`.
4. Answer `N` when L80 asks again. L80 returns to CP/M.

[Assembling a ROM](../../docs/devguide/assembling-roms.md) gives the setup.
