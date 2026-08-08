# Serial ports, sockets and telnet

The Altair had no screen. What it had was a serial card, and whatever you chose to hang off
it — a Teletype, a glass terminal, a modem, a paper tape reader. The card did not know or
care. It moved characters.

`altairsim` keeps that arrangement exactly. **Any board that moves characters has one or
more UNITS, and every unit can be CONNECTed to an ENDPOINT.** The unit is the socket on the
back of the board. The endpoint is what you plugged into it.

```
altairsim> CONNECT sio0:b socket:2323
altairsim> DISCONNECT sio0:b
```

That is the whole of the interface. The interesting part is the endpoint grammar, and it is
short enough to print in full.

## The endpoints

This table is exhaustive. There are no others.

| Endpoint | Is |
|---|---|
| `console` | the host terminal — your keyboard and your screen. |
| `null` | nowhere. Writes vanish. Reads never come. |
| `loopback` | itself. What the guest writes comes straight back as a read. |
| `socket:PORT` | **LISTENS** on that TCP port. This is the telnet-in case. |
| `socket:HOST:PORT` | **CALLS OUT** to that host and that port. |
| `serial:DEVICE` | a real serial port on this host. |
| `in:PATH` | a host file, read-only — a **paper-tape reader**. The file's bytes feed the board. |
| `out:PATH` | a host file — a **paper-tape punch**. Whatever the board sends is written to it. |
| `in:PATH,out:PATH` | both at once on one line: a reader and a punch, two files, two positions. |
| `terminal` | a terminal in a window the simulator draws itself — a built-in VT100 (or ADM-3A, VT52, H19). Present only in a build with a display. See *A terminal in its own window*, below. |
| `printer:QUEUE` | a real print queue on this host, write-only. Buffers the bytes into a job and prints it. Present only where the build found a host print system. |
| `scripted` | a terminal with a caller in place of a human. No tty need exist. It is what the MCP tools and the test suite type into; you are unlikely to type it yourself. |

Any of these can be **tapped**: append `|FILE` to log the line to a hex file as it runs. That
is a modifier on an endpoint, not an endpoint of its own — see *Tapping a line to a log file*,
below.

### `null` is not an error

An unconnected unit is `null`, and **that is a legitimate state, not a fault.** A 6850 with
no cable in it sits there with its transmit register permanently empty, forever ready, and a
program that writes to it runs perfectly and talks to nobody. Reads never complete because
nothing is sending.

Which is precisely what the real card does with no cable in it. A machine with a second
serial port nobody plugged anything into is not a broken machine. `null` models the missing
cable, and the guest is entitled to be fooled by it exactly as it would have been in 1977.

### The colon is the whole distinction

`socket:2323` **listens.** `socket:localhost:2323` **calls out.** One colon, and it is the
same convention every terminal program has used for forty years: a bare port is a port you
own, a host and a port is a place you go. Nothing else about the endpoint changes.

## Telnetting into the guest

Wire a unit to a listening socket, and the guest has a serial port with a terminal on the
end of it. That the terminal is your telnet client, several processes away, is not something
the guest can discover.

```
altairsim> CONNECT sio0:b socket:2323
altairsim> RUN
```

Then, from another terminal on your machine:

```
$ telnet localhost 2323
```

The guest is now talking to that window. Your first terminal still has the monitor and
`^E` in it. This is how you give a machine two terminals, and it is how you drive a program
that wants a console that is not the one you are sitting at.

## A terminal in its own window

The telnet route above works, but it has moving parts you do not own: a telnet client must be
installed, it must be pointed at the right port, and it mangles any byte it thinks is a telnet
command — which breaks cursor sequences and file transfers, because to a serial line every
byte is just data. And a modern host terminal emulates a modern terminal, not the ADM-3A or
VT52 the 1970s software on the disk was written for.

`terminal` removes all of that. It is a terminal the simulator draws itself, in its own
window:

```
altairsim> CONNECT sio0:a terminal
altairsim> RUN
```

The guest's console is now that window; the monitor and `^E` stay in the one you launched
from. There is nothing to install and nothing to connect — the window is up the moment the
line is, on every platform. Because the terminal is the simulator's, the emulation is exactly
the one you ask for, and it answers the reports (like `ESC[6n`) a period program expects.

