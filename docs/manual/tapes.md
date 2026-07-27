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
| Modulation | `fsk300` (2400/1850 Hz) | `cuts1200` (1200/600 Hz), `kcs300` (Kansas City, 2400/1200 Hz) |
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
about the boards that *do* take endpoints.

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

It still cannot **wind the tape** on its own. There is no rewind bit on a Sol-PC — a motor line
only says *turn*, not *which way* — so `WIND` (and its `REWIND` shorthand) is your finger here
too. Because there are two decks, you must name one: a bare `WIND sol0` is refused rather than
guessing which tape to move.

**And the speed is the guest's.** The ACR's 300 baud is soldered; the Sol's cassette runs at
300 or 1200 and `OUT 0FAh` D5 picks, at run time. SOLOS's `SE TA` command is that bit.

Once a tape is in, SOLOS's own commands work:

```
>SA MYPROG 0100 01FF        (save memory to the tape)
>GE MYPROG                  (find it again and load it)
>CA                         (catalog what is on the tape)
```

## Working a tape

Everything in this section is the same on both boards. Only the unit name differs — `acr0:tape`
on the Altair, `sol0:tape1` or `sol0:tape2` on the Sol.

### The tape is not hardware

**The tape is NOT in the machine file, deliberately.**

A machine file describes the **hardware** — what boards are in the backplane, what they decode,
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

**`play` loads from the file; `record` saves to it.** Which way the bytes are going is the
whole of the setting.

The two are **mutually exclusive** — one tape, one head, one direction at a time — so `mode`
is one setting with two values, and there is no third.

### Where the head is — the tape counter

`SHOW MOUNTS`, and `SHOW <id>`, report where the head is sitting as a position on the tape:

```
altairsim> SHOW MOUNTS
  acr0:tape  tape  BASIC.WAV  00:15 / 01:28 (17%)  301/2048 bytes
```

The counter is a **time and a percentage** — minutes and seconds into the tape, and how far
along it you are. For a `.WAV` that time is the recording's *own*: it counts the leader and
any silent gaps between programs, exactly as a real cassette counter would, so a program a
manual indexes by "seconds from the start of the tape" is at the second the manual says. For a
byte `.TAP`, which carries no audio, the time is estimated from the baud. The byte count is
still there beside it.

### `WIND` — move the head to a time

A tape unit brings a verb of its own:

```
WIND <id>:<unit> <mm:ss | START | END>
```

`WI` will do. It winds the head to a time on the tape — so a cassette holding several programs
one after another is **reachable**: read the counter (or a manual) for where the next one
begins, and wind there.

```
altairsim> WIND acr0:tape 2:05
acr0:tape: wound to 02:05 / 08:40 (24%) -- BASIC.WAV (...)
```

The position is a time — `mm:ss`, or a bare number of seconds — or the words `START` and
`END`. A time past the end lands at the end. On the Sol you must name the deck, because there
are two.

### `REWIND` — wind to the start

`REWIND <id>:<unit>` (`REW`) is `WIND … START`: the common case, kept as its own verb.

**You need it to load the same tape twice.** A tape that has been read is a tape whose head is
at the end of the tape, and playing it again gets you silence. This surprises people exactly
once.

```
altairsim> REW acr0:tape
```

### Watching a load — the live counter

When a tape plays in real time (`rate = real`, below) the counter **ticks up on the console
while it loads**, so you can watch a long tape's progress. It is on by default; turn it off for
a machine whose guest is writing to the same terminal:

```
altairsim> MOUNT acr0:tape "tape.wav" counter=off
```

or `SET acr0:tape counter=off` at any time. Turning it off changes nothing about `SHOW` — the
position is always there to ask for.

### `stop` — halt the tape at a time

A multi-program tape runs one program straight into the next. The `stop` mark is your finger on
the recorder's **STOP button at a counter mark**: set it to a time and the tape goes quiet there
instead of running on. Set it at `MOUNT`, or with `SET` any time after:

```
altairsim> MOUNT acr0:tape "tape.wav" stop=2:05
altairsim> SET  acr0:tape stop=2:05
```

Load program 1 and the line falls silent at 2:05 — the same quiet the end of the tape gives — so
the loader stops there rather than reading into program 2. To carry on, move the mark forward
(or clear it) and go again:

```
altairsim> SET acr0:tape stop=5:30      (the next boundary)
altairsim> SET acr0:tape stop=off       (play to the physical end)
```

It is `off` (play to the end) unless you set it. `SHOW` shows an armed mark as `stop @ 02:05`. It
halts **playback only** — a recording writes straight through it — and it is independent of
`WIND`: winding the head past an armed stop leaves the tape parked there (SHOW says why), so move
or clear the mark to continue.

