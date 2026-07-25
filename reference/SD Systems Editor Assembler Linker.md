# SD Systems Editor / Assembler / Linker

Source: [SD Systems Editor Assembler Linker.pdf](#) (SD Systems "Operations Manual — Text
Editor, Z80 Global Assembler, Linker"), with the companion
[SD Systems Z80 Assembler.pdf](#) and [SD Systems Z80 Linker.pdf](#).

This is the SD Systems Z80 development toolchain — a **Text Editor (`EDIT`)**, a **relocating
Z80 assembler (`ZASM`, "Z80 Global Assembler")** and a **linker (`LINK`)** — the very tools
that produced the SD Systems ROMs. Those ROM sources are Z80 assembly (`.Z80` / `.ASM`,
e.g. `SDMONV21.Z80`, `MSMONR21.Z80`, `DDB200.ASM`) assembled with `ZASM` and joined with
`LINK`; a submit file such as `R DDB200.ASM / ZASM DDB200.ASM /C / LINK DDB200` drives the
build. This is a distilled emulation reference: the manual's tutorials, worked examples,
sample editing/assembly sessions, and marketing are intentionally omitted — **only what is
needed to understand and reproduce the toolchain's behavior is kept**. It is **not**
something the simulator emulates directly; these are CP/M-compatible programs that run on the
emulated Z80 (they load at `0100H` and use the DOS/BDOS the way any `.COM` does). It is kept
here so a reader can follow how SD source assembles and links into the ROM images.

Versions documented: **Editor V1.0, Assembler V3.3, Linker V3.1** (copyright 1978, with a
1979 addendum). The toolchain targets "32K CP/M compatible" systems; on the SD boards that
DOS is [SDOS/COSMOS](SD%20Systems%20SDOS.md).

---

## 1. The three components and the build pipeline

| Tool | Command | Reads | Writes |
|------|---------|-------|--------|
| **Editor** | `EDIT file` | an ASCII source file (or new) | the edited file; old copy → `.BAK` |
| **Assembler** | `ZASM file.ext /opts` | one Z80 source module | object `file.OBJ` + listing `file.PRN` |
| **Linker** | `LINK f1,f2,… /opts` | one or more `.OBJ` modules | load module `f1.HEX` (+ `f1.CRS` map) |

The end-to-end flow: **source → `ZASM` → `.OBJ` (relocatable, ASCII hex) → `LINK` → `.HEX`
(absolute Intel-hex load module) → DOS `LOAD` → `.COM` (executable memory image)**. For a
ROM, the linked `.HEX` (origined at the PROM address) is the image that is burned/converted.
All three tools are disk-resident, load at `0100H`, and do all I/O through the console device
and the disk. (inferred: the ROM build's `.HEX` at e.g. `E000H`/`F000H` is produced by
`LINK … /A=E000` or an `ORG` in source, per §6.)

---

## 2. Editor (`EDIT`) — command set

Invoke `A>EDIT filename(CR)`. The file may **not** have an extension of `COM`, `BAK`, or
`$$$`. A nonexistent file prints `***NEW FILE` and drops into **data mode**; an existing file
opens for command entry. On `Q`, the original is renamed `.BAK` and all edits are saved under
the original name.

The Editor is **line-oriented over an ASCII file**. Lines carry **decimal line numbers
0001–9999**, assigned dynamically and **renumbered automatically** on every insert/delete.
The command prompt is `*`. A command is one letter plus an optional operand, terminated by
`CR`; up to 80 chars/line, several commands may share a line, and **blanks and commas are
ignored**. The operand is a decimal count `n` (0–9999) or a range `n-m`; if omitted it
defaults to **1** (except `F`). Inserted lines may be up to **128 characters**.

| Cmd | Form | Action |
|-----|------|--------|
| **A** | `An` | Advance the line pointer `n` lines toward EOF (prints the reached line). |
| **B** | `Bn` | Back up `n` lines toward the top. |
| **C** | `Cn/s1/s2/` | Change next `n` occurrences of `s1` to `s2` from the current line; any char not in either string is the delimiter (all three identical, last may be `CR`); empty `s2` deletes `s1`. |
| **D** | `Dn` or `Dn-m` | Delete `n` lines (or the range `n`..`m`). |
| **E** | `En` or `En-m` | Exchange lines — equivalent to `Dn` / `B` / `I` (delete then enter replacements via data mode). |
| **F** | `Fn` | Print-flag: `n=0` suppresses console echo on all but `V`; `n≠0` re-enables it. |
| **G** | `G file` | Get: read all lines of `file` and insert after the current line (source unchanged). |
| **I** | `I` | Insert data lines after the current line (`***DATA MODE`; a lone `CR` ends; blank line = `space CR`). |
| **L** | `Ln` | Go to line number `n` (`n=0`/omitted → line 1). |
| **P** | `Pn file` or `Pn-m file` | Put `n` lines (or range) out to `file` (overwrites it; source not deleted). |
| **Q** | `Q` | Quit — save edits, rename original to `.BAK`, return to DOS. |
| **S** | `Sn/string/` | Search forward (from the next line) for `n` occurrences of `string`. |
| **T** | `T` | Insert data lines at the top, before the first line. |
| **V** | `Vn` or `Vn-m` | View `n` lines (or the range) on the console. |

**Messages:** `***NEW FILE`, `***DATA MODE`, `***TOP` / `***TOF`, `***EOF`, `***END OF
EDITING`, `***END OF WINDOW. USE 'ADVANCE' TO SEE NEXT LINE`. An unknown command prints `?`
then a new `*`; a bad filename is a syntax error and returns to DOS.

---

## 3. Assembler (`ZASM`) — invocation, options, output

Invoke `A>ZASM file.ext /options(CR)`. The **slash enables batch operation**: with `/` the
options follow on the command line; **omit the slash and `ZASM` prompts** `SD SYSTEMS Z80
ASSEMBLER V3.3. OPTIONS?` (which blocks unattended batching). If no options are wanted, give
a bare `/`. `ZASM` makes **two passes**, printing `PASS 1 DONE` after pass 1 and
`ERRORS=nnnn` (decimal) at the end, then returns to `A>`.

Object output goes to **`file.OBJ`**, listing to **`file.PRN`**, unless `T`/`L` redirect the
listing.

| Option | Effect |
|--------|--------|
| **C** | Print a cross-reference table at the end of the listing. |
| **K** | No listing (errors still go to the console). |
| **L** | List to the listing (printer) device instead of a `.PRN` file. |
| **N** | No object output. |
| **P** | Pass 2 only (symbol table left intact — for single-pass use). |
| **R** | Reset the symbol table (automatic for pass 1; used with `P`). |
| **S** | Print a symbol table at the end of the listing. |
| **T** | List to the console device instead of a `.PRN` file. |

**Listing format:** statement and page numbers are decimal; an equated symbol's value is
flagged with `>`; **relocatable addresses are flagged with a leading prime `'`**; listing
directives (§4.4) are not shown but get statement numbers; `INCLUDE`d lines get a leading
`+`. Errors appear inline as `***** ERROR ***** <text>`; abort errors go only to the console.
**Single-pass (`P`) restrictions:** no forward references, no `NAME`, no cross-reference.

---

## 4. The Z80 assembly language `ZASM` accepts

Standard Zilog/Mostek Z80 mnemonics (full instruction set; Appendix A of the manual is a
complete opcode listing). Statement = optional label, opcode/pseudo-op, operands, comment.

- **Delimiters:** fields are separated by one or more spaces, commas, or **tabs (09H)**; a
  label may alternatively be separated from the opcode by a **colon**.
- **Labels:** 1+ chars, **only the first 6 are significant**. May not contain
  `' ( ) * + - / , = < > . : ; ` or space, and may not start with a digit. A label needs a
  trailing `:` unless it starts in **column 1**.
- **Comments:** everything after `;` (a `;` inside quotes is data, not a comment).
- **Number radix** (constants `0`..`0FFFFH`): **decimal** default (or `D` suffix), **hex**
  `H` suffix and must start with a digit (`0AF1H`), **octal** `Q`/`O` suffix (`377Q`),
  **binary** `B` suffix (`0110110B`), **ASCII** in quotes (`'A'` = `41H`).
- **`$`** is the program counter (current instruction address). A parenthesized expression
  `(expr)` denotes a **memory address** (indirection). Relative-jump reach is **−126..+129**;
  since **V3.1**, `JR TAG` / `DJNZ TAG` need no `-$`.
- **Expressions** evaluate left-to-right by hierarchy (parentheses override), all to 16-bit
  values; the one exception is `'str1'='str2'`, a char-by-char compare yielding `0FFFFH`
  (true) or `0` (false).

| Hierarchy | Operators |
|-----------|-----------|
| 0 (relational) | `=`/`.EQ.` `<` `>` `<=` `>=` `<>`/`.NE.` `.LT.` `.GT.` `.LE.` `.GE.` `.RES.` |
| 1 (unary) | unary `+` `-` `.NOT.` (one's complement) |
| 2 | `*` `/` |
| 3 | `+` `-` |
| 4 (logical) | `.AND.` `.OR.` `.XOR.` `.SHR.` `.SHL.` |

**Pseudo-ops:**

| Pseudo-op | Meaning |
|-----------|---------|
| `DEFB n,n,…` | Define successive **bytes** = expressions. |
| `label DEFL nn` | Define **label**, reassignable; takes the last value set at that point. |
| `DEFM 'aa'` | Define **message** — ASCII bytes (≤63 chars; `''` = a literal quote). |
| `DEFS nn` | Reserve `nn` bytes (contents left as-is; not usable at program start/end). |
| `DEFW nn,nn,…` | Define **words** — 2 bytes each, **low byte first**. |
| `END` | End of program (optional; also ends an assembly). |
| `IF nn` / `ENDIF` | Conditional assembly — assemble the block only if `nn` is non-zero (true); **cannot nest**. |
| `label EQU nn` | Equate — set label once (may not be redefined). |
| `GLOBAL symbol` | Declare a global (the assembler decides internal vs external — §5). |
| `INCLUDE file.ext` | Splice another source file in-line (**cannot nest**; the included file must not carry its own `END`). |
| `NAME symbol` | Module name (heads the listing and the first object record; default 6 blanks). |
| `PSECT REL`/`ABS` | Program section attribute; **once**, at the module start (`REL` default). |
| `ORG nn` | Set the PC to `nn`; multiple `ORG`s must be strictly increasing. |

**Listing directives** (formatting only — no statement output): `EJECT`, `LIST`, `NLIST`,
`TITLE s` (≤32 chars).

---

## 5. Module model — absolute, relocatable, global symbols

- **`PSECT REL` (default)** produces a **relocatable** module; **`PSECT ABS`** produces an
  **absolute** module that must load at its `ORG` addresses (used for drivers/constant
  blocks whose position is fixed — relevant to ROM code that must sit at a known address).
- In a relocatable module **only 16-bit address values are relocated**; 16-bit constants are
  not. A relocatable quantity **may not be used as an 8-bit operand**. A label `EQU`'d to a
  constant stays constant; `EQU`'d to a relocatable address stays relocatable.
- **`GLOBAL`** declares a symbol shared across separately-assembled modules. The assembler
  classifies it: **internal** if it also appears as a label here (its value is exported);
  **external** if it does not (a reference resolved from another module). An **internal
  symbol is always relocatable** in a relocatable module — even one that looks constant; for
  a shared constant use `PSECT ABS`. An **external symbol** is always a 16-bit address and
  **may not appear in an expression with operators**, in an 8-bit operand, or in an
  `EQU`/`DEFL` operand. In a link set, **internal names must be unique**.
- **Object module (`.OBJ`)** is **ASCII** and carries linking info, address/relocation info,
  machine code, and checksums; external references are encoded as a **backward-linked list
  through the object code**.

---

## 6. Linker (`LINK`) — operation, command, output

`LINK` concatenates `.OBJ` modules and resolves global references, emitting a **load module
`.HEX`** (Intel-hex; DOS `LOAD` turns it into a `.COM` memory image).

Invoke `A>LINK file1,file2,…,fileN /options(CR)` (input files are `.OBJ`). Output `.HEX`
takes **`file1`'s** primary name; with `C` a cross-reference `.CRS` is also written. Omit the
slash and options are prompted (`OPTIONS?`, then `ENTER STARTING LINK ADDRESS>`).

| Option | Effect |
|--------|--------|
| **C** | Write a global cross-reference table + **load map** (→ `.CRS`). |
| **U** | List all undefined global symbols. |
| **A=hhhh** | Starting link address (hex) for the first relocatable module. If more options follow `A=hhhh`, a **comma** must separate them. |

**Operation:** pass 1 reads all `.OBJ` and collects global definitions; pass 2 resolves
references and writes the `.HEX`, printing each module's `BEG ADDR`/`END ADDR` and type
`ABS`/`REL`. **Absolute** modules land at their `ORG`; **relocatable** modules pack
end-to-end; the **first relocatable** module goes at `A=` (default **0** if `A` is absent).
Absolute modules must be listed in **ascending** start-address order or a **module sequence
error** results. For a normal `.COM`, origin the first module at **`0100H`** (via `ORG` or
`/A=100`); for a ROM, origin at the PROM address. The `.CRS` load map lists each module's
range and a `SYMBOL / ADDR / REFERENCES` table.

---

## 7. Notes for reading SD ROM sources

- **Build chain:** a ROM `.Z80`/`.ASM` (e.g. `DDB200.ASM`, `SDMONV21.Z80`, `MSMONR21.Z80`)
  is assembled by **`ZASM name.ASM /C`** (`/C` = cross-reference; `.OBJ` + `.PRN` out) and
  positioned by **`LINK name`** into `name.HEX`; a `.SUB` submit file batches the two.
- **`ORG`** sets where code lands; **`PSECT ABS`** pins a module; a monitor/BIOS at `E000H`
  or `F000H` is placed by that `ORG` (or `LINK /A=`). Multiple `ORG`s must only increase.
- **`EQU`** = a fixed value (constants, port numbers, RAM cell addresses); **`DEFL`** = a
  reassignable one. **`DB`/`DW`/`DS`/`DM`** are spelled **`DEFB`/`DEFW`/`DEFS`/`DEFM`** here;
  `DEFW` is **low byte first**. `DEFM` is an ASCII string (`''` escapes a quote).
- **`GLOBAL`** is the single directive for both `PUBLIC` and `EXTRN` roles — the assembler
  decides which from whether the name is also a label in the module. Cross-module calls in
  SD sources therefore appear as `GLOBAL name` on both sides.
- **`IF/ENDIF`** (non-nesting) gate build variants; **`INCLUDE file`** (non-nesting, no `END`
  in the included file) and **`NAME`** stitch multi-file modules into one `.OBJ`.
- **Radix cues in listings/source:** `H` = hex (leading `0` if it would start with A–F), `Q`
  = octal, `B` = binary, `D`/none = decimal, `'x'` = ASCII. A `'` before a listing address
  means **relocatable**; a `>` before a value marks an equate.
- The final artifact is **Intel hex** (`.HEX`), the same format the simulator's `loadHex`
  reads — so a linked SD ROM `.HEX` can be loaded directly without going through CP/M `LOAD`.
