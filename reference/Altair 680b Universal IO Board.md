# Altair 680b Universal I/O Board (UI/O)

Source: [680-UIO Manual.pdf](#) (MITS *Altair 680b Universal I/O Board Documentation*,
© MITS Inc. 1976, second printing March 1977)

The Universal I/O Board is the general-purpose parallel-and-serial expansion board for the
**Altair 680b**, MITS's **Motorola 6800** machine. Like the [KCACR](Altair%20680b%20KCACR.md)
it is **memory-mapped** — the 6800 has no separate I/O space, so every register is an ordinary
memory address reached with `LDA`/`STA`, not `IN`/`OUT`. One board occupies **one expander
slot** and carries three logical ports built from two standard Motorola peripheral chips:

- a **6820 PIA** (Peripheral Interface Adapter) → one 8+8-bit **parallel** port (two ports,
  32 data lines, when a second PIA is populated), plus
- a **6850 ACIA** (Asynchronous Communications Interface Adapter) → one **serial** port
  (RS-232, TTL, or 20 mA TTY current loop).

It also provides a fixed **8-bit non-latched parallel output** and a set of **hardware
switch inputs** ("sense" bits). This is the 680-side counterpart to the 8800 family's
[88-PIO](MITS%2088-PIO.md)/[88-4PIO](MITS%2088-4PIO.md) and [88-SIO/2SIO](88-SIO%20Rev%200%20%26%201.md)
boards — same chips (6820/6850), different (memory-mapped, active-**high**) host wrapper.
See [[altairsim-pio-boards]], [[serial-io-architecture]], [[altairsim-88uio-board]].

This is a distilled emulation reference. Kit assembly, the 100-pin edge-connector and standoff
installation, cable fabrication, the RS-232/TTL/TTY level-conversion analog, the
troubleshooting/silkscreen chapter, and the full three-sheet schematic are omitted except
where they fix a software-visible value. What is kept: the address map, the PIA and ACIA
register models, baud selection, the non-latched output and switch inputs, and the interrupt
wiring.

> ⚠ **Unlike the KCACR, the UI/O is active-HIGH.** The KCACR's "True = Logic 0" convention is
> specific to that board's discrete status latch. The UI/O's status/control bits are the raw
> **6820 and 6850** register bits — ordinary active-high Motorola parts. Do not carry the
> KCACR's inverted-true rule across to this board.

## 1. Addressing (memory-mapped, F0XX)

The 680b reserves **256 memory locations (`F000`–`F0FF`)** for I/O; each I/O board uses a
16-address window inside it. Address decode:

| Lines | Role |
|---|---|
| **A15–A8** | Hard-wired to `F0` for every 680b I/O board (not user-changeable) |
| **A7–A4** (+ complements) | Board base select via switch **S9** — 16 possible window positions |
| **A3** | Selects **parallel** (PIA) vs **serial** (ACIA) |
| **A2** | Selects between the two parallel ports |
| **A1, A0** | Selects the register within a port (section + channel) |

