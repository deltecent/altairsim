# Tapes

Before the floppy disk, there was the **cassette recorder**. It was an ordinary home audio
cassette deck, with a microphone jack and an earphone jack. MITS sold a board that changed bytes
into a sound that the deck could record, and changed the sound back into bytes. That board is
the **88-ACR**, and it is in this simulator.

This chapter starts with the most important use of the ACR: loading Altair 4K BASIC 3.1 as
people loaded it in 1976. After that, it describes how to use a tape: mount it, move it, play it
at its real speed, use audio `.WAV` files, and record.

The **Sol-20** has its own cassette interface, with two decks and motor control that the guest
can use. Everything in this chapter also works on the Sol's decks, with the unit names
`sol0:tape1` and `sol0:tape2`. The `sol` section of the Boards chapter gives the differences.

## Load Altair 4K BASIC 3.1 by hand

An Altair owner did this every time that they wanted to use BASIC. There was nothing in ROM and
no disk. Type the commands in the folder where you unzipped the package.

1. Start the built-in `basic4k` machine. It has an 88-ACR, and its sense switches are set for a
   cassette load:

   ```
   $ altairsim basic4k
   ```

2. Put the cassette in:

   ```
   altairsim> MOUNT acr0:tape "examples/basic4k/4K BASIC Ver 3-1.tap"
   ```

3. Enter the bootstrap:

   ```
   altairsim> LOAD "examples/basic4k/LDR4K31.HEX"
   ```

   The bootstrap is 20 bytes. On a real Altair, you entered it by hand on the front-panel
   switches, from the listing that MITS printed in its manual. You set eight switches for each
   byte, and pressed DEPOSIT. `LOAD` puts the same 20 bytes in memory.

4. Run the bootstrap from address zero:

   ```
   altairsim> RUN 0
   ```

   The bootstrap reads the tape, BASIC loads into memory, and BASIC starts.

5. Answer the three questions. Press Return for the first two, and type `Y` for the third:

   ```
   MEMORY SIZE?
   TERMINAL WIDTH?
   WANT SIN? Y

   742 BYTES FREE

   ALTAIR BASIC VERSION 3.1
   [FOUR-K VERSION]

   OK
   ```

   An empty answer means "all of the memory" and "the default width". `WANT SIN?` asks whether
   to use some of your 4K for trigonometry.

You are in Altair BASIC, the first product that Microsoft sold. The *Worked examples* chapter
does the same with one command, `altairsim examples/basic4k/basic4k.toml`.

**If a tape load stops, and the tape is mounted, check the sense switches first.** The bootstrap
reads them to find the device to load from. The `basic4k` machine sets `sense = 0x80` on the
front panel: A15 on, for a cassette load, and all other switches off. With a different setting,
BASIC waits for bytes from a device that is not there. The `fp` section of the Boards chapter
describes the switches.

## The 88-ACR

`acr` is the **MITS 88-ACR**, an Audio Cassette Record and playback interface. It is **an 88-SIO
channel B with an FSK modem connected to it**, as MITS built it. The guest uses an ordinary
serial port, and the modem changes the bits into tones.

- The default port is **06**. `06` is status and control, and `07` is data.
- It runs at **300 baud**, the speed of the tape. You cannot change it.
- It has one unit, **`tape`**, because a cassette recorder has one place for a tape.

**You cannot `CONNECT` the ACR.** Its line goes to the modem on the board, and the modem goes to
a cassette deck. There is no connector for a terminal on the back of an 88-ACR. The serial
chapter describes the boards that take endpoints.

**You put the tape in by hand.** An 88-ACR cannot start, stop or rewind the tape, and the
machine does not know that a tape is there. For this reason, a machine file has no key for the
tape. The tape is not hardware. You put it in with `MOUNT`, and a `startup` command can type the
`MOUNT` for you, as `examples/basic4k/basic4k.toml` does.

## Use a tape

