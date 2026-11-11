# The monitor

This document is one of two short documents about how to use **`altairsim` itself**. `altairsim` is the
program that you run, and the machines that it simulates are a different subject. This document
is about **the monitor**. The monitor is the `altairsim>` prompt, where you start, stop, examine
and change the machine. The other document, *The Debugger*, tells you what to do in the monitor
when a guest does not work correctly.

Both documents are companions to the *User Manual*. The *User Manual* describes the hardware: the
boards, the machines, the disks and the tapes. `altairsim` simulates the MITS Altair 8800 and
its S-100 bus. If you do not know these, start with the first chapter of the *User Manual*.

The `altairsim>` prompt is **the monitor**. The monitor is the front panel of the machine, and
it is also the debugger. In `altairsim`, the front panel and the debugger are one thing. The monitor can do all that the front panel
of a real Altair could do, and much more. It sets breakpoints, single-steps and disassembles. It
also shows you the bus: which board decodes each address, which board asserts each
interrupt line, and where two boards decode the same address. *The Debugger* describes these
debug tools. The debug tools are the main reason that `altairsim` exists.

The monitor operates the machine directly. It is not a *menu* that stands between you and the
machine and lets you examine a fixed set of things. There is no debug mode to enter, and no
process watches the machine from outside. A breakpoint is **the machine stopping**. It is not a
script that sees afterward that the machine should have stopped.

`IN` and `OUT` do **real bus cycles**, with all the side effects of a real cycle. If you read a
UART's data port in the monitor, you take the byte from the UART, as the guest would. The front panel
and the debugger are one object because on an Altair they were one object. The operator set the
switches and read the lamps.

The monitor works while the machine is stopped. Most of the tasks that follow work with the
power on and the processor stopped. Examples are examining memory, doing a bus cycle and adding
a board. The front panel was also made for a machine with the power on and the processor stopped.

## Commands resolve by prefix

The monitor finds a command from the first letters that you type. It has **no aliases and no
abbreviations to learn**. Type enough letters to make the command unambiguous. The first command
that matches is the one that runs.

`HELP` prints the full list of commands, with the shortest form of each command outside the
brackets:

```
  BO[ARDS]          B[REAK]           COM[PARE]         C[ONFIG]
  CONN[ECT]         CONS[OLE]         DE[POSIT]         DI[SASM]
  DISC[ONNECT]      DO                D[UMP]            E[DIT]
  EX[AMINE]         F[ILL]            HE[LP]            H[ISTORY]
  I[N]              L[OAD]            MA[CHINE]         M[OUNT]
  MOV[E]            N[EXT]            NO[BREAK]         O[UT]
  P[OWER]           Q[UIT]            REGI[ON]          RE[GS]
  RES[ET]           REST[ORE]         R[UN]             SA[VE]
  SEA[RCH]          SE[T]             SH[OW]            SN[APSHOT]
  STA[RTUP]         S[TEP]            SY[MBOLS]         T[RACE]
  TY[PE]            U[NMOUNT]         W[HO]
```

Type the part before the bracket. `D` is `DUMP`, `DE` is `DEPOSIT` and `RES` is `RESET`. Upper
case and lower case are the same, in a command and in the name of a board.

`HELP <command>` shows the usage and examples for one command. `?` is the same as `HELP`.

Some commands are shortcuts for a longer command. The help of a shortcut says which command
it is short for. `CONSOLE` is a shortcut for `SET CONSOLE` and `SHOW CONSOLE`. Thus
`HELP CONSOLE` prints the full help of `SET CONSOLE`, which is also what `HELP SET CONSOLE`
prints.

> **`R` is `RUN`, not `RESET`. To reset, type `RES`.** You type `RUN` in every session, and an
> unwanted `RUN` does no damage. If `R` reset the machine, one wrong key could make you set up
> the machine again.

## Editing the command line

The monitor has a full line editor. The key with the label Backspace erases the character to the
left of the cursor. It does this for each byte that a terminal can send for that key. The arrow
keys move the cursor in the line. The line editor also keeps a **command history**. The up arrow goes
back through the lines that you typed, and the down arrow goes forward to the line that you were
typing.

