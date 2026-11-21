# PAiA SS-100 speech synthesizer board

**Status:** done. The ports, the control word, the interrupt and the sound are modeled. The
sound plays when the machine has a crystal (`clock_hz` not 0). The effects-mode clock is an
estimate (see [Limitations](#limitations-and-deliberate-departures)).

## The real hardware

The PAiA SS-100 (1982) is an S-100 board that carries a **Votrax SC-01-A** phoneme speech
synthesizer. The processor writes a 6-bit phoneme code and two inflection bits. The chip says
the phoneme, then raises its A/R pin to ask for the next one. The board adds the bus decode, two
latches, a programmable master clock, a filter, a volume control, an LM386 amplifier and the
interrupt request.

The parts that matter to a program:

- **S1-S4** set A7-A4 of the base. A3 and A2 must be 0. The board decodes four output strobes
  and four input strobes from the base, and uses two of the output strobes.
- **IC6** ('244) passes data bits 0-5 to the SC-01's P0-P5.
- **IC8** latches data bits 6 and 7 into I1 and I2.
- **IC10** ('273) latches the control word.
- **IC11A** is the interrupt request: A/R AND ENABLE. A jumper takes it to one of VI0-VI7 or to
  PINT.
- **Q1/Q2** with R14-R24 are a current source that charges C5. That sets the SC-01's master
  clock. The trim pot R21 calibrates it.
- **POC** clears IC8 and IC10. RESET does not reach the board.

## Sources

| Source | Path | Authority |
|---|---|---|
| PAiA *SS-100 Speech Synthesizer Software* booklet (1982), with the *Popular Electronics* article and the Votrax datasheet | `reference/PAiA SS-100 Speech Synthesizer.md` | The ports, the control word (Fig. 5), the interrupt jumper, POC, the schematic (Figs. 2-4) and the parts list, the calibration step, the phoneme chart and lengths (Table 1), the A/R timing (Table 3). |
| Votrax SC-01 patent US 4,433,210 | `reference/Votrax SC-01 patent US4433210.md` | The chip's design: the ROM fields, the transitions, the pitch counter, the noise gate, the filters. Used by the chip model (`src/chips/votrax-sc01.*`). |
| MAME `votrax.cpp` (BSD-3, Olivier Galibert), as a reference only | `LICENSE-MAME-VOTRAX` | What only the die shows: the ROM image `sc01a.bin` and its bit order, the capacitor sizes, the glottal levels, the clock dividers and the noise register. No MAME code is copied. Each value taken from it is marked where it is used. See `docs/sources.md`. |

**Where the sources disagree.**

- The patent's clock is 1.28 MHz. The datasheet's is 720 kHz. The datasheet wins, because the
  phoneme lengths in Table 1 are at 720 kHz and the board is calibrated to it.
- The patent's capacitor values differ from the die's. The die wins, because it is the chip
  that shipped.
- Code `35` is named `O1` here. The booklet's chart prints `O3` ⚠, but its dipthong table uses
  `O1`.

## Register reference

Ports are `BASE+n`. The default `BASE` is `30`. Both ports are write-only. An `IN` from the
block is not driven, and the bus reads `FF`.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `BASE+0` (SPORT) | b0-5 the phoneme code (P0-P5). b6 I1, b7 I2, the inflection. The write strobes the chip: A/R falls, and the phoneme starts. | not driven |
| `BASE+1` (PPORT) | The control word, below. | not driven |
| `BASE+2`, `BASE+3` | decoded, no effect | not driven |

The control word:

| Bits | Field | Values |
|---|---|---|
| 7 | ENABLE | 1 lets A/R make the interrupt request. |
| 6-5 | VOLUME | `00` full, `01` medium (×0.548), `10` low (×0.154), `11` amplifier off. |
| 4 | MODE | 0 speech: the 3.45 kHz low-pass (R25/C7) is in. 1 sound effects: it is out, and the clock range drops. |
| 3-0 | XF | The master clock. `0000` is the highest, `1111` the lowest. |

The master clock from XF, at R21 mid-travel:

| Mode | XF `0000` | XF `1000` | XF `1111` |
|---|---|---|---|
| Speech | about 785 kHz | 720 kHz (calibrated) | about 663 kHz |
| Effects ⚠ | about 208 kHz | | about 87 kHz |

`SHOW <id>` prints the value for the current control word.

## How it is simulated

- **Decode.** `decodes()` claims `IoWrite` cycles for `BASE` to `BASE+3`, and no read.
- **The chip.** `VotraxSc01` (`src/chips/votrax-sc01.{h,cpp}`) is the SC-01 at its pins
  (DESIGN.md §7.8). It knows nothing of S-100. The board passes it the T-state of each strobe
  and each inflection change, and tells it the rates of the processor and the master clock with
  `setClocks()`.
- **A/R and the interrupt.** A strobe fixes when A/R will rise. The board schedules that
  T-state with `Clock::at`. When it comes, A/R goes high and `intChanged()` is called. The
  request is a level: A/R high AND ENABLE. It goes out on the `interrupt` jumper (`vi0` to
  `vi7`, or `int` for PINT). It stays up until the next strobe or until ENABLE is cleared. With
  A/R high, nothing is queued, so an idle board lets a `HLT` finish.
- **The master clock.** `masterHz()` works out the current of the Q1/Q2 source from the
  schematic, and scales it so that control `08` is 720 kHz. That is how PAiA calibrates the
  board: "output xxx01000, adjust R21 for 720 kHz". The model takes Vbe and the diode drops as
  0.65 V, and a 74LS273 low as 0.2 V. The scale is fixed with R21 at mid-travel (500 Ω). The
  `r21` property sets the pot, from 0 to 1000 Ω, for a board that was trimmed to another
  point. R21 is in the current source only in speech mode. A change of the
  control word or of the processor's rate changes the length of the phoneme in progress, from
  that T-state on.
- **The sound.** `pump()` renders the chip up to now, then applies the board's filter and
  volume. A control word written in the middle of a slice takes effect at the T-state of its
  `OUT`. The samples go to the `Audio` service as one voice. The rules for when a slice is
  played, waited on or skipped are the same as for the `Speaker` (`planSlice()` in
  `src/host/speaker.h`).
- **`properties()`:** `port` (a multiple of `10`, to `F0`), `r21` (0 to 1000 Ω, default 500)
  and `interrupt` (default `vi0`).
- **`statusLines()`:** the last phoneme and A/R, the control word with the clock in kHz, and
  the sound state (`playing`, `silent`, `silent: the machine has no crystal (clock_hz = 0)`,
  `no sound device`).
- **No DMA.**

### Reset

- `Reset::PowerOn` (POC): clears the control word and the inflection latch. ENABLE is 0, so
  there is no interrupt request. A phoneme in progress continues, as on the real board, because
  POC does not reach the SC-01.
- `Reset::Bus` (RESET): no effect. The board does not take RESET.

### Snapshots

The control word, the inflection latch and the chip's state are saved. The port, R21 and the
jumper are configuration. The sound in progress is host state and is not saved. After `RESTORE`, A/R
is read from the restored chip, its deadline is scheduled again, and the sound continues from
the restored T-state.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| POC clears ENABLE, so the board comes up with no interrupt request. | A/R is high at power-on. A board that came up enabled would interrupt before the software set its vector. |
| The request is a level, not a pulse. It stays until the next strobe. | PAiA's ISR writes the next phoneme in the ISR. A pulse would be lost if interrupts are off at the time. |
| The inflection goes to the chip at once, not with the next phoneme. | The datasheet says I1/I2 "instantaneously set the pitch level". |
| XF and the processor's rate change the phoneme length from the T-state of the change. | The speed of the speech and the time of the interrupt would be wrong for a program that changes XF while it speaks. |
| A3 and A2 must be 0, so the base is a multiple of `10`. | A base the board cannot decode. |

## Limitations and deliberate departures

- **The effects-mode clock is an estimate ⚠.** In effects mode, MODE high removes the D7/R20/R21
  branch from the current source. The model's scale comes from the 720 kHz calibration with
  R21 at mid-travel, so the effects range is about 87 to 208 kHz. A real board can be
  different. In speech mode, with R21 at mid-travel, the clock stays within about ±10% of
  720 kHz. The `r21` property moves it further.
- **The current source is a first-order model.** It takes fixed junction drops and a
  frequency in proportion to the current. It does not model the temperature or the transistors'
  gain.
- **The amplifier is a gain.** The LM386 and the speaker are not modeled. The volume settings
  are the ratios of the R26/R28/R29/R30 divider. R30 is the board's volume pot, and it is
  taken at its full 10K. Set the volume on your computer.
- **The chip's output low-pass is at about 4 kHz.** The die's capacitors put it near 150 Hz,
  and recordings of the chip do not agree with that. The 4 kHz value follows MAME's fit to
  recordings.
- **The noise gate follows the patent.** In a voiced phoneme the noise passes only in the second
  half of each glottal period (patent Fig. 8). The die's own gating bit was not used.
- **Sound needs a crystal.** At `clock_hz = 0` the chip's timing and A/R are exact, and the
  interrupt comes at the right T-state, but nothing is played.

## Verification

- `tests/test_votrax_sc01.cpp` (suite `sc01`): the ROM's CRC32, the 64 phoneme lengths against
  Table 1, the A/R timing against Table 3, the length against the clock, silence for the
  pauses, the pitch direction for I1/I2, and the same samples however the time is sliced.
- `tests/test_paia_ss100.cpp` (suite `ss100`): the decode and the `port` property, the clock
  curve and the volume gains, the `r21` pot (the clock at every XF, effects mode untouched, a
  turn of the pot during a phoneme), POC and the ENABLE gate, A/R to the T-state at XF `8` and to one
  T-state at XF `0` and `F`, the `vi6` and `int` jumpers, POC against RESET, a snapshot taken
  in a phoneme, and the sound: HELLO is played with a crystal, the samples are the same however
  the time is sliced, medium is quieter, nothing plays with the amplifier off or with no
  crystal, and a volume change is heard at the T-state of its `OUT`.

## References

- `reference/PAiA SS-100 Speech Synthesizer.md`
- `reference/Votrax SC-01 patent US4433210.md`
- `docs/sources.md`, the SC-01 sourcing note
- `docs/boards/cromemco-d7a.md`, for the `Speaker` slice rules
