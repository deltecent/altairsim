# Troubleshooting

Most problems here are not faults. The machine does what a real machine did, in a way that you
did not expect. This chapter explains the most common cases.

## `Ctrl-C` does not stop the machine

It is not supposed to.

**`Ctrl-C` belongs to the guest.** CP/M reads it to do a warm boot, and BASIC reads it to stop a
program. If the stop key were a key that the guest also uses, the guest would take it, and you
could not stop the machine.

**`Ctrl-E` is the stop key.** It presses the STOP switch on the front panel. The program reads
it before the guest gets the byte, and **no program in the machine can turn it off, ignore it or
take it from you.**

If a guest needs `Ctrl-E` for something of its own, move the STOP key:

```
altairsim> CONSOLE stop=1D
```

The STOP key is then `Ctrl-]`. It must be a control character.

## I pressed STOP, and now the machine does not move

It is **stopped**, not broken. That is what STOP does, and nothing is lost.

STOP stops the processor and gives you the monitor. It is not RESET, and it is not POWER. The
registers, the memory and the disk are as the processor left them. The monitor printed where the
machine stopped, in the line *"still at ..."*.

```
altairsim> RUN
```

`RUN` with no address continues at that instruction, and your `A>` is where you left it.
`RUN <addr>` is different. It loads the PC first, so it *starts again* at that address.

While the machine is stopped, you can look at it. `REGS`, `EXAMINE`, `DUMP`, `DISASM` and `STEP`
all work at this prompt, on a machine that does not move. See *The Debugger*. Use `BREAK` to
stop at a place that you cannot reach by hand.

## My file was not written, or the disk lost my changes

You quit too early.

The CP/M in this package has a **track-buffering BIOS**. It keeps a whole track in memory, and
writes it to the image only when it next reads the console. The simulator does not cause this.
The BIOS works this way, and that is why it was fast. Not every CP/M does this. Some write each
sector at once. You cannot tell from the prompt which kind you have, so think of it as a
buffered one.

**Go back to the `A>` prompt before you quit or copy the image.** When CP/M waits for a key, the
buffer is on the disk. If you stop the program right after a file operation seems to finish, the
last track is not on the disk. The disks chapter gives the details.

## The guest reports a disk error

**The simulator adds no disk errors.** A simulated drive has no dust, no worn surface and no
head out of alignment. When the guest reports `Bad Sector`, `DISK WRITE ERROR` or a directory
that makes no sense, the cause is the setup of the drive or the image itself. Check these
things, in this order:

1. **A `media` line on the drive** in your machine file. Remove it, unless the drive holds a
   blank disk. See below.
2. **Write protection.** Type `SHOW MOUNTS`. A write to a disk that shows `(write-protected)`
   fails in the guest, and CP/M reports it as `Bad Sector`. The disks chapter tells you why.
3. **The image.** An image can be damaged, or it can be for a different machine. A download can
   be cut short, and an archived disk can have sectors that could not be read when somebody made
   the image. Try the image in the machine that it came from, or get another copy.

### `media` is the first thing to check

The program finds the format of a disk from the size of the file: the number of tracks, the
sectors on each track and the bytes in each sector. `media` turns this off, and sets the format
by name. If the name is wrong, the drive reads and writes the file at the wrong places:

- **A format that is too small** gives the drive fewer tracks than the file has. The first
  tracks work, so the directory and the first files are correct. The first read or write past
  the last track fails. CP/M uses the layout in its own BIOS, so `STAT` and `LS` still show the
  full disk and a lot of free space. The error can come late, after many good copies.
- **A format with a different layout**, such as a different sector size or density, puts every
  sector at the wrong place in the file. The errors start at the first read. The guest reads
  wrong data, sees a directory that makes no sense, or reports `Bad Sector`.

For example, this drive holds an 8 MB image, and `media` makes it an 8″ floppy with 77 tracks:

```toml
[[board.drive]]
unit  = 1
media = "8in"            # wrong: this file is an 8 MB disk
mount = "b-8mb.dsk"
```

CP/M copies the first files to B: with no error. Later, a copy that writes past track 76 fails
with `DISK WRITE ERROR`, and it leaves `.$$$` files. Remove the `media` line, and the program
finds the 8 MB format from the size of the file.

**Use `media` only when the size of the file cannot decide the format:**

- a blank disk from `create = true`, because an empty file has no size
- an image that is cut short, or that has a layout that the program does not know

For every other disk, leave `media` out. After you fix the machine file, delete the `.$$$` files
and any file that you are not sure of, and copy again. A write that failed can leave a file that
is only partly written.

## The disk image changed, and I did not want it to

There is no undo. **The image is mounted read/write, as on a real machine.** A CP/M that cannot
write its disk cannot save your work.

There are two answers. Use one *before* you experiment:

```
altairsim> MOUNT dsk0:drive0 file.dsk WP
```

`WP` refuses every write at the controller, so your file is safe whatever the guest does. Use it
for a disk that you only want to **read**. The guest is never told that the disk is
write-protected. The controller has no bit for it, and CP/M has no error for it. For this
reason, a program that tries to write to a `WP` disk does not fail in a controlled way. The
disks chapter tells you why.

The other answer is to copy the folder. It has a machine file and an image in it, and it boots
from any place:

```
$ cp -R examples/cpm my-cpm
```

## `MOUNT` says "no such file", and the file is there

Look at *which machine you run*.

