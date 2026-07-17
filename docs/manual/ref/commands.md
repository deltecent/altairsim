<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# Every monitor command

**Commands resolve by prefix, and the first match wins.** There are no aliases and
no fixed abbreviations: the shortest prefix that reaches a command is derived from
the table's order, so it is shown here as `D[UMP]` — type the part before the
bracket. (`?` is the one true alias, for `HELP`.)

**Numbers:** on the wire is **hex** (addresses, ports, bytes); never on the wire is
**decimal** (counts, widths, sizes). `0x`/`$`/`h` force hex, `#` forces decimal, and
a `K`/`M` suffix is always decimal.

## Not built yet

These **resolve** — they take their abbreviation today, so it cannot change
under your fingers when they land — and they tell you what they are waiting on.

| Command | Waiting on |
|---|---|
| `E[DIT]` | the line editor |
| `STO[P]` | a monitor that runs alongside the machine (ATTN leaves CONSOLE today) |
| `SN[APSHOT]` | the debugger |
| `REST[ORE]` | the debugger |
| `REC[ORD]` | the debugger |
| `REP[LAY]` | the debugger |

## The commands


### DUMP — `D[UMP]`

```
DUMP [<addr>|<range>] [WIDTH=16]
```
Hex and ASCII. A bare address runs to the END OF ITS PAGE, and a bare DUMP
continues from there -- so the rows and the columns both stay page-aligned
however you first landed. WIDTH is a count, so it is decimal.

```
D 100        0100-01FF, a whole page
D 0001       0001-00FF: stops on the boundary, last line full
D            the next page
D FF00-FF0F  an explicit range means exactly what it says
D 100/20     0100-011F (LEN is part of the address expression: hex)
D 0 WIDTH=8  eight bytes per line
```


### STEP — `S[TEP]`

```
STEP [n]
```
One instruction, with REAL bus cycles through the real decode. Prints each
instruction as it goes; past 32 it runs quietly and reports. `n` is a count,
so it is decimal.

```
S            one instruction
S 10         ten of them
```


### NEXT — `N[EXT]`

```
NEXT
```
STEP that does not descend. A CALL or RST runs to completion and stops at the
return address instead of stepping into it; anything else is a plain single
step. It is a temporary breakpoint at the return plus a RUN, so the callee is
LIVE -- it can use the console, and ^E (ATTN) or ^C stops it.

```
N            over the CALL/RST at PC (else single-step)
```


### RUN — `R[UN]`

```
RUN [addr]
```
Start the machine. `RUN <addr>` is EXAMINE + RUN -- it loads the PC first,
exactly as you would on the panel.

If a unit holds the console, the GUEST GETS THE KEYBOARD -- every key,
including ^C, which a CP/M program is entitled to read. The way back is ATTN
(^E), which the host takes before the guest is ever offered the byte, so the
guest cannot disable it. ATTN STOPS the machine -- nothing executes while this
prompt is up -- but it does not DISTURB it: ATTN is not RESET and not POWER, so
every register, every byte and every disk survives, and a bare RUN resumes at the
exact instruction. Stopped is not lost, and the debugger is at its most useful
here: REGS, EXAMINE, DUMP, DISASM and STEP all work at this prompt.

IT RUNS FLAT OUT unless the CPU card has a crystal. `clock_hz` defaults to 0,
so a cassette that took a real Altair 110 seconds comes off in about one. `SET
cpu0 clock_hz=2000000` buys back the 2 MHz machine AND its 110 seconds. What
the guest sees is identical either way -- the tape still costs the same
T-states -- so the crystal buys period FEEL, not period behaviour.

With no console connected there is no keyboard to hand over, and nothing to
pace against: it simply runs, ^C stops it. Either way it stops on a breakpoint
or on a HLT nothing can wake, and it ALWAYS says which.

```
RUN F800     boot the monitor PROM
RUN          carry on from wherever the PC is
```


### HISTORY — `H[ISTORY]`

```
HISTORY [n]
```
The last n BUS CYCLES the machine ran, oldest first -- a flight recorder that
is always running while the machine runs, so it already holds the run-up to a
breakpoint or a crash when you ask. n is a count, so it is decimal; bare
HISTORY shows the last 16. Each line is a cycle, not an instruction, and a DMA
transfer's cycles are in there too.

