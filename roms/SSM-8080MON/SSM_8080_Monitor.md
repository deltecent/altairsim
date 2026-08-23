# SSM 8080 Monitor V1.0 — Cheat Sheet

Source: `SSM8080.ASM` (OCR'd by B. Beech, Apr 2014, from the SSM CB1/CB1A monitor listing).
Programmer credited in the source: **C.E. Ohme**. Boot banner: `MONITOR V1.0`.

Everything below is traced directly from the command dispatch table and handler code, not
from an external manual (none of the surviving scans are transcribed online) — treat labels
for undocumented internal hooks as inferred, not authoritative.

---

## Operating conventions

- **Prompt** is a single `.` at the start of each line.
- **Command** = one uppercase letter, `A`–`X`. Anything outside that range aborts immediately.
- **Parameters** are hex digits (no `0x`/`H` needed), separated by **space or comma**, entered
  as a fixed count for that command. Line is terminated with **CR** (Enter).
- Supplying the wrong number of parameters, a bad terminator, or an invalid command aborts
  with linefeed + `*` and drops you back to the `.` prompt. `*` is the monitor's only error
  indicator — there's no descriptive error text.
- Each command implicitly expects **2 parameters** unless noted otherwise (this is set by the
  monitor's inner loop before it dispatches to your handler).
- **No line editing.** `TI` (terminal-in) reads and echoes one character with no backspace/
  rubout handling anywhere in the source. If you fat-finger an entry, there's no way to correct
  it in place — the practical move is to type a character that isn't a valid hex digit or
  delimiter (e.g. `.` or `Z`), which fails the hex parser and drops you into the monitor's error
  handler (`*`), aborting the current command back to the `.` prompt so you can retype it clean.

---

## Quick reference

| Cmd | Name | Syntax | Function |
|---|---|---|---|
| **A** | Assign | `A<C\|R\|P\|L>=<0-3>` | Assign logical device (Console/Reader/Punch/List) to physical device 0–3 |
| **B** | Binary punch | `B<start>,<end>` | Punch memory as raw binary-format tape |
| **D** | Display | `D<start>,<end>` | Dump memory in hex, 16 bytes/line |
| **F** | Fill | `F<start>,<end>,<byte>` | Fill memory range with a constant byte |
| **G** | Go | `G[<addr>][,<bp1>][,<bp2>]` | Execute, with up to 2 optional software breakpoints |
| **H** | Hex math | `H<val1>,<val2>` | Print val1+val2 and val1−val2 |
| **K** | Kopy | `K` (no params) | Echo Reader → Punch continuously (tape duplication) |
| **L** | Load | `L<addr>` | Load a raw binary-format tape at (offset) `<addr>` |
| **M** | Move | `M<start>,<end>,<dest>` | Copy memory block to `<dest>` |
| **N** | Null | `N` (no params) | Send 60 null bytes (tape leader/trailer) |
| **R** | Read | `R<offset>` | Load an Intel-HEX-style tape, relocated by `<offset>` |
| **S** | Substitute | `S<addr>` | Interactive examine/modify memory, byte by byte |
| **W** | Write | `W<start>,<end>` | Punch memory as Intel-HEX-style ASCII records |
| **X** | eXamine registers | `X` or `X<reg>` | Dump all registers, or examine/modify one |

**C, E, I, J, O, P, Q, T, U, V** are in the jump table but point at the error handler —
reserved/unimplemented slots, not real commands.

---

## Command details

### A — Assign I/O device
```
A<device>=<n>
```
`<device>` is `C` (console), `R` (reader), `P` (punch), or `L` (list). Any characters typed
between the device letter and `=` are ignored — the parser just scans forward for `=`.
`<n>` is `0`–`3`, selecting which physical device is bound to that logical device. This writes
into a 2-bit field of a system IOBYT (via the `ADIOB` host hook) — conceptually the same
scheme CP/M uses for `CON:`/`RDR:`/`PUN:`/`LST:` assignment. All console/reader/punch/list I/O
in the monitor (`CI`, `CO`, `RI`, `PO`, `LO`, `CSTS`) is indirected through this table rather
than calling fixed UART code, so the actual device driver lives outside this file, in a
system-specific configuration block.

### B — Binary punch
```
B<start>,<end>
```
Punches memory as a raw binary tape image: a 4-null lead-in, a `<` (0x3C) sync byte, a
**count byte**, a 16-bit little-endian address, a block of up to 255 data bytes, and a running
checksum byte — repeated in blocks until `<end>` is reached, then a final `x` (0x78) sync
followed by a 16-bit address field (`0000` = no auto-jump). This is **not** the Intel-HEX
format used by R/W.

### D — Display
```
D<start>,<end>
```
Dumps memory as hex bytes, address printed at the start of each line, 16 bytes per line,
until the range is exhausted.

### F — Fill
```
F<start>,<end>,<byte>
```
Writes `<byte>` into every location from `<start>` to `<end>` inclusive. (Takes 3 parameters —
one more than the default 2.)

### G — Go / execute (with breakpoints)
```
G                       resume execution from the last saved PC, unchanged
G<addr>                 set start address to <addr>, then execute
G<addr>,<bp1>[,<bp2>]   set start address and up to two breakpoints, then execute
G,<bp1>[,<bp2>]         keep current start address, just set/change breakpoints
```
Breakpoints are real software breakpoints: the monitor saves the opcode at each breakpoint
address and patches in an `RST 1` (`CFH`); hitting it traps back into the monitor via a handler
at the `RST 1` vector, restores the original opcode, prints `*` followed by the address where
execution stopped, and returns you to the `.` prompt. Entering `0000` for a breakpoint slot
clears it.

### H — Hex arithmetic
```
H<val1>,<val2>
```
Scratch calculator: prints `<val1>+<val2>` and `<val1>-<val2>` in hex. No memory is touched.

### K — Kopy
```
K
```
No parameters. Reads a byte from the Reader device and immediately echoes it to the Punch
device, in a tight loop, until the Reader signals an end condition. Effectively a
device-to-device tape duplicator.

### L — Load
```
L<addr>
```
Loads a tape previously punched with **B** (the raw binary format above). `<addr>` behaves as
a base/offset the loader adds to addresses embedded in the tape. A `x` (0x78) sync byte in the
stream is treated as an inline jump — if the two bytes following it aren't zero, execution
transfers there automatically once the load reaches that point.

### M — Move
```
M<start>,<end>,<dest>
```
Copies the memory block `[start..end]` to a new location starting at `<dest>`. Handles both
forward and reverse-overlapping copies correctly (walks from whichever end avoids clobbering
source data before it's read). Takes 3 parameters.

### N — Null
```
N
```
No parameters. Emits 60 null bytes — used to give a mechanical punch/reader (or a slow
Teletype) leader/trailer time on tape.

### R — Read (Intel-HEX-style load)
```
R<offset>
```
Reads an ASCII paper-tape format that is, byte-for-byte, standard **Intel HEX**: `:` sync,
length byte, 16-bit address, record type (`00`=data, `00`-length record = EOF), data bytes,
two's-complement checksum. `<offset>` is added to every record's address field, so the same
tape can be relocated to load anywhere in memory (pass `0` to load at the addresses it was
punched at). If the terminating (zero-length) record's address field is non-zero, the monitor
auto-jumps there after loading — the classic "auto-run" trick embedded in what's normally
just an EOF record's address bytes.

### S — Substitute (examine/modify memory)
```
S<addr>
```
Classic examine-and-deposit loop: prints the current byte at `<addr>` followed by `-`. Type a
new hex byte to replace it and advance to the next address; type space or comma to leave it
unchanged and advance; type **CR** to stop cleanly (returns to `.` without altering the current
byte). Typing anything else invalid (not a hex digit, space, comma, or CR) doesn't cleanly
exit — it falls into the hex parser, fails, and aborts the whole command via the monitor's
error handler (prints `*`, back to the `.` prompt). Same net effect as stopping, just via the
error path rather than a dedicated exit key.

### W — Write (Intel-HEX-style punch)
```
W<start>,<end>
```
Punches the range as standard Intel-HEX ASCII records, 16 data bytes per record (type `00`),
followed by a terminating zero-length record (type `01`/EOF). Pairs with **R** for reloading.

### X — Examine/modify registers
```
X            dump all registers (read-only)
X<reg>       examine/modify registers, starting at <reg> and walking forward
```
Register letters:

| Letter | Register |
|---|---|
| A | Accumulator |
| B | B |
| C | C |
| D | D |
| E | E |
| F | Flags |
| H | H |
| L | L |
| M | HL pair (16-bit) |
| P | PC (16-bit) |
| S | SP (16-bit) |

Plain `X` is a **display-only** dump — it prints `A=xx B=xx …` with no `-` prompt and no
editing. `X<reg>` is the editable form: it shows the value at `<reg>`, prints `-`, and lets you
type a hex replacement; space advances to the **next** register in the table and keeps going,
CR exits.

---

## Unimplemented commands

`C`, `E`, `I`, `J`, `O`, `P`, `Q`, `T`, `U`, `V` all point at the same error handler in the
jump table (`TBL`). They're reserved letters in the dispatch scheme, not documented features —
likely placeholders for options never built into this particular ROM revision.

---

## Notes for hardware bring-up

- The monitor's console/reader/punch/list routines (`CI`, `CO`, `RI`, `PO`, `LO`, `CSTS`) never
  touch a UART directly — they go through an indirection table (`IOTAB`, at the base of a
  configuration block `SCP` = `0F600H`) selected by the device assignment set with the **A**
  command. The actual ACIA/UART driver code is expected to live in that system-specific
  configuration block, not in this monitor source.
- The very top of the file is an **externally-referenced jump table**: `BEGIN`, `CI`, `RI`,
  `CO`, `PO`, `LO`, `CSTS`, `IOCHK`, `IOSET`, `MEMCK`, `STRNG` — a fixed-offset entry vector any
  host glue code (or your own test firmware) can call into.
- Four host hooks (`ADSCS`, `ADSCR`, `ADIOB`, `ADUST`, all offsets into `SCP`) are called
  during cold start and elsewhere to locate a relocatable "scratchpad" (register save area,
  breakpoint table, IOBYT) below the stack, and to fetch/clear the IOBYT. Their exact intended
  names aren't in the comments — this reading is inferred from call sites, so treat it as a
  starting hypothesis if you're annotating further, not settled fact.
