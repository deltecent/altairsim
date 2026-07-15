# Debugging

This is the chapter the simulator exists for.

Running old software is the easy half. The hard half is being able to see what a machine is
actually *doing* — which card answered, what went out on the bus, why the interrupt never
arrived — and that is what the commands in this chapter are for. Roughly fifteen of the
monitor's forty commands are here.

## Where the processor is — `REGS`

`REGS` prints the whole processor on one line. The flags come first — carry, zero, minus,
even parity, interdigit carry — then the register pairs, the stack pointer, the
interrupt-enable flip-flop, and the program counter. The last column is **the instruction the
processor is about to execute**, already disassembled.

You get this line free every time the machine stops, so most of the time you never type
`REGS` at all.

```
altairsim> REGS
C0Z1M0E1I0 A=00 B=007F D=CA01 H=BC0E S=BC37 IE=1 P=CA9C  CALL CA78
```

Flags are registers, and you may set them:

```
SET REG A=3F
SET REG CY=1
```

## Stepping — `STEP`

`STEP` runs **real bus cycles through the real instruction decode**. It is not an interpreter
running alongside the machine — it *is* the machine, moved forward by one instruction. It
prints each instruction as it goes; past thirty-two it runs quietly and tells you where it
ended up.

```
STEP        one instruction
STEP 20     twenty of them (a count, so it is decimal)
```

**`NEXT` steps *over* a subroutine.** At a `CALL` or `RST`, `STEP` walks you down into the
callee — every instruction it runs, and everything it in turn calls. Often you do not care: the
routine works, and you want the *next* instruction in the code you are reading, not a tour of a
print routine. `NEXT` gives you that. On a `CALL` or `RST` it runs the callee at full speed and
stops the instant it returns; on anything else it is just a single step. It does exactly what
you would do by hand — sets a breakpoint at the return address and runs to it — so the callee is
live while it runs: it can read the console, and `^E` (ATTN) or `^C` stops it if it never comes
back. A breakpoint that fires *inside* the callee stops you there, as it should.

```
NEXT        over the CALL/RST at PC, else one instruction
N           the same -- it owns the letter, because you type it constantly
```

## Breakpoints — `BREAK`, `NOBREAK`

There are two kinds, and only the first is about the processor at all.

**`BREAK MEM` and `BREAK IO` watch bus cycles, not instructions.** That is a much stronger
thing, and it is the reason to prefer them. A memory watch will catch a DMA transfer that no
instruction on the CPU ever performed, and it will work unchanged on any processor you put in
the machine, because it is watching the backplane rather than the program.

If you are chasing a byte that keeps getting clobbered, `BREAK MEM W <addr>` will find who is
doing it, whatever is doing it.

```
BREAK FF13            stop when the PC gets there
BREAK 2C00-2CFF       ...or anywhere in a range
BREAK MEM W 100       stop when ANYTHING writes to 0100
BREAK IO  R 10        stop on an IN from port 10
BREAK                 list them
NOBREAK 2             clear one (the id is a count, so decimal)
NOBREAK               clear them all
```

## Reading a block of memory — `DUMP`

`DUMP` is how you read a lot of memory at once. A bare `DUMP <addr>` runs to the **end of its
page**, and a bare `DUMP` carries on from there — so however you first landed, the rows stay
page-aligned and the columns never move under your eye.

It only *looks*. Nothing is consumed and no bus cycle is run.

```
DUMP 100          0100-01FF: a whole page
DUMP              the next page
DUMP FF00-FF0F    exactly that range
DUMP 100/20       0100-011F  (a length, and it is part of the address, so it is hex)
DUMP 0 WIDTH=8    eight bytes to a line (a count: decimal)
```

## One byte at a time — `EXAMINE`, `DEPOSIT`

These two are **the front panel's switches**, and they behave like them. `EXAMINE` shows a
single byte — hex, ASCII, and its bits — and a bare `EXAMINE` steps to the next one, which is
the panel's EXAMINE NEXT.

`DEPOSIT` runs a **real bus write**. If no card decodes that address, it says so rather than
pretending to have stored something — which is the difference between a debugger and a
notepad.

```
EXAMINE 2C00      one byte: hex, ASCII, and its bits
EXAMINE           the next one — the panel's EXAMINE NEXT
DEPOSIT 100 C3 00 2C
```

## Disassembling — `DISASM`

