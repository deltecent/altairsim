# The SD Systems SBC-200 -- a Z80 single-board computer

The **SD Systems SBC-200** was a whole computer on one S-100 board: a 4 MHz Z80, an Intel
8251 serial console, a Z80-CTC baud generator, a parallel port, RAM, and boot-PROM sockets,
acting as the system's bus master. It shipped with the **SD/MS monitor** in EPROM and,
with a VersaFloppy controller, booted SDOS or CP/M.

```
cd examples/sdsys
altairsim sbc200.toml
(press Enter)   ->   .
```

This is a thin delta on the built-in `sbc200` machine (`altairsim sbc200`): the 4 MHz Z80,
the 8251 console at ports 7C/7D, the **MSMONR21** monitor in EPROM at E000, the SD **DDBIOS**
disk BIOS at F000, and RAM filling the rest of the 64K.

## Press Enter to get the prompt

**The monitor prints nothing until you press Enter, and that is authentic.** The SBC-200
wires the 8251's receive-data line to its /DSR input, and MSMONR21 uses that to
**auto-detect your baud rate**: on reset it waits, then times the start bit of the first
character you type (a carriage return), reading status bit 7 in a tight loop. Press Enter
and it matches your speed and prints its `.` prompt.

The simulator models that line in emulated processor time, so the genuine ROM auto-bauds on
the console with no changes. (If a run ever seems to "hang" at startup, it is waiting for
that first CR -- press Enter.)

## The monitor

At the `.` prompt, MSMONR21 gives you a full Z80 monitor -- memory display/edit, fill, move,
search, I/O port access, breakpoints and single-step, and hex arithmetic. A few to try:

```
.D E000 E01F        display the monitor ROM, hex + ASCII
.E 8000             examine/substitute memory at 8000
.H 1234 0100        hex arithmetic: sum then difference
```

Type `.` to abort a command back to the prompt. The full command set is in
`reference/SD Systems Monitor.md`.

## Booting SDOS

`sdos.toml` adds a **VersaFloppy II** floppy controller with a bootable SDOS master disk in
drive A. From the monitor prompt, `C` cold-boots the OS:

```
cd examples/sdsys
altairsim sdos.toml
(press Enter)   ->   .
C               ->   cold-boot SDOS from drive A
```

```
32K SD-OS Version 1.8B
DELTEC ENTERPRISES LLC

[A]
```

`[A]` is the SDOS prompt. The monitor's other disk commands work too: `R`/`W` read and write
128/256-byte sectors, `Z` formats a diskette. The disk is `SDOS-18B-SSDDR-256-32K-MASTER.DSK`
-- an 8" single-sided, double-density, 256-byte-sector image (26 sectors x 77 tracks),
sysgen'd for a 32K system. It is mounted **read/write**, so SDOS can save files; run
`git checkout` on it to restore the master if you change it.

## A video console instead of a serial terminal

`sbc200v.toml` runs the same machine with the **SD Systems VDB-8024** video board as its console
instead of the 8251 serial port. It boots the video build of the monitor (**SDMONV21**), which
prints its `.` prompt straight onto an 80x24 screen -- and with no "press Enter first", because
the VDB is a parallel-handshake terminal, not a serial line with a baud rate to measure.

```
cd examples/sdsys
altairsim sbc200v.toml
.               <- the monitor prompt, ON THE VIDEO SCREEN
```

With SDL3 the screen is a window in the board's own character font; window and terminal keystrokes
both reach the monitor. The command set is identical to the serial machine's. See
`reference/SD Systems VDB-8024.md` for the board.

## What is not here yet

The Z80-CTC baud generator, the parallel port, and the SBC-200 memory switch-out are later
phases. The serial console, the video console, and the VersaFloppy disk all work today.
