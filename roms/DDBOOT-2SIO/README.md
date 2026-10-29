# DDBOOT-2SIO — Jade Double D boot PROM for an 88-2SIO console (`builtin:ddboot-2sio`)

Jade's DDBOOT PROM, with its console patched for a MITS **88-2SIO** at ports `10`/`11`. The
`jadedd` machine uses this PROM. Jade's unpatched PROM is
[`roms/DDBOOT/`](../DDBOOT/README.md) (`builtin:ddboot`). That README describes what the
PROM does, its entry points and its RAM. This file gives only the differences.

- **Run address:** `F000`.
- **Decoded image:** `F000`–`F24D`, 590 bytes, CRC32 `A017E174`. The image has 581 bytes.
  `F1E3`–`F1EB` are not programmed and read `FF`, the same as in Jade's PROM.
- **Every address is the same as in Jade's PROM.** The entry points `F000`–`F00F` are at the
  same places, so the BIOS on Jade's disks runs with this PROM unchanged.

## What the patch changes

Jade's header tells the owner to patch the console for the owner's own serial board. The
patch changes only the places that Jade marked for this: the `INIT` patch area and the console
equates.

| Item | Jade's DDBOOT | DDBOOT-2SIO |
|---|---|---|
| `INIT` (`F012`) | `NOP`s | `MVI A,03h` / `OUT 10h` / `MVI A,15h` / `OUT 10h`, then `NOP`s |
| `CNI$SP`, `CNO$SP` (status port) | `00h` | `10h` |
| `CNI$DP`, `CNO$DP` (data port) | `01h` | `11h` |
| `CNI$SB` (input ready bit) | `02h` | `01h` (RDRF) |
| `CNO$SB` (output ready bit) | `04h` | `02h` (TDRE) |
| `CNI$SI`, `CNO$SI` (invert) | `00h` | `00h` |

Jade's patch area is ten rows of four `NOP`s, `F012`–`F039`. The new `INIT` code is 8 bytes
and takes the place of the first two rows. Eight rows of `NOP`s follow it, and the code runs
through them into `BOOT` at `F03A`, as in Jade's PROM. The PROM therefore keeps its size, and
no address moves.

## The console

The console is channel A of an 88-2SIO at port `10`. The 2SIO has a 6850 ACIA.

| Port | Read | Write |
|---|---|---|
| `10` | Status. Bit 0 (`01h`) = RDRF, a byte is ready to read. Bit 1 (`02h`) = TDRE, ready to send. Both are active high. | Control. |
| `11` | The received byte. | The byte to send. |

At a cold start (`F000`), `INIT` writes `03h` to the control register, which is a master
reset. It then writes `15h`: clock divide by 16, 8 data bits, no parity, 1 stop bit, RTS low,
and no interrupts. A jump to `BOOT` (`F003`) does not run `INIT`.

## Use it

```toml
  [[board.region]]
  type  = "rom"
  at    = F000
  mount = "builtin:ddboot-2sio"
```

Add an 88-2SIO (`type = "2sio"`) at `port = 10`, with channel `a` on the console. Then type
`RUN F000`. [`machines/jadedd.toml`](../../machines/jadedd.toml) is a complete machine.

## Files here

| File | What it is |
|---|---|
| `DDBOOT2.HEX` | The image. The build embeds this file. |
| `DDBOOT2.ASM` | The patched source. It assembles with Digital Research `ASM`. |
| `DDBOOT2.PRN` | The assembler listing. `SYMBOLS LOAD` reads it. |

**Source:** `ddboot2.asm`, dated 2020-04-30, in the maintainer's Jade Double D folder. It is
Jade's `DDBOOT.ASM` with the patch above. The file here stops at the first Ctrl-Z, and Ctrl-Z
bytes fill the last 128-byte record. The text before the Ctrl-Z is the same as in that
file. DRI `ASM` 2.0, run inside altairsim on 2026-10-29, makes `DDBOOT2.HEX` and
`DDBOOT2.PRN` from the source. Both files are byte-identical to the maintainer's 2020
`DDBOOT2.HEX` and `DDBOOT2.PRN`. The CRC32 test is in [`docs/roms.md`](../../docs/roms.md).
