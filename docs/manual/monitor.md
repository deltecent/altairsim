# The monitor

The `altairsim>` prompt is **the monitor**: the front panel of the machine, and its debugger,
which here are the same thing. Everything the front panel of a real Altair could do, the
monitor can do — and a great deal it could not. It breakpoints, it single-steps, it
disassembles, and it will show you the bus itself: who decodes what, who is pulling which
interrupt line, and where two boards are fighting over an address. That is the **debugging**
chapter, and it is most of why this program exists.

What it is not is a *menu* — a layer sitting between you and the machine, offering a fixed set
of things it is prepared to let you inspect. There is no debug mode to enter and nothing is
watching from the outside. A breakpoint is **the machine stopping**, not a script noticing that
it should have. `IN` and `OUT` run **real bus cycles**, with every side effect a real one has —
read a UART's data port at this prompt and you have taken the byte, exactly as the guest would
have. The panel and the debugger are one object because on an Altair they were one object: a
man at the switches, reading lamps.

The machine does not have to be running for the monitor to work. Most of what follows —
examining memory, running a bus cycle, fitting a board — works on a machine with the power on
and the processor idle, which is exactly the arrangement the front panel was for.

## Commands resolve by prefix

There are **no aliases and no memorised abbreviations**. You type as much of a command as it
takes to be unambiguous, and the first command that matches wins.

`HELP` prints the whole menu with each command's shortest form in brackets:

```
  BO[ARDS]          B[REAK]           COM[PARE]         C[ONFIG]
  CONN[ECT]         CONS[OLE]         DE[POSIT]         DI[SASM]
  DISC[ONNECT]      D[UMP]            E[DIT]*           EX[AMINE]
  F[ILL]            HE[LP]            H[ISTORY]         I[N]
  L[OAD]            M[OUNT]           MOV[E]            N[EXT]
  NO[BREAK]         O[UT]             P[OWER]           Q[UIT]
  REC[ORD]*         REGI[ON]          RE[GS]            REP[LAY]*
  RES[ET]           REST[ORE]*        R[UN]             SA[VE]
  SEA[RCH]          SE[T]             SH[OW]            SN[APSHOT]*
  S[TEP]            STO[P]*           SY[MBOLS]         T[RACE]
  U[NMOUNT]         W[HO]
```

Type the part before the bracket. `D` is `DUMP`; `DE` is `DEPOSIT`; `RES` is `RESET`. Case
does not matter, here or in the name of any board.

The `*` marks the six commands that **resolve but are not built yet**. Type `SN` and it will
tell you that `SNAPSHOT` is waiting on the debugger. That is on purpose: their abbreviations are
claimed *now*, so the day they land, `SN` does not quietly stop meaning what your fingers have
learned it means. An abbreviation is a contract.

`HELP <command>` gives you the usage and worked examples for one of them, and `?` is the same
as `HELP`.

> **`R` is `RUN`, not `RESET`.** It is the command you type every session, and it is the one
> that costs nothing if you did not mean it. A bare `R` that reset the machine would be one
> you had to set up again. `RESET` pays the letters: `RES`.

## Numbers: one rule, and it is not negotiable

> **On the wire → hex. Never on the wire → decimal.**

If the 8080 can see it, it is **hex**: an address, a port, a data byte, a register.
If it never leaves your head, it is **decimal**: a count, a width, a size, a drive number.

```
DUMP 100            address  -> 0100 hex
STEP 10             a count  -> ten instructions
OUT FF 55           port and byte -> both hex
SET sio0:a baud=9600      a baud rate -> nine thousand six hundred
```

You can always force the issue: `0x`, `$` and a trailing `h` force hex; a leading `#` forces
decimal; and a `K` or `M` suffix is **always** decimal (`48K` is 49,152 — so `0x10K` is a
contradiction and is rejected rather than guessed at).

This rule is the same everywhere — in the monitor, in a machine file, and in every board's
settings. There is no second convention to learn.

## Naming a board: `<id>[:<unit>]`

Every board in the machine has an **id** you chose (`cpu0`, `sio0`, `dsk0`), and some boards
have **units** inside them — the two channels of a serial board, the four drives on a floppy
controller, the ROM socket on a memory board.