The history is **saved between sessions, for each directory**. When you quit, the monitor
writes your last commands to a hidden file, `.altairsim_history`, in the directory where you
started `altairsim`. The next time you start `altairsim` in *that* directory, the up arrow
shows those commands. Each project directory keeps its own list. The monitor writes the history file
only when you type at a real terminal. A script, a pipe or an automated run never makes a
history file.

The `history` setting on the console sets how many lines the history file keeps. The default is 50. Type
`SET CONSOLE history=200` to keep more lines. Type `SET CONSOLE history=0` to stop saving the
history file.

### Completing with `Tab`

`Tab` completes the word that you are typing. It gets the possible words from the current
machine. When you add a board, `Tab` can complete the board's id at once. There is no list to keep up to date. `Tab` completes these words:

- at the start of a line, a **command**: `SH`⇥ → `SHOW`
- after `SET`, a **board id**, or `CONSOLE` or `DISPLAY`: `SET me`⇥ → `SET mem0`
- after a board id, one of **the board's property names**, with the `=` added:
  `SET mem0 fi`⇥ → `SET mem0 fill=`
- after the `=`, one of **the property's legal values**: for `SET mem0 fill=`⇥ the choices
  are `zero` and `random`

When more than one word fits, `Tab` adds the letters that all of the words share, and then
stops. Press `Tab` again to see the list. When no word fits, `Tab` does nothing.

### Editing keys

| Key | Does |
|---|---|
| `←` `→` | move one character |
| `Ctrl-A` / `Home` | move to the start of the line |
| `Ctrl-E` / `End` | move to the end of the line |
| `Alt-B` / `Ctrl-←` | move back one word |
| `Alt-F` / `Ctrl-→` | move forward one word |
| `Backspace` | erase the character before the cursor |
| `Delete` | erase the character under the cursor |
| `Ctrl-W` | erase the word before the cursor |
| `Ctrl-K` | erase from the cursor to the end of the line |
| `Ctrl-U` | erase the full line |
| `↑` `↓` | move back and forward through the command history |
| `Tab` | complete the word at the cursor |
| `Ctrl-D` | on an empty line, quit, the same as `QUIT` |

> **In the monitor, `Ctrl-E` moves to the end of the line.** In the monitor you type to the
> *line editor*. When a **running** guest has the console, `Ctrl-E` is **STOP**. `Ctrl-E` stops the
> machine and starts the monitor, so you have the keyboard again (see below). The same key does two jobs in two places.

## Repeating the last command: `.`

Type `.` alone on a line to run your **last command again**. The monitor does not echo the
command, so you see only its output. The commands that you usually repeat continue from where
they stopped. A bare `DISASM` disassembles the next screen, a bare `DUMP` shows the next page,
and `STEP` steps again. To go forward through a routine, type `DI` one time and then `.` `.` `.`.
To single-step, type `S` and then `.`.

Each `.` repeats the original command, not the previous `.`. Ten presses of `.` run the original command ten times. If you type `.` before any other command, the monitor tells you
that there is nothing to repeat.

## Reaching the host: `!`

A line that starts with `!` goes to **your host shell**. The monitor sends all the text after
the `!` to the shell with no changes, spaces included. The monitor waits until the command ends,
and then it shows the `altairsim>` prompt again:

```
!ls                 list the directory you started from
!vi HELLO.PRN       open a file in your editor, then :q to return to the monitor
!cp game.dsk save.dsk   copy a disk and keep it mounted
```

`!` runs **your** shell, with your own permissions. The shell is not part of the machine,
and the guest cannot see it or use it. An editor works because the monitor releases the keyboard
before it starts the command. `vi` gets a normal terminal, and it gives the terminal back when it exits. The machine stays stopped while the command runs, as it always is while the monitor runs.

A bare `!` with no command shows how to use `!`.

Under `--mcp`, the monitor refuses `!` and runs nothing. Standard output carries the messages
between the server and its client, and the output of a shell command would break them.

## Numbers: one fixed rule

> **On the wire → hex. Never on the wire → decimal.**

If the processor can see a number, the number is **hex**. Addresses, ports, data bytes and registers
are hex. If the processor never sees a number, the number is **decimal**. Counts, widths, sizes and
drive numbers are decimal.

**Hex is only the *default* for the wire class.** If you set the console to octal, the monitor
reads and prints the wire class in octal. The MITS manuals and the front panel used octal. The
rule stays the same. Octal replaces hex for the wire class, and counts stay decimal. *Reading
and writing in octal*, below, tells you how to use octal.

