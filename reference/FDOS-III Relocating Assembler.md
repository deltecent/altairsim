# iCOM FDOS-III Relocating Assembler (ASMB / LINK / LIB / RDBFL)

Source: [FDOS-III Relocating Assembler.pdf](#), iCOM (a division of Pertec Computer
Corporation, Canoga Park CA), September 1977, ~158 pages. Scanned from the deramp.com iCOM
floppy archive (`.../altair/software/icom_floppy/FDOS/`). The `.md` here is a text-only
distillation; the scan itself is not committed.

The iCOM **Relocatable Assembly System** is a two-machine (8080 + Z80) assembler toolchain
that runs under the FDOS-III disk operating system, alongside the iCOM Text Editor (source
prep), the older FDOS-II Macro Assembler, and DEBBI BASIC. It sits above the iCOM
FD3712/FD3812 floppy hardware documented in
`reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`. Unlike the FDOS-II Macro Assembler,
which emits absolute Intel-hex object directly, this system can emit **relocatable** object
modules that are combined by a separate **LINK** step. It differs from the FDOS-II assembler
mainly in three source-language areas: macros, expressions, and labels.

The system comprises four programs: **ASMB** (assembler), **LINK** (linker), **LIB** (library
maintenance), and **RDBFL** (binary object-file dump). It accepts both 8080 and full Z80
mnemonics (Z80 gated by an option switch) and can generate either relocatable or absolute
object files.

## 1. The relocatable model (why LINK exists)

- An **absolute** object program has real machine addresses and loads directly under FDOS-III
  or any 8080/Z80 ROM monitor. Its source must define every machine address it references.
- A **relocatable** object program codes addresses as *relative* offsets or as *external*
  symbolic references. It must be processed by **LINK** (then loaded) before it can execute.
  A relocatable source refers to storage and other programs by name, not by address.
- LINK is given relative program files and library files; one relative file is designated the
  **main** routine. Unresolved external references are searched for in the named libraries;
  resolving a reference in a library routine pulls that *entire* routine in, and recursively
  any routines *it* references. This is the automatic-library-linkage mechanism.
- Benefits: recompile only changed modules; localize labels per module (an internal need only
  be unique within its module); globalize shared data structures (buffers/tables) independent
  of any one code module.

## 2. Operation

### 2.1 ASMB — the assembler

```
ASMB, source file, output file, option control value
```

- **source file** — ASCII text file (Section 3 format). A drive may be given, e.g. `DRIVR:1`.
- **output file** — optional; an Intel-standard absolute file, a binary relative file, or
  omitted, depending on the MODE and CODE option bits.
- **option control value** — a number treated as a bit array of option switches (below). Its
  binary 1s turn switches on. The same switches can be OR'd on from source with the `OPT`
  directive; entering the value at ASMB time then need only cover listing/object-file control.

Regardless of the control value, ASMB always assembles and posts the error count to the
console.

**Assembler option switches** (bit position = binary weight):

| Bit | Name | ON | OFF |
|-----|------|----|-----|
| 0 | PRINT | Print assembly listing (source + generated code + error msgs) to list device | No listing |
| 1 | CODE | Write generated code to the output file | No code file written |
| 2 | SYMBOL TABLE | Print symbol table to list device at end of assembly | No symbol table |
| 3 | MODE | Generate **relocatable** code (must be linked) | Generate **absolute** code (loadable, not linkable) |
| 4 | NO FF | Separate listing pages with multiple line feeds | Separate pages with form-feed characters |
| 5 | Z80 | Accept/generate Z80 mnemonics | 8080 mnemonics only |
| 6 | CONSOLE LISTING | Direct all listing output to the console | Direct listing to the list device |

Examples:
- `ASMB,BANG,POP,15` — `15` = `001111B`: bits 0–3 on. Listing + symbol table printed;
  relocatable file `POP` written; 8080 only; form-feed page breaks.
- `ASMB,DRIVR:1,,31H` — `31H` = `0011 0001B`: absolute Z80 program, **no** code file (CODE
  off), listing printed with line-feed page breaks.

### 2.2 LINK — the linker

