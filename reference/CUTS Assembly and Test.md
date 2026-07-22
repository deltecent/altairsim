# Processor Technology CUTS — Computer Users Tape System

**Provenance.** Distilled from **Processor Technology Corporation, *CUTS, Computer Users Tape
System — Assembly and Test Instructions*, © 1977** (39 pp; Sections I–VI, the modulation facts
in §1.2.1, §3.4, §4.6 and the *Theory of Operation* §5.3). Corroborated by **H. Holden, "The
SOL-20 Computer's Cassette interface," December 2018** (worldphaco.com) — a scope-and-counter
teardown of the *same modem design* as it sits on the Sol-PC motherboard, which supplies the
per-bit cycle counts the assembly manual states only in passing. Where the two agree (they do
throughout), the number is a fact and not a measurement. This **supersedes** the "MEASURED, not
from a manual" table that `reference/Sol-20.md` used to carry — we now hold the manual.

CUTS is a Processor Technology **S-100 cassette module**. The Sol-20 carries the *same modem*
built onto the Sol-PC motherboard (see `reference/Sol-20.md`), driven by SOLOS at the fixed
ports `F8h`–`FFh`; a standalone CUTS board answers at a DIP-selected port, default `FAh`. The
audio format below is identical on both.

## What CUTS is (§1.2.1)

> "CUTS … operates at **300 and 1200 bps** data rates under program control. The recording
> technique used is asynchronously **Manchester coded at 1200 or 2400 Hz** and is
> CUTS/Byte/Kansas City Standard compatible."

Two program-selected speeds. **1200 bps ("high speed") is the default**; 300 bps ("low speed")
is Kansas City / Byte-standard compatible. The guest picks the rate through the status latch
(below); a power-on clear leaves it at high speed. Data rates up to 9600 baud were a hardware
design goal (pads `AA,K,L,Q,R,S,T,U,V,W,X,Y,Z` are provided) but "Processor Technology does not
recommend operation higher than 1200 Baud" (§3.4).

## The audio format — the money table

From *Theory of Operation* §5.3.3 and the **CUTS TIMING** drawings (Section VI), which label the
recorded signal **MANCHESTER ENCODED DATA**:

| | **High speed — 1200 baud (default)** | **Low speed — 300 baud (Kansas City)** |
|---|---|---|
| Logic 1 (mark) | **1 cycle of 1200 Hz** | 8 cycles of 2400 Hz |
| Logic 0 (space) | **½ cycle of 600 Hz** | 4 cycles of 1200 Hz |
| Bit cell | **833 µs** | 3.33 ms |
| Idle line (no data) | steady **1200 Hz** | steady **2400 Hz** |

Both symbols in a mode occupy one bit cell. The high-speed default is therefore an **octave below
Kansas City**: its tones are **1200/600 Hz**, not the 2400/1200 Hz of the 300-baud mode. §5.3.3
states it exactly — "in the low speed mode, four cycles of the 1200 Hz represent a '0' and eight
cycles of 2400 Hz represent a '1'. In the high speed mode, **one cycle of 1200 Hz represents a '1'
and one-half cycle of 600 Hz represents a '0'**."

The **half-cycle space** is the characteristic detail: a "0" at 1200 baud is a single half-swing
of 600 Hz (a mark-to-space transition and no return within the cell), not a whole cycle. Holden:
"PT's clever tone decoder only requires ½ a cycle of the 600 Hz tone to identify it quickly." A
tape is thus *not* a cycle-counted Kansas City signal at 1200 baud — it is Manchester-style
**hold-the-tone-for-the-cell** FSK, where a full 1200 Hz cycle and a half 600 Hz cycle happen to
fill the same 833 µs.

**Idle line = mark = pure tone.** §4.6: "Absence of data is indicated by a pure 1200 Hz or 2400 Hz
tone if recorded at 1200 bps or 300 bps respectively." The UART output idles high (= mark), so a
CUTS leader/gap is a steady mark tone — 1200 Hz on a high-speed tape.

