# Moving files in and out

You will want to get a file into CP/M, and you will want to get one back out. There is a card
for it.

## The Host Bridge is ours, and it is not a period card

Say it plainly: **MITS never made this.** No S-100 vendor made this. The `hostbridge` card is
**our own**, invented for this simulator, and it exists because the alternative — a modem
protocol over a simulated serial port at 300 baud — is a slow, fiddly answer to a question
that only exists because the machine is simulated in the first place.

Everything else in this program is a board somebody could buy in 1977. This one is not, and
you should know which is which. It sits in the manual next to the real cards, and it is the
only one wearing a different badge.

It is in the **`default` machine**, so unless you have written a machine file that leaves it
out, **you already have it.**

Default port **B0**: `BASE+0` is command/status, `BASE+1` is data.

## The three utilities, and where they live

The utilities are **on the disk**, not in the card. They are ordinary CP/M `.COM` files, and
they were assembled inside the machine, by the machine's own assembler, from source that ships
with it.

Which means a disk can be missing them, and some are — the minidisk image in the package is
one. *Getting the utilities onto a disk that hasn't got them*, below, is how you fix that.

| | |
|---|---|
| `HDIR [pattern]` | List what is on the host. |
| `R <hostfile> [cpmfile]` | Read host → CP/M. |
| `W <cpmfile> [hostfile] [B\|T]` | Write CP/M → host. |

### `HDIR`

```
A>HDIR
A>HDIR *.ASM
```

**`HDIR` always prints the TRUE host names** — the real ones, with their real case and their
real length, not the 8.3 names CP/M is going to see them as. That is the point of it. When
you cannot work out what to call a file, ask `HDIR` what it is actually called.

### `R` — host to CP/M

```
A>R README.TXT
A>R *.ASM
A>R SRC/FOO.ASM
A>R SRC/FOO.ASM WORK.ASM
```

Wildcards work. Subdirectories work — and **both `/` and `\` are accepted on every host**,
because you should not have to remember which machine you are sitting at. A second argument
names the file on the CP/M side and overrides the automatic mapping.

### `W` — CP/M to host

```
A>W GAME.COM
A>W GAME.COM game.com
A>W NOTES.TXT notes.txt T
```

## `W` defaults to **B**, and that is the important sentence

**Binary is the default.** `W` writes **every byte of every record, exactly** — nothing is
inspected, nothing is stripped, nothing is guessed. A `.COM` file survives the trip and runs.

The cost is visible and it is honest: **a text file arrives with up to 127 trailing `^Z`
bytes** on the end, because CP/M stores files in 128-byte records and pads the last one, and
`W` in binary mode does not presume to know which of those bytes you meant. Your editor will
show them. Trim them, or ask for `T`.

**`T` (text) stops at the first `^Z`.** You get clean text, exactly as long as it should be.

And now the trap, which is the reason `T` is not the default: **a binary file containing byte
`1Ah` comes back TRUNCATED.** Byte `1Ah` is `^Z`. It is a perfectly ordinary byte in a `.COM`
file — it is `LDAX D` — and in text mode the transfer stops dead at the first one, silently,
and hands you a file that is the right name and the wrong length.

Some emulators default to text and keep a list of extensions to exempt. **That silently
truncates files** the day you transfer something whose extension is not on the list, and you
find out weeks later. We would rather hand you a text file with some padding on it than hand
you half a program and call it done.

| Mode | What it does | Costs you |
|---|---|---|
| **`B`** (default) | Every byte, exactly. | Up to 127 `^Z` bytes on a text file. |
| `T` | Stops at the first `^Z`. | **Truncates any binary containing `1Ah`.** |

## The sandbox

The card has a **`hostdir`** property. It is the host directory the guest can see, and it is
the **only** host directory the guest can see.

```
altairsim> SET hb0 hostdir=/tmp/xfer
```

Empty — which is the default — means **the directory you ran `altairsim` from.**

### Where a relative `hostdir` points

`hostdir` is a path, so it obeys the path rule from *Machines* — **both halves of it**, and this
is the one place the difference has teeth, because this path is a fence and the other end of it
is a CP/M program.

```
altairsim> SET hb0 hostdir=xfer          # you typed it -> ./xfer, from YOUR shell
```

```toml
[[board]]
id      = "hb0"
hostdir = "xfer"                          # the file wrote it -> the xfer beside THIS FILE
```

Both say `xfer`. They are **different directories**, and each is the one its author could see —
which is the same rule that decides where `mount = "cpm.dsk"` looks. The machine file's `xfer`
travels with the machine file, so an example directory you copy to your desktop still has its
own transfer folder.

**You never have to work it out.** The written value and the resolved one are both printed, and
the resolved one is the fence:

```
altairsim> SHOW hb0
  property         value            legal
  port             0xB0             0..254
  hostdir          xfer
  hostdir_root     /home/you/altair/disks/cpm22/xfer (read-only)
  readonly         false            true|false
