# Worked examples

Complete sessions. **Every transcript below was captured from the program**, not typed out
from memory — if it says the machine printed something, the machine printed it.

These are a few of the machines in `examples/`, gone through at length because each one teaches
something about how the machine works rather than merely how to start it. **They are not the
whole of what is there.** Every folder under `examples/` carries its own README — as Markdown
and as a PDF beside it — saying what that machine is and what to type, so the place to see what
you have is the directory itself, not a list in here.

---

## 1. CP/M from a floppy

```
$ altairsim {{MACHINE_CPM}}
```

```
startup> RUN FF00
[console -- ^E returns to the monitor]

56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>
```

### What is on the disk

Ask the obvious way and the answer is alarming:

```
A>DIR
A: L80      COM

A>
```

**One file, on a disk with 18K free.** The arithmetic does not work, and it is not supposed to:
almost everything on this disk is marked `$SYS`, and `$SYS` is precisely the attribute that
hides a file from `DIR`. It is how a period system disk kept its utilities out of the way of
your own filenames. `STAT` sees through it:

```
A>STAT *.*

 Recs  Bytes  Ext Acc
   31     4k    1 R/W A:ACOPY.COM
   23     4k    1 R/W A:AFORMAT.COM
   64     8k    1 R/W A:ASM.COM
   17     4k    1 R/W A:CRC.COM
   38     6k    1 R/W A:DDT.COM
   ...            ...  (thirty-eight lines in all)
  190    24k    2 R/W A:MBASIC.COM
   58     8k    1 R/W A:PIP.COM
  149    20k    2 R/W A:STARTRK.BAS
   83    12k    1 R/W A:WM.COM
Bytes Remaining On A: 18k

A>
```

Thirty-eight files: a macro assembler, a linker, Microsoft BASIC, `DDT`, `PIP`, a text editor,
a disk formatter, and Star Trek. It is a working 1977 development system, and `STAT *.*` is how
you look at it.

### Out, and back in

`^E` stops the machine and gives you the monitor. Nothing is lost — a bare `RUN` resumes at the
instruction it was about to execute.

```
A>
ATTN -- the machine is still at CA9C. RUN resumes.
C0Z1M0E1I0 A=00 B=007F D=CA01 H=BC0E S=BC37 IE=1 P=CA9C  CALL CA78
altairsim>
```

Look at the machine while it sits there:

```
altairsim> BOARDS
altairsim> SHOW dsk0
altairsim> DUMP 100
```

…and hand the keyboard back:

```
altairsim> RUN
```

Your `A>` prompt is exactly where you left it.

### Before you write anything

The disk is mounted **read/write** and there is no undo. Copy the folder first — it is
self-contained and boots from anywhere:

```
$ cp -R examples/cpm my-cpm
$ altairsim my-cpm/cpm22-buffered.toml
```

And when you are done writing files, **get back to the `A>` prompt before you quit.** The BIOS
holds a track in memory and only flushes it when it next reads the console. The disks chapter
explains why.

---

## 2. Altair BASIC from a cassette

```
$ altairsim {{MACHINE_BASIC}}
```

```
startup> MOUNT acr0:tape "4K BASIC Ver 3-1.tap"
acr0:tape: mounted 4K BASIC Ver 3-1.tap
startup> LOAD "LDR4K31.HEX"
loaded 20 bytes from LDR4K31.HEX (0000-0013)
startup> RUN 0
[console -- ^E returns to the monitor]

MEMORY SIZE?
TERMINAL WIDTH?
WANT SIN? Y

742 BYTES FREE

ALTAIR BASIC VERSION 3.1
[FOUR-K VERSION]

OK
PRINT 6*7
 42

OK
```

**Seven hundred and forty-two bytes free.** That is the machine Microsoft was founded on.

### What those three startup lines actually are

They are not magic, and they are not a boot command — **there is no boot command.** They are
the three things an operator did in 1975, written down:

| | |
|---|---|
| `MOUNT acr0:tape "…"` | **Put the cassette in the recorder and press PLAY.** |
| `LOAD "LDR4K31.HEX"` | **Toggle in the bootstrap.** Twenty bytes, entered by hand on the front-panel switches. MITS printed them in the manual. |
| `RUN 0` | **Set the switches to zero, press EXAMINE, then press RUN.** EXAMINE is what puts the switches into the program counter; without it RUN carries on from wherever the machine already was. |