```
LINK, command file, output file [, control number]
```

- **command file** — ASCII text file (prepared with the Text Editor) of linker directives,
  one per line.
- **output file** — the resulting absolute program in Intel Hex-ASCII format.
- **control number** — optional; set non-zero to force each linked routine onto a 256-byte
  page boundary (debugging convenience).

**Linker command-file directives:**

| Dir | Parameter | Meaning |
|-----|-----------|---------|
| `M` | relative file name | **Main** routine. Exactly one; must be the **first** directive. |
| `F` | relative file name | **File include** — force this relative file in. Optional, any number. |
| `L` | relative library name | **Library use** — pull only the routines needed to resolve externals. Optional, any number. |
| `E` | — | **End** — must be the **last** directive in the command file. |

The output absolute file is then run directly by name under FDOS-III.

### 2.3 LIB — library maintenance

A library is a group of binary relative program files. Create the file first with the
FDOS-III `ALLOL` command to reserve disk space, then in LIB use `F` to select it and `I` to
initialize its directory. LIB is started by typing `LIB` and reads directives until `E`.

| Dir | Parameter | Meaning |
|-----|-----------|---------|
| `F` | file name | Select current library file (**must be first**; file must already exist). |
| `I` | — | Initialize/clear the library directory (**erases all files**; used once at creation). |
| `A` | file name | Add a binary relative file to the library. |
| `D` | library file # | Mark a file deleted (contents kept; reclaimable with `U`). File # from the `L` listing. |
| `U` | library file # | Undelete a file (clear the deleted flag). |
| `P` | — | Pack: reclaim directory/file space of deleted files (no `U` possible afterward). |
| `L` | — | List the directory: name + file number of each non-deleted file, plus its internals and externals. |
| `C` | — | Direct LIB output to the console instead of the list device. |
| `E` | — | End LIB; return to FDOS-III. |

### 2.4 RDBFL — binary object-file dump

Prints a relative program file record by record, each with a sequence number and record-type
flag (see §6). Internal/external records print as one or two symbolic name/value pairs. Data
(load-text) records print as:

```
N LLLL   TT DD TT DD .... TT DD
```
- `N` = number of data bytes; `LLLL` = load address of record.
- `TT` = data type of each byte: `A` absolute internal, `R` relative internal,
  `EA` absolute external, `ER` relative external.  *(scan lists both "R = relative internal"
  and "ER = relative internal"; ER is the external-relative code — see the object type
  register in §6.)*
- `DD` = hex value of the byte.

The end record prints program start address, program high address, and the four record-count
tallies from the end record alongside RDBFL's own computed totals; a mismatch flags a possibly
corrupt object file.

## 3. Source program format

Statement form (any field may be absent; fields separated by ≥1 blank or tab; each statement
ends with a carriage return; the physically last statement must be `END`):

```
LABEL   OPERATOR   OPERAND   ;COMMENT
```

- **Label** — begins in column 1, must start with a letter A–Z (error 30/31 also permits `?`
  and `@` as valid first characters). Only the **first five** characters are significant; the
  rest are listed but ignored. A trailing colon is ignored. Labels must be unique.
- **Operator** — an assembler directive, a previously-defined macro name, or a machine
  instruction mnemonic.
- **Operand** — zero or more comma-separated items, per the operator.
- **Comment** — introduced by `;`.

**Operand data forms (nine):** hex (digits then `H`, must start with a digit), decimal
(optional trailing `D`), octal (trailing `O` or `Q`), binary (trailing `B`), current program
counter `$`, ASCII constant (in single quotes; `''` = one quote), labels assigned values,
labels of instructions, and expressions.

**Built-in register value symbols** (usable wherever a 0–7 register/memory value is needed):
`B`=0, `C`=1, `D`=2, `E`=3, `H`=4, `L`=5, `M`=6 (memory ref), `A`=7; register pairs `SP`=6,
`PSW`=6.

