# CDBL — Combo Disk Boot Loader (`builtin:cdbl`)

A single 256-byte 1702A PROM that boots **either** the Altair 88-DCDD 8″ floppy
system **or** the 88-MDS 5.25″ minidisk system, detecting which drive is
attached at run time. It is Martin Eberhard and Mike Douglas's replacement for
the two separate MITS PROMs — [`builtin:dbl`](../DBL) (8″ only) and
[`builtin:mdbl`](../MDBL) (minidisk only) — and works identically to them where
they overlap, with several fixes on top.

- **Version 3.00**, 16 January 2016 — Martin Eberhard & Mike Douglas.
- **Load address:** `FF00h` (a standard boot PROM socket, 177400 octal). v3.00
  is position-independent and will run from any 256-byte page except page 0.
- **Decoded image:** `FF00`–`FFF4`, 245 bytes, CRC32 `0558293E`.

## What it does

1. Copies itself into RAM at `4C00h` (046000 octal) and runs there — the 1702A
   is too slow to execute from, and some 8800b Turnkey modules disable the PROM
   on any `IN`. It issues no `IN`/`OUT` until it is in RAM.
2. Detects the drive by probing for sector 16: present → 8″ (32 sectors/track),
   absent → minidisk (16 sectors/track).
3. Reads the boot file into RAM from address 0 (sectors interleaved 2:1, even
   sectors then odd), then jumps to `0000h`.

Because its RAM page sits at `4C00h`, the loaded boot file is limited to 19 KB,
and the machine needs ≥ 20 KB of zero-wait-state RAM from address 0.

### Error codes

On an unrecoverable error CDBL lights the front-panel INTE lamp, stores the
ASCII code at address 0 and the offending address at 1–2, and prints the code
to every standard Altair terminal until you STOP/RESET:

| Code | Meaning |
|---|---|
| `C` | Checksum / marker-byte error (retried 15× first) |
| `M` | Memory error — could not write RAM (bad, read-only, or protected) |
| `O` | Overlay error — the boot file would overrun CDBL's own RAM page |

## Use it

```toml
[[board.region]]
type  = "rom"
at    = 0xFF00
mount = "builtin:cdbl"
```

## Files here

| File | What it is |
|---|---|
| `CDBL.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `CDBL.ASM` | Eberhard/Douglas source. |
| `CDBL.PRN` | Assembler listing — the byte-for-byte record CDBL's provenance is checked against. |
| `CDBL Manual.pdf` | The manual (v3.00), retained as downloaded. This README is distilled from it. |

**Source:** deramp.com, *M. Eberhard Improved ROMs*. Provenance and the CRC32
test are in [`docs/roms.md`](../../docs/roms.md).