**A relative path starts from the machine's folder, which is the folder that the machine file
was loaded from. It does not start from the folder that you started the program from.** For
example, you go to a folder that has a disk, start a machine from another folder, and type
`MOUNT dsk0:drive1 that.dsk`. The program looks for the disk beside the *machine* file, not in
your folder. `SHOW PATHS` prints the folder that it uses.

The program works this way on purpose, so that an example folder boots from any place. The
machine file says `cpm22.dsk`, and that means *the image beside me*. You can move the folder, or
run it from three levels up, and it still finds its disk. A name that you type finds the same
folder, so the disk that came with the machine and a disk that you mount by hand are in one
place.

## Every prompt ends in a wrong character

```
MEMORY SIZ?
```

**MITS BASIC sets bit 7 of the last character of every message**, to mark the end of the string,
and your terminal prints the result. The `E` is there. It arrived as `C5`, not `45`.

```
altairsim> CONSOLE strip7out=on
```

The real Teletype ignored bit 7 and printed the `E`. `strip7out` makes your terminal do the
same. The serial chapter tells you why the fix belongs on the console, and **never** on the
board.

## Nothing appears when I type, or the guest does not see my keys

Find out which unit has the keyboard:

```
altairsim> SHOW CONSOLE
```

**Only one unit can hold the console.** If the console is on a unit that the guest does not
read, you type into a board that nothing reads. Connect the correct unit. When you connect a
second unit to `console`, it *takes* the console from the first unit, and says so.

## The machine runs very fast, and a cassette loads in one second

The processor and the tape both run **as fast as possible** by default. They have separate
settings:

```
altairsim> SET cpu0 clock_hz=2000000
altairsim> SET acr0:tape rate=real
```

`clock_hz = 2000000` gives the processor the speed of a real 2 MHz Altair. `rate = real` makes a
tape load at its real speed, so a cassette takes its real 110 seconds. Each setting is
independent of the other. The tapes chapter describes both. The fast default and the real speed
are both correct.

## A file transfer times out: `PCGET`, `PCPUT`, XMODEM

**Slow the processor down. A transfer with the outside world needs the real crystal.**

```
altairsim> SET cpu0 clock_hz=2000000
```

This section tells you why, because the same cause explains many problems.

**A guest program has no clock. It counts instructions.** `PCGET` measures a second in the same
way as every program of the period: it runs a loop and counts the turns. Its own source says so:

```
MSEC    lxi  d,(159 shl 8)   ;49 cycle loop, 6.272ms/wrap * 159 = 1 second
```

That count is one second *only* with a 2 MHz crystal. `PCGET` waits three seconds for a block
header. At full speed, your computer runs those three seconds of **T-states** in a few tens of
**milliseconds**. The sending program at the other end of the line still counts real seconds.
`PCGET` decides that the sender is dead, sends a NAK, clears the line, and stops. Nothing is
broken. The two ends no longer use the same clock.

**This is the exact limit.** Timing inside the machine is correct at any speed, because
everything in the machine counts the same T-states. A transfer to your computer has **one end
inside the machine, and one end outside it**. Only the real crystal makes the two ends agree.

For this reason, use full speed for everything that the machine does by itself, and the real 2
MHz for everything that it does with you. You can set it back afterward. The built-in
file-transfer board does not have this problem, because it has no timeouts.

## Two boards use the same port

```
altairsim> SHOW BUS CONTENTION
```

This command names the boards. To see which board decodes each port, type:

```
altairsim> SHOW BUS IO
```

On a real S-100 machine, this was two boards set to the same address, and a bus that gave wrong
values. Here, the program lists the conflicts.

## An `IN` from a port returns `FF`, and I expected a value

**No board decodes that port.** When no board drives the bus, the bus floats high, and a
floating bus reads `FF`. This is not an error, and the machine does not tell you. A real machine
did not either.

To find which board *would* answer, without a bus cycle, type:

```
altairsim> WHO IO 10
```

A **hang** can have the same cause. A machine that you built yourself starts, prints nothing,
and never returns. It reads a port for a board that is not there, and gets `FF` forever. Tell
the bus to report it:

```
altairsim> SET BUS UNCLAIMED=WARN
altairsim> RUN
warning: IN 10 -> FF at PC=0043: no board decodes port 0x10. it floated to 0xFF.
```

`WARN` prints the port and the address of the instruction, and the machine continues. `HALT`
stops the machine at that instruction, so that `REGS` and `DUMP` show it as it stopped. The
setting watches I/O only, because a program often reads memory where nothing is. It reports each
missing port once in each run. The default is `SILENT`. Turn it on when you need it.

## `RESET` did not clear memory

It is not supposed to.

**`RESET` is the RESET\* line of the bus.** It does what that line did: the processor goes to
address zero, and the boards go back to their power-on state. RAM keeps its contents. The real
machine did not clear it, and the program does not clear it.

**Only `POWER` loses memory.** That is the difference between pressing a button and pulling the
plug, and the program keeps that difference.

## macOS refuses to run the program

macOS 27 shows *"altairsim" Not Opened* and *Apple could not verify "altairsim" is free of
malware*. Earlier versions show *"cannot be opened because the developer cannot be verified"*.

**Press Done. Do not press Move to Trash**, because that button deletes the program.
After you press Done, type:

```
$ xattr -dr com.apple.quarantine ./altairsim
```

Do this one time for each download. Each new archive has the mark again. The chapter
*Running it* tells you more.
