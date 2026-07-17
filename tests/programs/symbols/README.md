# Symbol-file fixtures — ground truth for `core/symbols`

These are real files from the CP/M toolchain that ships in this repo's disk images,
generated **inside the machine** rather than written by hand — the same rule the Host
Bridge utilities follow, so the format cannot drift from what a guest actually produces.

## Provenance

Generated 2026-07-17 by booting `disks/mits-88dcdd/cpm22/buffered/cpm22b23-56k.dsk`
(which carries `M80.COM`, `L80.COM`, `MAC.COM`) in the `default` machine, assembling and
linking the sources through the Host Bridge:

| File | Tool | Command | What it is |
|------|------|---------|------------|
| `MTEST.MAC` | — | source | Microsoft M80 syntax (`.8080`, `ASEG`, `ORG 0100H`) |
| `ATEST.ASM` | — | source | Digital Research ASM/MAC syntax, absolute `ORG 0100H` |
| `ATEST.SYM` | `MAC ATEST` | `MAC ATEST` | the ground-truth CP/M **`.SYM`** |
| `ATEST.PRN` | `MAC ATEST` | `MAC ATEST` | a MAC assembler **`.PRN`** listing |

## What they establish

- **`.SYM` is a DR-assembler product, not L80's.** Microsoft **L80 3.44** writes `.COM`
  and prints its link map to the console — it emits **no** `.SYM`. `MAC`/`RMAC` write the
  `.SYM`, and `SID` reads it. So `SYMBOLS LOAD *.SYM` targets the classic CP/M `.SYM`.
- **`.SYM` format:** tab-stop-aligned `HHHH NAME` records, several per line, `\t`-separated,
  lines end CRLF, file `^Z`-padded, symbols alphabetical, values 4-hex **absolute**. It has
  **no EQU-vs-label distinction** — `BDOS`/`CR` (EQUs) sit beside `START`/`LOOP` (labels).
- **`.PRN` geometry** (address cols 2–5, `=` at col 7 for an EQU, source at col 17, object
  field may abut col 16) is identical across CP/M ASM, M80, and MAC — one parser covers all.

`core/symbols.cpp` is written to these bytes; `tests/test_symbols.cpp` re-embeds the exact
`ATEST.SYM` content so the unit test needs no file I/O.
