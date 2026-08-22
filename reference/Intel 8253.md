# Intel 8253 Programmable Interval Timer

Source: [System Support 1 Technical Manual.pdf](#) (© CompuPro 1981, Godbout Electronics;
Document #11620), §3 and the reprinted Intel 8253 data sheet (pp.62–69).

The **Intel 8253** (and the faster **8253-5**) is a Programmable Interval Timer: three
independent 16-bit down-counters and one control-word register, in a four-address block. Each
counter has a **clock input**, a **gate input**, and an **output (OUT)** pin. It is the timer
on the [CompuPro System Support 1](CompuPro%20System%20Support%201.md); the chip model lives in
`src/chips/intel8253.{h,cpp}`.

This is a distilled emulation reference: the programmer-visible register map, the control-word
layout, and what each of the six modes does to OUT and to the count. The board-specific wiring
(clock/gate sources, OUT-to-interrupt routing) is in the SS-1 reference §3; the generic chip
theory Intel reprints wholesale is summarised to what an emulator needs.

> **Not to be confused with the 8254.** The 8254 is a pin-compatible superset that adds a
> **Read-Back Command** (control word with SC1 SC0 = `11`) and a status byte. The plain 8253
> has neither — `SC1 SC0 = 11` is simply illegal and is ignored.

---

## 1. Port map

The card decodes A1/A0 into four addresses. On the SS-1 they are base+4 … base+7.

| A1 A0 | Register | Access |
|:---:|---|---|
| 0 0 | Counter 0 (load / read) | read/write |
| 0 1 | Counter 1 (load / read) | read/write |
| 1 0 | Counter 2 (load / read) | read/write |
| 1 1 | Control Word Register | **write-only** |

Reading the control-word address returns nothing meaningful (it floats).

---

## 2. Control word (write, +3)

| D7 D6 | D5 D4 | D3 D2 D1 | D0 |
|:---:|:---:|:---:|:---:|
| **SC1 SC0** — Select Counter | **RL1 RL0** — Read/Load | **M2 M1 M0** — Mode | **BCD** |

- **SC1 SC0** — `00` counter 0, `01` counter 1, `10` counter 2, `11` **illegal** (8254
  read-back; not on the 8253).
- **RL1 RL0** — the read/load format:
  - `00` = **Counter Latch Command** (see §4) — does *not* change the mode or format.
  - `01` = read/load **LSB only**.
  - `10` = read/load **MSB only**.
  - `11` = read/load **LSB first, then MSB** (two accesses).
- **M2 M1 M0** — mode 0–5 (`110`→mode 2 and `111`→mode 3 alias, as on the silicon).
- **BCD** — `0` = 16-bit binary count (modulus 65536); `1` = four-decade **BCD** count
  (modulus 10000).

Writing a control word for a counter **stops** that counter and forces OUT to the mode's
initial level; counting begins again only when a new count is loaded (the full value — for the
LSB-then-MSB format, on the **MSB** write).

A count value of **0** means the **full modulus** (65536 binary, 10000 BCD).

---

## 3. Modes

Gate high = counting enabled. On the SS-1 every gate is tied high (§3.3), so the
gate-triggered modes (1 and 5) never see a rising edge and never trigger.

| Mode | Name | OUT behaviour (gate high) |
|:---:|---|---|
| 0 | Interrupt on Terminal Count | OUT **low** after the count loads; goes **high** at terminal count (N clocks later) and **stays high**. **The mode the SS-1's interrupt wiring is built for.** |
| 1 | Hardware Retriggerable One-Shot | OUT low on the **gate's rising edge**, high at terminal count. Needs a gate edge — idle otherwise. |
| 2 | Rate Generator | OUT normally high; **low for one clock** when the count reaches 1, then reloads N. Periodic; count reads N…1. |
| 3 | Square Wave Generator | OUT high for the first half of each period, low for the second (even N: N/2 each; **odd N: the extra clock is spent high**). |
| 4 | Software Triggered Strobe | OUT high; **low for one clock** at terminal count after the count loads. One-shot. |
| 5 | Hardware Triggered Strobe | Like mode 4 but the count starts on the **gate's rising edge**; retriggerable. Needs a gate edge. |

---

## 4. Reading a counter

A live read of a counting counter can catch it mid-decrement, and in the LSB-then-MSB format
the two bytes may straddle a tick. The **Counter Latch Command** (control word with
RL1 RL0 = `00`) latches the counter's current value into a hold register; subsequent reads
return that frozen value — LSB, MSB, or LSB-then-MSB per the counter's programmed format —
until the value has been fully read, after which reads track the live count again. The latch
command does not disturb counting or change the format.

---

## 5. Emulation notes

- **Counts are computed, not stepped.** A real 8253 decrements on every clock edge; stepping a
  2 MHz counter in software would be millions of callbacks a second. Instead remember the
  T-state a counter's value was loaded and derive the current count and OUT from the elapsed
  clock ticks — the chip is a pure function of `(programmed state, now)`. Schedule a wake only
  when an OUT that feeds an enabled interrupt is about to change.
- **The clock input is 2 MHz on the SS-1** (S-100 bus pin 49), independent of the CPU clock:
  one counter tick takes `cpu_hz / 2_000_000` T-states.
- **Modes 0/2/4** count-read exactly; **mode 3** decrements by two per tick and its read-back
  is rarely relied upon — OUT is what feeds interrupts, and OUT is exact.
- **Modes 1 and 5** are gate-triggered; with the gate tied high and no edge modeled they idle
  at their initial OUT (high). This is the faithful ungated behaviour, not a stub.
- **BCD** counting decodes the written value from BCD, counts in decimal against a 10000
  modulus, and re-encodes the read-back to BCD.
