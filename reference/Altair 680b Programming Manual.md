# Altair 680b Programming Manual (6800 model, assembler & ACIA I/O)

Source: [680 Programming Manual.pdf](#) (MITS *Programming Manual — altair 680b*, © MITS Inc.
1976; "Major portions … reprinted by permission of Motorola Semiconductor Products, Inc.,
copyright 1975.")

The programmer's guide to the **Altair 680b** (Motorola **6800**). Most of the volume is the
standard Motorola M6800 programming reprint — the CPU model, the seven addressing modes, the
72-mnemonic / 197-opcode instruction set, and arithmetic tutorials. What is genuinely
**680b-specific** — and the reason this is worth a reference — lives in the assembler's
680b conventions and in **Appendix C, Input/Output Information**, which is the full **6850
ACIA** register model for the machine's on-board serial port, the paper-tape-reader control,
and the interrupt-vector block. See the companion boards [[altairsim-88uio-board]] and the
[Altair 680b KCACR](Altair%20680b%20KCACR.md) / [Universal I/O Board](Altair%20680b%20Universal%20IO%20Board.md)
/ [Operator's Manual](Altair%20680b%20Operators%20Manual.md) references.

This is a distilled emulation reference. The **generic Motorola 6800 material is deliberately
omitted** — the per-instruction opcode/cycle definitions (§IV and Appendix A), the arithmetic
tutorials (§V: number systems, overflow, multiply/divide routines), and the sample programs
(§VI) are the same M6800 reference documented in any Motorola databook and in this project's
own CPU work; consult those for opcode-level detail. What is kept: the programming model, the
addressing-mode summary (byte counts, branch range, direct/extended auto-selection), the
interrupt timing, the 680b assembler source language and directives, and — in full — the
680b ACIA I/O model.

## 1. Programming model (M6800)

The MPU is a Motorola **M6800**: 8-bit data, **16-bit addresses**, memory-mapped I/O (each I/O
device has one or more 16-bit addresses; there is **no separate I/O space** — this is not the
8080/S-100 `IN`/`OUT` world). The programmable registers (Figure 2-2):

| Register | Width | Notes |
|---|---|---|
| **ACCA** | 8 | Accumulator A |
| **ACCB** | 8 | Accumulator B |
| **IX** | 16 | Index register |
| **PC** | 16 | Program counter |
| **SP** | 16 | Stack pointer |
| **CCR** | 6 | Condition codes: **H I N Z V C** |

The instruction fetch/execute loop is the ordinary one: PC → address bus, read opcode, PC++,
execute, repeat until halted. The 680b recognizes **197** of the 256 possible opcodes,
forming **72** distinct mnemonics (the larger opcode count comes from instructions with more
than one addressing mode).

## 2. Addressing modes

Seven modes. The assembler picks the mode from the operator + operand syntax.

| Mode | Bytes | How selected / notes |
|---|---|---|
| **Inherent** | 1 | Operand(s) implied by the mnemonic (e.g. `ABA`); 25 such instructions |
| **Accumulator** (single operand) | 1 | Operand field is just `A` or `B`; 13 operators (e.g. `ASRA`, `INCB`) |
| **Immediate** | 2 | Operand prefixed `#`; value goes in byte 2 |
| **Direct** | 2 | Address 0–255; address in byte 2 |
| **Indexed** | 2 | `n,X` / `,X` / `X`; byte 2 = unsigned 0–255 offset added to IX at run time |
| **Relative** | 2 | Branches only; byte 2 = signed offset |
| **Extended** | 3 | 16-bit address in bytes 2–3 (byte 2 = high, byte 3 = low) |

⚠ **Immediate exceptions:** `CPX`, `LDS`, `LDX` take a **16-bit** immediate operand, so they
assemble to **3 bytes** (not 2), with the operand's MS byte first. For these three, `#'C`
(ASCII literal) places the character in the third byte.

⚠ **Direct-vs-extended is chosen by value, at assembly time.** For an operator that supports
both, the assembler emits **direct** if the numeric address is 0–255 and **extended** if it
exceeds 255. Some operators support extended but not direct; there the assembler always emits
3 bytes. Model this if you ever build a 680b assembler — it is not a syntactic choice.

**Relative branch range:** the destination D must satisfy `(PC+2) − 128 ≤ D ≤ (PC+2) + 127`,
where PC is the address of the branch's first byte. The offset R is stored as an 8-bit two's
complement number, and `D = (PC+2) + R`. To branch farther, use `JMP`/`JSR` (which use
extended, not relative).

## 3. Instruction set & timing

The 72 mnemonics group functionally (Figure 3-3):

- **8-bit ops:** two-operand arith `ABA ADC ADD SBA SBC SUB`; single-operand `CLR DAA DEC INC
  NEG`; compare/test `CBA CMP TST`; shifts/rotates `ASL ASR LSR ROL ROR`; logic `AND BIT COM
  EOR ORA`; load/store `LDA STA PSH PUL`; transfers `TAB TBA`.
- **Jump/branch:** conditional `BCC BCS BEQ BGE BGT BHI BLE BLS BLT BMI BNE BPL BVC BVS`;
  unconditional `BRA NOP JMP`; subroutines `BSR JSR RTS`; interrupts `RTI SWI WAI`.
- **IX / SP:** `DEX INX LDX STX CPX` · `DES INS LDS STS` · `TSX TXS`.
- **CCR:** bit control `CLC CLI CLV SEC SEI SEV`; byte transfers `TAP TPA`.

Per-opcode cycle counts are in Figure 3-2 / Appendix A (generic M6800 — omitted here). One
timing fact worth recording because an emulator's interrupt model needs it:

⚠ **Interrupt latency = 12 machine cycles from the end of the instruction in progress —
except immediately following a `WAI`, when it is 4 cycles.** (`WAI` stacks the machine state
in advance precisely to shorten this.)

## 4. 680b assembler source language

Source lines have up to four fields: **label · operator (mnemonic) · operand · comment**.
A `*` in column 1 makes the whole line a comment. Labels: 1–6 alphanumerics, first alphabetic,
unique, and **never** a bare `A`, `B`, or `X` (reserved for ACCA / ACCB / IX). The special
operand symbol `*` is the program counter (value of the current instruction's first byte).

**Number formats** (prefix or suffix; default is decimal):

| Form | Base | | Form | Base |
|---|---|---|---|---|
| `Number` | decimal | | `Number O` / `Number Q` | octal |
| `$Number` | hex | | `%Number` / `Number B` | binary |
| `@Number` / `Number O` / `Number Q` | octal | | `Number H` | hex |
| `#'C` | ASCII literal → 7-bit code, bit 7 = 0 | | | |

⚠ Quirk: when the prefix is `$` **and** the last character is `B`, the assembler reads the whole
token as **hex** (not "binary suffix"). Expressions evaluate strictly **left-to-right, no
operator precedence, no parentheses**; fractional intermediate results truncate to integer.
Only **one level of forward reference** is legal (two-pass assembler).

**Assembler directives** (Appendix B):

| Directive | Meaning | 680b note |
|---|---|---|
| `ORG` | Set location counter | default origin is `0000` if no `ORG` |
| `EQU` | Equate symbol to value/expr | must have a label |
| `FCB` | Form constant byte(s) | comma list; void operands store `00` |
| `FCC` | Form constant chars → 7-bit ASCII | `count,text` or `/text/` delimited form |
| `FDB` | Form double (16-bit) constant | two bytes each |
| `RMB` | Reserve memory bytes | advances the counter, leaves memory unchanged |
| `END` | End of source | optional if `MON` ends it |
| **`MON`** | **Return to Monitor** | ⚠ **680b-specific** — last statement; returns assembler control to the **680b PROM Monitor** |
| `NAM` | Name / page heading | operand+comment treated as one text run |
| `OPT` | Assembler output options | details depend on the resident-assembler version |
| `PAGE` | Advance listing to top of page | not printed |
| `SPC` | Blank n listing lines | not printed |

## 5. ACIA — the 680b serial port (Appendix C)

The 680b's serial console/port is a **Motorola 6850 ACIA** (Asynchronous Communications
Interface Adapter) — the same chip family used across this simulator (88-2SIO, 88-UIO,
680b UI/O). Normally the **PROM Monitor** initializes it, but Appendix C documents the
registers for hand-rolled I/O. **The programming manual does not give the port's memory
address** — do not invent one; the address comes from the machine wiring (the KCACR cassette
ACIA sits at `F010`/`F011`, the UI/O serial ACIA at its S9 window). Two register-selects × R/W
give four registers:

- **TDR — Transmit Data Register** (write). Writing it clears **TDRE** (status bit 1) low; the
  byte transmits when the shifter is free.
- **RDR — Receive Data Register** (read). A completed receive sets **RDRF** (status bit 0) high;
  a non-destructive MPU read of RDR clears RDRF (the byte stays valid in RDR).
- **Control Register** (write-only).
- **Status Register** (read-only).

**Control Register** bits:

| Bits | Field | Values |
|---|---|---|
| CR1,CR0 | Counter divide | `00`=÷1 · `01`=÷16 · `10`=÷64 · **`11`=Master Reset** |
| CR4,CR3,CR2 | Word select | see table below (word/parity/stop; changes take effect **immediately**, unbuffered) |
| CR6,CR5 | Transmitter control | `00`=RTS low, Tx-IRQ off · `01`=RTS low, Tx-IRQ on · `10`=RTS **high**, Tx-IRQ off · `11`=RTS low, **send Break**, Tx-IRQ off |
| CR7 | Receive interrupt enable | high = enable Rx IRQ (from RDRF, or a low→high DCD transition) |

⚠ **After power-on / power-fail restart the ACIA is not automatically reset** — software must
first write `11` (Master Reset) to CR1,CR0, *then* select a real divide ratio. Master reset
clears the status register and initializes the receiver/transmitter but does not affect the
other control bits.

Word-select (CR4 CR3 CR2):

| CR4 CR3 CR2 | Format |
|---|---|
| 0 0 0 | 7 bits + even parity + 2 stop |
| 0 0 1 | 7 bits + odd parity + 2 stop |
| 0 1 0 | 7 bits + even parity + 1 stop |
| 0 1 1 | 7 bits + odd parity + 1 stop |
| 1 0 0 | 8 bits + 2 stop |
| 1 0 1 | 8 bits + 1 stop |
| 1 1 0 | 8 bits + even parity + 1 stop |
| 1 1 1 | 8 bits + odd parity + 1 stop |

**Status Register** bits:

| Bit | Name | Meaning |
|---|---|---|
| 0 | **RDRF** | Receive Data Register Full. ⚠ **DCD high forces RDRF to read empty.** |
| 1 | **TDRE** | Transmit Data Register Empty. ⚠ **CTS high inhibits TDRE** (holds it low). |
| 2 | **DCD** | Data Carrier Detect. Goes high when carrier is lost; **latches** and (if Rx-int enabled) raises IRQ; stays high until the MPU reads Status then Data, or a master reset — and if DCD input is still high, follows it. |
| 3 | **CTS** | Clear-to-Send input state. Low = clear to send; **high inhibits TDRE**. Master reset does not affect this bit. |
| 4 | **FE** | Framing error (missing/!bad stop bit; set/cleared per received char). |
| 5 | **OVRN** | Receiver overrun (a char was lost — received but not read before the next). |
| 6 | **PE** | Parity error (vs selected odd/even; inhibited if no parity selected). |
| 7 | **IRQ** | Reflects the (active-low) IRQ output; high here = an interrupt/service request is pending. |

## 6. Paper-tape reader control (via RTS)

When the paper-tape-reader-control circuit is fitted ([Operator's Manual](Altair%20680b%20Operators%20Manual.md)
§PTRC), the ACIA's **RTS** output turns the reader on/off: **RTS high → reader ON**, RTS low →
reader OFF. RTS is high only for the `CR6=1, CR5=0` transmitter-control combination, so the
reader is on **only** in that setting (which also has the transmit interrupt disabled); it is
off for the other three CR6/CR5 combinations.

## 7. Interrupt vectors

The processor interrupt vectors sit at **`FFF8`–`FFFF`** inside the **680b PROM Monitor**
(6800 convention: `FFF8/9` = IRQ, `FFFA/B` = SWI, `FFFC/D` = NMI, `FFFE/F` = RESET). Their
contents depend on the Monitor version — the manual defers to §VI of the *System Monitor*
manual. This matches the KCACR/UIO references' note that the 6800 `IRQ` vector `FFF8/FFF9`
points at `0100` in the shipped monitor.

## 8. Emulation notes / gotchas

- **The 6850 ACIA model here is authoritative and reusable.** It is bit-for-bit the same
  control/status model already implemented for the 88-2SIO and 680b UI/O
  [[serial-io-architecture]] [[altairsim-88uio-board]]; a 680b console emulation reuses it.
- **CTS-inhibits-TDRE and DCD-forces-RDRF-empty are real, load-bearing behaviors** — guest
  serial code polls TDRE/RDRF and will hang or spin if these interlocks aren't modeled.
- **Master-reset-after-power-on is mandatory in guest code**, so a freshly powered ACIA model
  should start in a state where the first thing software does (`11` to CR1,CR0) is required and
  meaningful — don't silently pre-initialize the divide ratio.
- **Don't invent the ACIA's address.** This manual describes registers, not the memory map;
  the address is a wiring fact (see KCACR `F010`/`F011`, UI/O S9 window) [[altairsim-no-invented-hardware]].
- **Direct-vs-extended and the 3-byte `CPX`/`LDS`/`LDX` immediates** are the two assembler
  facts most likely to bite a from-scratch 680b assembler or disassembler.
- **The generic M6800 ISA/timing is out of scope here on purpose** — use a Motorola databook
  or this project's existing CPU reference for per-opcode cycles; only the 12-cycle (4 after
  `WAI`) interrupt latency is recorded above.

## 9. Key facts at a glance

| | |
|---|---|
| MPU | Motorola **M6800**; 8-bit data, 16-bit address, **memory-mapped I/O** (no `IN`/`OUT`) |
| Registers | ACCA, ACCB (8) · IX, PC, SP (16) · CCR = **H I N Z V C** (6) |
| Instruction set | **72** mnemonics / **197** opcodes; 7 addressing modes |
| Addressing modes | Inherent(1) · Accumulator(1) · Immediate(2, but 3 for CPX/LDS/LDX) · Direct(2) · Indexed(2) · Relative(2) · Extended(3) |
| Mode auto-select | address ≤255 → **direct**, >255 → **extended** (assembler decides by value) |
| Branch range | `(PC+2) − 128 … (PC+2) + 127`; 8-bit two's-complement offset |
| Interrupt latency | **12 cycles** from end of instruction; **4 cycles** after `WAI` |
| Assembler numbers | default decimal; `$`/`H` hex, `@`/`O`/`Q` octal, `%`/`B` binary, `#'C` ASCII; ⚠ `$…B` = hex |
| Assembler quirks | one-level forward reference; no operator precedence; **`MON`** = return to 680b PROM Monitor |
| Serial port | **6850 ACIA**; TDR/RDR + write-only Control + read-only Status; **address not given here** |
| Control reg | CR1,0 divide (`11`=Master Reset) · CR4–2 word/parity/stop (unbuffered) · CR6,5 Tx+RTS+break · CR7 Rx-int |
| Status reg | RDRF · TDRE · DCD · CTS · FE · OVRN · PE · IRQ; **CTS-high inhibits TDRE**, **DCD-high forces RDRF empty** |
| Power-on ACIA | **not auto-reset** — software must Master-Reset (`11`) then set divide |
| Paper-tape reader | RTS **high** (CR6=1,CR5=0) = reader ON; off otherwise |
| Interrupt vectors | **`FFF8`–`FFFF`** in the 680b PROM Monitor; IRQ `FFF8/9` → `0100` (shipped monitor) |