**Framing is 8N2** (§5.3.3): a start bit (a low on the UART's TO output), then "eight data bits
and two stop bits," LSB first. This NRZ stream is what the modem modulates.

## How the tones are made — write path (§5.3.3)

- **Clocks (U10, a 7-stage counter off the ÷13 = 153.85 kHz timing chain):** Q7 = **1200 Hz**,
  Q6 = **2400 Hz**, Q5 = 4800 Hz, Q3 = 19.2 kHz, Q2 = 38.4 kHz. WRITE CLOCK = 4800 Hz (low speed)
  or 19.2 kHz (high speed).
- **Synchronizer (U3):** the UART's NRZ data is re-clocked at 1200 Hz so each bit cell starts on
  the clock edge; its complement drives the D/A stage.
- **Digital-to-audio (U2, a dual JK):** a frequency divider that divides its clock **by two when
  the write data is a "1" and by four when the data is a "0".** In high speed the U2 clock is
  2400 Hz, so a "1" → 1200 Hz (one cycle per cell) and a "0" → 600 Hz (one *half* cycle per cell,
  because the cell is only 833 µs). In low speed the clock is 4800 Hz, giving 2400 Hz / 1200 Hz.
- **Output stage:** the square wave passes through the R15/R16/R17 divider and an **A/B/C/D
  jumper** selecting one of three levels — **5 V p-p** for a digital recorder (A–D), **≈250 mV**
  for a recorder's *auxiliary* input (A–B, recommended), **≈50 mV** for a *microphone* input
  (A–C). RC filtering rounds the square edges (harmonics a tape deck would remove anyway).

## How the tones are read — read path (§5.3.4)

The read side is the harder half and is worth knowing, because it explains why archived dubs at
odd tape speeds still load on real hardware:

- **Front end:** the two recorder inputs are mixed (R3/4/6) into emitter follower Q1, then the
  U6 high-gain amp. A **FET AGC** (Q2, gate set by C29) limits the signal to ≈2 V peak regardless
  of playback level — CUTS "has absolutely no critical adjustments" (§1.2.1).
- **Comparator (U6 2nd stage):** switches on **each transition** of the audio, squaring it to a
  CMOS-level digital signal. Amplitude is now gone; only transition *times* carry data.
- **Transition detector (U19 / R20 / C22):** emits a pulse **< 1 µs per transition, regardless of
  polarity** — a frequency doubler. This is why *the polarity of the recorded ½-cycle does not
  matter* (the U2 flip-flops power up in a random but complementary phase; §Fig 2B in Holden).
- **Tone decoder (U3, U4 D-flops + U8 counter):** for each bit, it asks whether a **second
  transition pulse arrives before U8 reaches count 12**. Two transitions per cell ⇒ a full cycle
  ⇒ **logic 1**; one transition ⇒ a half cycle ⇒ **logic 0**. Real CUTS decodes by *counting
  transitions per bit*, not by measuring absolute frequency.
- **Read clock (U11 PLL + U8):** a PLL locked to the recovered data transitions regenerates a
  1200 Hz (×1) receive clock and "tracks input frequency variations … within its locking range"
  (set by VR1). **This self-clocking recovery is why a tape dubbed a few percent fast or slow
  still reads** — the receive clock follows the tape, not a fixed crystal. It is the hardware
  justification for a speed-adaptive, octave-tolerant software demodulator.
- Recovered NRZ data → UART (TMS6011, U18) → DI bus.

## Programming interface

- **Port:** default **`FAh`**, selected by the first seven positions of DIP switch S1 (one of 130
  addresses, 0–`FA`). `A0` low = status register, `A0` high = data register.
- **Status latch (U13), written on a STATUS WRITE (`A0` low):** four bits off `DO4–7` — recorder-1
  motor on/off, recorder-2 motor on/off, **low-speed select**, **high-speed select** (the two
  speed bits are complementary; power-on clear sets high speed).
- **UART status (read, `A0` low):** FE (framing error), OE (overrun error), DR (data ready),
  TBRE (transmitter buffer empty).
- **SOLOS tape structure** (unchanged by the modem): ~50 bytes of `00`, a `01` sync byte, a
  16-byte header (name / type / size / load / exec), a header checksum, then 256-byte data blocks
  each followed by a checksum. One bad byte fails a block checksum and the whole file is *invisible*
  (`CA` lists nothing). See `reference/SOLOS.md`.

## Mapping to altairsim

- **`tapeformats::cuts1200`** — high speed. `markHz = 1200`, `spaceHz = 600`, `baud = 1200`,
  8N2, **`cycleCounted = false`** (hold the tone for the cell, giving the 1-cycle mark / ½-cycle
  space this manual specifies). The idle/leader tone is 1200 Hz.
- **`tapeformats::kcs300`** — low speed / Kansas City. `markHz = 2400`, `spaceHz = 1200`,
  `baud = 300`, cycle-counted (8 / 4 cycles), idle 2400 Hz.
- **The correction this document forced.** `cuts1200` once used `2400/1200 Hz` — which are in
  fact the **300-baud KCS tones**, not the 1200-baud CUTS tones. Our decoder is octave-tolerant
  (it measures the tones present and recovers the data rate from the frame spacing, mirroring the
  hardware PLL), so it read genuine 1200/600 Hz dubs correctly and the writer's octave error stayed
  hidden — until a tape *written* by the simulator was played into a real Sol-20 and would not
  load. The genuine dubs on deramp.com all measure **1200/600 Hz** (see `reference/Sol-20.md`),
  matching this manual. `src/host/tapemodem.cpp` now emits at that octave.
- The ½-cycle-of-600 space breaks the matched-filter's integer-period orthogonality, so the
  software demodulator wants adequate oversampling: it is byte-perfect at 44.1 kHz (the archive
  standard and `modulate()`'s default) and degrades to a few framing errors at 22 kHz and below.
