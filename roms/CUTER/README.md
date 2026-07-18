# CUTER — Processor Technology's CUTS monitor (`builtin:cuter`)

**CUTER 1.3** (© 1977 Software Technology Corp.) — the stand-alone CUTS
operating system / monitor from Processor Technology's SOLOS/CUTER family, the
sibling of SOLOS for S-100 machines that are **not** a Sol-20. This is the
*original* part, with its **built-in VDM-1 driver**: it puts its console on the
Processor Technology VDM-1, writing the memory-mapped screen directly. (For the
plain serial-console Altair port, see [`../ACUTER/`](../ACUTER/) — `builtin:acuter`.)

- **Version 1.3**, 77-03-27. Source restored by J. Bowman from a scanned/OCR'd
  *Processor Technology Access* newsletter listing (issue #2) and reconciled
  against the OCR'd object listing byte-for-byte.
- **Load address:** `C000h` (a 2 KB PROM).
- **Decoded image:** `C000`–`C7FA`, 2043 bytes, CRC32 `B0106ED2`.

## Console: the VDM-1, on cold start

CUTER reads the **sense switches** (port `FFh`) at cold start to pick its
console. Bits 0–1 choose the default **output** pseudo-port, bits 2–3 the
**input** one:

| CUTER pseudo-device | Hardware | Actual port(s) |
|---|---|---|
| 0 — Console out | **built-in VDM-1 driver** | screen `CC00h`, control `C8h` |
| 0 — Console in | keyboard | data port `3`, ready on status port `0` bit 0 |
| 1 — Serial | serial port | data port `1`, status port `0` |
| 2 — Parallel | parallel port | data port `2`, status port `0` |
| Tape | CUTS cassette | status `FAh`, data `FBh` |

So **sense = 0** ⇒ console is VDM out + keyboard in. That is how
[`machines/cuter.toml`](../../machines/cuter.toml) is wired.

## Use it

```
altairsim cuter
```

CUTER cold-starts, clears the VDM-1, and prints its `>` prompt on the screen
(with SDL3, in a window; headless, inspectable with `DUMP CC00`). It then waits
on the keyboard — which is a **separate board not yet built**, a step toward a
full Sol-20 machine — so it takes no input yet; `Ctrl-C` returns to the monitor.

Or mount it into your own machine (console via sense-switch selection above):

```toml
[[board.region]]
type  = "rom"
at    = 0xC000
mount = "builtin:cuter"
```

CUTER keeps its scratch and stack (`SYSTP = CBFFh`) in RAM just above the ROM,
at `C800h`–`CBFF`h; the VDM-1 owns `CC00h`–`CFFF`h.

## Files here

| File | What it is |
|---|---|
| `CUTER13.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `CUTER13.ASM` | The restored CUTER 1.3 source (CP/M ASM syntax). |
| `CUTER13.PRN` | Assembler listing — the byte-for-byte record CUTER's provenance is checked against. |
| `CUTER-SOLOS Manual.pdf` | The Processor Technology SOLOS/CUTER User's Manual, retained as downloaded. This README is distilled from it plus the `CUTER13.ASM` header. |

**Source:** deramp.com (*Sol-20 software*). CUTER is © 1977 Software Technology
Corp. — see the note on redistribution in [`docs/roms.md`](../../docs/roms.md),
where the provenance and the CRC32 test are recorded.
