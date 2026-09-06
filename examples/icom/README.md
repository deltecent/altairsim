# CP/M and FDOS on an iCOM FD3712/FD3812 8″ floppy

```
altairsim cpm22.toml

48K CP/M 2.2 v1.0
for iCOM FD3712 and Altair

A>DIR
```

Ready-made machines for the **iCOM FD3712/FD3812** 8″ floppy controller — CP/M in single and
double density, and both revisions of iCOM's own FDOS disk operating system. Each boots the moment
you start it (`RUN F000` for CP/M, `RUN C000` for FDOS is the machine file's whole startup, because
on a real machine that was EXAMINE the PROM address and RUN).

The iCOM is **not a bit-shifting floppy card** like the Tarbell or VersaFloppy. It is a
**command/handshake** controller: it buffers a whole sector, and the CPU moves bytes through two
ports (`C0h`–`C1h`) while the operating system's disk driver runs from a **boot PROM** up in high
memory (`F000` for CP/M, `C000` for FDOS). The single-density **FD3712** disk is 77 × 26 × 128 =
256,256 bytes; the double-density **FD3812** disk is mixed density — a single-density track 0 then
double-density tracks 1–76 — 509,184 bytes. See `docs/boards/icom-fd3712.md`.

`^E` (STOP) takes the keyboard back to the monitor at any point; `RUN` resumes. `^C` belongs to
CP/M (it is warm boot) and CP/M gets it. FDOS's prompt is `!`. The two FDOS revisions do not share
a command language: **FDOS-III** takes word commands (`LIST` for a directory), while the original
**FDOS-I** takes single-letter directives — `L` lists the directory, `A` assembles, `P` prints.

## The files

| File | What it is |
|---|---|
| `cpm22.toml` | FD3712 **single-density CP/M 2.2**. `base = "icom"` plus the disk in drive 0. |
| `cpm22-3812.toml` | FD3812 **double-density CP/M 2.23** (Lifeboat). Swaps in the FD3812 boot PROM and the DD disk. |
| `fdos-iii.toml` | iCOM's own **FDOS-III** disk operating system. Uses the FDOS boot PROM at `C000` and boots to the `!` prompt. |
| `fdos-i.toml` | iCOM **FDOS-I**, the original revision. Same `C000` PROM as FDOS-III, but adds a high-RAM board (`C500-FFFF`) because FDOS-I loads its resident into high memory. Boots to `!`; use single-letter directives (`L` for the directory). |
| `CPM22v1.0-3712-48K.DSK` | The single-density CP/M system disk. |
| `CPM22-3812-48K.dsk` | The double-density CP/M system disk. |
| `FDOS-III (2SIO).DSK` | The FDOS-III system diskette. |
| `FDOS-I (2SIO).DSK` | The FDOS-I system diskette. |

**There is no undo.** Drive 0 is mounted read/write because that is what a real machine is, and
the guest writes to the disk for anything you create. In a clone `git checkout` puts the image
back; in the package you were handed, nothing does. Copy it first if you are about to test writes
in anger, or add `readonly = true` to the drive.

The boot PROMs are Mike Douglas disassemblies and the disk images are from deramp.com (iCOM Floppy
Systems). Read and write of existing disks work; like the other controllers here, this one will
not lay down a **blank** format from nothing — all the shipped disks are pre-formatted.
