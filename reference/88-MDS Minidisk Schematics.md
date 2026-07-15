# 88-MDS Minidisk Schematics

Source: [88-MDS Minidisk Schematics.pdf](#)

MITS 88-MDS 5.25" hard-sectored minidisk subsystem. The subsystem is three
boards plus the drive: **Controller Board #1** (address decode, read data path,
status, sector timing), **Controller Board #2** (write data path, drive/step
control, disk-enable timing), and the **Buffer / Power Supply** board (line
drivers/receivers to the drive, drive-address switch, the drive's +5 V / +12 V
supplies). The interface Board #1 presents to the 8080 bus is the standard MITS
88-DISK three-port scheme (ports 010₈–012₈).

This document captures everything electrically relevant for emulation. The
source is a scan of hand-drawn schematics; where a specific net, pin, or table
cell was not fully legible it is flagged as **(uncertain)**.

## Sheet index

| Figure | Sheet | Board | Contents |
|--------|-------|-------|----------|
| 4-16 | — | Buffer / Power Supply | Drive line drivers/receivers, drive-address DIP switch SW-1, +5 V (7805) and +12 V (7812) drive supplies, drive-interface connector P1, backpanel connectors P3/P5 |
| 4-14 | 1 of 3 | Controller #1 | 8080-bus address/I-O select (port decode), I/O strobes, 8-bit sector counter, sector/index timing one-shots, sector data buffer (8T97) |
| 4-14 | 2 of 3 | Controller #1 | Read data path (74164 serial→parallel, 74175 latches), read-clock/read-window one-shots, sync-bit detect, status output buffers (8T97), interrupt option jumper |
| 4-14 | 3 of 3 | Controller #1 | IC power-connection table, board-to-board interconnect, on-board 7805 regulator |
| 4-15 | 1 of 2 | Controller #2 | Write data path (74166 serial shift, write-bit counter), disk-control latch decode, step/direction/motor/head-settle one-shots, disk-enable timer (40205), interrupt-enable FF |
| 4-15 | 2 of 2 | Controller #2 | IC power-connection table, on-board 7805 regulator, full board-#2 interconnect / drive backpanel wiring |

## Bus interface — port map (Controller #1, Fig 4-14 sheet 1)

Address decode gates (left/`D` column) generate a "disk enabled" term from the
high address bits ANDed with the low octal digit. The base port is **010₈
(08H)**; the low octal digit selects one of three registers. Read vs. write is
selected by `SINP`/`SOUT`/`PDBIN`/`PWR` bus status. Each decoded access
produces a ~500 ns strobe.

| Octal | Hex | IN (SINP) | OUT (SOUT/PWR) |
|-------|-----|-----------|----------------|
| 010₈ (xx0) | 08H | **INPUT-STATUS STROBE** — drive status byte | **OUTPUT-DISK CONTROL LATCH STROBE** — drive select / enable |
| 011₈ (xx1) | 09H | **INPUT-SECTOR STROBE** — sector position byte | **OUTPUT-CONTROL DISK STROBE** — drive control command |
| 012₈ (xx2) | 0AH | **INPUT-READ DATA STROBE** — read data byte | **OUTPUT-WRITE DATA STROBE** — write data byte |

Decode terms explicitly annotated on the sheet:

- `= 1 WHEN DISK ENABLED`
- `1 = READ MODE (500 NS PULSE)` and `1 = WRITE MODE (500 NS PULSE)`
- `= 1 WHEN ADDRESS = 010₈`, `= 1 WHEN ADDRESS = 011₈`
- `1 WHEN ADDRESS = xx0₈`, `= 1 WHEN ADDRESS = xx1₈`, `= 1 WHEN ADDRESS = xx2₈`

Bus lines received on the `D` column: `SINP`, `PDBIN`, `SOUT`, `PWR`, `AI5`,
`AI4`, `AI3`, `AI2`, `AI1`, `AI0`, `A9`, `A6`, and `2 MHZ CLK` (buffered to
`2 MHZ CLK` on-board). Data-out `DO0–DO7` and data-in `DI0–DI7` cross to the
buffers noted below.

## Status byte — IN 010₈ (08H)

Read back through 8T97 buffers `H3`/`H5` on Fig 4-14 sheet 2. Signal names
lifted from the buffer inputs (standard 88-DISK active-low status; `0` = the
named condition is true / ready):