Bare `terminal` is a VT100 in an 80×24 window. Options ride the connect string after a `?`,
joined by `&`, the same as any other endpoint:

```
altairsim> CONNECT sio0:a terminal?emulation=adm3a
altairsim> CONNECT sio0:a terminal?emulation=vt52&size=80x24
altairsim> CONNECT sio0:a "terminal?emulation=h19&size=132x24"
```

`emulation` is one of `vt100` (the default; `ansi` is the same engine), `adm3a` (the Lear
Siegler ADM-3A, the archetypal CP/M terminal), `vt52`, or `h19` (the Heath/Zenith H19, a VT52
superset with an ANSI mode). `size` is *columns*×*rows* and defaults to `80x24`.

A `terminal` needs a window, so it is available only in a build with a display. Ask for one in
a build without, and it is refused cleanly at `CONNECT`, with the reason named; use `console`
or a `socket:` there instead.

## Calling out

```
altairsim> CONNECT sio0:b socket:bbs.example.com:23
```

The guest dials. As far as the software inside the machine is concerned it has a modem and
the modem is connected; it will happily run a period terminal program over it.

## A real serial port

```
altairsim> CONNECT sio0:b serial:/dev/tty.usbserial-A600K1XY
altairsim> CONNECT sio0:b serial:COM3
```

The second form is Windows. The bytes go out of a real UART, down a real wire, into whatever
you have on the other end.

**If you get the device name wrong, it lists the ports that actually exist on your machine.**
It does not merely say "cannot open". A cable that enumerated under a name one character off
from the one you expected is ten minutes of somebody quietly deciding the simulator is
broken, and the fix is to print the answer instead of the complaint.

### What the board does to the wire

The host port is opened at 9600 8N1, and then **immediately re-programmed by the board** —
because the board is the only thing in the system that knows what frame it is carrying. What it
re-programs the port *from* depends on the board, and the difference is real hardware, not a
house style:

- On an **88-SIO** or an **88-ACR** the word format is a set of **jumpers**, so it is the
  board's `baud`, `data_bits`, `stop_bits` and `parity` properties that become the frame on the
  wire.
- On an **88-2SIO** — the board in the example above — there are **no such properties, and there
  must not be**: the 6850's word format is a *register the guest writes*. A property would be a
  second place to say one thing, and the two would disagree the moment software touched the chip.
  So the frame on the wire is whatever the **guest** last programmed, and a guest that selects
  7E1 reconfigures the cable to 7E1.

So if you want 300 baud, 7 bits, even parity going out of that connector, you do not
configure it on the connector. You configure it on the **board**, where a 1975 operator would
have set it, with a jumper. Modem control lines — DCD, CTS, RTS — are wired through.

### And give the machine its real crystal before you transfer a file

```
altairsim> SET cpu0 clock_hz=2000000
```

The moment the wire leaves the machine, the guest is talking to something that keeps time the
way *you* do — and the guest does not. It counts instructions. `PCGET` spins a 49-T-state loop
to time a second, which is a second at 2 MHz and thirty milliseconds when the machine is running
flat out, so it will decide your sender is dead and give up before your sender has drawn breath.

Flat out is the right default for a machine talking to itself. **A machine talking to you wants
the crystal.** The troubleshooting chapter has the full story.

## A paper-tape reader and punch: `in:` and `out:`

A serial or parallel line is where a **paper-tape station** lived, and `altairsim` gives you one
out of two host files. The direction is the keyword, not a flag:

```
altairsim> CONNECT lpt0:prn out:printout.txt          # a punch: capture what the board sends
altairsim> CONNECT 4pio0:ja in:reader.tap             # a reader: feed a file to the board
altairsim> CONNECT 4pio0:ja in:reader.tap,out:punch.tap   # both, on one bidirectional line
```

`in:` is a **reader** — a byte *source*. It reads the file from the start, hands the bytes to the
board one at a time, and when the file runs out the line simply goes **quiet**: no error, no
end-of-file byte, exactly as a reader with no more tape sits idle. A file that is not there is a
clean refusal at `CONNECT`, with the path named.

