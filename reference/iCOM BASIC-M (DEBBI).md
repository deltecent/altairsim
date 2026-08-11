# iCOM BASIC-M (DEBBI) — Disk Extended BASIC for the iCOM Floppy Disk System

Source: [`iCOM BASIC-M (DEBBI).pdf`](#) — "OPERATOR'S MANUAL, BASIC-M", iCOM
(Microperipherals, A Division of Pertec Computer Corporation, Canoga Park CA),
© December 1976, 19 numbered pages plus front matter and two appendices. Scanned
image PDF (no text layer). Provenance: deramp.com iCOM floppy software archive
(`.../altair/software/icom_floppy/FDOS/`). "DEBBI" (Disk Extended BASIC by iCOM) is
the informal name for this product; the manual itself calls it BASIC-M.

BASIC-M is a disk-resident, conversational BASIC interpreter for the iCOM Floppy Disk
System running under the **FDOS-M** or **FDOS-II** operating system (see
`reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md` for the floppy hardware, and
the FDOS references for the OS it loads under). It is an 8080 program that does all
arithmetic in **BCD**, with 6-digit precision. It sits alongside the Text Editor,
Macro Assembler, and Relocating Assembler as one of the tools that run on the iCOM
FDOS. A BASIC program "generally requires no more than 8K bytes of memory."

## 1. Invocation under FDOS — the RUNGO command line

BASIC-M is loaded and started via the FDOS **RUNGO** (RunGo) command, which names the
interpreter file (`BASIC`) and, optionally, an **input file** and an **output file**.
The command syntax is `RUNGO,BASIC[,"ifile"][,"ofile"]`. Once started, BASIC prints
`READY`. Inside BASIC, **DLOAD** reads the input file into memory and **DSAVE** writes
the current program to the output file and returns to FDOS. The four usage forms:

| Goal | Command line |
|---|---|
| Conversational only; no save/load | `RUNGO, BASIC` |
| Bring in and run a program; no output | `RUNGO, BASIC, "ifile"` |
| Enter a program and write it out | `RUNGO,BASIC,,"ofile"` (note the doubled comma — empty ifile) |
| Bring in a program and write it back out | `RUNGO,BASIC,"ifile","ofile"` |

The short form used elsewhere (companion note) is written positionally, e.g.
`DEBBI,PROGA,PROGB` where `PROGA` = input file, `PROGB` = output file.

Typical interactive session after loading: `DLOAD` (load ifile) → `READY` → `LIST` →
`RUN` → `DSAVE` (write program to ofile and exit to FDOS). Filenames in OPEN/DLOAD
contexts are quoted strings.

## 2. Data, variables, and program entry

- **Number range:** `.E-127` to `.999999E+127` (i.e. ~1E-127 to ~1E+127), 6-digit
  precision; numbers auto-rounded to fit. Formats accepted/displayed: integer (`153`),
  decimal (`34.52`), exponential (`136E-2`).
- **Variable names:** one or two characters; first char must be a letter, second may be
  letter or digit (e.g. `A`, `B5`, `X`, `D1`).
- **Statement numbers:** 1 – 65000; each number used only once; program is
  auto-sequenced by statement number regardless of entry order. Insert between two
  lines by choosing an intervening number; retype a number to replace that line.
- **Line limits:** up to 72 characters/spaces per statement (line). Multiple statements
  per line separated by `;` (semicolon). A `;` may **not** terminate a line — end with
  carriage return. Commas separate multiple arguments within a statement/command.
- **Spaces** are optional/ignored except inside quoted strings (`LET E=M*C*C` ≡
  `LETE=M*C*C`).
- **Editing keys:** `@` erases the entire current line (before CR); `SHIFT/RUB`
  (rubout) deletes single characters — strike N times to delete N characters, then
  retype. Line termination = carriage return (line feed automatic).
- **REM** is a non-executed remark; it may **not** be terminated by `;` (anything after
  `;` on a REM line — e.g. a following `LET` — is not recognized/executed). Give the
  LET its own line number instead.

## 3. Commands (Section II) — no line number, executed immediately

| Command | Effect |
|---|---|
| `CLEAR` | Set all variables to zero; reset READ pointer; reinitialize program to run from start. May also be used as a statement in subprograms and to exit FOR-TO loops. |
| `LIST` / `LIST (n),(n)` | List program to console. With two args, list from first line number through second; no args lists whole program. |
| `RESTART` | Clear all error conditions without altering the program; return to command mode. |
| `NULL` | Transmit null character codes to the console after each carriage return (pad for slow terminals). |
| `RUN` | Begin execution at program start; resets the data pointer and performs a CLEAR. |
| `NEW` | Erase entire program; reset all pointers, variables, and working storage. |
| `DLOAD` | Load a BASIC-M program from the disk file named by "ifile" in the RUNGO line. |
| `DSAVE` | Write the current program to the disk file named by "ofile" in the RUNGO line (then exits to FDOS). |
| `@` | Delete the entire current line (must be used before carriage return). |
| `SHIFT/RUBOUT` | Delete a single character (repeat to delete more). |
| `,` (comma) | Establishes more than one argument in a command, e.g. `PRINT Y, "NEXT", X`. |

**Direct execution** (Section II.B): statements can be run without a line number as
immediate commands (calculation / debugging). Those usable directly include
`DIM (var)(exp)`, `GOTO (n)`, `IF (rel exp) THEN (n)`, `LET (var=exp)`, and `PRINT`
(also GOTO/LET/IF/DIM/PRINT noted throughout). Example: `PRINT "AREA IS",PI*R*R`.

## 4. Statements (Section III) — four types

All program statements: statement number, body, carriage-return terminated. The four
types are **Declaration**, **Assignment**, **Input/Output**, and **Control**.

### 4.1 Declaration statements

| Statement | Syntax | Effect |
|---|---|---|
| `DATA` | `DATA (n),(n),(n)` | Supplies values, combined sequentially (regardless of position in program) into one data list. |
| `READ` | `READ (var),(var),(var)` | Reads next value(s) from the data buffer into the named variable(s), sequentially. |
| `RESTORE` | `RESTORE` | Reset the data buffer pointer to the beginning (undo READ advancement). |
| `DIM` | `DIM (var)(exp)` | Allocate a **single-dimension** array. Max array size **10,000** elements; all elements zeroed. An array referenced without DIM is assumed 10 elements from first reference. An array may be dimensioned **only once** (statically or dynamically). DIM may also be a direct command. |

### 4.2 Assignment statements

- `LET (var)=(exp)` — the word `LET` is optional. `=` is the replacement operator.
  Examples: `LET B=827`, `B5=87E2`, `R=(X*X)/2*b`. May be a direct command.
- **Mathematical operators**, in evaluation order (left-to-right within equal
  precedence): `-` unary negate; `*` and `/` multiply/divide; `+` and `-` add/subtract.
  Parentheses control order (innermost first). E.g. `R=A+B-C/2*3` evaluates `C/2`, then
  `×3`, then `A+B −` that.

### 4.3 Control statements

| Statement | Syntax / notes |
|---|---|
| `FOR`…`NEXT` | `FOR (var)=(exp1) TO (exp2) STEP (exp3)` … `NEXT (var)`. STEP defaults to 1. Loop runs at least once even if TO < initial. Loop var ends equal to the value that caused termination (e.g. `FOR J=1 TO 9` leaves J=10). FOR/final/STEP expressions evaluated only on loop entry. `NEXT` without a variable steps the most recent FOR. **Nesting up to five deep**; the same variable may not be used in two nested loops. |
| `GOTO` | `GOTO (statement n)` — unconditional jump. Also a direct command. |
| `IF`…`THEN` | `IF (rel exp) THEN (statement n)` (jump if true, else fall through) or `IF (rel exp) THEN (BASIC statement)` (execute statement if true, then continue). Also a direct command. |
| `STOP` | Halt execution, return to command mode, display `"STOP IN LINE(n)"`. Resume with `GOTO`. |
| `END` | Assigned the last (highest) statement number; halts execution and returns to command mode. E.g. `500 END`. |

**Relational operators** (evaluated after arithmetic). A true relation has value **−1**,
false has value **0**.

| Char | Meaning |
|---|---|
| `=` | Equal |
| `<>` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `=>` | Greater than or equal |

Examples: `10+2*3<25*2` → false; `(12>10)` → −1; `(A<>A)` → 0.

### 4.4 Input/Output statements

Only **one file of each type** (one read, one write) may be open at a time. Example
sequence: `OPENR "file1"` / `OPENW "file2"` / `DSKIN P` / `DSKOUT P` / `CLOSE` / `STOP`.

| Statement | Syntax | Effect |
|---|---|---|
| `OPENR` | `OPENR "file-name"` | Open a file for disk READ (before DSKIN). |
| `OPENW` | `OPENW "file-name"` | Open a file for disk WRITE (before DSKOUT). |
| `DSKIN` | `DSKIN (var)` | Bring data in from the open disk read-file. |
| `DSKOUT` | `DSKOUT (var)` | Output data to the open disk write-file. |
| `CLOSE` | `CLOSE` | Must be issued when all data is written; flushes the last data buffer to the file. |
| `PRINT` | `PRINT (var)` / `"string"` / `(exp)` / `%(Z)(E)(F)(N)` | Print to console. Combine items with commas. A `PRINT` with no argument prints a blank line. If the next print position ≥ **56**, a CR is issued before the value. Also a direct command. |
| `:` (colon) | `10 : X,Z,3` | Colon may be substituted for the word `PRINT`. |

**TAB** function: `PRINT TAB(2),B,TAB(2*R),C` — positions output; the argument may be an
expression; used to align columns.

**Formatted print** (Section III.E.5b–c): default is 6-digit precision, low-order digit
rounded, trailing zeros suppressed, automatic choice among decimal/integer/exponential.
Format is overridden by embedding a spec between two percent signs (`%…%`) in the output
list. Codes:

| Code | Meaning |
|---|---|
| `F` | Free format (automatic) |
| `Z` | Print trailing zeros |
| `E` | Print in exponential format |
| `N` (1–6) | Print N places to the right of the decimal point |

A format persists until a new one is given; return to default with `%(exp)%`.
Examples: `110 PRINT %6E%`; `200 PRINT %Z2%,A,B; PRINT %Z3%,3D,%%`.

## 5. Subprograms (Section IV): subroutines and functions

### 5.1 GOSUB / RETURN

`GOSUB (statement n)` … body … `RETURN`. On RETURN (or falling out of the routine),
control returns to the statement after the GOSUB. **Nesting up to six deep**; a
subroutine may not call itself (no recursion). Avoid reusing variables already used
elsewhere. (Note: the GOSUB-RETURN nest limit is stated as **5 deep** in the NE error
message on p.19 — the scan gives 6 in Section IV and 5 in the error list; discrepancy in
source.)

### 5.2 Built-in functions

| Function | Definition |
|---|---|
| `ABS(exp)` | Absolute value |
| `INT(exp)` | Largest integer ≤ argument |
| `RND(exp)` | Pseudo-random number in 0.0–1.0. The argument does not affect the result (needed for syntax only); the generator is reset by CLEAR. |
| `SGN(exp)` | +1 if argument ≥ 0, −1 if negative |
| `SQR(exp)` | Square root |
| `SIN(exp)` | Sine (argument in radians) |
| `COS(arg)` | Cosine (argument in radians) |
| `TAB(exp)` | Position output characters in a PRINT statement |
| `PASS(exp)` / `CALL(exp)` | Link to 8080 assembly language (see below); usable as direct commands |

Note: the Appendix A index lists a `TAN` (tangent) function on p.17, but the function
table on p.17 of the scan does **not** include TAN — likely an index-only artifact;
TAN's presence is uncertain.

### 5.3 PASS and CALL — 8080 assembly-language linkage

`PASS(exp)` evaluates its argument as a **16-bit integer** and stores it temporarily in
the monitor; when a subsequent `CALL` links to an 8080 routine, that 16-bit value is
passed to the assembly code in the **D,E register pair**. `CALL(exp)` evaluates its
argument as a 16-bit **address** and transfers control there; the assembly routine loads
register pair **H,L** with a return value, which becomes the value of `CALL`. Examples:
`B=PASS(X2)`; `Y6=CALL(5.2*A4)`. Worked example (p.18): `L=PASS(X/6)` then
`K=CALL(Y5)` with `Y5=4280` links to assembly at address 4280, K ← value returned in
H,L.

## 6. Error messages (Section V)

Two-letter codes displayed on the console:

| Code | Meaning |
|---|---|
| `AE` | Floating-point arithmetic error — divide by zero, or result too large to represent. (Underflow → 0, no error.) |
| `CE` | Command error — a command not processable in direct execution mode. |
| `DE` | Dimension error — DIM more than once in a program. |
| `FE` | File reference error. |
| `IA` | Illegal argument. |
| `IE` | Input error — bad numeric response to INPUT or DSKIN. |
| `IS` | Illegal syntax. |
| `LE` | Limit error — array index, TAB value, or other integer exceeds its allowable limit. |
| `LO` | Line overflow — 72-character limit exceeded. |
| `NE` | Nesting error — FOR-NEXT stack exceeds 6 deep, FOR without matching NEXT, or GOSUB-RETURN exceeds nest limit of 5. |
| `NS` | Negative square-root argument. |
| `OV` | Overflow of storage — no room for text, symbol table, array space, or program data. |
| `RE` | Read error — data buffer full / READ exceeded the number of DATA values. |
| `UL` | Undefined line — bad line-number reference in GOTO, GOSUB, or IF. |

## 7. Character / symbol summary (Appendix B)

Editing/control: `@` (clear line buffer), `SHIFT/RUB` (clear single char), `;`
(multiple statements per line), `,` (multiple arguments), `:` (substitute for PRINT),
`%` (format delimiter, returns to default in PRINT/TAB). Operators: `*` `/` `+` `-`,
`-()` negate, `()` control negation/precedence, `" "` literal string. Relational: `=`
`<>` `<` `>` `<=` `=>`.

## 8. Feature summary (Section I.A)

Multiple statements per line; BCD arithmetic; formatted output; save/recover programs
from diskette; FUNCTION subprograms; direct-mode execution for immediate
calculation/debugging; programs generally ≤ 8K bytes; PASS/CALL for linking to other
8080 machine-language programs.

## Not distilled here

Nothing of reference value was omitted — the scan is only 19 pages of body plus a
title page, table of contents, Appendix A (an alphabetized command/statement index by
page) and Appendix B (character-set index), both of which are cross-referenced above.
Several intervening pages are intentionally blank in the scan. No schematics, listings,
or hardware detail appear in this manual.
