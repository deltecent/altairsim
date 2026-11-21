# Votrax SC-01 — the chip's internals (US Patent 4,433,210)

Source: [Votrax SC-01 patent US4433210.pdf](#)

US Patent **4,433,210**, *Integrated Circuit Phoneme-Based Speech Synthesizer*. The inventors are
Carl L. Ostrowski and Bertram White, and the assignee is **Federal Screw Works** (Detroit), the
company that owned Votrax. It was filed on 1982-04-19 as a continuation of Ser. No. 156,483, which
was filed **1980-06-04**, and it issued on 1984-02-21. The PDF has 22 pages: a cover page, 10
drawing sheets (Figs. 1–14), and 11 columns of text. The patent never names the SC-01. **Fig. 1
shows that it is the SC-01** all the same: it uses the chip's own pin names (P0–P5, Strobe,
A/R, I1/I2, MCRC/MCX, an analog output), it has 64 phonemes from a 6-bit code, and it has two
inflection bits. It also has the same parts that the datasheet lists (an on-chip master clock,
an RC or external clock, automatic inflection, and a four-filter vocal tract).

The pins, the timing and the phoneme chart are in
[PAiA SS-100 Speech Synthesizer](PAiA%20SS-100%20Speech%20Synthesizer.md) (the datasheet).
This file is **what is inside the chip**: the parameter ROM, the timing chain, the transitions,
the glottal source, the noise source and the switched-capacitor vocal tract.

**What the patent does NOT give.** It has **no ROM contents**: Fig. 12 shows the layout but no
values. It has **no glottal pulse levels**: they are trimpots in Fig. 6. It has no op-amp gains,
no numeric pitch range in Hz, and no taps for the noise register beyond the pins drawn. The
patent describes the 1980 design, and the production die can differ from it (see the ⚠ list).

**Status:** this is the source for the `VotraxSc01` chip model (`src/chips/votrax-sc01`), together
with the decap ROM image (see `docs/sources.md`).

## 1. Quick reference for emulation

