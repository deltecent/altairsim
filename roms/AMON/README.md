# AMON — full-featured ROM monitor (`builtin:amon`)

Martin Eberhard's ROM-resident monitor for an Altair 8800 with an 88-2SIOJP,
an Altair 88-2SIO, or an 88-UIO. It manipulates memory, uploads and downloads
in Altair Absolute Binary and Intel HEX, programs EPROMs (via a Cromemco
Bytesaver-style memory-mapped programmer), and boots from every Altair boot
device — and it rolls Eberhard's own boot loaders in as fixed entry points.

- **Version 3.0**, 12 February 2023 — Martin Eberhard, with later fixes by
  Dietrich Hansel.
- **This build** targets a 4K-byte (2732) EPROM at `F000h`, which enables the
  extra `?` (help) and `MT` (memory test) commands. Cold start is `F800h`.
- **Decoded image:** `F000`–`FFFE`, a 4 KB EPROM span (3807 bytes programmed;
  the 288 unprogrammed bytes read back as `FF`). CRC32 `C00DC413` over the
  `FF`-filled span.

## Entry points

| Address | Function |
|---|---|
| `F800h` | Cold-start AMON, enter the command loop |
| `FC00h` | Boot from an 88-HDSK hard disk (equivalent to [`hdbl`](../HDBL)) |
| `FE00h` | Boot from Altair paper/cassette tape (equivalent to MITS's MBL) |
| `FF00h` | Boot from an 88-DCDD 8″ floppy or 88-MDS minidisk (equivalent to [`cdbl`](../CDBL), DBL, MDBL) |

## I/O and RAM

Console is 88-2SIOJP/88-2SIO **port 0**. A configurable "Transfer Port" (default
port 1, or any standard Altair serial/parallel port) is the source/destination
for the transfer commands. AMON finds the highest contiguous 256-byte RAM page
for its stack, variables, buffers, and relocated code, and prints that page's
address right after the sign-on banner.

## Commands (abbreviated)

`CO DU EN EX FI MT SE VE` (memory) · `AD AL HD HL TE TP` (transfer) · `IN OT`
(ports) · `? BO HB TT` (other). Parameters are hex; most commands abort with
Control-C (Escape too, on the 4K build). See the manual for full syntax.

## Use it

```
$ altairsim amon

AMON 3.1 by M. Eberhard
RAM: DF00
>
```

`machines/amon.toml` is the built-in machine that does it — 56K, an 88-2SIO
console at `10h`, an 88-ACR on AMON's default transfer port, and an 88-DCDD for
the `FF00h` boot entry. To put the ROM in a machine of your own:

```toml
[[board.region]]
type  = "rom"
at    = 0xF000
mount = "builtin:amon"
```

Console on an 88-2SIO port 0; cold-start at `F800h`. Give it the **whole 4 KB**
span: the boot entry points at `FC00h`, `FE00h` and `FF00h` are part of this same
image, and a ROM region that stops short of them takes them away.

## Files here

| File | What it is |
|---|---|
| `AMON.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. It ends in a run of Ctrl-Z (CP/M soft-EOF) padding, which the loader treats as end-of-file. |
| `AMON.ASM` | Eberhard/Hansel source (assembles with Digital Research ASM). |
| `AMON.PRN` | Assembler listing — the byte-for-byte record AMON's provenance is checked against. |
| `AMON Users Manual.pdf` | The manual (v3.0), retained as downloaded. This README is distilled from it. |

**Source:** deramp.com, Martin Eberhard firmware archive. Provenance and the
CRC32 test are in [`docs/roms.md`](../../docs/roms.md).