### `rate = full | real`

**The cassette carries its own clock, and by default it runs flat out.** `rate = full` — the
default — hands the guest each byte the moment it is ready to read the next one, so a tape
loads in about a second whatever speed the CPU is set to. This is almost always what you want:
a program you are trying to run should not make you wait for a recorder that has been gone for
forty years.

```
altairsim> SET sol0:tape1 rate=real
```

`rate = real` paces playback in **real wall-clock time** at the tape's baud — 1200 or 300 —
so a load takes as long as it took on the day. Set it when the *wait itself* is the point: a
demo, a screen recording, the sound of the thing. It is playback only; recording takes as long
as the guest drives it either way.

**What this is *not* is the CPU clock.** The tape's clock and the processor's are separate
crystals on the real hardware, and they are separate here — see *Speed* below. A faster or
slower CPU no longer drags the tape with it; `rate` is the only thing that governs how fast a
tape plays.

## Audio tapes — `.WAV`

Most surviving Altair and Sol cassettes are not files of bytes. They are **audio**: somebody
put a cassette in a deck, played it into a sound card, and saved a `.WAV`. You can mount one,
on either board.

**There are several in the package**, in the Sol-20 example under `examples/`, each with a
machine file beside it set up to read it — its README lists them. `TRK80.WAV` is a CUTS
cassette carrying *Star Trek*, so this is a line you can actually type:

```
altairsim> MOUNT sol0:tape1 TRK80.WAV
sol0:tape1: mounted TRK80.WAV
TRK80.WAV: cuts1200, 7939 bytes, 0 framing errors (100.0% of frames intact)
```

Nothing else changes. The recording is demodulated **once, when you mount it** — never while
the machine is running — and from that moment everything above it, including `SHOW`'s byte
count and `WIND`/`REWIND`, means exactly what it meant for a `.TAP`. The one thing the audio
adds is a *real* clock: the tape counter reads the recording's own minutes and seconds, gaps
and leader included, where a byte tape can only estimate them. The guest cannot tell.

**Read that first line.** A mount always says what it found, and the framing-error count is
the number that matters: a tape that decoded at 60% is noise, not a program, and you want to
know that now rather than after the loader has crashed. A decode below 90% is refused outright.

**But it is a framing rate, not a clean bill of health.** The percentage counts frames whose
start and stop bits landed where they belonged; it cannot tell you the eight bits between them
are the ones that were recorded. The two can come apart: a worn or poorly dubbed recording can
keep its framing and still hand the loader wrong bytes. A high percentage means the demodulator
kept its footing, and no more. If a tape mounts well and the program still will not run, a worn
recording is the likely answer.

**What decides is the file's magic, never its name.** A `.TAP` somebody renamed `.WAV` is
still read as bytes, and a recording renamed `.TAP` is still demodulated.

### `extract` — split a WAV into per-program `.TAP` files

A cassette WAV often holds several programs one after another, separated by a few seconds of
silence. **`extract` writes each program out as its own `.TAP`** — so you can keep, mount or load
them one at a time instead of winding through the whole tape. Ask for it at `MOUNT`:

```
altairsim> MOUNT acr0:tape "games.wav" extract
acr0:tape: mounted games.wav
  games-1.tap  2048 bytes
  games-2.tap  3120 bytes
2 programs extracted
```

The files land **beside the WAV**, named from it: `games.wav` becomes `games-1.tap`,
`games-2.tap`, … with a 1-to-N index (a single-program tape is just `games.tap`, no index). Each
line prints the file's name and size. `extract=<base>` names them yourself
(`extract=disk1` → `disk1-1.tap`, …). It only **reads** the tape and **writes** the files —
nothing in the machine changes.

The same thing has a verb, so you can split a WAV that is already in the deck without re-mounting:

```
altairsim> EXTRACT acr0:tape          (on the Sol, name the deck: EXTRACT sol0:tape1)
```

Only a `.WAV` can be extracted — a `.TAP` is already the bytes, and has no gaps left to split on.
The boundary is a second or more of silence, which is far longer than the gaps *inside* a program,
so programs come apart cleanly and none is cut in half.

### A board will refuse audio it could not really have heard

That example mounted a Sol tape on a Sol. Put it in an Altair and the board says no:

```
altairsim> MOUNT acr0:tape examples/sol/TRK80.WAV
acr0: examples/sol/TRK80.WAV: this board's modem cannot hear that tape -- it carries
2393 Hz / 1204 Hz, and this board reads fsk300
```

