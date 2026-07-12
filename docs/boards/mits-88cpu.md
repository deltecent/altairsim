# `8080` — the MITS 88-CPU

The processor card. An 8080A at 2 MHz, and in a real Altair it *was* a card —
people pulled it out and put a Z80 in its place, which is why it lives in
`boards/` with everything else and gets `properties()`, both resets, and a line in
`BOARDS` with no special cases anywhere.

**Sources:** Intel *8080 Assembly Language Programming Manual* (1975) and the
*8080 Microcomputer Systems User's Manual*, for the opcodes, the T-states and the
flag rules. MITS *Altair 8800 Operator's Manual* for the card. Nothing was taken
from another emulator (DESIGN.md §0.1).

## It decodes nothing

    altairsim> BOARDS
      cpu0     8080     mem:-                        io:-

That is not a gap in the table. **Boards respond to bus cycles; a bus master
originates them** — and a plain 88-CPU has nothing on it that a bus cycle can
reach. `WHO 0100` will tell you honestly that the CPU is not there.

The payoff is DMA, and it is already paid for: a disk controller that steals the
bus is a `Board` that *becomes* a `BusMaster` and drives the same cycles through
the same interface. Nothing in the bus has to learn about it.

*(A CPU card with an onboard serial port or a boot PROM — and those were ordinary
products — would decode those, and only those. It would still be this same core.
The chip, the instruction set, and the card are three separate things: DESIGN.md
§3.0.2.)*

## Properties

| Property | Default | Runtime? | What it is |
|---|---|---|---|
| `clock_hz` | `2000000` | yes | The crystal, **which is on this card** — that is why the clock is the board's property and not the machine's. `0` runs flat out. A backplane with no CPU in it has no clock rate to speak of. |

## Units

| Unit | Kind | |
|---|---|---|
| `8080` | `cpu` | The core. **Soldered on** — neither mounted nor connected, and `MOUNT cpu0:8080 x.dsk` is told so in a sentence. |

A card carrying an 8080 *and* an 8085, switching between them when the guest does
an `OUT`, is a real product, and it would list two units with one active
(DESIGN.md §3.0.1). That needs no new bus concept at all: the card decodes the
`OUT` and sets its own latch, exactly as a memory card switches banks. **The bus
arbitrates nothing, here as everywhere.**

## Reset

Both resets do the same short thing, and it is short on purpose:

- **`RESET` (RESET\*, the front-panel button)** — `PC ← 0000`, interrupts off, out
  of `HLT`.
- **`POWER` (POC\*, pin 76)** — identical. The 8080's reset does not distinguish
  them.

**Neither clears the registers**, because the 8080's reset does not clear them.
Neither touches memory, and a core is the tidiest possible proof of that rule: it
has no memory to touch.

`RESET CPU` resets the processor and *not* the other boards. It is a **debugging
convenience and not a real signal** — no wire on the backplane does that — and
the monitor says so when you use it.

## Interrupts

The card raises no interrupt of its own; it *responds* to `pINT` (pin 73), which
the bus carries as the wire-OR of every board asserting it.

At an instruction boundary, if `INTE` is set and the line is down, the 8080 runs
an **`IntAck` cycle and executes whatever the bus drives** — fetching each byte of
that instruction with another `IntAck`, so a device driving a 3-byte `CALL` works.
The PC does not move during the fetch, so the pushed return address is the
interrupted instruction.

**With no vector-interrupt board in the machine, nobody drives the acknowledge
cycle, the data bus floats to `0xFF`, and `0xFF` is `RST 7`.** That is not a
fallback hack — it is the real Altair, and we get it free from the same rule that
makes an unmapped read return `FF`. `tests/test_debug.cpp` builds a card that
drives the acknowledge and gets `RST 2` instead, on an unmodified bus, which is
the 88-VI's entire mechanism proved before the 88-VI exists.

`EI` takes effect **after the following instruction**. That one instruction of
grace is why `EI / RET` at the end of a handler is safe.

`HLT` parks the processor but **time keeps passing**, because the board that will
wake it is clocked by those very T-states. `GO` stops on a `HLT` only when nothing
is pulling `pINT` — a halted machine with a live interrupt source is *waiting*,
not finished, and it says so.

## T-states are load-bearing

They drive the clock throttle, the baud rate and disk rotation, so they are
counted per instruction with the conditional fixups: a taken `CALL` is 17 and an
untaken one 11, a taken `RET` 11 and an untaken one 5 — while a **conditional jump
is 10 either way**, because the address has already been fetched by the time the
8080 decides.