`DISASM` **peeks**: it reads memory without running a bus cycle. That matters, and it is not a
detail. A `read()` on a serial card *consumes* a byte from its receiver, and a disassembler
that ate the guest's input while you were looking at it would be a debugger you could not
trust. Nothing in this chapter that only *looks* at memory will disturb it.

```
DISASM FF00       sixteen instructions
DISASM            carry on
DISASM 0-2F       exactly that range
```

## Searching, filling, moving

The block operations. `COMPARE` will take a file as its second operand, which is how you check
what the machine loaded against what you meant to load.

```
SEARCH 0-FFFF C3 00 2C      find those bytes
SEARCH 0-FFFF "BDOS"        ...or that string
FILL 100-1FF 00
MOVE 100-1FF 2000
COMPARE 100-1FF 2000        ...or against a file
```

## Running real bus cycles by hand — `IN`, `OUT`

These are not simulated reads. **`IN` runs an input cycle on the bus, with every side effect a
real one would have** — it will consume a character from a UART's receiver, it will advance a
disk controller's sector counter. That is the point of them: it is how you poke a card the way
the guest's software would, without writing any guest software.

```
IN  10            run a real IN cycle on port 10
OUT FF 55         run a real OUT cycle
```

## Asking without touching — `WHO`

`WHO` asks who *would* answer. **No cycle is run and nothing is consumed.** It is the question
you want when `IN 10` gives you `FF` and you cannot tell whether that is data or whether
nothing is there at all.

It reports contention, and it reports `PHANTOM*` — so if two cards are fighting, or if one
card has switched another one off, `WHO` is where you find out.

```
altairsim> WHO IO FF
port FF OUT: nobody (an OUT here goes nowhere)

WHO 2C00          who decodes this address?
WHO IO 08         who decodes this port?
```

## FF is not data

**An `IN` from a port nothing decodes returns `FF`.** So does a read from an address no card
answers for.

That is not an error code and it is not a convention we invented — it is what a **floating
bus** reads. Nobody is driving the data lines, they idle high, and the processor faithfully
reads eight ones. A real Altair does exactly this.

It has a famous consequence, and it is worth knowing because you will meet it: on a machine
with no interrupt-vector card, a board pulls the interrupt line, nobody drives the data bus
during the acknowledge cycle, the processor reads `FF` — and `FF` is `RST 7`. That is not a
fallback anybody coded. It is what the hardware does, and it is why the interrupt vector on a
bare Altair is `RST 7`.

So when you see `FF`, ask `WHO`.

## Looking at the bus itself — `SHOW BUS`

Where `WHO` asks about one address, `SHOW BUS` shows you the whole backplane at once.

`SHOW BUS IRQ` is the only window onto the interrupt wiring, and interrupt wiring is the part
of a machine you cannot see. A card strapped to a line that nothing listens to fails in total
silence — the software just never gets its interrupt, and there is nothing to look at. This
command is what makes that visible.

`SHOW BUS CONTENTION` is the one to reach for when a machine you built yourself is misbehaving
for no reason. Two cards decoding the same port is a real hardware fault, and the simulator
will not quietly pick a winner for you.

```
SHOW BUS MAP          who decodes what in memory — and what floats
SHOW BUS IO           who decodes which ports
SHOW BUS IRQ          the eight interrupt lines: who is strapped where, who is pulling
SHOW BUS CONTENTION   where two cards answer the same thing
```

## A debugging session

The machine is not booting. Where does it get to?

```
altairsim> BREAK 2C00           the loader relocates itself to 2C00; does it get there?
altairsim> RUN FF00
breakpoint at 2C00
altairsim> REGS
altairsim> DISASM               what is it about to do?
altairsim> STEP 20              walk into it
```

The disk is not being read. Is the card even being asked?

```
altairsim> BREAK IO R 08        stop on any read of the controller's status port
altairsim> RUN FF00
```

Something is scribbling on the BIOS.

```
altairsim> BREAK MEM W E400
altairsim> RUN
```

## The tools that are not here yet

`TRACE`, `SNAPSHOT`, `RESTORE`, `RECORD`, `REPLAY` and `HISTORY` all **resolve** and all tell
you they are waiting on the debugger. There is no execution trace and no rewind. When you
stop the machine, you stop it where it is.

Say so plainly rather than let you find out: if you need to catch a bug that happens once in
ten million instructions and you cannot predict where, the tool for that is not built.
