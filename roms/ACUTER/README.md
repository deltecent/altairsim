# ACUTER — CUTER monitor for the Altair 8800 (`builtin:acuter`)

Mike Douglas's port of **CUTER** — the standalone CUTS operating system /
monitor from Processor Technology's SOLOS/CUTER family (Software Technology
Corp., 1977) — to the Altair 8800. It keeps CUTER's command set and adds Intel
HEX transfer over the Altair's serial ports.

- **Version 1.0**, 25 April 2015 — Mike Douglas, from CUTER 1.3 (77-03-27).
- **Load address:** `F000h` (a 2 KB EPROM).
- **Decoded image:** `F000`–`F7FF`, 2048 bytes, CRC32 `4A4E608D`.

## Devices

| CUTER pseudo-device | Altair hardware |
|---|---|
| 0 — Console | 1st 2SIO port |
| 1 — Serial | 2nd 2SIO port |
| 2 — Parallel | PIO port |
| Tape | the Altair ACR (CUTS cassette) |

Console output does **not** emulate the Processor Technology VDM with terminal
escape sequences — it is a plain serial console.

## Added commands

Beyond CUTER's Execute / Enter / Dump / Terminal / tape commands, ACUTER adds
Intel HEX transfer over a chosen pseudo-port:

```
HGET [pseudo-port]                 load an Intel HEX file
HSAVE start end [pseudo-port]      save an Intel HEX file
```

Commands abort with `CTRL-@`, as in CUTER.

## Use it

```
$ altairsim acuter
>
```

`machines/acuter.toml` is the built-in machine that does it — 56K (ACUTER's own
1 KB system area sits at `BC00h`), an 88-2SIO whose two halves are CUTER pseudo-ports
0 and 1, and an 88-ACR for the cassette. To put the ROM in a machine of your own:

```toml
[[board.region]]
type  = "rom"
at    = 0xF000
mount = "builtin:acuter"
```

Console on the 1st 2SIO port.

## Files here

| File | What it is |
|---|---|
| `ACUTER.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `ACUTER.ASM` | Douglas's Altair port of the CUTER 1.3 source. |
| `ACUTER.PRN` | Assembler listing — the byte-for-byte record ACUTER's provenance is checked against. |
| `CUTER Manual.pdf` | The original Processor Technology SOLOS/CUTER User's Manual, retained as downloaded. This README is distilled from it plus the Altair-specific notes in `ACUTER.ASM`. |

**Source:** deramp.com, *CUTER for Altair*. CUTER is © 1977 Software Technology
Corp. — see the note on redistribution in [`docs/roms.md`](../../docs/roms.md).
Provenance and the CRC32 test are in `docs/roms.md`.