The check that this is right is not an opinion. Running the DBL boot PROM's
self-relocation loop from the default machine gives **1413 instructions and 9192
T-states**, and the datasheet predicts exactly that: three setup instructions (27)
plus 235 iterations of a six-instruction, 39-T-state loop (9165).

## The flags, and where a plausible-looking core goes wrong

`S Z 0 AC 0 P 1 CY`. Bit 1 always reads back as 1, bits 3 and 5 as 0.

Three rules are worth stating because they are invisible in ordinary code and each
one is a week of debugging when it is wrong:

- **`ANA`/`ANI` set AC from `(A | operand) & 0x08`** — not zero, and not the AND
  of the results. This is a documented 8080 quirk and it is one of the classic
  8080-vs-8085 divergences (the 8085 clears AC here).
- **Subtraction is `A + ~operand + 1` through the one adder the chip has.** `CY` is
  the *inverted* carry-out, and `AC` is the carry out of bit 3 of *that same
  addition*. Write `a - b` and invent a borrow rule that looks reasonable and you
  get an AC that is wrong on exactly the operands nobody tries by hand.
- **`P` is EVEN parity over all 8 bits** — not the low bit, and not odd.

`INR`/`DCR` touch every flag *but* carry, and that exception is the point of them:
it lets a loop counter be decremented inside a multi-byte add without destroying
the carry being propagated.

`DAA` always **adds** — the 8080 has no `N` flag, so it cannot tell an add from a
subtract and does not try. (The Z80 added `N` precisely because of this. Decimal
subtraction on an 8080 is done by adding the ten's complement.)

## The ten undocumented opcodes

`08 10 18 20 28 30 38` are `NOP`, `CB` is `JMP`, `D9` is `RET`, and `DD ED FD` are
`CALL`. **Real silicon runs them, so we run them**, and `DISASM` prints them with a
leading `*`. A disassembler that prints `???` is lying about what the chip does —
and worse, it would call the 3-byte `CB` a 1-byte unknown and desynchronise the
rest of the listing. A *run* of starred opcodes almost always means you are looking
at data, or at a Z80 binary, and being able to see that at a glance is the point of
the mark.

## The gate is passed (2026-07-11)

**TST8080, 8080PRE, CPUTEST and 8080EXM all pass** (DESIGN.md §3.2). The suites
live in `tests/cpu/`, the harness is `tests/cputest.cpp`, and `ctest` runs the
first three on every build; 8080EXM is labelled `slow` and is run with
`ctest -L slow`.

| Suite | Result | Cost |
|---|---|---|
| TST8080 (Microcosm, 1980) | CPU IS OPERATIONAL | 1,217 instructions |
| 8080PRE (Bartholomew & Cringle) | tests complete | 1,254 instructions |
| CPUTEST (SuperSoft Diagnostics II, 1981) | CPU TESTS OK | 34.0M instructions |
| **8080EXM** | **all 25 CRC groups PASS** | 2.92B instructions, 23.8B T-states |

**8080EXM is the one that counts.** It runs every instruction against every
interesting operand pair and CRCs the result *including all five flags*, then
compares against CRCs captured from real silicon. It does not care how confident
anybody was. Of its 25 groups, the two worth naming here are `aluop
<b,c,d,e,h,l,m,a>` — which is what proves the `ANA` half-carry rule and the
inverted borrow described above, and which would have failed for a single wrong
operand pair — and `<daa,cma,stc,cmc>`, which is precisely the instruction the
Python prototype's own notes admitted was "complex, not fully tested".

**The T-states are corroborated independently of the CRCs.** SuperSoft's own
documentation says CPUTEST's timing section takes about two minutes on a 2 MHz
8080. Our run of it costs 255,660,114 T-states, which at 2 MHz is **127.8
seconds** of simulated time. Nothing in the CRC checks constrains that number —
it comes out right because the cycle counts are right, and it is the only
evidence we have that they are.

## Open questions

- **The interrupt-acknowledge T-state.** The 8080's `INTA` machine cycle is longer
  than a normal opcode fetch, so an interrupt-driven `RST` should cost slightly
  more than the 11 T-states we charge it. The exact figure is not stated in the
  manuals to hand. **We charge the instruction's ordinary T-states and no more**,
  rather than invent a number — it is a fraction of a percent, and it is written
  down here instead of being guessed at.