This is deliberate, and it is not the simulator being fussy.

Not all published Altair cassette audio is in the 88-ACR's modulation. The ACR uses
**2400/1850 Hz FSK**; plenty of archive tapes are **Kansas City** (2400/1200 Hz), and the Sol's
own CUTS tapes are an octave lower still — **1200/600 Hz** at its default 1200 baud. The
demodulator here measures the tones actually on the tape, so it *could* read them perfectly well
— but a real 88-ACR could not. Its demodulator is a PLL centred at 2125 Hz with about ±100 Hz of
range, and a 1200 Hz (let alone 600 Hz) tone is nowhere near that. A real card fed that tape does
not read it badly; it reads **nothing**.

Decoding it anyway would hand your guest program data that no 88-ACR on earth could have
produced. So the board says what the tape is instead, and you go find a machine that reads it.

**The frequencies in that message are a measurement, and a measurement can be out by an
octave** — which is why the refusal above says 2393/1204 for a tape this chapter calls
1200/600. All the demodulator has to go on is where the signal crosses zero, and a crossing
interval is either a whole cycle or half of one depending on the *shape* of the wave; nothing
in the signal says which. So the message quotes whichever of the two readings is nearer the
tones the card asking was built for. Read it as *"not mine, and here is roughly what is
there"*, not as the tape's specification.

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
already in the file. There is no blank *audio* tape: a file that is not a WAV is not made into
one by naming it `.WAV`. To record fresh audio, start from a WAV you have and record over it.

**A blank *byte* tape, though, you can make — with `MOUNT … CREATE`.** `MOUNT acr0:tape
save.tap CREATE` writes an empty file and mounts it, and the guest can then record a program
onto it (a `CSAVE` from BASIC, say). That is a byte tape, not audio — which is exactly what
you want for saving and re-loading a program. Without `CREATE`, `MOUNT` needs a file that
already exists, so a mistyped name is caught as a mistake rather than turned into a blank tape.

**A file called `.WAV` that is not one mounts as a byte tape, quietly.** The magic decides,
so an empty file — or anything else that is not RIFF/WAVE — falls through to `raw`, and
recording then puts *bytes* in it, not audio. It will look like it worked. Nothing will play
it. If you meant audio and you get `raw` on the mount line, that is what happened.

The stop is what writes it. `UNMOUNT` and any `WIND` (`REWIND` included) are stops too, and on
the Sol so is the guest dropping the motor line — that board can see a deck stop, which the
88-ACR cannot.

The whole file is rewritten each time, not patched: the audio for a byte starts at an offset
that depends on every byte before it, so there is no cheaper splice.

**Time is the one thing that does not survive the round trip.** A byte image holds no
durations, so the leader a real transport needs has to be put back by whoever writes the audio.
Two properties do that, in seconds:

| Property | 88-ACR | Sol | Where the number comes from |
|---|---|---|---|
| `leader` | `15` | `3` | ACR: the MITS manual's *at least ~15 s of steady tone*. Sol: measured off the archived Star Trek recording, a real dub, which carries 3.05 s |
| `trailer` | `5` | `2` | ACR: §8's *at least 5 s between batches*. Sol: that recording's measured 1.93 s |

Set either to `0` to trim the file to its data — which is what the published archive `.wav`
files are, and why they will not load on real hardware. Even then a floor of sixteen bit times
goes on each end, because a start bit is found by its **edge**: a tape that opened on the start
bit itself would lose its first byte.

**The carrier can be shaped like the real hardware, or smoothed.** A third property, `waveform`,
chooses how the tone is drawn when audio is written back:

| Property | Default | Choices | What it does |
|---|---|---|---|
| `waveform` | `square` | `square` \| `sine` | `square` is what the real modem lays down — a fuller, louder tone that sounds like a genuine cassette dub. `sine` is a smoother, quieter tone. |

It is audible, not structural: a tape written either way decodes back to the same bytes, so this
changes how the recording **sounds**, never what it holds. `square` is the default because it is
the closer match to a real recorder.

**But the recording level and the edge shape are structural — they decide whether a real Sol
loads the tape.** A genuine Sol CUTS modem is a flip-flop dividing a master clock into a square,
rounded by an RC network, recorded at a modest level. Two more properties reproduce that, and
their defaults are measured off the one genuine dub in the package (`TRK80.WAV`):

| Property | 88-ACR | Sol | What it does |
|---|---|---|---|
| `level` | `36` | `36` | Recording level, percent of full scale. The old default ran at 80% — more than twice a real dub — which overdrove a real Sol's input AGC so the tape read its header and then failed. |
| `rc` | — | `4000` | Edge-rounding low-pass corner, Hz. Rounds the square's edges the way the modem's RC network and the cassette's own bandwidth do, so the tone curves like a real dub instead of sitting on a flat top. Sol CUTS only. |