| Bit | Signal | Meaning |
|-----|--------|---------|
| 7 | **NRDA** | New Read Data Available (0 = a read byte is ready) |
| 6 | **TRK 0 / TRACK 0** | Head is over track 0 |
| 5 | **INT STATUS (INTE)** | Interrupts enabled |
| 4 | ST4 | (uncertain — unused/reserved on minidisk) |
| 3 | ST3 / HEAD STATUS | Head status (uncertain bit position) |
| 2 | **HS ST2 (HEAD STATUS)** | Head status / head-settle done |
| 1 | **MOVE HEAD (MH ST1)** | OK to move head (step) |
| 0 | **ENWD** | Enter New Write Data (0 = controller ready for next write byte) |

Note: the minidisk has **no head-load** bit (unlike the 8-inch 88-DCDD); the
"head status" line here reflects head-settle timing, not a head-load solenoid.
Exact placement of ST3/ST4 was not fully legible.

## Sector position byte — IN 011₈ (09H)

Driven from the **8-bit sector counter** (7493 pair `R01`/`R02`, Fig 4-14
sheet 1) through 8T97 buffer `B197` onto `DI0–DI5`. Standard 88-DISK layout:

| Bit(s) | Meaning |
|--------|---------|
| 0 | **Sector True** — 0 at the start of a sector window (valid to strobe the head), returns to 1 after the leading portion of the sector |
| 1–4 | Current **sector number** (minidisk = 16 hard sectors, 0–15) |
| 5 | High sector-count bit / carry (the counter is 8-bit but the minidisk wraps at 16) |
| 6–7 | Unused |

Sector timing chain on sheet 1: **Sector Pulse one-shot** (74123 `A2`), **Index
Verification FF** (`INDEX`, 7474 `B3`) with **Index Window one-shot** (74123
`E1`) and index-window verification, **Index Pulse Compressor FF** (7473 `E3`,
7473 `F2`), **Index Latch** (7400 gates), **Sector Count one-shot** (74123
`F4`), **Read Clear one-shot** (74123 `F1`, labeled READ CLR), **Write Clear
one-shot** (74123 `F4`, START OF SECTOR CLR / WRITE DATA ENABLE). The
`IND/SECTOR` input from the drive carries both index and sector holes; the
index pulse is discriminated by width (compressor + verification).

## Disk control latch — OUT 011₈ (09H) (Controller #2, Fig 4-15 sheet 1)

The control command byte `DO0–DO7` is latched and decoded by gate banks `H1`
and `J1`. Command lines legible on the sheet: **STEP IN**, **STEP OUT**,
**TRACK RESET**, **ENABLE INTERRUPTS**, **DISABLE INTERRUPTS**, **WRITE
ENABLE**, and **STEP PULSE**. Following the standard 88-DISK control-byte
convention (minidisk variant — head-load/head-current bits are absent, replaced
by motor/head-settle timing):

| Bit | Command | Notes |
|-----|---------|-------|
| 0 | **Step In** | Pulse; direction toward higher track |
| 1 | **Step Out** | Pulse; direction toward track 0 |
| 2 | — | (head-load on DCDD; not used on minidisk) |
| 3 | — | (head-unload on DCDD; not used on minidisk) |
| 4 | **Enable Interrupts** | Sets Interrupt-Enable FF (`E2`) |
| 5 | **Disable Interrupts** | Clears Interrupt-Enable FF |
| 6 | — | (head-current on DCDD; not used on minidisk) |
| 7 | **Write Enable** | Sets Write-Enable FF (`E2`), arms write path |

Associated timing/one-shots on sheet 1:

- **Step Direction one-shot** — 74123 `A2`
- **Step Pulse one-shot** — 74123 `A1` (`STEP PULSE` output, RC `R3 10K`)
- **Head Settle one-shot** — 74123 `B1` (`HEAD SETTLE`)
- **Drive Motor On Delay** — 74123 `B1` (motor spin-up delay; motor is opt-in)
- **Disk Enable Timer** — **40205** CMOS long counter `B2` (auto-disable the
  drive after inactivity)
- **One-shot timer** — 74123 `B3` (DISK RESET, RC `R2 10K` / `C2`)
- **Motor FF** (`H2`), **Write-Enable FF** and **Interrupt-Enable FF** (`E2`),
  **Interrupt Latch**, **Disk-Enable FF**

