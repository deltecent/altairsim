# MON680 — Altair 680b PROM Monitor (ACIA version 1.0)

The System Monitor that shipped in the Altair 680b's PROM. It is a **Motorola 6800**
program in **Motorola S-record** form (`MON680.S19`) — the machine, and the format, the
6800 line of this simulator was built to reach.

- **`MON680.S19`** — the image, verbatim from deramp.com. A single 256-byte PROM at
  `FF00`–`FFFF` (PROM 1, the highest, holding the monitor and the reset/interrupt vectors).
- **`MON680.ASM` / `MON680.LST`** — the MITS source and assembler listing, retained beside
  the image so the bytes can be reconciled against the listing.

Embedded as **`builtin:mon680`** and used by [`machines/altair680.toml`](../../machines/altair680.toml):

    altairsim altair680

    .

That lone `.` is the prompt. The monitor talks to the onboard 6850 ACIA at `F000`/`F001`
(the `680io` board) and reads the config straps at `F002`. Its command set is one letter
each: `J` jump, `M`/`N` examine & deposit, `L` load a Motorola-S-record paper tape,
`P` proceed from a breakpoint.

Provenance, size and CRC32 are recorded in [`docs/roms.md`](../../docs/roms.md); a unit
test checks the CRC at build time. Fetched 2026-08-08 from deramp.com
(`.../altair/software/altair_680/PROM Monitor/`).
