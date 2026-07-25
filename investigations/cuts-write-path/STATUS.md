# STATUS — CUTS write-path (altairsim-written WAVs fail on real hardware)

**RESOLVED 2026-07-25. Fixed on branch `fix/cuts-write-waveform` and validated against the
genuine dub on every measurable axis; a simulator-produced tape now awaits final confirmation
on Mike's real Sol-20.**

## What it turned out to be — TWO independent failure modes

The original analysis (see `FINDINGS.md`) was right about the mechanism but incomplete. Two
hardware rounds with Mike, plus measurement of every candidate, isolated two separate causes,
and a tape had to fix **both** to load:

1. **Off-grid crossings.** The old `modulate()` ran a free-running oscillator whose phase was
   carried across the mark↔space boundary, smearing ~17% of the tape's half-cycles to neither
   tone's interval. A real Sol's read path is a *transition-timing* decoder, so it misreads
   them; our energy-based reader is blind to placement and round-tripped them happily.
2. **Overdrive.** The old output was written at 0.8 of full scale — more than twice a genuine
   dub's 0.36 — which pushes a real Sol's front-end AGC/comparator out of range. This is
   invisible to *any* digital decoder (ours and the hardware-faithful model both read a hot but
   clean tape fine); it is purely an analog-front-end effect.

The hardware rounds proved each independently: at a matched 36% level a phase-carried tape
(`F`) still failed (mode 1), and a grid-aligned tape at 90% (`E`) also failed (mode 2). No
candidate before the fix had *both* right at once.

## The fix

`TapeFormat::gridToggled` (true for `cuts1200` only) selects a new generator,
`modulateGrid()` in `src/host/tapemodem.cpp`: it models U2, the flip-flop dividing the 2400 Hz
master clock (mark = one whole 1200 Hz cycle, space = one half 600 Hz cycle), places every edge
at its exact sub-sample time so crossings sit on two intervals only, rounds the edges with a
one-pole RC low-pass (`rc`, default 4000 Hz -- chosen so the tone curves like the dub, ~72% of
the time near peak, rather than sitting on flat tops), and scales to `level` (default 0.36). The 88-ACR
is untouched (its FSK is genuinely continuous, read by a PLL) except for gaining a `level`
property. Both knobs are deck properties and `tapetool encode` args.

## How certainty was reached without a Sol on the bench

`hwmodel.py` (in `~/altairsim-wav-hardware-test/`) adds a **transition-timing decoder** that
models the real CUTS read path. It reads the genuine dub `Z` at 0 errors and reproduces the
real Sol's exact symptom on the old output `F` (reads the header, then fails). The
simulator-produced tape matches `Z` on all gates: 0% off-grid crossings, 36% peak, 3rd/5th
harmonic content, energy round-trip, and the hardware-faithful decode — all EXACT.

## Remaining step

Hand Mike the simulator-produced `TRK80` WAV (+ `Z` as control). On ✅ the doc caveat about
written WAVs is fully retired. If — against the model — it still fails, `hwmodel.py`'s decoder
is the oracle: diff the exact bit it misreads on the candidate vs `Z`.