`out:` is a **punch** — a byte *sink*. Whatever the board sends is written to the file. It does
**not** truncate: it overwrites forward from the start and extends past the old end, so a short run
into a longer old file leaves the old tail behind — the same as spooling fresh tape over a reel
that still had some on it. An absent file is created.

`in:` and `out:` are **separate files with separate positions**, so the combined form is two
independent heads on one line — reading the tape cannot disturb what the punch has written, and
vice versa. Both are **8-bit clean**: the bytes on the wire are the bytes in the file, control
codes and all. Nothing reformats them. A relative path follows the usual rule: relative to the file
when it is written in a machine configuration, relative to your shell when you type it.

### The 88-HSR — a reader with a speed

A real paper-tape reader has a rate, and a program that times its input cares. Pace an `in:` reader
with `?cps=N` (characters per second) or `?baud=N` (a line rate at 10 bits per character):

```
altairsim> CONNECT 4pio0:ja in:tape.tap?cps=300       # the 88-HSR high-speed reader
altairsim> CONNECT 4pio0:ja in:tape.tap?cps=30        # the slow reader
```

`in:tape.tap?cps=300` **is** the MITS 88-HSR. Give exactly one of `cps` or `baud`, and a positive
rate. With neither, the reader runs flat out — the generic default. The punch takes no options; it
writes at the line's own speed.

## Printing to a real printer

Where your build was made with host printing, a line can go to a real print queue instead of a
host file:

```
altairsim> CONNECT lpt0:prn printer:linewriter
```

`printer:` is a **write-only** sink like `out:`, and just as un-printer-specific — any line can
use it, not only the [88-C700](boards.md). The difference is what happens to the bytes: they are
held in a buffer and then handed to the host print system as one **job**.

**When does a job end?** A printer has no "done" signal — a program prints and then simply stops.
So `altairsim` decides the boundary for you, and you can tune it in the endpoint itself:

| Option | Means | Default |
|---|---|---|
| `?idle=N` | end the job after **N seconds** with nothing more printed. `0` = never. | `5` |
| `?onff` | also end the job on a **form feed** (the page-eject character). | off |
| `?max=N` | end the job at **N bytes**, so a runaway program cannot fill memory. | a large number |

```
altairsim> CONNECT lpt0:prn printer:linewriter?idle=15
altairsim> CONNECT lpt0:prn printer:linewriter?onff
altairsim> CONNECT lpt0:prn "printer:Generic / Text Only?idle=0&onff"
```

Write a bare option (`?onff`) to turn it on; the common case never types `=1`. The boundaries
combine — the first to fire ends the job — and an **empty buffer never prints**, so a form feed
followed by silence does not leave a blank page behind. A job also goes out when you `DISCONNECT`
the line, load another machine, or quit, so nothing you printed is ever left un-sent.

The queue must be one the host passes through **untouched** (a *raw* queue): a printer control
language is not text, and a normal queue would try to reformat it. Creating that queue is a
one-time step in your operating system's printer setup, outside `altairsim`. If a queue name
contains spaces, quote the whole endpoint as shown above. Connect to `printer:` with no name and
`altairsim` lists the queues it can see.

Like `out:`, a printer line is **8-bit clean** — the bytes the program sent are the bytes the
printer gets.

## Tapping a line to a log file (a poor man's protocol analyzer)

When you are trying to work out what a guest and the far end are actually saying to each other,
you want to *see the bytes*. Append **`|FILE`** to any endpoint and `altairsim` writes every
byte that crosses the line — both directions — to a text file as it runs, in hex and ASCII, with
timestamps. The guest cannot tell the tap is there, and it never changes a byte, so it is safe on
a binary transfer as well as on a terminal.

```
altairsim> CONNECT sio0:b socket:2323|bbs.hex
```

Telnet in, drive the guest, and `bbs.hex` fills with the conversation:

```
# altairsim capture  socket:2323  2026-07-29 14:03:11  fmt=dump
+0.001000  TX 0000  41 54 5A 0D                                       ATZ.
+0.048213  RX 0000  0D 0A 4F 4B 0D 0A                                 ..OK..
+9.100000  [DCD^]
+45.30000  [DTR_]
```

