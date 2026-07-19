# What is in the package

```
altairsim                the program. One file, no dependencies, nothing to install.
README.md                what it is, in one page.
LICENSE                  MIT.
```

That is the whole archive. There is no library to install, no runtime, and no configuration
file you must write before the program will start.

**This manual is not inside it.** `altairsim-manual.pdf` — and the *Developer Guide*, if you
want it — are downloaded separately from the same release page the archive came from. You are
reading one of them, so you have already found them.

## The machines are in the program

You do not need any files to get a running machine. Thirteen machine descriptions are
compiled into the binary, and naming one boots it:

```
$ altairsim --list                what the built-in names are
$ altairsim altmon                a monitor in ROM, on a terminal
$ altairsim sol20                 a Processor Technology Sol-20, running SOLOS
```

A built-in is an ordinary machine file that happens to live inside the executable — the same
TOML format you would write yourself. Several carry their ROM with them (`altmon`, `sol20`,
`cuter`, `vdm1`, and the boot PROM in `default` and `minidisk`), so they run out of the box
with nothing fetched and nothing mounted.

`CONFIG SAVE mine.toml` writes out the machine you are actually running, as a file you can
edit — which is the usual way to start one of your own.

## What is *not* in the package: the disks and tapes

**No disk images, cassettes or `.WAV` files ship with the binary.** The machines that need
media — CP/M on a floppy, BASIC on a cassette, Programming System II — start up perfectly
well, but with an empty drive:

```
$ altairsim -x "SHOW MOUNTS" basic4k
altairsim> SHOW MOUNTS
  UNIT       KIND  HOLDS
  acr0:tape  tape  (empty)
```

You supply the media and `MOUNT` it. The disks and tapes chapters describe how, and every
example in this manual that names an image is showing you the shape of the command, not a
file you already have.

> **Where the media will come from.** A separate **`altairsim-packages`** repository is
> planned to hold the disks, tapes and machine files, packaged so that each example is a
> self-contained folder you can drop anywhere. **It is not published yet**, and exactly which
> images go in it has not been settled. Until it exists there is nothing to link to and no
> download to point you at — so if you are reading this and want CP/M, you are bringing your
> own image.

Media is kept separate from the program on purpose: an image is large, most of the good ones
are not ours to redistribute, and the simulator's version and the software's have no reason
to move together.

## What is *not* in the package: the source

The **source code** is not here. `altairsim` is an open project under the MIT licence, and
the source is a separate thing to fetch:

**<https://github.com/deltecent/altairsim>**

Nothing in this manual requires it. The one exception worth naming: **if you want to build a
board of your own** — which is what the simulator is really for — you need the source, and
you want the *Developer Guide*, which is a different document. This one is about driving the
machine, not extending it.

## Reporting a bug, or asking for something

Both go in the same place — the **Issues** tab of that repository:

**<https://github.com/deltecent/altairsim/issues>**

Search it first; if nobody has raised your problem, open a new issue. You need a GitHub
account, and nothing else.

**What makes a bug report useful** is enough for somebody else to see what you saw:

- The **version** — the line `altairsim` prints at startup, or `altairsim --version` — and
  which operating system.
- The **machine**: the built-in's name, or the machine file itself, which is a small text
  file you can paste.
- **What you typed and what happened.** Paste the terminal, prompt and all. The monitor
  echoes every command, so a pasted session is a complete record of what was asked of it.
- What you expected instead, when that is not obvious.

If the guest software misbehaved rather than the simulator, say which software and where you
got it — a period program failing on real hardware in 1976 is a fair thing for it to do here
too, and knowing the image is how that gets untangled.

**A feature request is an issue as well**, and does not need an apology. Say what you are
trying to do rather than only which knob you want, because the machine often has a way in
already; and if it does not, the shape of the problem is what decides the shape of the
answer. A missing S-100 card is a particularly good request when you can name the manual it
was documented in — every board here was modelled from its own documentation, and a card
with no surviving source is one nobody can build honestly.
