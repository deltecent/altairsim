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

These facts come from running the image and from its disassembly. There is no listing in this
folder.

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
has no listing and no check against a real PROM. See [`docs/roms.md`](../../docs/roms.md).
