# Boards

An Altair is a **backplane**. Everything else is a board in it — the memory, the serial ports, the
disk controller, the front panel, and the processor itself. There is no "machine" underneath the
boards doing the real work; take the boards out and there is nothing left but a bus.

`altairsim` is built that way on purpose, and it is the reason the CPU's crystal is a property of
the CPU *board* and the sense switches are a property of the *front panel*. Not pedantry: it is what
lets you pull a board out, put a different one in, and find out what the software does about it.

This chapter says what the fourteen boards **are** — what the real hardware was, what it is for, and
what will bite you. **It does not list their parameters.** Every key of every board is in the board
reference at the back of this manual, printed from the program's own tables, which is why it cannot
be wrong.

## The fifteen boards

| Type | What it is |
|---|---|
| `memory` | RAM and ROM, as a list of regions |
| `8080` | the MITS 88-CPU |
| `z80` | a Z80 CPU board — the same bus, a different instruction set |
| `2sio` | MITS 88-2SIO — two serial ports. The usual console |
| `sio` | MITS 88-SIO — one serial port. MITS's first |
| `acr` | MITS 88-ACR — the cassette interface |
| `c700` | MITS 88-C700 — the line-printer controller. Capture to a file |
| `dcdd` | MITS 88-DCDD — the 8″ floppy controller |
| `mds` | MITS 88-MDS — the 5¼″ minidisk controller |
| `vdm1` | Processor Technology VDM-1 — memory-mapped video. Needs a display |
| `sol` | Processor Technology Sol-PC — the Sol-20's onboard I/O, on one card |
| `virtc` | MITS 88-VI/RTC — vectored interrupts and a clock |
| `fp` | the front panel |
| `turnkey` | MITS 8800b Turnkey Module — the front-panel-less Altair, on one card |
| `hostbridge` | file transfer to your host. **Ours, not a period card** |

---

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

This is how a disk Altair boots. The PROM overlays the RAM at the top of memory, the loader runs
out of it, and then **the loader gets out of the way** and the RAM underneath is uncovered — which
matters, because CP/M wants that memory back.

Whether a board honours `PHANTOM*`, and whether it asserts it, are **jumpers**. They are on the
board and they are yours to set. Getting them wrong produces a machine that does not boot and does
not say why — which is precisely what it did in 1977, and the bus view in the monitor will show you
both boards claiming the page.

### Banking

Sixty-four kilobytes was not enough for very long, and the industry's answer was **bank switching**:
several cards' worth of RAM at the same addresses, with a port that says which one is live. Nobody
agreed on how.

`altairsim` implements five of the real schemes — **ExpandoRAM**, **Vector Graphic**, **Cromemco**,
**North Star Horizon**, and **AB Digital B810** — and **no two are alike**. Different ports,
different bit meanings, different numbers of banks. That is not a failure of the simulator to
generalise; it is the actual history, and software written for one will not drive another.

If you are not running banked software, leave banking off. It is off by default.

---

## `8080` — the MITS 88-CPU

**The processor is a board like any other.** It plugs into the backplane, it can be removed, and
with `-n` you can build a machine that does not have one.

It decodes no ports and answers no addresses. What it does is **drive the bus** — which makes it
unlike every other board in the box, and is exactly what the 88-CPU did.

### The crystal is on the board

Which is why **`clock_hz` is this board's property and not the machine's** — and why writing it in
`[machine]` is an error with an explanation rather than a setting that quietly does nothing.

**`clock_hz = 0` is the default, and it means run flat out** — as fast as your host can go. On a
modern machine that is somewhere north of a hundred times a real Altair.

```
SET cpu0 clock_hz=2000000
```

buys back the real 2 MHz machine, and it is worth doing at least once. **What the guest *sees* is
identical either way.** Instructions cost the right number of T-states, a cassette takes the right
number of them to load, and the disk turns at the right speed regardless. The crystal buys period
***feel***, not period ***behaviour***. Watch BASIC print its banner at 2 MHz and you learn
something about 1976 that no amount of reading will teach you. Then set it back.

**With one boundary, and it is a real one: that guarantee ends at the edge of the machine.**
Everything *inside* keeps time by the same T-states, so it all agrees with itself at any speed.
But a guest program counts instructions to measure a second — `PCGET`'s timeout is a 49-T-state
loop spun 159 × 256 times — and flat out, your host retires its "three seconds" in a few tens of
milliseconds while the program at the other end of the wire is still using real ones. **Anything
the guest times against the outside world wants the real crystal**: XMODEM through a serial port
is the case you will meet. See the troubleshooting chapter.

### `idle` — the CPU stands down at a prompt

A guest sitting at a prompt is not doing anything. It is spinning on the serial board's status
register, waiting for a byte that has not arrived, and at `clock_hz = 0` it will spin as fast as
your CPU can let it — one core, pinned, indefinitely, to accomplish nothing.

**`idle` (on by default) stands the processor down when the guest is only polling an empty
keyboard.** A hundred percent of a core becomes about three and a half.

**The guest cannot tell.** Not "the guest probably won't notice" — it *cannot tell*, because the
moment a byte arrives the processor is back before the guest's next poll could have seen anything
different. An XMODEM transfer through an idling machine is byte-exact.

---

## `z80` — a Z80 CPU

**A second processor board.** It plugs into the same backplane as the 88-CPU, decodes nothing, and
drives the bus the same way — the only difference is the instruction set behind it. Put a `z80`
where an `8080` would go and the bus, the boards, and the debugger neither know nor care; that is
the whole point of keeping the processor a board.

