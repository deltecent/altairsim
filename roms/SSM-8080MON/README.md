# SSM 8080 System Monitor V1.0 — source only

The 8080 system monitor from **SSM Microcomputer Products** (formerly Solid State
Music), written by **C. E. Ohme**. This is the monitor the SSM PB1 EPROM
programmer manual calls `MONIT`: its EPROM-burner routines end with `JMP F021h`
(the monitor's warm re-entry) and its verify routines print through `CALL F009h`
(the monitor's console-out). See [`../../docs/boards/ssm-pb1.md`](../../docs/boards/ssm-pb1.md)
and [`examples/pb1`](../../examples/pb1).

- **Version 1.0** — C. E. Ohme, SSM Microcomputer Products.
- **Origin of this file:** OCR'd from the printed listing by B. Beech, April 2014.
- **Console:** serial-A on ports **0** (status) and **1** (data), the SSM IO2/IO4
  serial board convention — status bit 0 = ready. (Serial-B is ports 2/3; a
  parallel keyboard and the VB3 video board are also driven.)

## Jump table (`ORG F000h`)

| Address | Entry |
|---|---|
| `F000h` | `BEGIN` — cold start |
| `F003h` | `CI` — console in |
| `F006h` | `RI` — reader in |
| `F009h` | `CO` — console out |
| `F00Ch` | `PO` — punch out |
| `F00Fh` | `LO` — list out |
| `F012h` | `CSTS` — console status |
| `F015h` | `IOCHK` / `F018h` `IOSET` / `F01Bh` `MEMCK` / `F01Eh` `STRNG` |
| `F021h` | `REENT` — warm re-entry (the PB1 manual's `MONIT`) |

## Status: source only, no built-in ROM yet

**There is no `.HEX` here on purpose.** A built-in ROM is a hardware fact — its
bytes get a CRC in `docs/roms.md` and a test — so the image has to come from the
assembler these sources were actually written for, and that is not yet settled.
What is known so far:

- The sources use **C-style operators** — `~` for NOT (`FALSE EQU ~TRUE`) and `|`
  for OR (`DB LF,'*' | 80H`). Microsoft **M80 rejects these** (it wants `NOT`/`OR`),
  so M80 is not the assembler these were prepared for.
- They also assume **16-bit two's-complement arithmetic**: `TRUE=0FFFFH`, so
  `FALSE=~TRUE` is meant to be `0`. An assembler with wider integers computes
  `~0FFFFH = -65536` and then overflows a `DEFB`.
- **This V1.0 source has an internal conflict of its own:** it both `EQU`s
  `ADSCS/ADSCR/ADIOB/ADUST` into a "System Configuration Package" at `F600h` and
  defines them as local labels, which every assembler flags as a multiple
  definition. The later Z80 V1.10 (see [`../SSM-Z80MON`](../SSM-Z80MON)) keeps
  only the local labels, so it does not have this conflict.

Once the assembler is identified, the assembled `.HEX` goes here and the directory
becomes a built-in ROM automatically (`cmake/embed_roms.cmake` picks up any image
in a `roms/<NAME>/` directory).
