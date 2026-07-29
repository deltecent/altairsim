# Processor Technology 3P+S

Source: [3P+S Input/Output Module — Assembly and Operating Instructions.pdf](#)
(© Processor Technology Corporation 1976)

The **3P+S** ("3 Ports + Serial") is an S-100 board combining **one full-duplex
serial port** built around a general-purpose UART with **two independent 8-bit
parallel ports**. All three channels, plus a fourth "control/status" channel
that ties them together, share one 4-address I/O block that the builder places
anywhere in the 8800's 256-address I/O space with wire jumpers — there is no
fixed factory address and, unusually for a card of this era, **no fixed
status-word bit layout either**: the board ships as a field of unconnected
solder pads, and the builder wire-jumpers UART flags, parallel-port flags, and
EIA modem-control lines onto whichever status bits the driving software expects.
This reflects Processor Technology's kit-building philosophy of the mid-1970s;
practically every "register" on this board is a jumper fabric, not a fixed
silicon layout.

This is a distilled emulation reference: it keeps the programmer-visible model
— the 4-channel port map and its address-jumper arithmetic, the UART
(register/condition/status model, baud-rate generator), the two parallel ports
and their handshake, and the (jumper-optional, off-board) interrupt story. The
assembly instructions, parts list, component-placement figures, and the
device-specific wiring recipes in Section IV (RS-232-C modem, ASR33/KSR33
Teletype, Teleprinter Models 15/28, CT1024 TV Typewriter) are summarized only
where they confirm a signal's function or polarity; the full recipes are
omitted as installation detail, not emulation model.

**Source completeness.** An earlier pass at this reference worked from a
truncated scan (a linearized PDF whose declared length exceeded the bytes
actually present), covering only Sections I–III and Section IV through the
RS-232-C/ASR33/KSR33 wiring recipes (through p. IV-3). The complete 75-page
manual has since been obtained, including the rest of Section IV (Model
15/28 Teleprinter and CT1024 TV Typewriter wiring recipes), Section V
(drawings — block diagram and full schematic), and all five appendices —
notably **Appendix V, "3P+S Port Test Programs,"** whose assembled listings
confirm the UART status-flag polarity and give a concrete example bit
assignment. Every gap the truncated pass had flagged as *not recovered* has
now been checked against the complete text; most are resolved below, and the
few that remain are called out as genuinely **not in the manual** (the
manual is silent, not the scan).

---

## 1. Port map

The board occupies **one group of 4 consecutive I/O addresses**, jumper-set to
any even-aligned group of 4 in the 8800's 256-address space (64 possible
groups; p. III-1/III-2). Address bits **A0, A1** (the low 2 bits within the
group) select one of four "channels"; a second jumper field swaps which pair of
channels sits at the low two addresses:

| A1 | A0 | Channel (jumper: left→center) | Channel (jumper: left→right) |
|:--:|:--:|---|---|
| 0 | 0 | A | C |
| 1 | 0 | B | D |
| 0 | 1 | C | A |
| 1 | 1 | D | B |

(Figure 3-1, p. III-1.) Note the order is **not** a straight binary count of
A1A0 — it is literally A0,A1 read as a 2-bit value in that bit order (0→first,
2→second, 1→third, 3→fourth channel). The jumper only decides **which pair**
(A,B — the two parallel ports — or C,D — the UART/control pair) occupies the
group's low two addresses; the internal A0/A1→channel mapping above does not
change.

| Channel | IN (read) | OUT (write) |
|---|---|---|
| **A** | Parallel input port A (8 bits) | Parallel output port A (8 bits) |
| **B** | Parallel input port B (8 bits) | Parallel output port B (8 bits) |
| **C** | Jumper-built status byte (§2) | Jumper-built control byte (§2) |
| **D** | UART received data | UART transmit data |

