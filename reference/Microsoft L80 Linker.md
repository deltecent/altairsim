# Microsoft LINK-80 (L80) Linking Loader

Source: [Microsoft L80 Linker.pdf](https://deramp.com/downloads/altair/software/manuals/Microsoft%20L80%20Linker.pdf)
— *Microsoft LINK-80 Loader, CP/M Version, Software Reference Manual* (Heath/Zenith reprint,
595-…, © Microsoft 1979). 11 pp, real text layer. Fetched 2026-07-17 from deramp.com.

Not a hardware source. This is here for **one thing the debugger relies on**: the **`.SYM`
file** L80 writes and how it differs from the assembler's `.PRN`. The Microsoft toolchain is
`M80 → .REL → L80 → .COM/.HEX (+ optional .SYM)`; `src/core/symbols.{h,cpp}` loads either the
`.PRN` (from [Microsoft M80 Assembler](Microsoft%20M80%20Assembler.md)) or the `.SYM` (from
here), and `docs/manual/debugging.md`'s Symbols section documents the operator side.

## What L80 is

The linking loader for the relocatable `.REL` modules produced by MACRO-80 and by the
FORTRAN-80 / COBOL-80 / BASIC compilers. It resolves externals against the modules given and
the system library, then writes an absolute image. Run it, and it prompts with `*`; each
command line is file names and switches separated by commas:

```
objdev1:filename.ext/switch1, objdev2:filename.ext, …
```

- **Default input extension is `.REL`.** Omit the extension and L80 looks for `name.REL`.
- After each line it lists any still-**undefined globals**, each followed by `*`.
- Before it finishes it always searches the system library on the default drive.

## Switches — what altairsim cares about

Each switch is preceded by `/`. The three that decide what files come out:

| Switch | What it does |
|---|---|
| **`/N`** | Name the output file. `filename/N` saves under that name; **default extension `.COM`**. A jump to the start is inserted so the `.COM` runs. Naming is separate from *writing* — the file is written when `/E` or `/G` happens. |
| **`/E`** or `/E:Name` | Exit to CP/M. Searches the system library for undefined globals first. `/E:Name` uses global `Name` as the start address. |
| **`/G`** or `/G:Name` | Load-and-go: start execution once the line is interpreted. Prints two numbers (start address, next free byte) and `BEGIN EXECUTION`. |
| **`/X`** | If a `filename/N` was given, write the image as **Intel ASCII HEX**, extension **`.HEX`**, instead of binary. `FOO/N/X/E` → `FOO.HEX`. |
| **`/Y`** | **If a `filename/N` was given, create `filename.SYM` when `/E` is entered.** See below. |
| `/M` | Map: list program/data origin+end, **all defined globals and their values**, and undefined globals (`*`). Program info only if a `/D` was done. Goes to the console. |
| `/U` | Like `/M` but origins + undefined globals only (no symbol values). |
| `/R` | Reset the loader to its initial state (wrong file loaded — start over). |
| `/S` | Search the immediately-preceding file as a library to satisfy undefined globals. |
| `/P:addr`, `/D:addr` | Set the program / data origin for the *next* module loaded. Radix is the current radix — **default hex**; `/O` → octal, `/H` → hex. |

## `/Y` — the `.SYM` file, and exactly what is in it

> If a `filename/N` was specified, `/Y` will create a `filename.SYM` file when `/E` is
> entered. This file contains the names and addresses of **all Globals** for use with
> Digital Research's Symbolic Debugger, SID and ZSID.
>
> `FOO/N/Y/E` → `FOO.COM` and `FOO.SYM`.  `MYPROG/N/X/Y/E` → `MYPROG.HEX` and `MYPROG.SYM`.

Two facts an implementer must not miss:

- **A Microsoft `.SYM` holds *globals only*** — the PUBLIC/ENTRY symbols, the names L80 could
  even see across modules. Module-local labels and `EQU` names never leave M80's `.PRN`, so
  they are not in an L80 `.SYM`. This is *why* a `.SYM` is a flat name=value list with no
  label/EQU split, while a `.PRN` symbol table carries every symbol with a type flag.
- **The `.SYM` is a link-time product, not an assembler product.** M80 does **not** write it
  (see the M80 reference); L80 does, and only when both `/N` and `/Y` are given and `/E` runs.

So the Microsoft path can produce a `.SYM` after all — the debugger's `SYMBOLS LOAD prog.SYM`
works on an L80-`/Y` file. What M80 alone cannot do is emit a `.SYM`; its symbolic output is
the `.PRN` listing.

## `/D` vs `/P`, and the 100H trap

`/P` sets the program (code) origin, `/D` the data+common origin; both take effect when seen
and do not move already-loaded code. Without `/D`, data loads before program for each module.

**Do not `/P`/`/D` a load over 100H–102H** unless it *is* the program start: that is where L80
plants its startup `JMP`, and loading into those three bytes suppresses the jump — the `.COM`
would start executing whatever landed there instead of your entry point.

## Errors worth recognizing

| Message | Means |
|---|---|
| `?No Start Address` | `/G` with no main program loaded. |
| `?Loading Error` | Input was not a valid LINK-80 object file. |
| `?Nothing Loaded` | `/S`, `/E`, or `/G` with nothing loaded. **`TEST/N/E` alone hits this** — `/N` only *names* `TEST.COM`, it does not load `TEST.REL`. |
| `%Mult. Def. Global YYYYYY` | A global defined in more than one module. |
| `%2nd COMMON Larger /XXXXXX/` | First definition of a COMMON block was not the largest. |
| `Origin Above/Below Loader Memory, Move Anyway (Y or N)?` | Data/program origin lies outside loader memory. |

## `.REL` object format — the one-line version

L80 object files are a **bit stream**, not byte-aligned, to stay small. First bit `0` → the
next 8 bits are an absolute byte; first bit `1` → the next 2 bits pick a relocatable type
(01 program-, 10 data-, 11 common-relative, each followed by a 16-bit value to add to that
base). The `100` prefix introduces "special LINK items" (entry symbol, program name, define
entry point, chain external, define sizes, set location counter, end file, …). altairsim does
not read `.REL` — L80 resolves it into the `.COM`/`.HEX`/`.SYM` we actually consume — so the
detail is in the manual (pp. 1-7…1-8) if it is ever needed, and not reproduced here.