```
HISTORY          the last 16 cycles
HISTORY 100      the last hundred
```


### MOUNT — `M[OUNT]`

```
MOUNT <id>[:<u>] <file> [WP]
```
Put a disk in a drive, a tape in a recorder, or an image in a ROM socket.
WP is the write-protect tab: the guest may read it and may not write it.
RO is accepted and means the same -- it is the word for a ROM, which has no
tab to move.

A NAME IS CASE-BLIND, and you may leave off what carries no information: the
trailing index when only one such card is in the machine, and the unit when the
card has only one you could mount into. Anything genuinely plural you must say,
and it will tell you so.

```
MOUNT dsk0:drive0 disks/cpm.dsk
MOUNT dsk0:drive1 disks/master.dsk WP
MOUNT mem0:rom0 roms/monitor.bin
MOUNT ACR tape.bin      the one cassette, its one tape: acr0:tape
```


### BREAK — `B[REAK]`

```
BREAK [<addr> [IF <expr>] | MEM R|W <addr> | IO R|W <port>] [TRACE ON|OFF]
```
Bare BREAK lists them. Only the first kind is about the CPU at all -- the
other two watch BUS CYCLES, so they catch a DMA transfer too, and they work
unchanged on any processor.

```
BREAK FF13       stop when PC gets there
BREAK 2C00-2CFF  ...anywhere in a range
BREAK MEM W 100  stop when anything WRITES 0100
BREAK IO R 10    stop on an IN from port 10
```


A plain address breakpoint may carry a CONDITION -- IF <expr> over the
registers -- and stops only when it holds. A bare word that names a register IS
that register, so a literal is written with a leading zero (0A is ten, A is the
accumulator). == != < > <= >= compare; && || combine; & | mask.

```
BREAK 100 IF A==0
BREAK 100 IF HL==8000 && Z==1
BREAK 100 IF (A&0F)==0
```


TRACE ON|OFF makes it a TRACEPOINT: instead of stopping, it turns TRACE on or
off and the machine RUNS ON. Two of them trace a REGION and nothing else --
which is how you trace one subroutine out of a program that would otherwise
bury you. Unlike IF, this works on the MEM and IO kinds too, because it reads
no registers. TRACE ON at an address traces the instruction AT it; TRACE OFF
does not -- the region is [on, off).

```
BREAK 2C00 TRACE ON        start tracing when PC gets to 2C00
BREAK 2C40 TRACE OFF       ...and stop again at 2C40
BREAK MEM W 2000 TRACE ON  start when anything writes 2000 (that write is
                           the first line -- a trace shows its own reason)
BREAK 200 IF HL==8000 TRACE ON    conditional, and still does not stop
```

Where the trace GOES is TRACE's business, not the tracepoint's: set it up with
TRACE ON <file> MASK=..., then TRACE OFF to arm it without emitting. An
unconfigured tracepoint traces to the console.


### CONFIG — `C[ONFIG]`

```
CONFIG LOAD <f.toml> | CONFIG SAVE <f.toml>
```
SAVE writes the machine you are actually running, so it round-trips.

```
CONFIG SAVE machines/mine.toml
```


### SET — `SE[T]`

```
SET <id> <k>=<v>
```
SHOW <id> lists every property, its value, and whether it can be set while
the machine runs. A property's base is its own: a port is hex, a baud rate
is decimal.

```
SET mem0 fill=zero
SET mem0 phantom=read
```


### SHOW — `SH[OW]`

```
SHOW <id>|BUS [MAP|IO|IRQ|CONTENTION]|ROMS|MOUNTS|PATHS|CONSOLE|MACHINE
```

```
SHOW mem0        regions and properties
SHOW BUS MAP     who decodes what, and what floats
SHOW BUS IRQ     VI0-VI7: who is strapped where, who is pulling, who wins
SHOW MOUNTS      every disk, tape and ROM in the machine, and what is in it
SHOW PATHS       what a path resolves against -- and there is more than one answer
SHOW CONSOLE     which unit holds the keyboard, and its transforms
SHOW ROMS        the built-in images and their provenance
```


