# Altair 8800 Front Panel Schematic (880-106, Computer Front Panel Control)

Source: [Altair 8800 front panel schematic.pdf](#)

This is the MITS drawing **880-106, "Computer Front Panel Control"** — a single
schematic sheet. It is the *control logic* board of the Altair 8800 front panel:
it debounces the momentary panel switches, sequences the RUN/STOP, single-STEP,
EXAMINE, EXAMINE NEXT, DEPOSIT and DEPOSIT NEXT operations, drives the address
(A0–A15) and data (D0–D7) LEDs, and gates the SENSE switches onto the data bus.
The physical switches and LEDs mount on the display/switch board; their circuit
nodes appear here as the switch pull-ups (R15–R19) and the LED drivers (74LS05).

> **Scope note:** This document covers everything legible on sheet 880-106. Some
> RC timing values on the 74123 one-shots are printed very small on the drawing;
> where a value is uncertain it is flagged. Bit numbers, port/address decode,
> bus pin numbers, and IC types are transcribed as shown.

---

## 1. Bus / edge-connector signals

Signals enter and leave through the S-100 (Altair bus) edge connector. Numbers in
boxes on the drawing are the bus **pin numbers**.

### Inputs (left / bottom edge)

| Signal   | Pin | Meaning / use on this board |
|----------|-----|-----------------------------|
| PWR (⏦)  | 1   | Power / unregulated input reference into the RUN/STOP gating |
| SOUT     | 5   | Status: OUTput cycle |
| SOS / DO0? (marked "SOS") | 39 | Status line into RUN/STOP gating |
| Φ2       | 24  | Processor phase-2 clock — clocks the sequencing flip-flops / one-shots |
| PSYNC    | 76  | Processor SYNC — start of each machine cycle; buffered in and used to time EXAMINE/DEPOSIT/STEP |
| POC      | 99  | Power-On Clear (active-low reset) |
| PDBIN    | 78  | Processor Data Bus IN — qualifies the SENSE-switch / data read |
| +5V      | —   | Logic supply |

### Outputs (right / bottom edge)

| Signal   | Pin | Meaning |
|----------|-----|---------|
| MWRITE   | 68  | Memory WRITE strobe generated for panel DEPOSIT (and normal writes) |
| RUN      | 71  | Machine is running (panel RUN/STOP state) |
| PRDY     | 72  | Processor READY — pulled to hold the CPU while stopped/stepping |
| SS       | 21  | Single-Step line |
| SSW DSB  | 53  | **Sense-SWitch DiSaBle** — gates the sense-switch byte onto the bidirectional data bus during an input from port FF |
| RESET    | 75  | System reset out to the bus (from panel RESET/CLR) |
| EXT CLR  | 54  | External clear |

---

## 2. Address and data LEDs

The 16 address LEDs (**SA0–SA15**) and 8 data LEDs (**D0–D7**) are driven by
**74LS05** hex *inverter* buffers (open-collector). Each LED is lit by pulling
its cathode low through the open-collector output; the LEDs are wired to the
buffer outputs and the anodes to +5V through the panel's series resistors.

- Data LEDs **D0–D7**: driven from one/two 74LS05 packages fed by the buffered
  data-bus lines.
- Address LEDs **SA0–SA15**: driven from a bank of 74LS05 packages (labeled with
  the SA0…SA15 signal names on the drawing), one inverter section per bit.

**Drawing note on the 74LS05 packages:** the sheet carries an explicit pin-out
caution — *"NOTE: on 74LS05, Vcc is pin 4 (not pin 14) and GND is pin 11 (not
pin 7)."* This is a non-standard supply pin-out for the parts as mounted; honor
it if you ever reproduce the board, but it has **no effect on emulation** (the
LEDs simply reflect bus state).

**Emulation takeaway:** address LEDs = current value on the address bus
(A0–A15); data LEDs = current value on the bidirectional data bus. They are a
passive display of bus state; the inversion in the 74LS05 is undone by the
active-low LED wiring, so the lamp pattern equals the logical bus value.

---

## 3. Sense switches (read via IN port 0FFH)

