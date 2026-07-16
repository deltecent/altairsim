# Where these came from

Both files were downloaded on **2026-07-15** from

    https://github.com/agn453/ZEXALL

(the `master` branch), a maintained mirror of Frank Cringle's original Z80
instruction exerciser as extended by J.G. Harston and Patrik Rak.

These are **period-lineage test programs, not emulator source.** Like the 8080
suites in the parent directory, they were written to validate a Z80 against
CRCs captured from **real silicon**, which makes them exactly the kind of
first-hand artifact DESIGN.md §0.1 asks for. Reading them does not violate the
sourcing rule — that rule forbids learning how the hardware behaves by reading
somebody else's *simulator*.

| File | crc32 | What |
|---|---|---|
| `ZEXDOC.COM` | `A610193A` | Exercises every instruction, CRCs the result but **masks the undocumented flag bits** (F5/F3) and MEMPTR leaks. Validates *documented* behavior. |
| `ZEXALL.COM` | `ECF70FD6` | The same exerciser with the masks removed: it CRCs the undocumented F5/F3 bits too. The real gate. |

Each is 8704 bytes. They share 8080EXM's console convention exactly: every
subtest prints `<name>....OK` or `....ERROR **** crc expected:xxxx found:xxxx`,
and the whole run ends `Tests complete`. A single run therefore reveals *all*
failing subtests, not just the first.

## Run ZEXDOC before ZEXALL

ZEXDOC checks documented behavior only. If the core is wrong in a way that also
corrupts a documented result, ZEXDOC catches it with a smaller, clearer failure
surface. ZEXALL additionally pins the undocumented F5/F3 (YF/XF) bits, SCF/CCF
quirks and block-op flag leaks — pass ZEXDOC first, then chase the undocumented
bits with ZEXALL.

## Licensing

**These are the first GPL-2.0 artifacts in this repository.** The ZEXALL
sources in the upstream repo carry the GNU General Public License v2.0. The
`.COM` images redistributed here are builds of that GPL-2.0 source.

This repository does **not** currently carry a top-level `LICENSE` file, and
adding two GPL-2.0 test binaries is a licensing decision for Patrick to make
deliberately — it is flagged here rather than assumed away, in the same spirit
as the note at the bottom of the parent `PROVENANCE.md`.
