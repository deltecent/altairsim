# Boards

An Altair is a **backplane**. Everything else is a card in it — the memory, the serial ports, the
disk controller, the front panel, and the processor itself. There is no "machine" underneath the
cards doing the real work; take the cards out and there is nothing left but a bus.

`altairsim` is built that way on purpose, and it is the reason the CPU's crystal is a property of
the CPU *card* and the sense switches are a property of the *front panel*. Not pedantry: it is what
lets you pull a card out, put a different one in, and find out what the software does about it.

This chapter says what the ten cards **are** — what the real hardware was, what it is for, and
what will bite you. **It does not list their parameters.** Every key of every card is in the board
reference at the back of this manual, printed from the program's own tables, which is why it cannot
be wrong.

## The ten cards

| Type | What it is |
|---|---|
| `memory` | RAM and ROM, as a list of regions |
| `8080` | the MITS 88-CPU |
| `2sio` | MITS 88-2SIO — two serial ports. The usual console |
| `sio` | MITS 88-SIO — one serial port. MITS's first |
| `acr` | MITS 88-ACR — the cassette interface |
| `dcdd` | MITS 88-DCDD — the 8″ floppy controller |
| `mds` | MITS 88-MDS — the 5¼″ minidisk controller |
| `virtc` | MITS 88-VI/RTC — vectored interrupts and a clock |
| `fp` | the front panel |
| `hostbridge` | file transfer to your host. **Ours, not a period card** |

---

## `memory` — RAM and ROM

A memory card is **a list of regions**, and the regions are the card. That is not a modelling
convenience; it is what an S-100 memory board was. One physical card carried banks of chips
decoding whatever ranges its jumpers said, and a card with 56K of RAM low and a 256-byte boot PROM
at `FF00` is a perfectly ordinary card.

So `default` has exactly one memory board in it, and that board is the 56K *and* the PROM.

### `PHANTOM*` — how a boot PROM gets out of the way

The bus has a line called `PHANTOM*`. **A board pulls it to switch another board off.** When the
PROM at `FF00` is being read, it asserts `PHANTOM*`, and the RAM card underneath — if it is
jumpered to honour it — shuts up. Two cards decode `FF00`; only one answers.

This is how a disk Altair boots. The PROM overlays the RAM at the top of memory, the loader runs
out of it, and then **the loader gets out of the way** and the RAM underneath is uncovered — which
matters, because CP/M wants that memory back.

Whether a card honours `PHANTOM*`, and whether it asserts it, are **jumpers**. They are on the card
and they are yours to set. Getting them wrong produces a machine that does not boot and does not
say why — which is precisely what it did in 1977, and the bus view in the monitor will show you
both cards claiming the page.

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

**The processor is a card like any other.** It plugs into the backplane, it can be removed, and
with `-n` you can build a machine that does not have one.

It decodes no ports and answers no addresses. What it does is **drive the bus** — which makes it
unlike every other card in the box, and is exactly what the 88-CPU did.

### The crystal is on the card

Which is why **`clock_hz` is this card's property and not the machine's** — and why writing it in
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

### `idle` — the CPU stands down at a prompt

A guest sitting at a prompt is not doing anything. It is spinning on the serial card's status
register, waiting for a byte that has not arrived, and at `clock_hz = 0` it will spin as fast as
your CPU can let it — one core, pinned, indefinitely, to accomplish nothing.

**`idle` (on by default) stands the processor down when the guest is only polling an empty
keyboard.** A hundred percent of a core becomes about three and a half.

**The guest cannot tell.** Not "the guest probably won't notice" — it *cannot tell*, because the
moment a byte arrives the processor is back before the guest's next poll could have seen anything
different. An XMODEM transfer through an idling machine is byte-exact.

---

## `2sio` — MITS 88-2SIO

Two **6850 ACIAs**, units `a` and `b`, four ports at BASE+0 through BASE+3. Base defaults to `10`
hex, which is where every listing from the period expects it.

This is **the usual console card**, and it is what `default` has. If you are running CP/M or
Microsoft BASIC, this is the card the software is talking to.

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

This is the card that shows you what an Altair actually was: no disk, no PROM, a bootstrap you
toggle in by hand, and eight minutes of listening to a cassette. **The tapes chapter is the one to
read**, and {{NAME_BASIC}} is the machine to run.

