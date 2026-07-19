# Burcon CP/M 2.2 — Altair 8800

A third-party CP/M 2.2 for the Altair, booted by the DBL PROM off an 8" floppy.

```
altairsim disks/mits-88dcdd/cpm22/burcon/cpm22-burcon.toml

56K CP/M
Version 2.2mits (07/28/80)
Copyright 1980 by Burcon Inc.

A>DIR
```

Run it **from anywhere** — the paths inside the `.toml` resolve against **that file**, not
against the directory you launched from, so this folder boots wherever it is copied to. (A
path you *type* at the prompt is still relative to your shell, which is the other half of
the same rule.)

## The disk images are not in this repository

`*.dsk` is in `.gitignore` — they are large and not ours to redistribute. **Download them into this
directory** — this is a **manual download you have to supply yourself**: these two images are *not*
in `tools/fetch-disk-images.sh`, so a fresh clone has neither of them.

**Without `cpm56k.dsk` sitting in this directory, `cpm22-burcon.toml` will not start at all.** It is
not a machine that boots to a dead prompt — the mount fails while the machine file is being read,
and `altairsim` says so and stops:

```
disks/mits-88dcdd/cpm22/burcon/cpm22-burcon.toml: dsk0: 'disks/mits-88dcdd/cpm22/burcon/cpm56k.dsk': no such file
  ('cpm56k.dsk' is relative to the machine file that wrote it, in disks/mits-88dcdd/cpm22/burcon/)
```

> <https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/>

| File | What it is |
|---|---|
| `cpm56k.dsk` | Bootable, built for a **56K** machine. **This is the one `cpm22-burcon.toml` mounts.** |
| `cpm48k.dsk` | The same CP/M sized for a **48K** machine. It will **not** boot in 56K. |

**There is no undo.** CP/M writes to `A:` for anything you create, and the config mounts drive 0
read/write, because that is what a real machine is. Git cannot restore what it never tracked. Copy
the image before testing writes.

## Running the 48K image

The BIOS is linked to the top of memory — `BOOT.ASM` here puts it at `MSIZE*1024 - BIOSLEN` — so
**the RAM and the size CP/M was built for have to agree**, or CP/M loads into nothing. Mount
`cpm48k.dsk` instead and re-fit the memory card; `cpm22-burcon.toml` carries the block, commented,
at the bottom.

Or do it the way a 1977 owner would have, and the way this disk's own ReadMe tells you to: run
`MOVCPM` from inside CP/M to re-size the system, and `SYSGEN` it back onto the disk.

## What is tracked here

Only `README.md` and `cpm22-burcon.toml`. The images, the `.ASM`/`.PRN` listings and the vendor
ReadMe are all at the URL above — nothing in this directory is a build dependency, and nothing here
is cited as a hardware source (the 88-DCDD was modeled from `../buffered/BOOT.ASM` and `BIOS.ASM`,
which *are* in git).