### Put a tape in

```
altairsim> MOUNT acr0:tape "examples/basic4k/4K BASIC Ver 3-1.tap"
```

The ACR has one unit, so you can leave out the unit and the number:

```
altairsim> MOUNT ACR tape.bin
```

A Sol has two decks, so you must name the deck.

### `mode = play | record`

```
altairsim> SET acr0:tape mode=record
```

**`play` loads from the file, and `record` saves to it.** A tape has one head, and it moves in
one direction at a time, so `mode` has two values only.

### The tape counter

`SHOW MOUNTS` and `SHOW <id>` give the position of the head:

```
altairsim> SHOW MOUNTS
  UNIT       KIND  HOLDS
  acr0:tape  tape  4K BASIC Ver 3-1.tap  00:00 / 02:28 (0%)  1/4439 bytes

  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.
```

The counter gives minutes and seconds into the tape, a percentage, and a byte count. For a
`.WAV` file, the time is the recording's own time, with the leader and the silent gaps between
programs, as on a real cassette counter. For this reason, a program that a manual finds "so many
seconds from the start of the tape" is at that second here. A byte `.TAP` file has no audio, so
the program calculates its time from the baud rate.

When a tape plays at its real speed (`rate = real`, below), the counter **counts up on the
console while the tape loads**. Turn it off for a machine whose guest writes to the same
terminal:

```
altairsim> MOUNT acr0:tape "tape.wav" counter=off
```

`SET acr0:tape counter=off` also works. When the counter is off, `SHOW` still gives the
position.

### `WIND` and `REWIND`: move the head

```
WIND <id>:<unit> <mm:ss | START | END>
REWIND <id>:<unit>
```

`WIND` (`WI`) moves the head to a time: `mm:ss`, a number of seconds, `START` or `END`. On a
cassette with several programs, read the counter or a manual for the start of the next program,
and wind to that time:

```
altairsim> WIND acr0:tape 0:05
acr0:tape: wound to 00:05 / 02:33 (3%) -- 4K BASIC Ver 3-1.wav (1 of 4439 bytes)
```

`REWIND` (`REW`) is the same as `WIND … START`. **Use it to load the same tape a second time.**
After the guest reads a tape, the head is at the end, and a second read gets nothing.

### `stop`: stop the tape at a time

On a tape with several programs, one program runs into the next. The `stop` mark is like
pressing the **STOP button of the recorder at a counter mark**. Set it with `MOUNT`, or with
`SET` at any time:

```
altairsim> MOUNT acr0:tape "tape.wav" stop=2:05
altairsim> SET acr0:tape stop=2:05
```

The line goes silent at 2:05, as at the end of the tape, and the loader does not read into the
next program. To continue, move the mark forward or clear it, and play again:

```
altairsim> SET acr0:tape stop=5:30      (the next program ends here)
altairsim> SET acr0:tape stop=off       (play to the end of the tape)
```

The mark is `off` until you set it. `SHOW` shows a mark as `stop @ 02:05`. The mark stops
**playback only**, and a recording writes past it. If you wind the head past a mark, the tape
stays stopped there, and `SHOW` says why.

## Speed: `rate = full | real`

**The tape has its own clock, and by default it runs as fast as possible.** With `rate = full`,
the guest gets each byte as soon as it is ready to read the next one. A cassette that took a
real Altair about **110 seconds** loads in **about one second**.

```
altairsim> SET acr0:tape rate=real
```

`rate = real` plays the tape at the tape's baud rate, 300 or 1200, and a load takes as long as
it took on the real machine. Use it when you want to show the wait, for example in a
demonstration. It applies to playback only. A recording takes as long as the guest takes.

**`rate` is not the processor clock.** The tape and the processor had separate crystals on the
real hardware, and they are separate here. `SET cpu0 clock_hz=2000000` gives the *processor* the
speed of the period, so that a game plays at its real speed. It does not change the speed of the
tape. With `rate = full`, the guest sees no difference at any processor speed, because a polled
loader only asks for the next byte, and the byte is always ready.