**Expressions** — data forms 1–8 combined with operators. **No operator hierarchy**:
evaluation is strictly left-to-right; use parentheses to override (most-deeply-nested first).
All operators work on 16-bit values and yield 16-bit results; an 8-bit field requires the
high 8 bits be all-0 or all-1. **External labels may not appear in expressions.** The
word operators `MOD SHL SHR NOT AND OR XOR` must be surrounded by blanks (`DATUM AND0FH` is
invalid; `DATUM AND 0FH` is valid).

| Operator | Meaning |
|----------|---------|
| `+` | addition |
| `-` | subtraction / unary minus |
| `*` | multiplication |
| `/` | integer division (remainder discarded) |
| `MOD` | integer remainder |
| `NOT` | bitwise complement |
| `AND` `OR` `XOR` | bitwise logical |
| `SHR` `SHL` | logical shift right/left by second operand (zeros shifted in) |
| `( )` | grouping |

## 4. Relocation & structuring directives

These govern relocation, external/internal linkage, and absolute-vs-relative sections. `INT`
and `EXT` require relative mode (the assembler turns the MODE switch on if needed — warning
error 41).

| Directive | Label | Operand | Purpose |
|-----------|-------|---------|---------|
| `REL` | no | none | Set code-generation mode to **relative** (normally to restore mode after `ABS`). |
| `ABS` | no | none | Set code-generation mode to **absolute**. Used inside a relative assembly to force sections absolute — chiefly to define 8-bit symbolic constants as internals (an 8-bit use of a relative internal is otherwise an error, see error 39). |
| `INT` | — | ≥1 symbolic labels | **Internal linkage**: declare labels defined in *this* module and made available to other modules. Labels must be defined here (not in a MACRO). At program start; relative mode only. |
| `EXT` | — | ≥1 symbolic labels | **External linkage**: declare labels defined in *other* modules. Such labels must NOT appear in this module's label fields; normally usable only as 16-bit values (8-bit externals require an `ABS` section). At program start; relative mode only. |
| `ORG` | no | one value (not external) | Set the location counter to the operand address. Multiple `ORG`s allowed; default origin is 0. |
| `END` | no | optional value (not external) | Physical end of source; operand becomes the object end-record start/execution address for absolute loaders. |

Absolute vs. relative code generation is initially set by the MODE option switch (or `OPT`);
`ABS`/`REL` flip it within a source file.

## 5. Value-assignment, data, conditional, macro & listing directives

**Value assignment / data:**

| Directive | Label | Operand | Purpose |
|-----------|-------|---------|---------|
| `EQU` | required (= the symbol) | value/symbol/expr, not external | Equate a symbol to a value. Not emitted as code. Two-pass; only one level of forward reference resolves. |
| `SET` | required | value/expr | Assign a value to a label; **may be redefined** by later `SET`s. |
| `DB` | optional | ≥1 exprs (−128..+255) or quoted ASCII strings | Define byte(s). |
| `DW` | optional | ≥1 exprs (−32768..+65535) | Define 16-bit word(s), **low byte first**. |
| `DS` | optional | one value/expr, not external | Reserve N bytes (contents left unchanged). |
| `OPT` | no | value (not external) | OR the low 6 bits of the operand into the option-switch array (see §2.1). At program start. |

**Conditional assembly** (`IF` / `ELSE` / `ENDIF`): `IF expr` — if the operand is zero, the
statements up to the matching `ELSE`/`ENDIF` are listed but not assembled. Every `IF` must be
paired with an `ENDIF`. `ELSE` (no operand) inverts the assembled/not-assembled sense.
Nestable to **8** levels.

**Macros** (`MACRO` / `ENDM`): a label names the macro; the body between `MACRO` and `ENDM`
is substituted at each reference (the macro name placed in the operator field). Macros may not
be nested (error 32) and each must be defined once. Macro/expression/label handling is the
main source-language departure from the FDOS-II Macro Assembler.

**Listing control** (do not affect generated code; all optional):

| Directive | Effect |
|-----------|--------|
| `NOLST` | Suppress listing until `LIST` or `END`. Does not override the PRINT option switch. |
| `LIST` | Resume listing (cancels `NOLST`). |
| `PAGE` | Skip to top of next listing page (no effect if NO FF switch set). |
| `SPACE` | Insert blank line(s) in the listing. |
| `TITLE 'text'` | Print the quoted string (≤40 chars) at the top of every page. |

