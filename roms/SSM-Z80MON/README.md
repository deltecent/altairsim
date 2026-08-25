# SSM Z80 Monitor V1.10 — source only

The Z80 monitor from **SSM Microcomputer Products** (formerly Solid State Music),
programmed by **C. E. Ohme** (1977), with later work by P. Dennis, M. T. Wright,
and D. M. Fischler (1980). It shares the 8080 monitor's external jump table, so it
serves the same role as the SSM PB1 EPROM programmer manual's `MONIT`: warm
re-entry at `F021h`, console-out at `F009h`. See
[`../SSM-8080MON`](../SSM-8080MON), [`../../docs/boards/ssm-pb1.md`](../../docs/boards/ssm-pb1.md),
and [`examples/pb1`](../../examples/pb1).

- **Version 1.10** — C. E. Ohme et al., SSM Microcomputer Products, ©1980.
- **Origin of this file:** OCR'd from the printed listing by B. Beech, April 2014.
- **Console:** serial-A on ports **0** (status) and **1** (data), the SSM IO-4
  serial board convention — status bit 0 = ready. It also drives serial-B (ports
  2/3), a parallel keyboard, and the SSM VB3 video board.

## Jump table (`ORG F000h`)

| Address | Entry |
|---|---|
| `F000h` | `BEGIN` — cold start |
| `F003h` | `CI` — console in |
| `F006h` | `RI` — reader in |
| `F009h` | `CO` — console out |
| `F00Ch` | `PR` — printer / `F00Fh` `LD` — load |
| `F012h` | `CSTS` — console status |
| `F015h` | `IOCHK` / `F018h` `IOSET` / `F01Bh` `MEMCK` / `F01Eh` `STRNG` |
| `F021h` | `REENT` — warm re-entry (the PB1 manual's `MONIT`) |
| `F024h` | `JVTR` — jump-vector entry |

## Status: source only, no built-in ROM yet

**There is no `.HEX` here on purpose.** A built-in ROM is a hardware fact — its
bytes get a CRC in `docs/roms.md` and a test — so the image has to come from the
assembler these sources were actually written for, and that is not yet settled.
What is known so far:

- The source uses **C-style operators** — `~` for NOT (`FALSE EQU ~TRUE`,
  `IF ~INTERL`) and `|` for OR. Microsoft **M80 rejects these** (it wants
  `NOT`/`OR`), so M80 is not the assembler this was prepared for.
- It assumes **16-bit two's-complement arithmetic**: `TRUE=0FFFFH`, so
  `FALSE=~TRUE` is meant to be `0`. An assembler with wider integers computes
  `~0FFFFH = -65536`, and `DEFB (INTERL*128)+53H` then overflows a byte — the one
  place this source stops short of a clean assembly under a modern C-operator
  assembler (`zmac`).

Unlike the 8080 V1.0 source, this file is internally consistent: `ADSCS/ADSCR/
ADIOB/ADUST` are defined once, as local labels, with no `F600h` "System
Configuration Package" `EQU` block to collide with.

Once the assembler is identified, the assembled `.HEX` goes here and the directory
becomes a built-in ROM automatically (`cmake/embed_roms.cmake` picks up any image
in a `roms/<NAME>/` directory).
