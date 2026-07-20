# What is in the package

```
altairsim                the program. One file, nothing to install.
altairsim-manual.pdf     this.
USING-ALTAIRSIM.md       for an AI assistant driving the machine; see below.
LICENSE                  the MIT licence this is published under.
LICENSE-SDL3             the licence of SDL3, which is built into the program.
examples/                four machines that boot, media included.
```

That is the whole archive. There is no library to install, no runtime, and no configuration
file you must write before the program will start.

`altairsim` is a single self-contained program. The one outside library it uses — **SDL3**,
which opens the window the video boards draw into — is compiled *into* it rather than shipped
beside it, so there is nothing to install and nothing that can go missing. `LICENSE-SDL3` is
that library's licence, and it is in the package because its code is in the program.

The **Developer Guide** is not in here — it is a separate download from the same release page
this came from, and you want it only if you intend to build a board of your own.

### `USING-ALTAIRSIM.md`

This one is not for you, exactly. It is a briefing document for an **AI assistant**: drop it in
a working directory, start an assistant there, and say *"using altairsim, boot CP/M and show me
what is on the disk."* It tells the assistant how to drive the machine over the program's MCP
interface. Ignore it if that is not how you work — nothing else depends on it.

## The machines are in the program

You do not need any files to get a running machine. Sixteen machine descriptions are
compiled into the binary, and naming one boots it:

```
$ altairsim --list                what the built-in names are
$ altairsim altmon                a monitor in ROM, on a terminal
$ altairsim sol20                 a Processor Technology Sol-20, running SOLOS
```

A built-in is an ordinary machine file that happens to live inside the executable — the same
TOML format you would write yourself.

**Six of them carry their software in ROM and need nothing else at all**: `altmon`, `sol20`,
`cuter`, `vdm1`, `amon` and `acuter` come up running, with nothing fetched and nothing
mounted.

The rest carry at most a **boot PROM**, which is not the same thing. `default` and `minidisk`
hold the PROM that *would* boot a disk, and their drives are empty — the PROM runs, finds no
disk, and waits. They want media, and the next section is about where that comes from.

`CONFIG SAVE mine.toml` writes out the machine you are actually running, as a file you can
edit — which is the usual way to start one of your own.

## Four examples, media included

`examples/` holds four complete machines. **Each is a folder with the media in it**, so every
one of them boots the moment you unzip the archive — nothing to fetch, nothing to mount:

```
examples/cpm        CP/M 2.2 on an 8" floppy. This is the quick start.
examples/basic      {{NAME_BASIC}} on a cassette, with the bootstrap you toggle in.
examples/sol        A Sol-20 with {{NAME_SOL}} in the cassette deck.
examples/diskbasic  {{NAME_DISKBASIC}} on an 8" floppy.
```

```
$ altairsim {{MACHINE_CPM}}
```

**The folder is the unit, and you may move it anywhere.** A path written *inside* a machine
file resolves against **that file**, not against wherever you were standing when you ran the
program — so `{{MACHINE_CPM}}` names its disk as plain `cpm22b23-56k.dsk`, the one lying next
to it, and the folder still boots after you copy it to your desktop, rename it, or mail it to
somebody.

(The other half of that rule matters just as much: a path *you type* at the prompt is relative
to **your shell**, because you are the one who can see your own directory. The machines chapter
covers both halves.)

The examples chapter walks through all four, and `examples/sol` ships Processor Technology's
own manual for the game alongside the tape.

## What is *not* in the package: everything else to run

**Those four are the whole of the shipped media.** The other built-ins that want a disk or a
tape — `basic8k`, `ps2`, `minidisk` and the rest — start up perfectly well, with an empty
drive:

```
$ altairsim -x "SHOW MOUNTS" basic4k
altairsim> SHOW MOUNTS
  UNIT       KIND  HOLDS
  acr0:tape  tape  (empty)

  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.
```

You supply the media and `MOUNT` it. The disks and tapes chapters describe how — and where
those chapters name an image that is not one of the four above, they are showing you the
shape of the command, not a file you already have.

> **Where the rest will come from.** A separate **`altairsim-packages`** repository is planned
> to hold the wider collection of disks, tapes and machine files, packaged the same way — each
> example a self-contained folder you can drop anywhere. **It is not published yet**, and
> exactly which images go in it has not been settled, so there is nothing to link to here yet.

The bulk of the media is kept out of the program's own archive on purpose: an image is large,
most of the good ones are not ours to redistribute, and the simulator's version and the
software's have no reason to move together. The four that ship are the ones that make the
manual's first chapters true.

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
  which operating system. Paste it whole: the commit in the parentheses is what says which
  source built your copy, and between releases the number alone names them all the same.
  `SHOW VERSION` prints that from inside the monitor, plus a `video` row saying whether this
  copy can open a window — worth including in anything about the video boards.
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
answer. A missing S-100 board is a particularly good request when you can name the manual it
was documented in — every board here was modelled from its own documentation, and a board
with no surviving source is one nobody can build honestly.
