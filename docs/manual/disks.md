# Disks

A disk Altair is a floppy **controller** on the bus, some number of **drives** hanging off it,
and a **disk image** sitting in one of those drives. The three are separate things, and the
manual keeps them separate, because the machine did.

The controller is a board. It goes in the machine file. The drives are part of the
controller — a MITS 88-DCDD addresses up to sixteen of them, whether or not you own sixteen.
The disk goes in a drive, and you may put it there either way: name it in the machine file, or
`MOUNT` it at the prompt. They do the same thing.

> **A disk is not a cassette, and the difference is real.** A machine file *may* name the
> floppy that is in drive 0 — the CP/M example does exactly that — but it may never name the
> tape in the recorder. A floppy drive is wired to its controller and the guest can tell what
> is in it. **A cassette recorder has no motor control on the card**: nothing in the machine
> can start the tape, sense it, or know it is there. So the tape is something a human puts in
> and presses PLAY on, and it stays at the prompt where humans are. See the tapes chapter.

## The two controllers

| Card | What it is | Drives | Ports |
|---|---|---|---|
| `dcdd` | **MITS 88-DCDD** — the 8″ hard-sector floppy controller. The one CP/M booted from. | up to 16 | 08, 09, 0A |
| `mds` | **88-MDS** — the 5¼″ minidisk. Smaller, slower, four drives. | 4 | 08, 09, 0A |

Read the ports column again. **They are the same ports**, which means the two cards cannot
coexist in one machine. That is not a limitation of the simulator; it is the MITS address map.
A real Altair with both would have had two cards decoding 08 and fighting on the data bus,
and the bus view in the monitor will tell you so if you try it. Pick one.

Drives are units on the card: `drive0`, `drive1`, and so on up to the card's limit.

## Putting a disk in — `MOUNT`

```
MOUNT <id>[:<unit>] <file> [RO]
```

```
altairsim> MOUNT dsk0:drive1 my-scratch.dsk
```

That is a floppy going into drive 1 of the controller called `dsk0`. The socket was empty
before; it isn't now.

`WP` is **the write-protect tab**, and it does what the tab on a real diskette did: the guest may
read the disk, and a write is refused at the controller and never reaches the file on your host.

```
altairsim> MOUNT dsk0:drive2 golden-master.dsk WP
```

`RO` is accepted and means exactly the same thing. It is the right word for a ROM socket, which
has no tab to move — for a floppy, `WP` is the thing you are actually doing.

**The guest is not told, and cannot be** — see "Read-only is not an error message" below. Mount
`WP` for a disk you intend to read.

A protected disk says so wherever it is listed, so you never have to remember which one you did
it to:

```
altairsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  dsk0:drive2  disk  golden-master.dsk  (write-protected)
```

And if the **host** would not let us write the file — the permissions say no — the disk still
mounts, protected, and says that it was not your idea:

```
  dsk0:drive2  disk  master.dsk  (write-protected -- THE HOST WON'T LET US WRITE IT; you did not ask for this)
```

That one is worth reading closely. You did not ask for the tab, so CP/M is about to bounce every
write off a disk you believe is writable.

And taking it out:

```
altairsim> UNMOUNT dsk0:drive1
```

The socket is empty again. The guest sees a drive with no disk in it, which is a thing a real
drive could be.

### Names, and what you may leave out

Board names are **case-blind everywhere** — `dsk0`, `DSK0` and `Dsk0` are the same card.

Beyond that you may omit **what carries no information**:

- the **trailing index**, when only one card of that type is in the machine. One floppy
  controller means `dsk` finds `dsk0`.
- the **unit**, when the card has only one thing you could possibly mount into. `MOUNT ACR
  tape.bin` needs no unit, because a cassette recorder has one slot.

A floppy controller does not qualify for the second one. It has sixteen drives, and which
drive you meant is real information, so you must say it. **Anything genuinely plural, you
must name.**

## …or put it in the machine file

