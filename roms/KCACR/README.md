# KCACR — Altair 680b cassette loader/punch PROM

The optional PROM (socket V of the 680b main board) that loads and dumps memory over the
**KCACR** audio-cassette interface in **Motorola S-record** form. A **Motorola 6800**
program, like [MON680](../MON680/README.md), and in the same `.S19` form.

- **`KCACR.S19`** — the image, verbatim from deramp.com. A single 256-byte PROM at
  `FD00`–`FDFF`, one PROM below the monitor. It calls the MON680 console routines
  (`OUTCH` `$FF81`, `BADDR` `$FF62`, …), so it is used with `mon680` present.
- **`KCACR.ASM` / `KCACR.LST`** — Mike Douglas's July-2022 disassembly and listing, kept
  beside the image so the bytes can be reconciled against it. The listing's own equates
  name the board's two registers: `SIOSR equ $F010` (status) and `SIODR equ $F011` (data).

Embedded as **`builtin:kcacr`** and used by
[`examples/altair680/altair680-kcacr.toml`](../../examples/altair680/altair680-kcacr.toml)
alongside the `680kcacr` board. From the monitor prompt:

    .J FD00      loads an S-record tape (PLAY the cassette; ends at the S9 record)
    .J FD74      punches memory as S-records (answer the start/end address prompts)

On a load error it prints one character: `C` = checksum or non-hex character, `M` = memory
error. **Never load `0000`–`00FF`** — that is the monitor's and the loader's own work area.

Provenance, size and CRC32 are recorded in [`docs/roms.md`](../../docs/roms.md); a unit test
checks the CRC at build time. Fetched 2026-08-08 from deramp.com
(`.../altair/software/altair_680/PROM for KCACR/`).