Anything you can type at the prompt, a machine file can do. It gets no special powers, and
that is why you can do this yourself:

```
altairsim> REWIND acr0:tape
altairsim> RUN 0
```

`REWIND` is a verb the **cassette board brings with it** — it exists only because there is an
ACR in the machine. You need it to load the same tape twice, for exactly the reason you would
have needed it in 1975.

### The tape is not in the machine file

Look again at what the machine file declares: a front panel, a processor, a serial board, a
cassette interface, and some memory. **It does not declare the tape.**

A machine file describes **hardware**. Which cassette is in the recorder is not hardware —
and there is no motor control on the board to make it one. You put the tape in, and you press
PLAY. That is what `MOUNT` is.

### One number in that file you could not have guessed

The front panel's `sense` switches are set to `80`. That is `A15` up, and nothing else.

The bootstrap's own printed header says why:

```
** Set A15 on (cassette load) **
** All other switches off **
```

`A15` up means *load from the cassette*. Get it wrong and BASIC comes up talking to the wrong
device, or to nothing at all. Period software reads those switches, so they are not decoration.

### It loaded in a second, and a real one took two minutes

The machine runs **flat out** by default. A 300-baud cassette that took a real Altair 110
seconds comes off in about one.

If you want the real thing:

```
altairsim> SET cpu0 clock_hz=2000000
altairsim> REWIND acr0:tape
altairsim> RUN 0
```

…and now it takes 110 seconds, because the tape costs the same number of T-states either way.
**What the guest sees is identical.** The crystal buys period *feel*, not period *behaviour*.

### The one before this one: BASIC 1.0

Beside the 4K is `basic1.toml`, and it boots **"8080 BASIC VER 1.0"** — the oldest Altair BASIC
there is, the one Bill Gates and Paul Allen wrote in 1975. Same idea, one difference that is worth
seeing:

```
$ altairsim {{MACHINE_BASIC1}}
```

```
tape: 00:15 / 02:30 (100%)

[console -- ^E returns to the monitor]
RUN 0

MEMSIZ? 
WANT SIN-COS-ATN? 

2000 BYTES FREE

8080 BASIC VER 1.0

READY
```

**It takes two moves, not one.** The 4K's bootstrap jumps into BASIC on its own when the tape
runs out. BASIC 1.0's does not — it copies the tape into memory from `0000` and loops forever,
because it has no idea how long the tape is. That is not a shortcoming of the simulator; it is
what the 1975 operator faced. So you watch the tape load, press **`^E`** when it is done, and type
**`RUN 0`** to start BASIC by hand — the STOP/RESET/RUN a real operator did at the front panel.

Two small things that look like bugs and are not: the prompt really is `MEMSIZ?` — Microsoft set
the end-of-message bit on the `Z` rather than spend a byte on a trailing `E` — and at the flat-out
default the tape loads in an instant, so `tape: 02:30 / 02:30 (100%)` is the counter's *finished*
frame. `SET acr0:tape rate=real` before you run plays it at the true 300-baud speed and the
counter climbs through the two and a half minutes it took in 1975.

---

## 3. A Sol-20 loading {{NAME_SOL}} off cassette

```
$ altairsim {{MACHINE_SOL}}
```

The Sol-20 is not an Altair with a terminal on it. It is an integrated computer with a
**keyboard and a screen built in**, and that changes what this example looks like on your
terminal — so read the next paragraph before deciding something is broken.

**The Sol's screen is a VDM-1, and nothing it displays reaches your terminal.** SOLOS signs on,
`XE TRK80` echoes, the game paints its starfield — all of it into the VDM's video memory at
`CC00`–`CFFF`, which is a *display*, not a serial line. If altairsim was built with SDL you get
a window and you watch it there. If it was not, the machine still runs perfectly and the
console stays quiet. That quiet is correct.

Your typing goes the other way, and does work: keystrokes at the terminal reach the Sol's
keyboard port just as the window's would.

```
startup> MOUNT sol0:tape1 "TRK80.WAV"
sol0:tape1: mounted TRK80.WAV
TRK80.WAV: cuts1200, 7939 bytes, 0 framing errors (100.0% of frames intact)
startup> TYPE "XE TRK80\r"
startup> SET vdm0 cursor=steady
vdm0: cursor=steady
startup> RUN C000
[console -- ^E returns to the monitor]
```

