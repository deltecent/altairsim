# What is in the package

```
altairsim                the program. One file, no dependencies, nothing to install.
altairsim-manual.pdf     this.
disks/                   disk images, each with the machine file that boots it.
tapes/                   cassette images, each with the machine file that boots it.
```

That is the whole distribution. There is no library to install, no runtime, and no
configuration file you must write before the program will start.

## The disks and tapes are *examples*, and each one is self-contained

Every example is a **directory**, and it holds both the media and the machine file that
knows what to do with it:

```
{{MACHINE_CPM}}          the machine: a 56K Altair with a floppy controller
{{DISK_CPM}}             the disk that goes in it
```

Naming the machine file boots it:

```
$ altairsim {{MACHINE_CPM}}
```

**The directory is the unit, and you may move it anywhere.** A path written *inside* a
machine file is resolved against **that file**, not against wherever you happened to be
standing when you ran the program. So the machine file above names its disk as simply
`cpm22b23-56k.dsk` — the one lying next to it — and the folder still boots after you copy
it to your desktop, rename it, or mail it to somebody.

(The other half of that rule matters just as much: a path *you type* at the prompt is
relative to **your shell**, because you are the one who can see your own directory. The two
halves are covered in the machines chapter.)

## The examples

| Directory | What it is |
|---|---|
| `disks/cpm22` | **CP/M 2.2** on an 8″ floppy. This is the quick start. |
| `tapes/basic` | **{{NAME_BASIC}}** on a cassette, with the period bootstrap you toggle in to load it. |

> **{{NAME_DISKBASIC}} — not yet included.** The disk-BASIC example is still to be
> assembled. Where this manual describes it, it says so.

## What is *not* in the package

The **source code** is not here. `altairsim` is an open project and the source is a separate
thing to fetch; nothing in this manual requires it, and nothing in this manual refers to it.

The one exception worth naming: **if you want to build a board of your own** — which is what
the simulator is really for — you need the source, and you want the *Developer Guide*, which
is a different document. This one is about driving the machine, not extending it.
