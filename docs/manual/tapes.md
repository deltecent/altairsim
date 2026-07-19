# Tapes

Before the floppy, there was a **cassette recorder**. Not a special one — a domestic audio
cassette deck from a department store, with a microphone jack and an earphone jack, and MITS
sold you a card that turned bytes into a noise it could record and turned the noise back into
bytes. That card is the **88-ACR**, and it is in this simulator.

So is the other one. Two machines here have cassette interfaces, and they are not the same
hardware:

| | **88-ACR** | **Sol-20** |
|---|---|---|
| What it is | An S-100 card you plug into an Altair | Built into the Sol-PC motherboard |
| Board id | `acr` | part of `sol` |
| Units | one — `tape` | two — `tape1`, `tape2` |
| Modulation | `fsk300` (2400/1850 Hz) | `cuts1200`, `kcs300` (Kansas City, 2400/1200 Hz) |
| Speed | 300 baud, soldered | 300 or 1200, the *guest* picks |
| Motor control | none | yes, `OUT 0FAh` |

They are described one at a time below. Everything after them — mounting, rewinding, audio
files, recording — works the same way on both, and is written once.

The chapter ends with the thing this is all for: loading {{NAME_BASIC}} the way it was actually
loaded in 1976, with nothing in ROM, nothing on a disk, a bootstrap you enter by hand, and a
tape.

## The 88-ACR

`acr` is the **MITS 88-ACR** — an Audio Cassette Record/playback interface.

Underneath, it is **an 88-SIO channel B with an FSK modem soldered to it**. That is not a
metaphor; that is what MITS built. The guest talks to a perfectly ordinary serial port, and
the modem on the other side of it turns the bits into tones. Default port **06** (`0x06`
status/control, `0x07` data). **300 baud**, which is the tape's speed, not a setting you
picked.

It has one unit: **`tape`**. There is one slot in a cassette recorder.

### You cannot `CONNECT` it to anything

The ACR takes no endpoint. It cannot be wired to your terminal, to a socket, or to a real
serial port, and the reason is the same one: **the line is soldered to the modem.** There is
no connector on the back of an 88-ACR because there is nothing to connect — the signal goes
to a cassette deck and nowhere else. **It is a cassette interface, not a serial port**, and
the fact that it is built out of a serial port does not make it one. The serial chapter is
about the cards that *do* take endpoints.

### It cannot work the motor

**An 88-ACR cannot start the tape, stop it, or rewind it.** It can only listen to whatever is
going past the head. The machine genuinely does not know a tape is there, so the simulator
does not pretend that it does — which is why mounting is something you type, below.

## The Sol-20's cassette interface

The **Sol-20** has a CUTS cassette interface built into its motherboard rather than on a card
of its own, and it has **two decks**:

```
altairsim> MOUNT sol0:tape1 "mytape.tap"
altairsim> SET sol0:tape1 mode=record
altairsim> REW sol0:tape1
```

Two differences from the ACR are worth knowing, and both are the hardware talking.

**The Sol can work the motors.** `OUT 0FAh` starts and stops each transport, and SOLOS does it
for you — `SAVE` spins the deck up, writes, and spins it down. So a Sol tape plays only while
the guest is running it, and a deck whose motor is off yields nothing at all rather than
merely nothing yet.

It still cannot **rewind**. There is no rewind bit on a Sol-PC — a motor line only says
*turn*, not *which way* — so `REWIND` is your finger here too. Because there are two decks,
you must name one: a bare `REW sol0` is refused rather than guessing which tape to wind back.

**And the speed is the guest's.** The ACR's 300 baud is soldered; the Sol's cassette runs at
300 or 1200 and `OUT 0FAh` D5 picks, at run time. SOLOS's `SE TA` command is that bit.

Once a tape is in, SOLOS's own commands work:

```
>SA MYPROG 0100 01FF        (save memory to the tape)
>GE MYPROG                  (find it again and load it)
>CA                         (catalog what is on the tape)
```

## Working a tape

Everything in this section is the same on both cards. Only the unit name differs — `acr0:tape`
on the Altair, `sol0:tape1` or `sol0:tape2` on the Sol.

### The tape is not hardware

**The tape is NOT in the machine file, deliberately.**

A machine file describes the **hardware** — what cards are in the backplane, what they decode,
how much memory is on the bus. Which cassette is sitting in the recorder is not hardware. It
is a thing you did with your hands, this morning, and you can do a different thing with your
hands this afternoon without unscrewing the lid.

**You put the tape in, and you press PLAY.** That is `MOUNT`, and it is a thing you type.

### Putting a tape in

```
altairsim> MOUNT acr0:tape "{{TAPE_BASIC}}"
```

The ACR has one unit, so the unit carries no information, so you may drop it:

```
altairsim> MOUNT ACR tape.bin
```

A Sol has two, so you may not: name the deck.

Names are case-blind. The rules are the ones in the disks chapter, and they are the same rules.

### `mode = play | record`

```
altairsim> SET acr0:tape mode=record
```

Play and record are **mutually exclusive**, and not because it was easier to write that way.
It is **one head and one physical button**. You cannot press PLAY and RECORD at the same time
on a cassette recorder, so you cannot do it here.

### `REWIND`

A tape unit brings a verb of its own:

```
REWIND <id>:<unit>
```

`REW` will do. It winds the cassette back to the beginning.

**You need it to load the same tape twice.** A tape that has been read is a tape whose head is
at the end of the tape, and playing it again gets you silence. This surprises people exactly
once.

```
altairsim> REW acr0:tape
```

## Audio tapes — `.WAV`

Most surviving Altair and Sol cassettes are not files of bytes. They are **audio**: somebody
put a cassette in a deck, played it into a sound card, and saved a `.WAV`. You can mount one,
on either card.

```
altairsim> MOUNT sol0:tape1 TRK80.WAV
TRK80.WAV: cuts1200, 7932 bytes, 27 framing errors (99.7% clean)
```

Nothing else changes. The recording is demodulated **once, when you mount it** — never while
the machine is running — and from that moment everything above it, including `SHOW`'s *"at N
of M bytes"* and `REWIND`, means exactly what it meant for a `.TAP`. The guest cannot tell.

**Read that first line.** A mount always says what it found, and the framing-error count is
the number that matters: a tape that decoded at 60% is noise, not a program, and you want to
know that now rather than after the loader has crashed. A decode below 90% is refused outright.

**What decides is the file's magic, never its name.** A `.TAP` somebody renamed `.WAV` is
still read as bytes, and a recording renamed `.TAP` is still demodulated.

### A card will refuse audio it could not really have heard

That example mounted a Sol tape on a Sol. Put it in an Altair and the card says no:

```
altairsim> MOUNT acr0:tape TRK80.WAV
TRK80.WAV: this card's modem cannot hear that tape -- it carries 2398 Hz / 1206 Hz,
and this card reads fsk300
```

This is deliberate, and it is not the simulator being fussy.

Not all published Altair cassette audio is in the 88-ACR's modulation. The ACR uses
**2400/1850 Hz FSK**; plenty of archive tapes — the Sol's included — are **Kansas City**, at
2400/1200. The demodulator here measures the tones actually on the tape, so it *could* read
the Kansas City ones perfectly well — but a real 88-ACR could not. Its demodulator is a PLL
centred at 2125 Hz with about ±100 Hz of range, and a 1200 Hz tone is nowhere near that. A
real card fed that tape does not read it badly; it reads **nothing**.

Decoding it anyway would hand your guest program data that no 88-ACR on earth could have
produced. So the card says what the tape is instead, and you go find a machine that reads it.

### Recording back out to a `.WAV`

