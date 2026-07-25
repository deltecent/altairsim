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

## What is not here yet

This example is the **serial console** of the SBC-200. The Z80-CTC baud generator, the
parallel port, the SBC-200 memory switch-out, and a real **VersaFloppy** controller driven
by the DDBIOS (so the monitor's `C`/`R`/`W` disk commands boot SDOS or CP/M) are later
phases. For now the machine boots to the monitor prompt; the disk BIOS is present in EPROM
at F000 but has no controller to talk to.