**Default layout — S9 at its lowest position** (the "strapped at the lowest position" the
manual's example programs assume):

| Port | Chip | Addresses | Notes |
|---|---|---|---|
| Parallel port 1 | **PIA-C** | `F008`–`F00B` | first / standard parallel port |
| Parallel port 2 | **PIA-B** | `F00C`–`F00F` | second PIA, only if populated |
| Serial port | **ACIA-D** | `F006`–`F007` | `F006` = Control/Status, `F007` = Data |

S9 relocates the whole board (Table 3-2 in the manual lists all 16 positions, e.g. the next
step up puts the serial port at `F016`/`F017`, parallel at `F018`–`F01F`).

## 2. Fixed addresses — NOT relocated by S9

Two facilities decode to fixed locations regardless of S9:

| Address | Function |
|---|---|
| **`F003`** | **Hardware switch inputs** ("sense" bits) — a read-only tri-state driver gates the on-board switch settings (logic 1/0) onto the data bus. Fixed; cannot be moved. |
| **`F010`/`F011`** | 8-bit **non-latched parallel output**, "Drive 1" (`F010` = control/status, `F011` = data) |
| **`F012`/`F013`** | 8-bit non-latched parallel output, "Drive 2" |

⚠ **`F010`/`F011` collide with the [KCACR](Altair%20680b%20KCACR.md).** The KCACR's status/
control and data registers are at exactly `F010`/`F011`. This is why the KCACR manual instructs
you, with a UI/O present, to **remove IC A1 on the UI/O** — it disables the UI/O's non-latched
output decode so the cassette board can own those two addresses. An emulator wiring both boards
into one 680b must honour that: only one may answer `F010`/`F011`.

## 3. Parallel port — 6820 PIA

Each PIA has two independent sections, **A** and **B**. A section presents three registers
selected by the low address bits and by **bit 2 of its Control Register**:

| Register | Selected when |
|---|---|
| **Control/Status** (read/write) | low address = the section's control channel |
| **Data Register** | data channel **and** Control bit 2 = **1** |
| **Data Direction Register (DDR)** | data channel **and** Control bit 2 = **0** |

Default (PIA-C at `F008`): `F008` = section A Control/Status, `F009` = section A Data/DDR,
`F00A` = section B Control/Status, `F00B` = section B Data/DDR.

**Data direction:** write `1` into a DDR bit → that line is an **output**; write `0` → **input**.
Any per-line mix is allowed (`PA0`–`PA7`, `PB0`–`PB7`). **On power-up all PIAs reset**, which
clears the DDRs and leaves every line, and the C2 control lines, as **inputs**.

**Control/Status Register bits** (standard 6820):

| Bits | Function |
|---|---|
| 7 | **IRQA/IRQB status** — set by an active C1 transition; cleared by reading the Data Register |
| 6 | Second interrupt-flag status (C2-driven when C2 is an input) |
| 5,4,3 | **C2 control** — C2 is input (bit 5 = 0, mode by bits 4/3) or output (bit 5 = 1) |
| 2 | **DDR/Data select** (0 = DDR, 1 = Data Register) |
| 1,0 | **C1 control** — active edge (bit 1) and interrupt enable (bit 0) |

Bits 7 and 6 are read-only status and are **unaffected by a write** to the register. The
`CA1`/`CB1` handshake inputs strobe data in from the device; `CA2`/`CB2` can be programmed as
output strobes ("data taken"/"data ready") tied to the 6800 **E** (Φ2) pulse. Initialization
is a two-step-per-section idiom (clear bit 2 → write DDR → set bit 2 → write control), shown as
Program 2-1 in the manual.

## 4. Serial port — 6850 ACIA

The serial port is a standard **6850 ACIA** with a Register Select on **A0**:

| A0 | Read (R/W̄ = 1) | Write (R/W̄ = 0) |
|---|---|---|
| 0 (`F006`) | **Status Register** | **Control Register** |
| 1 (`F007`) | **Receive Data** | **Transmit Data** |

The 6800 **E** clock (Φ2, 500 kHz) is tied to the ACIA `ENABLE` and gates all transfers.

**Control Register** (write `F006`), standard 6850:

| Bits | Function |
|---|---|
| 1,0 | **Counter divide / master reset**: `00` = ÷1, `01` = ÷16, `10` = ÷64, **`11` = Master Reset** |
| 4,3,2 | **Word select**: data bits (7/8), parity (E/O/none), stop bits (1/2) |
| 6,5 | **Transmit control**: RTS level + transmit-interrupt enable + break |
| 7 | **Receive Interrupt Enable** |

⚠ **Master-reset the ACIA before use.** Write bits 1,0 = `11` first, then re-write the real
configuration. Normal init selects **÷16** because the baud-rate clock is 16× the line rate.
The 8N2-at-TTY example in the manual is `Control = 0x56` (÷16, 8 data / 2 stop / no parity,
RTS low, Tx+Rx interrupts enabled); a reset pass writes `0x03`.

**Status Register** (read `F006`), standard 6850:

| Bit | Flag |
|---|---|
| 0 | **RDRF** — Receive Data Register Full (cleared by reading RX data or master reset) |
| 1 | **TDRE** — Transmit Data Register Empty (ready for the next byte) |
| 2 | **DCD** — Data Carrier Detect (from modem; latches, cleared by read-status-then-read-data) |
| 3 | **CTS** — Clear To Send (from modem; while high, TDRE is inhibited) |
| 4 | **FE** — Framing Error |
| 5 | **OVRN** — Receiver Overrun |
| 6 | **PE** — Parity Error |
| 7 | **IRQ** — interrupt request pending |

## 5. Baud rate — switch S10 + clock divide

The line rate is set by the on-board **switch S10** (hardware baud-clock select) combined with
the ACIA counter-divide bits:

- **÷16 mode** (Control bits 1,0 = `01`) → the standard rates: **75, 110, 134.5, 150, 200,
  300, 600, 1200, 1800, 2400, 4800, 9600** baud (Table 2-9). Set the marked S10 switch to the
  `1`/`0` side per the silkscreen.
- **÷64 mode** (bits = `10`) → five **additional** rates; the selected S10 rate is then **4×**
  the desired line rate (Table 2-10). The ÷1 mode is not usable without external sync.

The baud-rate clock is always 16× (or 64×) the line rate; this is a hardware clock, not a
divisor the guest computes — see the "a plausible number that boots isn't correct timing" rule
[[altairsim-plausible-but-wrong-timing]].

## 6. Interrupts

Both the PIA and the ACIA drive the 680b **`IRQ`** line, **active-low**, and the request stays
asserted as long as the cause is present and that interrupt is enabled:

- **PIA:** an active `CA1`/`CB1` (or C2-as-input) transition sets the status flag and, if that
  interrupt is enabled in the Control Register, pulls `IRQ` low. **Reading the Data Register**
  clears the flag.
- **ACIA:** Receive Interrupt Enable (Control bit 7) and the transmit-interrupt encoding
  (bits 6,5) gate RDRF/TDRE/DCD onto `IRQ`. Status bit 7 reflects the pending request.

As on the KCACR, the 6800 vectors through **`FFF8`/`FFF9`**, which the 680b monitor PROM points
into RAM (`0100`). The device also brings selected data bits (0, 1, 7) out through a decode gate
so a handler can identify the interrupting line.

## 7. Emulation notes / gotchas

- **Memory-mapped, active-high, standard Motorola parts.** Model the parallel port as a plain
  **6820 PIA** and the serial port as a plain **6850 ACIA** at their `F0XX` addresses. If a
  6850 model already exists for the 2SIO/88-SIO, this is that chip with a memory-mapped host
  wrapper — not a new UART. Do **not** apply the KCACR's inverted-true convention here.
- **S9 relocates the board; `F003`/`F010`–`F013` do not move.** Decode the switch inputs and
  non-latched outputs at their fixed addresses independently of the S9 base.
- **`F010`/`F011` conflict with the KCACR.** Only one board may answer those addresses; the
  period fix is "remove UI/O IC A1." If both boards are configured, the UI/O non-latched output
  must yield. See [[altairsim-88uio-board]].
- **Reset both chips on init.** A guest sets ACIA Control `11` (master reset) before real
  config, and the PIA power-on reset leaves all lines as inputs — model both so the first real
  transfer behaves.
- **Serial handshake matters for a modem endpoint.** DCD and CTS are live status bits (CTS high
  inhibits TDRE); wire them to the same modem-signal model every serial board uses, per
  [[modem-signals-are-uniform]].
- **Baud is a hardware clock, not a divisor the CPU sets.** The line rate comes from S10 + the
  ÷16/÷64 counter mode, not from a value the guest pokes each byte.

## 8. Key facts at a glance

| | |
|---|---|
| Machine | Altair **680b** (Motorola **6800**), memory-mapped I/O |
| Chips | **6820 PIA** (parallel) + **6850 ACIA** (serial) |
| I/O window | `F000`–`F0FF` reserved; A15–A8 = `F0`; A7–A4 select via **S9** |
| Parallel port 1 (PIA-C) | `F008`–`F00B` (default) |
| Parallel port 2 (PIA-B) | `F00C`–`F00F` (default, second PIA) |
| Serial port (ACIA-D) | `F006` Control/Status, `F007` Data (default) |
| Switch inputs | **`F003`** (fixed, read-only) |
| Non-latched output | **`F010`/`F011`** (Drive 1), `F012`/`F013` (Drive 2) — fixed ⚠ collides with KCACR |
| Bit convention | **active-high** (raw 6820/6850 bits) |
| ACIA control | bits 1,0 divide (`11` = reset, `01` = ÷16); 4–2 word; 6,5 Tx ctl; 7 Rx int |
| ACIA status | RDRF, TDRE, DCD, CTS, FE, OVRN, PE, IRQ |
| Baud | switch **S10** + ÷16 (75–9600) or ÷64 (5 extra rates, 4× selected) |
| Serial levels | RS-232, TTL, or 20 mA TTY current loop |
| Interrupt | PIA + ACIA → 6800 **`IRQ`** (active-low); vector `FFF8/FFF9` → `0100` |
