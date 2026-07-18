# HDBL — Hard Disk Boot Loader (`builtin:hdbl`)

A 256-byte PROM that boots from an **Altair 88-HDSK** hard disk (the Pertec
D3422 cartridge/fixed-platter drive, and up to four platters on the D3462).
Martin Eberhard's loader for a subsystem MITS never shipped a boot PROM for.

- **Version 2.00**, 13 August 2014 — Martin Eberhard.
- **Load address:** `FC00h` (176000 octal — slot E of an 88-PMC, or L1 of a
  Turnkey module).
- **Decoded image:** `FC00`–`FCFE`, 255 bytes, CRC32 `796FCA9B`.

## What it does

Progress and error messages print on a 6850-based terminal at ports `10h`/`11h`
(88-2SIO port A, a Turnkey serial port, or an 88-UIO). HDBL reads the Pack
Descriptor Page (track 0, side 0, sector 0), which names the starting boot page
and page count, loads the boot file into RAM from address `0000h`, and jumps
there.

- **Boot platter select — Sense Switch 3 (A11):** down (0) boots the removable
  cartridge, up (1) boots the fixed platter. A11 was chosen so the switch does
  not collide with the `A11:A8` input-device selection the tape loaders read.
- **Requirements:** the standard build relocates its stack and code to page
  `BF00h`, so it wants ≥ 48 KB of RAM; the 88-HDSK controller must be at its
  standard I/O ports 280–287 octal (`A0h`–`A7h`).

On any error HDBL lights the front-panel INTE lamp and prints `LOAD ERR` with a
code; the error byte is left in memory for inspection.

## Use it

```toml
[[board.region]]
type  = "rom"
at    = 0xFC00
mount = "builtin:hdbl"
```

## Files here

| File | What it is |
|---|---|
| `HDBL.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `HDBL.ASM` | Eberhard source. |
| `HDBL.PRN` | Assembler listing — the byte-for-byte record HDBL's provenance is checked against. |
| `HDBL Manual.pdf` | The manual (v2.00), retained as downloaded. This README is distilled from it. |

**Source:** deramp.com, *M. Eberhard Improved ROMs*. Provenance and the CRC32
test are in [`docs/roms.md`](../../docs/roms.md).
