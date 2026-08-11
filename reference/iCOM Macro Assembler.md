# iCOM Macro Assembler — 8080 Macro Assembler for the iCOM Floppy Disk System

Source: [iCOM Macro Assembler.pdf](#) — *iCOM MACRO ASSEMBLER Operator's Manual*, © August 1976,
iCOM (a Division of Pertec Computer Corporation, Canoga Park, CA). Distilled from the scan
archived at deramp.com (`.../altair/software/icom_floppy/FDOS/`). ~81-page operator's manual;
a one-page errata sheet on the cover corrects several label examples (see §11).

The iCOM Macro Assembler is a two-pass 8080 assembler that runs under FDOS-II on the iCOM
Floppy Disk System, alongside the iCOM Text Editor (which produces the source). It sits in
the same tool family as the Relocating Assembler and DEBBI BASIC; the underlying EDOS →
FDOS-II → FDOS-III operating systems and the iCOM FD3712/FD3812 floppy hardware are
documented separately in `reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`. The
assembler reads an 8080 source file from disk and emits an Intel-hex object file and/or a
source listing. This is a standard Intel-mnemonic 8080 assembler; its syntax and pseudo-ops
closely follow the Intel 8080 conventions, with a macro facility layered on top.

## 1. Invoking the assembler under FDOS

The assembler is run from the FDOS command line (the manual variously prints "FODS"/"FDOS"):

```
ASMB,INPUT-FILE-NAME,OUTPUT-FILE-NAME,PASS
```

(terminated by carriage return). The command assumes:

1. the diskette in drive 0 is a systems diskette;
2. the input file exists on a mounted diskette and contains 8080 source code;
3. the output diskette has room for the resulting object-code file and/or listing.

The `PASS` argument selects what output is produced:

| PASS | Output |
|------|--------|
| 2 | Source listing on the **list device** |
| 3 | Executable object code in hexadecimal format on the **output file** |
| 4 | **Both** a source listing and an object file |
| 5 | Source listing on the **output file** |

(No PASS value of 1 is documented as a user selection; the assembler is internally two-pass.)

## 2. Statement format

Each statement has up to four fields:

| Field | Name | Purpose |
|-------|------|---------|
| 1 | **LABEL** | The instruction's name, used to reference it. |
| 2 | **CODE** | The operation (op-code, pseudo-op, or macro name). |
| 3 | **OPERAND** | Address/data information for the CODE field; absent, one item, or two comma-separated items. |
| 4 | **COMMENT** | Ignored by the assembler; must begin with a semicolon `;`. |

Example:

```
START:  LXI   SP,STACK   ; Set stack pointer
        MVI   A,20H      ; Set A to ASCII space
STEND:  DB    20H        ; Create one byte data constant
STACK:  EQU   0FFFH      ; Top of stack
```

### 2.1 Label field
- First character may be alphanumeric or one of the special characters `@` (at sign) or
  `?` (question mark). Valid examples: `F14F:`, `@JMP:`, `?MVI:`.
- A label is **up to five characters** and must end with a colon `:`. Only the first five
  characters are significant (`INSTRUCTION:` is read as `INSTR:`).
- Op-codes, pseudo-instruction names, and register names may **not** be used as labels.
- Labels are instruction addresses and cannot be duplicated, but one instruction may carry
  more than one label (stacked on preceding lines).

> **Errata (cover sheet):** several label examples in Section III were printed with a
> trailing colon that should be removed — pp. 44–45 `SPRT:` → `SPRT`, p. 46 `name:` → `name`
> and `LOAD:` → `LOAD`, p. 47 `PSMG:` → `PSMG`, p. 48 `MDEC:` → `MDEC`. (These are the macro
> **names** in the label field of a `MACRO` definition, which take no colon — see §7.)

### 2.2 Code field
Contains the op-code mnemonic identifying the machine operation. At least one space must
follow the CODE field (`JMP START` is correct; `JMPSTART` is not).

### 2.3 Comment field
Comments begin with `;` and may appear alone on a line.

## 3. Operand data types and notation

Four kinds of information (register, register pair, immediate data, 16-bit memory address)
may be specified in nine ways:

| # | Way of specifying | Notation / rule |
|---|-------------------|-----------------|
| 1 | Hexadecimal | Suffix `H`; must **begin with a numeric digit** (e.g. `0FFH`). |
| 2 | Decimal | Optional suffix `D`, or bare (e.g. `255`). |
| 3 | Octal | Suffix `O` or `Q` (e.g. `377Q`). |
| 4 | Binary | Suffix `B` (e.g. `11111111B`). |
| 5 | Current program counter | The character `$` — address of the current instruction (`JMP $+9`). |
| 6 | ASCII constant | One or more characters in single quotes; a literal quote is written as two single quotes (`'*''*'`). MSB of each ASCII byte is 0. |
| 7 | Labels assigned values | Symbols given a numeric value via EQU/SET; built-in register names (see §3.1). |
| 8 | Labels of instructions | A symbol appearing in another instruction's label field. |
| 9 | Expressions | Arithmetic/logical expressions over types 1–8 (see §4). |

### 3.1 Built-in register / register-pair names

| Value | Register (types 1 & 7) | | Register-pair spec (type) |
|-------|------------------------|---|----------------------------|
| 0 | B | `B` | Registers B & C |
| 1 | C | `D` | Registers D & E |
| 2 | D | `H` | Registers H & L |
| 3 | E | `PSW` | Program status word + A |
| 4 | H | `SP` | 16-bit stack pointer |
| 5 | L | | |
| 6 | M (memory reference) | | |
| 7 | A (accumulator) | | |

A register operand may be any expression that evaluates to 0–7 (e.g. `MVI 4H,2EH` or
`MVI 8/2,2EH` both address the H register). Memory reference `M` (=6) uses the byte
addressed by H (high) and L (low).

## 4. Expressions and operators

Expressions combine data types 1–8 with arithmetic and logical operators. **All operators
treat operands as 16-bit quantities and produce 16-bit results**; the programmer must ensure
the result fits the field (e.g. an `MVI` operand must be 8 bits — `MVI A,NOT 0` is invalid
because `NOT 0` = `0FFFFH`, whereas `MVI A,NOT 0 AND 0FFH` is valid).

| Operator | Meaning |
|----------|---------|
| `+` | Addition |
| `-` | Subtraction, or unary minus (arithmetic negative) |
| `*` | Multiplication |
| `/` | Integer division (remainder discarded) |
| `MOD` | Integer remainder of first ÷ second |
| `NOT` | Bitwise complement |
| `AND` | Bitwise AND |
| `OR` | Bitwise OR |
| `XOR` | Bitwise EXCLUSIVE-OR |
| `SHR` | Logical shift right of first operand by second (zeros in) |
| `SHL` | Logical shift left of first operand by second (zeros in) |
| `( )` | Parentheses for grouping |

**Precedence** (highest first):

1. Parenthesized expressions (most deeply nested first)
2. `*`  `/`  `MOD`  `SHL`  `SHR`
3. `+`  `-` (unary and binary)
4. `NOT`
5. `AND`
6. `OR`  `XOR`

The named operators `MOD`, `SHL`, `SHR`, `NOT`, `AND`, `OR`, `XOR` **must be separated from
their operands by at least one blank** (`DATUM AND 0FH` is valid; `DATUM AND0FH` is invalid).

An instruction mnemonic in parentheses is a legal expression whose value is the instruction's
encoding — e.g. `DB (JMP)` deposits `C3H`. Example: `MVI A,LOC SHR 8` loads the high byte of
label `LOC`; `MVI D,18+10H/2` loads `18+8 = 26 (1AH)`.

## 5. 8080 instruction set support

The assembler accepts the full standard Intel 8080 mnemonic set. Grouped as in the manual:

- **Data statements:** `DB` (define byte(s)), `DW` (define word), `DS` (define storage) — see §6.
- **Carry bit:** `STC` (37), `CMC` (3F).
- **Single register:** `INR`, `DCR` (reg/mem), `CMA` (2F), `DAA` (27).
- **No-op:** `NOP` (00).
- **Data transfer:** `MOV dst,src` (40–7F, excluding `MOV M,M`=76/HLT), `STAX B/D` (02/12),
  `LDAX B/D` (0A/1A).
- **Register/memory → accumulator:** `ADD ADC SUB SBB ANA XRA ORA CMP` (op groups 80–BF).
- **Rotate accumulator:** `RLC` (07), `RRC` (0F), `RAL` (17), `RAR` (1F).
- **Register pair:** `PUSH`/`POP` (PSW/B/D/H), `DAD` (B/D/H/SP), `INX`, `DCX`, `XCHG` (EB),
  `XTHL` (E3), `SPHL` (F9).
- **Immediate:** `LXI rp,data16` (3 bytes), `MVI r,data`, and `ADI ACI SUI SBI ANI XRI ORI CPI`
  (2 bytes each). LXI takes a 16-bit immediate; all other immediates are 8-bit.
- **Direct addressing (3 bytes):** `STA` (32), `LDA` (3A), `SHLD` (22), `LHLD` (2A).
- **Jumps:** `PCHL` (E9), `JMP JC JNC JZ JNZ JM JP JPE JPO` (3-byte, low address byte first).
- **Calls:** `CALL CC CNC CZ CNZ CM CP CPE CPO` (3 bytes, push return address).
- **Returns:** `RET RC RNC RZ RNZ RM RP RPE RPO` (1 byte).
- **Restart:** `RST n` where operand evaluates to 0–7; control transfers to `n × 8`.
- **Interrupt flip-flop:** `EI` (FB), `DI` (F3).
- **I/O:** `IN data` (DB), `OUT data` (D3) — 2 bytes.
- **Halt:** `HLT` (76).

Standard 8080 semantics apply (two's-complement arithmetic; condition bits per Intel spec;
16-bit values stored low byte first). A full mnemonic index with per-instruction page numbers
appears in the manual's Appendix.

## 6. Data-definition pseudo-instructions

| Directive | Operand | Effect |
|-----------|---------|--------|
| `DB` | list of 8-bit expressions and/or `'ASCII'` strings | Stores each value/character in successive bytes from LABEL. E.g. `DB 0FFH,'ABC',-05H` → `FF 41 42 43 FB`. |
| `DW` | list of 16-bit expressions | Stores each as two bytes, **least-significant byte first** (LABEL, then LABEL+1). E.g. `DW 0F4C1H` → `C1 F4`. |
| `DS` | expression | Reserves that many bytes; contents are **not** initialized (do not assume zero). |

## 7. Pseudo-instructions (directives)

Pseudo-instructions generate no object code (except the data statements above); they supply
the assembler with data for later use. Their names take **no colon**. `MACRO`, `EQU`, and
`SET` **require** a name in the label field; the others take an optional label.

| Directive | Form | Effect |
|-----------|------|--------|
| `ORG` | `ORG exp` | Set the location counter to the 16-bit value `exp`. If no ORG precedes the first instruction, assembly begins at location 0. |
| `EQU` | `name EQU exp` | Assign `name` the value of `exp`. **May not be redefined** — the name may appear only once. |
| `SET` | `name SET exp` | Like EQU, but the symbol **may be redefined** by a later SET. |
| `END` | `END` | Marks the end of the program; **required**, exactly one, must be the last statement. Object output and listing then begin. |
| `IF` / `ENDIF` | `IF exp` … `ENDIF` | Conditional assembly: if `exp` evaluates to zero, the enclosed statements are skipped; otherwise assembled normally. |
| `MACRO` / `ENDM` | `name MACRO list` … `ENDM` | Define macro `name` (see §8). |

## 8. Macro facility

A macro associates a `name` with a group of statements (the *macro body*) delimited by
`MACRO` and `ENDM`. Three aspects:

- **Definition** — the name and its body, given once. The body may contain assembly-language
  instructions, pseudo-instructions **except `MACRO`/`ENDM`**, comments, or references to
  *other* macros. Macros may **not** define other macros.
- **Reference** — the macro name placed in the CODE field of a later statement; its operand
  supplies the actual parameters.
- **Expansion** — the assembler substitutes the actual parameters into the body and assembles
  the resulting statements. Each expanded statement must itself be legal (an illegal
  expansion, e.g. `DCX L`, is flagged as an error).

### 8.1 Parameters (dummy parameters / "list")
The `list` after `MACRO` names the dummy parameters. On reference, the first operand string
replaces every occurrence of the first dummy parameter, the second the second, etc.

- Fewer actuals than formals → a **null string** is substituted for the rest.
- More actuals than formals → the extras are **ignored**.

Example:
```
PMSG:   MACRO  P1,P2,P3
        LXI    H,P2
        MVI    B,P1
        CALL
        ENDM
        PMSG   MSG1,CNT,ADDR   →   LXI H,MSG1 / MVI B,CNT / CALL ADDR
```

### 8.2 Local vs. global labels and names
- **GLOBAL**: known to any statement in the program. **LOCAL**: known only within one macro
  expansion.
- **Instruction labels** in a macro body are **local** by default (so repeated references
  don't collide). To make one **global**, follow the label with **two colons** (`CONTU::`) in
  the definition; a global macro label may be generated only once — a second reference is an
  error.
- **EQU names** generated inside a macro are always **local** to that expansion.
- **SET names**: if the name was already defined globally by an earlier SET, the generated SET
  **changes the global value** thereafter; if not previously defined, it is **local** to the
  current expansion (referencing it outside is a `**ERROR**`, undefined globally).

### 8.3 Delayed evaluation
A macro parameter is normally evaluated at reference time. Enclosing a parameter in
**quotation marks** delays evaluation: the character string is placed into the body and
evaluated at expansion. E.g. passing `LABL` substitutes its current value (5), whereas passing
`"LABL"` substitutes the characters `LABL`, which are then re-evaluated during expansion.

## 9. Listing and object output format

- **Object output** is executable 8080 code in **Intel hexadecimal format**, written to the
  output file (PASS 3 or 4).
- **Source listing** shows the assembled bytes alongside the source and may go to the list
  device (PASS 2) or the output file (PASS 5); PASS 4 produces both listing and object file.
- Errors are flagged **in the listing** by a single-letter code at the offending line; if a
  line has multiple errors, **only the first is shown** (§10).

The manual's worked examples show assembled bytes in a right-hand "ASSEMBLED DATA (Hex)"
column beside the CODE/OPERAND, but the exact column layout of a real listing line is not
given verbatim in the scan.

## 10. Error codes

The assembler flags errors with a single-letter code on the output listing (only the first
error per line is indicated):

| Code | Meaning | Typical cause / example |
|------|---------|-------------------------|
| `B` | Balance Error | Unbalanced parentheses or quotes (`ORG $/256+1)*256-$`; `DB 'A` ). |
| `E` | Expression Error | Poorly constructed expression: missing operator, omitted comma, or misspelled opcode (`ORG ($/256+1)256-$`). |
| `F` | Format Error | Missing or extraneous operand (`MOV A` ; `MOV A,B,C`). |
| `I` | Illegal Character | Illegal ASCII char, or a digit too large for the number base (`02B`, `79Q`). |
| `M` | Multiple Definition | Symbol or macro defined more than once; flagged on all definitions/references. Symbols must be unique in their first five characters. |
| `N` | Nesting Error | `ENDIF`, `ENDM`, or `END` improperly nested (e.g. `ENDIF` with no matching `IF`). |
| `P` | Phase Error | A symbol's value changed between pass one and pass two (e.g. `ORG BEGIN` before `BEGIN EQU 5`). |
| `Q` | Questionable Syntax | Omitted or misspelled opcode (`MVO A,B`). |
| `R` | Register Error | Register invalid for the operation (`INR 9`). |
| `S` | Stack Overflow | Assembler's expression-evaluation stack exhausted: overlong strings, excessive nested macros/IFs, or too-complex expressions. |
| `T` | Table Overflow | Symbol-table space exhausted: too many symbols or too much macro text. Add memory or reduce labels. |
| `U` | Undefined Identifier | Operand symbol never defined in any label field. |
| `V` | Illegal Value | Operand/expression out of range for the field (e.g. `MVI A,257` — MVI must be 0–255). |

## 11. Not distilled here

The scan also contains the full per-instruction prose descriptions with condition-bit effects
and complete `MOV`/arithmetic op-code tables (standard Intel 8080 semantics; summarized in §5),
a worked `DAA` bit-level example, the cover errata sheet (folded into §2.1), and the complete
alphabetical Mnemonic Index appendix with page references. These add no emulator-relevant
behavior beyond the standard 8080 ISA and are omitted.
