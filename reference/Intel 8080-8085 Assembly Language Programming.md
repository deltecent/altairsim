# Intel 8080/8085 Assembly Language Programming

Source: [8080/8085 Assembly Language Programming Manual.pdf](#) (Intel document 9800301D,
May 1981; all mnemonics © Intel 1974–1977). 222 pp. The definitive vendor description of the
programmer-visible 8080/8085 model and the ISIS-II macro-assembler language.

This is a distilled emulation reference for the **CPU's programmer model** — registers, the
condition flags and their exact semantics, the interrupt system, and the handful of places
where the **8085 behaves differently from the 8080**. Those differences are the reason to keep
this manual: an emulator that treats an 8085 as "a faster 8080" gets the flag-byte filler bits,
the AND auxiliary-carry rule, the conditional-branch fetch, and the RIM/SIM/TRAP interrupt
machinery wrong. The full alphabetical instruction descriptions (Chapter 3), the assembler
directive/macro language (Chapters 4–5), and the numeric/alphabetical opcode-and-timing tables
(Appendix A) are summarized to what an emulator or a test author needs; the per-instruction
prose is not transcribed — it is standard and already embedded in the simulator's ISA layer.
The assembler-language half of the manual (source-line format, expressions, macros, relocation)
is covered only briefly, at the end.

> The two processors share one instruction set. Except for **SIM** and **RIM** (8085-only),
> every 8080 opcode runs unchanged on the 8085; most 8080 programs run on an 8085 without
> modification. The differences that bite are behavioral, not mnemonic — see §5.

---

## 1. Register and memory model