## Audio tapes: `.WAV`

Most surviving Altair and Sol cassettes are **audio**. Somebody played a cassette into a sound
card, and saved a `.WAV` file. You can mount one on either board.

**`examples/basic4k/` has 4K BASIC on an audio tape**, `4K BASIC Ver 3-1.wav`. It holds the
program of the `.tap`, encoded in the audio format of the 88-ACR. The machine file
`basic4k-wav.toml` mounts it and boots.
In that folder, type:

```
altairsim> MOUNT acr0:tape "4K BASIC Ver 3-1.wav"
acr0:tape: mounted 4K BASIC Ver 3-1.wav
4K BASIC Ver 3-1.wav: fsk300, 4439 bytes, 0 framing errors (100.0% of frames intact)
```

`examples/basic1/` has Altair BASIC 1.0 on an audio tape too, `BASIC Ver 1-0.wav`. The machine
file `basic1-wav.toml` mounts it.

The program decodes the recording **one time, when you mount it**, and never while the machine
runs. After that, a `.WAV` works as a `.TAP` does. The counter gives the real minutes and
seconds of the recording, and the guest sees no difference.

**Read the second line of the mount output.** It gives the number of framing errors. A tape that
decodes at 60% is noise, not a program, and the program refuses a decode below 90%. The
percentage counts only the frames whose start and stop bits were in the correct place. A worn
recording can keep its framing and still give wrong bytes. If a tape mounts well and the program
still does not run, the recording is probably worn.

**The start of the file decides the format, never its name.** A `.TAP` file that somebody
renamed `.WAV` is still read as bytes. A recording renamed `.TAP` is still decoded as audio.

### `extract`: one `.TAP` file for each program

A cassette often holds several programs, with a few seconds of silence between them. **`extract`
writes each program to its own `.TAP` file**, so that you can mount or load one program at a
time:

```
altairsim> MOUNT acr0:tape "games.wav" extract
acr0:tape: mounted games.wav
  games-1.tap  2048 bytes
  games-2.tap  3120 bytes
2 programs extracted
```

The files go **beside the WAV**, numbered from 1. A tape with one program gives `games.tap`,
with no number. `extract=<base>` sets the names, for example `extract=disk1` gives
`disk1-1.tap`. `extract` reads the tape and writes the files. Nothing in the machine changes.

To split a WAV that is already in the deck, use the `EXTRACT` command:

```
altairsim> EXTRACT acr0:tape
```

You can extract only a `.WAV`. The program splits at one second or more of silence. That is much
longer than any gap inside a program, so no program is cut in half.

### A board refuses audio that it cannot hear

`examples/basic4k/` also has 4K BASIC in a modulation that the 88-ACR *cannot* read,
`4K BASIC (Kansas City).wav`. The board refuses it:

```
altairsim> MOUNT acr0:tape "4K BASIC (Kansas City).wav"
acr0: 4K BASIC (Kansas City).wav: this board's modem cannot hear that tape -- it carries 2400 Hz / 1200 Hz, and this board reads fsk300
```

Cassette audio of the period uses different tones:

- The 88-ACR uses **2400/1850 Hz FSK**.
- Many archive tapes are **Kansas City**, **2400/1200 Hz**.
- The Sol's CUTS tapes are one octave lower, **1200/600 Hz**, at 1200 baud.

The decoder of the program could read all of them, but a real 88-ACR could not. Its decoder is a
PLL centered at 2125 Hz, with a range of about ±100 Hz. A 1200 Hz tone is far outside that
range, so the real board reads **nothing** from that tape. If the program decoded the tape
anyway, it would give the guest data that no 88-ACR could produce. For this reason, the board
tells you what the tape is, and you use a machine that can read it.

