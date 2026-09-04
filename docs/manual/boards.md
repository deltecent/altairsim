# Boards

An Altair is a **backplane**. Everything else is a board in it — the memory, the serial ports, the
disk controller, the front panel, and the processor itself. There is no "machine" underneath the
boards doing the real work; take the boards out and there is nothing left but a bus.

`altairsim` is built that way on purpose, and it is the reason the CPU's crystal is a property of
the CPU *board* and the sense switches are a property of the *front panel*. Not nitpicking: it is what
lets you pull a board out, put a different one in, and find out what the software does about it.

This chapter says what the boards **are** — what the real hardware was, what it is for, and
what will bite you. **It does not list their parameters.** Every key of every board is in the board
reference at the back of this manual, printed from the program's own tables, which is why it cannot
be wrong.

## The boards

Grouped by what they do — the same order as the sections below.

**Memory**

| Type | What it is |
|---|---|
| `memory` | RAM and ROM, as a list of regions |
| `bankmem` | bank-switched RAM — Vector Graphic, Cromemco 64KZ, North Star HRAM, ExpandoRAM II |
| `v2z80rom` | S100Computers V2 Z80 CPU board's onboard MASTER monitor ROM — a paged EEPROM, not a processor |

**Processors**

| Type | What it is |
|---|---|
| `8080` | the MITS 88-CPU |
| `8085` | an 8085 CPU board — the 8080's binary-compatible superset, with RIM/SIM and the TRAP/RST interrupts |
| `z80` | a Z80 CPU board — the same bus, a different instruction set |
| `6800` | a Motorola 6800 — the CPU of MITS's *other* machine, the Altair 680b. See the note below |

**Serial ports and consoles**

| Type | What it is |
|---|---|
| `2sio` | MITS 88-2SIO — two serial ports. The usual console |
| `sio` | MITS 88-SIO — one serial port. MITS's first |
| `sbc` | SD Systems SBC-100/200 — a Z80 single-board computer's serial console |
| `propio` | S100Computers Console I/O — a Propeller-based serial console |
| `pmmi` | PMMI MM-103 — a Bell 103 telephone modem on one card |

**Cassette**

| Type | What it is |
|---|---|
| `acr` | MITS 88-ACR — the cassette interface |
| `uio` | MITS 88-UIO — a serial port and a cassette, on one card |

**Printers**

| Type | What it is |
|---|---|
| `c700` | MITS 88-C700 — the line-printer controller. Capture to a file |
| `lpc` | MITS 88-LPC — the other line-printer controller, line-buffered |

**Parallel and analog I/O**

| Type | What it is |
|---|---|
| `pio` | MITS 88-PIO — an 8-bit parallel port, in and out |
| `4pio` | MITS 88-4PIO — up to four programmable parallel ports |
| `d7a` | Cromemco D+7A — analog and parallel I/O; reads joysticks |

**Floppy and disk controllers**

| Type | What it is |
|---|---|
| `dcdd` | MITS 88-DCDD — the 8″ floppy controller |
| `mds` | MITS 88-MDS — the 5¼″ minidisk controller |
| `hdsk` | MITS 88-HDSK — the Datakeeper hard-disk controller |
| `versafloppy` | SD Systems VersaFloppy I/II — a soft-sector floppy controller. Boots SDOS |
| `tarbell` | Tarbell #1011 — a single-density floppy controller with its own boot PROM. Boots CP/M by itself |
| `tarbelldd` | Tarbell #2022 — the double-density twin, mixed-density disks |
| `icom` | iCOM FD3712/FD3812 — an 8″ floppy controller with its own boot PROM. Boots CP/M and FDOS |
| `dualsd` | S100Computers Dual SD — two microSD cards as CP/M drives. Boots CP/M 3 |
| `dualide` | S100Computers IDE-AB — two CompactFlash cards as CP/M drives. Boots CP/M 3 |

**Video and display**

| Type | What it is |
|---|---|
| `vdm1` | Processor Technology VDM-1 — memory-mapped video. Needs a display |
| `dazzler` | Cromemco Dazzler — color graphics. Needs a display |
| `vdb8024` | SD Systems VDB-8024 — an 80×24 video terminal on one board. Needs a display |
| `sol` | Processor Technology Sol-PC — the Sol-20's onboard I/O, on one card |

**Interrupts and the clock**

| Type | What it is |
|---|---|
| `virtc` | MITS 88-VI/RTC — vectored interrupts and a clock |
| `ss1` | CompuPro System Support 1 — a multifunction board: real-time clock, serial channel, interval timer, and dual interrupt controllers |

**The whole machine**

| Type | What it is |
|---|---|
| `fp` | the front panel |
| `turnkey` | MITS 8800b Turnkey Module — the front-panel-less Altair, on one card |

**Host integration**

| Type | What it is |
|---|---|
| `hostbridge` | file transfer to your host. **Ours, not a period card** |

**PROM programmers**

| Type | What it is |
|---|---|
| `pb1` | SSM PB1 — burn a 2708/2716 EPROM, then save it as a hex file |

---

## Memory

RAM, ROM, and the boards that switch banks of it.

## `memory` — RAM and ROM

A memory board is **a list of regions**, and the regions are the board. That is not a modelling
convenience; it is what an S-100 memory board was. One physical card carried banks of chips
decoding whatever ranges its jumpers said, and a card with 56K of RAM low and a 256-byte boot PROM
at `FF00` is a perfectly ordinary card.

So `default` has exactly one memory board in it, and that board is the 56K *and* the PROM.

### `PHANTOM*` — how a boot PROM gets out of the way

The bus has a line called `PHANTOM*`. **A board pulls it to switch another board off.** When the
PROM at `FF00` is being read, it asserts `PHANTOM*`, and the RAM card underneath — if it is
jumpered to honour it — shuts up. Two boards decode `FF00`; only one answers.

This is how an Altair with disks boots. The PROM overlays the RAM at the top of memory, the loader runs
out of it, and then **the loader gets out of the way** and the RAM underneath is uncovered — which
matters, because CP/M wants that memory back.

Whether a board honours `PHANTOM*`, and whether it asserts it, are **jumpers**. They are on the
board and they are yours to set. Getting them wrong produces a machine that does not boot and does
not say why — which is precisely what it did in 1977, and the bus view in the monitor will show you
both boards claiming the page.

### Banking is its own board

Sixty-four kilobytes was not enough for very long, and the industry's answer was **bank switching**:
several cards' worth of RAM at the same addresses, with a write-only port that says which is live.
Nobody agreed on how — so banking is **not a knob on `memory`**. It is its own board, **`bankmem`**
(below), and each real card it models owns its own decode. A plain `memory` board is exactly that:
plain, unbanked RAM and ROM.

---

## `bankmem` — bank-switched RAM

When 64K stopped being enough, S-100 makers put several planes of RAM at the same addresses and a
write-only **select port** that chose which plane the CPU saw. Every maker did it differently, so
`bankmem` is **one board with four decoders**, chosen by `card`:

| `card` | Real board | Select port | What a write does |
|---|---|---|---|
| `vector` | Vector Graphic 64K | `40` | **one-hot** — `01`→bank 0, `02`→1, `04`→2 … `80`→7 |
| `cromemco64kz` | Cromemco 64KZ / 64KZ-II | `40` | **8-bit mask** — bit *N* turns bank *N* on; **several at once** (`28`→banks 3 and 5) |
| `northstar` | North Star HRAM | `C0` | bit 0 = on/off, bits 1–7 = which bank; banks toggle **one at a time** |
| `expandoram2` | SD Systems ExpandoRAM II | `FF` | the byte is a **page number** (an approximation — see below) |

`banks` sets how many planes the card carries (one per real board): up to 8 for `vector` and
`cromemco64kz`, 6 for `northstar`, 10 for `expandoram2`. `fill` and `seed` behave exactly as they do
on `memory`. The select port is write-only and the **guest** drives it; from the monitor you can
drive it yourself with `OUT`, and `SHOW` lists every plane and which is live.

There is no banked operating system in the box to boot, so this board is here to be *driven* — the
quickest way to see it work is from the monitor:

```
altairsim bankmem
OUT 40 01            ; select bank 0
DEPOSIT 1000 A0
OUT 40 08            ; select bank 3 (one-hot 0x08, not bank 8)
DEPOSIT 1000 B3
OUT 40 01            ; back to bank 0 — DUMP reads A0
DUMP 1000-1000
OUT 40 08            ; bank 3 — DUMP reads B3; the plane really swapped
DUMP 1000-1000
```

