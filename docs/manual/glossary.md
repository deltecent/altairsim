# Glossary

**ACIA** — Asynchronous Communications Interface Adapter. The Motorola 6850 chip at the heart
of the 88-2SIO serial card. It has two registers a program can see: a status register and a
data register. Everything a program does with a serial port on this machine, it does through
one of those.

**ACR** — Audio Cassette Recorder interface. The MITS 88-ACR: a card that wrote bits to an
ordinary audio cassette and read them back. It is how you loaded BASIC if you could not
afford a floppy, which in 1976 was most people.

**ATTN** — Attention. The key that stops a running guest and returns you to the monitor. It
stops the machine but does not disturb it — not RESET, not POWER — so a bare `RUN` resumes at
the instruction it was about to execute. It is `^E` by default, the host intercepts it before
the guest sees the byte, and no guest program can take it from you. Move it with
`CONSOLE attn=`.

**backplane** — The board with the connectors on it that every card plugs into. On an Altair
it is the S-100 bus itself: eighteen slots, one hundred pins each, all wired in parallel.
There is no chipset. The backplane *is* the machine.

**bank switching** — Making more memory than the processor can address by putting several
banks at the same addresses and switching between them. An 8080 can address 64K and no more.
A machine with 128K of RAM in it lies to the processor about which half it is looking at.

**BDOS** — Basic Disk Operating System. The middle layer of CP/M: files, directories, the
console. A CP/M program asks the BDOS for things and does not know what hardware answers.

**BIOS** — Basic Input/Output System. The bottom layer of CP/M, and the only part that knows
what machine it is on. Move CP/M to a new computer and the BIOS is what you rewrite; the BDOS
and the CCP are untouched. It is also where a track buffer lives *if the author put one there*
— which is why, on some CP/Ms and not others, a file can be "written" and not yet be on the
disk. See the disks chapter.

**CCP** — Console Command Processor. The top layer of CP/M — the part that prints `A>` and
runs what you type. It is deliberately expendable: a big program is allowed to overwrite it,
and CP/M reloads it from disk afterwards. That reload is the warm boot.

**contention** — Two cards answering the same address. On a real S-100 machine both would
drive the data bus at once and you would get a byte that is neither of theirs, intermittently,
in a way that would take you a week. `SHOW BUS CONTENTION` names them instead.

**CP/M** — Control Program for Microcomputers. Digital Research's disk operating system, and
the reason the 8080 mattered. Write a program for CP/M and it ran on every 8080 machine ever
built, whoever built it — the first time that was true of anything.

**DBL** — Disk Boot Loader. The boot PROM on the MITS floppy controller, at `FF00`. It reads
one sector off track 0 and jumps into it. **There is no `BOOT` command on an Altair** — you set
the address switches to `FF00`, press EXAMINE to load them into the program counter, then press
RUN, and this is the thing you are running.

**decode** — What a card does when it recognises an address as its own and answers. A card
that does not decode an address stays silent and lets somebody else have it. Which card
decodes which address is the entire question of how an S-100 machine is put together.

**DMA** — Direct Memory Access. A card taking the bus away from the processor and reading or
writing memory itself, without the processor's help. Faster, and the origin of some
spectacular bugs.

**endpoint** — In this program, the thing on the far end of a serial unit's cable: `console`,
`null`, `loopback`, a TCP socket, or a real serial port. The serial chapter has the complete
list; there are no others.

**floating bus** — What the data bus reads when nothing is driving it. On the S-100 it floats
high, so an `IN` from a port no card decodes returns `FF`. That is not an error. That is the
absence of a card, and it looks like `FF`.

**front panel** — The switches and lamps on the front of the Altair. The address switches, the
data lamps, DEPOSIT, EXAMINE, RUN, STOP, RESET. Before there was a terminal, this *was* the
user interface, and you toggled your bootstrap in through it one byte at a time. In this
program it is a card like any other.

