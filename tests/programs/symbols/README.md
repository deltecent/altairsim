# Symbol-file fixtures — ground truth for `core/symbols`

These are real files from the CP/M toolchain that ships in this repo's disk images,
generated **inside the machine** rather than written by hand — the same rule the Host
Bridge utilities follow, so the format cannot drift from what a guest actually produces.

## Provenance

Generated 2026-07-17 by booting `cpm22b23-56k.dsk` (now `tests/media/cpm/`)
(which carries `M80.COM`, `L80.COM`, `MAC.COM`) in the `default` machine, assembling and
linking the sources through the Host Bridge:

| File | Tool | Command | What it is |
|------|------|---------|------------|
| `MTEST.MAC` | — | source | Microsoft M80 syntax (`.8080`, `ASEG`, `ORG 0100H`) |
| `ATEST.ASM` | — | source | Digital Research ASM/MAC syntax, absolute `ORG 0100H` |
| `ATEST.SYM` | `MAC ATEST` | `MAC ATEST` | the ground-truth CP/M **`.SYM`** |
| `ATEST.PRN` | `MAC ATEST` | `MAC ATEST` | a MAC assembler **`.PRN`** listing |
| `CASES.ASM` | — | source | ASM and MAC cases: labels, `EQU`, `SET`, `DS` |
| `CASESASM.PRN` | `ASM CASES` | `ASM CASES` | `CASES.ASM` listed by ASM |
| `CASESMAC.PRN` | `MAC CASES` | `MAC CASES` | `CASES.ASM` listed by MAC |
| `M80ABS.MAC`, `.PRN` | `M80` | `M80 ,M80ABS=M80ABS` | absolute M80: `TITLE`, `ASEG`, `EQU`, `SET`, `DEFL`, `ASET`, `::` |
| `M80REL.MAC`, `.PRN` | `M80` | `M80 ,M80REL=M80REL` | relocatable M80 |

## What they establish

- **`.SYM` is a DR-assembler product, not L80's.** Microsoft **L80 3.44** writes `.COM`
  and prints its link map to the console — it emits **no** `.SYM`. `MAC`/`RMAC` write the
  `.SYM`, and `SID` reads it. So `SYMBOLS LOAD *.SYM` targets the classic CP/M `.SYM`.
- **`.SYM` format:** tab-stop-aligned `HHHH NAME` records, several per line, `\t`-separated,
  lines end CRLF, file `^Z`-padded, symbols alphabetical, values 4-hex **absolute**. It has
  **no EQU-vs-label distinction** — `BDOS`/`CR` (EQUs) sit beside `START`/`LOOP` (labels).
- **`.PRN` has two geometries.** ASM and MAC: address cols 2-5, source col 17, constant
  mark in col 7 (`=` for `EQU`, `#` for a MAC `SET`). M80: a `MACRO-80` page header (a
  `TITLE` comes before it), address cols 3-6, relocation mark col 7, source col 33, and no
  constant mark. An `ASEG` line prints `0000'` in an absolute M80 listing.

`core/symbols.cpp` is written to these bytes; `tests/test_symbols.cpp` re-embeds the exact
`ATEST.SYM` content so the unit test needs no file I/O.
