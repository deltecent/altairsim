# Altair CP/M 2.2b — track-buffered, 8" floppy

Mike Douglas's track-buffered CP/M 2.2b v2.3 on an 8" Pertec FD-400, booted by the DBL PROM.

```
altairsim disks/mits-88dcdd/cpm22/buffered/cpm22-buffered.toml

56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>DIR
```

Run it **from the repository root** — the paths in the `.toml` are resolved against the process's
working directory, not against the file.

## The disk images are not in this repository

`*.dsk` is in `.gitignore` — they are large and not ours to redistribute. **Download them into this
directory:**

> <https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/CPM%202.2B/>

| File | What it is |
|---|---|
| `cpm22b23-56k.dsk` | The bootable system disk, built for a **56K** machine. **This is the one `cpm22-buffered.toml` mounts.** |
| `cpm22b23-24k.dsk` | The same CP/M relocated for a **24K** machine. It will **not** boot in 56K — see below. |

**There is no undo.** CP/M writes to `A:` for anything you create, and the config mounts drive 0
read/write, because that is what a real machine is. Git cannot restore what it never tracked. Copy
the image before testing writes.

## Running the 24K image

The BIOS is linked to sit at the top of memory, so **the RAM and the size CP/M was built for must
agree** — arithmetic straight out of `BOOT.ASM` in this directory:

```
MEMSIZE equ 56           BIOSLEN equ 1900h    (8" floppy; a minidisk is 1000h)
CCPBASE = 56*1024 - 1900 - 0E00 - 0800 = B100
BIOSBAS = B100 + 0800 + 0E00           = C700    ...and C700 + 1900 = E000
```

The BIOS ends at exactly `E000`, which is where the default machine's RAM stops — so the 56K image
needs no memory delta at all. The 24K image works out to `BIOSBAS 4700`, ending at `6000`. Mount it
instead, and re-fit the memory card; `cpm22-buffered.toml` has the block, commented, at the bottom.

## The sources here are tracked, and they are why the board is right

`BIOS.ASM` and `BOOT.ASM` are **in git** — unlike the images — because they are the **authoritative
source for the 88-DCDD**: `docs/boards/mits-dcdd.md` cites them by path. The complete equate block,
the register map, the sector layout and the cycle-count timing all come from these two files. They
are period artifacts written against real hardware — `DESIGN.md` §0.1's first-hand sources — so they
stay in the tree even though nothing builds from them.

`-ReadMe (1).pdf` is *not* tracked (vendor documentation, same rule as the images). It is at the URL
above.
