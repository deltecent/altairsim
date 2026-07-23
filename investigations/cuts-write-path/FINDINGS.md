# Why altairsim-written CUTS WAVs fail on real hardware — investigation

**Date:** 2026-07-22. Nothing committed. Tools built, analysis run, candidate tapes generated
for a hardware test loop. Scripts and raw files are in this folder; the reproducible commands
are at the bottom.

## TL;DR

- The **shipped example tapes are fine** — `trek80/pacman/atc/raiders` mount the genuine Philip
  Lord dubs, which real hardware reads. Only tapes altairsim **writes** (`tapetool encode`, a
  guest `SAVE` to `.WAV`) are affected.
- **Root cause found and measured:** `modulate()` carries a free-running oscillator phase across
  bit cells. Real CUTS hardware lays each tone with a flip-flop toggled by a stable master clock,
  so every zero-crossing sits on a fixed grid. Our carried phase **smears the crossings** — and
  real hardware decodes by *counting transitions per cell against a PLL clock and a fixed sub-cell
  count window*, so smeared crossings misread. Our own reader measures tone *energy*, is blind to
  the smear, and round-trips happily — which is exactly why the bug stayed invisible until a tape
  hit a real Sol.
- This is the **same class** of bug as the earlier cuts1200 octave error (also invisible to our
  reader, also only caught on real hardware).
- **A fix direction is validated in the lab** (crossings put back on the grid reproduce the real
  tape's signal statistics almost exactly) but **cannot be confirmed without a real Sol** — hence
  the candidate tapes in this folder for the reporter.

## The measurement

Same experiment throughout: take the genuine dub's **own decoded bytes** (`examples/sol/TRK80.TAP`,
7,939 B, decodes at 0 errors) and re-encode them with each modulator. Same bytes in every file, so
any difference is purely the modulation. Then measure the zero-crossing intervals — which is what
the hardware comparator actually fires on — and read off the implied mark/space tone and its spread.

```
tape                              mark mean/std       space mean/std
------------------------------------------------------------------------
REAL dub (loads on HW)         1199.4 /  15.4       601.0 /   1.7      <- the target
A  current square (SHIP)       1169.5 /  72.5       635.2 /  51.5      <- what we ship
   current sine                1169.8 /  72.6       635.1 /  51.5
D  hw square (grid)            1200.9 /  31.2       600.1 /   7.2      <- candidate fix
E  hw square + RC (grid)       1200.0 /   7.5       600.0 /   1.4      <- candidate fix (best)
```

Read the **space** column. The real tape's crossings are metronome-clean (std **1.7 Hz**). The
current modulator smears them to **51.5 Hz** *and* biases the mean the wrong way — mark pulled
**down** to 1169, space pushed **up** to 635, i.e. the two tones dragged toward each other. That
drag is the boundary artifact: at every mark↔space transition the carried phase makes the crossing
nearest the boundary land at an interval that's a blend of the two half-periods. The data is full
of such boundaries, so a large fraction of crossings are mistimed.

The fix candidates put the crossings back on the clock grid. **E matches the genuine tape almost
exactly** (space std 1.4 vs 1.7; mean dead-on) — its crossings are as clean as the real modem's.

## Why real hardware cares and our reader doesn't

From `reference/CUTS Assembly and Test.md` (read path, §5.3.4): the front end AGCs the level away,
a comparator squares the signal, a frequency-doubler emits one pulse per transition **regardless of
polarity**, and the tone decoder asks, per bit, *"did a second transition arrive before the U8
counter reached 12?"* — two transitions ⇒ logic 1, one ⇒ logic 0, gated by a PLL-regenerated
1200 Hz clock. It is a **transition-timing** decoder. The *count* per cell is right in our output
(that's why it decodes for us), but the *placement* is smeared across the cell, and placement is
precisely what the count-12 window is sensitive to. Our software reader is a matched filter on tone
energy, so placement drift is invisible to it. Classic write-path blind spot.

This also explains the reporter's exact symptom — *"reads the TRK80 header, then fails."* The header
region is dominated by the `00` padding and `01` sync (long runs of one symbol, few boundaries, so
little smear); the dense program data that follows has boundaries everywhere, smears worst, and the
first block checksum that lands on a mistimed bit fails — which makes SOLOS abandon the whole file.

## The fix, if we take it

In `src/host/tapemodem.cpp::modulate()`, the `cuts1200`/continuous-FSK path carries `phase` across
cells (deliberately, for click-free output that our own reader likes). The hardware instead aligns
every tone edge to a master-clock grid. Two ways to match it:

- **Grid-align the tones** — reset to a defined phase at each cell boundary and place the mark's
  mid-cell crossing on the grid (candidate **D**). Cheap, and already reproduces the real mean.
- **Model the flip-flop directly** — emit the divide-by-2 (mark) / divide-by-4 (space) square the
  real D/A produces, band-limited by the RC network + cassette bandwidth (candidate **E**). Most
  physically faithful; matches the real statistics best.

E is the one I'd build toward. Either way the change is confined to the write path; the reader,
the `.TAP` byte path, and every shipped example are untouched. **But do not merge a modulator
change on lab evidence alone** — our reader will bless a tape a real Sol rejects, which is how we
got here. Gate it on the hardware test (this folder) or a scope on a real modem.

Note: the candidates here were generated by a **standalone Python modem** (`modem.py`) so nothing
in the repo was touched. A real fix would port the grid-alignment into `modulate()` and add a
round-trip test — though note a round-trip test alone would *not* have caught this, so the test
should assert on **crossing-interval spread** (e.g. space-tone std under a few Hz), not just that
the bytes come back.

## Recommendation

1. **Ship an honest doc note now**, scoped precisely: reading real dubs and the shipped examples
   are unaffected and load on real hardware; `.TAP` byte images are unaffected; WAVs that altairsim
   **writes** round-trip in the simulator but are **not yet confirmed to load on a real Sol-20**.
   (Not "simulator-only by design" — it's a real, now-understood write-path defect.)
2. **Run the hardware test** in `README-TEST.md` with the reporter. If `A` fails and `D`/`E` load,
   port the winning approach into `modulate()` behind a crossing-spread test and delete the doc
   caveat.
3. If it can't be confirmed on hardware soon, the doc note stands and the fix waits — better than
   shipping a second write-path change we can't verify.

## Reproduce

```sh
# from the repo root
cmake --build build --target altair_tapetool
SP=<this-folder>

# same bytes, different modulators
./build/altair_tapetool encode examples/sol/TRK80.TAP $SP/A.wav cuts1200 44100 3 2 square
python3 modem.py hwsquare    examples/sol/TRK80.TAP $SP/D.wav
python3 modem.py hwsquare_rc examples/sol/TRK80.TAP $SP/E.wav

# each must decode back to the identical payload through the shipping reader
./build/altair_tapetool decode $SP/E.wav /tmp/rt.bin cuts1200 && cmp /tmp/rt.bin examples/sol/TRK80.TAP

# the ranking table
python3 measure.py $SP
```

`analyze.py` (full crossing-structure dump), `measure.py` (the ranking table) and `modem.py`
(the grid-aligned modem) are in this folder.
