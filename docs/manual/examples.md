# Worked examples

Two complete sessions. **Every transcript below was captured from the program**, not typed out
from memory — if it says the machine printed something, the machine printed it.

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

A macro assembler, a linker, Microsoft BASIC, `DDT`, `PIP`, a text editor, and Star Trek. It
is a working 1977 development system.

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
startup> MOUNT sol0:tape1 "TRK80.TAP"
sol0:tape1: mounted TRK80.TAP
altairsim> RUN C000
[console -- ^E returns to the monitor]
XE TRK80
```

The `MOUNT` is the machine file's doing — the tape is in the deck before you arrive. `RUN C000`
cold-starts SOLOS, and `XE TRK80` is yours to type: `startup` runs *monitor* commands, and no
machine file can type at a guest.

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

### The same tape, as audio

`{{TAPE_SOL}}` is a file of bytes. Beside it is `{{WAV_SOL}}` — the *same* program as a
cassette recording — and the machine reads either:

```
altairsim> MOUNT sol0:tape1 TRK80.WAV
TRK80.WAV: cuts1200, 7939 bytes, 0 framing errors (100.0% of frames intact)
```

Everything above this line is unchanged: SOLOS's tape reader cannot tell, because the
demodulation happens once, at mount. The tapes chapter has the detail.

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

- **Move a file between CP/M and your own machine** — the file-transfer chapter (`R`, `W`, `HDIR`).
- **Telnet into the guest, or wire it to a real serial port** — the serial chapter.
- **Look at the bus while it runs** — the debugging chapter.
