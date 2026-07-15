# MITS 88-VI / 88-RTC — Vector Interrupt and Real Time Clock Board

Source: [88-VI-RTC.pdf](#)

MITS, February 1976. The 88-VI (Vector Interrupt) and 88-RTC (Real Time Clock)
are one physical S-100 card. The 88-VI may be built alone; the 88-RTC is an
add-on that shares the same board, control port, and interrupt structure.
This document extracts everything needed to emulate the card; assembly,
soldering, parts lists, and marketing text are omitted.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| I/O port | **254 decimal = 376 octal = 0xFE** |
| Port direction | **Write only** (control/status register). Not read to service. |
| Interrupt lines | 8 bus lines **VI0 … VI7**, active **low** |
| Priority | **VI0 = highest, VI7 = lowest** |
| Vector | VI level *n* → **RST *n*** → CPU jumps to octal *n*×10 (see table) |
| RTC clock sources | 60 Hz line frequency **or** 10 kHz (2 MHz ÷ 200) |
| RTC divide rates | ÷1, ÷10, ÷100, ÷1000 (jumper-selected) |
| RTC output | one interrupt pulse per selected interval, on the jumpered VI level |
| Power-on | POC (power-on-clear) disables all functions until initialized |

The board serves two jobs at once:
1. Place the correct **RST instruction** (with the 3-bit code for the winning
   priority level) on the data bus when the CPU acknowledges an interrupt.
2. Allow **only the highest active priority** to interrupt, and mask equal or
   lower levels until the current service routine dismisses itself.

> A system using the 88-VI must **not** have any I/O board strapped for single-
> level interrupt. Interrupts on I/O boards must be hard-wired to one of the
> eight 88-VI levels.

---

## 2. Interrupt vectoring (8080 RST)

The 8080 has one interrupt input. On acknowledge, the board jams an RST
instruction onto the bus. `RST n` (opcode `3n7` octal, an `11 AAA 111` byte
where `AAA` is the 3-bit level code) pushes PC and jumps to octal location
*n*×10.

| VI level | Priority | RST | Jump target (octal) | Jump target (hex) |
|----------|----------|-----|---------------------|-------------------|
| VI0 | highest | RST 0 | 0   | 0x00 |
| VI1 |         | RST 1 | 10  | 0x08 |
| VI2 |         | RST 2 | 20  | 0x10 |
| VI3 |         | RST 3 | 30  | 0x18 |
| VI4 |         | RST 4 | 40  | 0x20 |
| VI5 |         | RST 5 | 50  | 0x28 |
| VI6 |         | RST 6 | 60  | 0x30 |
| VI7 | lowest  | RST 7 | 70  | 0x38 |

Only 8 bytes exist between adjacent vectors, so a real service routine at (say)
octal 40 must `JMP` out to the rest of its handler.

**Acknowledge sequence (hardware detail).** The eight VI lines are strobed into
the 8214 priority IC ("B") on every clock. When an active level out-prioritizes
the current level, the 8214 requests an interrupt and drives PINT low. On the
CPU's interrupt-acknowledge (SINTA) the 3-bit level code (bus data bits
DI3, DI4, DI5) is gated onto the bus together with the fixed `11…111` bits of
RST. **The CPU interrupt is automatically disabled when the RST is taken** and
must be re-enabled (`EI`) inside the service routine before a higher level can
preempt it.

---

## 3. Control / status register — OUT to port 0xFE (376 octal)

A single output byte to port 254 (0376 octal) drives all control functions.
The low 4 bits are latched into the internal status register of the 8214 ("B");
bits 4–7 drive discrete control gates.

| Bit | Name | Function |
|-----|------|----------|
| 0 | Level code b0 | Current interrupt level (3-bit code, bits 0–2). Prevents equal/lower priority from interrupting. See §3.1 and the level-code table. |
| 1 | Level code b1 | " |
| 2 | Level code b2 | " |
| 3 | Current-level-register enable | Enables the current-level comparison. **Output 0 (low) only during initialization.** In normal operation it is **1 (high)**, making the current priority uninterruptable by equal/lesser levels. If left 0, level 7 can never interrupt. Ha 12 high / Ha 13 low path. |
| 4 | RTC interrupt reset | A **high** resets/clears the RTC-generated interrupt (clears flip-flop IC "Fb", gates K10/K4). The RTC service routine must output bit 4 high, together with its 3-bit RST code, to dismiss the RTC interrupt. **The RTC interrupt is NOT cleared by reading/writing a data channel** the way most I/O boards clear theirs. |
| 5 | Clear clock divider | A **high** clears the counter network that divides the source down (ICs R, V, W, U, S). Set **high during initialization** and whenever the clock must restart at time zero. |
| 6 | RTC interrupt enable | High **enables** the RTC interrupt (Hb 9 high, gate G9); low disables it. |
| 7 | 88-VI structure enable | High **enables** the whole vectored-interrupt structure; low disables it. |

### 3.1 Level-code encoding (authoritative)

The 3-bit level code in bits 0–2 is the **one's complement of the level**, i.e.
`code = 7 − level`. Bit 3 is also set (high) in every normal service routine.
The manual's per-level `MVI A,xxQ` constants (page 5) are the ground truth:

| Interrupt level | RST vector | MVI A,#Q (octal) | Byte value | bits 3..0 |
|-----------------|-----------|------------------|-----------|-----------|
| 0 | 0  | `MVI A,17Q` | 0x0F | 1111 |
| 1 | 10 | `MVI A,16Q` | 0x0E | 1110 |
| 2 | 20 | `MVI A,15Q` | 0x0D | 1101 |
| 3 | 30 | `MVI A,14Q` | 0x0C | 1100 |
| 4 | 40 | `MVI A,13Q` | 0x0B | 1011 |
| 5 | 50 | `MVI A,12Q` | 0x0A | 1010 |
| 6 | 60 | `MVI A,11Q` | 0x09 | 1001 |
| 7 | 70 | `MVI A,10Q` | 0x08 | 1000 |

So bits 2..0 = (7 − level) and bit 3 = 1. Each routine then ORs in the enable
bits (`ORI 300Q` = set bits 6,7; use `ORI 330Q` = set bits 3,4,6,7 if the RTC is
hooked to this level, so bit 4 also clears the RTC interrupt).

> **Contradiction in the manual (page 3 vs. page 5).** Page 3 states "the
> routine at level 4 … outputs a 100 for bits 2, 1 and 0" — i.e. it describes the
> code as the level number itself (level 4 → 100b = 4). Page 5's constant chart,
> and the worked examples, instead use `code = 7 − level` (level 4 → 011b = 3,
> from `MVI A,13Q`). **Follow the page-5 chart and the code (`7 − level`); the
> page-3 wording is wrong.** (This is one of the two bit-level contradictions
> noted in the project memory; the MITS PS2 monitor's own ISR confirms the
> `7 − level` convention.)

---

## 4. Initialization

Initialize with a single output that: enables the VI structure, enables the RTC
interrupt, clears the RTC interrupt, clears the divider (start at time zero), and
holds the current-level register **disabled** (bit 3 = 0) with level code 0.

```
INIT:  MVI  A,360Q     ; = 0xF0 = bits 4,5,6,7 high; bits 0-3 low
       OUT  254        ; port 0376 octal / 0xFE
       EI
```

`360Q` = `1111 0000`: bit7 VI enable, bit6 RTC-int enable, bit5 clear divider,
bit4 reset RTC int; bits 3–0 = 0 (current-level register disabled, level 0).

POC (power-on-clear) forces every function disabled at power-up until this
output is made.

---

## 5. Vector-interrupt service-routine pattern

Each RST vector holds a short stub that saves registers and jumps to the body:

```
; at octal 20 (level 2 example)
20  PUSH B
21  PUSH D
22  PUSH H
23  PUSH PSW
24  JMP  LEV2       ; interrupts are auto-disabled the moment the RST is taken
```

The body updates the "current level" byte in RAM (`CURLEV`), tells the board the
new current level so equal/lower levels are masked, then re-enables interrupts so
higher levels can still preempt:

```
LEV2:  LDA  CURLEV      ; get level that was interrupted
       PUSH PSW         ; save old level on stack
       MVI  A,15Q       ; new current level = 2  (code for level 2)
       STA  CURLEV
       ORI  300Q        ; OR in VI enable bits (use 330Q if RTC on this level)
       OUT  376Q        ; tell VI board which levels to accept
       EI
       ...              ; device service routine
       DI
       POP  PSW         ; recover old level
       STA  CURLEV
       ORI  300Q        ; OR in VI bits for old level
       OUT  376Q        ; restore prior mask
       POP  PSW
       POP  H
       POP  D
       POP  B
       EI
       RET
```

Nesting is allowed up to 7 deep (a level can be interrupted by any strictly
higher level), and each lower routine is guaranteed to resume and complete.

---

## 6. Real Time Clock (88-RTC)

The RTC generates a periodic interrupt at a precise, jumper-selected interval,
letting software keep time. It rides on the same port (254/0xFE) and one of the
eight VI levels.

### 6.1 Source selection and rationale
- **Line frequency (60 Hz)** — best long-term accuracy; power companies trim
  line frequency to hold average correct. Jumper **S → LF**.
- **10 kHz** — derived from the 2 MHz system clock (÷200); use for fast
  intervals down to 100 µs. Jumper **S → CF**.

### 6.2 Interval table

The interrupt fires once per interval shown (the "time interval" is the period
between interrupts; e.g. 1000 Hz ⇒ 1000 interrupts/second).

| Source | Divide rate | Divided freq | Interval per interrupt |
|--------|-------------|--------------|------------------------|
| Line 60 Hz | ÷1    | 60 Hz    | 16.67 ms |
| Line 60 Hz | ÷10   | 6 Hz     | 166.7 ms |
| Line 60 Hz | ÷100  | 0.6 Hz   | 1.67 s |
| Line 60 Hz | ÷1000 | 0.06 Hz  | 16.67 s |
| 10 kHz (2 MHz derivative) | ÷1    | 10,000 Hz | 100 µs |
| 10 kHz | ÷10   | 1,000 Hz | 1 ms |
| 10 kHz | ÷100  | 100 Hz   | 10 ms |
| 10 kHz | ÷1000 | 10 Hz    | 100 ms |