`MOUNT` at the prompt is for the disk you are dealing with *now*. A disk that belongs to a
machine belongs in the machine file, and that is how the shipped examples do it:

```toml
[[board]]
id = "dsk0"                    # no `type`: the controller is already there

  [[board.drive]]
  unit  = 0
  mount = "cpm22b23-56k.dsk"   # relative to THIS FILE
  # readonly = true            # refuse every write; your file cannot change
```

The two forms do the same thing. The difference is only *when*: one is the drive as the
machine ships, the other is you, at the prompt, changing your mind.

Note the path. It names the file lying **beside the machine file**, with no directory at all —
which is what lets the whole folder be copied somewhere else and still boot. A path inside a
machine file is resolved against **that file**; a path you type is resolved against **your
shell**. The machines chapter has the rest of that rule.

## The geometry is probed, not declared

You do not tell `altairsim` what kind of disk you just mounted. It **looks at the file's byte
count** and works it out:

| Format | Tracks | Sectors | Bytes/sector | File size |
|---|---|---|---|---|
| `8in` | 77 | 32 | 137 | **337,568** |
| `minidisk` | 35 | 16 | 137 | **76,720** |
| `fdc8mb` | 2048 | 32 | 137 | **8,978,432** |

A file of 337,568 bytes is an 8″ floppy. There is nothing else it could be. This is why the
quick start never mentions a format: there was nothing to mention.

When the size genuinely cannot decide — a truncated image, a format you are inventing — a
`media` key on the drive forces the answer.

## Why an 8 MB disk works at all

`fdc8mb` is a 2048-track disk on a controller that MITS designed for 77 tracks, and it works
through the **stock card with the stock PROM**. That deserves an explanation, because it looks
like a cheat and is not one.

**The controller cannot tell an 8 MB disk from an 8″ floppy.** It steps the head in, it steps
the head out, it shifts bytes past a read head, and it reports what the drive tells it. It
never asks how big the disk is, because a real 88-DCDD had no way to ask and no reason to
want to. Step it 300 times and it steps 300 times. All the intelligence about *where track
1500 is* lives in the BIOS, which is software, and software can be rewritten.

The corollary is the useful part: **format and spindle are per drive, not per controller.**
Mixed geometry on one card is the intended arrangement, not an accident — period 8 MB CP/M
BIOSes expect exactly that, an 8 MB disk on A: and B: and ordinary 77-track floppies on C:
and D:, so that you can `PIP` between the big disk and something you can hand to somebody
else.

```
altairsim> MOUNT dsk0:drive0 big.dsk
altairsim> MOUNT dsk0:drive2 floppy.dsk
altairsim> SHOW dsk0
```

## THE TRACK BUFFER TRAP

Read this section. It is the one thing about disks in this simulator that will cost you work
if you do not know it, and it is not the simulator's doing — **it is what a BIOS may do**, and
the CP/M in this package is one that does.

**A track-buffering BIOS does not write to the controller when CP/M closes a file.**

`BIOS WRITE` copies your data into a **track buffer in memory** — 32 sectors of 137 bytes, one
whole track — marks it dirty, and returns. Nothing has touched the disk. Nothing will touch
the disk until something calls the flush. And the flush is called from **`CONIN`**: the BIOS
console-input routine. **Reading the console is the flush trigger.**

This is not madness. A track write is one seek and one revolution instead of thirty-two, and
the moment the machine sits down to wait for a human to type something is precisely the moment
it has spare time and nothing to lose. It is a very good piece of engineering. It is also a
loaded gun.

### Not every CP/M does this

Buffering is not a property of CP/M, and it is not a property of the controller. It is a
decision the author of a **BIOS** made, and the BIOS is precisely the part of CP/M that every
machine's owner rewrote for himself. Period Altair BIOSes went both ways:

- **Track-buffered.** Writes pile up in memory and reach the disk on a console read. The CP/M
  that ships in this package is one of these.
