# Altair 680b Theory of Operation (bus signals, clock, on-board I/O map)

Source: [680 Theory of Operation.pdf](#) (MITS *Theory of Operation — altair 680b*, © MITS Inc.
1976)

The circuit-level companion to the [Operator's Manual](Altair%20680b%20Operators%20Manual.md)
and [Programming Manual](Altair%20680b%20Programming%20Manual.md): how the **Altair 680b** main
board actually works. For an emulator this is the most useful of the three main-machine manuals,
because it is the one that **names the on-board addresses the Programming Manual withheld** — the
serial **6850 ACIA at `F000`/`F001`** and the **Baudot interface at `F002`** — and it defines the
680b clock and the full 6800 bus-control signal set (`RESET`, `HALT`, `R/W`, `VMA`, `DBE`,
`R/W-P`, `BA`, `TSC`). The 680b is a **6800** machine — memory-mapped, no `IN`/`OUT`; see the
companion boards [[altairsim-88uio-board]], the [KCACR](Altair%20680b%20KCACR.md) and the
[Universal I/O Board](Altair%20680b%20Universal%20IO%20Board.md).

This is a distilled emulation reference. The manual is largely **PC-board schematics, scope
photos, IC-by-IC gate-level walk-throughs, and a troubleshooting guide** — all omitted except
where a signal, address, or timing number is software- or bus-visible. What is kept: the clock
model, the bus-control signals, the memory/PROM map, the on-board serial + Baudot address decode,
the baud generator, the bus timing numbers, and the pinouts. The gate-level "IC ZZ pins 12/13
form an astable" narration and the resistor/capacitor part numbers are not.

## 1. System overview — the 680b clock

- **500 KHz** asymmetrical, **two-phase, non-overlapping** clock at Vcc (not TTL) level. A **2 MHz**
  crystal is divided by four (a Johnson counter of two J-K flip-flops) to produce Φ1 and Φ2.
- **Φ1**: high for **600 ns**, low for **1.4 µs**. **Φ2**: high for **1 µs**. Period = **2 µs**.
- ⚠ **All data transfers take place during Φ2.** Φ2 is therefore routed system-wide to enable
  memory and the interfaces (it drives the ACIA `Enable` pin directly — see §4).
- Transistors pull the crystal-derived clock up to the MPU's logic levels; the emulator only needs
  the *rates*, not the pull-up circuit.

⚠ The stock 6800 runs at ~1 MHz elsewhere; the **680b runs its 6800 at 500 KHz** (½ speed). If you
model instruction timing in wall-clock terms, this is the machine cycle to use.

## 2. Bus-control signals (the 6800 control bus)

| Signal | Meaning in the 680b |
|---|---|
| **RESET** | A **positive edge** starts the restart sequence: the PC is loaded from the **reset vector `FFFE`/`FFFF`**, which holds the start of the System Monitor. Used at power-up and by the front-panel reset. |
| **HALT** | **High = RUN** (MPU fetches and executes). **Low = halted:** `BA` goes high, and the address, data and `R/W` lines all go to **high impedance**, which is what **enables the front-panel address/deposit functions**. |
| **R/W** | **High = READ** (data into the MPU), **low = WRITE**. Goes **high-impedance (off) when the processor is halted**. |
| **VMA** | **Valid Memory Address** — tells peripherals (e.g. the ACIA) that a stable, valid address is on the bus. Part of the on-board address decode (§4). |
| **DBE** | Data Bus Enable — the three-state control for the MPU's data-bus drivers; **Φ2 drives it**. (During a read the MPU disables its own drivers internally.) |
| **R/W-P** | **Read/Write-Prime** = NAND of `R/W` and Φ2 — a proper-level R/W that is coincident with Φ2, so data is only ever read/written **during the valid-data window**. Used to steer direction in every memory and peripheral interface. |
| **BA** | **Bus Available** — **low while running**, **high when the MPU is halted**; the high state is what hands the buses to the **front panel** for addressing and deposit. |
| **TSC** | Three-State Control — forces all address lines and `R/W` to high impedance (≈500 ns after `TSC` = 2.4 V) and drives `BA` and `VMA` low. **Used for DMA.** |

## 3. Memory and PROM map

- **On-board RAM: 1 K** (2102A statics), placed at any 1 K boundary by jumpers 1–6 (decoded by a
  74LS30 + a 4449). See the [Operator's Manual](Altair%20680b%20Operators%20Manual.md) for the
  worked base addresses.
- **PROM: four 256 × 8 devices**, selected individually (74LS04/74LS20/74LS02 decode). **PROM 1 sits
  at the highest location, `FF00`**, and holds the **680b System Monitor together with the reset and
  interrupt vectors.** So the monitor/vector PROM occupies **`FC00`–`FFFF`** (4 × 256), with the
  vectors at the very top (§5).

## 4. On-board I/O — the address decode (the emulation payload)

This is the section the Programming Manual pointed at but did not contain. Eight-input NAND gates
plus inverters form the address decode for the on-board **ACIA** and the **Baudot** interface.

### 4.1 Serial ACIA at `F000` / `F001`

- When address **`F000` or `F001`** is on the bus and stable (gated by **VMA**), the decode enables
  both chip selects of the 6850 ACIA: NAND gate LL → **`CS2`** (active-low), and inverter MM →
  **`CS1`** — both asserted simultaneously.
- **A0 is tied directly to the ACIA Register Select (`RS`).** So:

| Address | A0 / RS | Read | Write |
|---|---|---|---|
| **`F000`** | 0 | **Status** register | **Control** register |
| **`F001`** | 1 | **Receive Data** register | **Transmit Data** register |

- **MPU `R/W` → ACIA `R/W` directly.** All transfers happen on Φ2, so the **ACIA `Enable` is tied to
  Φ2**.
- **ACIA `IRQ` (active-low) → MPU `IRQ` directly** — it stays low until the interrupt cause is
  cleared and the ACIA's own interrupt-enable allows it (register model in the
  [Programming Manual](Altair%20680b%20Programming%20Manual.md), Appendix C).
- **RTS controls the paper-tape reader**: `RTS` low (active) is fed through the 20 mA loop to control
  a reader on an ASR-33 (matches the Programming Manual's `CR6=1, CR5=0` = reader-ON combination).
- **Tx/Rx data** lines wire to either a **20 mA TTY current loop** or **RS-232** (one device at a
  time), buffered through inverter A.

⚠ **This resolves the Programming Manual's "address not given" caveat for the base machine:** the
680b's *own* serial port is the ACIA at **`F000`/`F001`**. The `F010`/`F011` in the
[KCACR](Altair%20680b%20KCACR.md) and the S9-selected window in the
[UI/O board](Altair%20680b%20Universal%20IO%20Board.md) are the **plug-in** boards, at different
addresses — they do not contradict this.

### 4.2 Baud-rate generator

- A **Fairchild 34702 Programmable Bit Rate Generator** (IC Z) on a **2.4576 MHz** crystal; its
  output (pin 10) drives both the ACIA **Rx clock** (pin 3) and **Tx clock** (pin 4).
- Rate select = the four jumper holes 0–3 tied to the ground plane (**L**) or +5 V plane (**H**) —
  the **same truth table as the Operator's Manual** (50–9600 baud; ⚠ **2400 listed twice**).
- ⚠ **The generator output is 16× the indicated baud** (e.g. programmed "9600" → 153.6 KHz to the
  ACIA; "110" → 1760 Hz), matching the ACIA's ÷16 mode.

### 4.3 Baudot interface at `F002`

- When the **Baudot** option is built, **the ACIA is not used at all** — a 5-level Baudot code
  cannot go through the 7/8-bit Motorola ACIA. Serial↔parallel conversion is done **in software by
  the MPU**, clocking single bits through D-type flip-flops on the **Buffered-Data-Zero (`BD0`)** line.
- The Baudot circuitry (and the hardware-programmable-bit tri-state buffer, a 74367 for reading the
  Operator's-Manual config bits) is decoded at memory address **`F002`**, enabled on a **Read** when
  `F002` is addressed. The monitor reads it to (1) detect a terminal other than the front panel,
  (2) check the number of stop bits, (3) check for a Baudot terminal.

## 5. Interrupt & reset vectors

The vectors live at the top of the monitor PROM (§3), consistent with the other 680b references:

| Vector | Address | 680b use |
|---|---|---|
| **RESET** | `FFFE`/`FFFF` | Start of the System Monitor (loaded into PC on a reset positive edge) |
| **IRQ** | `FFF8`/`FFF9` | → `0100` in the shipped monitor (see [KCACR](Altair%20680b%20KCACR.md) / [UI/O](Altair%20680b%20Universal%20IO%20Board.md)) |

(The 6800 also has NMI at `FFFC`/`FFFD` and SWI at `FFFA`/`FFFB` in the same PROM region — standard
6800, not detailed by this manual.)

## 6. Bus timing numbers

From the timing diagram and scope photos (§III):

- 2 MHz master; MPU Φ1 high **600 ns** / low **1.4 µs**; MPU Φ2 high **1 µs**; period **2 µs**.
- Bus-vs-MPU clock skews annotated at **40 ns / 100 ns / 120 ns**; `R/W` and `R/W-P` settle within
  **~100 ns** of Φ2.
- ⚠ **Erratum (the manual's own):** the two scope photos on **page 15** are **cross-labelled** — the
  one captioned "READ/WRITE During WRITE Cycle" is actually **READ/WRITE-PRIME**, and vice-versa.
  The text and the R/W-P definition (§2) are correct; trust those, not the photo captions.

## 7. Pin configurations (§V)

The manual reproduces standard vendor pinouts — useful only as a cross-check:

- **6800 MPU** (40-pin): `HALT` 2, Φ1 3, `IRQ` 4, `VMA` 5, `NMI` 6, `BA` 7, A0–A15, D0–D7,
  `R/W` 34, `DBE` 36, Φ2 37, `TSC` 39, `RESET` 40.
- **6850 ACIA** (24-pin): Rx Data 2, Rx Clk 3, Tx Clk 4, `RTS` 5, Tx Data 6, `IRQ` 7,
  `CS0` 8, `CS2` 9, `CS1` 10, `RS` 11, `R/W` 13, `E` 14, D0–D7, `DCD` 23, `CTS` 24.
- **2102A** static RAM (16-pin), **1702A** PROM (24-pin), **34702** Bit Rate Generator (16-pin).

## 8. Emulation notes / gotchas

- **The address decode is the take-away.** Model the on-board serial ACIA at **`F000` (Control/Status)
  / `F001` (Tx/Rx Data)** and, if you ever model Baudot, the **`F002`** software-bit-banged port. This
  is the definitive answer to the Programming Manual's deliberately blank address
  [[altairsim-88uio-board]] [[serial-io-architecture]].
- **Clock the 6800 at 500 KHz**, not 1 MHz — a plausible-but-wrong clock still boots but mis-times
  everything serial [[altairsim-plausible-but-wrong-timing]] [[altairsim-real-serial-clock-inversion]].
- **`HALT` low / `BA` high is the front-panel hand-off**, and `TSC` is the DMA hand-off — both simply
  tri-state the MPU's buses. This is the 680b analogue of the 8800's bus-mastering
  [[dma-bus-mastering]] and front-panel [[altairsim-front-panel]] seams.
- **Φ2 gates every transfer**, so an emulated cycle should present the ACIA/RAM access "on Φ2"; the
  `R/W-P` (R/W ∧ Φ2) is just the guarantee that writes land only in the valid-data window — a
  bus-model detail, not guest-visible.
- **Don't add hardware this manual doesn't describe.** It documents *this* board's decode; the KCACR
  and UI/O boards bring their own ACIAs at their own addresses [[altairsim-no-invented-hardware]].
- **The page-15 caption swap is a documented erratum** — if you transcribe the timing photos, apply
  the fix (§6).

## 9. Key facts at a glance

| | |
|---|---|
| Machine | Altair **680b** (Motorola **6800**), memory-mapped, front panel + terminal |
| CPU clock | **500 KHz**, two-phase non-overlapping (2 MHz crystal ÷4); Φ1 hi 600 ns/lo 1.4 µs, Φ2 hi 1 µs, period 2 µs |
| Transfer window | **all data transfers on Φ2**; Φ2 enables memory + ACIA |
| On-board RAM | **1 K** (2102A), jumper-placed at any 1 K boundary |
| Monitor PROM | four 256×8 PROMs; **PROM 1 at `FF00`**, monitor + vectors → **`FC00`–`FFFF`** |
| **Serial ACIA** | **6850 at `F000`/`F001`** — `F000` = Control(W)/Status(R), `F001` = TxData(W)/RxData(R); A0→RS; Enable=Φ2; `R/W`→ACIA `R/W`; `IRQ`→MPU `IRQ` |
| Baud generator | **34702** on **2.4576 MHz**, 4 jumper holes select rate; ⚠ output = **16×** baud; drives ACIA Rx+Tx clk |
| Paper-tape reader | ACIA **`RTS` low → reader ON** (via 20 mA loop) |
| **Baudot** | at **`F002`**, **ACIA unused**, MPU bit-bangs 5-level via `BD0`; also the config-bit read port |
| Reset vector | **`FFFE`/`FFFF`** → System Monitor start (positive-edge RESET) |
| IRQ vector | **`FFF8`/`FFF9`** → `0100` (shipped monitor) |
| Bus signals | `RESET` `HALT`(hi=RUN) `R/W`(hi=READ) `VMA` `DBE`(=Φ2) `R/W-P`(R/W∧Φ2) `BA`(hi=halted) `TSC`(DMA) |
| ⚠ Erratum | **page-15 scope photos cross-labelled** (R/W ↔ R/W-P) — trust the text |
