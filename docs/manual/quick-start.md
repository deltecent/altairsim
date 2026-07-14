# Quick start: CP/M in one command

```
$ altairsim {{MACHINE_CPM}}
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
program** — booting a disk Altair meant setting the address switches to `FF00` and pressing
RUN, so that is what the machine file says, in the operator's own words. Anything you can
type, a machine file can do; it gets no special powers.

From there it is all real: the PROM read sector 0 off track 0, that loader pulled CP/M into
high memory and jumped into the BIOS, and the BIOS printed its banner.

## Getting back out — `^E`

Press **`^E`** (Control-E). This is **ATTN**, and it is how you take the keyboard back from
a running program:

```
A>
ATTN -- the machine is still at CA9C. RUN resumes.
C0Z1M0E1I0 A=00 B=007F D=CA01 H=BC0E S=BC37 IE=1 P=CA9C  CALL CA78
altairsim>
```

You are back at the monitor. **The machine is still running** — ATTN did not stop it, it
just took the keyboard away. It tells you where the processor is and what the registers hold.

### Why it is not `^C`

Because **`^C` belongs to the guest.** CP/M reads `^C` — it is how you warm-boot it, and how
you interrupt a BASIC program. A stop key that the guest also wants is a stop key the guest
will eat, and then you are trapped inside your own simulator.

So the host intercepts `^E` **before the guest is ever offered the byte**. No program running
inside the machine can disable it, ignore it, or take it away from you. Everything else on
the keyboard — including `^C` — goes straight through to the guest, which is entitled to it.

(If `^E` collides with something you need, it moves: `CONSOLE attn=1D` makes it `^]`.)

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
| **`^E`** | out of the guest, back to the monitor. The machine keeps running. |
| **`RUN`** | back into the guest. |
| **`QUIT`** | done. |

## Careful: the disk is real, and there is no undo

The disk image is mounted **read/write**, because that is what a real machine is — a CP/M
whose `A:` is read-only fails on its first `PIP`. So anything you do in there *happens to the
file on your host*, and nothing is keeping a copy.

**Before you experiment, copy the folder.** It is a directory with a machine file and a disk
image in it, and it boots from anywhere:

```
$ cp -R disks/cpm22 my-cpm
$ altairsim my-cpm/cpm22-buffered.toml
```

There is one more trap, and it is not obvious: **this BIOS does not write to the disk when
CP/M closes a file.** It writes into a track buffer in memory and flushes it the next time it
reads the console. So do not kill the program the instant a file operation finishes — **get
back to the `A>` prompt first**, and the directory update will have landed. The disks chapter
explains why.

## No disk? Start with a tape instead

If you would rather see something boot from nothing at all — no disk, no PROM, the bootstrap
toggled in by hand exactly as MITS printed it in the manual — turn to the tapes chapter and
load {{NAME_BASIC}}. It is the more instructive machine, and it is the one that shows you
what an Altair actually was.