- **Write-through.** Every `BIOS WRITE` puts a sector on the disk before it returns, and when
  you get your prompt back your file is already there. Some of these hook `CONIN` too — but
  only to *unload the head*, which is a courtesy to the drive and touches no data.

You cannot tell which one you have by looking at the `A>` prompt, and a disk image somebody
handed you arrives with whatever BIOS its author wrote on it. So treat the rule below as the
rule regardless: on a write-through BIOS it costs you nothing, and on a buffered one it is the
difference between having your file and not.

> **Never `UNMOUNT`, copy, or kill the program immediately after a file operation.**
> **Get back to the `A>` prompt first.**

The `A>` prompt is CP/M reading the console; the console read flushes the buffer if there is
one, and the directory update lands on the disk. Once you are looking at `A>`, the disk on
your host is what you think it is — under either kind of BIOS. Save the file, watch the prompt
come back, *then* do whatever you were going to do.

`^E` back to the monitor from the `A>` prompt is safe. `^E` back to the monitor one instant
after `ED` says it has written your source file is **not**, and on the CP/M in this package
the file will not be there.

## The disk is real, and there is no undo

A mounted disk is mounted **read/write**, and every write goes through to the file on your
host as it happens. There is no journal, no snapshot, and no way back. A CP/M cannot save your
work onto a disk it is not allowed to write, so read/write is the only default that lets the
machine be a machine — and the price of it is that you can destroy the example disk with one
mistyped `ERA`.

**Copy the directory before you experiment.** It is self-contained and boots from anywhere.

Use `RO` when you want the guest to look and not touch.

## Read-only is not an error message

`RO` is a promise to **you**, about your file. It is not a message to the guest, and this is
worth being exact about, because it is easy to assume otherwise.

**The controller has no way to say "write protected."** The 88-DCDD's status byte is seven
bits — write-circuit-wants-a-byte, head-movement-OK, head-loaded, drive-enabled,
interrupts-enabled, on-track-0, new-read-data — and none of them means *the notch is covered*.
A program reading that byte cannot distinguish a protected disk from an ordinary one.

**And CP/M has nowhere to put the answer even if it had it.** A CP/M 2.2 BIOS write returns
`0` for success and `1` for a non-recoverable error. That is the entire vocabulary. There is no
"protected" code, which is why a genuine hardware write failure surfaces as the blunt
`Bdos Err On A: Bad Sector` — CP/M is telling you the only thing the interface let it hear.

The message people remember — `Bdos Err On A: R/O` — is a **different mechanism entirely**. That
is CP/M's own software read-only flag, the one `STAT A:=R/O` sets and a warm boot clears. It
lives in CP/M's head, not on the disk and not in the controller, and a write-protected image will
never produce it.

So: a guest that tries to write to an `RO` disk is asking for something it cannot be refused
politely. **Do not mount `RO` and then expect CP/M to cope gracefully** — mount `RO` for a disk
you are going to read. If you want a disk the guest can write and you can throw away, copy the
directory; that is what the copy is for.

## Making a scratch disk

Two ways, and they arrive at the same place.

**From the monitor.** Mount a filename that does not exist yet:

```
altairsim> MOUNT dsk0:drive1 my-scratch.dsk
```

**From inside CP/M.** Boot, and format it the way the period did — the CP/M disk in the
package carries a formatter, and formatting an image is formatting a disk. This is the more
faithful route and it is the one that produces a disk the guest is certain to be happy with,
because the guest made it.

Either way, remember which chapter you are in: get back to `A>` before you go looking at the
file.

## Looking at what is in the machine

`SHOW` on the controller lists its drives and what is in each one:

```
altairsim> SHOW dsk0
```

`BOARDS` shows the backplane — every card, where it decodes, and who is fighting whom:

```
altairsim> BOARDS
```

Between them they answer nearly every "why is it not booting" question there is, and the
first one is almost always *the disk is in the wrong drive*.
