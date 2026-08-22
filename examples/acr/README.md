# Writing Altair cassettes — the MITS Tapes disk

A CP/M disk full of the **MITS distribution tapes** — every 4K, 8K, Extended and Disk BASIC,
and the Programming System II monitor — as `.TAP` images, together with **WRTAPE.COM**, the
utility that writes one of them back out through the cassette port the way a real Altair did.

Boot it, put a blank tape in the recorder, and record a bootable cassette you can then load on
the `basic4k` / `basic8k` / `ps2` machines — or on real hardware, from the `.WAV` it makes.

The disk and WRTAPE are **Mike Douglas's**, from
[deramp.com](https://deramp.com/downloads/altair/software/papertape_cassette/CPM%20Utilities%20for%20Altair%20Cassette/).
This directory adds a machine file that plugs an 88-ACR into the bus alongside the disk, and
the host-bridge utilities (`HDIR`, `R`, `W`) so you can move `.TAP` files in and out of your
working directory.

From this directory (`examples/acr`):

```
altairsim mitstapes.toml

48K CP/M
Version 2.2mits (07/28/80)
A>
```

`TYPE CONTENTS.TXT` at the `A>` prompt is the disk's own annotated tape list; it is also
reproduced below.

## Recording a tape

WRTAPE reads a `.TAP` off the disk and writes it, byte by byte, to the 88-ACR. On a real
Altair the operator held PLAY+RECORD on the deck first; here that is one `MOUNT` with
`mode=record`, and `CREATE` makes the output file since it does not exist yet. `MOUNT` is a
**monitor** command, so you reach it with `^E`:

The `A>` prompt is CP/M's; the `altairsim>` prompt is the monitor's. `^E` is a keypress, not a
typed command — it toggles from CP/M to the monitor.

```
$ altairsim mitstapes.toml                                  # boots to A>

A>^E                                                        # press ^E -> the monitor
altairsim> MOUNT acr0:tape "8kbas40.wav" CREATE mode=record # blank tape in, PLAY+RECORD down
altairsim> RUN                                              # resume CP/M -> back at A>

A>WRTAPE 8KBAS40.TAP                                         # press RETURN; wait for:
                                                            #   File Transfer Complete

A>^E                                                        # press ^E -> the monitor
altairsim> UNMOUNT acr0:tape                                # flush the recording to disk
```

That leaves `8kbas40.wav` in this directory — a **300-baud FSK audio cassette**, 44.1 kHz, that
decodes back with zero framing errors and plays into a real Altair's 88-ACR. Name the file
`.tap` instead of `.wav` and you get the raw **byte image** rather than audio; everything else
is the same.

- **A fresh tape does not need a REWIND.** `CREATE` starts an empty tape with the head at the
  beginning. (You only REWIND when you are recording over a tape that already has something on
  it.)
- **The transfer is instant at the default clock.** On real hardware it runs at 30 cps — about
  two minutes for 4K BASIC, four and a half for 8K. `SET acr0:tape rate=real` before you record
  buys back that true speed, and the console tape counter climbs while it runs.
- **A second cassette port.** WRTAPE writes through the 88-SIO at 6/7 (the Altair Cassette port,
  which is where the `acr0` in this machine lives) by default. Following the file name with a
  `2` — `WRTAPE 8KBAS40.TAP 2` — writes through the second port of an 88-2SIO instead, for a
  machine strapped that way.

Cassette and paper-tape images are identical, so the exact same steps write a paper tape.

## Moving tapes in and out — the host bridge

The `default` machine this builds on carries an 88-hostbridge (`hb0`), and the disk has its
three guest utilities installed:

These are ordinary CP/M programs, typed at the `A>` prompt:

```
A>HDIR               # list your working directory (the host)
A>R MYPROG.TAP       # host -> CP/M: copy a .TAP onto drive A:
A>W 8KBAS40.TAP      # CP/M -> host: copy a .TAP back out to your working directory
```

So the full loop is: `R` a tape image you got from somewhere onto the disk, then `WRTAPE` it to
a cassette — or `W` one of the disk's own tapes out to keep it.

## What is on the disk

**Boot tapes** — each loads and runs on its own through the bootstrap MITS shipped:

| File | What it is |
|---|---|
| `4KBAS32.TAP` | 4K BASIC ver 3.2 |
| `4KBAS40.TAP` | 4K BASIC ver 4.0 |
| `8KBAS32.TAP` | 8K BASIC ver 3.2 |
| `8KBAS40.TAP` | 8K BASIC ver 4.0 |
| `EXTBAS40.TAP` | Extended BASIC ver 4.0 |
| `EXTBAS41.TAP` | Extended BASIC ver 4.1 |
| `DSKBAS50.TAP` | Disk BASIC 5.0 |
| `PS2-MON.TAP` | MITS Programming System II monitor |
| `DBL40.TAP` | Disk Boot Loader ver 4.0 |
| `DBL45.TAP` | Disk Boot Loader ver 4.5 |

**Boot tapes strapped for port B of an 88-2SIO as the cassette input:**

| File | What it is |
|---|---|
| `8K2SIO.TAP` | 8K BASIC ver 4.0 |
| `EXT2SIO.TAP` | Extended BASIC ver 4.1 |
| `PS2-2SIO.TAP` | MITS Programming System II monitor |

**Programming System II files:**

| File | What it is |
|---|---|
| `PS2-EDT.BIN` | Editor |
| `PS2-ASM.BIN` | Assembler (overlays the editor) |
| `PS2-AM2.BIN` | Assembler (co-resident with the editor) |
| `PS2-DBG.BIN` | Debugger |

The disk also holds `WRTAPE.COM` and its source `WRTAPE.ASM`, `CONTENTS.TXT`, the host-bridge
utilities `R.COM` / `W.COM` / `HDIR.COM`, and the usual CP/M tools (`PIP`, `STAT`, `DDT`,
`ASM`, `SUBMIT`, `FORMAT`, and so on).

## The files in this directory

| File | What it is |
|---|---|
| `mitstapes.toml` | The machine: `base = "default"` plus the MITS Tapes disk in drive A: and an 88-ACR at port 006. |
| `MITS Tapes.dsk` | Mike Douglas's disk, with the host-bridge utilities added. |
| `README.md` | This file. |
| `README.pdf` | This file, built by CI. |

The recorder starts **empty** — a deck with no cassette in it — so a plain boot writes nothing;
you `MOUNT` a tape when you are ready to record, as above.
