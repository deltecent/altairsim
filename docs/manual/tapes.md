# Tapes

Before the floppy, there was a **cassette recorder**. Not a special one — a domestic audio
cassette deck from a department store, with a microphone jack and an earphone jack, and MITS
sold you a card that turned bytes into a noise it could record and turned the noise back into
bytes. That card is the **88-ACR**, and it is in this simulator.

This chapter is how you load {{NAME_BASIC}} the way it was actually loaded in 1976: nothing in
ROM, nothing on a disk, a bootstrap you enter by hand, and a tape.

## The card

`acr` is the **MITS 88-ACR** — an Audio Cassette Record/playback interface.

Underneath, it is **an 88-SIO channel B with an FSK modem soldered to it**. That is not a
metaphor; that is what MITS built. The guest talks to a perfectly ordinary serial port, and
the modem on the other side of it turns the bits into tones. Default port **06** (`0x06`
status/control, `0x07` data). **300 baud**, which is the tape's speed, not a setting you
picked.

It has one unit: **`tape`**. There is one slot in a cassette recorder.

### `mode = play | record`

```
altairsim> SET acr0:tape mode=record
```

Play and record are **mutually exclusive**, and not because it was easier to write that way.
It is **one head and one physical button**. You cannot press PLAY and RECORD at the same time
on a cassette recorder, so you cannot do it here.

### You cannot `CONNECT` it to anything

The ACR takes no endpoint. It cannot be wired to your terminal, to a socket, or to a real
serial port, and the reason is the same one: **the line is soldered to the modem.** There is
no connector on the back of an 88-ACR because there is nothing to connect — the signal goes
to a cassette deck and nowhere else. **It is a cassette interface, not a serial port**, and
the fact that it is built out of a serial port does not make it one. The serial chapter is
about the cards that *do* take endpoints.

## The tape is not hardware

**The tape is NOT in the machine file, deliberately.**

A machine file describes the **hardware** — what cards are in the backplane, what they decode,
how much memory is on the bus. Which cassette is sitting in the recorder is not hardware. It
is a thing you did with your hands, this morning, and you can do a different thing with your
hands this afternoon without unscrewing the lid.

There is also a mechanical reason, and it is the honest one: **the card has no motor
control.** An 88-ACR cannot start the tape, stop it, or rewind it. It can only listen to
whatever is going past the head. The machine genuinely does not know a tape is there. So the
simulator does not pretend that it does.

**You put the tape in, and you press PLAY.** That is `MOUNT`, and it is a thing you type.

## Putting a tape in

```
altairsim> MOUNT acr0:tape "{{TAPE_BASIC}}"
```

The ACR has one unit, so the unit carries no information, so you may drop it:

```
altairsim> MOUNT ACR tape.bin
```

Names are case-blind. The rules are the ones in the disks chapter, and they are the same rules.

## `REWIND`

The ACR brings a verb of its own:

```
REWIND <id>:tape
```

`REW` will do. It winds the cassette back to the beginning.

**You need it to load the same tape twice.** A tape that has been read is a tape whose head is
at the end of the tape, and playing it again gets you silence. This surprises people exactly
once.

```
altairsim> REW acr0:tape
```

## Loading {{NAME_BASIC}} — the three-step ritual

This is the whole point of the chapter. It is three commands, and it is what an Altair owner
did every single time they wanted to use BASIC, because there was nowhere to keep it.

**1. Put the cassette in.**

```
altairsim> MOUNT acr0:tape "{{TAPE_BASIC}}"
```

**2. Toggle in the bootstrap.**

```
altairsim> LOAD "{{LOADER_BASIC}}"
```

About twenty bytes. **On a real Altair you entered this by hand on the front-panel switches**
— eight switches, one byte, DEPOSIT, again, twenty times — and if you got a bit wrong you
found out by the tape not loading. MITS printed the listing in the manual and expected you to
type it in with your fingers. `LOAD` is doing exactly that job and no more: it is not a
loader, it is a pair of hands.

**3. Run it from address zero.**

```
altairsim> RUN 0
```

The bootstrap starts the ACR reading, the tones come off the tape, and BASIC lands in memory
and starts itself.

Then BASIC asks you the three questions:

```
MEMORY SIZE?
TERMINAL WIDTH?
WANT SIN? Y

 742 BYTES FREE

ALTAIR BASIC VERSION 3.1
[FOUR-K VERSION]
OK
```

Empty answers to the first two mean "all of it" and "the default". `WANT SIN?` is asking
whether you would like to spend some of your 4K on trigonometry. Say `Y`; you have 742 bytes
left and a working `SIN`.

You are in Altair BASIC. It is 1976, and this is the first product Microsoft ever sold.

To do the whole thing in one command, the package ships the machine that does it for you:

```
$ altairsim {{MACHINE_BASIC}}
```

That machine file types those three lines on your behalf. It gets no special powers — every
line in it is a line you could have typed.

## The sense switches matter

The machine file sets **`sense = 0x80`** on the front panel, and it is not decoration.

The bootstrap reads the sense switches to find out **what device to load from**. Its own
printed header says: *"Set A15 on (cassette load), all other switches off."* A15 on, and
nothing else, is `0x80`. Get it wrong and BASIC dutifully loads from **the wrong device**, and
then sits there forever waiting for bytes that are never coming, because you told it to listen
to a teletype that isn't there.

If a tape load hangs and you are sure the tape is mounted, **check the sense switches first.**
The front-panel chapter says where they live.

## Speed, and what the crystal actually buys you

**The machine runs flat out by default.** A cassette that took a real Altair about **110
seconds** to load comes off the tape in **about one**.

You can have the 110 seconds back:

```
altairsim> SET cpu0 clock_hz=2000000
```

That is the real 2 MHz Altair, and the tape now takes as long as a tape took.

Here is the part worth understanding. **What the guest sees is identical either way.** The
tape costs the same number of T-states whichever way you run it — the ACR is clocked in
T-states, not in wall-clock seconds, and 300 baud means *this many T-states per bit* no matter
how fast those T-states are going past. The bytes arrive in the same order with the same gaps
between them, measured in the only clock the guest has.

So **the crystal buys you period *feel*, not period *behaviour*.** If you want to watch the
tape load at the speed your uncle watched it load, set it. If you want your BASIC prompt, do
not. Nothing inside the machine can tell the difference, and no program from the period ever
could.

## {{NAME_DISKBASIC}}

> **Not yet in the package.** {{MACHINE_DISKBASIC}} — {{NAME_DISKBASIC}}, the version that
> lives on a floppy and has files and `SAVE` — is **TBD**. It is not shipped with this
> release. Where the rest of this manual mentions it, it is describing something that is
> coming, not something you have.

The cassette BASIC in the package is the real thing, and it is the one that shows you what an
Altair actually was: a box with no operating system, no storage, and no software in it, which
you talked into existence one toggled byte at a time.
