# Altair CP/M 2.2b — track-buffered, 8" floppy

Mike Douglas's track-buffered CP/M 2.2b v2.3 on an 8" Pertec FD-400, booted by the DBL PROM.

## The disk and the machine file live in `examples/cpm/` now

**Moved 2026-07-19.** `cpm22-buffered.toml` and `cpm22b23-56k.dsk` are the shipped CP/M example,
so they live in the one place the package is assembled from:

```
altairsim examples/cpm/cpm22-buffered.toml

56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>DIR
```

Nothing was copied — each file has exactly one home, and the tests follow it there. Run it **from
anywhere**: the paths inside the `.toml` resolve against **that file**, not against the directory
you launched from, so the folder boots wherever it is copied to. (A path you *type* at the prompt
is still relative to your shell, which is the other half of the same rule.)

What is left in *this* directory is what is **source rather than product**: the two period `.ASM`
listings below, and the download stub for the optional 24K image.

## The sources here are tracked, and they are why the board is right

`BIOS.ASM` and `BOOT.ASM` are **in git** — unlike the images — because they are the **authoritative
source for the 88-DCDD**: `docs/boards/mits-dcdd.md` cites them by path. The complete equate block,
the register map, the sector layout and the cycle-count timing all come from these two files. They
are period artifacts written against real hardware — `DESIGN.md` §0.1's first-hand sources — so they
stay in the tree even though nothing builds from them, and `tools/build-package.sh` strips them from
the package for the same reason.

`-ReadMe (1).pdf` is *not* tracked (vendor documentation, same rule as the images). It is at the URL
below.

## The 24K image, which is optional and not tracked

| File | Where | Tracked? | What it is |
|---|---|---|---|
| `cpm22b23-56k.dsk` | `examples/cpm/` | **yes** | The bootable system disk, built for a **56K** machine, with the host-bridge utilities installed. **This is the one `cpm22-buffered.toml` mounts.** |
| `cpm22b23-24k.dsk` | here — fetch | no | The same CP/M relocated for a **24K** machine. It boots in a 56K machine too — see below. |

```sh
tools/fetch-disk-images.sh
```

downloads it to its expected path and checks it against a pinned SHA-256. Upstream is
<https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/CPM%202.2B/>; see
`docs/sources.md` for provenance.

**There is no undo on the tracked image either — but git has one.** CP/M writes to `A:` for
anything you create, and the config mounts drive 0 read/write, because that is what a real machine
is. `git checkout` puts it back. A *fetched* image has no such net; copy one before testing writes,
or re-run the fetch. In the package you were handed there is no rope at all.

### Running the 24K image

The BIOS is linked to sit at the top of the memory **CP/M was built for**, so the constraint is
one-way: **the machine must be at least as big as the image**, not the same size. Arithmetic
straight out of `BOOT.ASM` in this directory:

```
MEMSIZE equ 56           BIOSLEN equ 1900h    (8" floppy; a minidisk is 1000h)
CCPBASE = 56*1024 - 1900 - 0E00 - 0800 = B100
BIOSBAS = B100 + 0800 + 0E00           = C700    ...and C700 + 1900 = E000
```

The 56K image's BIOS ends at exactly `E000`, which is where the default machine's RAM stops — so it
needs no memory delta at all. The 24K image works out to `BIOSBAS 4700`, ending at `6000`, and
those addresses are perfectly good RAM in a 56K machine: **just mount it and boot it.** It prints
`24K CP/M 2.2b v2.3` and runs, with the 32K above it simply unused. CP/M does not probe memory; it
uses the addresses it was linked for.

Only the other direction fails — a 56K image in a 24K machine loads its BIOS into addresses that
are not there. If you want the authentic small machine, `examples/cpm/cpm22-buffered.toml` has the
memory block, commented, at the bottom.

## Getting the host-bridge utilities onto a disk

`R`, `W` and `HDIR` are ordinary CP/M programs (`cpm/hostbridge/`) and they have to be **on the
disk** to run — without them there is no way to move a file in or out of a running guest.

**`examples/cpm/cpm22b23-56k.dsk` already has them.** It ships with `R.COM`, `W.COM` and
`HDIR.COM` installed and 18K free.

Any other image installs them for itself in one command:

```sh
tools/install-hostbridge-utils.sh <image>
```

It boots the disk, `PIP`s **only** `R.HEX` in through the console and `LOAD`s it — then deletes the
hex and lets **`R.COM` fetch `W.COM` and `HDIR.COM` off the host itself**. That order is not
decoration: the buffered floppy ships with 26K free and the three hex files are 12.5K, so PIPping
all three does not fit. The card bootstraps its own utilities. It costs 8K (26K free → 18K), it
works on the 24K image too, and it is idempotent — running it twice is running it once.

It then re-boots the image and has `W` write each `.COM` back out, byte-comparing them against
`cpm/hostbridge/` — so a pass means they are on the disk, they *run* off the disk, and they are the
right bytes.
