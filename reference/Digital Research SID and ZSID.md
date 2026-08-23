# Digital Research SID / ZSID — CP/M Symbolic Instruction Debugger

Sources:
- [SID_ZSID.pdf](#) ("SID Users Guide", Digital Research, © 1978) — the full **8080** SID manual
  (sections 1–5: operation, symbolic expressions, the command set, the utilities, and worked
  sample debugging sessions). This is the primary source for the prose below.
- [zsid-m.pdf](#) ("ZSID Symbolic Instruction Debugger Command Summary, Z-80 Version", Digital
  Research, © 1979, 1981), OCR'd and re-typeset by Miguel I. García López (2007), from
  www.cpm.z80.de — the **Z80** command summary, plus the reprinted Z80 instruction set.

**SID** ("Symbolic Instruction Debugger") is Digital Research's CP/M debugger for the **8080**;
**ZSID** is the identical tool retargeted to the **Z80**. Both **expand on
[`DDT`](CPM%202.2%20Manual.md)** (the "CP/M Dynamic Debugging Tool"), adding real-time
breakpoints, fully-monitored (single-step) execution, symbolic assembly/disassembly, memory
display/fill, and dynamically-loaded **utilities** (traceback and histogram). They are CP/M
`.COM` programs that run on the emulated CPU; `altairsim` does **not** emulate them. This
reference is kept because the simulator's own monitor/debugger deliberately **mirrors this
command family** (`X`/`T`/`G`/`L`/`D`/`A`/`S`, the `CZMEI A=… B=… …` register line, and RST-7
breakpoints — see the DDT notes in [CP/M 2.2 Manual](CPM%202.2%20Manual.md)), so this is the
authoritative period statement of what those commands mean.

**SID vs ZSID.** The invocation, memory model, expression grammar, command set, utility
mechanism, and register/flag display are **identical**. The differences are only:

| | SID | ZSID |
|---|---|---|
| Target CPU | 8080 | Z80 |
| `A`/`L` mnemonics | Intel 8080 | Zilog Z80 |
| Copyright | 1978 | 1979, 1981 |
| Size when operating | ~6K (min 20K CP/M for self-relocate setup; ~10K left for the test program) | ~10K |
| `-A` (remove assembler/disassembler) frees | ~1½K (→ SID ~4½K) | ~4K |

Everything below describes **both**; Z80-only notes are called out. This distillation **omits**
the reprinted Z80 instruction-set table (ZSID's `A` input / `L` output) — for opcodes/mnemonics
use [Zilog Z80 CPU](Zilog%20Z80.md) — and the worked sample debugging sessions (SID Users Guide
§5). The few places ZSID's assembler/disassembler differs from a normal Z80 assembler (`MAC`)
are in §6.

---

## 1. Invocation and startup

SID/ZSID loads into the **topmost** part of the Transient Program Area (TPA), overlaying the CCP,
and self-relocates directly below the BDOS; it then **lowers the BDOS entry (`JMP` at 0005)** to
reflect the reduced free memory. Because it self-relocates, it is independent of the machine's
memory size. Command forms (letters `x.y u.v` are file names):

| Form | Effect |
|---|---|
| `SID` / `ZSID` | Start with no test program (examine memory, or use the built-in assembler). |
| `SID x.y` | Load test program `x.y` (`y` normally `COM`) into the TPA — **loaded, not executed** (control passes only via `C`/`G`/`T`/`U`). |
| `SID x.HEX` | Load `x.HEX` in Intel hex format; the initial PC comes from the hex file's last record (the `END` address) unless zero, else the TPA base. |
| `SID x.UTL` | Load **and relocate** a utility `x` (see §5); the utility moves itself just below SID. |
| `SID x.y u.v` | Load `x.y` **with** symbol table `u.v` (normally `x.SYM`). |
| `SID * u.v` | Skip the code load; load **only** the symbol table `u.v`. |

Examples: `SID DUMP.COM`, `SID SAMPLE.HEX`, `SID DUMP.COM DUMP.SYM`, `SID TRACE.UTL`,
`SID * DUMP.SYM`.

**Startup responses.** A load error prints `?`. On success:

```
NEXT  PC   END
nnnn  pppp eeee
```

`nnnn` = next free address after the loaded program (usually the start of its data area),
`pppp` = initial program counter (TPA base for a `.COM`), `eeee` = last memory location available
to the test program. When a symbol load begins, the message `SYMBOLS` prints first — so a `?`
**before** `SYMBOLS` is a code-load error, a `?` **after** it is a symbol-load error.

**Command input.** SID prompts with `#`. Each command is a single letter (lower case folded to
upper) plus optional parameters, terminated by carriage return; **a space serves as a comma
delimiter**. All CP/M line editing applies, max **64** characters per line. Long typeouts (e.g.
`D`) are aborted by pressing any key (a return suffices). CP/M line-editing controls:

| Key | Function |
|---|---|
| ctl-C | CP/M system reboot, return to CCP (this is how SID terminates) |
| ctl-E | Physical end-of-line |
| ctl-P | Print console output (on/off toggle) |
| ctl-R | Retype current input line |
| ctl-S | Stop/start console output |
| ctl-U | Delete current input line |
| ctl-X | Same as ctl-U |
| ctl-Z | End of console input (not used in SID) |
| rubout | Delete and echo last character |

---

## 2. Operand and expression syntax

Command parameters are **symbolic expressions**, reducing to a 16-bit value (0–65535).

- **Literal hexadecimal** (the default base): digits `0-9 A-F`. One-to-four digits; more than
  four keep the **rightmost** four. Lower-case hex digits are folded to upper case *outside*
  string apostrophes. E.g. `1→0001`, `100→0100`, `fffe→FFFE`, `10000→0000`, `38001→8001`.
- **Literal decimal**: prefix `#`, digits `0-9`. Converted to hex, then padded/truncated by the
  hex rules. E.g. `#9→0009`, `#10→000A`, `#256→0100`, `#65535→FFFF`, `#65545→0009`.
- **Literal character**: graphic ASCII in paired apostrophes `'`. **No case folding.** Leftmost
  char = most significant byte, rightmost = least; length 1 is left-padded with `00`; length >2
  keeps the **rightmost two**; `''` inside a string reduces to one `'`. E.g. `'A'→0041`,
  `'AB'→4142`, `'aA'→6141`, `''''→0060`, `' A'→2041`, `'A '→4120`. (Upper-case ASCII starts at
  `41`, lower at `61`, space `20`, apostrophe `60`.)
- **Symbolic references** (when a symbol table is present), `s` matching a symbol (1–16 chars):
  - `.s` — the **address** of `s`.
  - `@s` — the **16-bit word at** `.s` (8080/Z80 low-byte-first, so a word display reverses the
    two stored bytes).
  - `=s` — the **8-bit byte at** `.s`.

  With `0100 GAMMA  0102 DELTA` and memory `0100:02 0101:3E 0102:4D 0103:22`:
  `.GAMMA→0100`, `.DELTA→0102`, `@GAMMA→3E02`, `@DELTA→224D`, `=GAMMA→0002`, `=DELTA→004D`.
- **Qualified symbols** for duplicates (from separate modules or nested scopes): `s1/s2/.../sn`.
  The search runs from the first symbol loaded to the last, matching `s1`, then scanning onward
  for `s2`, and so on; no match prints `?`. E.g. with `0100 A 0300 B 0200 A 3E00 C 20F0 A 0102 A`:
  `.A→0100`, `@A→3E02`, `.A/A→0200`, `.C/A/A→0102`, `=C/A/A→004D`, `@B/A/A→20F0`.
- **Operators `+` and `-`**: the sequence (no embedded blanks) is evaluated **left to right**,
  overflow/underflow ignored, accumulating a 16-bit value. A binary `+`/`-` adds/subtracts the
  next value to/from the accumulated value.
- **Unary `+`/`-` (last-value)**: a leading `+x` uses the **previous completed expression** as
  the starting value (zero at startup) — so `DFE00+#128,+5` ≡ `DFE80,FE85`. A leading `-x`
  computes `0-x` — so `DFF00-200,-#512` ≡ `DFD00,FE00`, and `R-100` ≡ `RFF00`.
- **Stack reference `^`**: a run of *n* `^` characters yields the *n*'th **stacked value** in the
  test program (without changing its stack). Chiefly used with `G` to break on return from a
  subroutine (`G,^`).

---

## 3. Command set

Letters: `A C D F G H I L M P R S T U X` (lower case folded to upper). Parameters `s f d p a b c`
are symbolic expressions.

### A — Assemble (in-line)

`As` begins in-line assembly at `s`, prompting each successive address until a **null line** or a
lone `.` is entered; delimiters between mnemonic and operands are spaces or tabs. `A` alone
resumes from the last assembled/listed/traced address. `-A` **removes** the
assembler/disassembler module (frees space; see the size table), **discards all symbol info**,
and disables later `A`/`L`; traces/backtraces then show only hex. Operands are symbolic
expressions (§2). An invalid mnemonic/operand prints `?` and returns to command mode. SID
assembles **8080/Intel** mnemonics; ZSID assembles **Z80/Zilog** (§6).

### C — Call

`Cs` calls absolute location `s` **without disturbing the test program's CPU state**, entering
with `BC=0000 DE=0000`; the called routine returns to SID via `RET`. `Cs,b` sets `BC=b`;
`Cs,b,d` sets `BC=b DE=d`. Chiefly used to drive utilities (`C.INITIAL`, `C.DISPLAY`), but also
useful to run a test subroutine or a BDOS setup call before execution.

### D — Display memory

Byte forms `Ds`, `Ds,f`, `D`, `D,f`; word forms `DWs`, `DWs,f`, `DW`, `DW,f`.
`Ds` dumps from `s` for a **half screen (12 lines)**; `Ds,f` dumps `s` through `f`; `D`/`D,f`
continue from the last displayed address (or from `HL` after a breakpoint). Byte lines:
`aaaa bb bb … bb  cc…cc` — up to 16 hex bytes plus their ASCII (a `.` for non-graphic). Byte
mode **normalizes to modulo-16 boundaries**; word mode does **not**. Word lines pack up to 8
16-bit values (low-byte-first storage, shown MSB-first): `aaaa wwww wwww … wwww  cc…cc`.

### F — Fill memory

`Fs,f,d` stores the 8-bit value `d` in every location from `s` through `f` (inclusive). It is
the operator's responsibility not to fill CP/M or SID's own resident area.

### G — Go (run with breakpoints)

Forms: `G`, `Gp`, `G,a`, `Gp,a`, `G,a,b`, `Gp,a,b`, and their symbol-disabling `-G…`
counterparts. `G` runs from the current PC in **real time**, no breakpoints; `Gp` first sets
`PC=p`. `G,a` sets one temporary breakpoint at `a`; `G,a,b` sets two. Control returns to SID on
a breakpoint, a **pass point**, or an externally-supplied **RST 7** (front panel or the program
itself). The break prints:

```
*a  .s
```

(`.s` = first symbol matching `a`, if any). **The instruction at a temporary breakpoint is *not*
executed** — the break occurs before it; temporary breakpoints are cleared when SID regains
control. `G,^` sets a breakpoint at the top-of-stack value — i.e. run to the **return** of the
current subroutine. `-G…` suppresses intermediate pass-point traces until the pass count reaches
1. Temporary breakpoints may be mixed with permanent pass points; a permanent breakpoint
**overrides** a temporary one at the same address.

### H — Hexadecimal value / conversion

`Ha,b` prints `ssss dddd` — the sum `a+b` and difference `a-b` (overflow ignored). `Ha` converts
one value: `hhhh #ddddd 'c' .s` — hex, decimal, ASCII (if graphic), and the first symbol matching
it (if any). `H` alone prints the **entire symbol table** (name + address), abortable with a
keypress.

### I — Input line (set up FCBs / command tail)

`Icccc…` initializes CP/M's default low-memory areas exactly as the CCP would on program load,
so a test program (or the `R` command) sees the file names/command tail it expects. It fills the
**default FCB `DFCB1` at 005C**, a second FCB **`DFCB2` at 006C** (= `DFCB1+0010`; overwritten by
the first file operation, so move it first if both are needed), the **current-record byte at
007C** (→ 00), and the **default buffer `DBUFF` at 0080–00FF** with the command tail (leading
length byte). Drive letters map `A/B/C/D → 01/02/03/04` (empty → 00 = default drive); names/types
are upper-cased, blank-padded, right-truncated; `*` expands to `?`s as in the CCP. The simulated
tail is limited to **63** characters (SID's line buffer). `I` is most often used with `R` to load
code/symbols after SID has started (see §3, R).

### L — List code (disassemble)

`Ls` disassembles from `s` for a half screen; `Ls,f` disassembles `s` through `f`; `L` continues
from the last listed/assembled/traced address; the `-L…` forms disable symbol lookup. Output:

```
sssss:
aaaa  opcode operand  .ttttt
```

`sssss:` labels the address `aaaa` when a symbol matches; `.ttttt` annotates an operand that
matches a symbol. Memory-referencing instructions (e.g. `INR M`) print `opcode M =hh`, where
`hh` is the byte at `(HL)` before execution. A non-CPU byte prints as `??= hh`. SID lists
**8080** mnemonics; ZSID lists **Z80**.

### M — Move memory

`Ms,h,d` copies bytes from `s` through `h` to the block starting at `d`, one byte at a time with
ascending addresses — **areas may overlap** (e.g. `M100,1FF,101` propagates the byte at 0100
through 0101–01FF).

### P — Pass counter (conditional / permanent breakpoints)

A **pass point** is a monitored PC address with an associated **pass count** 1–FF (1–#255),
decremented each time the address executes; when it reaches **1** the pass point becomes a
**permanent breakpoint**. **Unlike a temporary `G` breakpoint, a pass point stops *after* the
instruction executes.** Forms: `Pp` (set at `p`, count 1), `Pp,c` (count `c`), `P` (display
active points as `cc pppp .sssss`), `-Pp` (clear `p`, ≡ `Pp,0`), `-P` (clear all). **Up to 8**
pass points may be active. Each pass prints `cc PASS pppp .sssss` followed by the register state;
`-G`/`-U` suppress the intermediate traces until the count hits 1. A keypress during a pass trace
aborts execution after the current pass-point instruction. Pass points and temporary breakpoints
coexist; `T`/`U` may trace while pass points are set.

### R — Read code / symbols

Used after `I` to load code, symbols, and utilities into the TPA. `R` loads what `I` staged
(no bias); `Rd` adds the displacement `d` to every load and symbol address (no overflow check, so
`R-200` loads 0200 **below** the origin). File-type rules: `.HEX` loads to its Intel-hex
addresses; `.UTL` is a utility (self-relocates below SID and **discards existing symbols**); all
other types load at the TPA base. **Symbol loads are cumulative** (except when a `.UTL` clears
them), so modules' `.SYM` files can be loaded selectively:

```
I* MAIN.SYM
R
```

`SYMBOLS` prints before the symbol load (same error-locating rule as startup). This is how a
memory image assembled from several `.HEX`/`.SYM` modules is rebuilt, `SAVE`d, and later
re-debugged with per-module symbols (symbol files can be concatenated with `PIP`).

### S — Set memory (examine / deposit)

`Ss` sets bytes (8-bit / character-string mode); `SWs` sets words (16-bit). Each address is
displayed with its current content; type a value to change it (and advance), or a bare carriage
return to leave it unchanged (and advance). Input ends on **invalid input** or a lone `.`. In
byte mode, a leading quote `"` enters a **long ASCII string** (`"cccc…`, no case folding),
terminated by carriage return, with the next prompt at the first unfilled location. `Ss` rejects
a word value with a non-zero high byte (`?`).

### T — Trace (single-step, monitored)

Forms: `Tn` (trace `n` steps), `T` (one step), `Tn,c` / `T,c` (utility form — `CALL c` after each
step, for data collection), the symbol-disabling `-T…`, and the **trace-without-call** `TW…` /
`-TW…`. Each step shows the register state (see `X`) and the decoded instruction **before** it
executes, with a `*bbbb` final PC. **`TW`** ("trace without call") traces only the **current
subroutine level** — a `CALL`/`RST` runs its callee in real time and tracing resumes on return —
useful to isolate mainline code from library calls. Tracing **stops at the BDOS** (runs it in
real time, resumes on return) and at **ROM** (entered via call/jump; the return address is taken
as the top of the test-program stack). **Abort a trace with a keypress — do *not* use RST** to
end a trace.

### U — Untrace (monitored, no register display)

`U…` parallels `T…` exactly but **does not print the register state** each step, so the program
runs fully monitored (breakable at any point, and usable for utility data collection) but far
faster to read. `-U…` additionally suppresses intermediate pass-point display until counts reach
1; `UW…` is untrace-without-call. Abort with a keypress.

### X — Examine / alter CPU state

`X` displays the machine state:

```
CZMEI A=aa B=bbbb D=dddd H=hhhh S=ssss P=pppp op sym
```

The leading field shows the five flags **C**arry, **Z**ero, **M**inus, **E**ven-parity,
**I**nterdigit(half)-carry — the flag **letter** when true, `-` when false (e.g. `C-M--`).
`A=aa` is the accumulator; `B`/`D`/`H` are the **BC/DE/HL pairs** (16-bit); `S` = SP, `P` = PC;
`op sym` is the decoded next instruction at PC (hex if `-A` is in effect) with symbolic operand.
`Xf` changes a flag, `f ∈ C Z M E I`: the current state prints; enter `1`, `0`, or a bare return.
`Xr` changes a register, `r ∈ A B D H S P`: the current value prints; enter a new expression or a
bare return. **BC/DE/HL are altered as a pair** (`XA` takes a byte; `XB`/`XD`/`XH`/`XS`/`XP` take
16-bit).

---

## 4. Symbol table

Symbols are the `.SYM` format produced by the CP/M **Macro Assembler (`MAC`/`RMAC`)**: a sequence
of *address–name* pairs, where the address is **four hex digits**, a **space**, then a symbol of
**up to 16 graphic ASCII chars**, terminated by one or more **tabs (ctl-I)** or a **CR/LF**. The
table loads **downward from SID's base** toward the test program, and shrinking free memory is
reflected in the lowered BDOS pointer. An operator may hand-create or edit a `.SYM` with the CP/M
editor as long as the format holds. Symbols power the `.s`/`@s`/`=s` references, qualified names
(§2), disassembly labels, and the symbolic column of `X`/`L`/`H`/`T`.

---

## 5. Utilities (`.UTL`)

A utility is a self-relocating program that loads just below SID (`SID x.UTL`, or via `I`/`R`);
loading it **discards existing symbols**, and running **more than one** utility per session gives
unpredictable results. Each utility exposes three entry points, added to the symbol table and
printed on load:

```
.INITIAL = iiii     (re)initialize
.COLLECT = cccc     collect data (called per step during T/U)
.DISPLAY = dddd     display accumulated data
```

Reinitialize with `C.INITIAL`/`Ciiii`; display with `C.DISPLAY`/`Cdddd`. **Collection** happens
during monitored execution — `T`/`U` with the collect address as the second argument; the
idiomatic form is `Un,.COLLECT` (or `UFFFF,.COLLECT` = up to 65535 steps, stopped early by a
keypress or a pass point).

### 5.1 HIST — execution histogram

Finds "hot spots" as a bar graph of execution frequency between two addresses. On startup/reinit
it prompts `TYPE HISTOGRAM BOUNDS`; answer `llll,hhhh`. Collect with `Un,.COLLECT`, then
`C.DISPLAY`:

```
HISTOGRAM:
  ADDR   RELATIVE FREQUENCY, MAXIMUM VALUE = mmmm
  aaaa   *****
  …
  yyyy   ******************************************
```

The range is scaled over **64 address slots** with a maximum of **64 asterisks** per bar, scaled
to the peak count `mmmm`. Re-`C.INITIAL` with a narrower range to zoom in; use `L` to identify
the exact hot instructions.

### 5.2 TRACE — dynamic backtrace

Records a wraparound buffer of up to **256** instruction addresses ending at the current break;
collection only during `T`/`U`. On load it prints `READY FOR SYMBOLIC BACKTRACE` (or
`ADDRESSES ONLY` if `-A` removed the disassembler). Collect (e.g. `U#500,.COLLECT`), then
`C.DISPLAY` prints the backtrace **most-recent instruction first**, each as `label:` / `addr
opcode sym` (same format as `L`); `C.INITIAL` clears it. Best used **with pass points** — set a
pass point at the address of interest, run to it under `U…,.COLLECT`, then display the path that
led there.

---

## 6. Assembler / disassembler quirks (ZSID vs `MAC`)

ZSID's `A` accepts, and `L` produces, standard **Z80** mnemonics (SID uses **8080/Intel**), but
ZSID's assembler differs from the `MAC` macro assembler:

- **No pseudo-operations**, and **no labels** may be inserted.
- **Operands are ZSID symbolic expressions** (§2), not `MAC` expressions.
- **Register references must be names, not numbers.**
- `LD HL,'ab'` fills **H with `'a'`, L with `'b'`** (the two-char string's rightmost char is the
  low byte) — **opposite** of the `MAC` convention.
- An **absolute address** may be given in place of the relative offset for the jump-relative
  instructions (`JR`, `DJNZ`).
- On disassembly, a non-Z80 byte prints as `??= hh`.

For the actual opcode/mnemonic tables use [Zilog Z80 CPU](Zilog%20Z80.md).

---

## 7. Implementation notes

- SID/ZSID loads at the **top of the TPA**, overlays the CCP, self-relocates below the BDOS, and
  **lowers the BDOS `JMP` at 0005** as the symbol table (which grows downward from its base)
  fills. Programs that "size" memory via the BDOS pointer must not start until **all symbols are
  loaded**. (Sizes: SID ~6K operating, needs ≥20K CP/M to self-relocate, leaves ~10K for the test
  program; ZSID ~10K.)
- The assembler/disassembler is a **separate module**; `-A` removes it (freeing ~1½K under SID,
  ~4K under ZSID), after which `A`/`L` are disabled, traces/backtraces show only hex, and **symbol
  info is discarded** (reloadable via `I`/`R`).
- Tracing **stops at the BDOS** and resumes on return; **ROM** subroutines (entered by call/jump)
  run in real time with an automatic return breakpoint. **Abort a monitored `T`/`U` run with a
  keypress; abort an unmonitored `G` run with an external RST 7** — do not use RST to end a trace.
