# Troubleshooting

Most of what goes wrong here is not a fault. It is the machine doing exactly what a real one
did, in a way nobody warned you about. This chapter is the warning, after the fact.

## `^C` does not stop the machine

It is not supposed to.

**`^C` belongs to the guest.** CP/M reads it — it is how you warm-boot, and how you break out
of a BASIC program. A stop key the guest also wants is a stop key the guest will eat, and
then you are trapped inside your own simulator.

**`^E` is the stop key.** It is ATTN, the host intercepts it before the guest is ever offered
the byte, and **no program running inside the machine can disable it, ignore it, or take it
from you.**

If a guest genuinely needs `^E` for something of its own, move ATTN out of its way:

```
altairsim> CONSOLE attn=1D
```

That makes it `^]`. It must be a control character.

## I pressed ATTN and now the machine seems stuck

It is not stuck. It is **stopped**, which is what ATTN is for, and nothing has been lost.

ATTN halts the processor and hands you the monitor. It is not RESET and not POWER: the
registers, the memory and the disk are precisely as the guest left them, and the monitor told
you where to pick it up when it printed *"still at ...".*

```
altairsim> RUN
```

A bare `RUN` — no address — resumes at that exact instruction, and your `A>` is where you left
it. (`RUN <addr>` is a different thing: it loads the PC first, so it *restarts* rather than
resumes.)

And while you are stopped, you can look: `REGS`, `EXAMINE`, `DUMP`, `DISASM` and `STEP` all
work at this prompt, on a machine that is not moving under you. See the debugging chapter.
`BREAK` is for stopping at a place you cannot reach by hand.

## My file was not written / the disk lost my changes

You quit too early.

The CP/M in this package has a **track-buffering BIOS**: it keeps a whole track in memory and
only flushes it to the image when it next reads the console. This is not a shortcut in the
simulator; it is what that BIOS does, and it is why the machine was fast. (Not every CP/M is
built this way — some write each sector as it goes — but you cannot tell from the prompt which
one you are sitting at, so assume you are on a buffered one.)

**Get back to the `A>` prompt before you quit or copy the image.** The moment CP/M asks you
for a keystroke, the buffer has landed. Kill the program the instant a file operation appears
to have finished and the last track never reached the disk. The disks chapter has the detail.

## The disk image changed and I wanted it not to

There is no undo. **The image is mounted read/write, like a real machine** — a CP/M that cannot
write its disk cannot save your work.

Two answers, and take one *before* you experiment:

```
altairsim> MOUNT dsk0:drive0 file.dsk RO
```

`RO` refuses every write at the controller, so your file is safe whatever the guest does. It is
for a disk you mean to **read**: the guest is never told the disk is protected — the controller
has no bit that says so and CP/M has no error that means it — so a program that sets out to write
to an `RO` disk will not fail gracefully. The disks chapter explains why.

or copy the folder. It is a directory with a machine file and an image in it, and it boots
from anywhere:

```
$ cp -R examples/cpm my-cpm
```

## `MOUNT` says "no such file" and the file is right there

Look at *where you typed it from*.

**A path you TYPE is relative to YOUR SHELL. A path inside a machine file is relative to
THAT MACHINE FILE.** Those are two different directories, and one of them is not the one you
are thinking about.

This is deliberate, and it is the reason an example folder boots from anywhere: the machine
file says `cpm22.dsk` and means *the image next to me*, so you can move the folder, or run it
from three levels up, and it still finds its disk. If a typed path resolved the same way, you
could not mount a file from your own working directory without knowing where the machine file
happened to live.

## Every prompt ends in a garbage character

```
MEMORY SIZ?
```

That is **MITS BASIC setting bit 7 of the last character of every message** as a string
terminator, and your terminal printing the result. The `E` is there; it arrived as `C5`
instead of `45`.

```
altairsim> CONSOLE strip7out=on
```