```

`hostdir` is what was **written**. `hostdir_root` is where the fence **actually is** — read-only,
because it is a fact about this run rather than a setting, and it is never written back into a
machine file.

`SHOW PATHS` prints the same root beside the other two bases. If `R` cannot find a file you are
certain you put in `xfer`, that is the line to read: there is very likely a second `xfer`, and
the guest is standing in the other one.

### …and what that means for `R` and `W`

The path rule stops at the card. **It does not reach the guest, and `R` and `W` never see it.**

At the `A>` prompt you are not typing a host path at all — you are typing a **name**, and the
card resolves it inside `hostdir_root` and nowhere else:

```
A>R FOO.ASM          <- hostdir_root/FOO.ASM. Never your cwd, never the machine file's
A>W RESULT.TXT       <- hostdir_root/RESULT.TXT
```

So the path rule reaches `R` and `W` **exactly once**: it decides which directory `hostdir`
named, back when someone wrote it. After that the guest has one directory and no way to say
anything about any other — `..`, an absolute path and a drive letter are all refused at the
card, as below.

This is worth stating plainly because the two mechanisms look alike and are not:

| | decides | confines |
|---|---|---|
| **the path rule** | where a path *written by a human* points | nothing at all |
| **`hostdir`** | nothing you type | **everything the guest can reach** |

This is the *only* fence in the simulator, and it is worth not confusing with the path rule in
*Machines*. That rule decides where a path written in a machine file points, and it confines
nothing at all. This one decides how far the **guest** can reach, and confines everything. A
machine file may mount a disk from anywhere on your system; the CP/M program running off that
disk still sees only `hostdir`.

**The guest cannot escape it.** All of the following are refused, at the card, before anything
touches your filesystem:

- an absolute path
- a drive letter
- any `..` component
- a symlink that resolves outside the root

This is a **deliberate, hard boundary**, not a best-effort filter. The guest is running
software from 1977 that you found on the internet, and the answer to "what if it is hostile"
should not be "well, it probably isn't."

`readonly` makes it a one-way street:

```
altairsim> SET hb0 readonly=on
```

Files come **out of** the host. Nothing goes back in. `W` fails.

## And it means the guest can write your working directory

Here is the other half of that, and it does not get buried:

**By default, guest software can read and write the directory you ran `altairsim` from.**

That is a real capability and it is on unless you turn it off. It is a **deliberate trade**,
not an oversight: `R FOO.ASM` working the instant you unzip the package, with no setup, is
worth a great deal, and the directory you are standing in is the directory you almost always
meant. But it is your directory, with your files in it, and a CP/M program you did not write
can reach them.

If that is not what you want, point `hostdir` somewhere else, or set `readonly=on`. Both are
one line.

## Names: 8.3, and how a host name becomes one

CP/M has eight characters and an extension of three, and your host does not. So the card maps:

1. **uppercase** the host name,
2. truncate the base to **8**,
3. truncate the extension to **3**,
4. **drop illegal characters.**

```
my-notes(2).txt   →   MYNOTES2.TXT
```

A **second argument overrides the whole business**, and when the mapping produces something
you do not like, that is what it is for:

```
A>R my-notes(2).txt NOTES.TXT
```

And again: `HDIR` prints the **true** host names, so you can always find out what you are
actually asking for.

## Case, and why the card does not guess

**The CP/M command line folds everything to upper case.** This is not something we do; it is
what the CCP does, before your program ever sees the line. By the time `R` runs, `R
readme.txt` and `R README.TXT` are **the same command**. The information is gone.

So the card compensates, in this order:

1. **Exact match wins.** If there is a file called exactly what you asked for, that is the
   file.
2. **Otherwise, fold case and look again.** `README.TXT` will find `readme.txt`.
3. **If several files match, it REFUSES.**

Step 3 is the one that matters. If your directory holds `readme.txt` and `README.TXT` and
`ReadMe.Txt`, the card does not pick one, does not pick the newest, and does not pick the
first. **It tells you it cannot tell**, and stops. A file transfer that guesses wrong and
tells you it succeeded is worse than one that fails.

## Getting the utilities onto a disk that hasn't got them

There is an obvious hole in everything above: **`R` is how you get a file onto a disk, and `R`
is a file on the disk.** Boot something that has not got it — the package's minidisk image, or
a fresh disk you made yourself — and you cannot use the card to fetch the program that drives
the card.

You break the loop through **the console**, which is the one channel that exists before any
file-transfer utility does. You type the program in. Not literally: you paste it.

**You cannot paste `R.COM`.** It is binary, and a console is a text device — the first `1Ah`
in it would end the paste, and the bytes above `7Fh` would not survive the trip at all. So you
paste the **`R.HEX`** that `LOAD` was going to turn into `R.COM` anyway. It is Intel HEX:
colons, hex digits and line ends, about 4.8 KB of them, and every byte of it is printable.

It ships in the package, beside the sources, in `cpm/hostbridge/`.

**1. Tell PIP to write a file from the console.**

```
A>PIP R.HEX=CON:
```

PIP is now copying what you type into `R.HEX`, and it will keep doing so until you tell it to
stop.

**2. Paste the whole of `R.HEX` into the terminal.**

Open `cpm/hostbridge/R.HEX` on your host, select all of it, and paste it into the window where
`altairsim` is running. It echoes as it goes — 108 lines of it — because PIP echoes console
input, and that echo is your confirmation the guest is really receiving it.

**3. End it with `^Z`.**

```
^Z
A>
```

Ctrl-Z is CP/M's end-of-file on a console, and it is what closes the file. You are back at the
`A>` prompt and `R.HEX` is on the disk.

**4. Turn the HEX into a program.**

```
A>LOAD R