Put the recorder in `record`, and when the transport stops the tape is re-modulated and
written back over the file — in the format and at the sample rate it was mounted with:

```
altairsim> SET acr0:tape mode record
altairsim> RUN 0                        (the guest records)
altairsim> SET acr0:tape mode play      (...and the WAV is written here)
```

**This records over a WAV you already mounted; it does not make one.** The format and the
sample rate to re-modulate into are the ones the decode found at mount time, so they have to
have come from somewhere — and the only place they come from is a real recording that was
already in the file. There is no blank tape: `MOUNT` needs a file that exists, and a file
that is not a WAV is not made into one by naming it `.WAV`. To record fresh audio, start
from a WAV you have and record over it.

**A file called `.WAV` that is not one mounts as a byte tape, quietly.** The magic decides,
so an empty file — or anything else that is not RIFF/WAVE — falls through to `raw`, and
recording then puts *bytes* in it, not audio. It will look like it worked. Nothing will play
it. If you meant audio and you get `raw` on the mount line, that is what happened.

The stop is what writes it. `UNMOUNT` and `REWIND` are stops too, and on the Sol so is the
guest dropping the motor line — that card can see a deck stop, which the 88-ACR cannot.

The whole file is rewritten each time, not patched: the audio for a byte starts at an offset
that depends on every byte before it, so there is no cheaper splice.

**Time is the one thing that does not survive the round trip.** A byte image holds no
durations, so the leader a real transport needs has to be put back by whoever writes the audio.
Two properties do that, in seconds:

| Property | 88-ACR | Sol | Where the number comes from |
|---|---|---|---|
| `leader` | `15` | `3` | ACR: the MITS manual's *at least ~15 s of steady tone*. Sol: measured off TRK80.WAV, a real dub, which carries 3.05 s |
| `trailer` | `5` | `2` | ACR: §8's *at least 5 s between batches*. Sol: TRK80's measured 1.93 s |

Set either to `0` to trim the file to its data — which is what the published archive `.wav`
files are, and why they will not load on real hardware. Even then a floor of sixteen bit times
goes on each end, because a start bit is found by its **edge**: a tape that opened on the start
bit itself would lose its first byte.

**A multi-file tape comes back as one continuous run.** The decoded bytes carry no file
boundaries, so the gaps a real operator left between programs are not reproduced.

Recording to a `.TAP` works as it always has, and is unaffected by any of this.

### `format`, when you need to overrule the sniff

Each tape unit has a `format` property. `auto` is the default and is almost always right.

```
altairsim> SET acr0:tape format=raw
altairsim> SHOW acr0:tape
```

| Value | What it does |
|---|---|
| `auto` | Sniff for RIFF magic; demodulate a recording, read anything else as bytes |
| `raw` | Read the file's own bytes **even if it is a WAV** — how you inspect a tape that decodes badly |
| a modulation | Force one: `fsk300` on the ACR, `cuts1200` or `kcs300` on the Sol |

It selects a *reading*; it never widens the hardware. Telling an 88-ACR to demodulate
`cuts1200` is refused just as firmly as letting it sniff one — the card has the modem it has.
A companion read-only `detected` property reports what the mounted tape turned out to be.

`format` takes effect at the **next** `MOUNT`, because a tape is decoded once, when you put
it in.

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

### The sense switches matter

The machine file sets **`sense = 0x80`** on the front panel, and it is not decoration.

The bootstrap reads the sense switches to find out **what device to load from**. Its own
printed header says: *"Set A15 on (cassette load), all other switches off."* A15 on, and
nothing else, is `0x80`. Get it wrong and BASIC dutifully loads from **the wrong device**, and
then sits there forever waiting for bytes that are never coming, because you told it to listen
to a teletype that isn't there.

If a tape load hangs and you are sure the tape is mounted, **check the sense switches first.**
The front-panel chapter says where they live.

### Speed, and what the crystal actually buys you

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
