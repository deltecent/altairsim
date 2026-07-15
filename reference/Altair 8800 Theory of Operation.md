# Altair 8800 Theory of Operation Manual & Schematics

Source: [Altair 8800 Theory of Operation.pdf](#)

MITS, Inc., 1975. Extracted for the altairsim project as an in-repo reference for
emulating the machine's internals. Marketing, order, and warranty material is
omitted. Signal names, bit numbers, and pin numbers are preserved verbatim.

> Notation used throughout the original: `+` means **or**, `•` means **and**, an
> overbar (rendered here as a leading/trailing bar or noted as "active-low") means
> the signal is asserted low. A trailing `*` in a logic equation marks the
> complemented (active-low) form of a term.

---

## System Overview

The Altair 8800 is designed around Intel's **8080** microprocessor (n-channel
silicon-gate MOS, single-LSI CPU). The 8080 uses a separate **16-line address
bus** and an **8-line bidirectional data bus**.

The Altair uses a **100-line bus structure** for all data transfer between the CPU
and memory or I/O devices. The bus carries all data and address lines plus the
unregulated supply voltages and all control and status signal lines. Cards other
than the CPU take control of the bus **only when addressed by the CPU**.

Core boards:
- **CPU Board** — 8080 chip, bus drivers, system clock, gating logic, system status latch.
- **Display/Control Board** — front panel RUN/STOP, single step, examine/deposit.
- **1K Static Memory Board** — Intel 8101 (256×4) static RAM.
- **Power Supply** — unregulated +8, +16, −16 V to the bus.

---

## CPU Board

Contains the 8080 chip, bus drivers, system clock, gating logic, and the system
status latch. Schematics: 880-101, 880-102, 880-103.

### Bus Drivers

All signals entering or leaving the CPU board are buffered with **8T97 tri-state
drivers**. "In" and "out" are always defined **with respect to the processor**.
The 8080's single bidirectional bus is split at the processor into a separate
**data-in** bus and **data-out** bus.

| Group | Driver ICs | Notes |
|-------|-----------|-------|
| 16 address lines | B, C, and 4 gates of D | disabled by `ADDR DSBL` |
| 8 data-out lines | E, and 2 gates of D | disabled by `DO DSBL` |
| 8 data-in lines | F, and 2 gates of H | enabled by processor via one gate of IC R |
| 8 status output signals | 4 gates of G, 4 gates of H | disabled by `STATUS DSBL` |
| 6 command/control output signals | 8T97 IC J | disabled by `C/C DSBL` |
| 4 command/control input signals | 4 gates of 8T97 IC I | READY, HOLD, INT, RESET |

- **8 status output signals** (buffered): `SINTA, SWO, SSTACK, SHLTA, SOUT, SMI, SINP, SMEMR`.
- **6 command/control output signals** (buffered): `SYNC, DBIN, WAIT, WR, HLDA, INTE`.
- **4 command/control input signals** (buffered): `READY, HOLD, INT, RESET`.

**Critical timing note:** `PRDY` and `PHOLD` are **synchronized to the leading
edge of the Φ2 clock**. This is required because a transition of either signal
during the second half of Φ2 will drive the processor into an undefined state.

### System Clock

- Standard TTL oscillator (IC P) with a **2.000 MHz crystal** as the feedback element.
- Correct pulse widths and phase separation for the two phases come from dual
  single-shots (IC Q) and a delay circuit (R43, C6).
- The 8080 requires a **12-volt swing on the clock**, produced by a 7406 driver (IC N).
- TTL clock levels go to the system bus via 8T97 drivers (2 gates of IC I).
- The `CLOC` (bus `CLOCK`, active-low) signal goes to the bus through one gate of 8T97 IC G.
- Bus `CLOCK` is the **inverted output of the 2 MHz oscillator** that generates the 2-phase clock.

### Gating Logic

External gating on the CPU board is IC O (3 gates) and IC R (1 gate). Define the
output on IC O pin 13 as **G1 ENB** (Data Input Enable):

```
G1 ENB = (DBIN + HLDA) • (RUN + SS)*
```

Define **G1 DSB = NOT(G1 ENB)**. Then IC R pin 8 (the disable input for the input
data drivers) is:

```
DI DSB = G1 DSB + SSW DSB
```

- **G1 DSB** is processor-generated. When the 8080 is ready for input data it
  allows G1 DSB to go low, enabling the input data drivers.
