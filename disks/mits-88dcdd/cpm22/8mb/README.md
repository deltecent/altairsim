# FDC+ 8 MB CP/M 2.2 — Altair 8800

CP/M 2.2 on an **8 MB disk**, in the ordinary 88-DCDD, booted by the DBL PROM.

```
altairsim disks/mits-88dcdd/cpm22/8mb/cpm22-8mb.toml

56K CP/M 2.2b v1.0
For Altair 8Mb Virtual Drive

A0>DIR
```

Run it **from anywhere** — the paths inside the `.toml` resolve against **that file**, not
against the directory you launched from, so this folder boots wherever it is copied to. (A
path you *type* at the prompt is still relative to your shell, which is the other half of the
same rule.) `A0>` is not a typo: this BIOS prints the drive's unit number after the letter.

## There is no second controller, and no special case

The **FDC+** is a modern card that *emulates* the 88-DCDD, and its **serial disk server** (drive type
**7**) is what can hand a machine images this big. But the images themselves are still in **the
88-DCDD's own hard-sector format** — the same 137-byte slot, the same 32 sectors to a track. The
FDC+ manual (§3.7.4) puts it in one sentence:

> *"The 8Mb drive looks like an Altair 8 inch drive with **2048 tracks instead of 77**."*

**The controller cannot tell the difference.** It steps the head and shifts bytes; nothing in it
asks how big the disk is supposed to be. That is why `fdc8mb` is simply one more row in the card's
format table beside `8in` and `minidisk` (`src/boards/mits-88dcdd.cpp`), and not a mode, a quirk, or
a fork of the driver — and why this disk boots through the *stock* card at ports 08/09/0A with the
*stock* DBL PROM, exactly as an 8" floppy does.

## Mixed geometry is the intended arrangement, not a trick

The same manual, same section:

> *"CP/M for these drives expects an 8Mb drive image to be mounted on drives **A and B** (0 and 1)
> and **normal 77 track Altair images on drives C and D** (2 and 3)."*

That works here because the format **and the `Spindle` are per drive** — a 2048-track disk and a
77-track one turn the same 32 sectors past the head, so they can share a controller. It is also the
practical way to move files in and out of an 8 MB system:

```
MOUNT dsk0:drive2 "disks/mits-88dcdd/cpm22/buffered/cpm22b23-56k.dsk"
A0> DIR C:
```

…and `PIP` between them.

## The disk images are not in this repository

`*.dsk` / `*.DSK` are in `.gitignore` — they are large (8.6 MB each here) and they are not ours
to redistribute. **Download them into this directory:**

> <https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/FDC+%208Mb%20CPM%202.2/>

| File | What it is |
|---|---|
| `CPM22-8MB-56K.DSK` | The bootable system disk. **This is the one `cpm22-8mb.toml` mounts.** |
| `BLANK 8MB.DSK` | A formatted, empty 8 MB disk — the master for making new ones. |

**There is no undo.** CP/M writes to `A:` for anything you create, and the config mounts drive 0
read/write, because that is what a real machine is — a read-only `A:` is a CP/M that fails on
its first `PIP`. Git cannot restore what it never tracked. **Copy the image before testing
writes**, and keep `BLANK 8MB.DSK` pristine: mount a *copy* of it as drive 1 when you want
somewhere to write.

```
MOUNT dsk0:drive1 "my-scratch.dsk"
```

## Mind the track buffer

This BIOS does **not** write to the controller when CP/M closes a file. `BIOS WRITE` only copies
into an in-memory `trkBuf` (32 × 137 bytes) and marks it dirty; the real port-`0x0A` write happens
in `invFlush`, which the BIOS calls **from CONIN** — console input is its flush trigger.

So **never unmount or snapshot the image right after a file operation.** The directory update from
BDOS Close sits in RAM until the next BDOS function 1. Get back to the `A0>` prompt first, and it
will have landed. See `docs/boards/mits-dcdd.md`.

## The sources here are tracked, and they are why the board is right

`BIOS.ASM`, `BOOT.ASM` and `FORMAT8M.ASM` are **in git** — unlike the images — because they are
Mike Douglas's own listings, written against real hardware, and the 88-DCDD was modeled from them.
The 8 MB geometry (2048 × 32 × 137 = 8,978,432), the CP/M DPB, the `ANI 7Fh` system/data wrap every
128 tracks, and the track-buffer behavior above all come out of these files. They are first-hand
sources under `DESIGN.md` §0.1, so they stay in the tree.

`-ReadMe.pdf` is *not* tracked (vendor documentation, same rule as the images). It is at the URL
above.
