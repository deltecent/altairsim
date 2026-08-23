# SSM 8080 System Monitor V1.0

The 8080 system monitor from **SSM Microcomputer Products** (formerly Solid State
Music), written by **C. E. Ohme**. This is the monitor the SSM PB1 EPROM
programmer manual calls `MONIT`: its EPROM-burner routines end with `JMP F021h`
(the monitor's warm re-entry) and its verify routines print through `CALL F009h`
(the monitor's console-out). See [`../../docs/boards/ssm-pb1.md`](../../docs/boards/ssm-pb1.md)
and [`examples/pb1`](../../examples/pb1).

- **Version 1.0** — C. E. Ohme, SSM Microcomputer Products.
- **Origin of these files:** OCR'd from the printed listing by B. Beech, April 2014.
- **Console:** serial-A on ports **0** (status) and **1** (data), the SSM IO-2
  serial board convention — status bit 0 = ready. (A parallel keyboard on ports
  2/3, a paper-tape reader/punch on 4/5, a thermal printer, and the VB1 video
  board are also driven — see the System Configuration Package below.)

## The 2K EPROM (`F000`–`F7FF`)

The part is a single 2 KB EPROM built from **three source modules**, each with its
own origin. They are assembled independently and dropped into the same 2K image —
they do **not** link against one another (they reach each other only through fixed
addresses):

| Module | Source | Origin | What it is |
|---|---|---|---|
| Monitor | `SSM8080.ASM` | `F000h` | The monitor itself: jump table, command interpreter, EPROM/memory/transfer commands. |
| System Configuration Package | `SSMSCP.ASM` | `F600h` | The logical-device / driver tables (`IOTAB`) and the physical device drivers (teletype, keyboard, reader, punch, thermal, video). |
| VB1 video driver | `SSMVID.ASM` | `F700h` | Console-output and graphics driver for the SSM **VB1** video board. |

Programmed bytes span `F000`–`F7FF` with two small gaps (`F5EE`–`F5FF` and
`F6EA`–`F6FF`) that read back `FF`. CRC32 of the `FF`-filled 2K image = **`DEB0D584`**
(recorded in [`../../docs/roms.md`](../../docs/roms.md) and checked by
`tests/test_roms.cpp`).

## Jump table (`ORG F000h`)

| Address | Entry |
|---|---|
| `F000h` | `BEGIN` — cold start |
| `F003h` | `CI` — console in |
| `F006h` | `RI` — reader in |
| `F009h` | `CO` — console out |
| `F00Ch` | `PO` — punch out |
| `F00Fh` | `LO` — list out |
| `F012h` | `CSTS` — console status |
| `F015h` | `IOCHK` / `F018h` `IOSET` / `F01Bh` `MEMCK` / `F01Eh` `STRNG` |
| `F021h` | `REENT` — warm re-entry (the PB1 manual's `MONIT`) |

## How the `.HEX` was built (and how to rebuild it)

These sources assemble with Microsoft **M80 + L80** under CP/M, driven inside the
simulator itself — never with `cpmtools`. The three `.ASM` files here are the
**relocatable** M80 form (`.8080`, no `ASEG`, no absolute top `ORG`); L80's `/P`
switch sets each module's origin. Their listings are the three `.PRN` files.

For each module (origins `F000`, `F600`, `F700`):

```
M80 MON,MON=MON.ASM               ; -> MON.REL + MON.PRN
L80 /P:F000,MON,MON/N/X/E         ; -> MON.HEX  (answer Y to "move anyway")
```

L80 warns `Origin above loader memory, move anyway (Y or N)?` for a high ROM
origin — answer **`Y`**; it builds low but writes the `.HEX` at the true logical
origin. The three module `.HEX` files are then concatenated into the single
`SSM-8080MON.HEX` embedded here. The full recipe, including the alternate one-step
DRI `ASM.COM` route (byte-identical output), is in
[`../../docs/devguide/assembling-roms.md`](../../docs/devguide/assembling-roms.md).

## Why this was "source only" until now

Earlier the file circulated as one concatenated listing that no assembler accepted:
it EQU'd `ADSCS/ADSCR/ADIOB/ADUST` at `F600h` **and** defined them as local labels,
a multiple-definition every assembler rejects. That was the tell that it is really
**three separate assemblies** — the monitor references the configuration package by
`EQU`, while the package *defines* those same labels locally. Split into the three
modules above (and with two OCR typos corrected — `IOTAS`→`IOTAB` and a missing
colon on `LOAD:`), each assembles clean, and the combined image boots to
`MONITOR V1.0` with a working command prompt.
