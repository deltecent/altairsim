# Altair 680b Turnkey Front Panel (schematic 6800-TURNKEY, SH 1-11)

Source: [680 Turnkey Front Panel Schematic.pdf](#) (MITS *6800 Turnkey Schematic*, drawing
**SH 1-11**, © MITS Inc.)

The single-sheet schematic for the **Altair 680b Turnkey** front panel — the *reduced* panel
a turnkey machine ships with **instead of** the full hex front panel of the
[Operator's Manual](Altair%20680b%20Operators%20Manual.md). It has **no address/data toggle
switches, no DEPOSIT, no EXAMINE, and no A0–A15 / D0–D7 LEDs**. It does exactly two things to
the 6800: **RESET** it and put it in **RUN or HALT**, with three indicator LEDs (power, run,
halt). It is the 680b analogue of the 8800b [Turnkey Module](MITS%20Turn%20Key%20Board.md) idea
— minus the boot PROM (on the 680b the monitor lives on the main board, see
[[altairsim-turnkey-board]]).

This is a distilled emulation reference. The schematic is a **single image sheet with no
accompanying text**; the gate-level flip-flop/NAND wiring, the debounce RC values, the pull-up
resistors, the LED dropping resistors, and the test points are **out of scope except where a
value sets a bus-visible signal or a build option**. What is kept: the two software/bus-visible
signals and their connector pins, the reset-scope jumper, the switch and LED complement, and how
this panel differs from the full hex panel.

## 1. What the panel is

A turnkey 680b is meant to power up, run its monitor (or a jumpered auto-start program), and be
operated **through the terminal**, not the panel. So the turnkey panel carries only:

- **SW1 — RESET** (momentary SPDT).
- **SW2 — RUN / HALT** (momentary; a push toward RUN or toward HALT).
- **LD1 — PWR ON**, **LD2 — RUN**, **LD3 — HALT** (indicator LEDs).

There are **no data or address controls at all** — nothing to deposit or examine memory with.
Program entry on a turnkey machine is via the terminal + monitor, or the machine boots straight
into a program placed by the main-board jumpers.

## 2. Bus-visible signals (the emulation payload)

Only two signals leave this board onto the 680b bus, plus power/ground. Both are **debounced and
synchronized to Φ2** (a chain of 7474 D flip-flops clocked by Φ2) so the guest never sees switch
bounce or a mid-cycle change:

| Signal | Connector pin | Sense | Drives |
|---|---|---|---|
| **`RES̄`** (reset) | **54** | **active-low** | 6800 `RESET` — positive edge → PC from `FFFE/FFFF` (Monitor start) |
| **RUN/HALT** | **63** | — | 6800 `HALT` — hi = RUN, lo = halted (→ `BA` hi, buses tri-stated) |
| **Φ2** (clock in) | **57** | — | the synchronizing clock for both above (generated on the main board) |
| **VCC** | 1 / 51 | +5 V | — |
| **GRD** | 50 / 100 | ground | — |

The `RESET` edge behavior and the `HALT`→`BA`→tri-state semantics are documented on the
[Theory of Operation](Altair%20680b%20Theory%20of%20Operation.md) — this panel is simply where
those two lines are *asserted from*. On the full hex panel, HALT also enables the
front-panel deposit path; **the turnkey panel has no deposit path**, so HALT here just stops the
processor and lights LD3.

## 3. Reset-scope jumper (JA / JB / JC)

A three-pad jumper selects **when the RESET switch is honored**, gated against the RUN/HALT
state:

| Jumper | Meaning |
|---|---|
| **JA–JC** | **Reset in Run or Halt** — the RESET switch works whether the machine is running or halted |
| **JB–JC** | **Reset in Halt only** — the RESET switch is ignored while the machine is running |

This is a **power-on build constant**, not guest-writable state — model it as a panel/machine
property, the same way the main-board config jumpers are handled in the
[Operator's Manual](Altair%20680b%20Operators%20Manual.md) reference.

## 4. Switch debounce and the RUN/HALT latch (build detail, not bus-visible)

Recorded so the values are not re-derived from the sheet; **none of this is guest-visible** — it
only shapes the two signals in §2.

- **RESET debounce:** SW1 (MOM SPDT) with `R5` 20 K pull-up to VCC, `R6` 10 Ω, `C1` 0.1 µF, test
  point TP1; through a gate into the reset D flip-flop (`R4` 4.7 K pull-up on the D input),
  reclocked on Φ2, out through a NAND to `RES̄` (TP2).
- **RUN/HALT:** SW2 (MOM) with `R2` / `R3` 4.7 K pull-ups; a cross-coupled NAND **set/reset
  latch** (`R8` 4.7 K) holds the last-pushed state and drives the RUN/HALT flip-flops → pin 63
  (TP4), plus **LD2 RUN** (`R7` 560 Ω) and **LD3 HALT** (`R9` 560 Ω).
- **Power indicator:** **LD1 PWR ON** across VCC/GRD through `R1` 560 Ω. Purely an LED — no
  bus signal.
- ICs: two 7474 dual D flip-flops (sheet labels **A** and **B**) and one 7400 quad NAND (**C**).
  Test points TP1–TP4.

## 5. Emulation notes / gotchas

- **This is a front-panel *model*, not the hex panel.** A turnkey 680b exposes only RESET,
  RUN/HALT, and three LEDs. Do **not** give it deposit/examine or address/data toggles — those
  belong to the [Operator's Manual](Altair%20680b%20Operators%20Manual.md) full panel, and a
  machine has one panel or the other. Reproducing a deposit path here would be inventing a
  control the board never had [[altairsim-no-invented-hardware]].
- **The only two things to model are the two bus signals** — `RES̄` (pin 54, active-low → 6800
  RESET) and RUN/HALT (pin 63 → 6800 HALT). Their *effects* (reset vector fetch, HALT→`BA`
  tri-state) are the 6800/main-board behavior in
  [Theory of Operation](Altair%20680b%20Theory%20of%20Operation.md); the panel just drives the
  lines.
- **Both signals are Φ2-synchronized.** In an instruction-stepped emulator this is invisible
  (apply RESET/HALT at an instruction boundary); the flip-flop chain only matters to a
  cycle-accurate model, and even then it is a one-Φ2 alignment, not a behavior change
  [[altairsim-plausible-but-wrong-timing]].
- **The JA/JB/JC jumper is a config constant.** Expose "reset in run-or-halt" vs "reset in
  halt-only" as a panel property; with JB–JC selected, a RESET request while RUN is asserted is
  a no-op.
- **No boot PROM on this board.** Unlike the 8800b Turnkey Module
  [[altairsim-turnkey-board]], the 680b turnkey panel carries no PROM window — the reset vector
  still points into the **main-board** monitor PROM at `FF00`
  ([Theory of Operation](Altair%20680b%20Theory%20of%20Operation.md)).

## 6. Key facts at a glance

| | |
|---|---|
| Board | Altair **680b Turnkey** front panel (schematic SH 1-11) — the reduced panel |
| Controls | **RESET** (SW1, MOM SPDT) · **RUN/HALT** (SW2, MOM) — **no** address/data/DEPOSIT |
| Indicators | **PWR ON** (LD1) · **RUN** (LD2) · **HALT** (LD3) |
| `RES̄` | connector pin **54**, **active-low** → 6800 RESET (→ PC from `FFFE/FFFF`) |
| RUN/HALT | connector pin **63** → 6800 HALT (hi = RUN, lo = halted → `BA` → buses tri-stated) |
| Φ2 in | connector pin **57** — synchronizes both signals |
| Power | VCC pins 1/51, GRD pins 50/100 |
| Reset-scope jumper | **JA–JC** = reset in Run **or** Halt; **JB–JC** = reset in Halt **only** |
| Debounce | RESET: R5 20 K / R6 10 Ω / C1 0.1 µF; RUN/HALT: cross-coupled NAND latch, R2/R3 4.7 K |
| ICs | two 7474 dual D flip-flops (A, B) + one 7400 quad NAND (C) |
| Not present | no boot PROM (monitor is on the main board at `FF00`); no deposit/examine path |
