# PAiA SS-100 Speech Synthesizer

Source: [PAiA SS-100 Speech Synthesizer.pdf](#) — PAiA Electronics *SS-100 Speech Synthesizer
Software* booklet (SS100-L), © 1982 PAiA Electronics, Inc., Oklahoma City, OK. Reprints "This Is
Your Computer Speaking" by Randy Carlstrom (*Popular Electronics*, Sept/Oct 1982), the three
SS-100 program listings, and the **Votrax SC-01 Speech Synthesizer Data Sheet** (© 1982
Votrax/PAiA). Provenance in `docs/sources.md`.

The **PAiA SS-100** is an S-100 bus board that carries a **Votrax SC-01** single-chip phoneme
speech synthesizer, plus the bus-decode, latching, interrupt, and audio-amplifier glue needed
to drive it from an 8080/Z80 host. The CPU feeds it a stream of 6-bit phoneme codes; the SC-01
turns each into continuous, unlimited-vocabulary speech. This is a distilled emulation
reference: the host-visible I/O model (two ports, one interrupt), the control word, the phoneme
table, and enough of the SC-01 chip contract to model it.

## Host interface — two I/O ports

The board decodes a block of S-100 I/O addresses set by a 4-position DIP switch (`S1`–`S4`).
Four input and four output ports are decoded; only two output ports are used, the rest reserved.
The upper 4 bits of the base address `n` are selected by the switches (all closed → `n=0`; the
vocabulary software assumes `n=3`, i.e. `S1`,`S2`,`S3` closed and `S4` open, giving base `30H`).

| Port | Dir | Name | Purpose |
|:--:|:--:|---|---|
| `30H` (`n0`) | OUT | **SPORT** — synthesizer **data** port | Latches the 6-bit phoneme code + 2 inflection (pitch) bits into the SC-01. The rising edge of the write strobes the phoneme code and pitch into the chip. |
| `31H` (`n1`) | OUT | **PPORT** — synthesizer **parameter** port | Latches the **control word** (see below): enable/interrupt, volume, mode, and master clock frequency. |

- **Data port write** = one phoneme. Bits 0–5 are the phoneme code (Table 1); bits 6–7 are the
  inflection/pitch level (`I1`,`I2` on the SC-01). The program's interrupt-service routine ORs
  the stored pitch bits into the phoneme byte before the `OUT`.
- **Parameter port write** = reconfigure the board/chip via the control word.
- The board is **automatically disabled at power-up** via the S-100 `POC` (power-on clear)
  signal; software must enable it by writing the control word with the enable bit set.

## Control word (parameter port, Fig. 5)

```
 b7      b6  b5    b4    b3 b2 b1 b0
[ENABLE][VOLUME][MODE][ MASTER CLOCK FREQ ]
```

| Bits | Field | Values |
|:--:|---|---|
| 7 | **ENABLE** | Enables/disables the synthesizer's interrupt request to the CPU. 0 = disable, 1 = enable. |
| 6,5 | **VOLUME** (output power, `XAn`) | `00` = full power, `01` = medium, `10` = low, `11` = no power. |
| 4 | **MODE** (`XE`/`XS`) | 0 = speech, 1 = sound effects. In sound-effects mode a low-pass filter (`Q3`) is switched out for better effect quality. |
| 3–0 | **MASTER CLOCK FREQ** (`XFn`) | 16 steps. `0000` = highest frequency … `1111` = lowest. Sets the SC-01 clock at pin 15 via a programmable current source (`Q1/Q2`, `R14`–`R24`), which varies voice pitch/quality and phoneme duration. |

A fixed-frequency alternate circuit (boxed in Fig. 4) can replace the programmable current
source for a stand-alone (non-variable) synthesizer; then the master-clock bits are inert.

## Interrupts

Each time the SC-01 finishes a phoneme and is ready for the next, it requests a CPU interrupt
(gated only when the enable bit is set). The board's request output (`IC11A` pin 3) is wired to
one of the S-100 vectored-interrupt lines `VI0`–`VI7` by an on-board jumper.

- The vocabulary software assumes **VI0 → RST 7 (0038H)** wiring, matching a system whose data
  bus floats high (reads `FFH` = `RST 7`) during the interrupt-acknowledge period. On a
  non-vector-interrupt system, tie the board output to the S-100 `PINT` line; requests then
  vector through `0038H` (RST 7) as with a VI0 restart.
- The ISR reads the stored parameter word and pitch, ORs pitch into the next phoneme byte, and
  writes the data port. A **STOP** phoneme (`3FH`) at end-of-word lets the ISR re-enable or
  disable further interrupts.
- Clear the enable bit (parameter port bit 7 = 0) before leaving speech, or the SC-01 keeps
  interrupting the CPU.

## Power / analog

- Requires **+5 V regulated** (`IC4`, LM340-5/LM7805) and **+12 V regulated** (`IC5`,
  LM340-12/LM7812) — derived on-board from the S-100 unregulated supplies.
- Audio amp is `IC12` (LM386); `Q4` shuts it down when the "no power" volume code is decoded.
  `R30` (10 kΩ pot) is the output volume control. A programmable 3.4 kHz low-pass filter
  (`R25`,`C7`,`Q3`) cleans phoneme-generator switching noise in speech mode and is bypassed in
  sound-effects mode. Speaker `SPKR1` is 8 Ω, 200 mW.

## Calibration

Output `xxx01000` to the parameter port (`n1`, address selected by `S1`–`S4`), connect a
frequency counter to SC-01 pin 15, and adjust `R21` (10 kΩ pot) for a **720 kHz** clock — the
SC-01's nominal frequency for standard phoneme timing. The frequency is not sacred; the user is
encouraged to experiment.

---

# Votrax SC-01 (the chip on the board)

The **Votrax SC-01** is a self-contained CMOS single-chip phoneme speech synthesizer, 22-pin
package, ~9 mA drain, wide-voltage-range, that synthesizes continuous unlimited-vocabulary
speech from a **6-bit phoneme code** at a low data rate (**70 bits/sec**, < 9 bytes/sec). It has
on-chip phoneme storage, a timing/construction algorithm, voiced and fricative sources, a
four-pole (F1–F4) filter network modeling the vocal tract, automatic inflection, an on-chip
master clock (or optional external clock), and variable voice/sound-effect modes.

## Pinout (22-pin DIP)

| Pin | Name | | Pin | Name |
|:--:|---|---|:--:|---|
| 1 | Vp | | 22 | AO (Audio Output) |
| 2 | I2 | | 21 | AF (Audio Feedback) |
| 3 | I1 | | 20 | CB (Class-B current source) |
| 4 | NC | | 19 | NC |
| 5 | TP3 | | 18 | Vg |
| 6 | TP2 | | 17 | TP1 |
| 7 | STB | | 16 | MCRC |
| 8 | A/R | | 15 | MCX |
| 9 | P5 | | 14 | P0 |
| 10 | P4 | | 13 | P1 |
| 11 | P3 | | 12 | P2 |

`NC` = no connection, `TPx` = test point (no connection).

## Signals

- **P0–P5** — 6-bit phoneme selection code (data input).
- **STB** (Strobe) — latches the phoneme code on its **rising edge**. Must remain low for 72×
  master-clock periods before the rising edge.
- **I1, I2** (Inflection level) — instantaneously set the pitch level of **voiced** phonemes
  (2-bit digital pitch).
- **A/R** (Acknowledge/Request) — goes high→low one master-clock cycle after the active STB
  edge (acknowledges receipt); goes low→high when the current phoneme times out (requests new
  data). If external phoneme timing is desired, requests can be ignored, but best speech uses
  internal timing.
- **MCRC** (Master Clock R-C) — sets internal master-clock frequency via an external R-C; pick
  values for **720 kHz** for standard timing. Tie MCRC→MCX when using the internal clock;
  ground MCRC when using an external clock.
- **MCX** (Master Clock External) — external clock input.
- **AO** — analog audio output; peak-to-peak swing up to **0.26 × Vp**, DC-biased.
- **AF** — audio feedback, for Class-A/B transistor amp stability.
- **CB** — current source for Class-B push-pull amp.
- **Vp** — supply; **Vg** — ground.

Varying the master clock varies voice and sound effects: as clock frequency decreases, audio
frequency decreases and phoneme timing lengthens.

## Timing (720 kHz clock, Table 3)

| Characteristic | Symbol | Min | Typ | Max | Unit |
|---|:--:|:--:|:--:|:--:|:--:|
| Input setup (P to STB) | Ts | 450 | | | ns |
| Input hold (P to STB) | Th | 0 | | | ns |
| STB rise (0.8→4 V) | Trs | | | 100 | ns |
| A/R width (A/R tied to STB) | Tarw | 1 | 1.3 | 2 | µs |
| STB width | Tsw | 200 | | | ns |
| Prop delay (STB→A/R after Tarw) | Tdar | | | 500 | ns |
| A/R req → STB service | Tars | 0 | | 500 | µs |
| **Phoneme duration** | Tph | 47 | 107 | 250 | ms |

Phoneme duration is clock-dependent (values above at 720 kHz).

## Amplifier notes

- **Class-A** (Fig. 11): single transistor; R,C,Rs chosen for Vp and desired level.
- **Class-B** (Fig. 12): push-pull; needs the CB current source. Formula: (β) × (Rs min) =
  81.6 × Vp. AO has an internal ~90 Ω series limiter; exceeding the formula causes distortion.
  At Vp = +12 V, Rs = 40 Ω, idle bias drain ≈ 3.5 mA (minimum power when speech inactive).
- **Output power control** (Fig. 13): a resistor/pot from speaker to ground.

## Stand-alone (no CPU)

- **Single message** (Fig. 8): counter clocked out of a ROM by A/R; system stopped on DONE.
  With A/R tied to STB, add a .01 µF cap to TP3 for power-up reset. Data at address 0 must be a
  pause phoneme.
- **Multiple message, fixed block** (Fig. 9): message-address block loaded into counter, clocked
  out by A/R; block = 2ⁿ max.
- **Multiple message, variable block** (Fig. 10): microprocessor loads phonemes into a data bus;
  A/R generates an interrupt request per new phoneme (this is how the SS-100 board works).

---

# Phoneme chart (Table 1 / Table I)

64 phonemes, 6-bit code (hex), with symbol, duration at 720 kHz (ms), and example word.

| Code | Symbol | ms | Example | | Code | Symbol | ms | Example |
|:--:|---|:--:|---|---|:--:|---|:--:|---|
| 00 | EH3 | 59 | jack**e**t | | 20 | A | 185 | d**a**y |
| 01 | EH2 | 71 | **e**nlist | | 21 | AY | 65 | d**ay** |
| 02 | EH1 | 121 | h**ea**vy | | 22 | Y1 | 80 | **y**ard |
| 03 | PA0 | 47 | *no sound* | | 23 | UH3 | 47 | mi**ss**ion |
| 04 | DT | 47 | bu**tt**er | | 24 | AH | 250 | m**o**p |
| 05 | A2 | 71 | m**a**de | | 25 | P | 103 | **p**ast |
| 06 | A1 | 103 | m**a**de | | 26 | O | 185 | c**o**ld |
| 07 | ZH | 90 | a**z**ure | | 27 | I | 185 | p**i**n |
| 08 | AH2 | 71 | h**o**nest | | 28 | U | 185 | m**o**ve |
| 09 | I3 | 55 | inhib**i**t | | 29 | Y | 103 | **a**ny |
| 0A | I2 | 80 | **i**nhibit | | 2A | T | 71 | **t**ap |
| 0B | I1 | 121 | inh**i**bit | | 2B | R | 90 | **r**ed |
| 0C | M | 103 | **m**at | | 2C | E | 185 | m**ee**t |
| 0D | N | 80 | **n**un/sun | | 2D | W | 80 | **w**in |
| 0E | B | 71 | **b**ag | | 2E | AE | 185 | d**a**d |
| 0F | V | 71 | **v**an | | 2F | AE1 | 103 | **a**fter |
| 10 | CH* | 71 | **ch**ip | | 30 | AW2 | 90 | s**a**lty |
| 11 | SH | 121 | **sh**op | | 31 | UH2 | 71 | **a**bout |
| 12 | Z | 71 | **z**oo | | 32 | UH1 | 103 | **u**ncle |
| 13 | AW1 | 146 | l**aw**ful | | 33 | UH | 185 | c**u**p |
| 14 | NG | 121 | thi**ng** | | 34 | O2 | 80 | **fo**r |
| 15 | AH1 | 146 | f**a**ther | | 35 | O3 | 121 | ab**oa**rd |
| 16 | OO1 | 103 | l**oo**king | | 36 | IU | 59 | y**o**u |
| 17 | OO | 185 | b**oo**k | | 37 | U1 | 90 | y**o**u |
| 18 | L | 103 | **l**and | | 38 | THV | 80 | **th**e |
| 19 | K | 80 | tri**ck** | | 39 | TH | 71 | **th**in |
| 1A | J* | 47 | **j**udge | | 3A | ER | 146 | b**ir**d |
| 1B | H | 71 | **h**ello | | 3B | EH | 185 | g**e**t |
| 1C | G | 71 | **g**et | | 3C | E1 | 121 | **b**e |
| 1D | F | 103 | **f**ast | | 3D | AW | 250 (253*) | c**a**ll |
| 1E | D | 55 | pai**d** | | 3E | PA1 | 185 | *no sound* |
| 1F | S | 90 | pa**ss** | | 3F | STOP | 47 | *no sound* |

`*` `/T/` must precede `/CH/` to produce the CH sound; `/D/` must precede `/J/` to produce the J
sound. `AW` (3D) duration reads 250 ms in the SS-100 booklet's Table I and 253 ms in the Votrax
datasheet's Table 1 — a print discrepancy; both refer to the same phoneme.

## Dipthongs (SC-01 datasheet Table 2)

Vowels formed as two phonemes in sequence, heard as one sound:

| Combination | Key words |
|---|---|
| A1-AY-Y | f**a**te, m**ai**d |
| AH1-EH3-Y | f**i**nd, w**i**de |
| UH3-AH2-Y | f**igh**t, wh**i**te |
| AH1-I3-UH3-L | f**i**le, sm**i**le |
| O1-UH3-Y | f**oy**, b**oy** |
| O1-I3-UH3-Y | f**oi**l, sp**oi**l |
| AH1-O2-U1 | f**ou**nd, c**ow** |
| UH3-AH2-U1 | f**ou**st, h**ou**se |
| O1-U1 | fl**oa**t, n**o**te |
| Y1-IU-U1 | f**ew**, y**ou**, m**u**sic |
| AY-I1 | f**ea**r, b**ee**r |

---

# SS-100 software (three listings in the booklet)

All three are 8080 assembly by Randy Carlstrom (CP/M port by John Simonton). The phoneme
translation tables map ASCII phoneme symbols → the codes above; the programs parse typed symbol
strings, look them up, and stream the codes to the data port under interrupt control.

### Program commands (vocabulary/development systems)

| Command | Action |
|---|---|
| `XA0`–`XA3` | Set amplitude (output power). `XFn` maps to volume field. |
| `XF1`–`XF16` | Set master clock frequency (16 steps). |
| `XI1`–`XI4` | Set inflection (pitch). |
| `XE` | Sound-effects mode. |
| `XS` | Speech mode. |
| `ESC` | Repeat the current phoneme sequence. |
| `CTRL-C` | Exit to the monitor/CP/M. |
| `CTRL-U` | (CP/M version only) copy the old input buffer to the new — build long phrases without retyping. |

Symbols and commands are separated by commas; a carriage return (`RET`) terminates the input
sequence and runs it. Parameter values persist until changed.

- Insert **PA1** (or **PA0** short pause) at the start and end of phoneme sequences for proper
  operation of the SC-01's dynamic articulation controller (maintains articulation of the first
  and last sounds). `PA0` also separates fricative/stop sounds occurring consecutively.