It carries the same three properties as the 8080 — `clock_hz` (the crystal, flat out by default),
`idle` (stands down at a prompt), and the read-only `achieved_hz` — and each means exactly what it
means there.

The core is validated the same way the 8080 was, against the same kind of gate: ZEXDOC and ZEXALL,
the standard Z80 exercisers, both pass before a single board is built on top of it. The built-in
`z80` machine is a minimal one — a `z80`, 64K of RAM, and a 2SIO console — for putting it through
its paces.

---

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

## `acr` — MITS 88-ACR

The **cassette interface**: an 88-SIO channel B with an FSK modem on the end of it, so a byte on the
bus becomes an audible tone on a tape and back again. Unit `tape`, default port `06`, and it runs at
300 baud because that is what an audio cassette could carry.

It brings its own verb — **`REWIND`** — because a tape has a position and a disk does not, and
pretending otherwise would help nobody.

This is the board that shows you what an Altair actually was: no disk, no PROM, a bootstrap you
toggle in by hand, and eight minutes of listening to a cassette. **The tapes chapter is the one to
read**, and {{NAME_BASIC}} is the machine to run.

---

## `c700` — MITS 88-C700

The **line-printer controller**: an output-only board that sends characters to an Altair C700
printer. Unit `prn`, default port `02` — the MITS default, with Control/Status at `02` and Data
at `03`.

There is no printer in the box, so **`CONNECT` its `prn` line wherever you want the output**: a
file (`CONNECT lpt0:prn file:printout.txt`), the `console` to watch it print live, a `socket:`, or
a real `serial:` printer. The capture is byte-for-byte — the bytes the program sent, control codes
and all, not a reformatted page.

It is **polled**: write a character to the data port (`03`), then poll the status port (`02`, bit 0
ACKNOWLEDGE, set = ready) before the next. The real card's single-level interrupt is not modeled.

The **`lineprinter`** machine is `default` with one of these already fitted and capturing to a file.

---

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

### The window is live while a program runs

The display is serviced by the running machine, so **a stopped machine's window does not redraw
and does not answer its close button.** This surprises people in three places, and they are all
the same fact:

- **`vdm1`'s demo halts once it has drawn.** That is the point — it draws the banner and stops —
  but it means the window you are looking at belongs to a stopped machine. Its **close button will
  not work**, and there is no way to close it from the window itself. **`QUIT` at the monitor
  prompt closes it and exits**, which is the way out.
- **A change like `SET vdm0 video=reverse` does not appear** until you `RUN` again.
- **The cursor does not blink** while the machine is stopped. To watch it blink, use `sol20`,
  whose SOLOS sits in a loop rather than halting.

Closing the window of a **running** machine is not the same as quitting: it stops the guest and
gives you the monitor prompt, leaving the machine exactly where it was and the window on screen.
`RUN` goes back into it; `QUIT` exits.

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
| `MODE SELECT` | `80` | Return to the command mode, restarting the command line | — | Ctrl-@ |
| `CLEAR` | `8B` | Erase the screen, cursor home | — | Ctrl-K |
| `LOAD` | `8C` | Nothing — neither SOLOS nor CONSOL ever claimed it | — | — |

**The `Press` column works in the video window only.** Your keyboard's own arrows and Home send
these codes when the window has focus. They cannot work from a terminal: there an arrow key sends
an escape sequence rather than a single byte, and `ESC` is a character the guest legitimately
needs, so there is nothing to safely match on.

**`MODE SELECT` and `CLEAR` have no key yet** — a PC keyboard simply has nothing to put them on,
and choosing what to borrow is a decision that has not been made. Use the last column.

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

Fit it with a `vdm1` and you have the **`sol20`** machine, which cold-starts the real SOLOS
operating system.

---

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

---

## `fp` — the front panel

The switches and the lamps. The panel is **a board**, because on a real Altair it was one — it
plugged into the bus like everything else, and a machine without it is a machine you cannot toggle
a bootstrap into.

### The SENSE switches, at port `FF`

The eight switches on the left of the address bank, `SA8`–`SA15`. **`IN FFH` reads them.** They are
**read-only**: an `OUT FF` is not this board's business and goes nowhere at all.

**They are not decoration.** Period bootstraps read the sense switches to decide **what to boot
from** — which device, at which port, at which speed. That is why every tape machine in this package
sets one:

```
sense = 0x80        # basic4k: load from the 88-SIO at port 00
sense = 0x8E        # ps2:     the 2SIO, and interrupts off
```

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
`altairsim turnkey` is the machine; `examples/turnkey` boots CP/M on it off a floppy and off a
hard disk.

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
at    = FC00        # socket L1 — the hard-disk loader
mount = "builtin:hdbl"
```

---

## `hostbridge` — ours, not a period card

**This card never existed.** It is the one thing in the machine that is not history, and the manual
says so plainly rather than letting you discover it in a museum catalogue.

It moves **files between the guest and your host**, in both directions, and it is **sandboxed**: the
guest sees one directory you nominate and **cannot escape it**. Not by `..`, not by an absolute
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

## Working with the backplane at the prompt

Everything a machine file can do to a board, you can do by hand.

| Command | |
|---|---|
| `BOARDS` | what is in the backplane |
| `BOARDS TYPES` | what you can add |
| `BOARDS ADD <type> <id>` | fit a board |
| `BOARDS REMOVE <id>` | pull one out |
| `SHOW <id>` | one board's settings, with the legal values |
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
