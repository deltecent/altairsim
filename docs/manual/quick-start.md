# Quick start: CP/M in one command

This chapter boots CP/M 2.2 on a 56K Altair with an 8″ floppy disk. It also shows you the three
commands that you use every time: `Ctrl-E`, `RUN` and `QUIT`.

Type the commands in the folder where you unzipped the package.

On **macOS**, if you got the package with a browser, type this command first. Without it, macOS
does not run the program:

```
$ xattr -dr com.apple.quarantine ./altairsim
```

If macOS shows a message about the program, press **Done**, not **Move to Trash**. The chapter
*Running it* tells you more.

## Boot CP/M

1. Start the machine:

   ```
   $ ./altairsim examples/cpm/cpm22-buffered.toml
   ```

   On **Windows**, the program is `altairsim.exe`, and the path uses backslashes:

   ```
   > altairsim.exe examples\cpm\cpm22-buffered.toml
   ```

   The machine boots CP/M:

   ```
   56K track-buffered CP/M 2.2b, booting off the 8" floppy in drive A.
   You land at the A> prompt -- type DIR to see what is on the disk.

   Drives B: through D: are empty. FORMAT one from CP/M, or MOUNT an image onto dsk0.
   AltairSim X.Y.Z -- 8080, full speed.
   machine: cpm22-buffered.  HELP for commands.
   startup> RUN FF00
   [console -- ^E returns to the monitor]

   56K CP/M 2.2b v2.3
   For Altair 8" Floppy

   A>
   ```

2. Type `DIR` to see what is on the disk:

   ```
   A>DIR
   A: L80      COM : LADDER   COM : ED       COM : ASM      COM
   A: DUMP     COM : XSUB     COM : PCGET    COM : LS       COM
   A: SUBMIT   COM : LOAD     COM : SURVEY   COM : VIEW     COM
   A: LADDER   DAT : LUNAR    BAS : M80      COM : MAC      COM
   A: MBASIC   COM : PIP      COM : STAT     COM : DDT      COM
   A: MOVCPM8  COM : NSWP     COM : SYSGEN   COM : W        COM
   A: ACOPY    COM : OTHELLO  COM : STARTRK  BAS : TICTAK   BAS
   A: WM       COM : WM       HLP : CRC      COM : PCPUT    COM
   A: AFORMAT  COM : STARINS  BAS : IOBYTE   TXT : R        COM
   A: HDIR     COM
   A>
   ```

   The disk has an assembler, a debugger, Microsoft BASIC, a text editor and Star Trek.

3. Type `MBASIC` to start Microsoft BASIC, and give it a line to run:

   ```
   A>MBASIC
   BASIC-80 Rev. 5.21
   [CP/M Version]
   Copyright 1977-1981 (C) by Microsoft
   Created: 28-Jul-81
   21816 Bytes free
   Ok
   PRINT 2+2
    4
   Ok
   ```

4. Type `SYSTEM` to go back to the `A>` prompt.

## Stop, continue and leave

1. Press **`Ctrl-E`**. The machine stops, and you get the monitor:

   ```
   A>
   STOP -- the machine is still at CA9C. RUN resumes.
   C0Z1M0E1I0 A=00 BC=007F DE=CA01 HL=BC0E SP=BC37 IE=1 PC=CA9C  CALL CA78
   altairsim>
   ```

2. Type `RUN`. The machine continues from the instruction where it stopped. Your `A>` prompt
   is where you left it:

   ```
   altairsim> RUN
   ```

3. Press `Ctrl-E` again, and type `QUIT` to leave the program:

   ```
   altairsim> QUIT
   ```

These are the three commands to remember:

| | |
|---|---|
| **`Ctrl-E`** | Stop the machine, and go to the monitor. The machine loses nothing. |
| **`RUN`** | Start the machine again, from where it stopped. |
| **`QUIT`** | Leave the program. `Q` is enough. There is no `EXIT` command. |

## What `Ctrl-E` does

`Ctrl-E` does what the STOP switch on the front panel does. **The machine stops at the
instruction where it was, and it loses nothing.** STOP is not RESET, and it is not POWER. The
registers, the memory, the disk in the drive and the program counter do not change. While the
machine is stopped, you can read memory, step one instruction at a time and set breakpoints.

`Ctrl-C` is not the stop key, because **`Ctrl-C` belongs to the guest**. CP/M does a warm boot
when you press `Ctrl-C`, and BASIC stops the program. The program reads `Ctrl-E` before the
guest can get it, and every other key goes to the guest. If a guest needs `Ctrl-E`, type
`SET CONSOLE stop=1D` at the monitor. The stop key is then `Ctrl-]`.

## Before you change the disk

**Copy the folder before you write to the disk.** The machine mounts the disk image
read/write. Everything that you do in CP/M changes the file on your computer, and the program
keeps no copy. The folder has all that the machine needs, and the copy boots from any place:

```
$ cp -R examples/cpm my-cpm
$ ./altairsim my-cpm/cpm22-buffered.toml
```

**Go back to the `A>` prompt before you quit or copy the image.** This BIOS keeps a full track
in memory. It writes the track to the disk the next time it reads the console. If you quit
before that, the last write is lost.

To look at a disk that you do not want to change, write-protect it at the monitor:

```
altairsim> MOUNT dsk0:drive0 cpm22b23-56k.dsk WP
```

The controller then refuses every write. CP/M does not know that the disk is write-protected,
and a program that tries to write can fail. Do not use `WP` for a disk that you save work on.
The disks chapter tells you more.

## What happened when it booted

The machine file describes a **56K Altair with an 8″ floppy disk controller and a boot PROM at
`FF00`**. It puts the disk image in drive 0, and it types `RUN FF00` for you. The `startup>`
line shows this command.

**There is no `BOOT` command.** To boot a disk on a real Altair, you set the address switches
to `FF00`, pressed EXAMINE, and then pressed RUN. `RUN FF00` does the same thing. A machine file
can do only what you can type at the monitor.

The rest of the boot is real software. The PROM loads a boot loader from the disk. The boot
loader loads CP/M into memory, and the BIOS of CP/M prints its banner.

The machine file names its disk `cpm22b23-56k.dsk`, with no folder. The program looks for the
disk in the folder of the machine file. For this reason, the copy in `my-cpm` finds its own
disk.

## Where to go next

- **Other machines.** Type `./altairsim --list` to see the built-in machines. The machines
  chapter describes them.
- **A machine with no disk.** The tapes chapter loads Altair 4K BASIC from a cassette. You
  enter the bootstrap by hand, from the listing that MITS printed in its manual.
- **Complete sessions.** The worked examples chapter has more, from start to end.