Every line there is the machine file's `startup`, run before you arrive — you type nothing.
`MOUNT` puts the tape in the deck; `TYPE "XE TRK80\r"` queues that command at the console,
where SOLOS's type-ahead reads it at its first prompt; `SET vdm0 cursor=steady` stops the
game's reverse-video cells strobing; and `RUN C000` cold-starts SOLOS, which signs on, reads
the queued `XE TRK80`, and loads the game. On a real Sol-20 you would type `XE TRK80` at the
SOLOS prompt yourself — here `TYPE` is the monitor command that does it for you.

Then wait. **The tape takes about a minute**, because `{{MACHINE_SOL}}` sets the Sol's real
2.045 MHz and a cassette at 1200 baud takes what a cassette took. That minute is the example
being honest, not the simulator being slow — `SET cpu0 clock_hz=0` buys the wall clock back
and changes nothing the guest can observe.

### Reading the screen without a screen

With no window, you can still see what the game painted: stop the machine and dump the VDM's
memory. Each row is 64 bytes and `DUMP` prints the ASCII beside the hex, so one row is one
line. `^E` first, then:

```
altairsim> DUMP CD00-CD3F WIDTH=64
CD00  43 4F 50 59 52 49 47 48 54 ... 43 4F 52 50 2E  COPYRIGHT (C) 1977  PROCESSOR TECHNOLOGY CORP.
```

That is the game's own copyright line, off a 1977 tape, read out of the screen it drew it on.
And the row near the bottom is where it stops to ask you something:

```
altairsim> DUMP CFC0-CFFF WIDTH=64
CFC0  45 4E 54 45 52 20 53 50 ... 54 29 29           ENTER SPEED FACTOR (9(SLOW)-0(FAST))
```

`RUN` resumes, and the answer you type reaches the game.

### The same tape, as bytes

`{{WAV_SOL}}` is a cassette recording — real tones, which is why its mount line above counts
framing errors as it demodulates them. Beside it is `{{TAPE_SOL}}`, the *same* program already
decoded to bytes; the machine reads either, so you can mount it in place of the WAV:

```
altairsim> MOUNT sol0:tape1 TRK80.TAP
sol0:tape1: mounted TRK80.TAP
```

Everything else is unchanged: SOLOS's tape reader cannot tell the two apart, because with the
WAV the demodulation happens once, at mount — there are no tones left to decode by the time the
guest reads. The tapes chapter has the detail.

---

## 4. {{NAME_DISKBASIC}} from a floppy

```
$ altairsim {{MACHINE_DISKBASIC}}
```

The BASIC in example 2 comes off a cassette and forgets everything when you turn it off. This
one lives on an 8" floppy and has files, a directory, and a `SAVE` that takes a name. It boots
from the same DBL PROM at `FF00` that CP/M does — `startup` runs it for you.

**It interviews you first**, and this is the example where knowing the answers matters:

```
MEMORY SIZE? 
LINEPRINTER? C
HIGHEST DISK NUMBER? 0
HOW MANY FILES? 
HOW MANY RANDOM FILES? 

37033 BYTES FREE
ALTAIR BASIC REV. 4.1
[DISK EXTENDED VERSION]
COPYRIGHT 1977 BY MITS INC.
OK
```

Three of the five take a bare Return. `HIGHEST DISK NUMBER?` is `0`, because there is one
drive and it is numbered from zero.

**`LINEPRINTER?` takes `C`, `O` or `Q` — and re-asks in silence on anything else.** No error,
no hint. Answer it with Return or `N` and you get the prompt back, forever, which looks like a
hang and is not one. `C` is the 88-C700 line printer; it is legal here even with no printer
board fitted, since the answer only decides where `LPRINT` would go.

That is the whole trick. Past it, it is BASIC, and the tapes chapter has the longer version.

---

## Where to go next

- **The examples this chapter did not walk through** — `examples/`, and the README in each folder.
- **Move a file between CP/M and your own machine** — the file-transfer chapter (`R`, `W`, `HDIR`).
- **Telnet into the guest, or wire it to a real serial port** — the serial chapter.
- **Look at the bus while it runs** — the debugging chapter.