| Item | Value (patent's preferred embodiment) | Where |
|---|---|---|
| Command word | 8 bits: P0–P5 phoneme (64), I1–I2 inflection | col. 3 l. 17–21 |
| Master clock | **1.28 MHz** (internal RC on MCRC, or external on MCX) | col. 8 l. 61–64 |
| Ripple counter | 10 bits, clocked by the master clock | col. 8 l. 60–63 |
| Parameter selects A0/A1/A2 | ripple Q4/Q5/Q6 = **40 / 20 / 10 kHz** | col. 7 l. 49–51 |
| φ1/φ2 (switched-capacitor clocks) | two-phase, non-overlapping, **20 kHz** | col. 8 l. 30–34, Fig. 14 |
| P1/P2 (sub-phoneme clocks) | two-phase, **5 kHz** | col. 8 l. 41–47 |
| ROM | 64 phonemes × 8 parameter slots × 8 bits (= **512 bytes**) | Fig. 12 |
| Parameters | 12: F1 F2 FC F3 F2Q VA FA (4 bits each, transitioned); CL CLD VD PAC (on/off or timing); phoneme timing (**7 bits**) | col. 7 l. 35–48 |
| Phoneme length | the phoneme counter counts **16** ticks of the sub-phoneme clock; the timing parameter sets that clock's rate | col. 9 l. 35–41 |
| Transition | new = current − current/8 + target/8; **~70% of the way in 33 ms** | col. 12–13 |
| F2 during transition | 5 bits (an extra ½-LSB) | col. 14 l. 31–40 |
| Vocal tract | F1 → F2 → F3 → F5 (F5 fixed), switched-capacitor, all at 20 kHz | Figs. 4, 5 |
| Glottal pulse | 16 d.c. levels, one each **0.2 ms** (5 kHz), then held | col. 16 l. 1–15 |
| Pitch | an 8-bit jam counter at 20 kHz, preset from I1/I2 and **inverted F1** | col. 14–15 |
| Noise | an 18-stage shift register (CD4006) with a jam counter, clocked by P1 (5 kHz) | col. 16 l. 55–60, Fig. 8 |
| Switched-capacitor law | R = 1 / (F·C); F = (F_switch / 2π) · (Cr / Cf) | col. 17–18, Fig. 13 |

## 2. The interface, as Fig. 1/2 draws it

- **Strobe** latches P0–P5 into a 6-bit latch (26). The latch drives ROM address **A8–A3**.
  The same strobe **resets output latch 28, so A/R goes LO** ("acknowledges receipt of the
  new data"). A/R LO also initializes the phoneme timing counter (col. 3 l. 22–31; col. 5
  l. 60–68).
- **A/R goes HI** when the phoneme counter reaches its end count. NOR-gate 90 sees 0000 on
  the counter, flip-flop 92 is set, and **two P2 pulses later** NAND 94 sets latch 28
  (col. 9 l. 35–45).
- I1/I2 go straight to the glottal source. They are not latched with the phoneme in Fig. 1,
  which matches the datasheet's "instantaneously sets pitch level".
- The datasheet (not the patent) gives the A/R and STB timing (Table 3).

## 3. The parameter ROM (Fig. 12, Fig. 11)

The address is **A8–A3 = P5–P0** and **A2–A0 = the parameter select**, which comes from ripple counter
Q4–Q6. Each address gives two nibbles:

| A2 A1 A0 | D7–D4 (high nibble, transitioned) | D3–D0 (low nibble, not transitioned) |
|:--:|---|---|
| 000 | F1 | — |
| 001 | F2 | — |
| 010 | FC | — |
| 011 | F3 | **CL** (closure, on D3 only) |
| 100 | F2Q | **CLD** (closure delay) |
| 101 | VA | **VD** (vocal delay) |
| 110 | FA | **PAC** (pause, on D3 only) |
| 111 | **phoneme timing, 7 bits on D6–D0** | |

The text confirms 011 → CL/F3, 100 → CLD/F2Q and 111 → timing (col. 7 l. 40–48;
col. 8 l. 64 – col. 9 l. 5). For 101 → VD and 110 → PAC, see col. 10 l. 1–3 and col. 11
l. 34–40. Each parameter is read ≥ ~500 times per phoneme (col. 7 l. 51–58).

**Meanings** (col. 6 – col. 7):
- **F1, F2, F3**: the resonant frequencies of the three tunable filters. **F5 is fixed.**
- **FC**: the fricative control. It sends noise into **F2** (with FC) and into **F5** (with the
  inverse, FC̄). One parameter replaces the usual fricative frequency and low-pass pair.
- **F2Q**: the Q (bandwidth) of F2. It is lowered for the nasals n, m, ng.
- **VA / FA**: the voice amplitude and the fricative (noise) amplitude.
- **VD**: delays the change to VA for part of the phoneme (fricative→vowel).
- **CLD**: delays the change to CL and FA for part of the phoneme (vowel→fricative).
- **CL**: closure. An abrupt amplitude change at the output, for stops ("b" in "be").
- **PAC**: pause. **Freezes the formants** until all excitation has died away, or until the pause
  ends, whichever comes first. This lets a nasal ending decay as "n" and not as a vowel.

## 4. Timing chain (Fig. 2A)

- Ripple counter 34 (10 bits): Q0–Q3 → latch 70, and Q4–Q6 + Q9 → latch 72.
- Latch 70 makes **BIT0–BIT2**, the bit clocks of the serial transition arithmetic. Its
  remaining output feeds flip-flop 74, which makes **B1** at twice the BIT0 rate.
- Latch 72 makes **A0–A2** (the parameter selects, slightly delayed from the ROM's) and a
  **1.25K** clock. That clock is ripple Q9 = 1.28 MHz / 1024 = **1.25 kHz**, and Fig. 3A takes it
  into the transition/freeze circuit.
- Q3 → R-S flip-flops 77/78 → **φ1/φ2** (20 kHz). Q5 → flip-flops 75/76 → **P1/P2** (5 kHz).

## 5. Phoneme timing, delays and closure (Figs. 2A, 2B, 7)

- **Sub-phoneme clock 42 (Fig. 7):** an RC oscillator whose R is a switched-capacitor bank,
  set by **PH0–PH6** (the 7-bit timing parameter, latched in 80 when A2–A0 = 111). The
  bank is **5, 11, 21, 43, 86, 173, 343 pF** (PH0 → PH6, binary-weighted), and it is switched by
  P1/P2 at **5 kHz**, so **R = 1 / (5 kHz × C79)** (col. 9 l. 20–28). The oscillator's
  timing capacitor is **1000 pF**. Its divider is 5K / 9K / 1K / 5K, as drawn.
  A larger timing value means more capacitance, a smaller R and a **faster** clock, so a
  **shorter** phoneme. (This is our reading of the circuit; the text does not state the direction.)
- **Phoneme counter 84** counts sub-phoneme clocks. The phoneme ends when Q0–Q3 = 0000 again
  (**16 counts**), and then A/R goes HI (§2).
- **VD:** comparator 82 compares the counter (Q0–Q3) with the D0–D3 nibble. When A2–A0 = 101
  and they match, flip-flop 112 is set and the **vocal delay ends**. Until then the VA transition
  is frozen. **CLD** does the same at 100 with flip-flop 114: it freezes **FA and CL**.
  Both flip-flops reset at the end of each phoneme (col. 9 l. 46 – col. 10 l. 28).
- **CL:** at 011, D3 is the closure bit. Flip-flop 132 makes the closure signal once the
  closure delay has ended. **A closure is also made whenever VA = 0 and FA = 0** (NOR 190 and 192,
  NAND 134). This silences the tract when there is no excitation (col. 10 l. 34 – col. 11 l. 20).
- **PAC:** at 110, D3 sets flip-flop 142. The output, **FF = PAC · (VA + FA)**, freezes F1, F2, FC,
  F3 and F2Q. It is released when VA and FA both reach zero, or when the pause phoneme
  ends (col. 11 l. 21 – col. 12 l. 4).

## 6. The digital transition (Fig. 3A/3B)

- Serial arithmetic, one parameter at a time. The ROM's high nibble T0–T3, shifted down 3 bits
  through mux 150, gives **target/8**. The current value from RAMs 164/166, shifted down 3 bits
  through mux 154 and inverted, gives **−current/8**. Adders 156 and 158 form
  **(Current − ⅛ Current) + ⅛ Target = New Current** (col. 12 l. 9 – col. 13 l. 1).
- The new value is written back to RAMs 164/166 (addressed by A0–A2) through the φ2 write
  strobe. The write is **inhibited** (that is, frozen) for:
  - **FF** (freeze formants), on the slots 000–100 (F1, F2, FC, F3, F2Q)
  - **VD**, on slot 101 (VA)
  - **CLD**, on slot 110 (FA)

  (col. 13 l. 17–68)
- The text says the rate gives "approximately 70% of its new target position in 33 msec"
  at a 20 kHz φ2 (col. 13 l. 43–46). The arithmetic: 1 − (7/8)ⁿ = 0.70 gives n ≈ 9 steps, so
  **one step about every 3.7 ms**, or about 270 Hz. The text does not say which clock sets the
  step rate. Fig. 3A takes the 1.25 kHz clock into a counter and the freeze gating. ⚠ **The exact
  step rate is not settled by the patent.**
- **F2 carries 5 bits** during the transition (the ½-LSB of the adder output, "for F2 only" in
  Fig. 3B), because F2's span is about twice F1's (col. 14 l. 31–40; col. 19 l. 15–35).
- Output latches 194–200 (4042) demultiplex F1, F2, FC/FC̄, F3, F2Q, VA and FA. **F1, F2, F3 and F2Q
  are clocked only at the glottal sync pulse** (the start of a glottal period), so that the CMOS
  switches do not click while the tract rings with no excitation. FC, VA and FA update at once
  (col. 14 l. 14–31; col. 16 l. 39–54).

## 7. Glottal source (Figs. 6, 10)

- **Period:** cascaded 4-bit jam counters 220 + 222 count from a preset up to all ones, at
  **φ2 = 20 kHz**. At the carry they reload and start a new pulse.
  - Counter 222 L1–L3 ← **inverse of F10–F12** (the three LSBs of the transitioned F1).
  - Counter 220 L0–L2 ← **inverse of F13**, plus **I1, I2**.
  - So the inflection bits sit **higher** in the preset than F1. The pitch follows I1/I2
    strongly and F1 weakly and inversely: **a lower F1 gives a higher pitch**. This is the
    "automatic inflection" (col. 14 l. 41 – col. 15 l. 15).
  - Our reading: period = (256 − preset) / 20 kHz. The patent gives no formula and no Hz range.
- **FGATE:** a third jam counter, 224, is loaded with the same preset but clocked by **A0
  (40 kHz, twice as fast)**. Its carry sets R-S flip-flop 226 **halfway through the period**.
  FGATE is LO for the first half and HI for the second half (col. 15 l. 16–30).
- **Shape:** counter 234, clocked at **5 kHz** (φ2 ÷ 4 through counter 236), steps two 8-to-1
  analog muxes (4051, 242/244) through **16 preset d.c. levels**, each held **0.2 ms**. At
  count 1111 the counter stops itself (TC → EP), so **the last level is held for the rest of
  the period**. The carry of counter 220 clears counters 234/236 and starts the next pulse
  (col. 15 l. 31 – col. 16 l. 38). The text says that an **8-segment** pulse (one mux) was
  adequate. Fig. 10 sketches it: a jump up, then a falling staircase, then a low level.
- **The level values are trimpots to Vp and are not given.**
- **Glottal sync** (NOR 240) = counter 234 at 0000, at the start of each pulse.

## 8. Noise source and fricative path (Fig. 8)

- **White noise generator 64:** an **18-stage static shift register 252 (CD4006)** and a **jam
  counter 250**, clocked by **P1 (5 kHz)** through NOR 254. The other input of NOR 254 is **FA̅**,
  so the generator **stops when FA = 0** (col. 16 l. 55–64).
- Feedback, as drawn: the 4006 outputs at pins **13, 12, 15, 4, 9, 8** go through two
  gates into an XOR. That result goes into a second XOR with a NOR of jam-counter outputs **Q2, Q3**,
  and back into the register's input (pin 6). The noise output is pin 10/1.
  ⚠ **The gate types are hard to read in the scan, and the pins must be mapped to stage numbers
  through the CD4006 pinout.** This is not settled here.
- **Voice-fricative gating:** NAND 258 passes the noise only when OR 260 allows it. OR 260 has
  **FGATE** and **VA̅** as inputs. So, when a phoneme is both voiced and fricative (z, v), **the noise
  is on only in the second half of each glottal period** (col. 17 l. 1–10).
- **Fricative amplitude 260:** a switched bank of **27, 53, 107, 213 pF** (FA0 → FA3), driven
  through 100K / 2K from the gated noise.
- **High-pass noise shaping 262:** a switched-capacitor stage with an integrator capacitor of
  **400 pF**. The capacitors drawn around it read **566**, **400** and **358 / 358 pF** ⚠ (the scan is poor).
  The output is stepped by φ1/φ2 and injected through the **FC** and **FC̄** banks.

## 9. Vocal tract (Figs. 4, 5, 9; values in pF)

Every switched capacitor works at **20 kHz**. Fig. 9 draws Fig. 4 again with each switched
capacitor replaced by its resistor, R = 1 / (20 kHz · C). **Every pair checks within about 3%**
(for example, 24 pF ↔ 2 MΩ, and 270 pF ↔ 185 kΩ). This is how the bank assignments below were confirmed.

| Block | Fixed parts | Tuning bank (bit 0 → MSB) | Fig. 9 equivalents |
|---|---|---|---|
| **VA** (voice amplitude, 62) | 400 feedback | **27, 53, 107, 213** (VA0–VA3) | 1.8M, 950K, 470K, 235K |
| **F1** | 253 input; 270 switched; 1080 ×2 integrators; **20** feedback (= 2.5 MΩ) | **16.4, 33, 66, 130** (F10–F13) + **24** fixed | 3M, 1.5M, 750K, 385K + 2M fixed; 185K for the 270 |
| **F2** | 400 coupling; 470 switched (= 106K); 620 ×2 integrators | **14, 28, 56, 113, 226** (F20–F24, 5 bits) + **46** fixed | 3.6M, 1.8M, 900K, 450K, 225K + 1.1M |
| **F2Q** (in F2) | **20** fixed | **23, 46, 93, 186** (F2Q0–F2Q3) | 2.2M, 1.1M, 540K, 270K + 2.5M |
| **FC** (noise → F2) | — | **19, 38, 76, 152** (FC0–FC3) | 2.6M, 1.3M, 650K, 325K |
| **F3** (Fig. 5) | 390 input switched; 420 ×2 integrators; **20** feedback | **21, 42, 84, 168** (F30–F33) + **76** fixed | — |
| **FC̄** (noise → F5) | **115** fixed ⚠ | **24, 48, 96, 192** (FC̄0–FC̄3) | — |
| **F5** (fixed) | 470 switched ×2; **30** feedback; 338 ×2 integrators | none | — |
| Output | a switched stage into a follower "to closure circuit" (no values) | | |

- Each tunable filter is a two-integrator loop. The bank sets the **ratio Cr / Cf**, and so the
  frequency: **F = (F_switch / 2π) · (Cr / Cf)**. The patent's example is Cr = 1 pF and
  Cf = 3.183 pF at 20 kHz, which gives 1000 Hz (col. 18 l. 19–36).
- F1 has 4 bits and F2 has 5 bits (bit 4 is the transition's ½-LSB), because "the frequency span
  of the F2 resonant filter is approximately twice that of the F1".
- The bank values are given; the frequency in Hz for each step is not. Working it out means
  solving each two-integrator loop with these ratios. That belongs in the model, not in this file.
- VR is a 1K/1K divider from Vp (Fig. 5): the analog ground is Vp/2.

## 10. Claims (summary)

There are 12 claims. They cover the ⅛-difference transition (claims 1–3, "approximately equal to 12.5%"),
the mux-of-d.c.-levels glottal source with counter timing (claims 4–6, 8–10), the transition synced
to the glottal pulse (claims 7, 12), and the binary-weighted switched-capacitor filters (claims 11, 12).

## Quirks worth carrying

- ⚠ **The clock is 1.28 MHz here and 720 kHz in the datasheet.** The patent's 20 kHz φ and
  40/20/10 kHz selects come from 1.28 MHz. At the datasheet's 720 kHz, the same dividers give
  **11.25 kHz** φ. The datasheet clock wins for the shipped chip. **The die settles it:** the
  production chip divides the master clock by 36 for φ (and by 18 for the analog samples), so
  at 720 kHz φ is **20 kHz**, as the patent says. The patent's absolute rates hold at the
  datasheet clock; only the divider changed.
- ⚠ **The tap outputs of the ripple counter do not all divide as the text says.** From 1.28 MHz,
  Q4/Q5/Q6 give 40/20/10 kHz. That is right for the selects. But the text puts the 20 kHz φ on Q3 (which would
  be 80 kHz) and the 5 kHz P on Q5 (which would be 20 kHz). The latch and flip-flop stages after them
  (Fig. 2A) may divide again. The stated **frequencies** are used here, not the tap numbers.
- ⚠ **The patent is the 1980 design, not necessarily the production die.** The decapped
  SC-01A (2007) is the authority for the shipped chip's ROM and for any capacitor value that
  the die shows differently. Where they disagree, `src/chips/votrax-sc01` says which one it uses.
- ⚠ **The 115 pF fixed capacitor in the FC̄ bank** reads "11S" in the scan. It is not binary-weighted with
  its bank (as are the 24 / 20 / 46 / 76 fixed parts of the other banks).
- ⚠ **The ROM layout in Fig. 12 is logical.** The 512-byte decap image stores the bits in
  the die's own order, not as nibble pairs in this table.
- The patent cites Brodersen, Gray and Hodges, *MOS Switched-Capacitor Filters* (Proc. IEEE,
  1979), as the filter technique. It also cites earlier Votrax patents: Gagnon 3,836,717 and 3,908,085,
  Dorais 4,128,737, and Ostrowski 4,130,730.