---

## `dcdd` — MITS 88-DCDD

The **8″ hard-sector floppy controller**, up to sixteen drives, three ports at `08`, `09` and `0A`.
**This is the card CP/M booted from**, and it is in `default`.

It also carries the **8 MB medium** — a large-capacity format the same controller can address.

Its status bits are **inverted**, for the same reason the 88-SIO's are and with the same
consequence: a clear bit means ready.

The disks chapter is where this card lives: formats, mounting, write protection, and the track-buffer
trap that means you should get back to the `A>` prompt before you stop the machine.

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

### It cannot share a machine with a `dcdd`

**Same three ports.** Two controllers decoding `08` is not a limitation of this program; it is the
MITS address map, and a real Altair with both cards in it would have had them fighting on the data
bus. Fit both here and the bus view will name the contention rather than leave you wondering why
the guest has gone strange.

Pick one.

---

## `virtc` — MITS 88-VI/RTC

Two things on one card, which is why it has an awkward name.

**Vectored interrupts.** Eight lines, **VI0 through VI7**. A device is strapped to a line; when it
interrupts, **level *n* becomes `RST n`** — the processor jumps to `8×n` and the right handler runs
without anybody having to poll anything. **VI0 is the highest priority**, and the card enforces
that: a lower level cannot interrupt a higher one that is being serviced.

This is what turns a machine that busy-waits into a machine that gets on with something else. Every
card with an interrupt strap in its properties — the serial cards, the floppy controllers — is
strapped to one of these lines, or to `int`, or to nothing at all.

**A real-time clock**, on the same board: a periodic interrupt off the 60 Hz line or off the system
clock, divided down.

One port at `FE`, and it is **write-only**. There is nothing to read back. Interrupt cards are the
easiest thing in a machine to get subtly wrong, and this one is worth reading the reference for
before you strap anything to it.

`ps2int` is the machine that shows it working.

---

## `fp` — the front panel

The switches and the lamps. The panel is **a card**, because on a real Altair it was one — it
plugged into the bus like everything else, and a machine without it is a machine you cannot toggle
a bootstrap into.

### The SENSE switches, at port `FF`

The eight switches on the left of the address bank, `SA8`–`SA15`. **`IN FFH` reads them.** They are
**read-only**: an `OUT FF` is not this card's business and goes nowhere at all.

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

## `hostbridge` — ours, not a period card

**This card never existed.** It is the one thing in the machine that is not history, and the manual
says so plainly rather than letting you discover it in a museum catalogue.

It moves **files between the guest and your host**, in both directions, and it is **sandboxed**: the
guest sees one directory you nominate and **cannot escape it**. Not by `..`, not by an absolute
path, not at all. That is a hard requirement, not a setting with a default.

Default port `B0`. The guest-side utilities — `R`, `W` and `HDIR` — are assembled *inside the
machine*, which means they are 8080 code like everything else and you can read them.

The file-transfer chapter is where this card is explained. It is the fastest way to get your own
code into CP/M, and it beats XMODEM by a distance — but XMODEM works too, over an ordinary serial
line, exactly as it did.

---

## Working with the backplane at the prompt

Everything a machine file can do to a card, you can do by hand.

| Command | |
|---|---|
| `BOARDS` | what is in the backplane |
| `BOARDS TYPES` | what you can add |
| `BOARDS ADD <type> <id>` | fit a card |
| `BOARDS REMOVE <id>` | pull one out |
| `SHOW <id>` | one card's settings, with the legal values |
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

Two cards decoding the same port is **contention**, and it is a real thing that real backplanes did.
Both cards answer, both drive the data bus, and what the processor reads is neither.

The simulator does not stop you. It does something more useful — **it tells you**:

```
altairsim> SHOW BUS CONTENTION
```

which names the port and names both cards. Fit an `mds` in a machine that already has a `dcdd` and
this is what you will see, and it is a great deal more helpful than a guest that has mysteriously
gone mad.

Being able to build a machine that does not work is not a defect. **It is the point** — this is a
bench for developing hardware, and hardware that cannot be wired up wrong cannot be wired up at all.