Eight-bit accumulator **A** plus six general-purpose 8-bit registers **B, C, D, E, H, L**, a
16-bit **stack pointer SP**, and a 16-bit **program counter PC**. Memory and I/O are separate
address spaces: a 16-bit memory address bus (64K) and a separate 8-bit I/O port space (256
ports), the latter reachable only through **IN**/**OUT** via the accumulator.

Registers pair for 16-bit operations. The symbolic pair name is the high register:

| Pair name | Registers | Notes |
|:---:|---|---|
| `B` | B (hi) & C (lo) | |
| `D` | D (hi) & E (lo) | |
| `H` | H (hi) & L (lo) | also the memory pointer for `M` |
| `M` | (H&L as a memory reference) | not a register — the byte `[HL]` |
| `PSW` | A & the flag byte | pushed/popped as a unit (§3) |
| `SP` | the stack pointer | |

`H` vs `M` selects intent on the same pair: `INX H` increments the HL pair; `ADD M` adds the
byte at `[HL]`. Register code `110` means M/SP/PSW depending on the instruction; `111` is A.
The stack grows **downward** (PUSH decrements SP first, then stores); a program must initialize
SP (typically `LXI SP,addr`) before any CALL, PUSH, or interrupt.

---

## 2. Condition flags

Five flags. When a flag is "set" it is 1; "reset" it is 0.

| Flag | Bit in PSW | Meaning |
|:---:|:---:|---|
| **S** (Sign) | 7 | copy of bit 7 of the result (1 = negative in two's-complement) |
| **Z** (Zero) | 6 | 1 when the result is zero |
| **AC** (Auxiliary Carry) | 4 | carry out of bit 3; feeds **DAA** only — not directly testable |
| **P** (Parity) | 2 | 1 = even parity (even number of 1 bits in the result) |
| **CY** (Carry) | 0 | carry/borrow out of bit 7; also the 9th bit for the rotate instructions |

- **AC is not testable by any conditional instruction** — its sole purpose is to let **DAA**
  correct a BCD add. It is affected by all add/subtract/increment/decrement/compare and all
  logical AND/OR/XOR instructions.
- **CY** is a borrow for subtraction and the ninth bit for `RAL`/`RAR`/`RLC`/`RRC`; the logical
  AND/OR/XOR instructions also drive it (they reset it, per the individual descriptions).
- A pitfall the manual calls out: **IN does not affect any flag.** A conditional jump placed
  immediately after `IN` tests a *previous* operation's flags, not the port. Insert a
  flag-setting instruction (e.g. `ADI 0`) between `IN` and the branch.

---

## 3. The PSW (flag) byte — and an 8080/8085 difference ⚠

`PUSH PSW` packs A and the five flags into two stack bytes; `POP PSW` restores them. The flag
byte is laid out:

```
 bit  7   6   5   4   3   2   1   0
      S   Z   0   AC  0   P   1   CY
```

- **On the 8080**, the three filler bits are fixed: **bit 5 = 0, bit 3 = 0, bit 1 = 1**.
- **On the 8085**, those same filler bits are **undefined**. ⚠ An emulator that hard-codes the
  8080 filler pattern for both CPUs, or that lets an 8085 program *rely* on reading them back,
  is modelling behavior the 8085 does not guarantee. (This is the classic reason a `POP PSW` /
  `PUSH PSW` round-trip test can pass on one part and disagree on the other.)

---

## 4. Instruction groups and timing

The Chapter-1 taxonomy (each instruction's full semantics are in Chapter 3, alphabetical):

- **Data transfer** — `MOV`, `MVI`, `LXI`, `LDA`/`STA`, `LHLD`/`SHLD`, `LDAX`/`STAX`, `XCHG`.
- **Arithmetic** — `ADD`/`ADI`/`ADC`/`ACI`, `SUB`/`SUI`/`SBB`/`SBI`, `INR`/`DCR`, `INX`/`DCX`,
  `DAD` (16-bit add to HL), `DAA`.
- **Logical** — `ANA`/`ANI`, `ORA`/`ORI`, `XRA`/`XRI`, `CMP`/`CPI`, `RLC`/`RRC`/`RAL`/`RAR`,
  `CMA`, `CMC`, `STC`.
- **Branch** — `JMP`, `CALL`, `RET` and their conditional forms (`Jcond`/`Ccond`/`Rcond` on
  Z/NZ/C/NC/PE/PO/P/M), `PCHL`, `RST n`.
- **Stack / I/O / machine control** — `PUSH`/`POP`, `XTHL`, `SPHL`, `IN`/`OUT`, `EI`/`DI`,
  `HLT`, `NOP`, and the 8085-only `RIM`/`SIM`.

**Timing model (Appendix A).** Every instruction is tabled with its bit pattern and its number
of *time periods (T-states)* for both the 8080 and the 8085 — they differ for many instructions.
A conditional call/return shows two counts (e.g. **5/11** states): the smaller applies when the
condition fails, the larger when it is taken. Execution time = T-states × clock period; a period
ranges 480 ns–2 µs on the 8080 and **320 ns–2 µs on the 8085** (≈50% faster). Register codes in
the opcode bytes: `DDD`/`SSS` = 000 B, 001 C, 010 D, 011 E, 100 H, 101 L, 110 M/SP/PSW, 111 A.

---

## 5. 8085 processor differences (the part that matters)

Beyond speed, the emulation-relevant behavioral differences from the 8080:

1. **AND sets AC differently.** The 8085 logical-AND instructions (`ANA`/`ANI`) **always set AC
   ON**. The 8080 sets AC to the logical **OR of bit 3** of the two operands. ⚠ This changes what
   a following `DAA` does, so it is observable, not cosmetic.
2. **Conditional-branch byte fetch.** The 8080 always fetches all three bytes of a conditional
   `Jcc`/`Ccc` whether or not the condition holds. The 8085 evaluates the condition while
   fetching byte 2 and, if the condition is **not** met, **skips byte 3** and goes straight to
   the next instruction — the source of the different T-state counts.
3. **PSW filler bits** are undefined on the 8085 (see §3 ⚠).
4. **Two new instructions, `SIM`/`RIM`**, and **four new hardware interrupt inputs** — §6.
5. The 8085 integrates the 8224 clock generator and 8228 system controller on-chip and needs
   only a single +5 V supply, plus serial **SID**/**SOD** lines (accessed via RIM/SIM).

---