**The frequencies in the message are a measurement.** On a worn recording, a value can be one
octave wrong. The message means *"this is not my format, and this is about what is there"*.

## Record a tape

Set the recorder to `record`, and run the guest. When the tape stops, the program writes the
recording to the file:

```
altairsim> SET acr0:tape mode=record
altairsim> RUN 0                        (the guest records)
altairsim> SET acr0:tape mode=play      (the program writes the file here)
```

The tape stops when you set `mode=play`, `UNMOUNT`, `WIND`, `REWIND` or `QUIT`. On a Sol, the
guest can also stop the motor. The program writes the whole file again each time.

**A recording writes over the file.** A mounted `.WAV` is recorded in the format and at the
sample rate that the program found when it decoded the file. Record on a *copy* if you want to
keep the original.

### A blank tape: `MOUNT … CREATE`

```
altairsim> MOUNT acr0:tape new.wav CREATE mode=record
altairsim> MOUNT acr0:tape new.tap CREATE mode=record
```

A blank file has nothing to examine, so its **name** decides what it becomes. A `.wav` name
makes a blank **audio** tape. Any other name makes a blank **byte** tape. The mount line tells
you which one you got:

```
new.wav: blank fsk300 tape, ready to record
new.tap: blank raw byte tape
```

Give `mode=record`, because the deck starts in PLAY. The guest can then write to the tape, for
example with `CSAVE` in BASIC.

**A file that already exists is different.** Its start decides, never its name. A `.wav` file
that is not really a WAV is read as bytes, and a recording then puts bytes in it, not audio.
Nothing can play it. If you wanted audio and the mount line says `raw`, this is what happened.

### The leader, the tone and the level

A byte image holds no times, so the program adds the leader that a real tape needs when it
writes audio. These properties of the tape unit shape the audio:

| Property | 88-ACR default | What it does |
|---|---|---|
| `leader` | `15` | Seconds of steady tone before the data. The MITS manual asks for about 15 s |
| `trailer` | `5` | Seconds of tone after the data. The MITS manual asks for 5 s between programs |
| `waveform` | `square` | `square` is the tone of the real modem, fuller and louder. `sine` is smoother and quieter. Both decode to the same bytes |
| `level` | `36` | The recording level, in percent of full scale. This is the level of a real tape. A much higher level overloads the input of a real deck |

Set `leader` or `trailer` to `0` to make the file as small as possible. The archive `.wav` files
are cut in this way, and for this reason they do not load on real hardware. Even at `0`, the
program writes sixteen bit times of tone at each end, because the loader finds a start bit by
its edge.

The Sol's decks have shorter defaults, and one more property, `rc`. The `sol` section of the
Boards chapter gives them.

**A tape with several files comes back as one continuous recording.** The bytes do not mark
where one file ends, so the gaps between programs are not written back.

None of this applies to a `.TAP` file.

## `format`: choose the format yourself

Each tape unit has a `format` property. The default, `auto`, is almost always correct.

```
altairsim> SET acr0:tape format=raw
altairsim> SHOW acr0
```

| Value | What it does |
|---|---|
| `auto` | Decode a WAV file as audio. Read any other file as bytes |
| `raw` | Read the bytes of the file, **also for a WAV**. Use it to look at a tape that decodes badly |
| a modulation | Decode with this modulation: `fsk300` on the ACR, `cuts1200` or `kcs300` on the Sol |

`format` selects how the program *reads* the file. It never changes the hardware. If you tell an
88-ACR to decode `cuts1200`, it refuses. The read-only `detected` property gives the format of
the mounted tape.

`format` also selects the modulation of a **blank** tape from `MOUNT … CREATE`. A `.wav` name
records with the board's own modem, and `format` selects a different one by name.

`format` takes effect at the **next** `MOUNT`, because the program decodes a tape when you put
it in. `SET` names the unit, and `SHOW` names the board. There is no `SHOW <id>:<unit>`.