The 8 SENSE switches are read by the CPU with an **input instruction from
address FF** (i.e. `IN 0FFH`), which places FFH on the high address byte.

Decode chain shown on the sheet (center-right):
- The upper address byte **A8–A15** feeds a **7430 (8-input NAND)**. When all of
  A8–A15 are high (address = FFxx), the NAND asserts.
- This is qualified by **SINP** (status = INPut) and **PDBIN/DBIN** (data bus in)
  through **74L10 / 7420** gates.
- The result drives **SSW DSB (pin 53)** — the sense-switch-disable/enable — which
  gates the 8 sense-switch bits onto the data bus so the CPU reads them, while
  disabling other bus drivers.

So: the sense switches appear at **I/O port 0FFH**, read into the accumulator by
`IN FF`. The eight switch bits map to data bits **D0–D7**.

> On the Altair the same 8 toggles that are the high address switches (A8–A15)
> double as the SENSE switches; that is why the decode looks at A8–A15. In
> altairsim this is modeled as the `fp` card: **sense = SA8–SA15 at port FF**
> (see the front-panel memory note).

---

## 4. Panel switch conditioning and sequencing

All panel action switches are **momentary** (spring-return) and are debounced,
then fed to one-shots and flip-flops that produce the timed control strobes.

### 4.1 Switch debounce ("TYP 3S", 3 places)

Lower-left inset labeled **"TYP 3S — 3 places"**: a **cross-coupled 2-NAND latch
(SR flip-flop)** per switch. The switch throws select set/reset of the latch, so
contact bounce produces a single clean edge. This standard SPDT-momentary +
NAND-latch debounce is used for the paired throws
(RUN/STOP, DEPOSIT/DEPOSIT NEXT, EXAMINE/EXAMINE NEXT, etc.).

### 4.2 Switch pull-ups and inputs

| Ref | Value | Node |
|-----|-------|------|
| R15 | 1K | SS (single-step) switch pull-up |
| R16 | 1K | EXM (examine) switch pull-up |
| R17 | 1K | EXM NXT (examine next) switch pull-up |
| R18 | 1K | DEP (deposit) switch pull-up |
| R19 | 1K | DEP NXT (deposit next) switch pull-up |
| R13/R14 | ~1K | PROT/UNPROT switch pull-ups |
| R22 | — | RESET/CLR side, pull-up to +5V |
| R20/R21 | — | around the 7420 / RUN timing |

The five action switches (SS, EXM, EXM NXT, DEP, DEP NXT) sit on a common node
row (each with its 1K pull-up) and feed the corresponding one-shots.

### 4.3 One-shots (74123 dual retriggerable monostables)

Each operation uses one half of a **74123** to produce a fixed-width strobe. On
the drawing each half is lettered; the RC network sets the width.

| Function | Device (½ 74123) | Timing R | Timing C | Notes |
|----------|------------------|----------|----------|-------|
| STP SS (step single-shot)   | M | R3 ≈ 7.5K  | C3 ≈ 20 pF   | short step strobe |
| — (2nd half near STP)       | M | R4/R5 ≈ 47K | C            | |
| EXM SS (examine)            | L | R6 ≈ 7.5K  | C4 ≈ 20 pF   | |
| EXM NXT SS (examine next)   | K | R7 ≈ 30K, R8 ≈ 7.5K | C5 ≈ 0.1 µF, C6 ≈ 20 pF | longer settle then strobe |
| DEP SS (deposit)            | G | R9 ≈ 30K   | C7 ≈ 0.001 µF | |
| DEP (2nd half)              | G | R10 ≈ 30K  | C8 ≈ 0.01 µF  | |
| DEP NXT SS (deposit next)   | F | R11 ≈ 47K  | C9           | |
| DEP NXT (2nd half)          | F | R12 ≈ 7.5K | C10 ≈ 20 pF  | |

> The exact R/C digits are hard to read on the scan; the 7.5K / 47K / 30K
> resistor family and the 20 pF / 0.001 µF / 0.01 µF / 0.1 µF capacitor family
> are legible and consistent with the standard 880 pulse widths. Treat the
> pairing (which ref goes to which one-shot) as authoritative; treat individual
> digits as approximate. These widths are **not observable in emulation** — the
> sim only needs the *logical* effect (one write / one step per switch press).