FIRST ADDRESS 0100
LAST  ADDRESS 07A0
BYTES READ    06A1
RECORDS WRITTEN 0E
```

`LOAD` reads `R.HEX` and writes `R.COM`. It assumes the `.HEX`, so `LOAD R` is the whole
command. `FIRST ADDRESS 0100` is the number to glance at: a CP/M program starts at `100h`, and
if that line says anything else the paste lost something.

**5. Use it, and never paste again.**

```
A>R W.COM
A>R HDIR.COM
```

That is the payoff. `R.COM` exists now, so the other two utilities come across the card as
ordinary binaries — no HEX, no `LOAD`, no pasting — provided `hostdir` points at the directory
they are in:

```
altairsim> SET hb0 hostdir=cpm/hostbridge
```

### Two things that would bite you on real hardware and do not bite you here

**Nothing is dropped, however fast you paste.** On a real Altair, shoving 4.8 KB at a 300-baud
serial card with no flow control is exactly how you overrun the UART and lose characters in the
middle of a HEX record. Here you cannot: the simulated 6850 and 1602 both pace arrivals at the
line rate but **only take a byte when the receive register is free**, so the byte waits on the
host side rather than being lost. A paste of any size arrives intact. (This is a deliberate
departure from the hardware, and the serial chapter says so — an overrun is data loss the host
transport genuinely does not have.)

**And a bad paste tells you.** Every Intel HEX record carries a checksum, and `LOAD` checks it.
A corrupted line is an error, not a program that runs almost correctly.

### The same route builds them from source

Pasting `R.HEX` gets you the program. Pasting `R.ASM` gets you the program *and* the ability to
change it — the disk's own `ASM.COM` assembles it, and `LOAD` finishes the job exactly as
above:

```
A>PIP R.ASM=CON:
   ...paste R.ASM, ^Z...
A>ASM R
A>LOAD R
```

That is not a different technique, only a longer paste — about 28 KB instead of 4.8 — and it is
how the committed `.COM` files in this repository were built in the first place. If you only
want to *run* the utility, paste the HEX; it is six times shorter and needs no assembler on the
disk.

## Why one `R.COM` works on every disk you will ever build

**Every file operation goes through CP/M's BDOS.** Not the BIOS. Not a disk parameter block.
Not an assumption about how many sectors are on a track or where the directory lives.

`R` and `W` open, read, write and close through the same BDOS calls any CP/M program uses, and
the BDOS is the thing that knows about your disk. Which means the same `R.COM`, unmodified,
runs on:

- the 8″ floppy controller,
- the 8 MB disk,
- the minidisk,
- and **any BIOS anybody writes later**, for a controller that does not exist yet.

That was the design goal. The card is not a period card, but the software that drives it is a
perfectly well-behaved CP/M program, and it will still work on the machine you have not built
yet.
