# `z80` — a generic Z80 CPU card

The processor card, with a Z80 in the slot instead of an 8080. In a real Altair
the CPU *was* a card and people did exactly this swap, which is the whole reason
the 8080 lives in `boards/` and gets `properties()`, both resets, and a line in
`BOARDS` with no special cases — so a second processor is *additive*, not a
rewrite.

It is **deliberately generic**. A named Z80 S-100 board typically added features —
a power-on-jump PROM, a 4 MHz crystal, an on-card MMU — and this card models none
of them, so claiming a specific product would be inventing fidelity we cannot
source (DESIGN.md §0.1). It is a `z80` the way the other card is an `8080`: the
core, on a card that decodes nothing.

**Sources:** Zilog *Z80 CPU User's Manual* (UM008) for the documented behaviour,
and Sean Young's *The Undocumented Z80 Documented* for the `F5`/`F3` result bits,
the `MEMPTR`/`WZ` register they leak, and the `DD`/`FD` half-register and `DDCB`
reg-copy forms. Nothing was taken from another emulator.

## Everything the 88-CPU says still holds

This card is the twin of the [`8080`](mits-88cpu.md). Read that page for the parts
that are identical here, because they *are* identical — same code, different core:

- **It decodes nothing.** A bus master originates cycles; it does not answer them.
  `WHO 0100` says the CPU is not there, and that is the truth.
- **The crystal and the idle nap are on the card**, as `clock_hz` and `idle`, with
  the same defaults (flat out; stand down at a prompt) and the same read-only
  `achieved_hz` companion. `SET cpu0 clock_hz=4000000` gives a period 4 MHz Z80;
  emulated time is identical either way and only the host sleep differs.
- **The core is a soldered-on unit**, neither mounted nor connected.

## Properties

| Property | Default | Runtime? | What it is |
|---|---|---|---|
| `clock_hz` | `0` | yes | The crystal on the card. `0` runs flat out (the default). `2000000` or `4000000` buy back a period Z80's real timing and the real waiting with it; the guest cannot tell the difference in emulated time. |
| `idle` | `on` | yes | Stand down when the guest is only polling an empty keyboard. A host policy, orthogonal to `clock_hz`; the guest cannot tell. `SET cpu0 idle=off` for the spin. |
| `achieved_hz` | — | read-only | T-states per real second the run loop last reached. `0` until it has run. |

## Units

| Unit | Kind | |
|---|---|---|
| `z80` | `cpu` | The core. Soldered on. `DISASM` and `REGS` follow it automatically because the active core says which instruction set it speaks. |

## The register set

The 8080's, plus everything the Z80 added, and the monitor renders all of it with
no change — `REGS`, `SET REG`, breakpoint conditions and the MCP schema are written
against the core's own reflection, so a Z80 gets every one of them for free:

- **`IX` / `IY`** — the 16-bit index registers, and their undocumented halves
  `IXH`/`IXL` reachable through the `DD`/`FD` forms.
- **The alternate bank** `AF'` `BC'` `DE'` `HL'` — swapped by `EXX` (the BC/DE/HL
  trio) and `EX AF,AF'`.
- **`I`** (interrupt vector base), **`R`** (memory refresh), **`IM`** (interrupt
  mode 0/1/2), and **`IFF1`/`IFF2`** (the interrupt enable and its shadow).
- **`WZ`** — the internal `MEMPTR` latch, reachable by name because its high byte
  leaks into `F5`/`F3` and being able to watch it is worth the row.

The flags are **`S Z H P/V N C`** on the status line. `SET REG` names them
`CY` (carry) and `HF` (half carry) rather than `C`/`H`, because `C` and `H` are the
`B,C,…H,L` register halves — the same clash the 8080 core dodges by calling its
carry `CY`. Name is identity, label is only paint (DESIGN.md §3.0.3).

## Flags are the whole game

The Z80 keeps two **undocumented** flag bits, `F5` and `F3`, that copy result bits
5 and 3 — and a hidden `MEMPTR`/`WZ` register that decides what `F5`/`F3` become
for `BIT n,(HL)`, `BIT n,(IX+d)` and the block instructions. Getting those right is
the entire reason the acceptance bar is **ZEXALL**, not merely ZEXDOC: ZEXDOC masks
the undocumented bits off, ZEXALL checks them against CRCs captured from real
silicon and does not care how confident the author was. This is the same ruthless
standard the 8080 met with 8080EXM.

`DAA` uses the `N` flag to tell an add from a subtract — the thing the 8080 could
not do — so decimal subtraction is a first-class operation here.

## Interrupts

The card raises none of its own; it responds to `pINT` (pin 73), the bus's wire-OR
of every board pulling it. At an instruction boundary, if `IFF1` is set:

- **IM 0** executes whatever the acknowledging device drives onto the bus — the
  same `IntAck` seam as the 8080, so a device jamming an `RST` works, and a machine
  with no vector card floats `0xFF` and runs `RST 38`.
- **IM 1** jams `RST 38` regardless.
- **IM 2** forms a vector address from `I` (high byte) and the device's byte (low),
  and reads the handler address from there.

`EI` takes effect **after the following instruction**, so `EI / RET` at the end of
a handler returns before another interrupt can land. `HLT` parks the processor
while T-states keep passing, because the board that will wake it is clocked by
them. `NMI` has no S-100 wire in this machine, so there is no source to trigger it
and it is not modelled — an absent signal, not an invented one.

## The gate

Nothing here ships as "working" until **ZEXDOC and ZEXALL pass** — the same rule
that kept the 8080 boards unbuilt until 8080EXM was green. The suites and the run
results live in `tests/cpu/z80/` and the harness is `tests/cputest.cpp`; ZEXALL is
labelled `slow` and runs with `ctest -L slow`.
