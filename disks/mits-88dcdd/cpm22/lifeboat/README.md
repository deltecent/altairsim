# Lifeboat CP/M 2.2 — Altair 8800

Lifeboat Associates' CP/M 2.2 for the Altair, booted by the DBL PROM off an 8" floppy. **A 48K
build**, so the machine file re-fits the memory card — see below.

```
altairsim disks/mits-88dcdd/cpm22/lifeboat/cpm22-lifeboat.toml

CONFIG   Version 4.8
(c) 1981 Lifeboat Associates
...
CP/M2 on Altair
```

It comes up in Lifeboat's `CONFIG` program rather than straight at a prompt — that is the disk, not
the simulator. Run it **from the repository root**; the paths in the `.toml` are resolved against the
process's working directory, not against the file.

## The disk image is not in this repository

`*.DSK` is in `.gitignore` — large, and not ours to redistribute. **Download it into this
directory:**

> <https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Lifeboat%20CPM/>

| File | What it is |
|---|---|
| `LIFEBOAT-CPM22-48K.DSK` | The bootable system disk, built for a **48K** machine. |

**There is no undo.** CP/M writes to `A:` for anything you create, and the config mounts drive 0
read/write, because that is what a real machine is. Git cannot restore what it never tracked. Copy
the image before testing writes.

## Why this one has a memory delta and the others do not

The BIOS is linked to the top of memory — `BOOT.ASM` here puts it at `MSIZE*1024 - BIOSLEN` — so the
RAM and the size CP/M was built for **must agree**. The default machine is 56K and this image is 48K,
so `cpm22-lifeboat.toml` **replaces** the memory card rather than adding to it:

```toml
[[board]]
type = "memory"      # `type` on an id the BASE brought REPLACES the card outright
id   = "mem0"
```

That is deliberate, and it is the clearest example of the rule in `docs/config.md`: **regions are a
list**, so *adding* a 48K region to the base's 56K board would **overlap** it — two boards driving
`0000–BFFF`, which is bus contention — rather than shrink it. Replacing the card also means
re-stating the DBL PROM, because it was on the card you just replaced.

To re-size the system from inside CP/M instead, run `MOVCPM` and `SYSGEN`.

## What is tracked here

Only `README.md` and `cpm22-lifeboat.toml`. The image, the `.ASM`/`.PRN` listings and the Lifeboat
users' notes are all at the URL above — nothing here is a build dependency, and nothing here is cited
as a hardware source (the 88-DCDD was modeled from `../buffered/BOOT.ASM` and `BIOS.ASM`, which *are*
in git).
