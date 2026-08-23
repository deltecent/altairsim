# Assembling a ROM (`.ASM` → `.HEX`)

When a built-in ROM arrives as period **source** rather than an image — an OCR'd
monitor listing, a disassembly someone posted — you have to assemble it before it
can go in `roms/<NAME>/`. Do it the way the machines did it: **run the period
assembler inside the simulator.** The result is a `.HEX` (or `.PRN` listing) that
came out of the real M80/L80, on the real 8080, not out of a modern cross-tool
that might round a corner differently.

> **Never use `cpmtools` (`cpmcp`, `cpmls`, `mkfs.cpm`) to move files on or off a
> CP/M disk.** The simulator does that for you through the **host-bridge** board.
> `cpmtools` is a separate, unrelated toolchain that is easy to reach for and wrong
> here — it bypasses the machine, and the machine is the thing under test.

## The setup

You need a CP/M machine with three things on it: the **assembler** (`M80.COM`,
`L80.COM`), the **host-bridge utilities** (`R.COM`, `W.COM`, `HDIR.COM`), and room
to work. `examples/cpm/cpm22b23-56k.dsk` carries all of them. For a large source or
many modules, mount a fresh copy of that floppy as the tool drive and an **8 MB
CP/M image** as the work drive — a floppy has only ~18 KB free after the tools.

A machine file for that, launched from the directory that holds your `.ASM` files
(the host-bridge sandbox is the launch directory):

```toml
[machine]
name = "asm"
[[board]]
type = "8080"
id   = "cpu0"
[[board]]                      # host-bridge: R/W move files host <-> CP/M
type = "hostbridge"
id   = "hb0"
[[board]]
type = "io2"                   # serial console, SSM ports 0/1
id   = "io0"
connect = "console"
# ... your disk controller + A: (8 MB work) and C: (tools floppy) ...
```

Drive it with `--mcp`, never a hand-rolled `expect` script (see
[the MCP chapter](../manual/mcp.md)). MCP does not run the machine file's
`startup>`, so boot yourself: `run {from:<boot PROM address>}`. **Stop each command
on idle, not by matching the prompt** — the CP/M prompt (`A0>`) recurs, and matching
it splits the next line you send.

## The recipe

M80 emits only a relocatable `.REL`; **L80** turns that into an Intel `.HEX`. Two
things bite:

1. **M80's default source extension is `.MAC`.** Name the source explicitly:
   `M80 MON,MON=MON.ASM`, or M80 reports `?File not found` and drops to its `*`
   prompt (where your next command becomes garbage).
2. **The source must be relocatable.** `.8080` is fine; an `ASEG` with a high
   absolute `ORG` is not — L80 can't buffer it under CP/M and dies with
   `?Out of memory`. Strip the absolute top `ORG` and let L80's `/P` set the origin.

For one module at origin `F000`:

```
R MON.ASM MON.ASM T           ; host -> CP/M  (T = text: CRLF, trims trailing ^Z)
M80 MON,MON=MON.ASM           ; -> MON.REL + MON.PRN   (objfile,lstfile=srcfile)
L80 /P:F000,MON,MON/N/X/E     ; -> MON.HEX
W MON.HEX MON.HEX T           ; CP/M -> host
W MON.PRN MON.PRN T
```

The L80 switches, in the order they must appear:

| Switch | Meaning |
|---|---|
| `/P:<addr>` | Program origin. **Must precede the module** — it sets the origin for the *next* module loaded. |
| `<module>` | Load `<module>.REL`. |
| `/N` | Name the output file after the preceding module (so `MON` → `MON.HEX`). |
| `/X` | Emit Intel **HEX** (not a `.COM`). |
| `/E` | Exit, writing the file. |

For a ROM origin above the loader (`F000`+), L80 asks
`Origin above loader memory, move anyway (Y or N)?` — answer **`Y`**. It builds in
low memory but writes the `.HEX` at the true logical origin. (Verified byte-for-byte
against the alternate route below.)

## Alternate route: DRI `ASM.COM`

For a single absolute file, Digital Research's `ASM.COM` writes the `.HEX`
**directly, no linker**: keep the absolute `ORG`, strip the M80-only `.8080`/`ASEG`,
then `ASM FILE` → `FILE.HEX` + `FILE.PRN`. No memory limit, no prompt. It is a good
cross-check — its output matched M80/L80 byte-for-byte on the SSM monitor — but note
**`ASM.COM` botches negative `DB` values** (`DB -1` assembles to `0C`, not `FF`),
so where a source uses them, **M80/L80 is authoritative.**

## Several modules into one part

A ROM built from independent modules at different origins (the SSM 8080 monitor is
`F000`/`F600`/`F700`) is **not linked** — each module assembles on its own to its
own `.HEX`, and you concatenate the images into one file. Keep **only the combined
`.HEX` in `roms/<NAME>/`**: `cmake/embed_roms.cmake` globs `*.HEX` and takes the
first, so a directory with several would embed the wrong one. Ship the module
`.ASM`/`.PRN` for provenance, but one image.

## Then wire it in

1. Drop the combined `.HEX` (and the `.ASM`/`.PRN` sources) in `roms/<NAME>/`, with
   a one-line `DESC`. The directory becomes a built-in ROM automatically.
2. Add its CRC32 row to [`docs/roms.md`](../roms.md) and a case to
   `tests/test_roms.cpp` — a built-in ROM is a hardware fact, so a mangled embed
   must fail the **build**, not a user. The test CRC is over `Image::flat()`, i.e.
   the `FF`-filled span `lo`–`hi`, so a part with gaps is `contiguous == false`.
3. Prove it runs. Mount it and boot: a monitor should reach its banner and respond
   to a command over the console.

A worked example, end to end, is `roms/SSM-8080MON/` and its
[`README.md`](../../roms/SSM-8080MON/README.md).