> **`expandoram2` is an approximation.** The real board decodes the page number through an on-board
> PROM into a 32K or 48K partition; that decode is not published in a form we can reproduce
> faithfully, so `bankmem` models a plain page-select over 64K planes and says so here. The other
> three cards are exact.

---

## `v2z80rom` — the S100Computers V2 Z80 CPU board's onboard monitor ROM

**This board is not a processor** — it is the **monitor ROM** that a real S100Computers V2 Z80 CPU
board carries on it: an **8K EEPROM** at `F000`–`FFFF` holding John Monahan's MASTER V6.6 ROM
monitor. It is a separate board from the CPU on purpose — a machine that uses it still needs a `z80`
(below) beside it for the processor, exactly as the real card is a Z80 with its own onboard firmware.

The EEPROM is **paged**: two 4K halves both live at `F000`–`FFFF`, and a write to port `D3` chooses
which is visible (and can switch the EEPROM off altogether, so the RAM underneath shows through).
That is how CP/M gets a flat 64K after boot — it inactivates the EEPROM and the monitor's window
becomes ordinary memory. While it is on, the EEPROM shadows the RAM in its window for reads.

There is no `BOOT` verb — **the monitor is the boot command**. `startup = ["RUN F000"]` cold-starts
it, and at its `->` prompt the **`I` command** boots CP/M 3 off a Dual SD card (below). This is the
board that makes `altairsim dualsd` go.

---

## The CPU boards — one plugs in, and it drives the bus

`altairsim` has three S-100 processor boards — `8080`, `8085`, and `z80` — and everything in this
shared section is true of all three. Each is described on its own below; what they have in common
is here.

**A fourth CPU, the Motorola `6800`, is not one of them.** It drives MITS's *other* machine, the
Altair 680b — a different bus, with memory-mapped I/O rather than S-100 ports, so little of this
section applies to it. The `6800` and the 680b's own I/O boards (`680io`, `680uio`, `680kcacr`) are
catalogued in the board reference at the back, and the `altair680` machine boots MON680 out of the
box.

**The processor is a board like any other.** It plugs into the backplane, it can be removed, and
with `-n` you can build a machine that does not have one. It decodes no ports and answers no
addresses. What it does is **drive the bus** — which makes it unlike every other board in the box,
and is exactly what a CPU card did. Put a `z80` where an `8080` would go and the bus, the boards,
and the debugger neither know nor care; that is the whole point of keeping the processor a board.

Every CPU board carries the same three properties: **`clock_hz`** (the crystal), **`idle`**
(stands the processor down at a prompt), and the read-only **`achieved_hz`** (the speed it
actually reached).

### The crystal is on the board — `clock_hz`

Because the crystal is soldered to the CPU card, `clock_hz` is the *board's* property, not the
machine's. **`clock_hz = 0` is the default, and it means run flat out** — on a modern host,
north of a hundred times a real Altair.

```
SET cpu0 clock_hz=2000000
```

buys back the real 2 MHz machine, and it is worth doing once. **What the guest sees is identical
either way**: instructions cost the same T-states and a cassette loads in the same number of
them, so the crystal buys period *feel*, not period *behaviour*.

That holds everywhere except at the edge of the machine. A guest counts instructions to measure
time, so flat out it retires a "three-second" timeout in milliseconds of yours — which is why
anything the guest times against the *outside* world (XMODEM through a serial port) wants the
real crystal. The troubleshooting chapter has the full story.

### `idle` — the CPU stands down at a prompt

At a prompt a guest is only spinning on the serial status register waiting for a keystroke, and
flat out that pins a core to accomplish nothing. **`idle` (on by default) stands the processor
down while the guest is polling an empty keyboard** — a pinned core becomes a few percent — and
**the guest cannot tell**, because the moment a byte arrives the processor is back before the
next poll. An XMODEM transfer through an idling machine is byte-exact.

---

## `8080` — the MITS 88-CPU

The original — the board the Altair shipped with, and the one the other two processors stand in
for. As a board it is the plain case of everything above: it drives the bus, decodes nothing, and
carries `clock_hz`, `idle`, and `achieved_hz`.

---

## `8085` — an 8085 CPU

**The 8080's own successor, and a binary *superset* of it** — every 8080 program runs on an 8085
unchanged, which makes it the closest of the three to the 88-CPU. Everything in *The CPU boards*
above applies unchanged; what follows is only what the 8085 adds.

Over the 8080 it adds `RIM` and `SIM` (read and set the interrupt mask and the SID/SOD serial
pins) and the on-chip interrupts — `TRAP` plus `RST 5.5`, `6.5` and `7.5`, layered on the
8080-style `INTR` line. The documented set is faithful (including the one instruction that
differs, `ANA`/`ANI` always setting the auxiliary carry), and the undocumented opcodes execute
too, with the extra V and K flag bits some of them set; `DISASM` marks each undocumented byte
the way `DDT` does. The built-in `8085` machine is a minimal one — an `8085`, 64K of RAM, and a
2SIO console.

What no board drives is the 8085's on-chip *pins*: the `SID`/`SOD` serial lines and the
`TRAP`/`RST 5.5`/`6.5`/`7.5` interrupt inputs. Nothing on the S-100 bus carries them, so no card
asserts them; ordinary interrupts over the `INTR` line work as they do for the 8080.

---

## `z80` — a Z80 CPU

**The other processor you can drop into the slot** — the same backplane, a different instruction
set behind it. Everything in *The CPU boards* above applies unchanged. The built-in `z80`
machine is a minimal one — a `z80`, 64K of RAM, and a 2SIO console — for putting it through its
paces.

---

## Serial ports and consoles

The boards that carry a serial port — a console, a modem, or both.

## `2sio` — MITS 88-2SIO

Two **6850 ACIAs**, units `a` and `b`, four ports at BASE+0 through BASE+3. Base defaults to `10`
hex, which is where every listing from the period expects it.

This is **the usual console board**, and it is what `default` has. If you are running CP/M or
Microsoft BASIC, this is the board the software is talking to.

### The two halves share nothing

Not the baud rate, not the endpoint, not the interrupt strap. **They are two independent chips that
happen to be bolted to the same board**, and the model says so: `a` and `b` are separate units with
separate properties.

So a console on `a` at 9600 and a modem on `b` at 1200, one interrupting and one polled, is not a
configuration you have to work around. It is Tuesday.

The serial chapter covers what a channel can be *connected* to: your terminal, a TCP socket, or a
real serial port on your host, with the modem control lines wired through.

---

## `sio` — MITS 88-SIO

One **COM2502 UART**, unit `tty`, two ports. This was **MITS's first serial card** — it predates
the 2SIO, and the earliest Altair software talks to it. `basic4k` uses it.

### Its status bits are inverted

**A clear bit means ready.** Read that twice, because every instinct you have says otherwise, and
because it will make you certain you have found a bug in the simulator.

You have not. **It is a fact about the chip**, not a quirk anyone invented: the COM2502's status
lines came out of the package active-low, MITS wired them to the data bus as they were, and every
program that drove an 88-SIO was written knowing it. `basic4k`'s I/O routine masks and branches on
zero, and it is right to.

The port must be **even**: control at BASE, data at BASE+1.

---

## `sbc` — SD Systems SBC-100/200

SD Systems built S-100 boards for people who wanted a whole computer on as few cards as possible,
and the **SBC-100** and **SBC-200** are the heart of one: a **Z80 single-board computer** —
processor, some memory, and a serial console — on a single card. `altairsim` models the console
half, which is the part the software talks to.

That console is an **Intel 8251 USART**, not the 6850 the MITS boards use — unit `tty`, with data
at `7C` and the status/command register at `7D`. Software written for a 2SIO will not drive it; the
SBC's own **SD monitor** will.

### It measures your terminal's speed

The board's one memorable trick is **auto-baud**. Run `altairsim sbc200` and the SD monitor is
waiting — not at a fixed rate, but for you to **press Return**. It times the bits of that one
character and sets its own baud to match, so a terminal at any common speed just works. Nothing
happens until that first Return, which surprises people: it is not hung, it is listening.

The `sbc200` machine boots the **SD monitor**. Give the machine the **DDBIOS** disk BIOS in a PROM
socket and a `versafloppy` controller beside it, and the monitor's `C` command boots **SDOS** — see
the VersaFloppy below, and the SD Systems example in `examples/`. `variant` picks the generation
(`sbc200` or `sbc100`).
The parallel ports, timer and interrupts of the real card are a later phase; the console is here now.

