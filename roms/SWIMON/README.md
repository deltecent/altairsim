# SWIMON — Altair 680b PROM Monitor (SWI-breakpoint variant)

The same Altair 680b System Monitor as [`MON680`](../MON680/README.md), customized so the
`SWI` instruction vectors through **`$0010`** instead of straight to the monitor's own
breakpoint handler. A user who wants SWI breakpoints sets `$0010` to `7E FF EE`
(`JMP $FFEE`) to point SWI back at the original entry point.

- **`SWIMON.S19`** — the image, verbatim from deramp.com. Same `FF00`–`FFFF` window as
  MON680, a different image, a different CRC.
- **`SWIMON.ASM` / `SWIMON.LST`** — the MITS source and assembler listing.

Embedded as **`builtin:swimon`**. The console path is identical to MON680 (the onboard
6850 ACIA at `F000`/`F001`), so it drops into `machines/altair680.toml` by changing the
monitor region's `mount` to `builtin:swimon`.

Provenance, size and CRC32 are in [`docs/roms.md`](../../docs/roms.md); the CRC is checked
by a unit test at build time. Fetched 2026-08-08 from deramp.com
(`.../altair/software/altair_680/PROM Monitor/`).