`TX` is what the guest sent, `RX` what came back; the offset counts bytes within a burst, and the
right-hand column is the ASCII (a `.` for anything unprintable). Lines in `[...]` are **modem
control-line** edges — carrier, DTR, RTS and the rest — logged as they change, with `^` for a
rising edge and `_` for a falling one, so you can see the far end pick up and hang up.

The file is truncated each time you connect: a capture is a fresh trace, not an append. The whole
tap is remembered — `SHOW` prints it and `CONFIG SAVE` writes it back — so a machine file can carry
a line that is permanently traced.

### Three layouts, and a few knobs

The tap's options ride the same `?key=value` grammar the other endpoints use, after the file name:

```
altairsim> CONNECT sio0:b socket:2323|bbs.hex?fmt=cols
altairsim> CONNECT sio0:b in:reader.tap?cps=300|trace.log?fmt=jsonl
```

- **`fmt=dump`** (the default) is the layout above: one hex row per line, strictly in time order —
  the easy one to read, and the easy one to `grep`.
- **`fmt=cols`** puts what the guest sent on the **left** and what it received on the **right**, so a
  request and its reply read down the page like a transcript.
- **`fmt=jsonl`** writes one JSON record per transfer — for feeding another program, diffing two
  runs, or importing into a spreadsheet.

The rest, all optional: `ts=elapsed` (the default — seconds since the first byte), `ts=wall` (the
host clock), or `ts=none`; `width=N` bytes per hex row (default 16); `gap=MS` for how long a quiet
line waits before a partial row is flushed (default 200 ms); and `pins=off` to leave the modem
control-line edges out. Note that the second example taps a **paced paper-tape reader** — the tap
composes with any endpoint, options and all.

## A `CONNECT` it does not understand is an error

If `altairsim` cannot make sense of your endpoint, it **refuses, and tells you what it could
have meant.** It never quietly falls back to `null`.

This matters more than it sounds like it does. A silent fallback gives you a machine that
boots, runs, prints nothing, and hands you a dead terminal to debug — and you will debug the
guest, and the board, and the disk, before you think to doubt the thing you typed. A refusal
is a worse morning for exactly two seconds. A dead terminal is a worse afternoon.

## Exactly one unit may hold the console

The console is **your keyboard**, and there is one of it.

```
altairsim> CONNECT sio1:a console
console taken from sio0:a
sio1:a: connected to console
```

Connecting a second unit to `console` **steals it, and says who it took it from.** It is not
an error and it is not shared. Two boards reading one keyboard would each get roughly half
the characters, in an order neither of them could predict, and the resulting machine would
appear to be haunted.

To see who has it:

```
altairsim> SHOW CONSOLE
```

That also shows the transforms, which is the rest of this chapter.

## The transform chain belongs to the console, and only the console

This is the most important rule in the chapter, and it is worth stating twice before
explaining it.

**The `[console]` settings are the only thing in the simulator that alters a byte. Every
serial LINE is 8-bit clean. There is no knob anywhere on any board that masks a bit.**

They belong to the console, so they stop where the console does. Send a board's console unit
down a `socket:` or a real `serial:` port and the far end gets the bytes as the guest wrote
them — see *Where the transforms stop*, below, because it is the same rule and it surprises
people.

| Setting | Does |
|---|---|
| `upper` | folds what you type to upper case |
| `strip7in` | clears bit 7 of every character you type |
| `strip7out` | clears bit 7 of every character the guest prints |
| `crlf` | translates line endings |
| `echo` | echoes your keystrokes locally |
| `bell` | rings the terminal bell on `^G` |
| `bsdel` | folds backspace and delete together: `off` (default), `bs` (send BS for both), or `del` (send DEL for both) |
| `attn` | which control character is ATTN (default `^E`) |
| `base` | `hex` or `octal` — the base the **monitor** prints numbers in. Not a transform: it changes nothing about a byte crossing the console, only how a number is spelled back to you. The monitor chapter has it. |

Set them with `CONSOLE k=v`. (`SET CONSOLE k=v` is the same thing said longer.)

```
altairsim> CONSOLE strip7out=on
altairsim> CONSOLE upper=on crlf=off
altairsim> CONSOLE attn=1D
```