### 6.3 RTC control bits (subset of the port; see §3)
- **Bit 4 high** clears the RTC interrupt. The RTC does **not** clear on a data-
  channel read/write; the ISR must explicitly output bit 4 high along with its
  RST code.
- **Bit 5 high** clears the divide chain (the circuit that divides 2 MHz to
  10 kHz and the decade dividers). Set high at init and to start at time zero.
- **Bit 6 high** enables the RTC interrupt; low disables it.

### 6.4 RTC signal path (for accurate emulation)
- 2 MHz `CLOCK` → 4-bit binary counters R and V → 10 kHz, available at jumper
  **CF**.
- Line voltage (bus line #55, unregulated +16 V) → R19/R20 → Schmitt triggers
  (IC "T") → 60 Hz square wave, available at jumper **LF**.
- The selected source (jumper **S** to LF or CF) feeds decade counter W, then U,
  then S: output appears at **A** (÷1), **B** (W ÷10), **C** (U ÷100),
  **D** (S ÷1000). One of these is jumpered to pad **IN** to become the RTC
  clock input.
- When enabled, each falling edge of that square wave toggles flip-flop Fb
  (pin 12), which is inverted through open-collector gate H3 to pad **RI**.
- Pad **RI** is jumpered to one of the eight VI levels, so each interval raises
  that VI line. The ISR outputs bit 4 high to clear Fb (gates K10/K4).

---

## 7. Jumper / strapping options

All are hard-wired (soldered) links on the 88-RTC portion; the 88-VI alone needs
none of the RTC straps.

| Jumper group | Pads | Meaning |
|--------------|------|---------|
| Interrupt level | **RI → 0..7** | Which VI level the RTC interrupt drives (0 highest, 7 lowest). |
| Source input | **S → LF** | Use 60 Hz line-frequency source. |
| | **S → CF** | Use 10,000 Hz (2 MHz derivative) source. |
| Frequency divide | **IN → A** | ÷1 |
| | **IN → B** | ÷10 |
| | **IN → C** | ÷100 |
| | **IN → D** | ÷1000 |

Additional taps referenced by the schematic: **CF** (10 kHz clock out), **LF**
(line-freq square-wave out). For the line-frequency option a chassis hard-wire
brings unrectified +16 V from power-supply diode D5 to motherboard pin/bus **#55**.

---

## 8. Worked 8K-BASIC clock example (timekeeping quirks)

The manual ships a machine-language ISR (8K BASIC + USR) that maintains hours,
minutes, seconds, and 60ths-of-a-second in four consecutive RAM bytes. It straps
the RTC for **line frequency, ÷1** (60 interrupts/second) on **level 1** (`RST 1`
at octal 10; note the ISR loads `MVI A,10Q` for level 7 code style but uses level
via the JMP at 70 — see below). Key facts an emulator author may need:

- The interrupt entry uses a `JMP` placed at octal 70 (RST 7 vector) that
  branches to the response routine.
- The ISR reloads the current level with `MVI A,10Q`, `ORI 330Q` (bits 3,4,6,7),
  `OUT 254` — bit 4 clears the RTC interrupt, bit 6 keeps RTC enabled.
- Counters roll at 60 (60ths→seconds), 60 (seconds→minutes), 60 (minutes→hours),
  24 (hours), using `SBI 59` / `SBI 23` compares.
- BASIC is patched via POKEs: a `JMP` to the ISR is stored at location 70
  (`POKE 56,195` / `POKE 57,187` / `POKE 58,31`), and the USR `JMP FCERR` at 72
  is redirected to `INIT` (`POKE 73,250` / `POKE 74,31`). BASIC's "memory size"
  is answered 8122 to reserve the routine.
- Time is seeded by POKEing the four counter bytes 8180–8183 (60ths, seconds,
  minutes, hours) and started with `A = USR(1)`.

These specifics are BASIC-integration detail, not board behavior, but they
document the expected register/port usage.

---

## 9. Implementation notes and pitfalls

- **Port is write-only.** All configuration and interrupt dismissal happen via
  `OUT 0xFE`. There is no meaningful read of this port; do not model RTC-clear on
  a read.
- **RTC interrupt must be explicitly cleared** by outputting bit 4 high; it is
  level/latched via flip-flop Fb, not auto-clearing.
- **VI lines are active-low**, priority-encoded by an 8214, VI0 highest.
- **The vector code is `7 − level`** in bits 0–2 with bit 3 = 1 during normal
  operation. Ignore the contradictory page-3 description (§3.1).
- **Init value 0xF0** (`360Q`): VI on, RTC-int on, divider cleared, RTC-int
  reset, current-level register disabled, level 0.
- **Auto-disable on RST**: after any interrupt is vectored the 8080's INTE is
  cleared; higher-priority nesting only resumes after the ISR issues `EI`.
- **8214 = IC "B"**, socketed (3214 or 8214 acceptable). Priority resolution,
  level latching, and the current-level status register all live there.