- **SSW DSB** (`SSW DSBL`) is generated on the Display/Control board. It disables
  the input data drivers when an input from the **sense switches** (device address
  `377` octal = decimal 255) takes place. This is necessary because the sense
  switch inputs are tied directly to the bidirectional data bus at the processor.

### System Status Latch

The system status latch is **IC K, an 8212**. At the start of each machine cycle
the processor places the system status on the bidirectional data bus. When
**SYNC and Φ1 are coincident**, this data is latched by IC K and remains latched
for the remainder of the machine cycle.

The 8 latched status bits are the status output signals listed above:
`SINTA, SWO, SSTACK, SHLTA, SOUT, SMI, SINP, SMEMR` (see bus definition for each
bit's meaning and bus pin).

---

## Display / Control Board (Front Panel)

Provides RUN/STOP and single-step control, plus examine/modify of any memory
location via the front-panel switches. Schematics: 880-104, 880-105, 880-106.

The primary function is controlling the **ready line (PRDY)** — or a combination
of PRDY and the bidirectional data bus — to perform the panel functions. PRDY
control is exercised at IC O pin 8:

```
PRDY = RUN + SS + EXM + EXM NXT*
```

For the ready line to be released, one of these inputs to IC O must go high. The
circuitry ensures only one of these signals is high at any given time.

### RUN/STOP
An R-S flip-flop plus gating. The RUN/STOP flip-flop controls PRDY through its Q
output. A STOP occurs when **DO5, Φ2, and PSYNC are true** and the STOP switch is
depressed.

### SS (Single Step)
A dual single-shot (IC M) for debounce and the **SGL STP flip-flop** (R-S). When
stopped, depressing SS sets the SGL STP flip-flop (the machine must be stopped for
any panel switch except RESET to be active). This lets PRDY go high; the machine
executes one machine cycle, and PSYNC on the next cycle resets the SGL STP
flip-flop, pulling PRDY low and stopping the machine.

### EXM (Examine)
Dual single-shot (IC L) debounce, a 2-bit counter (IC J), the top 3 sets of 7405s
on 880-106 (ICs A, B, C and 2 gates of D), plus gating. When Examine is depressed,
the counter (IC J) starts:

1. **First count:** a jump instruction (`JMP` = octal `303`) is strobed directly
   onto the bidirectional data bus at the processor. Open-collector gates pull down
   data lines **D2, D3, D4, D5**, putting `303` on the bus (the JMP opcode).
2. **Second count:** switch settings **SA0–SA7** are strobed onto the data bus —
   the first (low) byte of the JMP address.
3. **Third count:** switch settings **SA8–SA15** are strobed onto the bus — the
   high byte of the JMP address. The processor executes `JMP` to the address set
   on SA0–SA15, so the contents of that location can be examined.
4. **Fourth count:** resets the counter and pulls the `EXM` line low, which pulls
   PRDY low and stops the processor.

### EXM NXT (Examine Next)
Same as Examine, except a **NOP is strobed** onto the data lines (via 4 gates of
IC D and 4 gates of IC E). This steps the program counter.

### DEP (Deposit)
Places a write pulse on the `MWRITE` line and enables switches **SA0–SA7**. The
contents of those 8 switches are stored in the currently addressed memory location.

### DEP NXT (Deposit Next)
Sequential operation of EXM NXT followed by DEP.

---

## 1K Static Memory Board

Built around the **Intel 8101 (256×4)** static RAM. Two 8101s make 256 8-bit
bytes. Minimum config = two 8101s (256 bytes); expandable in 256-byte increments
(pairs of 8101s) up to 1024 bytes. Schematics: 880-107, 880-108.

Four circuit units: Address Decoding, Processor Slow Down, Memory Protect, and
Buffers/Buffer Disabling Gating.

### Address Decoding
Lower-left of 880-107. **Address bits A10–A15** select a particular 1K block using
IC A and IC B. Patching IC B inputs to the "1" or "0" address input for each of
A10–A15 lets a board be assigned any 1K block from 0 to 63. **Address bits A9 and
A8** select a particular 256-byte page within the 1K (gating IC D, IC F, 2 gates
of IC C, 4 gates of IC E form a 2-to-4 line decoder).

### Processor Slow Down Circuit
The 8101 RAMs require **850 ns** for stable data on a read output, so **2 wait
cycles (~1 µs)** must be inserted when the processor reads from memory. Done via IC
K, 2 gates of IC N, 1 gate of IC C. IC K pin 8 goes low for ~2 clock cycles
starting with **PSYNC**. If the 1K card is addressed and the processor is in a
memory read cycle, two drivers of IC H are enabled, transmitting the low from IC K
pin 8 to **PRDY** on the bus, making the processor wait ~1 µs for data to stabilize.

### Memory Protect Circuit
An R-S flip-flop (IC L) set/reset by the `PROT` and `UNPROT` bus lines when the
card is addressed (`CE` true). When set, pin 11 of IC N is disabled and `MWRITE`
pulses from the bus cannot reach the 8101s. A status signal, `PS` (Protect Status),
is returned to the front panel display via the bus to indicate the protect
flip-flop is set.

### Buffers
Output drivers are 8T97 tri-state (ICs J & H). Enable/disable gating uses IC G and
1 gate of IC C:

```
NOT(G2) = SINP + SOUT + NOT(CE)      (OR)
    G2  = NOT(SINP) • NOT(SOUT) • CE
                                     (AND)
NOT(G1) = NOT(SMEMR) + NOT(CE)       (OR)
    G1  = SMEMR • CE
```

---

## Power Supply

Provides unregulated **+8 (×2)** and **±16 V** to the bus and the D/C board. All
voltages are unregulated until they reach the cards; each card regulates its own.
Schematic 880-109.

| Transformer | Output | Rating | Destination |
|-------------|--------|--------|-------------|
| T1 | +8 V unreg | 8 A | system bus (all boards except D/C use this for regulated +5) |
| T2 | +8 V unreg | 1 A | display/control board |
| T2 | +16 V unreg | 0.8 A | system bus |
| T3 | −16 V | 0.3 A | system bus |

All AC and DC voltages are wired to a terminal block for distribution.

---

## 8800 System Bus Structure (100 lines)

Drawing 880-110. The bus has **100 lines, 50 on each side** of the plug-in boards.

**General rules:**
- **Symbols:** a `P` prefix indicates a processor command/control signal; an `S`
  prefix indicates a processor status signal.
- **Loading:** all inputs to a card are loaded with a maximum of 1 TTL low-power load.
- **Levels:** all bus signals except the power supply are TTL.

Overbarred signal names in the source (active-low) are marked "(active-low)" below.

### Bus Pinout — Full Definition

| No. | Symbol | Name | Function |
|-----|--------|------|----------|
| 1 | +8V | +8 volts | Unregulated input to 5V regulators |
| 2 | +16V | +16 volts | Positive unregulated voltage |
| 3 | XRDY | External Ready | Special applications: pulling low forces the processor into a WAIT state and lets the status of the normal Ready line (PRDY) be examined |
| 4 | VI0 | Vectored Interrupt Line #0 | |
| 5 | VI1 | Vectored Interrupt Line #1 | |
| 6 | VI2 | Vectored Interrupt Line #2 | |
| 7 | VI3 | Vectored Interrupt Line #3 | |
| 8 | VI4 | Vectored Interrupt Line #4 | |
| 9 | VI5 | Vectored Interrupt Line #5 | |
| 10 | VI6 | Vectored Interrupt Line #6 | |
| 11 | VI7 | Vectored Interrupt Line #7 | |
| 12–17 | — | TO BE DEFINED | |
| 18 | STA DSB (active-low) | Status Disable | Tri-states the buffers for the 8 status lines |
| 19 | C/C DSB (active-low) | Command/Control Disable | Tri-states the buffers for the 6 output command/control lines |
| 20 | UNPROT | Unprotect | Input to the memory protect flip-flop on a given memory board |
| 21 | SS | Single Step | Indicates the machine is performing a single step |
| 22 | ADD DSB (active-low) | Address Disable | Tri-states the buffers for the 16 address lines |
| 23 | DO DSB (active-low) | Data Out Disable | Tri-states the buffers for the 8 data output lines |
| 24 | Φ2 | Phase 2 Clock | |
| 25 | Φ1 | Phase 1 Clock | |
| 26 | PHLDA | Hold Acknowledge | Processor C/C output in response to HOLD; indicates the data and address bus will go to the high-impedance state |
| 27 | PWAIT | Wait | Processor C/C output; the processor is in a WAIT state |
| 28 | PINTE | Interrupt Enable | Processor C/C output; interrupts enabled. Reflects the CPU's internal interrupt flip-flop; set/reset by EI/DI; when reset it inhibits interrupts from being accepted |
| 29 | A5 | Address Line #5 | |
| 30 | A4 | Address Line #4 | |
| 31 | A3 | Address Line #3 | |
| 32 | A15 | Address Line #15 | |
| 33 | A12 | Address Line #12 | |
| 34 | A9 | Address Line #9 | |
| 35 | DO1 | Data Out Line #1 | |
| 36 | DO0 | Data Out Line #0 | |
| 37 | A10 | Address Line #10 | |
| 38 | DO4 | Data Out Line #4 | |
| 39 | DO5 | Data Out Line #5 | |
| 40 | DO6 | Data Out Line #6 | |
| 41 | DI2 | Data In Line #2 | |
| 42 | DI3 | Data In Line #3 | |
| 43 | DI7 | Data In Line #7 | |
| 44 | SM1 | M1 | Status: processor is in the fetch cycle for the first byte of an instruction |
| 45 | SOUT | OUT | Status: address bus holds an output device address; data bus will contain output data when PWR is active |
| 46 | SINP | INP | Status: address bus holds an input device address; input data should be placed on the data bus when PDBIN is active |
| 47 | SMEMR | MEMR | Status: the data bus will be used for memory read data |
| 48 | SHLTA | HLTA | Status: acknowledges a HALT instruction |
| 49 | CLOCK (active-low) | Clock | Inverted output of the 2 MHz oscillator that generates the 2-phase clock |
| 50 | GND | Ground | |
| 51 | +8V | +8 volts | Unregulated input to 5V regulators |
| 52 | −16V | −16 volts | Negative unregulated voltage |
| 53 | SSW DSB (active-low) | Sense Switch Disable | Disables the data input buffers so sense-switch input can be strobed onto the bidirectional data bus right at the processor |
| 54 | EXT CLR (active-low) | External Clear | Clear signal for I/O devices (front-panel switch closure to ground) |
| 55–67 | — | TO BE DEFINED | |
| 68 | MWRT | Memory Write | Current data on the Data Out bus is to be written into the memory location on the address bus |
| 69 | PS (active-low) | Protect Status | Status of the memory protect flip-flop on the currently addressed memory board |
| 70 | PROT | Protect | Input to the memory protect flip-flop on the currently addressed memory board |
| 71 | RUN | Run | The RUN/STOP flip-flop is reset |
| 72 | PRDY | Ready | Processor C/C input controlling run state; pulling low puts the processor in a wait state until released |
| 73 | PINT (active-low) | Interrupt Request | Processor recognizes the request at the end of the current instruction or while halted. Ignored if in HOLD or if the Interrupt Enable flip-flop is reset |
| 74 | PHOLD (active-low) | Hold | Processor C/C input requesting the HOLD state; lets an external device gain control of the address and data buses once the processor finishes using them for the current machine cycle |
| 75 | PRESET (active-low) | Reset | Processor C/C input; while active the program counter is cleared and the instruction register is set to 0 |
| 76 | PSYNC | Sync | Processor C/C output; indicates the beginning of each machine cycle |
| 77 | PWR (active-low) | Write | Processor C/C output for memory write or I/O output control; data on the data bus is stable while PWR is active |
| 78 | PDBIN | Data Bus In | Processor C/C output; indicates to external circuits that the data bus is in input mode |
| 79 | A0 | Address Line #0 | |
| 80 | A1 | Address Line #1 | |
| 81 | A2 | Address Line #2 | |
| 82 | A6 | Address Line #6 | |
| 83 | A7 | Address Line #7 | |
| 84 | A8 | Address Line #8 | |
| 85 | A13 | Address Line #13 | |
| 86 | A14 | Address Line #14 | |
| 87 | A11 | Address Line #11 | |
| 88 | DO2 | Data Out Line #2 | |
| 89 | DO3 | Data Out Line #3 | |
| 90 | DO7 | Data Out Line #7 | |
| 91 | DI4 | Data In Line #4 | |
| 92 | DI5 | Data In Line #5 | |
| 93 | DI6 | Data In Line #6 | |
| 94 | DI1 | Data In Line #1 | |
| 95 | DI0 | Data In Line #0 | |
| 96 | SINTA | INTA | Status: acknowledge signal for INTERRUPT request |
| 97 | SWO (active-low) | WO | Status: the operation in the current machine cycle will be a WRITE memory or output function |
| 98 | SSTACK | STACK | Status: the address bus holds the pushdown stack address from the Stack Pointer |
| 99 | POC (active-low) | Power-On Clear | Provides a low pulse during power-up (used by the I/O card, per handwritten note) |
| 100 | GND | Ground | |

### Physical connector layout (880-110)

Two 50-pin edge-connector rows: **TOP OF BOARD = pins 1–50** (numbered), **BOTTOM
OF BOARD = pins 51–100** (lettered A–ZZ in the drawing). Board is 10.0 in tall,
5.0 in of usable width; the connector fingers span the 6.375 in center region
(1.5 in above, 2.125 in below).

---

## Status Byte (8080 machine-cycle status)

At the start of each machine cycle the 8080 emits a status byte on the data bus,
latched by the 8212 (IC K) when SYNC and Φ1 coincide. The eight status signals map
to the data-out lines as follows (standard 8080 assignment; these are the signals
the CPU board buffers to the bus):

| Status signal | Data bus bit | Bus pin | Meaning |
|---------------|-------------|---------|---------|
| SINTA | D0 | 96 | Interrupt acknowledge |
| SWO (active-low) | D1 | 97 | Write/Output cycle (low = write operation) |
| SSTACK | D2 | 98 | Stack address on the address bus |
| SHLTA | D3 | 48 | Halt acknowledge |
| SOUT | D4 | 45 | Output device address on the address bus |
| SMI | D5 | 44 | M1 — first byte fetch of an instruction |
| SINP | D6 | 46 | Input device address on the address bus |
| SMEMR | D7 | 47 | Memory read data on the data bus |

> Note: the original bus definition labels the M1 status line `SM1` (pin 44); the
> CPU-board text names the same signal `SMI`. They are the same status bit (D5).

---

## Machine Cycle / Timing / Control Logic (summary for emulation)

- **Clock:** 2 MHz crystal, two non-overlapping phases Φ1 (pin 25) and Φ2 (pin
  24). Bus `CLOCK` (pin 49, active-low) is the inverted oscillator output.
- **SYNC (PSYNC, pin 76):** marks the beginning of every machine cycle. Status
  byte is valid and latched at SYNC•Φ1.
- **DBIN (PDBIN, pin 78):** high during the input (read) portion of a cycle;
  external circuits drive the data-in bus while PDBIN is active.
- **WRITE (PWR, pin 77, active-low):** active during memory-write / I/O-output;
  data-out is stable while PWR is active. `MWRT` (pin 68) qualifies a memory write.
- **WAIT / READY:** pulling **PRDY (pin 72)** low forces a WAIT state until
  released. **XRDY (pin 3)** is an independent external ready line that also forces
  WAIT and lets PRDY be examined. **PWAIT (pin 27)** is the CPU's acknowledgment
  that it is in a WAIT state. The 1K memory board asserts PRDY low for ~2 clocks
  (~1 µs) on every read to compensate for 8101 access time.
- **HOLD / DMA:** **PHOLD (pin 74, active-low)** requests the processor release the
  address/data buses; **PHLDA (pin 26)** acknowledges and signals the buses are
  going to high impedance. PHOLD is synchronized to the Φ2 leading edge on the CPU
  board (a transition during the second half of Φ2 causes undefined behavior).
- **RESET:** **PRESET (pin 75, active-low)** clears the program counter and sets
  the instruction register to 0 while active. **POC (pin 99, active-low)**
  provides a power-up clear pulse. **EXT CLR (pin 54, active-low)** clears I/O
  devices (front-panel switch to ground).
- **Interrupts:** 8 vectored interrupt lines **VI0–VI7 (pins 4–11)**. The
  interrupt request line **PINT (pin 73, active-low)** is recognized at the end of
  the current instruction or while halted; ignored if in HOLD or if the internal
  interrupt-enable flip-flop is reset. **PINTE (pin 28)** reflects that
  enable flip-flop (set/reset by EI/DI). **SINTA (pin 96)** acknowledges the
  interrupt (INTA machine cycle).
- **Tri-state / bus-mastering controls:** `ADD DSB` (22), `DO DSB` (23), `STA DSB`
  (18), `C/C DSB` (19) tri-state the address, data-out, status, and command/control
  buffers respectively — used when another card takes the bus.
- **Sense switches:** device address `377` octal (255 decimal). `SSW DSB` (pin 53)
  disables the data-input buffers so the sense switches drive the bidirectional
  data bus directly at the processor.
- **Memory protect:** `PROT` (70) sets, `UNPROT` (20) resets the per-board protect
  flip-flop; `PS` (69) reports its state. When set, MWRITE is blocked on that board.