```
SHOW sio0              the board
SET  sio0:a baud=1200  one channel of it
MOUNT dsk0:drive1 my.dsk
```

**You may leave out anything that carries no information.** If there is only one floppy
controller in the machine, `dsk` will find it. If a board has only one thing you could mount
into, you need not name it — `MOUNT ACR tape.bin` puts a cassette in the one recorder.

But anything **genuinely plural you must say**. There are four drives on that controller and
the machine will not guess which one you meant; it will tell you so and stop.

## Seeing the machine

```
altairsim> BOARDS
  ID    TYPE    I/O       UNITS                       MEMORY
  ----  ------  --------  --------------------------  ------------------------------
  fp0   fp      FF        -                           -
  cpu0  8080    -         1 cpu: 8080                 -
  sio0  2sio    10,12     2 serial: a*, b             -
  dsk0  dcdd    08,09,0A  4 disk: drive0(empty), ...  -
  mem0  memory  -         1 rom: rom0                 0000-DFFF  ram  56K
                                                      FF00-FFFF  rom  dbl  phantom:all

  * holds the console
```

That is the backplane: what is plugged in, what ports each board answers to, what is in its
units, and what it decodes in memory.

| Command | Shows |
|---|---|
| `BOARDS` | the backplane |
| `SHOW <id>` | one board: every setting, its value, and what it will accept |
| `SHOW MACHINE` | the whole machine |
| `SHOW CONSOLE` | which unit holds your keyboard, and how bytes are being transformed |
| `SHOW BUS MAP` | who decodes which addresses — and what floats |
| `SHOW BUS IO` | who decodes which ports |
| `SHOW BUS IRQ` | who is strapped to which interrupt line, and who is pulling it |
| `SHOW BUS CONTENTION` | where two boards are fighting |

`SHOW <id>` is worth dwelling on, because it is the **only** thing you need in order to
configure a board. It lists every property, what it is set to, and what values are legal —
and those property names **are** the keys you write in a machine file. There is no second
schema anywhere in this program. The board reference at the back of this manual is printed
from the same source.

## Changing the machine

```
SET cpu0 clock_hz=2000000      give it the real 2 MHz crystal
SET mem0 fill=zero             RAM comes up zeroed instead of random
SET fp0  sense=80              set the SENSE switches
BOARDS ADD 2sio sio1 port=20   fit a second serial board
BOARDS REMOVE sio1             pull it out
CONFIG SAVE mine.toml          write out the machine you are actually running
```

`CONFIG SAVE` round-trips: what it writes, `altairsim mine.toml` will boot.

## Running, and stopping

```
RUN FF00     load the PC and go — the same two motions as the panel's switches
RUN          carry on from wherever the processor is
```

**`RUN <addr>` is EXAMINE followed by RUN**, exactly as you would do it on the front panel.
There is no `BOOT` command in this program, and there should not be: a machine that ought to
start says so with the operator's own keystroke.

If a board holds the console, **the guest gets the keyboard** — every key, including `^C`,
which a CP/M program is entitled to read.

### ATTN takes it back

**`^E`** is ATTN. The host intercepts it *before the guest is ever offered the byte*, so no
program running inside the machine can disable it, trap it, or take it from you.

```
A>
ATTN -- the machine is still at CA9C. RUN resumes.
altairsim>
```

**ATTN stops the machine and gives you the monitor.** Nothing executes while this prompt is up.
But it stops the machine without *disturbing* it — ATTN is not RESET and not POWER, so the
registers, the memory and the disk are exactly as the guest left them, and a bare `RUN` (no
address) picks up at the very instruction it was about to execute. That is what *"still at
CA9C"* is telling you.

`CONSOLE attn=1D` moves ATTN to `^]` if `^E` collides with something the guest wants.

### What stops it for real

A `RUN` ends when it hits a **breakpoint**, or a `HLT` that nothing can wake — and it always
says which. With no console connected there is nothing to hand the keyboard to, so it simply
runs, and `^C` stops it.

## Speed

**It runs flat out by default.** `clock_hz` on the CPU board is `0`, which means "as fast as
this host can go", and a cassette that took a real Altair 110 seconds comes off in about one.

```
SET cpu0 clock_hz=2000000
```

