# STATUS — CUTS write-path (altairsim-written WAVs fail on real hardware)

**Parked 2026-07-23, awaiting a real-hardware test by Mike.** No code change is committed;
this branch holds the investigation only.

## Where things are

- **`FINDINGS.md`** — the full analysis and the fix location/plan. Read this first.
- **`README-TEST.md`** — self-contained protocol to hand to the hardware tester.
- **`modem.py` / `analyze.py` / `measure.py`** — the reproducible tooling.
- **The candidate WAVs live OUTSIDE the repo**, in `~/altairsim-wav-hardware-test/`
  (too large to track; regenerable — see the Reproduce section of `FINDINGS.md`). A packaged
  `TRK80-Tests.zip` for the tester is in that same folder.

## The state in one line

Root cause found and measured (phase carried across cells smears the tone crossings; real
hardware decodes by transition *timing*, our reader by tone *energy*, so the defect is invisible
to our round trip). A grid-aligned modulator reproduces the real tape's signal statistics almost
exactly — **but must not be merged on lab evidence alone**, because our own reader blesses a tape
a real Sol rejects. Gate the fix on Mike's result.

## Next step when the hardware result comes back

- If control `A` fails and `D`/`E` load: port the grid-alignment into
  `src/host/tapemodem.cpp::modulate()`, add a test that asserts on **crossing-interval spread**
  (space-tone std within a few Hz), not just a byte round trip. Then drop any doc caveat.
- If it can't be confirmed soon: ship the scoped doc note from `FINDINGS.md` §Recommendation and
  let the fix wait.
