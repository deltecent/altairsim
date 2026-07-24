# MBL — Multi Boot Loader (`builtin:mbl`)

A 256-byte ROM **paper/cassette tape** bootstrap for the Altair 8800. Unlike the
disk loaders ([`builtin:dbl`](../DBL), [`builtin:mdbl`](../MDBL),
[`builtin:cdbl`](../CDBL)), MBL loads a program from a **tape reader** — and it
reads *any* of the punched-/cassette-tape formats MITS designed, without needing
the matching first-stage loader for that particular tape.

- Reverse-engineered and commented by **Geoff Harrison**, from a hex dump
  provided by **Grant Stockly**.
- **Load address:** `FE00h` (177000 octal). The source may be reassembled to run
  at any location.
- **Decoded image:** `FE00`–`FEFF`, 256 bytes, CRC32 `5E21410E`.

> **Not `mdbl`.** `mdbl` is the **Mini**disk Boot Loader; `mbl` is the
> **Multi** (tape) Boot Loader. The names are one letter apart and the parts do
> completely different things — see [`docs/roms.md`](../../docs/roms.md) and
> GitHub issue #124.

## What it does

Because it runs from ROM (too slow to execute a tight read loop from a 1702A),
MBL first **builds a byte-read routine in RAM** tailored to the reader board the
user selects on the front panel, then:

1. Initializes just about every MITS I/O card of the era (ports `020h`–`027h`,
   plus the 2SIO at port `10h`), since it does not know which are installed.
2. Skips the leader bytes and the tape's own **stage-2 loader** — which is why
   it does not care which tape version you feed it.
3. Reads the payload load-records, verifying each record's checksum, then reads
   the entry-address (EOF) record and **jumps to the loaded program**.

### Front-panel switches

The reader device is chosen with sense-switch bits **A11–A8**:

| Value | Reader device |
|---|---|
| `0` | 2SIO, 2 stop bits |
| `1` | 2SIO, 1 stop bit |
| `2` | SIO |
| `3` | ACR (cassette) |
| `4` | 4PIO |
| `5` | PIO |
| `6` | HSR (high-speed reader) |

A value above 7 is an error. For BASIC 4.0 and later, bits **A15–A12** select the
terminal device; older tapes (e.g. BASIC 3.2) used different terminal-switch
conventions — load with the switches above, then stop, set the terminal switches
per the MITS manual, and restart at the program's entry point (`0000h` for
BASIC 3.2).

## Use it

```toml
[[board.region]]
type  = "rom"
at    = 0xFE00
mount = "builtin:mbl"
```

Put a tape reader board (an 88-2SIO/SIO/ACR/…) on the bus, set the sense
switches for it, and `RUN FE00`.

## Files here

| File | What it is |
|---|---|
| `MBL.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `MBL.ASM` | Geoff Harrison's reverse-engineered, commented source. |
| `MBL.PRN` | Assembler listing — the byte-for-byte record MBL's provenance is checked against. |

**Source:** deramp.com (`.../altair/software/roms/orginal_roms/`); a MITS ROM,
its listing reverse-engineered by Geoff Harrison from Grant Stockly's dump.
Provenance and the CRC32 test are in [`docs/roms.md`](../../docs/roms.md).
