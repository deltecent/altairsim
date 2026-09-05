# Quick start: CP/M in one command

```
$ altairsim examples/cpm/cpm22-buffered.toml
```

On **Windows** the program is `altairsim.exe` and the path is spelled with backslashes:

```
> altairsim.exe examples\cpm\cpm22-buffered.toml
```

That is the whole of it.

```
startup> RUN FF00
[console -- ^E returns to the monitor]

56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>
```

You are in CP/M. It is 1977. Type `DIR`:

```
A>DIR
A: L80      COM : LADDER   COM : ED       COM : ASM      COM
A: DUMP     COM : XSUB     COM : PCGET    COM : LS       COM
A: SUBMIT   COM : LOAD     COM : SURVEY   COM : VIEW     COM
A: LADDER   DAT : LUNAR    BAS : M80      COM : MAC      COM
A: MBASIC   COM : PIP      COM : STAT     COM : DDT      COM
A: MOVCPM8  COM : NSWP     COM : SYSGEN   COM : ACOPY    COM
A: OTHELLO  COM : STARTRK  BAS : TICTAK   BAS : WM       COM
A: WM       HLP : CRC      COM : PCPUT    COM : AFORMAT  COM
A: STARINS  BAS : IOBYTE   TXT
A>
```

An assembler, a debugger, Microsoft BASIC, a text editor, and Star Trek. Run one:

```
A>MBASIC
```

## What actually happened

Nothing was faked, and it is worth knowing what the one command did, because the rest of the
manual is built on it.

The machine file named a **56K Altair with an 8″ floppy controller and a boot PROM at
`FF00`**, put the disk image in drive 0, and then did one more thing: it typed `RUN FF00` for
you. That is what the `startup>` line is telling you. **There is no `BOOT` command in this
program** — booting a disk on an Altair meant setting the address switches to `FF00`, pressing
EXAMINE, and then pressing RUN, so that is what the machine file says, in the operator's own
words. (EXAMINE is the step that matters: the switches by themselves change nothing, and it is
EXAMINE that loads them into the program counter. `RUN FF00` is precisely those two presses —
see the *Monitor* document.) Anything you can type, a machine file can do; it gets no special
powers.

From there it is all real: the PROM read sector 0 off track 0, that loader pulled CP/M into
high memory and jumped into the BIOS, and the BIOS printed its banner.

## Getting back out — `^E`

Press **`^E`** (Control-E). This is **ATTN**, and it is how you take the keyboard back from
a running program:

```
A>
ATTN -- the machine is still at CA9C. RUN resumes.
C0Z1M0E1I0 A=00 BC=007F DE=CA01 HL=BC0E SP=BC37 IE=1 PC=CA9C  CALL CA78
altairsim>
```

You are back at the monitor, and **the machine is stopped exactly where it stood**. The
processor executes nothing while this prompt is up: the `PC=CA9C` above is where it will still
be in an hour. That is what makes the prompt useful — you can read memory, single-step, and set
a breakpoint, and none of it is a moving target.

Stopped is not **lost**. ATTN is not RESET and it is not POWER: every register, every byte of
memory, the disk in the drive and the guest's place in its own program are all exactly as they
were. That is the whole content of *"the machine is still at CA9C"* — it is telling you the
machine is intact and says where to pick it up.

Why `^E` and not `^C`? Because **`^C` belongs to the guest** — CP/M warm-boots on it and BASIC
breaks on it — so the host intercepts `^E` before the guest is ever offered the byte, and no
program inside the machine can take it from you. Everything else, `^C` included, goes straight
through. (If `^E` collides with something you need, `CONSOLE attn=1D` moves it to `^]`.)

## Going back in — `RUN`

```
altairsim> RUN
```

That is all. The machine never stopped, so it simply picks up where it was, and your `A>`
prompt is where you left it.

## Leaving — `QUIT`

```
altairsim> QUIT
```

There is no `EXIT`. `Q` will do.

## The three things to remember

| | |
|---|---|
| **`^E`** | out of the guest, back to the monitor. The machine stops where it stands, and loses nothing. |
| **`RUN`** | back into the guest. |
| **`QUIT`** | done. |

## Careful: the disk is real, and there is no undo

The disk image is mounted **read/write**, because that is what a machine with a disk in it is —
so anything you do in CP/M happens to the file on your host, with nothing keeping a copy. Two
ways to be safe:

- **Write-protect it.** `MOUNT dsk0:drive0 examples/cpm/cpm22b23-56k.dsk RO` refuses every write at the
  controller, so the file cannot change however the guest behaves. Use it to *look around*. But
  the guest is not *told* the disk is protected, so a program that means to write may not survive
  being refused — mount `RO` to read, not to run a CP/M you expect to save your work.
- **Copy the folder** when you actually intend to write. It is self-contained and boots from
  anywhere:

  ```
  $ cp -R examples/cpm my-cpm
  $ altairsim my-cpm/cpm22-buffered.toml
  ```

One non-obvious trap: **this BIOS does not write to the disk when CP/M closes a file** — it
buffers a whole track and flushes it the next time it reads the console. So **get back to the
`A>` prompt before you quit or copy the image**, or the last write never lands. The disks
chapter explains all three in full.

## No disk? Start with a tape instead

If you would rather see something boot from nothing at all — no disk, no PROM, the bootstrap
toggled in by hand exactly as MITS printed it in the manual — turn to the tapes chapter and
load Altair 4K BASIC 3.1. It is the more instructive machine, and it is the one that shows you
what an Altair actually was.