### DEPOSIT — `DE[POSIT]`

```
DEPOSIT <addr> <bytes...>
```
The front-panel switch. Runs a REAL bus write, so if no board decodes the
address the byte is simply gone -- and DEPOSIT says so rather than lying.

```
DE 100 C3 00 F8
```


### EXAMINE — `EX[AMINE]`

```
EXAMINE [<addr>]
```
One byte: hex, ASCII, and the bits as the panel's LEDs showed them. Bare
EXAMINE is the panel's EXAMINE NEXT -- it steps one byte. Its cursor is its
own; a DUMP does not move it.

```
EX 100       0100  C3  .  11000011
EX           and the next byte, and the next
```


### IN — `I[N]`

```
IN <port>
```
Runs a REAL IN cycle, with real side effects: an IN from a UART's data port
consumes the byte and the guest never sees it. To look without touching, use
WHO IO <port>. Reports whether anybody actually answered.

```
I 10         port 10 -> FF   (nobody answered -- the bus floated it)
```


### OUT — `O[UT]`

```
OUT <port> <byte>
```
Runs a REAL OUT cycle. Says so if no board decodes the port.

```
O 10 41
```


### LOAD — `L[OAD]`

```
LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [RAW <id>]
```
Format is autodetected. RAW <id> reaches BEHIND the bus into one board's
store -- that is the PROM burner, and it is why the operator can write a ROM
region while the guest cannot.

```
LOAD dbl.hex
LOAD monitor.bin AT F000 RAW mem0
```


### SAVE — `SA[VE]`

```
SAVE <file> <range> [FORMAT=BIN|HEX] [RAW <id>]
```

```
SAVE out.hex 0-FFF
```


### FILL — `F[ILL]`

```
FILL <range> <byte>
```

```
FILL 0-3FF 00
```


### SEARCH — `SEA[RCH]`

```
SEARCH <range> <bytes...>|"str"
```

```
SEA 0-FFFF C3
SEA 0-FFFF "CP/M"
```


### COMPARE — `COM[PARE]`

```
COMPARE <range> <addr>|<file>
```


### MOVE — `MOV[E]`

```
MOVE <range> <dest>
```


### WHO — `W[HO]`

```
WHO <addr> | WHO IO <port>
```
Who WOULD answer -- it looks without running a cycle, so nothing is consumed
and no card is poked. Reports contention, and reports PHANTOM*.

```
WHO FF00
WHO IO 10
```


### BOARDS — `BO[ARDS]`

```
BOARDS [LIST]|TYPES|ADD <type> <id> [k=v...]|REMOVE <id>
```
The backplane: what is in it, what each card answers to, and what is in its
sockets. A bare BOARDS lists them. RAM and ROM are named separately, and a
ROM range says which image is in it -- an empty socket decodes nothing, so it
is not in the memory column at all; it is in UNITS, marked (empty).

```
BOARDS                   the backplane
BOARD                    the same thing: a prefix of BOARDS
BOARDS TYPES             every card, and its properties
BOARDS ADD memory mem0
```


### REGS — `RE[GS]`

```
REGS | SET REG <r>=<v>
```
The flags are registers too, so SET REG CY=1 works. A register value is on
the wire, so it is HEX.

```
REGS
SET REG A=3F
SET REG PC=FF00
```


### REGION — `REGI[ON]`

```
REGION ADD <id> type=ram|rom at=<addr> [size=|mount=]
```
A region is a POPULATED part of a card. What is not covered by one is an
empty socket: it decodes nothing and floats to FF. `at` is an address, so it
is hex; `size` is a size, so it is decimal, and K/M work.

```
REGI ADD mem0 type=ram at=0 size=48K
REGI ADD mem0 type=rom at=FF00 mount=builtin:dbl
```


### DISASM — `DI[SASM]`

```
DISASM [<addr>|<range>] [n] [CPU=8080]
```
It needs an INSTRUCTION SET, not a CPU -- so it works on an empty backplane.
You normally never type CPU=: the active core says what it speaks, and DISASM
asks it. It PEEKS, so it cannot consume a byte from a UART in the range.

