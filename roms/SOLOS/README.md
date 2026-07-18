# SOLOS — Processor Technology's Sol-20 operating system (`builtin:solos`)

**SOLOS 1.3** (Processor Technology Corp., Emeryville CA; release 77-03-27) — the
standard **stand-alone operating system for the Sol-20** personality module, the
Sol's sibling of [`../CUTER/`](../CUTER/) (`builtin:cuter`, the S-100 CUTS monitor
for machines that are *not* a Sol-20). SOLOS drives the Sol-PC's own integrated
I/O — keyboard, VDM-1 video, serial port, parallel port, and CUTS cassette — at the
fixed hardware ports `F8h`–`FFh`.

- **Version 1.3**, 77-03-27. This is the same software revision burned into the
  MM5204 EPROM set labeled **"SOLOS 4-1"**.
- **Load address:** `C000h` (a 2 KB PROM).
- **Decoded image:** `C000`–`C7FF`, **2048 bytes** (a complete part), CRC32 `4D0AF383`.

## The Sol-PC I/O map SOLOS drives (ports `F8h`–`FFh`)

SOLOS talks to the Sol's integrated I/O directly — these are the ports the `sol`
board decodes ([`machines/sol20.toml`](../../machines/sol20.toml),
[`docs/boards/proctech-sol.md`](../../docs/boards/proctech-sol.md)):

| Port | Dir | Device | Notes |
|---|---|---|---|
| `F8h` | in | Serial status | bit 6 = RX ready, bit 7 = TX empty (active **high**) |
| `F9h` | in/out | Serial data | |
| `FAh` | in | General/tape status | bit 0 keyboard, bits 1–2 parallel (active **low**); bits 3/4/6/7 tape (active **high**) |
| `FAh` | out | Tape/baud control | bit 7/6 = cassette motors, bit 5 = 300-baud select |
| `FBh` | in/out | Tape (CUTS) data | |
| `FCh` | in | Keyboard data | ready = `FAh` bit 0, active low |
| `FDh` | in/out | Parallel (printer) data | |
| `FEh` | out | VDM display parameter (`DSTAT`) | low 4 bits = scroll / beginning-of-text line |
| `FFh` | in | Sense switches | the `fp` board; SOLOS does not read it for console select |

Pseudo-ports (SOLOS's logical device numbers, not hardware ports): `0` =
keyboard/VDM, `1` = serial, `2` = parallel, `3` = user driver — selected with
`SET I=n` / `SET O=n`. The default console is pseudo-port 0 (keyboard in, VDM out).

**Memory:** SOLOS ROM `C000`–`C7FF` (2 K) · SOLOS scratch RAM + stack `C800`–`CBFF`
(1 K, `SYSTP = CBFFh`) · VDM screen RAM `CC00`–`CFFF` (1 K).

## Use it

```
altairsim sol20
```

SOLOS cold-starts, clears the VDM-1, and prints its sign-on and `>` prompt on the
screen (with SDL3, in a window; headless, inspectable with `DUMP CC00`). Type at it
from the video window or the terminal — the Sol keyboard comes in on the `sol`
board's `keyboard` unit (default `connect = "console"`).

Or mount it into your own machine:

```toml
[[board.region]]
type  = "rom"
at    = 0xC000
mount = "builtin:solos"
```

## Files here

| File | What it is |
|---|---|
| `SOLOS13.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `SOLOS13.ASM` | The SOLOS 1.3 source. |
| `SOLOS13.PRN` | Assembler listing — the byte-for-byte record SOLOS's provenance is checked against. |
| `SOLOS-CUTER Manual.pdf` | The Processor Technology SOLOS/CUTER User's Manual, retained as downloaded. This README is distilled from it plus the `SOLOS13.ASM` header. |

**Source:** deramp.com (*Sol-20 software*). SOLOS is © 1977 Processor Technology
Corp. — see the note on redistribution in [`docs/roms.md`](../../docs/roms.md), where
the provenance and the CRC32 test are recorded.