## 6. Interrupts

**8080 model.** A single maskable `INTR` line; `EI`/`DI` toggle the master enable (the `INTE`
pin). When enabled and interrupted, the device jams an instruction onto the bus — conventionally
an `RST n`, which pushes PC and vectors to **n × 8** (RST 0→0000H, RST 1→0008H, … RST 7→0038H).
`RST n`'s operand is 000B–111B.

**8085 model.** Adds four dedicated inputs that generate an *internal* RST — the device only
pulses the pin, no bus instruction needed. Fixed vectors that interleave with the 8080 RST
addresses (leaving just 4 bytes per routine — enough for a jump/call + return):

| Input | Vector | Priority | Maskable? |
|:---:|:---:|:---:|:---:|
| **TRAP** | 24H | highest | **no** — non-maskable, and fires even with interrupts disabled |
| **RST7.5** | 3CH | ↑ | yes; **rising-edge latched** (see below) |
| **RST6.5** | 34H | | yes; level-sensitive |
| **RST5.5** | 2CH | | yes; level-sensitive |
| **INTR** | (from jammed instr.) | lowest | yes |

Priority governs only the order of *recognition* when several are pending; service routines have
no inherent priority (an RST5.5 can interrupt an RST7.5 handler unless masked). `DI` still
overrides everything except **TRAP** — even `TRAP` can interrupt a `DI`/`EI` instruction. Use
`TRAP` for power-fail / catastrophic events, and always provide a handler if the input is used.

**RST7.5 is edge-triggered:** it is sensed by an on-chip flip-flop set on the input's rising
edge (for devices that cannot hold the request until serviced). The flip-flop stays set until
one of: a processor RESET, the interrupt being recognized, or a `SIM` with accumulator bit 4 set
(§7). It can be set even while masked, but is only serviced when unmasked and interrupts enabled.

---

## 7. RIM / SIM bit layouts (8085 only)

Both take **no operand**; both work through the accumulator.

**`RIM` — Read Interrupt Mask** loads A with the current interrupt state:

```
 bit   7    6    5    4    3    2    1    0
      SID  I7   I6   I5   IE  M7.5 M6.5 M5.5
```

- Bits 0–2 (**M5.5/M6.5/M7.5**): the RST mask — **1 = masked**.
- Bit 3 (**IE**): master Interrupt Enable (same sense/level as the 8080 `INTE` pin; 1 = whole
  system enabled).
- Bits 4–6 (**I5/I6/I7**): **pending** RST5.5/6.5/7.5 — 1 = pending.
- Bit 7 (**SID**): one bit of serial input data from the SID pin, if any.

**`SIM` — Set Interrupt Mask** interprets A as follows. Bits 3 and 6 are **enable switches**:

```
 bit   7    6    5    4    3    2    1    0
      SOD  SOE   -   R7.5 MSE M7.5 M6.5 M5.5
```

- Bit 3 (**MSE**, "mask set enable"): if **1**, bits 0–2 are applied as the new mask
  (1 = mask that RST); if **0**, bits 0–2 are ignored (use this to write serial data without
  disturbing the mask).
- Bit 4 (**R7.5**): if 1, **resets the RST7.5 edge flip-flop** (cancels a pending edge).
- Bit 6 (**SOE**, "serial output enable"): if 1, bit 7 is latched to the **SOD** pin; if 0,
  bit 7 is ignored.
- Bit 7 (**SOD**): the serial output data bit.
- `DI` overrides `SIM`: RST5.5/6.5/7.5 stay disabled while a `DI` is in effect regardless of the
  mask. Read back current state with `RIM`.

Worked examples from the manual: A = `00011100` → resets the RST7.5 flip-flop *and* masks RST7.5
(overriding any pending 7.5). A = `11001111` → masks all three levels and latches a 1 to SOD.
A = `10000111` → **no effect**, because enable bits 3 and 6 are both 0.

---

## 8. Assembler language (brief)

Included only so a test/asm author can read the manual's examples; the full language is Chapters
2, 4, 5 and the ISIS-II operator's manual (Intel 9800292).

