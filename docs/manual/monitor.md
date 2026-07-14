# The monitor

The `altairsim>` prompt is **the monitor**. It is not a shell and it is not a debugger menu —
it is you, standing in front of the machine with your hands on the switches. Everything the
front panel of a real Altair could do, the monitor can do, and it can do a good deal the
front panel could not.

The machine does not have to be running for the monitor to work. Most of what follows —
examining memory, running a bus cycle, fitting a card — works on a machine with the power on
and the processor idle, which is exactly the arrangement the front panel was for.

## Commands resolve by prefix

There are **no aliases and no memorised abbreviations**. You type as much of a command as it
takes to be unambiguous, and the first command that matches wins.

`HELP` prints the whole menu with each command's shortest form in brackets:

```
  BO[ARDS]          B[REAK]           COM[PARE]         C[ONFIG]
  CONN[ECT]         CONS[OLE]         DE[POSIT]         DI[SASM]
  DISC[ONNECT]      D[UMP]            E[DIT]*           EX[AMINE]
  F[ILL]            HE[LP]            H[ISTORY]*        I[N]
  L[OAD]            M[OUNT]           MOV[E]            N[OBREAK]
  O[UT]             P[OWER]           Q[UIT]            REC[ORD]*
  REGI[ON]          RE[GS]            REP[LAY]*         RES[ET]
  REST[ORE]*        R[UN]             SA[VE]            SEA[RCH]
  SE[T]             SH[OW]            SN[APSHOT]*       S[TEP]
  STO[P]*           T[RACE]*          U[NMOUNT]         W[HO]
```

Type the part before the bracket. `D` is `DUMP`; `DE` is `DEPOSIT`; `RES` is `RESET`. Case
does not matter, here or in the name of any card.

The `*` marks the eight commands that **resolve but are not built yet**. Type `T` and it will
tell you that `TRACE` is waiting on the debugger. That is on purpose: their abbreviations are
claimed *now*, so the day they land, `T` does not quietly stop meaning what your fingers have
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

## Naming a card: `<id>[:<unit>]`

Every card in the machine has an **id** you chose (`cpu0`, `sio0`, `dsk0`), and some cards
have **units** inside them — the two channels of a serial card, the four drives on a floppy
controller, the ROM socket on a memory card.

```
SHOW sio0              the card
SET  sio0:a baud=1200  one channel of it
MOUNT dsk0:drive1 my.dsk
```

**You may leave out anything that carries no information.** If there is only one floppy
controller in the machine, `dsk` will find it. If a card has only one thing you could mount
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

That is the backplane: what is plugged in, what ports each card answers to, what is in its
units, and what it decodes in memory.

| Command | Shows |
|---|---|
| `BOARDS` | the backplane |
| `SHOW <id>` | one card: every setting, its value, and what it will accept |
| `SHOW MACHINE` | the whole machine |
| `SHOW CONSOLE` | which unit holds your keyboard, and how bytes are being transformed |
| `SHOW BUS MAP` | who decodes which addresses — and what floats |
| `SHOW BUS IO` | who decodes which ports |
| `SHOW BUS IRQ` | who is strapped to which interrupt line, and who is pulling it |
| `SHOW BUS CONTENTION` | where two cards are fighting |

`SHOW <id>` is worth dwelling on, because it is the **only** thing you need in order to
configure a card. It lists every property, what it is set to, and what values are legal —
and those property names **are** the keys you write in a machine file. There is no second
schema anywhere in this program. The board reference at the back of this manual is printed
from the same source.

## Changing the machine

```
SET cpu0 clock_hz=2000000      give it the real 2 MHz crystal
SET mem0 fill=zero             RAM comes up zeroed instead of random
SET fp0  sense=80              set the SENSE switches
BOARDS ADD 2sio sio1 port=20   fit a second serial card
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

If a card holds the console, **the guest gets the keyboard** — every key, including `^C`,
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

**It runs flat out by default.** `clock_hz` on the CPU card is `0`, which means "as fast as
this host can go", and a cassette that took a real Altair 110 seconds comes off in about one.

```
SET cpu0 clock_hz=2000000
```

…buys back the 2 MHz machine, and with it the 110 seconds. **What the guest sees is identical
either way** — the tape still costs the same number of T-states, the timing loops still count
the same. The crystal buys period *feel*, not period *behaviour*.

## RESET is not POWER

| | |
|---|---|
| `RESET` | the bus's RESET* line. The processor restarts at `0000`. **Memory survives**, disks stay mounted. |
| `POWER` | a power cycle. **This is the only thing that loses RAM** and re-reads the ROM images. |

`RESET` does not clear memory because pressing RESET on a real Altair did not clear memory.
That is not a simplification; it is the behaviour a lot of period software depends on.