## Drive select latch — OUT 010₈ (08H)

Selects/enables the drive. The two drive-address bits **DA-A / DA-B** (`DAA`,
`DAB`) go out to the drive; the on-board **SW-1** DIP switch on the Buffer board
sets each drive's own address (`DRIVE ADDRESS`), compared against the selected
address to assert `DRIVE SELECT` / `DE` (drive enable). Selecting a drive also
starts the motor / disk-enable timer.

## Read data path (Controller #1, Fig 4-14 sheet 2)

- **74164 (`G1`)** — READ DATA SERIAL-TO-PARALLEL SHIFT REGISTER (`SERIAL
  DATA` in, 8 parallel out).
- **74175 (`G3`) + 74175** — **READ DATA LATCHES** (`RD0–RD7`), presented to
  `DI0–DI7` via 8T97 buffer `H4`.
- **93L16 (`B1`)** — 4-bit presettable counter, "counts 8 read clocks" (byte
  framing).
- **Read Clock one-shot** — 74123 `A1` (RC `R16 15K` / `C5 430pF`, ≈ 2.0 µs).
- **Read Data Window one-shot** — 74123 `A1` (RC `R15 22K` / `C6 90pF`,
  ≈ 6.1 µs), gated by **Read Data Window Gate** (74L00 `A4`).
- **Read Data Bit Latch** — 74L00 `G2`; **Read Data Mask Gate** `E5`.
- **New Data Read FF** — 7473 `B2` (generates `NRDA`).
- **Sync Bit Detector FF** — 7474 `B2`.
- **Read Latch Pulse Compressor FF** — 7473 `F2`.
- Raw `READ DATA` enters from the drive (`3-D7`), `READ CLEAR` from `I-C1`.

## Write data path (Controller #2, Fig 4-15 sheet 1)

- **74166 (`G3`)** — WRITE DATA SHIFT REGISTER (parallel `DO0–DO7` in, `SERIAL
  OUT`), with **Write Data Latch** (`G3`).
- **Write Data Window Counter** and **Write Bit Counter** (`A3`/`A4`, ÷8) —
  generate `WRITE CLOCK` and the bit-window that clocks the shift register;
  `WRITE DATA WINDOW` term.
- **Disk Address Latch** (`G2`) — holds the selected drive address for the
  write session.
- `WRITE CLK` derived from `2 MHZ` (`C = 1 MHZ`); `WDS` = write-data strobe,
  `WDE` = write-data enable.
- Output `WRT DATA` / `DISK MODE A` / `DISK MODE B` drive the head write
  circuitry through 8T98 (`J2`) drivers.

## Interrupt option (Fig 4-14 sheet 2)

Jumper block labeled **INTERRUPT OPTION**: pads `SEL INT` / `PINTE` and
**`VI0`** / **`VI7`**. Selects which vectored-interrupt line the controller
asserts. Annotation: *"INTERRUPT ON BEGINNING OF SECTOR WHEN DISK INTERRUPT
ENABLED."* `PINTE` (processor interrupt enable) gates the output.

## Controller Board #1 — IC list (Fig 4-14 sheet 3)

All resistors ohms, ½ W unless noted; all caps µF unless noted; all diodes
1N914 unless specified. `(280 ±5)` = nominal time constant. Substitute column
is the 74LS equivalent.

| Ref(s) | Type | VCC | GND | Substitute |
|--------|------|-----|-----|-----------|
| B5, E4, G5, J3 | 74L04 (hex inverter) | 14 | 7 | 74LS04 |
| E2, E5, G2 | 74L00 (quad NAND) | 14 | 7 | 74LS00 |
| B4 | 74L10 (triple 3-in NAND) | 14 | 7 | 74LS10 |
| A4, A5 | 74L20 (dual 4-in NAND) | 14 | 7 | 74LS20 |
| A3 | 74L30 (8-in NAND) | 14 | 7 | 74LS30 |
| F5 | 74L02 (quad NOR) | 14 | 7 | 74LS02 |
| A2 | 74367 (hex buffer) | 16 | 8 | 74367 |
| H2, H3, H4, H5 (H1) | 8T97 (hex 3-state buffer) | 16 | 8 | 74367 |
| E1, F1, F4 | 74L73 (dual JK FF) | 4 | 11 | 74LS73 |
| G4 | 74123 (dual one-shot) | 16 | 8 | 74LS123 |
| B1 | 93L16 (presettable counter) | 16 | 8 | 93L16 |
| B2 | 74L74 (dual D FF) | 14 | 7 | 74LS74 |
| G3, H1 | 74L75 (quad latch) | 5 | 12 | 74LS75 |
| G1 | 74164 (8-bit shift reg) | 14 | 7 | 74LS164 |
| (reg) | 7805 (+5 V) | 2 | 3 | — |

