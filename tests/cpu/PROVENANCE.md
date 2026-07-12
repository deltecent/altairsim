# Where these came from

All files in this directory were downloaded on **2026-07-11** from

    https://altairclone.com/downloads/cpu_tests/

which Patrick explicitly authorized as a source. `README.TXT` is that
directory's own `+README.TXT`, unmodified.

These are **period test programs, not emulator source.** Reading them does not
violate the sourcing rule in DESIGN.md §0.1 — the rule forbids learning how the
hardware behaves by reading somebody else's *simulator*. These were written to
test **real silicon on a real bench**, which makes them exactly the kind of
first-hand artifact §0.1 asks for. That the assembler source is here too is a
bonus: when a suite fails, you read what it expected rather than guessing.

| File | What |
|---|---|
| `TST8080.COM` / `.ASM` | Microcosm Associates (Kelly Smith), 1980. 8080/8085 CPU diagnostic v1.0. |
| `8080PRE.COM` / `.MAC` | Bartholomew & Cringle. Preflight for the exerciser. |
| `CPUTEST.COM` | SuperSoft Associates, *Diagnostics II* v1.2, 1981. Includes a timing test. |
| `8080EXM.COM` / `.MAC` | Bartholomew & Cringle. The exerciser, with CRCs corrected for a real 8080. |
| `8080EXER.COM` / `.MAC` | The *uncorrected* exerciser. Kept only as a cross-check. |

## Use 8080EXM, not 8080EXER

They look interchangeable and are not. **8080EXER prints `ERROR` for every single
test even on correct hardware** — its built-in CRCs are wrong, and it expects you
to diff its output against a run on real silicon by hand. 8080EXM is the same
program with the CRCs corrected, so it can say PASS or FAIL and mean it. The
harness runs **EXM**. EXER is committed only so that nobody, finding EXM
surprising one day, has to go looking for it.

## Licensing

These are 40-plus-year-old diagnostics, published for free download by the
Altair Clone project and redistributed for decades across the CP/M and
retrocomputing archives. They are committed here so that the validation gate is
reproducible offline and in CI without a network fetch.

**This is Patrick's call to revisit**, in the same breath as `docs/roms.md` and
the DBL PROM in `roms/`. It is noted here rather than assumed away.
