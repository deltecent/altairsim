# DDBIOS — SD Systems VersaFloppy disk BIOS (`builtin:ddb200`)

The floppy **disk BIOS** for an SD Systems Z80 machine — an SBC-100 or SBC-200
single-board computer with a **VersaFloppy II** controller. It is the driver the
[MS monitor](../MSMONR21) calls to boot **SDOS / COSMOS** (and CP/M) from disk:
its cold-boot routine reads track 0, sector 1 into `0080h`, probes the diskette
to identify its format, loads the operating-system resident into the top of RAM,
and jumps to it.

- **DDBIOS version 3.3**, 24 June 1982 — Rex Brown (SDOS-COSMOS floppy driver;
  runs on SD200/700 systems). Earlier revisions split DDBIOS for the SD100/200
  (3.1, 8/80) and added track-0 format typing on boot (3.2, 8/81).
- **Load address:** `F000h` — the disk-BIOS PROM window above the monitor.
- **Decoded image:** `F000`–`F7F9`, 2042 bytes, CRC32 `D94C68DC`.

## What it does

- Drives a **VersaFloppy II** (WD1791) controller through its 8-port block at
  `60h`–`67h`: board control/drive-select latch at `63h`, FD1791 registers at
  `64h`–`67h`, and a **wait-state (PRDY) data transfer on port `67h`** — no `DRQ`
  polling. (Full port and bit maps are in the SD Systems reference set,
  `reference/SD Systems VersaFloppy.md`.)
- Auto-identifies the diskette format on boot, adjusting the controller's
  size/density bits between tries. The density tables it carries cover 8″ single
  (26×77), 8″ double (50×77), 8″ double 256-byte (26×77×256), 5¼″ single
  (18×35) and 5¼″ double (29×35).
- Presents the standard SD/CP-M-style entry vectors at the base of the PROM —
  the warm-boot/`WBOOTE` entry is at **`F003h`**, and it returns to the monitor
  at **`E003h`**. The monitor's `R`/`W`/`Z` disk commands share the base-page
  disk parameter cells at `0040h`–`004Fh` with this driver.

## Use it

```toml
[[board.region]]
type  = "rom"
at    = 0xF000
mount = "builtin:ddb200"
```

**No built-in machine uses it yet.** It is embedded ahead of the SD Systems
board work: the SBC-100/200 CPU board and the VersaFloppy controller board are
still to be built, and only then will a machine boot SDOS through this BIOS. It
ships now so the board work has a first-hand, CRC-checked image to build against.

## Files here

| File | What it is |
|---|---|
| `DDB200.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `DDB200.ASM` | Rex Brown's DDBIOS 3.3 source. |
| `DDB200.PRN` | Assembler listing — the byte-for-byte record the provenance is checked against. |

**Source:** `~/…/sd-systems/DDBIOS/DDB200-33/` (SD Systems archive; the manuals
are on deramp.com). Hardware detail is distilled in
`reference/SD Systems VersaFloppy.md`; provenance and the CRC32 test are in
[`docs/roms.md`](../../docs/roms.md).