**FSK** — Frequency Shift Keying. Encoding bits as two audible tones — one for a zero, one for
a one. It is how the ACR got data onto a cassette, and it is why a loading tape sounds the way
it does.

**hard-sector** — A floppy where the sector boundaries are marked by physical holes punched in
the disk, and the controller counts them going past. The MITS floppies are hard-sectored. A
soft-sectored disk has one hole and finds its sectors by reading marks written in the data,
which is the arrangement that won.

**IntAck** — Interrupt Acknowledge. The bus cycle the 8080 runs when it accepts an interrupt.
The interrupting card puts one instruction on the data bus during that cycle, and the
processor executes it. Almost always an `RST`.

**MCP** — Model Context Protocol. The protocol an AI assistant uses to call structured tools.
`altairsim --mcp` speaks it, so an assistant can drive the machine directly. See the MCP
chapter.

**monitor** — The `altairsim>` prompt. Not a program running inside the machine — it is you,
standing in front of it, with a much better front panel than MITS shipped.

**PHANTOM\*** — An S-100 line that tells memory cards to shut up. Pull it, and a ROM can
answer at an address a RAM card also decodes, without contention. It is how a boot PROM can
sit on top of RAM and then get out of the way. The asterisk means the line is active low, and
on this bus most of them are.

**pINT** — The S-100 interrupt request line. Any card may pull it. The processor notices, if
interrupts are enabled, and runs an IntAck cycle to find out who and why.

**PROM** — Programmable Read-Only Memory. A chip with a program burned into it that survives
power-off. The Altair's boot loader lives in one, which is the only reason a disk machine can
start at all: something has to already be there to read the disk.

**RST** — Restart. A one-byte 8080 instruction that calls a fixed low address — `RST 0` through
`RST 7`, at `0000`, `0008`, and so on to `0038`. One byte, so it fits in an IntAck cycle,
which is exactly what it is for.

**S-100** — The bus. One hundred pins, eighteen slots, designed by MITS for the Altair and
then adopted by everyone. It was the first open standard in personal computing, mostly by
accident, and it is the reason a card from one company worked in another company's machine.

**sector** — The smallest chunk of a disk you can read or write. On these floppies, 137 bytes
of which 128 are yours.

**SENSE switches** — The top eight address switches (A8–A15) on the front panel, readable by
a program as an input port. Software used them for configuration before there was anywhere
else to put it: which port is the console, how much memory to use, whether to load from tape.
Half the boot procedures in this manual begin with a sense switch setting.

**T-state** — One clock cycle. Every 8080 instruction costs a known number of them, and that
is how this program knows what time it is. At 2 MHz a T-state is 500 nanoseconds, and a
cassette takes 110 seconds because it takes 220 million of them.

**TDRE** — Transmit Data Register Empty. The bit in the 6850's status register that means
"the card has room for another character". A program that wants to print polls it until it
sets. A serial port connected to `null` sets it forever, which is why writing to nothing works
fine.

**track** — One concentric ring of sectors on a disk. The head steps in and out to reach a
track and does not move again to reach the sectors on it, which is why a BIOS that buffers
buffers a whole track at a time: having paid for the seek, it may as well have the lot.

**UART** — Universal Asynchronous Receiver/Transmitter. The chip that turns a byte into a
sequence of bits on a wire and back again. The ACIA is one.

**unit** — One channel on a card that moves characters — the socket on the back. A 2SIO has
two of them, `a` and `b`, and they are connected independently. `CONNECT sio0:b` names the
board and then the unit.

**VI0–VI7** — The eight vectored interrupt lines on the S-100 bus, served by the 88-VI/RTC
card. **VI0 is the highest priority.** A card pulling VI*n* gets `RST` *n*, and the interrupt
card sorts out who wins when two pull at once.

**warm boot** — CP/M reloading its own top layer (the CCP) from the disk, because the program
that just finished was allowed to overwrite it. `^C` at the `A>` prompt does one deliberately.
It is not a reset; the machine never stopped.