```
DUMP 100            address  -> 0100 hex
STEP 10             a count  -> ten instructions
OUT FF 55           port and byte -> both hex
SET sio0:a baud=9600      a baud rate -> nine thousand six hundred
```

A prefix or a suffix sets the base of one number:

| Write | Base |
|---|---|
| `0x`, `$`, or a trailing `h` | hex |
| `0o`, or a trailing `q` | octal |
| `%` | binary. Use it for the front panel's sense switches: one digit for each switch. |
| `0b` | binary, but only where the number is decimal or octal. Where the number is hex, `B` is a digit: `0B10` is the address 0B10. |
| a leading `#` | decimal |
| a `K` or `M` suffix | **always** decimal. `48K` is 49,152. |

The monitor rejects a number with two bases, such as `0x10K`. It does not guess.

The same rule applies in the monitor, in a machine file and in the settings of every board. You
learn only one rule.

The **classes** are fixed. Each kind of number is always in the same class. You can change the
base in which the monitor *prints* the wire class, and the next section tells you how to change it.

### Reading and writing in octal

The MITS manuals and the Altair front panel used **octal**, not hex. You can use octal too:

```
SET CONSOLE base=octal
```

After this command, the wire class is **octal**. The monitor reads and prints addresses, ports, data
bytes and registers in **split octal**. Each byte is a group from `000` to `377`, and a 16-bit
address is two groups. The front-panel address lamps are grouped the same way:

```
EXAMINE 100         -> 000 100  076   (the byte 0x3E at address 0x40)
DUMP 100-100        -> 000 100  076
DISASM 0            -> JMP 022 064     (a jump to 0x1234)
```

A bare number is octal now too, so `100` is address `0x40`. The decimal class, such as counts,
widths and baud rates, does not change. The prefixes and suffixes still work in both modes, so
you can always type a number in the base that you want. `0x1234` is hex in octal mode, and
`0o377` is octal in hex mode. `base=hex`, the default, sets hex again. To start in octal every
time, set `[console] base = octal` in a machine file.

## Naming a board: `<id>[:<unit>]`

Every board in the machine has an **id**. The id is a name that you give the board when you add
it, in a machine file or with `BOARDS ADD`. An id can be any name, such as `sio0` or `serial`.
Upper case and lower case are the same.

The machines that come with `altairsim` use a short type name and a number, such as `cpu0`,
`sio0` and `dsk0`. The number has no special meaning. It only tells two boards of the same kind
apart, such as `sio0` and `sio1`.

Some boards also have **units**. A unit is a part of a board that has its own name. For example,
a 2SIO serial board has two channels, `a` and `b`. A floppy controller has four drives, `drive0`
to `drive3`. A memory board has a ROM socket, `rom0`. To name a unit, type the board id, a colon
and the unit name:

```
SHOW sio0                  the board
SET  sio0:a baud=1200      channel a of the board
MOUNT dsk0:drive1 my.dsk   drive1 of the board
```

### Shorter names

When you type in the monitor, you can shorten the id, and you can omit the unit.

**You can omit the digits at the end of an id.** `sio` finds `sio0`, and `dsk` finds `dsk0`. A shorter id
works only when one board has that id with digits after it. If the machine has `sio0` and `sio1`,
`sio` is ambiguous. The monitor then lists `sio0` and `sio1` and stops. You can omit only the
digits at the end, so `si` does not find `sio0`.

**You can omit the unit when the board has only one unit that the command can use.** For `MOUNT`,
the monitor counts only the units that take a medium, such as a drive or a tape. For `CONNECT`,
it counts only the serial units. For example, `MOUNT acr tape.bin` puts a cassette in the one tape unit of
`acr0`. A floppy controller has four drives, so `MOUNT dsk0 my.dsk` does not work. The monitor
lists the four drives and stops.

To change a property of a unit, `SET` always needs the unit name. Without a colon, `SET` changes
a property of the board.

Shorter names work only in monitor commands. A machine file, and an MCP tool that takes a board
id, must use the full id and the full unit name.

## Seeing the machine