…buys back the 2 MHz machine, and with it the 110 seconds. **What the guest sees is identical
either way** — the tape still costs the same number of T-states, the timing loops still count
the same. The crystal buys period *feel*, not period *behaviour*.

**`SHOW cpu0` reports back what it actually reached.** Beside `clock_hz` — the crystal you
asked for — sits `achieved_hz`, the clock the run loop hit the last time it ran. Flat out,
that is how fast this host went; with a crystal set, it is how close the pacing landed. It
reads `0` until the machine has run, and you cannot set it — it is a measurement, not a knob.

**Until the guest talks to something outside the machine.** A program measures time by counting
instructions, so at `clock_hz = 0` a timeout it believes is three seconds can expire in thirty
milliseconds of yours — which is why an XMODEM transfer to your host wants the real crystal, and
a cassette does not. See the troubleshooting chapter.

## RESET is not POWER

| | |
|---|---|
| `RESET` | the bus's RESET* line. The processor restarts at `0000`. **Memory survives**, disks stay mounted. |
| `POWER` | a power cycle. **This is the only thing that loses RAM** and re-reads the ROM images. |

`RESET` does not clear memory because pressing RESET on a real Altair did not clear memory.
That is not a simplification; it is the behaviour a lot of period software depends on.

### RESET* is a wire, and every board is listening

The processor is not the only thing that hears it. `RESET*` is a **line on the backplane**, and
it runs past every board in the machine — so `RESET` is not an instruction the simulator carries
out on your behalf. It is a signal put on the bus, and each board answers it the way its own
silicon answered it, which is not the same answer twice:

- The **memory board** clears its bank latch — and does not touch one byte of RAM. A RAM chip has
  no reset pin to touch it with.
- The **floppy controller** flushes the sector it was in the middle of writing, deselects the
  drive, and lets the head unload. On the 5¼″ minidisk the motor spins down; on the 8″ drive
  nothing spins down, because that card has no motor control to spin down *with*.
- The **2SIO does nothing at all**, and that is the most instructive answer of the three. The
  MC6850 has **no reset pin** — Vss, RxD, RxCLK, TxCLK, RTS, TxD, IRQ, CS0–CS2, RS, Vcc, R/W, E,
  D0–D7, /DCD, /CTS, and that is the entire package. `RESET*` reaches the card and has **nowhere
  to land**, so the baud rate, the word format, RTS and the interrupt enables all survive a
  reset, exactly as they do on the bench. (The 6850's *master reset* is a real thing, but it
  belongs to the **guest**: a program performs it by writing to the control register. The front
  panel cannot do it for you.)

Which is why a reset here behaves like a reset there. Hit `RESET` in the middle of a disk write
and you get what the hardware gave you: a half-written sector on the disk, a serial port still
configured exactly as the dead program left it, and every byte of your RAM intact and waiting
to be looked at. Nothing is tidied up on the way out, because on a real machine nobody was there
to tidy it.

### And `POWER` is a *different wire*

This is the part that makes `POWER` a different event rather than a bigger one. Switching the
machine on drives **`POC*`** — Power-On Clear, its own line on the backplane — and a board is
free to treat the two lines differently, because the real cards did.

The **88-VI/RTC** is the case that proves it. Its manual is explicit that POC disables every
function on the board, and the schematic runs POC — and *only* POC — to that logic. `RESET*` is
not wired to it at all. So an interrupt controller that a crashed program left armed and
enabled **stays armed through a `RESET`**, and comes back only when you `POWER` the machine.
That is not our shortcut; it is the card, and it is why a program that resets its way out of
trouble can still be taking interrupts it forgot it asked for.

`POC*` is also the only moment RAM is allowed to forget. On `POWER` the memory board refills
itself — with **random bytes by default**, because static RAM does not come up zeroed, and a
simulator that quietly zeroes it will never once catch the program that assumed otherwise — and
re-reads every ROM image from disk.

| | The processor | The boards | RAM |
|---|---|---|---|
| `RESET` | restarts at `0000` | `RESET*` on the bus; each board answers as its silicon did — some do nothing | **survives** |
| `POWER` | restarts at `0000` | `POC*` on the bus; the boards come up as they do from cold | **refilled**, ROMs re-read |