## 6. Object file formats (Appendix A)

### 6.1 Relative program file

A relative program file is a sequence of **binary, fixed 16-byte records** of five types,
terminated by an end record.

| Record | Type flag | Layout summary |
|--------|-----------|----------------|
| Relative internal | `81H` | Up to two (name, relative-address) entries. |
| Absolute internal | `82H` | Up to two (name, absolute-value) entries. |
| External | `83H` | Up to two (name, external-sequence-number) entries. |
| Load data | `84H` | 1–8 data bytes with per-byte relocation mode. |
| End | `85H` | Program start/high addresses + record tallies. |

**Label record layout** (types 81/82/83 — Figure A-2): byte 1 = type flag; 2–6 = name 1;
7–8 = value 1; 9–13 = name 2 (spaces if unused); 14–15 = value 2; 16 = checksum.

**Load data record** (84H — Figure A-4): byte 1 = flag; byte 2 = data length (1–8); bytes 3–4
= load address; bytes 5–6 = data-mode "type register" (two bits per data byte); bytes 7–15 =
load data; byte 16 = checksum. Per-byte 2-bit relocation mode (Figure A-3):

| Bits | Mode |
|------|------|
| `00` | A — Absolute |
| `01` | R — Relative |
| `10` | EA — External Absolute |
| `11` | ER — External Relative |

**End record** (85H — Figure A-5): byte 1 = flag; 2–3 = program start address; 4–5 = program
upper-address limit; 6 = tally of relative-internal records; 7 = tally of absolute-internal
records; 8 = tally of external records; 9–10 = tally of load-data records; 11–15 = unused
(zero); 16 = checksum.

### 6.2 Library file

Byte 0 = identifier flag `80H`. First four sectors = directory; remainder = filespace
(a series of relative program files as above). Directory holds up to **42** entries of 7 bytes
each; first byte is the entry-type flag (Figure A-1):

| First byte | Entry type |
|------------|------------|
| `00–7F` | Active entry; the byte is the first character of the name. |
| `80–EF` | Deleted entry; byte with bit 7 cleared is the first name character. |
| `FF` | End of directory. |

An active/deleted entry is a 5-character file name plus a 2-byte binary length.

### 6.3 Absolute program file (Intel Hex-ASCII, Figure A-6)

Variable-length ASCII records: byte 1 = record identifier (ASCII colon `:`); 2–3 = data byte
count N; 4–7 = load address; 8–9 = unused (zeros); 10..(2N+10) = load data; 2N+11..2N+12 =
checksum. End of file is a record with byte count 0, whose load-address field holds the
program start address.

## 7. Error codes (Appendix B)

Assembler error codes 0–43 (a listing error flags the offending line by code):