---

## `io4` — SSM IO-4, the strap-configurable serial board

Most serial boards in this list model **one specific chip** — the 2SIO's 6850, the SBC's 8251, the
88-SIO's COM2502 — so the software has to be written for that chip. The **SSM IO-4** is the other
kind: it has no fixed chip to imitate. You **describe** the port with straps — which port is
status/control, which is data, which status bit means a byte is waiting (`dav`), which means the
transmitter is ready (`tbmt`), and whether the shared **`inverter_gate`** flips their sense — and
that description *is* the port. It is the general-purpose serial board for reaching whatever polled
"read a status bit, then a data byte" interface a piece of software expects.

The real IO-4 carries **two independent serial channels**, and so does this board: units **`a`** and
**`b`**, each with its own straps, `baud` and `connect` endpoint, configured under its own
`[board.unit.a]` / `[board.unit.b]` table in a machine file. By default `a` answers ports `0`/`1`
and `b` answers `2`/`3`. (The real board's parallel ports are not modeled.)

You rarely set the straps by hand. A **profile** presets them to imitate a known card: **`sior0`**
(MITS SIO Rev 0 — the default, what the SSM 8080 monitor expects), `tuart` (Cromemco TU-ART),
`imsai-sio2`, `compupro-if2` (CompuPro Interfacer II), `compupro-ss1` (CompuPro System Support 1).
Pick a profile per channel, then override any individual strap afterward — the real board is
jumpered, and so is this one. The board is polled, with no interrupts.

---

## `propio` — S100Computers Console I/O

A **serial console** of the reproduction era: the S100Computers Console I/O board, built around a
Parallax Propeller instead of a 6850 or 8251, with status at `00` and data at `01`. It is a polled
console, unit `serial` — you `CONNECT` it to a terminal, a file, a socket or a serial port like any
other serial board. It is the console the Dual SD machine uses.

Underneath, `propio` is just a **preset**: it is the strap-configurable serial engine (the same
one behind the [`io4`](#io4) board) with this board's documented convention filled in — the ports,
and which status bit means receive-ready and which means transmit-ready. Because the real board is
jumpered, every one of those straps is still yours to override, so a differently-strapped Console
I/O board needs no new board type, just a property or two.

---

## `pmmi` — PMMI MM-103 modem

A **Bell 103 telephone modem on one S-100 card** — the first S-100 modem approved for direct
connection to the phone line. In a real machine it dialed, answered, and carried a serial link over
the line at up to 600 baud. Here it is the card's **transmit and receive path**: unit `line`, four
ports from a base that must sit on a four-port boundary (default `C0`; the DIP switch on the real
card set it, and PMMI's own North Star software used `E0`).

Its four ports are the card's quirk: **read and write at the same address are different registers.**
Writing `BASE+0` sets the character format (data bits, parity, stop bits) and the modem-control bits;
*reading* it gives you UART status. `BASE+1` is transmit on a write, receive on a read. `BASE+2`
writes the baud-rate divisor and reads modem status; `BASE+3` writes the modem chip's control word
and, read, returns nothing the card drives. The three control registers are **write-only** — the
program keeps its own copy of what it wrote, exactly as it had to on the hardware.

There is no telephone network in the box, so **`CONNECT` its `line` to a byte source and sink**: the
straightforward test is a pair of paper-tape-style files — `CONNECT pmmi0:line
in:incoming.tap,out:outgoing.tap` — where what the guest sends lands in one file and what it receives
is read from the other. The bytes are exact; the Bell 103 tones are not simulated, because the
line here is a byte stream, not audio.

**What it does *not* do yet, on purpose.** It does not dial: the make-and-break of the hook relay a
period dialer program produces is not decoded into a phone number, and no number picks a far end —
**placing the call is `CONNECT`'s job**, and reading a number out of the hook would be a behaviour
this card never had. Modem status (dial tone, ringing, carrier, clear-to-send) reads a fixed
"connected and ready" value rather than following a real handshake, and the card raises no
interrupts. `SHOW pmmi0` reports the live frame, baud, UART flags and modem lines alongside the
base address. To try one, add it to `default` — `BOARDS ADD pmmi` fits it at `C0`.

---

## Cassette

Boards that read and write cassette tape.

## `acr` — MITS 88-ACR

The **cassette interface**: an 88-SIO channel B with an FSK modem on the end of it, so a byte on the
bus becomes an audible tone on a tape and back again. Unit `tape`, default port `06`, and it runs at
300 baud because that is what an audio cassette could carry.

It brings verbs of its own — **`WIND`**, **`REWIND`** and **`EXTRACT`**. The first two are there
because a tape has a **position** and a disk does not, and pretending otherwise would help nobody:
`WIND` puts the head at a time on the tape (`mm:ss`, or `START` / `END`), so a tape holding several
programs one after another is reachable, `REWIND` is the common case of `WIND START`, and `SHOW`
reads the counter back the same way. **`EXTRACT`** is a different job — it demodulates a mounted
`.WAV` and writes each program it finds out as its own `.TAP` file, so an audio recording becomes
something you can mount directly.

This is the board that shows you what an Altair actually was: no disk, no PROM, a bootstrap you
toggle in by hand, and eight minutes of listening to a cassette. **The tapes chapter is the one to
read**, and {{NAME_BASIC}} is the machine to run.

---

## `uio` — MITS 88-UIO

**A serial port and a cassette interface on one board** — the Universal I/O card, which is very
nearly what a 2SIO channel and an 88-ACR are when you put them on the same card and let them share
the parts. It comes up looking like the two boards it replaces: a **6850 serial port** at `10`
(unit `serial`) and a **cassette section** at `06` (unit `tape`), the standard addresses, so
software that expects a 2SIO console and an ACR tape finds both where it left them.

What it adds over two separate cards is **motor control** and a **modulation switch**. `SW-1`
chooses between the two encodings the era used — the MITS 300-baud format and the Kansas City
standard — because the UIO was sold to talk to either. The tape half brings the same
`WIND`/`REWIND`/`EXTRACT` verbs and the position counter the 88-ACR does.

---

## Printers

Line-printer controllers — each captures its output to a file or a print queue.

## `c700` — MITS 88-C700

The **line-printer controller**: an output-only board that sends characters to an Altair C700
printer. Unit `prn`, default port `02` — the MITS default, with Control/Status at `02` and Data
at `03`.

There is no printer in the box, so **`CONNECT` its `prn` line wherever you want the output**: a
file (`CONNECT lpt0:prn out:printout.txt`), the `console` to watch it print live, a `socket:`, a
real `serial:` printer, or a **real print queue on your host** (`CONNECT lpt0:prn
printer:linewriter`) — that last one where your build found a print system, and the serial chapter
has the job-submission options. The capture is byte-for-byte — the bytes the program sent, control
codes and all, not a reformatted page.

Drive it **polled or on interrupts**. Polled: write a character to the data port (`03`), then poll
the status port (`02`, bit 0 ACKNOWLEDGE, set = ready) before the next. Or arm its **single-level
interrupt** (control bit 1) and let the printer interrupt the CPU each time it takes a byte, so the
handler just feeds the next one. The `interrupt` strap picks the wire — pin 73 (`int`, the default),
a vectored-interrupt level (`vi0`..`vi7`), or `none`; `interrupt_after` is the card's SW2 #4,
firing after every character or only after a CR/LF.

The **`lineprinter`** machine is `default` with one of these already fitted; its output starts on
`null`, so `CONNECT lpt0:prn out:printout.txt` to capture it to a file, or `console` to watch it
live (that takes the console from the terminal).

---

## `lpc` — MITS 88-LPC

The **other** line-printer controller — for the 88-LP printer, where the `c700` drives the C700.
Same two-port shape (control at an even base, data above it; MITS default `02`), but it drives the
mechanism the way it really worked, and the difference shows in what you capture.

The C700 is a **transparent byte pipe**: the bytes the program sends are the bytes you get, control
codes and all. The **LPC is line-buffered**. The guest loads a **6-bit character code** at a time
into an **80-character line buffer**, and nothing prints until it sends a **PRINT** command — or the
buffer fills. **LINE FEED** and **CLEAR** are commands too. So the capture is the printed *page* —
the codes decoded to their glyphs, one text line per printed line — not a byte stream, because on
this board the line breaks are commands, not data.

`CONNECT` its `prn` line wherever a line can go — an `out:` file, the `console`, a `socket:`, a real
`printer:` queue. The **`lineprinter-lpc`** machine has one fitted at `02`, its output on `null`
until you `CONNECT` it. It is polled; unlike the `c700`, this card's interrupt is not modeled.

---

## Parallel and analog I/O

Parallel ports, and one board that also reads analog inputs.

## `pio` — MITS 88-PIO

An **8-bit parallel port**, in and out — the simplest way to move a byte that is not a serial
character. It has **two lines you `CONNECT` independently**: `out` (an output device — a printer, a
socket) and `in` (an input device — a keyboard, another socket), so one board can punch to an
`out:` file *and* read a keyboard off the `console` at once, because the two directions are two
separate connections. Default ports `04`/`05`. It is **polled**: a byte moves when a driver polls the
status port for it.

---

## `4pio` — MITS 88-4PIO

The programmable cousin of the `pio`: up to **four Motorola 6820 PIAs** on one card, whose **data
direction the guest sets in software** rather than the board fixing it. Each populated port is a
section — `ja`, `jb`, and so on — and each section is its own connectable line, so a card with two
PIAs fitted gives you several independent ports to `CONNECT`. Sixteen ports from a default base of
`20`. Polled, like the `pio`.

Between them the two parallel boards cover the span from *a fixed eight bits each way* to *however
the software wants it configured* — the same range the real MITS parallel line did. The
**`parallel`** machine has a `pio` fitted and capturing to a file.

---

## `d7a` — Cromemco D+7A

An **analog and parallel I/O card** — one parallel port and **seven analog channels** in a block of
eight ports (default base `18`). Each analog channel is an **A/D converter when you read it and a
D/A converter when you write it**, in 8-bit two's-complement: `00` is 0 V, `7F` about +2.5 V, `80`
about −2.5 V. On a real bench it read sensors and drove instruments.

Here its job is the input end of a **game console**. It reads **one or two JS-1 joysticks**: the X
and Y pots on analog channels, and the four buttons — **active-low** — packed into the parallel byte,
low nibble for one stick, high nibble for the other. The sticks come from your host through the same
kind of injected service the display uses: a **USB gamepad** where SDL3 is present, or the
**keyboard** as a fallback (arrows and a few keys), and nothing at all in a headless build, which
still runs.

`joystick1`/`joystick2` choose which host device drives each console. Both default to `auto`,
which claims a **different** gamepad per console — console 1 takes gamepad 0, console 2 gamepad 1 —
so two controllers work with no configuration, each falling back to the keyboard when its gamepad
is absent. `SHOW <id>` shows what each console currently resolves to (a named controller, the
keyboard, or nothing), and `SHOW JOYSTICKS` lists the controllers your host actually sees. The
Dazzler examples in `examples/` pair the board with color graphics and set the video window to be a
**display, not a keyboard** (`[display] keyboard = none`), so your keystrokes drive the stick instead
of landing at a prompt. (The board's designed-but-unbuilt sound output — a JS-1's speaker is a D/A
the CPU writes a waveform to — is not here yet.)

## Floppy and disk controllers

The disk controllers, from 8-inch floppies to CompactFlash — several boot an operating system by themselves.

## `dcdd` — MITS 88-DCDD

The **8″ hard-sector floppy controller**, up to sixteen drives, three ports at `08`, `09` and `0A`.
**This is the board CP/M booted from**, and it is in `default`.

It also carries the **8 MB medium** — a large-capacity format the same controller can address,
on an image **you supply**; no 8 MB disk is in the package.

Its status bits are **inverted**, for the same reason the 88-SIO's are and with the same
consequence: a clear bit means ready.

The disks chapter is where this board lives: formats, mounting, write protection, and the
track-buffer trap that means you should get back to the `A>` prompt before you stop the machine.

---

## `mds` — MITS 88-MDS

The **5¼″ minidisk**, four drives. **The same registers as the DCDD** — a program written for one
will drive the other — but **different physics**:

| | `dcdd` | `mds` |
|---|---|---|
| Spindle | 360 RPM | **300 RPM** |
| Byte time | 32 µs | **64 µs** |
| Motor | always turning | **stops after 6.4 seconds** |

The minidisk's motor is not permanently on. It spins up, and if nobody touches the drive it spins
down again — which the software has to cope with, and which you can watch it cope with by setting
the motor to `real`. By default the motor is `free`: always at speed, no waiting. The DCDD needs no
such switch, because its spindle never stopped.

**A minidisk image is one you supply.** The board is here and the `minidisk` machine boots its
PROM, but no 5¼″ image is in the package, so the drives come up empty.

### It cannot share a machine with a `dcdd`

**Same three ports.** Two controllers decoding `08` is not a limitation of this program; it is the
MITS address map, and a real Altair with both cards in it would have had them fighting on the data
bus. Fit both here and the bus view will name the contention rather than leave you wondering why
the guest has gone strange.

Pick one.

---

## `hdsk` — MITS 88-HDSK Datakeeper

A **hard-disk controller** — the "Datakeeper" — and the board CP/M boots from when it is not booting
from a floppy. It is an outboard controller with a **command/handshake protocol** and four internal
**256-byte page buffers**, so unlike the floppy cards it **moves whole sectors for you** rather than
shifting bits in real time. Eight ports, default `A0`–`A7`.

The **HDBL** boot PROM at `FC00` reads the disk's descriptor page and brings the system up. There
is a ready-made machine for it in `examples/`, image and all: run it and you land at `A>` on a
multi-megabyte CP/M 2.2 platter, **read/write**, with CP/M saving to it. Its README says how.

---

## `versafloppy` — SD Systems VersaFloppy I & II

SD Systems' **soft-sector floppy controller**, built around a **Western Digital FD177x** — and the
board that boots **SDOS**, a CP/M work-alike, on an SBC-200.

It is **one board covering both generations**, chosen with `variant`: **`vfii`** (the default) is
the double-density **FD1791** VersaFloppy II, and **`vfi`** the single-density **FD1771**
VersaFloppy I. They differ only in the controller chip and a few control bits; the port block (eight
ports, default `60`) and the driver family are the same. Neither carries a boot PROM — the bootstrap
BIOS lives on a separate PROM, which on the SBC-100/200 is the onboard socket.

Fit a `vfii`, mount an 8″ double-density disk, and with the SBC-200's **DDBIOS** the monitor's disk
commands come alive: **`C`** cold-boots SDOS, **`R`** and **`W`** read and write sectors. The SD
Systems example in `examples/` is that machine with the disk already in it: press Return for the
auto-baud, type `C`, and *32K SD-OS* comes up to its `[A]` prompt.

### What it will not do

`Z` **formats** a disk, and here it cannot make one from nothing. A raw disk image is only its data —
it has none of the gaps and address marks a real format writes *between* the sectors — so there is
nothing for a low-level format to lay down, and the controller says so (a WRITE FAULT) rather than
pretending. This is the honest limitation every soft-sector controller here shares: mount a disk that
already carries a format and read and write work; ask the board to create a blank one and it tells
you it can't. For a fresh SDOS disk, copy one that is already formatted.

---

## `tarbell` — Tarbell #1011 single-density floppy

The **Tarbell Electronics #1011** (July 1977) was the S-100 floppy interface that booted CP/M on a
whole generation of Altair and IMSAI machines. It is a **Western Digital FD1771** soft-sector
controller — eight ports, default `F8` — and, unlike the VersaFloppy, it carries **its own 32-byte
boot PROM**. That is the whole experience of this card: you do not type a boot command.

Power on with a disk in drive 0 and the machine boots itself. RESET arms the PROM at address 0000,
where it shadows the bottom of memory; the PROM reads the first sector off the disk, and the moment
the loaded code runs, the shadow falls away and CP/M comes up. There is a Tarbell example in
`examples/` with a disk in the drive: run it and you land at `A>` with no monitor in between. With
no disk in the drive the PROM has nothing to load and simply halts — put a disk in and reset.

The `bootstrap` switch turns the PROM off, leaving a plain disk controller for a machine that boots
some other way. Four drives, selected by the software; the disks are 8″ single-density, 128-byte
sectors, and the card recognises them by size when you mount one.

## `tarbelldd` — Tarbell #2022 double-density floppy

The **#2022** (1979-80) is the #1011's twin with a **Western Digital FD1791**, which reads
**double-density** as well as single. It boots exactly the same way — the same automatic boot PROM,
the same `F8` ports — from a **mixed-density** disk: the first track is single density (so the boot
PROM can read it), and the rest are double density. The same Tarbell example folder carries a
double-density machine that boots CP/M 2.2 off one to `A>`.

Everything the `tarbell` card does, this one does; it adds only the second density and a disk format
that carries more per track. Choose it when your disk is a double-density Tarbell image; choose
`tarbell` for a single-density one. The card tells the two apart by the size of the image you mount.

---

## `icom` — iCOM FD3712 / FD3812 8″ floppy

An **8″ floppy controller** of a different kind: not a bit-shifting floppy card but a
**command/handshake** one, like the Datakeeper — it buffers a whole sector and the CPU moves bytes
through two ports (default `C0`–`C1`), while the operating system's disk driver lives in a **boot
PROM** up in high memory. One board covers both iCOM generations: the single-density **FD3712** and
the double-density **FD3812**, whose disk is mixed density (a single-density track 0, then
double-density tracks). The card tells them apart by the size of the image you mount.

Which system it boots is set by its PROM, chosen with `rom`:

- **`builtin:icom-fd3712-cpm`** (the default) boots **CP/M 2.2** single density from the PROM at
  `F000`.
- **`builtin:icom-fd3812-cpm`** boots **CP/M 2.23** double density, also at `F000`.
- **`builtin:icom-fd3712-fdos`** boots iCOM's own **FDOS** disk operating system from the PROM at
  `C000` — not CP/M, but iCOM's `!`-prompt executive, with its own `LIST`, `EDIT`, `ASMB` and the
  rest.

`altairsim icom` is CP/M 2.2 the moment a disk is in it. The `examples/` folder carries ready-made
machines for all three — single- and double-density CP/M and FDOS-III — image and all: start one and
you land at the prompt, read/write, with the guest saving to the disk. Read and write of existing
disks work; like the other controllers here, it will not lay down a **blank** format from nothing.

---

## `dualsd` — S100Computers Dual SD

A **modern** disk controller among the period ones: the S100Computers Dual SD board puts **two
microSD cards** on the S-100 bus as raw 512-byte-sector drives, so a Z80 machine runs **CP/M 3** off
flash. Like the iCOM and the Datakeeper it is a **command-and-handshake** card — two ports (default
`80`–`81`) and a byte-at-a-time protocol to an onboard microcontroller that does the actual card
I/O — not a floppy-shift card. Each SD card is one CP/M drive, and the drive letter comes from the
**socket** it sits in: socket 1 is A:, socket 2 is B:.

**It has no boot PROM** — the CPU board's monitor boots it. That is why `altairsim dualsd` is the
three boards together: a `z80` for the processor, the `v2z80rom` monitor EEPROM whose `I` command reads
CP/M 3 off the card, and this controller. Bring the machine up, type `I` at the `->` monitor prompt,
and CP/M 3 signs on at `A>`.

**Mount a card in both sockets.** The CP/M 3 boot ROM checks that each socket has a card before it
will come up — a card in socket 1 alone boots the loader and then stops. So the example fits the
bootable system card in socket 1 and a blank spare in socket 2.

A card is a raw **`.img` with a sibling `.geo`** file beside it that declares the card's true size.
The `.img` can be **shorter** than the card — just the live filesystem — and every sector past its
end reads back as an erased card would (all `FF`), which is why a card that would be hundreds of
megabytes on real flash ships here as a couple of megabytes. `MOUNT … CREATE` creates a **blank**
data card (an empty `.img` and its `.geo`) for the guest to format; a *bootable* card, like the
other controllers here, has to come from a real image — its system tracks cannot be invented. See
`examples/dualsd/`, which boots CP/M 3 with the host bridge fitted so `R`/`W`/`HDIR` move files to
and from your host at the `A>` prompt.

---

## `dualide` — S100Computers IDE-AB (CompactFlash)

The other half of the same physical S100Computers card. Where the Dual SD engine drives microSD,
the **IDE-AB** side drives **two CompactFlash cards** through an 8255 parallel port wired to a CF
card's IDE bus (default ports `30`–`34`). It presents its two cards as CP/M drives A: and B:, boots
the same **CP/M 3** off flash, and — because it lays a card out byte-for-byte the way the SD side
does — **a card image is interchangeable between the two**.

**It has no boot PROM** either. `altairsim dualide` is the `z80`, the `v2z80rom` monitor EEPROM, and
this controller; at the `->` monitor prompt the **`P` command** (rather than the Dual SD's `I`) reads
CP/M 3 off CompactFlash drive 0, and CP/M signs on at `A>`.

Cards behave exactly as on the Dual SD board: a raw **`.img` with a sibling `.geo`**, truncatable to
the live filesystem with `FF` past its end; `MOUNT … CREATE` creates a blank data card, while a
bootable one has to come from a real image. See `examples/dualide/` for the CompactFlash-only
machine, and `examples/dualidesd/` for the **whole combination board** at once — CompactFlash as
A:/B: and microSD as C:/D:, one CP/M 3 system spanning all four drives.

---

## Video and display

Boards that put a picture on a display window.

## `vdm1` — Processor Technology VDM-1

**Memory-mapped video**, and the first board here that is not a MITS one. A 1K screen of **16 rows
by 64 columns** lives in the machine's own address space — by default at `CC00` — so a program
puts a character on the screen by *storing a byte*, with no port and no driver. That is why it is
fast enough to be worth having, and why it needs no `CONNECT`: the screen is memory.

One port (default `CC`) does the rest: writing it sets which row is at the top, which is how the
VDM-1 scrolls — the text does not move, the *window* does. Reading it gives back two timing bits
the software uses to avoid writing while the beam is in the way.

**It needs a display.** Built with SDL3, it opens a real window; built without, it runs headless
and everything else still works — a program writing to the screen simply has nowhere to show it.

**Closing that window stops the machine, it does not quit the simulator.** The close box is the
operator talking, so it does what `ATTN` does: the guest stops at an instruction boundary and you
get the monitor prompt back, with the machine exactly where it was. `RUN` resumes it into the same
window; `QUIT` is still how you leave.

**Which window has your keyboard is yours to say.** By default the terminal keeps it: the video
window opens behind whatever you were doing, and when the guest stops you can type at
`altairsim>` straight away. You can still click the window and type into it — keys typed there
and keys typed at the terminal reach the guest as one stream — but the next time the guest stops,
the keyboard goes back to the terminal.

That is the right default for a machine you drive from the monitor, and the wrong one for a
Sol-20, where the window *is* the console. So:

```
altairsim> SET DISPLAY focus=on
```

and the window comes to the front when it opens and keeps the keyboard when the guest stops.
`SHOW DISPLAY` says which way it is set, and a machine file can ask for it directly:

```toml
[display]
focus = true
```

It is a setting of the **display**, not of this board — a machine with two video boards still has
one operator with one keyboard — so it reads the same whichever board is drawing. Setting it says
what should happen from now on; it does not go back and re-focus a window that is already open.

Bit 7 of each byte is the **cursor/blink** flag rather than part of the character, so the board
draws 128 glyphs from a real character ROM, not 256.

Two machines fit one: **`vdm1`**, which is an Altair with a VDM-1 and a demo that draws on it, and
**`cuter`**, which runs the period CUTER monitor with its own built-in VDM-1 driver.

### How big the window opens — the `width` property

Every video board — the VDM-1, the Dazzler, the VDB-8024 — carries a **`width`** property that
sets how wide its window opens, in pixels. `auto` (the default) opens the window about **half the
screen wide**; a number like `width = 1024` asks for that many pixels. The **height follows the
board's own aspect** — you set width, height comes with it — and in the crisp default the picture
is drawn at a whole-number multiple of the board's pixels so a 1970s frame stays a crisp grid rather
than a blur (the leftover is a thin dark border). Under the period tube look (`[display] crt = true`)
the window instead fills exactly the width you asked for, since the soft raster has no square pixels
to keep sharp. A width that would run off the screen is brought down to fit.

The built-in `terminal` window (see the [Serial ports](serial.md) chapter) sizes itself the same
way, through a `width=` option on its connect string rather than a board property.

`width` lives on the board, not on `[display]`, because on real hardware each board has its own
video-out and could drive its own monitor — and the simulator matches that: **each video board
opens its own window**, sized by its own `width`. Fit a VDM-1 and a Dazzler in one machine and you
get two pictures on two windows, just as the real cards would drive two monitors. The keyboard and
focus stay shared, though — one operator, one keyboard (see `[display]` in the Configuring
chapter), so whichever window you type into, the keys reach the machine.

### A stopped machine's window is responsive, but it does not redraw

The window stays live even when the machine is stopped: you can move it, and its close button
works — clicking it at the monitor prompt closes the window (`RUN` opens a fresh one the next
time a program draws). What a stopped machine does **not** do is *repaint*, because drawing is the
running machine's job. Two consequences, and they are the same fact:

- **A change like `SET vdm0 video=reverse` does not appear** until you `RUN` again — the setting
  is remembered, but nothing redraws the screen to show it until the machine does.
- **The cursor does not blink** while the machine is stopped. To watch it blink, use `sol20`,
  whose SOLOS sits in a loop rather than halting.

`vdm1`'s demo halts once it has drawn its banner — that is the point — so the window you are left
looking at belongs to a stopped machine: still there, still closeable, just not redrawing.

Closing the window of a **running** machine is not the same as quitting: it stops the guest and
gives you the monitor prompt, leaving the machine exactly where it was and the window on screen.
`RUN` goes back into it; `QUIT` exits.

---

## `dazzler` — Cromemco Dazzler

The **first color-graphics card for the S-100 bus**, and the second video board here that is not a
MITS one. Where the VDM-1 paints text, the Dazzler paints a **picture**, and it does it the same
clever way: out of a **framebuffer in the machine's own RAM**. You point the board at a 512-byte or
2 KB block anywhere on a 512-byte boundary and it scans that memory onto the screen — so a program
draws by *storing bytes*, with no port in the inner loop.

Two ports (default `0E`/`0F`) set the rest: on/off and where the framebuffer is, then the format —
resolution, size and color. Four modes fall out of it: **32×32 or 64×64** color or grey elements,
and **64×64 or 128×128** on/off elements, in **16 colors** or 16 greys. Small numbers — but this was
1976, and it was in color.

**It needs a display**, and like the VDM-1 it draws into it: an SDL3 build opens a window, a headless
build runs and simply has nowhere to show the picture. The Dazzler example in `examples/` comes up
running **Li-Chen Wang's Kaleidoscope**, a four-way-mirrored pattern turning over in the window
(`ATTN` breaks back to the monitor); the `dazzler` machine is the bare board to build on. Because a
64×64 frame is tiny, the board's `width` property (above) sizes the window up to land near a VDM-1's
size on your screen rather than a sixth of it.

---

## `vdb8024` — SD Systems VDB-8024

An **80-column by 24-line video terminal on one board** — the SD Systems answer to a serial
console. Where the `sbc` card gives an SBC-100/200 a serial port for a teletype or a glass
terminal, the VDB-8024 gives it *the terminal itself*: a whole intelligent display, keyboard and
all, plugged straight into the backplane.

**Despite the name, it is not memory-mapped.** Nothing of its screen lives in the machine's
address space. To the computer it is simply **two I/O ports** — a status port and a data port,
at `00` and `01` — that behave like a terminal on a wire: the program reads the status to
see whether a key is waiting or the display is ready, writes a character or a control code to the
data port, and reads a typed key back from it. The screen, its memory and its own processor all
sit behind that pair of ports, invisible, exactly as they were on the real card. The real card's
pair was wired at `00`, not jumpered; the board here still carries a `port` so you can move it,
the same liberty the `sol` takes below.

It runs the **SD monitor's video build, `sdmonv21`**, which is the same monitor as the serial
`sbc200` machine (same commands, same `.` prompt) built to talk to this board instead of the 8251.
There is **no “press Enter first”** here — the VDB is not a serial line with a speed to measure, so
the prompt is on the screen the moment the machine starts:

```
altairsim sbc200v
```

comes up at the monitor's `.` prompt **on the video display**, and the SD Systems example in
`examples/` carries the same machine as a file you can read and change.

**It needs a display**, like the VDM-1: built with SDL3 it opens a real window in the board's own
character font; built without, it runs headless and the text simply has nowhere to show. And like
a Sol-20, **the window is the console** — the machine asks for `focus=on`, so the window keeps the
keyboard while the guest runs, and window keys and terminal keys reach the monitor as one stream.
The characters are drawn from the board's own character-generator font, with true lower-case
descenders on `g`, `j`, `p`, `q` and `y`, the way the hardware's socketed font PROM did it.

The board understands the control codes its firmware did: carriage return, line feed, backspace,
tab, cursor up and right, clear-screen and home, and the `ESC` sequences that position the cursor
and erase to the end of a line or the screen. A line that fills wraps, and a line feed at the
bottom scrolls the page up.

---

## `sol` — Processor Technology Sol-PC

The **Sol-20's onboard I/O, as one card** — because on a real Sol-20 that is what it was. The
Sol was not an Altair with cards in it; it was an integrated machine whose serial port, keyboard,
parallel port and cassette interface were all on the one processor board, at `F8`–`FE`. On the
real hardware those addresses were wired, not jumpered; here the board still carries a `base` so
you can move it, which is the one liberty taken and the reference chapter records it.

So this board carries four things at once, and you reach them as units: `serial`, `printer` and
`keyboard` are lines you `CONNECT`, and `tape1`/`tape2` are cassette transports you `MOUNT`. The
keyboard is connected to the console by default, so it simply takes what you type — from the
display window when there is one, and from your terminal when there is not.

### The keyboard's special keys

The Sol's keyboard is not a subset of a modern one. Eight of its keys send codes with no ASCII
equivalent at all, and they are how you drive SOLOS and the screen:

| Key | Sends | What it does | Press | Or type |
|---|---|---|---|---|
| `←` | `81` | Cursor left one | ← | Ctrl-A |
| `→` | `93` | Cursor right one | → | Ctrl-S |
| `↑` | `97` | Cursor up one | ↑ | Ctrl-W |
| `↓` | `9A` | Cursor down one | ↓ | Ctrl-Z |
| `HOME CURSOR` | `8E` | Cursor to the top left, screen untouched | Home | Ctrl-N |
| `MODE SELECT` | `80` | Return to the command mode, restarting the command line | F1 | Ctrl-@ |
| `CLEAR` | `8B` | Erase the screen, cursor home | F2 | Ctrl-K |
| `LOAD` | `8C` | Nothing — neither SOLOS nor CONSOL ever claimed it | F3 | — |

**The `Press` column works in the video window only.** Your keyboard's own arrows and Home, and
the function keys F1–F3, send these codes when the window has focus. They cannot work from a
terminal: there an arrow or function key sends an escape sequence rather than a single byte, and
`ESC` is a character the guest legitimately needs, so there is nothing to safely match on. From a
terminal, use the last column.

A PC keyboard has nothing that reads as `MODE SELECT`, `CLEAR` or `LOAD`, so the function keys
stand in for them: **F1** is `MODE SELECT`, **F2** is `CLEAR`, **F3** is `LOAD`. On a Mac the
function keys reach the window only if the system is set to send F1, F2, … as plain function keys
(otherwise hold `fn`).

**That last column is not a hack** — it is how the hardware was built.
Each special key's code is exactly `80` plus the control code for the same action, because SOLOS's
display driver masks the top bit off everything it is handed before it looks the character up. So
`CLEAR` and Ctrl-K arrive at one routine, `HOME CURSOR` and Ctrl-N at another. The command-mode
reader does the same masking, which is why a NUL byte is `MODE SELECT`: type Ctrl-@ (Ctrl-Space on
many keyboards) and SOLOS abandons the line you were typing and gives you a fresh prompt.

The console is 8-bit clean, so if you have some way of sending the byte itself — a paste, a
script, a terminal macro — the real code works too, and behaves identically.

One register (`FA`) reports the state of *all* of them at once — and it does so with **mixed
polarity**, the keyboard and parallel bits reading active-low while the tape bits read
active-high. That is not a bug in the board or in this simulator; it is what the hardware did, and
the period software inverts what it needs.

### The cassette decks

The Sol's cassette interface is on the motherboard, not a card, and it has **two decks** —
`tape1` and `tape2` — where the 88-ACR has one. Everything in the tapes chapter applies to them;
only the unit name changes, and you must always name the deck (a bare `WIND sol0` is refused
rather than guessing which tape to move):

```
altairsim> MOUNT sol0:tape1 "mytape.tap"
altairsim> SET  sol0:tape1 mode=record
altairsim> REW  sol0:tape1
```

Three things differ from the ACR, and each is the hardware talking:

- **The Sol can work the motors.** `OUT 0FAh` starts and stops each transport, and SOLOS does it
  for you — `SAVE` spins the deck up, writes, and spins it down. So a Sol tape plays only while
  the guest is running it, and a deck whose motor is off yields nothing at all rather than merely
  nothing yet. That dropped motor line is a *stop*, so it writes a recording out — something the
  88-ACR, which cannot see the deck at all, has no equivalent of. It still cannot **wind** the
  tape on its own: a motor line only says *turn*, not which way, so `WIND`/`REWIND` is your finger
  here too.
- **The speed is the guest's.** The ACR's 300 baud is soldered; the Sol's cassette runs at 300 or
  1200 and `OUT 0FAh` bit D5 picks, at run time. SOLOS's `SE TA` command is that bit.
- **The modulation is CUTS, not the ACR's FSK.** A Sol tape is `cuts1200` (1200/600 Hz) at 1200
  baud, an octave below the ACR's 2400/1850 Hz; it also reads Kansas City (`kcs300`, 2400/1200).
  This is why a Sol tape mounted on an 88-ACR is refused — the tapes chapter has that story.

Once a tape is in, SOLOS's own commands work:

```
>SA MYPROG 0100 01FF        (save memory to the tape)
>GE MYPROG                  (find it again and load it)
>CA                         (catalog what is on the tape)
```

**Writing audio a real Sol will load** takes more than the right tones — it takes the right
*shape* and level. A genuine Sol CUTS modem is a flip-flop dividing a master clock into a square,
rounded by an RC network, recorded at a modest level. Three properties reproduce that when the
board writes a `.WAV` back, and their defaults are measured off a real archived dub:

| Property | Default | What it does |
|---|---|---|
| `leader` | `3` | Seconds of steady tone before the data — the real dub carries 3.05 s (the ACR's is longer, 15 s). |
| `trailer` | `2` | Seconds of tone after it — the dub's measured 1.93 s. |
| `rc` | `4000` | Edge-rounding low-pass corner, Hz. Rounds the square's edges the way the modem's RC network and the cassette's own bandwidth do, so the tone curves like a real dub instead of sitting on a flat top. CUTS only. |

`level` (percent of full scale, default `36`) matters on both boards and is covered in the tapes
chapter; the point of these Sol defaults together is that a CUTS tape the simulator writes lays
its tones on the same clock grid a real Sol expects, at a real dub's level — built to load on the
hardware, not only to read back here.

Fit it with a `vdm1` and you have the **`sol20`** machine, which cold-starts the real SOLOS
operating system.

---

## Interrupts and the clock

Vectored interrupts and a real-time clock, on one board.

## `virtc` — MITS 88-VI/RTC

Two things on one board, which is why it has an awkward name.

**Vectored interrupts.** Eight lines, **VI0 through VI7**. A device is strapped to a line; when it
interrupts, **level *n* becomes `RST n`** — the processor jumps to `8×n` and the right handler runs
without anybody having to poll anything. **VI0 is the highest priority**, and the board enforces
that: a lower level cannot interrupt a higher one that is being serviced.

This is what turns a machine that busy-waits into a machine that gets on with something else. Every
board with an interrupt strap in its properties — the serial boards, the floppy controllers — is
strapped to one of these lines, or to `int`, or to nothing at all.

**A real-time clock**, on the same board: a periodic interrupt off the 60 Hz line or off the system
clock, divided down.

One port at `FE`, and it is **write-only**. There is nothing to read back. Interrupt boards are the
easiest thing in a machine to get subtly wrong, and this one is worth reading the reference for
before you strap anything to it.

`ps2int` is the machine that shows it working — with a MITS Programming System II tape **you
supply**, since none is in the package. Its cassette deck comes up empty.

## `ss1` — CompuPro System Support 1

A **multifunction** board: on the real card, one 16-port block holds a serial channel, an
interval timer, two interrupt controllers, a battery-backed **real-time clock/calendar**, and a
socket for a math coprocessor. This board is its **clock**, its **serial channel**, its **interval
timer** and its **dual interrupt controllers** — everything but the (empty) math-chip socket.

The clock is the OKI MSM5832. It comes up reading **your host's own date and time**, so a guest
program that reads it gets the real wall clock for free. A guest can also **set** it, and once set
it is battery-backed — the time survives a RESET, exactly as the real chip's battery does. The
block sits at base **`50H`** by default (the CompuPro convention), movable with the `base` property.

The serial channel is a **2651 UART** at `5C`–`5F`. It is a spare serial port — a guest programs
its rate, frame and RS-232 handshaking through the mode and command registers — that you point at
something with `CONNECT ss1:serial <endpoint>`. Its `baud`, `interrupt` and `connect` are unit
settings, shown under `SHOW ss1`.

The interval timer is an **Intel 8253** at `54`–`57`: three independent 16-bit counters a guest
can program as timers or square-wave/rate generators. The counters tick at 2 MHz. Their outputs
feed the interrupt controllers. `SHOW ss1` prints a live `timer` line with each counter's mode,
count and output.

The interrupt system is two **Intel 8259A** controllers at `50`–`53`, cascaded master and slave.
The master watches the eight vectored-interrupt lines of the bus and drives the CPU's interrupt
pin; the slave gathers the on-board sources — the three timer outputs and the UART's transmit- and
receive-ready signals — and feeds them up through the master. A guest programs them the usual
8259A way (the ICW/OCW words), unmasks the sources it wants, and on an interrupt the controller
hands the CPU a `CALL` to the vector for the winning source. `SHOW ss1` prints a live `pic` line
with each controller's request, in-service and mask registers. Because the master is the machine's
interrupt priority encoder, do not also fit an `88vi` — the two would fight over the acknowledge.

`compupro` is the machine that fits one: a stock Altair with the System Support 1 added, its
console still on the 2SIO. The clock lives at `5A` (command) / `5B` (data); the digit map, the
read/set sequences and the register layouts are in the board's reference.

---

## The whole machine

The front panel, and the turnkey board that stands in for it.

## `fp` — the front panel

The sense switches. The panel is **a board**, because on a real Altair it was one — it
plugged into the bus like everything else, and a machine without it is a machine you cannot toggle
a bootstrap into.

### The SENSE switches, at port `FF`

The eight switches on the left of the address bank, `SA8`–`SA15`. **`IN FFH` reads them.** They are
**read-only**: an `OUT FF` is not this board's business and goes nowhere at all.

`SA15` is the **top** bit of the byte the program reads and `SA8` is the bottom, so the switches
line up left to right the way they sit on the panel:

```
switch   SA15  SA14  SA13  SA12  SA11  SA10   SA9   SA8
bit         7     6     5     4     3     2     1     0
value    0x80  0x40  0x20  0x10  0x08  0x04  0x02  0x01
```

That is what makes a period boot procedure followable. "Raise A15 and A11" is `0x80` plus `0x08`
— or, written so it looks like the panel, `0b10001000`.

**They are not decoration.** Period bootstraps read the sense switches to decide **what to boot
from** — which device, at which port, at which speed. That is why every tape machine in this package
sets one:

```
sense = 0x80        # basic4k: load from the 88-SIO at port 00
sense = 0b10000000  #          the same eight switches, drawn
sense = 0x8E        # ps2:     the 2SIO, and interrupts off
sense = 0b10001110  #          again, one digit per switch
```

Binary is nothing special to this board — `0b` works anywhere a number does, at the prompt and in
a machine file alike, and the *Monitor* document's number rule has the whole list of prefixes. It is
just that eight switches would rather be eight digits than two hex ones. Whichever way you write
it, `SHOW fp` reads the byte back in hex.

Get it wrong and the loader sits there reading a device that is not there. It will not tell you.
It has no way to.

`sense` is a **board property**, because the switches are on the panel. There is no machine-level
`sense`, and asking for one is an error that says so.

---

## `turnkey` — the MITS 8800bt, on one card

The 8800b "turnkey" system had **no front panel**. One board — the Systems Turnkey Module —
did the panel's job and more: it carried the boot PROM, the terminal serial port, the sense
switches, and a circuit that booted the machine the moment you switched it on. This board is
that card, so a `turnkey` machine has **no `fp` and no separate `2sio`** — all three live here.
`altairsim turnkey` is the bare machine. Give it a disk and one of the two boot loaders below
— `start = "FF00"` for the floppy, `FC00` for the hard disk — and CP/M comes up the moment it
powers on.

### It boots itself

There is no front panel to toggle a bootstrap in from, so the card has an **Auto-Start**
circuit. `RUN 0000` starts the processor at address 0, and the card jams a `JMP` onto the bus
so the first thing that runs is the boot PROM — exactly what pressing the panel's START switch
did. The `start` property is the START ADDR switches: `FF00` runs the floppy loader, `FC00` the
hard-disk loader. A `startup = ["RUN 0000"]` in the machine file makes it happen at launch.

### The boot PROM gets out of the way

The PROM sits at `FC00`–`FFFF` and **shadows the RAM underneath it for reads** — until the
first `IN` from port `FE` or `FF`, when it switches itself out and the machine has the **full
64 KB** of RAM. That is why this machine has 64K where a front-panel Altair stops at 56K: the
PROM is not permanently taking up the top of memory, it is a boot device that steps aside. The
same `IN FF` that reads the sense switches is what triggers it — which is exactly how period
software (Altair BASIC, the CP/M loaders) frees the whole 64K without knowing the trick.

### The console and the sense switches

The serial console is the `tty` unit, at port `10h`, and it behaves like the A channel of a
`2sio` — so the same software drives it. The sense switches answer port `FF`, as on the front
panel. Because this card owns `FF`, **do not put an `fp` in a `turnkey` machine**: they would
both try to answer the port.

The PROM sockets are a list, like a memory card's regions:

```
[[board.socket]]
at    = 0xFC00      # socket L1 — the hard-disk loader
mount = "builtin:hdbl"
```

---

## Host integration

Not period hardware — a board that bridges to your host.

## `hostbridge` — ours, not a period card

**This card never existed.** It is the one thing in the machine that is not history, and the manual
says so plainly rather than letting you discover it in a museum catalogue.

It moves **files between the guest and your host**, in both directions, and it is **sandboxed**: the
guest sees one directory you choose and **cannot escape it**. Not by `..`, not by an absolute
path, not at all. That is a hard requirement, not a setting with a default.

Default port `B0`. **Nothing of the utilities is in the board.** `R`, `W` and `HDIR` are
ordinary CP/M `.COM` programs — they live on a disk, they run at the `A>` prompt, and they
talk to the board through its two ports exactly as any other CP/M program would. What is
unusual is only where they came from: they were assembled *inside the machine*, by the
machine's own assembler, from 8080 source that ships with it. So they are readable code
rather than a magic trick the simulator performs on your behalf.

The file-transfer chapter is where this board is explained. It is the fastest way to get your own
code into CP/M, and it beats XMODEM by a distance — but XMODEM works too, over an ordinary serial
line, exactly as it did.

---

## PROM programmers

A board here does not run software — it *burns* it. You load a period EPROM-programmer routine,
point it at the board, and the bytes it "programs" pile up in a socket you can then save to a file.

## `pb1` — SSM PB1

The **SSM PB1** (Solid State Music) is a 2708/2716 EPROM programmer. It puts a **4K window** in
memory (default `D000`) for its two sockets — **U22** holds a 2708 (1K), **U23** a 2716 (2K) — and
one **control port** (default `10`). The burn is done by *software*: an `OUT` to the control port
arms the board and picks the chip (write `01` for the 2708, `02` for the 2716), and from then on
every byte the guest **writes into the window** is programmed into the socketed chip. A **read** of
the window disarms the board again — which is exactly how the period routines finish and turn the
LED off.

Two things follow from "programming is a write, disarming is a read", and they are worth knowing
before you chase a bug:

- **A socket starts erased** — every byte `FF` — and **programming can only clear bits to 0**, never
  set them, just like a real EPROM. So burn a *blank* chip; a second burn over the first only ANDs.
- The burn lives in the board, not on the host. To keep it, **`SAVE` it to a file** (below). A
  power-cycle re-reads the sockets from their mounts, so an unsaved burn is gone.

### Burning an EPROM, and making a hex file

This is the whole point of the board (and the answer to "do I need a burner board, or is a bus
write enough?" — you need the board, because you want to run the *software*). The example under
`examples/pb1` is a stock Altair with a `pb1` in it, plus SSM's own burner routines from the PB1
manual: `PROG2708.HEX` (a 2708) and `PROG2716.HEX` (a 2716). Each copies bytes from `4000` into
the socket. So put the data you want to burn at `4000`, run the burner, and save the result:

```
cd examples/pb1
altairsim pb1.toml
FILL 4000-43FF E5                ; the 1K you want to burn (or LOAD your own data there)
LOAD PROG2708.HEX                ; the SSM 2708 burner
BREAK F021                       ; where the burner "returns to the monitor"
RUN 100                          ; run it: it arms the board and burns the 2708
SAVE eprom.hex D000-D3FF         ; the burned chip, as an Intel HEX file on your host
```

The `BREAK F021` is there because SSM's routine ends by jumping to its own system monitor, which
this machine does not carry — the breakpoint catches that jump once the burn is done. `SAVE` then
reads the socket window back off the bus and writes it out — the socket reads like any other memory
once it is programmed — so `eprom.hex` is an ordinary Intel HEX image of the chip you burned, ready
to hand to another tool or `LOAD` back later. Your own burner works the same way: put your data in
RAM, `OUT 10,01` (or `,02` for a 2716), write it into the window, and `SAVE`.

The board also has an optional **on-board EPROM area** (the real card's U11–U14): read-only chips
you mount above `8000` with `[[board.prom]]` (`at` + `mount`), handy as the *source* for copying one
EPROM to another.

---

## Working with the backplane at the prompt

Everything a machine file can do to a board, you can do by hand.

| Command | |
|---|---|
| `BOARDS` | what is in the backplane |
| `SHOW BOARDS` | the board types you can add |
| `SHOW BOARD <type>` | one type's description and its settings (add `UNITS` for just the units) |
| `BOARDS ADD <type> <id>` | fit a board |
| `BOARDS REMOVE <id>` | pull one out |
| `SHOW <id>` | one installed board's settings, with the legal values |
| `SET <id> <key>=<value>` | change one |

```
altairsim> BOARDS ADD virtc vi0
altairsim> SET vi0 rtc_source=line
altairsim> SHOW vi0
```

**The keys are the same keys.** `SET cpu0 clock_hz=2000000` at the prompt and `clock_hz = 2000000`
in a machine file are the same property reached two ways — there is no separate config schema, which
is the whole reason the board reference at the back can be exhaustive.

And when you have the machine you want:

```
altairsim> CONFIG SAVE mine.toml
```

writes it out, and it round-trips.

### Looking a board type over before you fit it

`SHOW BOARD <type>` reads the *catalog*. It builds one of that board, describes it, lists
its settings, and throws it away — nothing is added to the backplane. A disk controller,
for instance, ends by naming the units it carries:

```
altairsim> SHOW BOARD dcdd
  dcdd  MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives...
  ...
  This board has units: drive
  SHOW BOARD dcdd UNITS for their properties.
```

Add `UNITS` and you get just the units — each under a heading that names its **kind** and,
after it, the **verb that fills it**:

```
altairsim> SHOW BOARD sol units

  Unit 'serial'  (serial, CONNECT)
  connect    The endpoint on the other end of this line (CONNECT sets this)
  ...

  Unit 'tape1'  (tape, MOUNT)
  mode       Which way the bytes go: play loads from the file, record saves to it
  ...
```

A **serial** unit is an endpoint, so you `CONNECT` it (to `console`, a socket, a real
port). A **disk**, **rom** or **tape** unit holds an image, so you `MOUNT` a file into it.
A **cpu** unit is soldered on — neither — and shows only its kind. The heading tells you
which verb a unit takes without your having to fit the board and find out.

Where a unit is filled from a *machine file* rather than by hand, `UNITS` shows the TOML
table and its keys instead — `[[board.drive]]` for a disk controller, with its `mount`,
`readonly` and `media` keys — so the file form is as discoverable as the prompt form.

## Contention

Two boards decoding the same port is **contention**, and it is a real thing that real backplanes
did. Both boards answer, both drive the data bus, and what the processor reads is neither.

The simulator does not stop you. It does something more useful — **it tells you**:

```
altairsim> SHOW BUS CONTENTION
```

which names the port and names both boards. Fit an `mds` in a machine that already has a `dcdd` and
this is what you will see, and it is a great deal more helpful than a guest that has mysteriously
gone mad.

Being able to build a machine that does not work is not a defect. **It is the point** — this is a
bench for developing hardware, and hardware that cannot be wired up wrong cannot be wired up at all.
