# CompuPro Interface II

Source: [Interfacer II User's Manual.pdf](#) (© Godbout Electronics 1981, rev. 1/81)

The **CompuPro (Godbout) Interfacer II** is an S-100 board carrying **three full-duplex
latched parallel I/O ports** and **one RS-232/current-loop serial channel**, built around a
single MOS LSI **1602/1863 UART** (U34) and a **1941** crystal-controlled baud-rate generator
(U20, 5.0688 MHz crystal). Both sections are independently addressable anywhere in the 256-port
I/O space via on-board comparators and DIP switches, and both are fully hardware/software
programmable: on power-up, quad `74LS175` latches (U35/U36 for the serial parameters, U23 for
the parallel interrupt enables) come up in a state set by a snap-in jumper shunt (J8/J9/J10),
and the CPU can flip any bit of that state later by writing a `1` to the corresponding control
port bit (a `0` returns it to the power-up setting).

This is a distilled emulation reference: it keeps the programmer-visible model — port map and
addressing, the UART's status/control bit layout and polarity, the parallel ports' data/status/
attention/enable handshake, the baud and timer-interrupt rate tables, and the RS-232 pin/
polarity mapping (master vs. slave). The discrete-TTL circuit description (I/O select logic,
bus-driver logic, level-conversion logic) and the board-bring-up test procedures are
summarized only where they confirm a bit's function or polarity. Jumper-selectable current-loop
vs. RS-232 physical wiring (J4–J6) is noted but not modeled — an emulator only needs the
logical serial channel behind it.

---

## 1. Chips and board model

- **Serial UART: 1602/1863** (U34) — a MOS LSI USART-class chip (WD/GI TR1602/1863 family).
  The manual treats it as a black box: "the UART performs the complete parallel to serial and
  serial to parallel conversion, error detection, and serial format modification" (p.3, p.18).
  No internal register map is given beyond the pins the board decodes (status, data, and the
  eight programming lines below).
- **Baud-rate generator: 1941** (U20) — one crystal (5.0688 MHz, X1) drives both the serial
  baud clock and (via the other half of the same chip) the parallel section's programmable
  timer-interrupt clock (p.18, p.19).
- **RS-232 line drivers/receivers:** 1488 (U12, EIA driver) and 1489 (U13, EIA receiver);
  **current-loop:** two MCT-2 opto-isolators (U14, U15) (p.18, Parts List p.22).
- **Parallel ports:** three full-duplex channels (J1/J2/J3), each an input latch
  (`74LS374`/`74LS373`, user-selectable — U2/U5/U9 shown as `374`) plus an output latch
  (`74LS374`, U3/U6/U10), **not** a MOS PIA — "the use of TTL latches rather than a MOS parallel
  interface chip negates the need for mode selection and initialization" (p.3). A fourth port
  position is the shared status/interrupt-control port.
- **Address decode:** two `25LS2521` octal comparators (U28 serial, U31 parallel) gated by the
  board's own I/O-detect logic (p.18–19).

---

## 2. Serial section — port map and addressing

The serial channel occupies **a single 2-port block anywhere in the 256-port I/O space**,
set by DIP switch **S3**:

| Switch position | Function |
|:---:|---|
| 1–7 | Address bits A1–A7 (`ON`=`0`, `OFF`=`1`) |
| 8 | Channel disable (`ON`=disabled, `OFF`=enabled) |

Data resides at the switch-set address (even); status resides at **address + 1** (odd)
(p.4). **No factory-default base is given in the manual** — the worked example addresses the
channel at `00H`/`01H` (all of S3 positions 1–7 `ON`, position 8 `OFF`).

---

## 3. Baud rate selection

DIP switch **S2** positions 1–4 set the baud rate for *both* transmit and receive (`ON`=`0`,
`OFF`=`1`):

| 1 2 3 4 | Baud | 1 2 3 4 | Baud |
|:---:|---:|:---:|---:|
| 0000 | 50 | 1001 | 2000 |
| 1000 | 75 | 0101 | 2400 |
| 0100 | 110 | 1101 | 3600 |
| 1100 | 134.5 | 0011 | 4800 |
| 0010 | 150 | 1011 | 7200 |
| 1010 | 300 | 0111 | 9600 |
| 0110 | 600 | 1111 | 19200 |
| 1110 | 1200 | | |
| 0001 | 1800 | | |

(p.4.) S2 positions 5–8 are shared with the **parallel section's programmable timer** — see
§13.

---

## 4. UART programming — hardware/software latches (J9/J10)

On power-up the eight UART programming lines are reset to a fixed state defined by which
trace is left intact on jumper sockets **J9** and **J10** (each a cuttable-trace 16-pin DIP
shunt: leave the trace for the desired level, cut the other — **never cut both**, "this could
lead to heating and possible damage to the 74LS175's") (p.4). The CPU can flip any bit later by
writing a `1` to that bit's position in the **Control Port** (§6); a `0` returns it to the
power-up (jumpered) setting.

| Signal | J9/J10 pin group | `'0'` | `'1'` |
|---|---|---|---|
| **NBI** — Number of Bits | J9 | 7 bits | 8 bits |
| **EPS** — Even Parity Select | J9 | Odd parity | Even parity |
| **NP** — No Parity | J9 | Parity | No parity |
| **TSB** — Number of Stop Bits | J9 | 1 stop bit | 2 stop bits |
| **CA** — RS-232 CA output | J10 | "Spacing" | "Marking" |
| **CD** — RS-232 CD output | J10 | "Spacing" | "Marking" |
| **TxINTE** — Transmit interrupt enable | J10 | Disabled | Enabled |
| **RxINTE** — Receive interrupt enable | J10 | Disabled | Enabled |

(J9/J10 diagram and table, p.4.)

---

## 5. Status port — bit assignment (read)

Inputs from the status port to the CPU (p.5):

| Bit | Name | Signal | Polarity |
|:---:|------|--------|----------|
| D0 | **TBMT** | Transmitter buffer empty | active-high (`1`=ready to send) |
| D1 | **DAV** | Data available (received) | active-high (`1`=char waiting) |
| D2 | **OPT** | Optional status line — jumper-selected (§7) as either **CF** (Rec'd Line Signal Detect) or the UART's own **EOC** (End Of Character) output, via J13 | per selection |
| D3 | **PE** | Parity error | active-high |
| D4 | **OR** | Overrun | active-high |
| D5 | **FE** | Framing error | active-high |
| D6 | **CC** | RS-232 CC input (Data Set Ready) | active-high as wired to UART; RS-232 sense polarity is external |
| D7 | **CB** | RS-232 CB input (Clear To Send) | active-high as wired to UART |

The board's own bring-up test confirms **DAV** (bit 1, UART pin 19) goes high after a
character arrives and **TBMT** is polled directly at the UART: `LOOP: IN STATUS / JMP LOOP`
watched on a scope (p.16 test routine, p.17 §3–4). The sample test program masks `ANI 02H` for
DAV and `ANI 01H` for TBMT (p.16), consistent with the bit numbers above.

---

## 6. Control port — bit assignment (write)

Outputs from the CPU to the control port, one bit per J9/J10 programming line (p.5):

| Bit | Name | Signal |
|:---:|------|--------|
| D0 | RxINT E | Receiver interrupt enable |
| D1 | TxINT E | Transmitter interrupt enable |
| D2 | CD | RS-232 CD output |
| D3 | CA | RS-232 CA output |
| D4 | TSB | Number of stop bits |
| D5 | NP | No parity |
| D6 | EPS | Even parity select |
| D7 | NBI | Number of bits/character |

Writing a `1` to a bit flips that parameter to the opposite of its power-up/jumpered state;
writing a `0` returns it to that state (p.4–5). Note the **bit-order mismatch with §4's table**:
§4 lists the *jumper-header* order (NBI…TSB then CA…RxINTE, high-to-low on J9 then J10); the
*control-port* bit order above is the reverse pairing (RxINTE=D0 up to NBI=D7). Both describe
the same eight signals — implement by name, not by assuming the two tables share bit order.

---

## 7. RS-232 control lines and serial mode jumpers

**RS-232 pin map** (p.5):

| Pin | Circuit | Dir. | Description |
|:---:|:---:|---|---|
| 1 | AA | — | Protective ground |
| 2 | BA | TO DCE | Transmitted data |
| 3 | BB | TO DTE | Received data |
| 4 | CA | TO DCE | Request to send |
| 5 | CB | TO DTE | Clear to send |
| 6 | CC | TO DTE | Data set ready |
| 7 | AB | — | Signal ground |
| 8 | CF | TO DTE | Rec'd line signal detect |
| 20 | CD | TO DCE | Data terminal ready |

**Output lines** (control port → RS-232, p.6):

| Data bit | RS-232 line | DB25 pin |
|:---:|:---:|:---:|
| D2 | CD | 20 (master) or 6 (slave) |
| D3 | CA | 4 (master) or 5 (slave) |

**Input lines** (RS-232 → status port, p.6):

| Data bit | RS-232 line | DB25 pin |
|:---:|:---:|:---:|
| D2 (opt., via J13) | CF | 8 (either mode) |
| D6 | CC | 6 (master) or 20 (slave) |
| D7 | CB | 5 (master) or 4 (slave) |

The **Serial Mode Jumpers (J5/J6)** set whether the board is **RS-232 master** (DTE, e.g. a
modem) or **RS-232 slave** (DCE, e.g. a CRT terminal or serial printer) — "since almost all CRT
terminals and serial interface printers operate as the 'master'... the Interfacer II board must
operate as the 'slave'" to talk to them; a modem, itself a slave, needs the board in **master**
mode (p.5). J5/J6 also select **current loop** (on-board current source, e.g. a TTY, or an
external current source) instead of RS-232 levels (p.5, worked jumper diagrams p.7).

One optional input, **CF** (Received Line Signal Detect) or the UART's own **EOC** output, is
selected onto status bit D2 by jumper **J13** (point B→A = CF, point B→C = EOC) (p.6).

---

## 8. Serial interrupts (vectored)

When enabled (RxINTE/TxINTE, §4/§6) and jumpered on **J7** to an S-100 vectored-interrupt line
(VI0–VI7), the serial channel drives:

- **RxINT** low when a character is available and RxINTE is enabled; resets to high-impedance
  once the UART is read.
- **TxINT** low when the UART can accept a new character and TxINTE is enabled; resets to
  high-impedance once the transmit buffer is loaded.

"Note that 'sINTA', S-100 bus pin 96, is not monitored by this board and is not needed to
implement a useful interrupt scheme" (p.5) — i.e. no INTA vector-gating logic; the vectored
interrupt lines themselves (VI0–VI7) are the whole mechanism. **A polled console needs none of
this** — poll DAV/TBMT in the status port.

---

## 9. Parallel section — port map and addressing

Three parallel channels (J1/J2/J3) plus a status/interrupt port occupy **a single 4-port block
anywhere in the 256-port I/O space**, set by DIP switch **S4**:

| Switch position | Function |
|:---:|---|
| 1 | Not used |
| 2–7 | Address bits A2–A7 (`ON`=`0`, `OFF`=`1`) |
| 8 | Block disable (`ON`=disabled, `OFF`=enabled) |

Channel 0 (J1) is at the switch address; Channel 1 (J2) at address+1; Channel 2 (J3) at
address+2; the **Status/Interrupt Control Port** is at address+3, always last in the block
(p.8, p.19). **No factory-default base given** — the worked example addresses the block at
`C8H` (Channel 0=`C8H`, 1=`C9H`, 2=`CAH`, status=`CBH`).

---

## 10. Parallel status port — bit assignment (read)

Inputs from the status port to the CPU (p.8):

| Bit | Name | Signal | Polarity |
|:---:|------|--------|----------|
| D0 | DAV0 | Data available, Channel 0 | active-high |
| D1 | TKN0 | Data taken, Channel 0 | active-high |
| D2 | DAV1 | Data available, Channel 1 | active-high |
| D3 | TKN1 | Data taken, Channel 1 | active-high |
| D4 | DAV2 | Data available, Channel 2 | active-high |
| D5 | TKN2 | Data taken, Channel 2 | active-high |
| D6 | — | Not used | — |
| D7 | — | Not used | — |

DAVx is set when the input strobe latches new data and cleared when the CPU reads that
channel's input port (§12). TKNx reflects whether the output register has been read by the
external device — it "has no significance when the output register is enabled at all times
since the attention flag is being held clear" (p.19).

---

## 11. Parallel interrupt control port — bit assignment (write)

Outputs from the CPU to the Interrupt Control Port (p.8):

| Bit | Name | Signal |
|:---:|------|--------|
| D0 | INTEJ1 | Interrupt enable, Channel 0 (J1) |
| D1 | INTEJ2 | Interrupt enable, Channel 1 (J2) |
| D2 | INTEJ3 | Interrupt enable, Channel 2 (J3) |
| D3 | TMRIE | Timer interrupt enable |
| D4–D7 | — | Not used |

Power-up/reset state for D0–D2 is set by a jumper shunt at **J8** (same convention as J9/J10:
leave the trace for the desired power-up level). Writing a `1` toggles the bit from its
jumpered state; writing a `0` returns it (p.8).

---

## 12. Port control lines (per parallel channel)

- **Input strobe:** latches external data into the input register (74LS374: edge-latched; or
  74LS373: either fully-latched-at-strobe-end or fully-transparent-with-latch-on-close, mode set
  by the per-channel **Strobe Polarity Select** switch, S1 positions 1/2, 4, 6) and sets DAVx
  (p.9).
- **Output enable line:** tri-states the output register's `Q`/`Q̄` drivers (Polarity Select
  switch S1 positions 3/5/7: `ON`=active-low enable, `OFF`=active-high/floating enable). While
  tri-stated, **TKNx stays high** until the external device asserts the enable; if the output
  register is always enabled (the common case), TKNx stays low (p.10).
- **Attention line:** jumper-selectable (J14/J15/J16) to drive `Q`, `Q̄`, or a strobe pulse (`L`
  low-going or `R` high-going, 150–1000 ns, tied to the system `PWR*` strobe) whenever new data
  is loaded into the output register — informs an external device that output data is ready
  (p.10).
- **I/O connector (DB25) pinout, per parallel port:** DI0–DI7 (input data), DO0–DO7 (output
  data), STROBE, ENABLE, ATTENTION, ±12 V, +5 V, GROUND (two pins) (p.11 pinout diagram).
  A small amount of current (+5 V @ 200 mA, +12 V @ 50 mA, −12 V @ 50 mA, **total for all three
  connectors**) is available to power an external keyboard or A/D converter (p.11).

---

## 13. Programmable timer interrupt

The Interfacer II has a fixed-rate interrupt timer built from one half of the baud-rate
generator (U20), a divide-by-16 stage, and a 4-bit counter (U29). DIP switch **S2** positions
5–8 set the **base** interrupt rate (same physical switch used for baud, §3):

| 5 6 7 8 | Rate (int/sec) | 5 6 7 8 | Rate (int/sec) |
|:---:|---:|:---:|---:|
| 0000 | 50 | 1001 | 2000 |
| 1000 | 75 | 0101 | 2400 |
| 0100 | 110 | 1101 | 3600 |
| 1100 | 134.5 | 0011 | 4800 |
| 0010 | 150 | 1011 | 7200 |
| 1010 | 300 | 0111 | 9600 |
| 0110 | 600 | 1111 | 19200 |
| 1110 | 1200 | 0001 | 1800 |

(p.11.) The base rate can be further divided by 2, 4, or 8 by cutting the J12 trace from point
`A` to common and jumpering common to `B` (÷2), `C` (÷4), or `D` (÷8) (p.11).

**Enable/service protocol:** the timer bit (TMRIE, control-port D3, §11) is disabled after
power-up/reset and must be written `1` to start it. On interrupt, the proper response is to
**write `0` then `1`** to TMRIE — this both removes the interrupt and re-arms the counter for
the next period (p.11, p.19).

---

## 14. Parallel vectored interrupts

Same physical interrupt-vector jumper block **J7** is shared with the serial section (§8) —
`INT J1`/`INT J2`/`INT J3` (one per parallel channel, driven low when that channel's input is
strobed and INTEJn is enabled), `TMRI` (timer), `RxINT`/`TxINT` (serial). Each line is jumpered
to whichever S-100 vectored-interrupt pin (VI0–VI7) the system uses; a channel's interrupt line
resets to high-impedance once its data is read (p.8, p.11 diagram).

---

## 15. Board options not modeled (not in scope for emulation)

- **Current-loop wiring (J4–J6):** on-board vs. external current source selection is a physical
  drive-circuit choice with no register-visible effect; model only the logical byte stream.
- **RS-232 slew-rate capacitors (C7–C13):** cable-length compensation, no logical effect.
- Exact internal 1602/1863 pinout/timing is **not in this manual** — it treats the UART as a
  fixed black box between the programming latches and the status/data ports.

---

## Emulation checklist

- **Serial:** 2-port block anywhere in I/O space (S3, addr bits A1–A7 + disable). Data at base,
  status at base+1. **UART = 1602/1863**, programmed by 8 latch bits (NBI/EPS/NP/TSB/CA/CD/
  TxINTE/RxINTE), power-up state from J9/J10 jumper, runtime override via the control port
  (§6) — a `1` flips a bit from its jumpered default, a `0` restores it.
- **Serial status is active-high**: TBMT=D0 (ready to send), DAV=D1 (char available), PE=D3,
  OR=D4, FE=D5, CC=D6, CB=D7; D2 is jumper-selected CF or EOC (J13). For a byte-clean transport
  model PE/OR/FE=0.
- **Baud (S2 bits 1–4)** and **timer base rate (S2 bits 5–8)** share the same DIP switch but
  are logically independent 4-bit fields against the same 16-row table.
- **RS-232 master/slave (J5/J6)** swaps which DB25 pins carry CA/CD (out) and CC/CB (in) —
  model as a single master/slay convention with the pin table in §7, not separate polarity.
- **Parallel:** 4-port block anywhere in I/O space (S4, addr bits A2–A7 + disable): channel 0/1/2
  at base/base+1/base+2, status+interrupt-control at base+3 (always last).
- **Parallel status is active-high**: DAV0/TKN0/DAV1/TKN1/DAV2/TKN2 = D0–D5, in that
  interleaved order (not three DAVs then three TKNs).
- **Parallel interrupt-enable port (write, base+3):** INTEJ1/2/3 = D0–D2 (power-up state from J8
  jumper), TMRIE = D3. Timer-service idiom is write-0-then-1 to TMRIE.
- **Interrupts (both sections) share one jumper block, J7**, to the S-100 vectored-interrupt
  lines (VI0–VI7); `sINTA` (pin 96) is **not used** — no INTA vector-gating to model. A polled
  implementation needs none of this.
- **No factory-default base for either section** — expose both as straps; the manual's own
  worked examples use serial `00H` and parallel `C8H`.

**Recorded discrepancy.** §4 (UART Programming, p.4) lists the eight programming-latch bits in
jumper-header order (J9: NBI…TSB; J10: CA…RxINTE, high-to-low). §5/§6 (Status/Control Port Bit
Assignment, p.5) list the same eight *control-port* bits in the opposite pairing (RxINTE=D0 up
through NBI=D7). Both tables are consistent internally and describe the same eight named
signals; the manual never states outright that the two orderings are reverses of each other, so
implement by signal name against the control-port table (§6), not by assuming a shared bit
order with the jumper-header diagram.