```
DI FF00      sixteen instructions of the boot PROM
DI           carry on from there
DI 0-2F      exactly that range
DI FF00 CPU=8080   when there is no CPU in the machine to ask
```


### UNMOUNT — `U[NMOUNT]`

```
UNMOUNT <id>:<u>
```
The socket is then EMPTY -- those pages float to FF, exactly as a card with
no chip in it does.

```
U dsk0:drive0
```


### DISCONNECT — `DISC[ONNECT]`

```
DISCONNECT <id>:<u>
```
The line then goes nowhere. NOT an error: an unconnected 6850 sits there with
TDRE set forever, and a program that writes to it works fine and talks to
nobody -- which is exactly what the card does with no cable in it.

```
DISC sio0:b
```


### CONSOLE — `CONS[OLE]`

```
CONSOLE [<k>=<v>...]
```
The host's terminal: what it is, who holds it, and how you get back from it.
Bare CONSOLE shows it; CONSOLE k=v sets it. (SHOW CONSOLE and SET CONSOLE are
the same thing said the long way.)

ATTN is the key that takes the keyboard BACK from a running guest. The host
intercepts it before the guest is ever offered the byte, so the guest cannot
disable it -- and that is why it must not be a key the guest needs.

```
CONSOLE            what it is set to, and which unit holds it
CONSOLE attn=1D    make it ^]  (hex: it is a byte on the wire)
```

To choose WHICH unit the console is wired to, that is CONNECT.


### CONNECT — `CONN[ECT]`

```
CONNECT <id>:<u> <endpoint>
```
Endpoints: {endpoints}

socket:PORT LISTENS -- that is the telnet-in case. socket:HOST:PORT CALLS OUT.
serial:DEVICE is a real port on this host; it is opened at 9600 8N1 and then
immediately re-programmed by the card, which is the only thing that knows what
it is strapped to.

Exactly ONE unit may hold the console; connecting a second STEALS it and says
who from. Two boards reading one keyboard would each get half the characters.

```
CONN sio0:a console
CONN sio0:b null
```


### RESET — `RES[ET]`

```
RESET [CPU]
```
A reset does NOT clear memory. Only removing power does that -- see POWER.
RESET CPU is a debugging convenience, NOT a real signal: no wire on the
backplane resets the processor and nothing else.


### POWER — `P[OWER]`

```
POWER
```
Power cycle. THE ONLY THING THAT LOSES RAM -- a RESET does not, because on
real hardware it does not.


### TRACE — `T[RACE]`

```
TRACE ON|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]
```
Log every BUS CYCLE while the machine runs -- to the console, or to a file.
A cycle, not an instruction: MR/MW are memory, IN/OUT are I/O, INTA is an
interrupt acknowledge, and a granted DMA master's cycles are tagged [DMA].
This watches the same stream every board sees, so it is not a CPU feature and
works unchanged on any processor.

MASK keeps only the cycles you name (no MASK keeps all): IN, OUT, IRQ, DMA,
CONTENTION. A cycle is kept if it is any of them -- MASK=DMA is every cycle a
master drove, whatever its type.

```
TRACE ON                    every cycle, to the console
TRACE ON run.log            ...to a file
TRACE ON MASK=IN,OUT        just the port traffic
TRACE OFF
```


TRACE OFF stops the tracing but REMEMBERS where it was going -- a later TRACE
ON, or a tracepoint, resumes to the same file and mask. That is what lets you
aim a tracepoint at a file: TRACE ON run.log MASK=DMA, then TRACE OFF to arm
it without emitting, then BREAK <addr> TRACE ON. See BREAK.


### NOBREAK — `NO[BREAK]`

```
NOBREAK [id]
```
Bare NOBREAK clears them all. An id is not on the wire, so it is decimal.

```
NOBREAK 2
NOBREAK
```


### HELP — `HE[LP]`

```
HELP [<command>]
```
Bare HELP lists the commands and nothing else -- the whole set on a few
lines, which is what you want when you are hunting for the name. HELP with a
command gives the usage and the examples.

```
HELP         the list
HELP DUMP    the detail
?            the same as HELP
```


### QUIT — `Q[UIT]`

```
QUIT
```

