# Vector Graphic Monitor 2.0C (`builtin:vgmon20c`)

The Vector 1 Monitor for a serial terminal, set up for the `vgmz` machine. It is 8080 code at
`C000h`. Its console is the serial port of a Bit Streamer at port `02`: `03h` is status and `02h` is
data. Its stack is at `CFD0h`, so 1K of RAM must be at `CC00h`.

- **Load address:** `C000h`. Entry at `C000h`.
- **Image:** `C000`–`C3D5`, 982 bytes, CRC32 `F1103B3D`.
- **Boot:** `B` jumps to `D800`, the boot PROM of a [`vgmdc`](../../docs/boards/vector-micropolis.md)
  board at its 48K address.

## Source

`source/MON20C-E900.HEX` is the file from deramp.com. Mike Douglas re-created the source in 2015.
That file has `E900`, a North Star boot address, in the `B` entry at `C039h`. `VGMON20C.BIN` is this
HEX file with the address changed to `D800`, one of the options in the source.
`source/MON20C.ASM` has that option selected. The source was not assembled here.

Vector's PROM sheet lists Monitor 2.1C, not 2.0C, for a 48K serial machine. We do not have 2.1C.