The real Teletype ignored bit 7 and printed the `E`. `strip7out` is your terminal doing the
same. The serial chapter explains why the fix belongs on the console and **never** on the
board.

## Nothing appears when I type / the guest does not see my keys

Ask who has the keyboard.

```
altairsim> SHOW CONSOLE
```

**Exactly one unit may hold the console.** If the console is on a unit the guest is not
reading, you are typing into a board nobody is listening to. Connect the right one — and note
that connecting a second unit to `console` *steals* it from the first, and says so.

## The machine runs impossibly fast — a cassette loads in one second

It is running **flat out**, which is the default. `clock_hz = 0` means "no divisor, go".

```
altairsim> SET cpu0 clock_hz=2000000
```

That is the real 2 MHz Altair, and it will give you the real 110-second cassette load. Both
behaviours are correct; one of them is just a much longer lunch.

## A file transfer keeps timing out — `PCGET`, `PCPUT`, XMODEM

**Slow the machine down. Transfers with the outside world want the real crystal.**

```
altairsim> SET cpu0 clock_hz=2000000
```

Here is why, because it is worth understanding once and it explains a whole class of symptom.

**A guest program has no clock. It counts instructions.** `PCGET` times a second the way every
program of the period timed a second — by spinning in a loop and counting the trips. Its own
source says so:

```
MSEC    lxi  d,(159 shl 8)   ;49 cycle loop, 6.272ms/wrap * 159 = 1 second
```

That arithmetic is *only* a second if the crystal is 2 MHz. `PCGET` waits three of them for a
block header. Run the machine flat out and those three seconds — three seconds of **T-states** —
are retired by your host in a few tens of **milliseconds**, while the sending program on the
other end of the wire is still living in seconds of the ordinary kind. `PCGET` concludes the
sender is dead, NAKs, purges the line, and gives up. Nothing is broken. The two ends are simply
no longer using the same clock.

**And that is the boundary, exactly:** timing inside the machine is consistent at any speed,
because everything in there counts the same T-states — which is why a cassette loads correctly
flat out, the ACR and the tape agreeing with each other at whatever speed the pair of them run.
A transfer to your host has **one end inside the machine and one end outside it**, and only the
crystal makes those two agree.

So: flat out for everything the machine does to itself. The real 2 MHz for anything it does with
you. Set it back afterwards if you like the speed — and note the built-in file-transfer board is
not affected by any of this, having no timeouts to expire.

## Two boards are fighting over a port

```
altairsim> SHOW BUS CONTENTION
```

names them. To see the whole map of who decodes what:

```
altairsim> SHOW BUS IO
```

On a real S-100 machine this was two cards strapped to the same address and a bus you could
not trust. Here it is a list.

## An `IN` from a port returns `FF` and I expected something

**Nothing decodes that port.** The bus floats high when no board is driving it, and `FF` is
what a floating bus reads. It is not an error, and the machine will not tell you — a real one
did not either.

To find out who *would* have answered, without running a cycle:

```
altairsim> WHO IO 10
```

## `RESET` did not clear memory

It is not supposed to.

**`RESET` is the bus's RESET\* line.** It does what pulling that line did: the processor goes
to zero, boards return to their power-on state. RAM is RAM; it was not cleared on the real
machine and it is not cleared here.

**`POWER` is the only thing that loses memory.** That is the difference between pressing a
button and pulling a plug, and the manual keeps it.

## macOS refuses to run it

*"cannot be opened because the developer cannot be verified"*.

```
$ xattr -dr com.apple.quarantine ./altairsim
```

Once. It is not a comment on the program; it is what macOS does to every unsigned binary that
arrives in a zip.

If that prints nothing, it worked — and if the flag was never there (a zip fetched with `curl`
or `scp` is not marked; only one a browser or a mail client downloaded is), it also prints
nothing and succeeds. That is the whole reason for `-dr` over a plain `-d`, which announces an
error when there is nothing to remove.
