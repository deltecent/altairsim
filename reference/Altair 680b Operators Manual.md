# Altair 680b Operator's Manual (main board configuration & front panel)

Source: [680 Operators Manual.pdf](#) (MITS *Operator's Manual — altair 680b*, © MITS Inc.
1976)

This is the operator-facing guide to the **Altair 680b** itself — the Motorola **6800**
machine, not a plug-in board. It covers the three user-wired areas of the **main PC board**
(baud-rate generator, RAM starting address, hardware-programmable bits), the three
serial-interface build options in the on-board I/O section, the optional paper-tape-reader
control, and — the part that matters most to an emulator — **front-panel operation and the
power-up / deposit sequence**. The 680b is memory-mapped and generally active-low; see the
companion boards [[altairsim-88uio-board]] and the [Altair 680b KCACR](Altair%20680b%20KCACR.md)
/ [Altair 680b Universal I/O Board](Altair%20680b%20Universal%20IO%20Board.md) references.

This is a distilled emulation reference. Kit assembly, the physical jumper-hole geography,
component installation drawings for each interface, resistor/diode part numbers, and the
suggested Baudot isolation schematic are omitted except where they set a software- or
operator-visible value. What is kept: the baud-rate truth table, the RAM base-address scheme,
the three hardware-programmable bits, the three interface levels, the front-panel model, and
the power-up sequence.

## 1. Baud-rate generator (main-board jumpers)

If a terminal is used, its baud rate is set by **jumpers across a 4-hole field (labelled 0–3)**
to the left of IC Z on the main board. Each hole is tied either to the ground (LOW) plane or
the +5 V (HIGH) plane; the four bits select the rate-generator output at **Z pin 10**:

| 0 | 1 | 2 | 3 | Output rate |
|---|---|---|---|---|
| L | H | L | L | 50 baud |
| H | H | L | L | 75 baud |
| L | L | H | L | 134.5 baud |
| H | L | H | L | 200 baud |
| L | H | H | L | 600 baud |
| H | H | H | L | 2400 baud |
| L | L | L | H | 9600 baud |
| H | L | L | H | 4800 baud |
| L | H | L | H | 1800 baud |
| H | H | L | H | 1200 baud |
| L | L | H | H | 2400 baud |
| H | L | H | H | 300 baud |
| L | H | H | H | 150 baud |
| H | H | H | H | 110 baud |

⚠ **The table lists 2400 baud twice** (codes `HHL0` and `LLHH`), and 300 baud appears only
once — this is the manual's own printing, a quirk of the 14411-family generator's map, not a
transcription error. Reproduce the table verbatim rather than "fixing" it.

## 2. RAM starting address selection

The 680b ships with **1 K of RAM**. Its base address is set by **jumpers across pads 1–6 and
10–15** (between ICs AA and BB). Because address lines A0–A9 address the 1 K block, the
high lines **A10–A15 (or their complements) are NANDed to derive the chip-select (`CS`)**, so
the block is selected at exactly one 1 K boundary and nowhere else (addressing above the block
must produce *no* `CS`, or the RAM would alias).

To place the block at a given base, each high line that must be **1** for that base is fed to
the NAND directly, and each that must be **0** is fed **inverted** (jumper `n` → pad `1n`, the
complement output). The manual gives worked configurations for:

| Base (hex) | Decimal top | Note |
|---|---|---|
| `0000` | 0 | jumpers 1–6 → inverted pads `10`–`15` (all high lines must read as 1 when low) |
| `2000` | 8 K | A13 already high, not inverted; jumpers 1,2,3,5,6 → `10,11,12,14,15`, jumper 4 → pad 13 |
| `4000` | 16 K | |
| `5000` | 20 K | |
| `6000` | 24 K | |
| `8000` | 32 K | |

The 680b's own 1 K stays wherever these jumpers place it even when an external RAM board fills
the rest of the map; the manual's `2000` example is "keep the on-board K but start it at 8 K so
an 8 K board owns `0000`."

## 3. Hardware-programmable bits (main-board jumpers)

A second **5-hole field (labelled 1–5)** to the left of IC WW, each hole tied HIGH (+5 V) or
LOW (ground):

| Hole | Function | HIGH | LOW |
|---|---|---|---|
| **1** | **Baudot interface** | Baudot in use | no Baudot |
| **2** | **Number of stop bits** | **1** stop bit | **2** stop bits |
| **3** | reserved (future) | — | tie LOW |
| **4** | reserved (future) | — | tie LOW |
| **5** | **Terminal / No Terminal** | **no** terminal — program via the **front panel** | terminal present — program via TTY/CRT |

⚠ Holes **3 and 4 are unused** and **must be jumpered to the LOW plane** for bus noise immunity
— not left floating.

## 4. Serial interface configurations (on-board I/O section)