- Example words are developed by trial and error, adjusting phoneme choice and duration until it
  sounds right, then transferred to a ROM/lookup table for use by other programs.

### Key equates (non-CP/M vocabulary listing)

| Symbol | Value | Meaning |
|---|:--:|---|
| `RSTAD` | `38H` | Restart address for VI0 (RST 7). |
| `START` | `100H` | Program start. |
| `WIDTH` | `64` | Terminal width. |
| `SPORT` | `30H` | Synthesizer **data** port. |
| `PPORT` | `31H` | Synthesizer **parameter** port. |

The CP/M "Speech Development System" adds `INTAD = 30H` (VI6 interrupt-vector address) and moves
its interrupt-service routine into page-zero space; `WIDTH = 80`. Startup writes `0A8H` to the
parameter port to enable hardware and set XS/XF8/XA2. The **Simple Speech Output Driver** (John
Simonton) is a minimal `SPOUT` subroutine + `INTSVC` at `30H` (VI6) for quick interface
verification and application use — pass a phrase pointer in HL and the parameter byte in A.

---

## Emulation notes for altairsim

- The host contract is tiny: **two write-only OUT ports** (data `30H`, parameter `31H`) plus one
  **vectored interrupt** (jumper-selectable `VI0`–`VI7` / `PINT`), matching the uniform S-100
  I/O + interrupt model already used by other boards here.
- Model the SC-01 as a phoneme queue with a per-phoneme timer: on a data-port write, latch the
  6-bit code + 2 pitch bits; after the phoneme's duration (Table 1, scaled by the master-clock
  field of the last parameter write), raise the interrupt request (A/R low→high) if enabled.
- The base I/O address is set by a 4-bit DIP field (`n`); the booklet's software assumes `n=3`
  (`30H`/`31H`), which is the natural default.
- Audio is out of scope for a text/console-driven sim unless a Display/audio seam is used; the
  emulation value is the **timing/interrupt behavior** a guest program observes, not the sound.
- Interrupt vectors default to **RST 7 / 0038H** (VI0 on a bus-floats-high system, or via
  `PINT`); the CP/M variant uses **VI6 / 30H**.