- **Source line:** `{Label: | Name}  Opcode  Operand  ;Comment` — one statement per line,
  terminated by CR/LF, no continuation lines; fields separated by ≥1 blank/delimiter.
- **Numeric radix:** a bare number is decimal; suffixes select radix (`H` hex, `O`/`Q` octal,
  `B` binary, `D` decimal). A hex constant must start with a digit (`0FFH`, not `FFH`).
- **Data / symbol directives:** `DB` (define bytes / ASCII string), `DW` (define words), `DS`
  (reserve storage), `EQU` (permanent value), `SET` (redefinable value), `ORG` (set location
  counter), `END` (terminate assembly).
- **Conditional assembly:** `IF` / `ELSE` / `ENDIF`.
- **Relocation & linkage:** `ASEG`/`CSEG`/`DSEG` (absolute/code/data segments), relocatable
  `ORG`, `PUBLIC`/`EXTRN`/`NAME`/`STKLN` for linking separately assembled modules.
- **Macros:** `MACRO`/`ENDM`, `LOCAL`, `REPT`/`IRP`/`IRPC`, `EXITM`, with nesting and the
  special macro operators; distinct from subroutines (expanded inline at assembly time).
- Appendices: **A** instruction summary (opcodes + 8080/8085 T-states), **B** directive summary,
  **C** ASCII table, **D** additional reference material.

---

## 9. Emulation notes / gotchas

- **Do not model an 8085 as a fast 8080.** The observable divergences are: AND always sets AC
  (§5.1), conditional branches skip the third byte when not taken (§5.2, and the different
  T-state counts), and PSW filler bits are undefined (§3). If a machine is an 8080, keep the
  8080 rules; if it is an 8085, keep the 8085 rules — a per-CPU flag, not one shared path. See
  [[altairsim-z80-isa-next]] for how the ISA layer is split.
- **AC is write-only to software** — never expose a "test AC" path; only `DAA` consumes it.
- **RST7.5 is edge-latched**, not level — a correct model needs the internal flip-flop with its
  three reset sources (RESET / recognition / `SIM` bit 4), or edge interrupts will either
  re-fire or vanish. RST5.5/6.5 and INTR are level/line-driven.
- **TRAP ignores `DI` and the mask** — the only interrupt that does. Everything else is gated by
  the master enable and, for the 5.5/6.5/7.5 levels, the SIM mask.
- **Timing is per-CPU:** the same opcode has different T-states on the two parts, and the clock
  period floors differ (320 ns vs 480 ns). "It runs" is not "the cycle count is right" — verify
  against Appendix A, not against boot success. See [[altairsim-plausible-but-wrong-timing]].
- **`IN` sets no flags** — a guest that branches right after `IN` is reading stale flags by
  design; don't "fix" it in the model.

## 10. Key facts at a glance

| | |
|---|---|
| Registers | A + B,C,D,E,H,L (8-bit); SP, PC (16-bit); pairs BC/DE/HL, PSW = A+flags |
| Flags (PSW bits) | S=7, Z=6, AC=4, P=2, CY=0; 8080 filler 5=0/3=0/1=1, **8085 undefined** |
| Address spaces | 64K memory (16-bit) + 256 I/O ports (8-bit, via IN/OUT) |
| RST n vector | n × 8 (0000H…0038H); operand 000B–111B |
| 8085 vectors | TRAP 24H, RST5.5 2CH, RST6.5 34H, RST7.5 3CH |
| 8085 priority | TRAP > RST7.5 > RST6.5 > RST5.5 > INTR |
| TRAP | non-maskable; fires even under `DI` |
| RST7.5 | rising-edge latched; cleared by RESET / recognition / SIM bit 4 |
| 8085-only opcodes | `RIM`, `SIM` (SID/SOD serial + mask) |
| Clock period | 8080: 480 ns–2 µs; 8085: 320 ns–2 µs |
| AND & AC | 8085 always sets AC; 8080 = OR of bit 3 of the operands |
