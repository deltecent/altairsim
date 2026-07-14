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

`^E` takes the keyboard back. The machine keeps running.

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
$ cp -R disks/cpm22 my-cpm
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

`REWIND` is a verb the **cassette card brings with it** — it exists only because there is an
ACR in the machine. You need it to load the same tape twice, for exactly the reason you would
have needed it in 1975.

### The tape is not in the machine file

Look again at what the machine file declares: a front panel, a processor, a serial card, a
cassette interface, and some memory. **It does not declare the tape.**

A machine file describes **hardware**. Which cassette is in the recorder is not hardware —
and there is no motor control on the card to make it one. You put the tape in, and you press
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

## Where to go next

- **Move a file between CP/M and your own machine** — the file-transfer chapter (`R`, `W`, `HDIR`).
- **Telnet into the guest, or wire it to a real serial port** — the serial chapter.
- **Look at the bus while it runs** — the debugging chapter.

> **{{NAME_DISKBASIC}}** is not in the package yet. When it is, it will boot the same way
> everything else here does: name its machine file, and it runs.
