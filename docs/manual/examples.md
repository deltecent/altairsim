# Worked examples

These are complete sessions. **Every transcript below was captured from the program.** None of
them was typed from memory.

This chapter goes through two of the machines in `examples/` in detail. Each one shows something
about how the machine works, not only how to start it. Every folder in `examples/` has its own
README, in Markdown and as a PDF, that says what the machine is and what to type.

More examples, with more documentation, are at https://altairsim.com.

---

## 1. CP/M from a floppy disk

```
$ altairsim examples/cpm/cpm22-buffered.toml
```

```
startup> RUN FF00
[console -- ^E returns to the monitor]

56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>
```

### What is on the disk

`DIR` lists the files:

```
A>DIR
A: L80      COM : LADDER   COM : ED       COM : ASM      COM
A: DUMP     COM : XSUB     COM : PCGET    COM : LS       COM
A: SUBMIT   COM : LOAD     COM : SURVEY   COM : VIEW     COM
A: LADDER   DAT : LUNAR    BAS : M80      COM : MAC      COM
A: MBASIC   COM : PIP      COM : STAT     COM : DDT      COM
A: MOVCPM8  COM : NSWP     COM : SYSGEN   COM : W        COM
A: ACOPY    COM : VI       DOC : STARTRK  BAS : TICTAK   BAS
A: VI       COM : CRC      COM : PCPUT    COM : AFORMAT  COM
A: STARINS  BAS : IOBYTE   TXT : R        COM : HDIR     COM
A: SIMCLK   COM
A>
```

There are 37 files: a macro assembler, a linker, Microsoft BASIC, `DDT`, `PIP`, a text editor, a
disk formatter and Star Trek. It is a complete development system of 1977. `STAT *.*` gives the
size of each file, and the space that is left:

```
A>STAT *.*

 Recs  Bytes  Ext Acc
   31     4k    1 R/W A:ACOPY.COM
   23     4k    1 R/W A:AFORMAT.COM
   64     8k    1 R/W A:ASM.COM
   17     4k    1 R/W A:CRC.COM
   38     6k    1 R/W A:DDT.COM
   ...
  168    22k    2 R/W A:VI.COM
  123    16k    1 R/W A:VI.DOC
    6     2k    1 R/W A:XSUB.COM
Bytes Remaining On A: 6k
```

### Out, and back in

`Ctrl-E` stops the machine and gives you the monitor. Nothing is lost. `RUN` with no address
continues at the instruction that the machine was about to execute.

```
A>
STOP -- the machine is still at CA9C. RUN resumes.
C0Z1M0E1I0 A=00 BC=007F DE=CA01 HL=BC0E SP=BC37 IE=1 PC=CA9C  CALL CA78
altairsim>
```

Look at the machine while it is stopped:

```
altairsim> BOARDS
altairsim> SHOW dsk0
altairsim> DUMP 100
```

To give the keyboard back to CP/M, type:

```
altairsim> RUN
```

Your `A>` prompt is where you left it.

### Before you write anything

The disk is mounted **read/write**, and there is no undo. Copy the folder first. The folder has
everything that the machine needs, and it boots from any place:

```
$ cp -R examples/cpm my-cpm
$ altairsim my-cpm/cpm22-buffered.toml
```

When you finish writing files, **go back to the `A>` prompt before you quit.** The BIOS keeps a
track in memory, and writes it to the disk only when it next reads the console. The disks
chapter tells you why.

---

## 2. Altair BASIC from a cassette

```
$ altairsim examples/basic4k/basic4k.toml
```

```
startup> MOUNT acr0:tape "4K BASIC Ver 3-1.tap"
acr0:tape: mounted 4K BASIC Ver 3-1.tap
startup> LOAD "LDR4K31.HEX"
loaded 20 bytes (1 page) from LDR4K31.HEX (0000-0013)
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

**742 bytes free.** Altair BASIC was Microsoft's first product.

### What the three startup lines are

They are not a boot command. **There is no boot command.** They are the three things that an
operator did in 1975:

| | |
|---|---|
| `MOUNT acr0:tape "…"` | **Put the cassette in the recorder and press PLAY.** |
| `LOAD "LDR4K31.HEX"` | **Toggle in the bootstrap.** Twenty bytes, entered by hand on the front-panel switches. MITS printed them in the manual. |
| `RUN 0` | **Set the switches to zero, press EXAMINE, then press RUN.** EXAMINE puts the switches into the program counter. Without it, RUN continues from where the machine already was. |

A machine file can do what you can type at the prompt, and nothing more. For this reason, you
can do the same yourself:

```
altairsim> REWIND acr0:tape
altairsim> RUN 0
```

`REWIND` is a command that **the cassette board adds**. It exists only because the machine has
an ACR. You need it to load the same tape a second time, as you did in 1975.

### The tape is in a startup command, not in a key

The machine file declares a front panel, a processor, a serial board, a cassette interface and
some memory. **No key in it holds the tape.** A machine file's keys describe **hardware**. The
cassette in the recorder is not hardware, and the board cannot control the motor. The `startup`
list types the `MOUNT` for you, as you would type it. The tapes chapter tells you more.

### The sense switches

The front panel's `sense` switches are set to `80`. That is `A15` up, and nothing else. The
bootstrap's own printed header says why:

```
** Set A15 on (cassette load) **
** All other switches off **
```

`A15` up means *load from the cassette*. If the setting is wrong, BASIC talks to the wrong
device, or to nothing. The tapes chapter tells you more.

### It loaded in one second, and a real Altair took two minutes

The machine and the tape both run **as fast as possible** by default. A 300-baud cassette that
took a real Altair 110 seconds loads in about one second.

To load at the real speed:

```
altairsim> SET acr0:tape rate=real
altairsim> REWIND acr0:tape
altairsim> RUN 0
```

The load now takes 110 seconds. **The guest sees the same result.** The tape has its own clock,
separate from the processor's `clock_hz`, as on the real hardware. The tapes chapter describes
both clocks.

---

## Where to go next

- **The examples that this chapter does not describe:** `examples/`, and the README in each
  folder.
- **More machines:** https://altairsim.com has more examples and their documentation.
- **Move a file between CP/M and your computer:** the file-transfer chapter (`R`, `W`, `HDIR`).
- **Use telnet to connect to the guest, or connect it to a real serial port:** the serial
  chapter.
- **Look at the bus while the machine runs:** *The Debugger*.