Unlike `waveform`, these are not merely audible. A Sol CUTS tape the simulator writes now lays its
tones on the same clock grid a real Sol expects, at a real dub's level — so it is built to load on
the hardware, not only to read back here.

**A multi-file tape comes back as one continuous run.** The decoded bytes carry no file
boundaries, so the gaps a real operator left between programs are not reproduced.

Recording to a `.TAP` works as it always has, and is unaffected by any of this.

### `format`, when you need to overrule the sniff

Each tape unit has a `format` property. `auto` is the default and is almost always right.

```
altairsim> SET acr0:tape format=raw
altairsim> SHOW acr0
```

(`SET` addresses the unit; `SHOW` addresses the **board**, and lists every unit on it with
its properties underneath. There is no `SHOW <id>:<unit>`.)

| Value | What it does |
|---|---|
| `auto` | Sniff for RIFF magic; demodulate a recording, read anything else as bytes |
| `raw` | Read the file's own bytes **even if it is a WAV** — how you inspect a tape that decodes badly |
| a modulation | Force one: `fsk300` on the ACR, `cuts1200` or `kcs300` on the Sol |

It selects a *reading*; it never widens the hardware. Telling an 88-ACR to demodulate
`cuts1200` is refused just as firmly as letting it sniff one — the board has the modem it has.
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

**The machine runs flat out by default, and so does the tape.** A cassette that took a real
Altair about **110 seconds** to load comes off the tape in **about one**.

**The tape and the CPU are on separate clocks — as they were in the hardware.** The processor's
speed is `clock_hz` on the CPU card; the tape's is `rate` on the deck (above). Setting the CPU
back to a real 2 MHz:

```
altairsim> SET cpu0 clock_hz=2000000
```

buys back the period *feel of the machine* — a game plays at the speed it was played at — but
it **no longer drags the tape with it**. If you want the load itself to take as long as a load
took, that is a separate switch:

```
altairsim> SET acr0:tape rate=real
```

Here is the part worth understanding. In `rate = full`, **what the guest sees is identical**
whatever the CPU clock: the ACR is clocked in T-states, and a byte is simply ready whenever the
guest asks for the next — no program from the period could tell, because a polled loader only
ever asked. In `rate = real`, the gaps are paced in real seconds instead, off a wall clock the
CPU speed cannot touch — which is the one and only way to make a load take *wall-clock* time.

So **`clock_hz` buys period feel for the processor; `rate = real` buys it for the tape.** They
are independent, because on a Sol-20 or an Altair the cassette UART and the CPU each ran off
their own crystal, and neither could hurry the other.

## {{NAME_DISKBASIC}}

The other BASIC in the package does not use tape at all. {{NAME_DISKBASIC}} lives on an 8"
floppy, and it has what a cassette cannot give you: files, a directory, and a `SAVE` that
takes a name.

```
$ cd examples/diskbasic
$ altairsim diskbasic.toml
```

It asks five questions before it will talk to you, and **the second one is the one that
stops people**:

```
MEMORY SIZE? 
LINEPRINTER? C
HIGHEST DISK NUMBER? 0
HOW MANY FILES? 
HOW MANY RANDOM FILES? 

37033 BYTES FREE
ALTAIR BASIC REV. 4.1
[DISK EXTENDED VERSION]
COPYRIGHT 1977 BY MITS INC.
OK
```

`MEMORY SIZE?` takes a bare Return and uses all of it. `HIGHEST DISK NUMBER?` is `0` — one
drive, numbered from zero. The last two take Return.

**`LINEPRINTER?` accepts `C`, `O` or `Q`, and nothing else.** Give it a blank line, or an
`N`, and it asks again — no error, no complaint, no hint that it wants something particular.
A machine sitting at a prompt that reappears every time you answer it looks exactly like a
machine that has hung, and it is not one; it is a 1977 program that expects you to have the
manual open. `C` is the 88-C700 line printer, and it is a legal answer here even though this
machine has no printer board fitted — the answer only tells BASIC where `LPRINT` should go,
and nothing is sent anywhere until you use it.

Past that, it is BASIC:

```
OK
PRINT 2+2
 4 
```

The disk is mounted read/write, because that is what a real machine was. `SAVE` will write
to it.

The cassette BASIC in the package is the real thing, and it is the one that shows you what an
Altair actually was: a box with no operating system, no storage, and no software in it, which
you talked into existence one toggled byte at a time.