| Code | Message / meaning |
|------|-------------------|
| 0 | TOO MANY EXTERNALS — only 256 external declarations allowed. |
| 1 | MULTIPLY DEFINED LABEL — label already used in another label field or declared external. |
| 2 | PHASE ERROR — location counter clobbered; likely `ORG`/`DS` with a label operand defined later by MACRO/SET/EQU. |
| 3 | INVALID INSTRUCTION — operator is not an instruction, directive, or defined macro. |
| 4 | INTERNAL DEFINITION ERROR — an `INT` operand label was not defined in the program. |
| 5 | NUMBER TOO LARGE FOR 8 BITS — 8-bit operand outside −128..+255. |
| 6 | NUMBER TOO LARGE FOR 3 BITS — register/3-bit operand outside 0..+7. |
| 7 | INVALID DIGIT FOR BASE — digit not valid for the number's radix. |
| 8 | INSTRUCTION NOT ALLOWED — W register specified too many times. |
| 9 | EXTRANEOUS CHARACTERS — more operands than needed (often a missing comment `;`). |
| 10 | NESTED IF MORE THAN 8 — max 8 `IF` nesting levels. |
| 11 | ELSE BUT NO IF. |
| 12 | ENDIF BUT NO IF. |
| 13 | MISSING PARAMETER — null operand (adjacent commas, or too few macro args). |
| 14 | END OF MEMORY — macro/symbol table full; split into modules and link. |
| 15 | EXTERNAL ALREADY DEFINED — `EXT` operand also defined in a label field. |
| 16 | UNBALANCED PARENTHESIS. |
| 17 | INVALID UNARY OPERATOR — term missing before `*` or `/`. |
| 18 | STRING TOO LONG/SHORT — character constant wrong length. |
| 19 | EMBEDDED QUOTE IN STRING. |
| 20 | INVALID OPERAND GP1 — first operand not a valid register number 0–7. |
| 21 | INVALID OPERAND GP2 — not a valid register pair (0,2,4,6); SP must be pair 6. |
| 22 | INVALID OPERAND GP3 — not 0,2,4,6; PSW must be pair 6. |
| 23 | INVALID OPERAND GP4 — not 0 or 2 as the op requires. |
| 24 | NO LABEL FOR EQUATE — missing/invalid label before `EQU`. |
| 25 | NO LABEL FOR SET — missing/invalid label before `SET`. |
| 26 | NUMBER GREATER THAN 16 BITS — expression outside −32768..+65535. |
| 27 | UNDEFINED IDENTIFIER — operand label defined nowhere. |
| 28 | MISSING TERM IN EXPRESSION — register designator or term missing. |
| 29 | INVALID OPERATOR IN EXPRESSION — symbol between terms not a valid operator. |
| 30, 31 | ILLEGAL LABEL / ILLEGAL LABEL FOR MACRO — label doesn't begin with A–Z, `?`, or `@`. |
| 32 | NESTED MACROS NOT ALLOWED — macro expansion contains a MACRO/macro reference. |
| 33 | ENDM BUT NO MACRO. |
| 34 | IF BUT NO ENDIF. |
| 35 | MACRO BUT NO ENDM. |
| 36 | EXTERNAL NOT ALLOWED WITH PSEUDO-OP — that directive forbids an external operand. |
| 37 | EXTERNAL NOT ALLOWED IN EXPRESSION. |
| 38 | EXTERNAL NOT ALLOWED IN 8 BIT OPERAND — relative external used where 8 bits required. |
| 39 | RELATIVE NOT ALLOWED IN 8 BIT OPERAND — relative internal (16-bit) used where 8 bits needed; wrap the SET/EQU in an `ABS`…`REL` pair to force absolute. |
| 40 | MISSING END INSERTED — EOF reached with no `END`; one is inserted. |
| 41 | RELATIVE MODE SET — `INT`/`EXT` require relative mode; the switch was turned on. |
| 42 | INVALID OPERAND — a comma expected between two operands was not found. |
| 43 | INVALID CHARACTER — an unrecognized character was found. |

## 8. Worked flow (from the Foreword)

Two separately-assembled relative modules (a main and a `CLEAR` subroutine) are linked:

```
ASMB, PRG1S, PRG1B, 79        ; assemble module 1 → relative file PRG1B
ASMB, PRG2S, PRG2B, 79        ; assemble module 2 → relative file PRG2B
```
`79` = `1001111B`: listing + symbol table + code + relative MODE, output to console. The main
module declares the sub and shared table with `EXT CLEAR,FOOS` / `TBL` (no hand-coded equates
for external addresses); the subroutine declares `CLEAR` with `INT`. A linker command file
`LNKCM` is built with the editor:

```
M PRG1B
F PRG2B
E
```
then:
```
LINK, LNKCM, PROG0            ; produce absolute PROG0
PROG0                         ; run under FDOS-III
```

## Not distilled here

The scan also contains: Section IV's full addressing-mode taxonomy and functional instruction
classification; **Section V**, the complete alphabetical 8080/Z80 opcode reference (~196
entries, ABS…XTIX/XTIY) with per-instruction descriptions, flag effects, and coding examples;
**Appendix C**, the instruction-summary table (mnemonic, length, machine-code byte patterns,
flag columns); and **Appendix D**, the machine-code index (primary + CB / ED / DD·FD /
DD·FD·CB sub-indexes). Only the relocation-relevant directives and formats are captured above;
consult the scan for individual machine-instruction semantics.