`attn=1D` moves the escape key from `^E` to `^]`. **It must be a control character** — an
ATTN you can type by accident in the middle of a sentence is not an escape key, it is a
trap.

### Why it works this way: `MEMORY SIZ?`

Boot MITS BASIC with the transforms off and it asks you:

```
MEMORY SIZ?
```

The `E` is missing, and there is a garbage character where it should be. BASIC is not
broken. **MITS BASIC sets bit 7 of the last character of every message**, as a string
terminator — that is how it knows where a string ends — and it sends it. `E` is `45`; with
bit 7 set it is `C5`, and your terminal prints whatever `C5` happens to mean to it.

The fix is `CONSOLE strip7out=on`, and the reason the fix lives *there* is the whole
argument:

**On the real machine, the card sent all eight bits.** Nothing masked anything. The card
put `C5` on the wire because that is the byte BASIC handed it. The Teletype on the other end
simply **did not look at bit 7** — on a Model 33, that is the parity position, and the
printing mechanism does not decode it. It printed `E` and threw the eighth bit on the floor.

Nothing was masked. Something on the far end did not look. **`strip7out` is the terminal not
looking.** It is a property of the thing you are sitting at, and it belongs on the thing you
are sitting at.

### Where the transforms stop

Follow that argument one step further and it tells you something the first time it happens is
alarming. If `strip7out` is your terminal not looking at bit 7, then the moment your terminal
is **not** the thing on the far end, there is nothing there to do the not-looking:

```
altairsim> CONSOLE strip7out=on
altairsim> CONNECT sio0:a socket:2323
sio0:a: connected to socket:2323
```

Telnet in, and `MEMORY SIZ?` is back — garbage character and all, exactly as though you had
never set `strip7out`. The same is true of `upper`, `crlf`, `echo`, `bell` and `bsdel`, and it
is true of a `serial:` port as well as a socket.

**Nothing has been undone.** The console settings are still on and still doing their job; the
byte simply no longer goes through the console. It goes 2SIO → socket → your telnet client,
and every hop on that path is 8-bit clean — which is the rule at the top of this section, not
an exception to it. A transform that *did* travel down the cable would be a filter on a line,
and the previous two sections are about why that is the one thing this simulator will not do.

So set the equivalent where it now belongs — on the terminal that is actually displaying the
text. Every terminal emulator and telnet client has these, under its own names: strip parity or
7-bit display for `strip7out`, local echo for `echo`, newline or CR/LF handling for `crlf`.
That is not a workaround; it is the same fix in the same place, one machine further out.

ATTN is the other half of the same fact: it is intercepted at **your keyboard**, before any
board is offered the byte, and it is never looked for on a socket or a serial line. A `05`
arriving down a cable is a byte of somebody's protocol, and scanning a modem line for a key
that exists only on the operator's terminal would corrupt data rather than help.

What *does* travel is the board's own line coding — baud, data bits, parity, stop bits — because
that belongs to the board and not to you. That is the section after next.

### Why not just strap the board to 7 bits

Because it would work, and then it would silently destroy your data.

Set a 7-bit mask on the board — or a filter on the line — and BASIC's prompt comes out clean.
It also **silently corrupts every XMODEM transfer through that port**, because XMODEM sends
binary, every byte of it matters, and bit 7 is a real bit in half of them. The file arrives.
The checksums even pass on a bad packet often enough to be maddening. And the corruption is
in the *plumbing*, which is the last place anyone looks.

**A line may carry binary. A terminal is not a line.** The transform belongs to the terminal
because only the terminal knows it is displaying text.

### `data_bits` and `parity` are real hardware, and are not this

A board genuinely does have `data_bits`, `stop_bits` and `parity`, and they are genuinely
configurable, because a 6850 genuinely has those straps. They are **a FRAME** — they describe
what physically travels down the wire, bit by bit, and on a real serial port they are what
the far end must agree to or it will read garbage.

They are never a mask. `data_bits=7` is not "and the byte with `7F`". It is "put seven data
bits in the frame", which is a statement about the wire, not about the byte the guest wrote.
Do not reach for it to fix a prompt.