**Base address.** Area A provides 6 jumper pads (A2–A7) that feed one side of
a 6-bit magnitude comparator (IC 24, a DM8131); each pad is wired to **+V**
(logic 1) or **Ground** (logic 0, labeled **V**/**G** in the manual) to set the
reference the incoming address must match (Figure 3-2, table p. III-1/III-2/
III-3; addresses 374–377 octal are reserved for the front-panel switches and
must not be used). Area B (a 74155 dual demux + 74400) supplies the
channel-swap jumper above (Figure 3-1, p. III-1); Area C supplies the UART's
Control Register Load strobe jumper (§2).

---

## 2. UART (serial channel) — chip and register model

**Chip:** AMI **S1883** UART (alternate: **TMS6011NC**) — a general-purpose,
pin-compatible member of the same industry-standard family as the AY-5-1013 /
COM2502/COM2017 used on the 88-SIO (see `com2502.md`): double-buffered
transmit and receive, framing set by **condition-input pins** latched through
a control-strobe pin rather than a memory-mapped control register, status
outputs enabled onto the bus by a status-word-enable pin. Nothing in the 3P+S
manual contradicts that chip's documented behavior; the 3P+S wraps it in a
jumper fabric rather than hard wiring it.

### 2.1 Data path (Channel D)

`IN` at Channel D returns the UART's received byte; `OUT` at Channel D loads
the UART's transmit buffer (p. III-9). Separately (not part of the D-channel
register, but part of "Channel D" in the manual's own section numbering), the
UART's single serial pins are jumpered to the outside world:

- **Receive (3.1.4, p. III-9):** the UART's serial input (**R IN**, in Area G)
  may be jumpered from any of 4 EIA-level receiver inputs, or from the TTY
  20 mA current-loop receiver (jumper the pad right of Q5's collector to the
  pad right of Q4).
- **Transmit (3.1.5, p. III-9):** the UART's serial output (bottom row, Area
  J) may be jumpered to any of the 4 EIA-level driver inputs (top row, Area
  J). Area D (Figure 3-5, p. III-9) either floats the current-loop output
  (not used) or enables it (current-loop transmitter used).

### 2.2 Condition (framing) inputs — Channel C output, bits 4–7

Channel C's output byte splits: bits 0–3 go to the baud generator/peripheral
driver/EIA outputs (§2.3); **bits 4–7 are jumpered (Area H) to the UART's
condition inputs**, which the manual states are **all active-high** (p. III-6):

| Channel C out bit | Area H row | Condition input | Function |
|:--:|---|---|---|
| 4 | bottom | **PI** — Parity Inhibit | 1 = no parity bit transmitted/checked |
| 5 | 5th | **WLS2** | with WLS1, selects data bits/character (below) |
| 6 | 4th | **WLS1** | with WLS2, selects data bits/character |
| 7 | 3rd | **EPE** — Even Parity Enable | 1 = even parity (if PI = 0) |

| WLS1 | WLS2 | Bits/character |
|:--:|:--:|:--:|
| H | H | 8 |
| L | H | 7 |
| H | L | 6 |
| L | L | 5 |

**SBS** (Stop Bit Select) is a fifth condition input, at the row 2nd from left
in Area H: 0 = one stop bit, 1 = two stop bits (1.5 if a 5-bit word length is
also selected). It is not one of the 8 Channel-C output bits — the manual
shows only 4 of the 5 condition rows (PI, WLS2, WLS1, EPE) mapped to C bits
4–7, and shows SBS as the 2nd-from-left terminal in Area H without stating a
Channel-C bit for it. **This is confirmed absent, not merely unrecovered:**
the complete manual's Section V schematic (block diagram and full schematic,
pp. V-1 to V-4) and Appendix IV's chip-pinout sheets show Area H's jumper
field and the S1883's condition-input pins, but neither the schematic nor any
narrative text ties SBS's terminal to a specific Channel-C output bit the way
PI/WLS2/WLS1/EPE are tied to bits 4–7. Emulate SBS as jumper-configurable
alongside the other four condition inputs (most builds likely tie it directly
to +V for a fixed 1- or 2-stop-bit format, per the note below), but do not
assume it occupies a particular bit position — **the exact Channel-C bit, if
any wiring uses one, is not documented anywhere in the manual.**

All five condition inputs may instead be tied directly "high" (floating, per
Area H's own note) for static framing, or tied "low" to the Area H ground row
— the standard ASR33 wiring at 110 baud uses all five held high and the CRL
strobe tied to +5 V (below), i.e. framing never changes at runtime (p. III-5).

**Loading the condition inputs (CRL).** The UART's Control Register Load pin
(pin 34) is brought to the right-hand terminal of Area C. Jumpered to the
left-hand terminal, CRL goes active every time Channel C is written — so the
condition bits (4–7) must hold *the same value* on every Channel-C output
write, or the framing will be silently rewritten each time the status/control
byte changes for an unrelated reason. Jumpered instead to Area C's center
terminal, CRL is tied high permanently ("hardwired" framing) (p. III-9).

### 2.3 Status flags — Channel C input (fully jumper-assigned)

Channel C's input byte has **no fixed bit assignment** — each of its 8 bits is
individually jumperable to any one of the following sources (p. III-4):

| Source | Meaning |
|---|---|
| **PE** | UART parity error |
| **FE** | UART framing error |
| **OE** | UART overrun error |
| **RDA** | UART receiver data available |
| **TBE** | UART transmitter buffer empty |
| **FA** | latch set by XDAA — external data available, parallel port A |
| **FB** | latch set by XDAB — external data available, parallel port B |
| **XA** | external-device-ready input, parallel port A (from XDRA) |
| **XB** | external-device-ready input, parallel port B (from XDRB) |
| **EIA A/B/C/D** | any of up to 4 EIA-level modem status inputs (e.g. carrier detect) |

**TBE and RDA polarity is confirmed by the port-test-program code itself**
(Appendix V, "3P+S Port Test Programs," Serial I/O Test, pp. AV-6–AV-8),
exactly as `com2502.md`/`Cromemco TU-ART.md` confirm their UART's polarity
from `AND 80H`/`AND 40H`. Serial Test 3's assembled listing jumpers TBE to
Channel-C input bit 7 and RDA to bit 6 (Step 1, p. AV-2: "jumper TBE
(transmitter buffer empty) and RDA (receiver data available) flags to D7 and
D6 respectively") and its equates confirm the masks: `TBE EQU 80H`, `RDA EQU
40H` (p. AV-8). The polled routines spin exactly like the TU-ART's:

```
TBET:  IN   STATUS      ; RST 1 — wait for transmitter ready
       ANI  TBE         ; AND with 80H
       RNZ               ; return (exit the wait) once the bit is 1
       JMP  TBET
...
CKIN:  IN   STATUS      ; RST 4 — wait for a received character
       ANI  RDA         ; AND with 40H
       RNZ               ; return once the bit is 1
       JMP  CKIN
```

Both loop *while the bit reads 0* and exit *once it reads 1* — TBE=1 means
"transmitter ready," RDA=1 means "character available." That is
**active-high, confirmed**, not inferred.

**PE/FE/OE are not exercised by AND-masked test code** the way TBE/RDA are —
the port-test program only echoes the raw status/data bytes to the front
panel and to the peripheral, it doesn't branch on PE/FE/OE. Their sense is
still given in Appendix V's own descriptive text (Step 3, p. AV-2): "OE
indicates the UART received the character, but before you could unload it...
another character started to write into the UART buffer"; "FE indicates the
UART did not see an expected stop bit..."; "PE indicates the UART detected a
parity error..." — each phrased as the flag *indicating* the fault, i.e. true
(1) when the fault occurred, matching active-high. The full schematic (p.
V-1/V-2) shows the same thing structurally: the "UART FLAGS" box takes PE,
FE, OE, RDA straight off the S1883's receiver-section pins into the
Channel-C input multiplexers (74LS153, IC11–14) with no inverter in the
path. So: **TBE and RDA are code-confirmed active-high; PE/FE/OE are
corroborated active-high by the manual's own wording and by the schematic's
uninverted signal path, but not literally bit-tested in the recovered
programs.**

Without interrupts, software polls this status byte continuously; with the
optional vectored-interrupt hookup, the byte is examined only after the
interrupt system has already flagged a change (p. III-4). See §4.

**A concrete, as-tested example wiring (Appendix V).** Because Channel C's
bit assignment is entirely up to the builder (§2.3), the factory's own
"3P+S Port Test Programs" (Appendix V) had to pick one to demonstrate the
board, and its listing is the closest thing to a documented default:

| Channel C input bit | Flag | Source |
|:--:|---|---|
| 7 | TBE | UART transmitter buffer empty |
| 6 | RDA | UART receiver data available |

(Serial I/O Test Preparation, Step 1, p. AV-2; equates `TBE EQU 80H`, `RDA EQU
40H`, p. AV-8.) This is presented explicitly as a **test-time convenience,
not a factory default** — Step 1 is the first thing the technician jumpers
*before* installing the board for the test, and nothing elsewhere in the
manual calls it out as the shipped configuration. The parallel-port test
programs (Appendix V.1) similarly assume, but do not mandate, one concrete
layout: `PRTA EQU 6` / `PRTB EQU 7` (octal), with a base of 0 and the
channel-swap jumper (Area B) set "left to right" so the parallel ports A/B
occupy the block's two higher addresses and Channel C/D the two lower — the
same swap already described in §1, now shown exercised in running code.

---

## 3. Baud-rate generator

A 12-bit programmable-modulus divider (IC 7–9, 93L16/74LS163 4-bit counters,
Area E) divides the board's 2 MHz φ2 clock by a preset value reloaded at every
counter overflow; the divider's output feeds the UART's 16× receive/transmit
clock (p. III-7). Selected rates and their 12-bit preset (binary, LSB→MSB
across the three 4-bit words):

| Baud | Preset (decimal) | Baud | Preset (decimal) |
|--:|--:|--:|--:|
| 35 | 514 | 300 | 3668 |
| 45.5 | 1341 | 600 | 3877 |
| 50 | 1585 | 1200 | 3981 |
| 75 | 2418 | 2400 | 4033 |
| 110 | 2949 | 4800 | 4059 |
| 150 | 3252 | 9600 | 4072 |

(Full table 35–9600 baud, p. III-7/III-8.) A **1** is wired by jumpering that
counter input to the +V row (Area E, 3rd row from top); a **0** to the Ground
row (2nd row). For a fixed baud rate, wire the preset directly. For
software-selectable baud (two rates only, via this mechanism): jumper bit
positions common to both target rates directly to V/G, jumper positions that
differ to spare rows in Area E, then jumper those spare rows so that **Channel
C output bit 2 = 1, bit 3 = 0 selects the top (first) rate** and **bit 2 = 0,
bit 3 = 1 selects the bottom (second) rate** (procedure and truth table, p.
III-8/III-9). More than two software-selectable rates requires extra parts not
covered by this base design (factory engineering bulletin referenced but not
included, p. III-8).

Channel C output bit assignments recorded by the manual (p. III-4/III-7,
III-9): bit 0 → EIA-output row 2 / Area F left terminal (peripheral control
driver); bit 1 → EIA-output row 4 / Area F right terminal; bit 2 → baud-select
terminal G (Area E); bit 3 → baud-select terminal H (Area E) / EIA-output row
3; bits 4–7 → UART condition inputs (§2.2). Bit 2 is **not** available at Area
J (the EIA-output jumper field) — only bits 0, 1, and 3 are.

---

## 4. Parallel ports (Channels A and B)

Two independent, byte-wide, general-purpose parallel ports, each built from a
pair of 74177 latches (input latch + output latch) — a fixed-direction design
like the MITS 88-PIO, not a bidirectional PIA. Connector pinout (J1/J2,
Figure 4-1, p. IV-3):

| Signal | Direction (at 3P+S) | Polarity | Function |
|---|---|---|---|
| **Parallel input port A/B, bits 0–7** | in | — | 8-bit input latch outputs |
| **XDAA / XDAB** | in | active-low (external strobe) | "data available" strobe from the external device; sets flag **FA/FB** |
| **AKA / AKB** | out | active-high | acknowledge — asserted while the FA/FB latch is set, until the CPU reads that port (p. III-10) |
| **Parallel output port A/B, bits 0–7** | out | — | 8-bit output latch inputs |
| **Output strobe, parallel port A/B** | out | — | pulses when the CPU writes that port |
| **XDRA / XDRB** | in | — | "external device ready" input; polled by the CPU as status flag **XA/XB** through Channel C (§2.3), not latched |

**FA/FB require an external data latch.** Because the flag is a simple
flip-flop (IC 15) clocked by XDAA/XDAB, the external device's data must remain
stable on the port-A/B input lines from the moment the flag sets until the CPU
has read it — the manual explicitly calls out that this needs an external
latch on the peripheral side, with AKA/AKB usable as that latch's clock (p.
III-10). 2.2 kΩ pull-ups (R19/R20) are provided for XDAA/XDAB in case the
external device has none; they return to the board's own +5 V, not an
external supply (p. III-10).

---

## 5. Interrupts (brief)

The 3P+S has **no interrupt controller of its own** — no vector logic, no
priority encoder, unlike the Cromemco TU-ART or the 88-VI-adjacent boards.
Interfacing to the 8800's vectored-interrupt bus is described only as a
jumper-selectable *option* requiring a separate 88-VI Vectored Interrupt
board; any of the UART error/handshake flags that can be jumpered into the
Channel-C status byte (§2.3) can equally be jumpered to an interrupt request
line instead (p. I-1).

**Confirmed by the complete manual's block diagram and schematic (Section V,
pp. V-1/V-2), not merely absent from the recovered pages:** the board's only
connection to the 8800's 8-bit Vectored Interrupt bus (VI0–VI7) is a bank of
non-inverting bus receivers (7406, IC7/IC8) that simply brings those 8 lines
onto the board — the same passive-buffering treatment given every other bus
signal (address, data, PWR/SINP/PDBIN). There is no gate, encoder, or latch
that turns a UART/parallel-port flag into a request on that bus, and no
single-level (RST) generation logic anywhere in the schematic. Section IV,
Section V, and all five appendices — including Appendix V's test
programs, which drive the board entirely by polling Channel C — contain no
interrupt-vector pinout, priority scheme, or RST wiring recipe. This is now
a confirmed feature of the design, not a scan gap: **the 3P+S genuinely has
no on-board interrupt logic of any kind**, and any interrupt use is entirely
the builder's own off-board wiring to an 88-VI (undocumented here because
it belongs to that board's manual, not this one).

---

## Emulation checklist

- **4 channels (A, B, C, D) at one jumper-set base**, selected by A0/A1 in the
  order 00→1st, 10→2nd, 01→3rd, 11→4th channel of whichever pair (A,B or C,D)
  the "channel swap" jumper puts first. Base is any of 64 four-address groups
  in 0–255 (not 252–255, reserved for the front panel).
- **Channel A, B = two independent, fixed-direction parallel ports.** Input
  strobe **XDAA/XDAB active-low**, sets flag FA/FB; **AKA/AKB acknowledge
  active-high**, clears when the CPU reads that port. Output has its own
  strobe pulse; **XDRA/XDRB** (external-ready) is polled, not latched, surfaced
  as flags XA/XB via Channel C.
- **Channel C = the whole board's status/control channel, fully
  user-jumpered — model it as configurable, not fixed.** Input byte: any
  combination of PE/FE/OE/RDA/TBE (UART), FA/FB/XA/XB (parallel ports), or up
  to 4 EIA modem-status inputs, one per bit, jumper-chosen by the builder.
  Output byte: bits 4–7 are UART condition inputs (word length, stop bits,
  parity — see §2.2 tables), bits 0–3 drive the peripheral driver, baud-rate
  override select, and EIA control outputs.
  **TBE and RDA are confirmed active-high** by Appendix V's port-test-program
  code (`ANI TBE`/`ANI RDA` against masks `80H`/`40H`, looping until the bit
  reads 1 — see §2.3); **PE/FE/OE are active-high too**, corroborated by the
  manual's own "flag indicates the fault" wording and by the schematic's
  uninverted UART→Channel-C signal path, though not literally bit-tested in
  the recovered code.
  **CRL jumper matters**: if wired to the Channel-C write strobe, condition
  bits 4–7 must be re-sent unchanged on every Channel-C write or framing
  silently changes.
  **SBS (stop-bit select)'s Channel-C bit is confirmed absent, not just
  unrecovered** — the complete schematic and appendices never tie its Area H
  terminal to a Channel-C output bit the way PI/WLS1/WLS2/EPE are tied to
  bits 4–7. Model it as configurable (most likely hardwired high/low by most
  builders) but do not assign it a bit position; the manual is silent, not
  the scan.
  **A concrete example wiring exists** (Appendix V, not a factory default):
  the port-test program jumpers TBE→bit 7, RDA→bit 6.
- **Channel D = the UART's data register** (IN = received byte, OUT =
  transmit byte); the UART's serial pins are separately jumpered (not through
  Channel D as a register) to one of 4 EIA driver/receiver pairs or the TTY
  20 mA current loop.
- **UART = AMI S1883 / TMS6011NC**, behaviorally the same family as the
  88-SIO's UART (`com2502.md`) — model status/condition semantics from that
  chip reference when this manual is silent.
- **Baud generator is a 12-bit preset divider ÷16 off 2 MHz**, hardwired or
  2-way software-selectable via Channel-C output bits 2/3 (bit2=1,bit3=0 →
  first rate; bit2=0,bit3=1 → second rate). Table of presets in §3.
  35–9600 baud representable.
- **No onboard interrupt controller — confirmed by the block diagram and full
  schematic**, not just absent from an earlier partial scan. The board's
  only touch on the 8800's Vectored Interrupt bus is a bank of passive,
  non-inverting bus receivers (7406) — the same treatment given every other
  backplane signal. Vectored interrupt use is entirely an off-board,
  jumper-optional add-on (88-VI); treat any UART/handshake flag as a
  candidate interrupt source with no fixed priority scheme, vector, or RST
  wiring documented anywhere in this manual.
- **Genuine gaps (checked against the complete 75-page manual, not just a
  truncated scan):** the exact Channel-C bit carrying SBS is never assigned
  in the schematic or text — it is not "missing pages," it simply is not
  specified. Nothing beyond the ASR33/RS-232-C/Model 15/Model 28/CT1024
  recipes documents further peripherals. No interrupt vector, priority
  scheme, or RST wiring for the 3P+S itself exists in the manual at all —
  that lives entirely in the 88-VI's own documentation.