On-board supply: `+8 V` → `7805` with series inductors `L1/L2/L3` and bypass
`C7 33µF`, `C11 33µF`, `C2 .1µF`, `C15–C25 .1µF`, producing `VCC (+5 V
REGULATED)`. `VHA–VHL` pull-up rail from `E5-24` via 1 K.

## Controller Board #2 — IC list (Fig 4-15 sheet 2)

(Some cells partly illegible — flagged.)

| Ref(s) | Type | VCC | GND | Substitute |
|--------|------|-----|-----|-----------|
| F1, F3, F4, H1, J1 | 74L02 (quad NOR) | 14 | 7 | 74LS02 |
| E1, E5 | 74L00 (quad NAND) | 14 | 7 | 74LS00 |
| B4, H2, G3 (uncertain) | 74L04 (hex inverter) | 14 | 7 | 74LS04 |
| J2 | 8T98 (hex 3-state inv buffer) | 16 | 8 | 74366B |
| E4 | 74L10 (triple 3-in NAND) | 15 (uncertain) | 7 | 74LS10 |
| H2 | 74166 (8-bit shift reg) | 16 | 8 | 74LS166 |
| G3, H3 | 74L75 (quad latch) (uncertain) | 5 | 12 | 74LS75 |
| A3, A4 | 93L16 (presettable counter) | 16 | 8 | 93L16 |
| J4 | 74L74 (dual D FF) | 14 | 7 | 74LS74 |
| A2, E2 | 74L73 (dual JK FF) | 4 | 11 | 74LS73 |
| A1, B3 | 74123 (dual one-shot) | 16 | 8 | 74LS123 |
| K3 | 8T97 (hex 3-state buffer) | 16 | 8 | 74367 |
| L1 | 7805 (+5 V) | — | 3 | — |
| B2 | 40205B (CMOS long counter) | 16 | 8 | — |

On-board supply mirrors Board #1: `+8 V` → `7805 (L1)` → `VCC (+5 V REG)`, with
`C18 .1µF`, `C21 35µF`, `C9 35µF`, `C30 .1µF`; `VHA–VHC` pull-up rail from
`R11–R13 1K`.

## Buffer / Power Supply board — IC list (Fig 4-16)

| IC | Type | VCC | GND | Function |
|----|------|-----|-----|----------|
| C | 74367 | 16 | 8 | Hex 3-state buffer (control signals out to drive) |
| D | 7406 | 14 | 7 | Open-collector inverting driver (WRITE GATE, WRITE DATA, STEP, DIRECTION SEL, MOTOR ON, DRIVE SELECT to drive) |
| B | 7410 | 14 | 7 | Triple 3-in NAND (drive-address decode with SW-1) |
| A | 7402 | 14 | 7 | Quad NOR (drive-address / select logic) |

Line resistors: `R6–R16` = 680 Ω (drive-signal terminations); `R9/R10/R11` =
1 K; `R2/R4` = 180 Ω; `R5` = 1 K / 180 Ω with LED `D7` (power indicator).

### Drive-address DIP switch SW-1

Sets the physical drive's address, compared to the selected `DA-A`/`DA-B` to
generate `DRIVE SELECT` / `DRIVE ADDRESS`. Signals `DA H` / `DA L` feed the
7410/7402 decode.

### Drive supplies (Fig 4-16)

| Reg | Type | Output | Load |
|-----|------|--------|------|
| VR-2 | 7812 | **+12 V drive motor supply** | .5 A standby / .9 A drive selected |
| VR-1 | 7805 | **+5 V drive logic supply** | ~1 A typical |

