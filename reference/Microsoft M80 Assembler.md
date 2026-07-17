# Microsoft MACRO-80 (M80) Assembler

Source: [Microsoft M80 Assembler.pdf](https://deramp.com/downloads/altair/software/manuals/Microsoft%20M80%20Assembler.pdf)
— *Microsoft MACRO-80 Assembler, CP/M Version, Software Reference Manual* (Heath/Zenith reprint,
595-2666-02, © Microsoft 1979). 46 pp, real text layer. Fetched 2026-07-17 from deramp.com.

Not a hardware source. This is here for the **`.PRN` listing** M80 writes — the file
`src/core/symbols.{h,cpp}` parses when the operator does `SYMBOLS LOAD prog.PRN`, and the file
whose **symbol-table section** carries every symbol with a type flag. The Microsoft toolchain
is `M80 → .REL → L80 → .COM/.HEX (+ optional .SYM)`; the linker and the `.SYM` are in
[Microsoft L80 Linker](Microsoft%20L80%20Linker.md).

## What M80 is

An 8080 **and** Z80 macro assembler. Source in, relocatable `.REL` module + `.PRN` listing
out; the `.REL` then goes to L80. Run it and it prompts `*`. The command string is:

```
objprog.ext, list.ext = source.ext
```

**Default extensions:**

| File | Extension |
|---|---|
| source | `.MAC` |
| relocatable object | `.REL` |
| listing | `.PRN` |
| cross-reference | `.CRF` |

Either the object file or the listing may be dropped; a bare comma left of `=` suppresses
both. `=EXP` assembles `EXP.MAC` → `EXP.REL` (no listing). `EXP,EXP=EXP` → `.REL` + `.PRN`.

## The `.PRN` listing — what the symbol loader reads

Two parts matter to altairsim: the **per-line body** and the **symbol table** printed after it.

### Listing-line format

Each assembled line is `address  bytes  source`, where address/bytes are in the listing radix
— **hex by default**, octal under `/O`. A **relocation marker** follows a relocatable address:

| Marker | Meaning |
|---|---|
| `'` | Program- (code-) relative value |
| `"` | Data-relative value |
| `!` | COMMON-relative value |
| *(space)* | Absolute value |

A `+` between the code and the source marks a line from a macro expansion or an `INCLUDE`
file. (These per-line addresses are *relative to the segment*, not final — L80 fixes the
origin. A `.PRN` gives symbolic **names**, but its addresses are only absolute for `ASEG`
code or after the program is located.)

### Symbol-table listing — the section that carries the symbols

At the end of the listing, **all macro names alphabetically, then all symbols alphabetically**.
Each symbol is `name <tab> value`, then flag characters:

- an `I` immediately after the value if the symbol is **Public**;
- then one type character:

| Char | Symbol type |
|---|---|
| `U` | Undefined |
| `C` | COMMON block name (its "value" is the block length in the listing radix) |
| `*` | External |
| *(space)* | Absolute value |
| `'` | Program-relative value |
| `"` | Data-relative value |
| `!` | COMMON-relative value |

This is richer than an L80 `.SYM` (which holds only globals, as name=value): the `.PRN` table
has **every** symbol and tells you EQU/absolute from relocatable-label from external. That is
why loading a `.PRN` can populate a byName/byValue split that a `.SYM` cannot.

## Symbols and constants — the assembler's rules (mind the radix)

- **Symbols:** any length, **only the first 6 characters significant**. Legal characters
  `A–Z 0–9 $ . ? @ _`. May not start with a digit. Lower case folds to upper. A reference
  suffixed `##` (e.g. `NAME##`) is declared external.
- **Numeric constants default to DECIMAL** (`.RADIX` changes the base, 2–16). Override per
  number with a suffix: `B` binary, `D` decimal, `O`/`Q` octal, `H` hex; or `X'nnnn'` hex. A
  hex number starting with a letter needs a leading `0` (`0FFH`). Numbers are 16-bit unsigned;
  overflow past two bytes is truncated to the low 16 bits.
  - ⚠ **Different from altairsim's own expression evaluator**, whose default radix is hex.
    An M80 source constant written bare is decimal; the same digits typed at the monitor are
    hex. Do not assume a value copied out of a `.MAC` is hex.

## Public / external declaration (how a symbol becomes a global)

- **`label::`** (double colon) declares the label PUBLIC. `FOO:: RET` ≡ `PUBLIC FOO` + `FOO:`.
- **`PUBLIC`/`ENTRY`** `<name>,…` — internal, available to other modules.
- **`EXTRN`/`EXT`** `<name>,…`, or the `##` suffix — external, defined elsewhere.

Only PUBLIC/ENTRY names survive linking as globals — which is exactly the set an L80 `/Y`
`.SYM` contains, and a subset of what the `.PRN` symbol table lists.

## Switches (M80 command line)

| Switch | Action |
|---|---|
| `/O`, `/H` | Listing radix octal / hex (**hex default**). |
| `/R`, `/L` | Force an object / listing file. |
| `/C` | Force a cross-reference `.CRF` (for CREF-80). |
| `/Z`, `/I` | Assemble Z80 / 8080 mnemonics (**8080 default**; also `.Z80` / `.8080` in source). |
| `/M` | Initialize `DS` space to zeros (otherwise `DS` does not clear). |
| `/P` | +256 bytes of assembly stack (use on stack-overflow during assembly). |
| `/X` | Initial suppress/list mode for false conditional blocks (pairs with `.TFCOND`). |

## Pseudo-ops worth knowing when reading a `.MAC`/`.PRN`

- **Segments:** `ASEG` (absolute), `CSEG` (code-relative, the default), `DSEG` (data-relative),
  `COMMON /name/`. `ORG <exp>` sets the location counter in the current segment.
- **Data:** `DB`/`DC` (bytes/string; `DC` sets bit 7 on the last char), `DW`, `DS <exp>` (reserve).
- **Symbols:** `EQU` (fixed), `SET` (redefinable), `EXTRN`, `PUBLIC`/`ENTRY`, `NAME 'mod'`.
- **`.PHASE`/`.DEPHASE`** — assemble labels as absolute from a phase origin while loading the
  code elsewhere (code meant to be copied to `100H` and run there). A `.PRN` for phased code
  shows the phase address, which is not where the bytes were assembled.
- **Macros/blocks:** `MACRO…ENDM`, `REPT`, `IRP`, `IRPC`, `LOCAL` (unique `..nnnn` labels),
  `EXITM`; operators `&` (concatenate), `%` (value substitution), `NUL`, `TYPE`.
- **Z80 pseudo-op aliases** (Z80 mode): `DEFB=DB`, `DEFW=DW`, `DEFS=DS`, `DEFM=DB`, `DEFL=SET`,
  `GLOBAL=PUBLIC`, `EXTERNAL=EXTRN`, `COND=IFT`, `ENDC=ENDIF`, `EJECT=PAGE`.

## Error flags (column 1 of the listing)

One character in column 1 flags a line: `A` argument, `C` conditional-nesting, `D`/`M`
double-/multiply-defined, `E` external-misuse, `N` number, `O` bad opcode/syntax, `P` phase
(label value differs on pass 2), `Q` questionable (warning — usually unterminated line),
`R` relocation misuse, `U` undefined symbol, `V` value (pass-1 value needed but undefined
later). The listing ends with `[nn] [No] Fatal error(s) [, nn warnings]`.