```
altairsim> BOARDS
  ID    TYPE        I/O       UNITS                            MEMORY
  ----  ----------  --------  -------------------------------  --------------------------------
  fp0   fp          FF        -                                -
  cpu0  8080        -         1 cpu: 8080                      -
  sio0  2sio        10,12     2 serial: a*, b                  -
  dsk0  dcdd        08,09,0A  4 disk: drive0(empty),           -
                                drive1(empty), drive2(empty),
                                drive3(empty)
  hb0   hostbridge  B0,B1     -                                -
  mem0  memory      -         1 rom: rom0                      0000-DFFF  ram  56K
                                                               FF00-FFFF  rom  dbl  phantom:all

  * holds the console
```

The `BOARDS` output is the backplane. It shows each board in the machine, the ports and memory that the board
decodes, and what is in its units. A UNITS or MEMORY entry longer than 32 characters continues
on the lines below it. A line breaks only after a comma or between two fields.

| Command | Shows |
|---|---|
| `BOARDS` | the backplane |
| `SHOW <id>` | one board: each setting, its value, and the values that it accepts |
| `SHOW MACHINE` | the whole machine |
| `SHOW CONSOLE` | which unit has your keyboard, and which transforms apply to its bytes |
| `SHOW DISPLAY` | the windows altairsim opens: whether a window comes to the front when it opens, where its keys go, and whether the CRT look is on |
| `SHOW JOYSTICKS` | the host game controllers that a board can read, such as a D+7A (needs an SDL3 build) |
| `SHOW SERIAL` | the serial ports on your computer, and the unit that holds each port |
| `SHOW BUS` | the whole backplane at one time: `MEMORY`, `I/O` and a short `INTERRUPTS` summary |
| `SHOW BUS MAP` | which board decodes each address, and which addresses float |
| `SHOW BUS IO` | which board decodes each port |
| `SHOW BUS IRQ` | which board is strapped to each interrupt line, and which board asserts it |
| `SHOW BUS CONTENTION` | where two boards decode the same address or port |

