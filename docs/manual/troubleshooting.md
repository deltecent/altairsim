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

It is not stuck. It is running.

**ATTN does not stop the machine — it takes the keyboard back.** The processor is still
executing, exactly where it was, and the monitor told you where when it printed the prompt.

```
altairsim> RUN
```

resumes, and your `A>` is where you left it.

If you want to actually *stop* it and look at it — halt the processor, examine registers,
single-step — that is `BREAK` and `STEP`, and that is the debugging chapter.

## My file was not written / the disk lost my changes

You quit too early.

The CP/M BIOS keeps a **track buffer in memory** and only flushes it to the image when it
next reads the console. This is not a shortcut in the simulator; it is what the BIOS does,
and it is why the real machine was fast.

**Get back to the `A>` prompt before you quit or copy the image.** The moment CP/M asks you
for a keystroke, the buffer has landed. Kill the program the instant a file operation appears
to have finished and the last track never reached the disk. The disks chapter has the detail.

## The disk image changed and I wanted it not to

There is no undo. **The image is mounted read/write, like a real machine** — a CP/M whose
`A:` is read-only fails on its first `PIP`.

Two answers, and take one *before* you experiment:

```
altairsim> MOUNT dsk0:drive0 file.dsk RO
```

or copy the folder. It is a directory with a machine file and an image in it, and it boots
from anywhere:

```
$ cp -R disks/cpm22 my-cpm
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
card.

## Nothing appears when I type / the guest does not see my keys

Ask who has the keyboard.

```
altairsim> SHOW CONSOLE
```

**Exactly one unit may hold the console.** If the console is on a unit the guest is not
reading, you are typing into a card nobody is listening to. Connect the right one — and note
that connecting a second unit to `console` *steals* it from the first, and says so.

## The machine runs impossibly fast — a cassette loads in one second

It is running **flat out**, which is the default. `clock_hz = 0` means "no divisor, go".

```
altairsim> SET cpu0 clock_hz=2000000
```

That is the real 2 MHz Altair, and it will give you the real 110-second cassette load. Both
behaviours are correct; one of them is just a much longer lunch.

## Two cards are fighting over a port

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

**Nothing decodes that port.** The bus floats high when no card is driving it, and `FF` is
what a floating bus reads. It is not an error, and the machine will not tell you — a real one
did not either.

To find out who *would* have answered, without running a cycle:

```
altairsim> WHO IO 10
```

## `RESET` did not clear memory

It is not supposed to.

**`RESET` is the bus's RESET\* line.** It does what pulling that line did: the processor goes
to zero, cards return to their power-on state. RAM is RAM; it was not cleared on the real
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
