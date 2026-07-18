# VDM1DEMO — VDM-1 banner demo

A 52-byte 8080 program that writes `PROCESSOR TECHNOLOGY VDM-1  READY` into a
Processor Technology VDM-1's memory-mapped screen RAM at `0CC00H` and halts. It is
the smallest thing that shows the [VDM-1](../../docs/boards/proctech-vdm1.md) is in
the machine and rendering: no console, no monitor — text appears because the CPU
*writes memory*, which is what a memory-mapped video terminal is.

- **Source:** [`VDM1DEMO.ASM`](VDM1DEMO.ASM) — origin `0F800H`.
- **Image:** [`VDM1DEMO.HEX`](VDM1DEMO.HEX) — Intel HEX, embedded as `builtin:vdm1demo`.
- **Used by:** [`machines/vdm1.toml`](../../machines/vdm1.toml) (`RUN F800` at startup).

This is **ours**, not a period ROM — a worked example built for the VDM-1 board,
in the same spirit as the Host Bridge utilities. See
[`reference/Processor Technology VDM-1.md`](../../reference/Processor%20Technology%20VDM-1.md)
for the hardware it drives.

Run it:

```
altairsim vdm1
```