Look closely at `SHOW <id>`, because it is the **only** command that you need to configure a
board. It lists every property, its value and its legal values. The property names **are** the
keys that you write in a machine file. `altairsim` has no second schema. The board reference
in the *User Manual* is printed from the same tables. (The reference at the end of this document
is for the monitor's commands.)

## Changing the machine

```
SET cpu0 clock_hz=2000000      run at the real 2 MHz clock
SET mem0 fill=zero             RAM starts as zeros, not random bytes
SET fp0  sense=80              set the SENSE switches
BOARDS ADD 2sio sio1 port=20   add a second serial board
BOARDS REMOVE sio1             remove it
CONFIG SAVE mine.toml          save the machine as it is now
```

`altairsim mine.toml` starts the same machine that `CONFIG SAVE` saved.

## Running, and stopping

```
RUN FF00     set the PC and start: the same two steps as on the front panel's switches
RUN          continue from where the processor stopped
```

**`RUN <addr>` is EXAMINE followed by RUN**, as on the front panel. `altairsim` has no `BOOT`
command, and this is deliberate. A machine starts only when a `RUN` tells it to start, as a real
Altair started only when the operator pressed RUN.

If a board holds the console, **the guest gets the keyboard**. The guest gets every key,
including `Ctrl-C`, because a CP/M program can read `Ctrl-C`.

### `Ctrl-E` stops the machine and starts the monitor

**`Ctrl-E`** is the front-panel STOP switch. `altairsim` reads `Ctrl-E` *before the guest can see
the byte*. The guest never gets `Ctrl-E`, and it cannot disable `Ctrl-E`. `Ctrl-E` always stops the
machine.

If the guest needs `Ctrl-E`, move STOP to a different control key. For example, type
`SET CONSOLE stop=1D` to use `Ctrl-]`. The value is the key's control code, from `01`
(`Ctrl-A`) to `1F` (`Ctrl-_`). To use the new key every time, set `[console] stop = 0x1D` in a
machine file.

**`Ctrl-E` stops the machine and starts the monitor.** The guest no longer has the keyboard, and you
type to the monitor. No instruction executes while the monitor runs.

```
A>
STOP -- the machine is still at CA9C. RUN resumes.
altairsim>
```

STOP does not *change* the machine, because STOP is not RESET and not POWER.
The registers, the memory and the disks stay as the guest left them. A bare `RUN`, with no
address, continues at the instruction that the processor was about to execute. The message
*"still at CA9C"* tells you this.

### How a run ends

A `RUN` ends when it reaches a **breakpoint**, or a `HLT` that no interrupt can end. It always
tells you which. If no board holds the console, the guest does not get the keyboard. To stop
that run, press `Ctrl-C`.

After a `HLT`, the processor stays halted until `RESET`. On a halted processor, `RUN`,
`EXAMINE` and `DEPOSIT` do nothing, as the front-panel switches do. The monitor tells you:
*"processor halted at PC=0101 -- RESET ends the halt."* *The Debugger* has the details.

**`RUN` and `Ctrl-E` are the front panel's RUN and STOP switches.** `RUN` starts the processor. STOP, a
breakpoint or a `HLT` stops the processor and starts the monitor. **The monitor is
available only while the machine is stopped.** While the machine runs, the guest has the keyboard
and there is no `altairsim>` prompt. Every `SET`, `DEPOSIT` and `EXAMINE` acts on a *stopped* machine. A property never changes during an instruction. No property is locked while the machine
runs, and no property can be set only while it runs.

## Speed

**By default, the machine runs as fast as the host can run it.** `clock_hz` on the CPU board is
`0`. For example, a cassette that took 110 seconds on a real Altair loads in about one second.

Type `SET cpu0 clock_hz=2000000` to run at the speed of a real 2 MHz Altair. The guest sees the
same result at either speed, because the cassette uses the same number of T-states. The real clock
speed changes how the machine *feels*, not how it *behaves*. `SHOW cpu0` shows `achieved_hz`
next to `clock_hz`. `achieved_hz` is the clock speed that the run loop reached. You can read it,
but you cannot set it.

A crystal is always kept: at the prompt, from a script, from a pipe and under `--mcp`. A script
that must be fast leaves `clock_hz` at 0, or starts with `SET cpu0 clock_hz=0`. `SHOW CLOCK`
tells you which: its `pacing` line reads `paced` or `free`.

The speed matters to a guest that measures time against the *outside* world. An XMODEM transfer
needs the real clock speed, and a cassette does not. For more information, see `clock_hz` and
`idle` in the Boards chapter of the *User Manual*, and see its Troubleshooting chapter.

## RESET is not POWER

| | |
|---|---|
| `RESET` | the bus's RESET* line. The processor restarts at `0000`. **Memory stays the same**, and the disks stay mounted. |
| `POWER` | a power cycle. **This is the only command that loses the contents of RAM.** It also reads the ROM images again. |

`RESET` does not clear memory, because RESET on a real Altair did not clear memory. Much period
software needs this behavior.

`RESET*` is a **line on the backplane**. It is not an instruction that `altairsim` does for
you. Every board receives it, and each board does what its real hardware did. The result is different for each board. The memory board clears its bank latch, but it does not change RAM, because a
RAM chip has no reset pin. The floppy controller flushes the sector that it was writing and
deselects the drive.

The 2SIO does **nothing**, because the 6850 has no reset pin for `RESET*`. Its baud rate, word
format and interrupt enables stay the same after a reset, as on real hardware. If you type
`RESET` during a disk write, you get the result that the hardware gave. You get a half-written
sector and a serial port that is still set as the failed guest left it. All of RAM stays the
same.

**`POWER` uses a different line.** When the machine powers on, it drives `POC*` (Power-On
Clear), which is a separate backplane line. A board can respond to `POC*` and `RESET*` in
different ways, because the real cards did. The 88-VI/RTC is an example. `POC*` disables the
board, and `RESET*` is not connected to it. If a crashed guest left the interrupt controller armed, the controller **stays armed after a `RESET`**. Only `POWER` clears it.

`POC*` is also the only time when RAM loses its contents. On `POWER`, the memory board fills RAM
again, with **random bytes by default**, because static RAM does not start with zeros. It also
reads every ROM image again.

| | The processor | The boards | RAM |
|---|---|---|---|
| `RESET` | restarts at `0000` | `RESET*` on the bus. Each board does what its hardware did, and some do nothing. | **stays the same** |
| `POWER` | restarts at `0000` | `POC*` on the bus. Each board starts as it did at power-on. | **filled again**, ROMs read again |