The boxed **"I/O"** foil area in the upper-left of the main board is built one of three ways
(component values on the manual's install drawings; not reproduced here):

- **A. ASR33/KSR33 Teletype** — **20 mA current loop** (`Q200` 2N2907 driver, `R204` 470 Ω,
  `D200`/`D201`/`D202` 1N914, etc.).
- **B. Standard RS-232** — `Q300` 2N2907, `D300`/`D301` 1N914, `R300`–`R305`.
- **C. Baudot interface driver** — `Q400` 2N2907, `R400`–`R403`. ⚠ This component set **alone
  does not make a Baudot-level interface**; it also needs an external level-converting /
  isolation circuit (a reed relay or opto-isolator — the manual suggests a 60 V supply,
  bridge rectifiers, an IL-74 opto or a TIP-146 Darlington, min current-transfer ratio 12.5×).

## 5. Paper-tape reader control (PTRC)

A separate component block labelled **"PTRC"** in the upper-left of the main board, populated
**only** if the 680b drives a Teletype fitted with a **Call/Control Unit (a controllable
reader)**: `R100` 1 K, `R101` 220 Ω, `D100`/`D101` 1N914, `Q100` 2N2907. Omit entirely for a
non-controllable reader.

## 6. Front-panel operation

- **Power-on** is shown by the **"AC" LED** at the top-left of the panel.
- The panel is **hexadecimal**: **address = 4 hex digits**, **data = 2 hex digits**. Address is
  shown on LEDs **A0–A15**, data on LEDs **D0–D7**.
- A switch (ADDRESS or DATA) **down = 0 (LED off)**, **up = 1 (LED on)**.
- In **HALT** mode the unit **continuously addresses whatever the ADDRESS switches show**;
  change the switches and the new address appears on the address LEDs and its **contents** on
  the data LEDs.
- To write, set the DATA switches and **actuate DEPOSIT** (upper-left corner of the panel); the
  new byte appears on the data LEDs.
- **RESET must be actuated before running a program** to initialize the MPU and the peripheral
  interfaces.

## 7. Power-up / deposit sequence

1. **RUN–HALT** switch → **HALT**.
2. Turn on the **rear-panel POWER** switch.
3. Actuate **RESET**.
4. Set the **ADDRESS** switches to the target location.
5. Set the **DATA** switches to the byte to deposit.
6. Actuate **DEPOSIT**.
7. Repeat 4–6 until all data is entered.
8. When the **front panel is the system I/O device**, the **No-Terminal bit (§3, hole 5 HIGH)**
   must be set — this makes the **MPU program counter start at location `0000`** when the
   machine is switched to **RUN**.

⚠ **With a terminal, this sequence does not apply** — the manual defers power-up to the *System
Monitor* manual (the monitor PROM greets the terminal and takes commands instead of the front
panel).

## 8. Emulation notes / gotchas

- **Front-panel semantics are the payload here.** Model the HALT-mode "continuously addressing
  the switch value" behavior, the 4-hex-digit address / 2-hex-digit data split, the down=0/up=1
  convention, DEPOSIT, RESET-before-RUN, and the No-Terminal → PC=`0000` rule. This is the 680b
  analogue of the 8800 front panel [[altairsim-front-panel]].
- **The config jumpers are power-on constants, not runtime state.** Baud rate, RAM base, stop
  bits, Baudot/terminal presence are all set once by jumpers; expose them as machine/board
  properties, not as guest-writable registers.
- **Reproduce the baud table verbatim**, duplicate 2400 and all — see the §1 ⚠. A "corrected"
  table is a wrong table [[altairsim-plausible-but-wrong-timing]].
- **Stop-bit and Baudot bits feed the UART framing**, matching the 6850 ACIA control model on
  the UI/O board [[serial-io-architecture]]; the operator's manual only says *where* they are
  set, not the register bits.
- **RAM base uses NAND-of-(A10..A15-or-complement).** If you model the address decode literally,
  the jumper pattern is "direct for lines that must be 1, complement pad for lines that must be
  0"; the block never aliases above itself.

## 9. Key facts at a glance

| | |
|---|---|
| Machine | Altair **680b** (Motorola **6800**), memory-mapped, front-panel + terminal |
| Baud generator | 4 jumpers (holes 0–3) → rate-select at IC Z pin 10; 50–9600 baud table |
| Baud table quirk | **2400 baud listed twice** (`HHL0` and `LLHH`) — reproduce as printed |
| On-board RAM | **1 K**; base set by jumpers 1–6 / pads 10–15 (NAND of A10–A15 or complements) |
| RAM bases given | `0000`, `2000` (8 K), `4000` (16 K), `5000` (20 K), `6000` (24 K), `8000` (32 K) |
| HW bit 1 | Baudot interface: HIGH = Baudot, LOW = none |
| HW bit 2 | Stop bits: HIGH = 1, LOW = 2 |
| HW bit 5 | Terminal: HIGH = **no** terminal (front-panel programming), LOW = terminal |
| HW bits 3,4 | reserved — **must tie LOW** |
| Interface options | ASR33/KSR33 **20 mA loop** · **RS-232** · **Baudot** (needs external isolation) |
| PTRC | paper-tape-reader control block — only for a Teletype with a Call/Control Unit |
| Front panel | address = **4 hex digits** (A0–A15), data = **2 hex digits** (D0–D7); down=0/up=1 |
| Panel controls | HALT/RUN, RESET (before run), DEPOSIT (upper-left), "AC" power LED |
| Power-up | HALT → POWER → RESET → set ADDRESS/DATA → DEPOSIT; No-Terminal bit → PC starts `0000` |
