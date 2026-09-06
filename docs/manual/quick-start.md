# Quick start: CP/M in one command

```
$ ./altairsim examples/cpm/cpm22-buffered.toml
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

## Where it found the disk — two paths, two rules

You typed **one** path — the machine file — and the simulator found the rest itself: the disk
image, named *inside* that file. Those two kinds of path follow two different rules, and knowing
which is which is the whole of how the program handles directories.

- **A path you type** — the machine file on the command line, or anything you type at the
  `altairsim>` prompt (`MOUNT`, `SYMBOLS LOAD`) — is resolved against **your shell**: the
  directory you were in when you ran the command. `examples/cpm/cpm22-buffered.toml` is walked
  down from there.
- **A path written inside a machine file** — the disk it mounts, a PROM it loads — is resolved
  against **that file**, wherever the file happens to sit. `cpm22-buffered.toml` mounts
  `cpm22b23-56k.dsk` with no directory at all, and the simulator looks for it *beside the
  `.toml`*, never beside you.

Here it is concretely. Say you unzipped the package into `~/altairsim` and launched it from
there:

```
$ pwd
/home/you/altairsim
$ ./altairsim examples/cpm/cpm22-buffered.toml
```

Two paths get resolved, from two different places:

- `examples/cpm/cpm22-buffered.toml` — **you** typed it, so it is found from your shell:
  `~/altairsim/examples/cpm/cpm22-buffered.toml`.
- `cpm22b23-56k.dsk` — the **machine file** names it, in its `mount =` line, so it is found beside
  the `.toml`: `~/altairsim/examples/cpm/cpm22b23-56k.dsk`.

Copy that folder somewhere else and it still boots, because the disk's path is tied to the file,
not to you:

```
$ cp -R examples/cpm /tmp/mycpm
$ ./altairsim /tmp/mycpm/cpm22-buffered.toml
```

Your shell never left `~/altairsim` — the simulator does not change your working directory — yet
the disk is now read from `/tmp/mycpm/cpm22b23-56k.dsk`, beside the machine file you named. That
is the whole reason every example is a self-contained folder you can copy anywhere and still
boot: the disk always travels with the machine file that names it.

When you lose track of which is which, don't guess — **`SHOW PATHS`** at the `altairsim>` prompt
lays out all three base directories side by side:

- **the working directory** — what a path you *type* resolves against, and where the `-s` or `DO`
  script you name is looked up.
- **the config directory** — what a path written *inside* a machine file resolves against, which
  is the file's own folder.
- **the sandbox root** — the single directory the hostbridge file-transfer board may reach, and
  cannot escape.

### The `./`, and running from anywhere

Every command so far began with `./altairsim` — which means *the `altairsim` in **this** folder*.
A fresh unzip drops the program into a directory your shell does not search for commands, so you
have to point at it, and `./` is how you say *look right here*.

You can make that unnecessary by **installing** the program — copying it into one of the
directories your shell already searches, its **`PATH`** (on macOS and Linux, `/usr/local/bin` is
the usual one). Once it is on your `PATH` you drop the `./` and type just `altairsim`, from any
directory at all — including from *inside* an example folder:

```
$ cp altairsim /usr/local/bin/          # once: put it on your PATH
$ cd examples/cpm
$ pwd
/home/you/altairsim/examples/cpm
$ altairsim cpm22-buffered.toml
```

Now your shell is *inside* `examples/cpm`, so the machine file is simply `cpm22-buffered.toml`
with no directory — found from where you are — and the disk it mounts is found beside the file,
as always. The two rules did not change; only where the program lives, and which directory you
ran it from, did.

## Getting back out — `^E`

Press **`^E`** (Control-E). This is the **STOP** switch — `^E` presses the Altair's front-panel
STOP for you — and it is how you take the keyboard back from a running program:

```
A>
STOP -- the machine is still at CA9C. RUN resumes.
C0Z1M0E1I0 A=00 BC=007F DE=CA01 HL=BC0E SP=BC37 IE=1 PC=CA9C  CALL CA78
altairsim>
```

You are back at the monitor, and **the machine is stopped exactly where it stood**. The
processor executes nothing while this prompt is up: the `PC=CA9C` above is where it will still
be in an hour. That is what makes the prompt useful — you can read memory, single-step, and set
a breakpoint, and none of it is a moving target.

Stopped is not **lost**. STOP is not RESET and it is not POWER: every register, every byte of
memory, the disk in the drive and the CPU's place in its own program are all exactly as they
were. That is the whole content of *"the machine is still at CA9C"* — it is telling you the
machine is intact and says where to pick it up.

Why `^E` and not `^C`? Because **`^C` belongs to the software running on the machine** — CP/M
warm-boots on it and BASIC breaks on it — so the host intercepts `^E` before the running
program is ever offered the byte, and no program inside the machine can take it from you. Everything else, `^C` included, goes straight
through. (If `^E` collides with something you need, `CONSOLE stop=1D` moves it to `^]`.)

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
| **`^E`** | stop the CPU, back to the monitor. The machine stops where it stands, and loses nothing. |
| **`RUN`** | start the CPU running again. |
| **`QUIT`** | done. |

## Careful: the disk is real, and there is no undo

The disk image is mounted **read/write**, because that is what a machine with a disk in it is —
so anything you do in CP/M happens to the file on your host, with nothing keeping a copy. Two
ways to be safe:

- **Write-protect it.** `MOUNT dsk0:drive0 examples/cpm/cpm22b23-56k.dsk RO` refuses every write at the
  controller, so the file cannot change however the program behaves. Use it to *look around*. But
  the program is not *told* the disk is protected, so a program that means to write may not survive
  being refused — mount `RO` to read, not to run a CP/M you expect to save your work.
- **Copy the folder** when you actually intend to write. It is self-contained and boots from
  anywhere:

  ```
  $ cp -R examples/cpm my-cpm
  $ ./altairsim my-cpm/cpm22-buffered.toml
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