Transformer `T1` = 28 VAC 2.5 A (yellow). Rectifier diodes `D1`, `D2`, `D3` =
1N4004. Bulk caps: `C2` 1000µF/25V, `C3`/`C4` 2200µF/25V, `C5` 3.3µF/50V,
`C6`/`C7` (16V/50V). Mains: `SW2` on/off, fuse `F1`, jumper-selectable **110 V**
vs **220 V** primary wiring. `P3` 5-pin Molex for the transformer secondary.

## Connectors

### Drive Interface Connector P1 (Buffer → drive, Fig 4-16, 33-pin)

Signal groups carried (odd pins signal, even/adjacent ground):

`IND/SECTOR`, `TRACK 0`, `READ DATA`, `WRITE PROTECT`, `WRITE GATE`, `WRITE
DATA`, `MOTOR ON`, `STEP`, `DIRECTION SEL`, `DRIVE SELECT`. (Pin numbering on
the scan runs 1,3,5…33 down one row with returns; individual pin→signal
assignments were only partly legible.)

### Backpanel connectors P3 / P5 (Buffer board, 26-pin)

Carries the drive-cable signals between the buffer board and the drive
backpanel: `IND/SECT`, `TRK0`, `RDAT`, `DISK PWRD`, `WEN`, `WDAT`, `STEP`,
`DIRECTION`, `HDLD`, `DAA`, `DAB` (plus grounds).

### Mini-Disk Drive Back Panel Connector (26-pin, Fig 4-15 sheet 2 / 4-16)

Odd pins = signal, even pins = ground.

| Pin | Signal |
|-----|--------|
| 1 | IND/SECT |
| 3 | TRK 0 |
| 5 | RDAT (read data) |
| 7 | DISK PWRD (disk power / reset) |
| 9 | WEN (write enable) |
| 11 | WDAT (write data) |
| 13 | STEP |
| 15 | DIRECTION |
| 17 | HD LD (head load — unused on minidisk) |
| 19 | DE (drive enable) |
| 21 | DA-A (drive address A) |
| 23 | DA-B (drive address B) |
| 25 | DC (PWR / PS) |

### Board-to-board interconnect (Fig 4-14 sheet 3, Fig 4-15 sheet 2)

Controller #1 ↔ Controller #2 ↔ computer backpanel ↔ drive backpanel, via a
**50-pin computer back-panel connector**, a **24-conductor interconnect cable**
(DC pairs crossed 1↔2, 3↔4, 5↔6), and the 26-pin drive backpanel connector.

Signals crossing #1↔#2 (partial, legible names): `WDS`, `CD`, `DE`, `DCL`,
`SDS`, `INT`, `MH`, `ENWD`, `WDE`, `HS`, `HD`, `KEY`, `READ CLEAR`, plus the
`DCCA` (data / control) group and grounds.

## Derived timing (from RC one-shots)

| One-shot | IC | RC parts | Approx. width |
|----------|----|----------|---------------|
| Read Clock | 74123 A1 (bd #1) | R16 15K / C5 430pF | ~2.0 µs |
| Read Data Window | 74123 A1 (bd #1) | R15 22K / C6 90pF | ~6.1 µs |
| I/O access strobes | address decode | — | ~500 ns |
| Sector-related one-shots | 74123 E1/F1/F4/G4 | R4 47K, R5 15K, R6 47K, R8 33K, R12 33K etc. | see sheet 1 (nominal TC `280 ±5` referenced) |
| Step / Head-settle / Motor-on delay | 74123 A1/A2/B1 (bd #2) | R1/R2/R3 10K, R8 33K, C2/C4/C6 | see sheet 1 |

Media/mechanical parameters (per project design notes, consistent with the
schematic's sector counter and RC constants): **5.25" hard-sectored, 16 sectors
/ track, 300 RPM, ~64 µs / byte**. The minidisk has **no head-load solenoid**
(no head-load control bit) and a **motor-on delay is opt-in**; the 8-inch
88-DCDD differs (32 sectors, head-load).

## General notes (from sheets)

1. All resistors in ohms, ½ W, unless otherwise specified.
2. All capacitors in µF unless noted.
3. All diodes 1N914 (signal) / 1N4004 (supply, Buffer board) unless specified.
4. `(280 ±5)` on Board #1 = nominal RC time constant reference.
5. Board #1 and Board #2 each carry their own on-board 7805 (+5 V) regulator
   fed from backpanel `+8 V`; the Buffer board carries the drive's own +5 V
   (7805) and +12 V (7812) regulators from transformer T1.