### 4.4 Sequencing flip-flops

Built from cross-coupled gates / the second halves of the packages:

| Flip-flop | Purpose |
|-----------|---------|
| **RUN/STOP FF** | Holds machine RUN vs STOP; gated by SOUT/SOS/PWR and clocked by the RUN/STOP switch latch. Its RUN output = bus pin 71 and also releases/holds PRDY. |
| **SGL STP FF** (single-step FF) | Arms one machine cycle when RUN is off and SS is pressed; interacts with PSYNC/Φ2 to advance exactly one cycle, then re-stop. |
| **EXM NXT FF** (examine-next FF) | Latches the "advance address then examine" behavior for repeated EXAMINE NEXT. |

**Sequencing clocks:** **Φ2 (pin 24)** and **PSYNC (pin 76)** time these
flip-flops so that EXAMINE/DEPOSIT/STEP take effect on a real machine cycle
boundary. **PDBIN (pin 78)** qualifies reads; **MWRITE (pin 68)** is asserted for
the DEPOSIT write.

---

## 5. Reset / clear / protect

- **RESET (pin 75)** and **EXT CLR (pin 54)** are generated from the panel
  RESET/CLR switch (debounced), with **R22** pull-up and **POC (pin 99)** as the
  power-on-clear source. A `74LS05` inverter section buffers RESET out.
- **PROT (70) / UNPROT (20):** the memory-protect toggle drives PROT/UNPROT
  outputs (boxed pins 70 and 20 on the sheet) through inverters, with R13/R14
  pull-ups and an RC (0.1 µF) hold. These set/clear the memory-protect latch on
  protected RAM boards. (Not modeled unless a protected-memory card is present.)

---

## 6. Named ICs on the sheet

| Type | Function | Where used |
|------|----------|------------|
| **74123** (dual retriggerable one-shot) | STP SS, EXM SS, EXM NXT SS, DEP SS, DEP NXT SS pulse generators (letters F,G,K,L,M,N) | §4.3 |
| **7430** (8-input NAND) | Decode A8–A15 = FF for sense-switch read | §3 |
| **7420** (dual 4-input NAND) | RUN / status gating, sense-switch qualify | §3, §4 |
| **74L10 / 7410** (triple 3-input NAND) | RUN/STOP, DBIN, EXM NXT gating | §3, §4 |
| **74LS05 / 7405** (hex inverter, open-collector) | Address (SA0–SA15) and data (D0–D7) LED drivers; RESET buffer | §2, §5 |
| Cross-coupled NAND latches | Switch debounce (TYP 3S) and the SGL STP / RUN-STOP / EXM NXT flip-flops | §4.1, §4.4 |

Component letter labels (H, J, K, L, M, N, F, G, etc.) on the drawing identify
individual gate/one-shot packages; R1–R22 and C1–C10 are the discretes.

---

## 7. What matters for the `fp` emulation

1. **Sense switches → `IN 0FFH`.** Port FF returns the 8 sense-switch bits on
   D0–D7. In altairsim these are SA8–SA15. (`Machine::sense` is deleted; the
   panel is a card.)
2. **Address LEDs = A0–A15 bus; data LEDs = D0–D7 bus.** Passive display of
   current bus state — no separate latch to model.
3. **RUN / STOP, single STEP, EXAMINE, EXAMINE NEXT, DEPOSIT, DEPOSIT NEXT** are
   the operations. Their *logical* effect (halt/resume, load PC / read one byte,
   write one byte, auto-increment address) is what the sim implements; the 74123
   pulse widths and the Φ2/PSYNC timing are hardware-only.
4. **PRDY (pin 72)** is how the panel actually stops the CPU (holds it not-ready)
   — the model's "stopped" state corresponds to PRDY held low.
5. **MWRITE (pin 68)** is the DEPOSIT write strobe.
6. **SSW DSB (pin 53)** is the bus-arbitration signal that lets the sense
   switches drive the data bus during the `IN FF`; irrelevant unless you model
   bus contention.
