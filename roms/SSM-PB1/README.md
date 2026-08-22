# PB1PROG — SSM PB1 2708 EPROM programmer

The **2708 Programming Software** from the SSM PB1 instruction manual (section 4.2,
copyright Solid State Music 1978), transcribed byte-for-byte from the manual's object
listing — a real period program you run against the emulated
[PB1 board](../../docs/boards/ssm-pb1.md) to burn a 2708 and read it back out as a hex file.

It arms the board for the 2708, copies 1K from RAM at `4000H` into the programming socket
window at `0D000H`, and reads the socket once to disarm it (LED off). A 1K test pattern
(byte *N* = *N* AND `0FFH`) is assembled in at `4000H`, so the whole demo is self-contained.

- **Source:** [`PB1PROG.ASM`](PB1PROG.ASM) — origin `0100H`.
- **Image:** [`PB1PROG.HEX`](PB1PROG.HEX) — Intel HEX (program at `0100H`, source pattern at `4000H`).
- **Used by:** [`machines/pb1.toml`](../../machines/pb1.toml) and the `acceptance-pb1` test.

This is **SSM's software, not ours** — the one change from the manual is the exit: the
manual ends with `JMP MONIT` (a jump to the SSM 8080 monitor at `F021H`, documented as
"set by user"), and here that exit is a `HLT` so control returns to altairsim when the burn
is done. To burn a **2716** instead, the manual's section 4.3 changes three bytes:
`MVI A,02` / `MVI B,01` / `MVI C,07`, with a 2K source — the socket window then maps to U23.

See [`reference/SSM PB1 EPROM Programmer.md`](../../reference/SSM%20PB1%20EPROM%20Programmer.md)
for the hardware and the full set of the manual's routines (programmer, erase check, copy verify).

Run it:

```
altairsim pb1
LOAD roms/SSM-PB1/PB1PROG.HEX
RUN 100
SAVE eprom.hex D000-D3FF        # the burned 2708, as an Intel HEX file on the host
```
