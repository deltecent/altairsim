# Serial ports, sockets and telnet

The Altair had no screen. What it had was a serial card, and whatever you chose to hang off
it — a Teletype, a glass terminal, a modem, a paper tape reader. The card did not know or
care. It moved characters.

`altairsim` keeps that arrangement exactly. **Any board that moves characters has one or
more UNITS, and every unit can be CONNECTed to an ENDPOINT.** The unit is the socket on the
back of the card. The endpoint is what you plugged into it.

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

### What the card does to the wire

The host port is opened at 9600 8N1, and then **immediately re-programmed by the card** —
because the card is the only thing in the system that knows what it is strapped to. The
card's `baud`, `data_bits`, `stop_bits` and `parity` become the actual frame on the actual
wire.

So if you want 300 baud, 7 bits, even parity going out of that connector, you do not
configure it on the connector. You configure it on the **card**, where a 1975 operator would
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

## A `CONNECT` it does not understand is an error

If `altairsim` cannot make sense of your endpoint, it **refuses, and tells you what it could
have meant.** It never quietly falls back to `null`.

This matters more than it sounds like it does. A silent fallback gives you a machine that
boots, runs, prints nothing, and hands you a dead terminal to debug — and you will debug the
guest, and the card, and the disk, before you think to doubt the thing you typed. A refusal
is a worse morning for exactly two seconds. A dead terminal is a worse afternoon.

## Exactly one unit may hold the console

The console is **your keyboard**, and there is one of it.

```
altairsim> CONNECT sio1:a console
console: taken from sio0:a
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
serial LINE is 8-bit clean. There is no knob anywhere on any card that masks a bit.**

| Setting | Does |
|---|---|
| `upper` | folds what you type to upper case |
| `strip7in` | clears bit 7 of every character you type |
| `strip7out` | clears bit 7 of every character the guest prints |
| `crlf` | translates line endings |
| `echo` | echoes your keystrokes locally |
| `bell` | rings the terminal bell on `^G` |
| `bsdel` | swaps backspace and delete |
| `attn` | which control character is ATTN (default `^E`) |

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

### Why not just strap the card to 7 bits

Because it would work, and then it would silently destroy your data.

Set a 7-bit mask on the card — or a filter on the line — and BASIC's prompt comes out clean.
It also **silently corrupts every XMODEM transfer through that port**, because XMODEM sends
binary, every byte of it matters, and bit 7 is a real bit in half of them. The file arrives.
The checksums even pass on a bad packet often enough to be maddening. And the corruption is
in the *plumbing*, which is the last place anyone looks.

**A line may carry binary. A terminal is not a line.** The transform belongs to the terminal
because only the terminal knows it is displaying text.

### `data_bits` and `parity` are real hardware, and are not this

A card genuinely does have `data_bits`, `stop_bits` and `parity`, and they are genuinely
configurable, because a 6850 genuinely has those straps. They are **a FRAME** — they describe
what physically travels down the wire, bit by bit, and on a real serial port they are what
the far end must agree to or it will read garbage.

They are never a mask. `data_bits=7` is not "and the byte with `7F`". It is "put seven data
bits in the frame", which is a statement about the wire, not about the byte the guest wrote.
Do not reach for it to fix a prompt.
