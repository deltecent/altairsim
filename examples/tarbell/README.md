# The Tarbell floppy interfaces — CP/M that boots itself

The **Tarbell Electronics** floppy controllers were the S-100 disk interface that booted CP/M
on a whole generation of Altair and IMSAI machines. Unlike most controllers, a Tarbell card
carries **its own 32-byte boot PROM** — so there is no monitor, no boot command, and nothing
to type. Power on with a disk in the drive and CP/M comes up.

```
cd examples/tarbell
altairsim tarbell.toml     ->   48K CP/M 2.2 on the single-density #1011
altairsim tarbelldd.toml   ->   48K CP/M 2.2 on the double-density #2022
```

Each is a thin delta on a built-in machine (`altairsim tarbell` / `altairsim tarbelldd`): an
8080, an 88-2SIO console at port 0x10, the Tarbell controller at ports F8–FF with its boot
PROM, 64K of RAM, and a host bridge at port B0. All the example file adds is the floppy in
drive 0.

## How the boot works

Reset arms the boot PROM at address 0000, where it **shadows the bottom of memory**. The PROM
reads the first sector off track 0 through the WD FD177x controller and jumps into it; the
instant that loaded code runs, the shadow falls away (it is released by a single address line)
and the disk's own cold loader pulls CP/M into memory. You land at `A>` with nothing in
between. **With no disk in the drive the PROM has nothing to load and the machine halts** —
mount a disk and reset.

## Single density vs double density

- **`tarbell.toml`** — the **#1011** (1977), a WD **FD1771**, single density. The disk is a
  uniform 8″ single-density image (128-byte sectors, 26 per track).
- **`tarbelldd.toml`** — the **#2022** (1979–80), a WD **FD1791**, which reads double density
  too. Its disks are **mixed density**: track 0 is single density (so the boot PROM can read
  it) and the rest are double density.

The card recognises which format a disk is by its size when you mount it — you do not tell it.

## Moving files in and out

Both machines carry the **host bridge** at port B0. The **single-density disk** ships with the
CP/M utilities `HDIR`, `R` and `W` installed, so they work at the `A>` prompt:

```
HDIR              list the files in the directory you launched from
R  FOO.TXT        copy FOO.TXT from your host into CP/M
W  FOO.TXT        copy it back out
```

It can only reach the directory you started `altairsim` in, and no further.

The **double-density master ships full** (0 K free) with its own serial transfer tools
(`PCGET` / `PCPUT`) already on it, so the host-bridge `.COM` files are not installed there —
there is no room. Copy a file off it first if you need the space, or use the single-density
disk for host transfers.

## A word about the disks

The disk images here are **tracked in the repository**, and drive 0 is mounted read/write —
which is what a real machine is. If you are about to test writes in anger, copy the image
first, or add `readonly = true` to the drive in the `.toml`. In a clone, `git checkout` puts a
dirtied image back; in a release package there is no such safety net.
