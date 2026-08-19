# Intel 8085 undocumented instructions and flags

Source: [Ken Shirriff, "Silicon reverse-engineering: The 8085's undocumented
flags"](https://www.righto.com/2013/02/looking-at-silicon-to-understanding.html)
· [Ken Shirriff, "The 8085's instruction set: the octal
table"](https://www.righto.com/2013/02/8085-instruction-set-octal-table.html)
· [electronicerror, "Undocumented flags and
instructions"](https://electronicerror.blogspot.com/2007/08/undocumented-flags-and-instructions.html)

**Provenance and how much to trust each part.** The three web sources above were
provided and authorized by Patrick on **2026-08-19** (the sourcing rule in
`docs/sources.md` says *ask Patrick, he sources it* — he did). They are **not** a
period Intel manual: Intel deliberately left this material out of the 8085
programmer's manual (`reference/Intel 8080-8085 Assembly Language Programming.md`
records the flag-byte filler bits as merely "undefined on the 8085"), so this file
is the only place the *values* are written down.

They are not equal in weight, and this file keeps them separate on purpose:

- **The two flags (§1) are authoritative.** Ken Shirriff derived them by tracing
  the **actual 8085 silicon die** — reading the flag-computation transistors, not
  another emulator (which the §0.1 rule forbids). Reverse-engineering the physical
  chip is as first-hand as a source gets. He also **corrects** an earlier published
  formula for the K flag from the silicon, which is exactly why his is the one to
  trust.
- **The ten instructions' opcodes, mnemonics and lengths (§2) are solid** — the
  octal table cross-checks them, and they already match the `8085` disassembler
  (`src/isa/isa8085.cpp`).
- **The ten instructions' operation and flag effects (§2) are second-hand and in
  two places contradictory** — they come from the electronicerror compilation
  (which reads as machine-translated) and the octal table's notes, *not* from the
  silicon. Every uncertainty is flagged ⚠ inline. **Do not implement an opcode's
  flag effect from this file alone** where a ⚠ sits on it; settle it against a
  genuine artifact first (the Tundra CA80C85B data sheet and the 1979 *Electronics*
  article are the usual primary references, and Shirriff's instruction-decode-ROM
  work is the silicon cross-check).

This split is why the follow-up work is sequenced flags-first: §1 can be built and
gated now; §2 needs firmer sourcing before the ALU-affecting ones (DSUB/ARHL/RDEL)
land. See issue #347.

---

## 1. The two undocumented flags — V and K

The 8085 computes two more condition bits than the 8080 and puts them in the two
PSW positions the 8080 nails to constants. The flag byte (as `PUSH PSW` pushes it):

| bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| 8080 | S | Z | **0** | AC | 0 | P | **1** | CY |
| 8085 | S | Z | **K** | AC | 0 | P | **V** | CY |

- **V is bit 1** (the 8080's always-1 filler).
- **K is bit 5** (the 8080's always-0 filler), also written **X5** or **UI**.
- Bit 3 stays 0 on both parts.

This is why the 8085 CPU exerciser `8085EXM` masks the flag byte with **`0D5h`**
(`[S Z X AC X P X C]`, bits 5 and 1 masked out): real 8085 silicon puts *computed*
values in those bits where the 8080 puts constants, so a documented-flags exerciser
has to ignore them (`tests/cpu/PROVENANCE.md`). **The corollary matters for us: the
stock exerciser cannot validate V or K** — a faithful V/K core has to be gated some
other way (hand-derived vectors from the rules below; see §3).

### 1.1 V — the overflow flag (bit 1)

> "The V flag is simply the exclusive-or of the carry into the top bit and the carry
> out of the top bit. This is a standard formula for computing overflow for signed
> addition and subtraction." — Shirriff

```
V = (carry into bit 7) XOR (carry out of bit 7)
```

For subtraction the ALU adds the ones-complement of the operand plus one, so the
**same add-based formula** applies to the ALU's actual inputs (A, ~operand, carry-in
= 1). Compute V from the *raw* internal carry, before the 8080 borrow-inversion — the
stored CY for a subtract is inverted, the carry that feeds V is not.

Per-instruction (all quotes Shirriff):

| Instruction(s) | V |
|---|---|
| `ADD ADC SUB SBB ADI ACI SUI SBI CMP CPI` | overflow of the 8-bit result |
| `INR` | set **only** on `0x7F → 0x80` (127 → −128) |
| `DCR` | set **only** on `0x80 → 0x7F` (−128 → 127) |
| `RLC`, `RAL` | "add the accumulator to itself, so they can be treated the same as addition: V is set if the signed result is too big for a byte" (i.e. `V = bit6 XOR bit7` of the old A) |
| `RRC`, `RAR`, `ANA`, `XRA`, `ORA` | **0** — "these operations have constant carry values inside the ALU" |
| `DAA` | "V will only be set if the top digit goes from 7 to 8" (the same overflow rule applied to the decimal-adjust addition) |
| `DAD` | 16-bit signed overflow — "computed from the result of the high-order addition" (`carry into bit15 XOR carry out of bit15`) |

### 1.2 K — the X5 / UI flag (bit 5)

Shirriff **corrects the earlier published description**:

> "The published description is mistaken. The K flag actually is the V flag
> exclusive-ored with the sign of the result."

```
K = V XOR (sign bit of the result)          (the general rule)
```

Two consequences and one special case:

- **Logical ops** (`ANA XRA ORA` + immediates): V = 0, so **K = sign**. Quote: "For
  AND, OR, and XOR, the K flag is the same as the sign, since the V flag is 0."
- **Signed subtract / compare**: K is the useful bit — **K = 1 iff the second value
  is larger than the first** (i.e. the signed comparison came out "below").
- **`INX` / `DCX` — the special case.** These 16-bit inc/dec do **not** go through the
  V-XOR-sign path. Quote: "the carry_to_k_flag control line sets the K flag according
  to the carry from the incrementer/decrementer." So:
  - `INX rp`: **K = carry out of the 16-bit increment** (set when `rp` was `0xFFFF`).
  - `DCX rp`: **K = borrow out of the 16-bit decrement** (set when `rp` was `0x0000`).
  - `INX`/`DCX` leave **V and every other flag untouched** — the article names K for
    these and nothing else ("The first function of the K flag is overflow/underflow
    for an INX/DEX instruction").
- **`DAD`**: K is computed (`V XOR sign` of the high byte) but **has no useful
  meaning** — DAD is an unsigned 16-bit add.

> Rotates write V and K even though they leave S/Z/P/AC alone (a rotate is
> documented as "carry only"). That follows directly from Shirriff's treatment of
> `RLC`/`RAL` as `A + A` and `RRC`/`RAR` as constant-carry — the flag-compute logic
> still runs. Pin this with a unit test rather than trusting the summary.

---

## 2. The ten undocumented instructions

Opcodes, mnemonics and lengths are solid (octal table + the shipped disassembler).
**Operation and flag columns are second-hand — read the ⚠ notes.**

| Opc | Oct | Mnemonic | Len | Clocks | Operation | Flags (⚠ = unsettled) |
|---|---|---|---|---|---|---|
| `08` | 010 | `DSUB` | 1 | 10 | `HL = HL − BC` | S Z AC P CY **and V, K** — "all flags" (a 16-bit subtract) |
| `10` | 020 | `ARHL` | 1 | 7 | Arithmetic shift `HL` right: bit 15 (sign) preserved, `L` bit 0 → CY (`HL = HL/2` signed) | **CY** only ⚠ ("flags unchanged" except CY — wording conflicts, confirm) |
| `18` | 030 | `RDEL` | 1 | 10 | Rotate `DE` left through carry: `D` bit 7 → CY, old CY → `E` bit 0 (`DE = DE*2`) | **CY** ⚠ ("no other flags"; some refs also set V — confirm) |
| `28` | 050 | `LDHI d8` | 2 | 10 | `DE = HL + imm8` | ⚠ **conflict**: electronicerror says "setting flags"; the common tables say **no flags**. Do not implement flag effects until settled. |
| `38` | 070 | `LDSI d8` | 2 | 10 | `DE = SP + imm8` | ⚠ same conflict as `LDHI` |
| `CB` | 313 | `RSTV` | 1 | 6 / 12 | **If V set**: `RST` to **`0x0040`** (push PC, `PC = 0x0040`). If V clear: no-op. | none |
| `D9` | 331 | `SHLX` | 1 | 10 | `[DE] = L`, `[DE+1] = H` (store HL at address DE) | none |
| `ED` | 355 | `LHLX` | 1 | 10 | `L = [DE]`, `H = [DE+1]` (load HL from address DE) | none |
| `DD` | 335 | `JNK a16` (JNX5) | 3 | 7 / 10 | **If K = 0**: `PC = a16` | none |
| `FD` | 375 | `JK a16` (JX5) | 3 | 7 / 10 | **If K = 1**: `PC = a16` | none |

Notes:

- **`RSTV` vectors to `0x0040`**, *not* one of the eight standard `RST` vectors
  (`0x00`–`0x38`). It is "RST V" / "RST 8". The split clocks (6 not-taken / 12 taken)
  and the jumps' split clocks (7/10) mirror the documented 8085 habit of skipping the
  bytes it does not use when a condition fails.
- **`RSTV`, `JK`, `JNK` depend on the V and K flags from §1** — so §1 must land before
  these can even be tested. That is the clean reason to sequence flags first.
- **`SHLX`/`LHLX`** are the `DE`-addressed twins of `SHLD`/`LHLD` (which are
  absolute-addressed) and of `STAX`/`LDAX` (which move only the accumulator).
- **`LDHI`/`LDSI`** are the one genuinely useful pair — a base-plus-displacement
  address calculation the documented 8085 lacks — which is why their flag behavior
  matters and why the conflict above must be resolved from a primary source, not
  guessed.

---

## 3. What this means for `altairsim`

The `8085` core (`src/cpu/cpu8085.{h,cpp}`) models the **documented** 8085 —
RIM/SIM, the TRAP/RST n.5 interrupts, and the faithful `ANA` half-carry, gated by
`8085EXM` (real-silicon CRCs) — **plus the V and K bits of §1**, which now compute
per the rules above and ride PSW bits 1 and 5 (`test_8085_cpu.cpp` pins them by
hand-derived vector). Five of the ten undocumented opcodes now execute too —
`SHLX`/`LHLX`/`RSTV`/`JK`/`JNK` (§3, sequencing item 2); the other five
(`DSUB`/`ARHL`/`RDEL`/`LDHI`/`LDSI`) still run as NOP pending firmer sourcing. Issue
#347 tracks the remaining gap; this reference is its source.

**The gate problem, stated plainly.** `8085EXM`'s `0D5h` mask means the stock
exerciser structurally **cannot** see V or K (§1) — a faithful-V/K core passes it
whether V/K are right or wrong. There is no V/K CRC exerciser in `tests/cpu/`. So
the V/K gate is **unit tests whose expected values are hand-derived from §1's rules**
(the way `test_8085_cpu.cpp` already pins the `ANA` rule and the interrupt vectors
from the Intel manual). That is legitimate under DESIGN §3.2 — the oracle is
Shirriff's silicon analysis, applied by hand, **not** the emulator grading itself —
*provided* the vectors are simple, hand-checkable cases (7F+1 overflows; 50+50
overflows; a signed compare where the second operand is larger sets K; `INX` of
`0xFFFF` sets K; and so on).

**Sequencing (issue #347):**

1. **V/K flags** — DONE. Sourced here (§1), gated by hand-derived vectors in
   `test_8085_cpu.cpp`. This also makes `RSTV`/`JK`/`JNK` implementable, since they
   only branch on the new bits.
2. **The five SAFE undocumented opcodes** — DONE. `SHLX`/`LHLX` (pure data movement)
   and `RSTV`/`JK`/`JNK` (they only read the V/K bits) execute in the core, gated by
   `test_8085_cpu.cpp`. None affects a flag, so nothing beyond §1 + the opcode table
   was needed. `RSTV` pushes and jumps like the `RST` *instruction*, so it leaves
   INTE alone (only the hardware TRAP/RST n.5 vectors clear it). They still
   disassemble DDT-style (`?\?= XX *MNEM`, one byte) — Intel-undocumented regardless
   of the core running them, exactly as the 8080's own executed-but-undocumented
   0xCB-as-JMP does.
3. **The five ALU-affecting undocumented opcodes** — `DSUB`, `ARHL`, `RDEL`, and the
   `LDHI`/`LDSI` flag question (§2) need a primary source (Tundra CA80C85B / 1979
   *Electronics*) before their operation and flag effects can land.

Nothing here changes the documented gate: because `8085EXM` masks V/K out and the
undocumented opcodes are distinct byte values the exerciser never emits, adding all
of this leaves every stock 8080/8085 suite green.
