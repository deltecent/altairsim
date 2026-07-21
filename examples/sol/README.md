# TREK80 on a Sol-20

A Processor Technology **Sol-20** at the SOLOS prompt, with the 1977 game **TREK80** in the
cassette deck.

```
altairsim trek80.toml
```

and then **type this at the machine**:

```
XE TRK80
```

The tape runs, and after a minute or so:

```
COPYRIGHT (C) 1977  PROCESSOR TECHNOLOGY CORP.

ENTER SPEED FACTOR (9(SLOW)-0(FAST))
```

Pick a digit and play. `Trek80 Manual.pdf` beside this file is Processor Technology's own — it has
the commands, the quadrant display and the scoring.

**Why you type the last command yourself.** `startup` in a machine file runs *monitor* commands —
ours, before the guest has the keyboard. `XE TRK80` is input to **SOLOS**, a program running inside
the machine, and no configuration file can type at it any more than it could press keys on a real
Sol-20. So `trek80.toml` gets you to the prompt with the tape mounted and PLAY pressed; the two
words are yours.

## What you are looking at

With SDL3 the screen is a **window**, in the VDM-1's own character font — the Sol-20's display was
memory-mapped video at `0CC00H`, and that is what TREK80 paints on. Headless, the same machine runs
identically and the screen is readable with `DUMP CC00`, which is exactly how the acceptance test
checks this example.

`^E` (ATTN) takes the keyboard back to the monitor at any point; `RUN` resumes. The machine is not
disturbed by stopping it.

**The clock is 2.045 MHz**, which is the stock Sol-20 — the 14.31818 MHz dot clock divided by 7
(Sol Systems Manual, Theory of Operation §VIII). Everywhere else in this simulator the default is
flat out; here the real speed is set on purpose, because this is a game and it was played at that
speed. TREK80 also asks for a speed factor of its own, which is the knob the 1977 player had.

To run it a second time, rewind first — nothing inside the guest can wind a tape back:

```
REW sol0:tape1
```

## The files

| File | What it is |
|---|---|
| `trek80.toml` | The machine: `base = "sol20"`, the tape, and the clock. |
| `TRK80.WAV` | **The tape.** A Sol-20 cassette digitized by Philip Lord (hosted on deramp.com) — real CUTS audio at 1200 baud, decoded the way the hardware did. This is what `trek80.toml` mounts. |
| `TRK80.TAP` | The same cassette as a byte stream, decoded from `TRK80.WAV`. `MOUNT sol0:tape1 "TRK80.TAP"` skips the audio and loads faster; it is otherwise identical. |
| `TREK80.ENT` | A SOLOS `ENTER` script from the archive — 7,840 bytes loading at `0000`, entry `AF C3 5C 1D`. The source for the tape-writing demonstration below. |
| `Trek80 Manual.pdf` | Processor Technology's manual for the game. |
| `make-trek80-tape.sh` | A demonstration: it has SOLOS write its own tape from `TREK80.ENT`. |

**The tape is Philip Lord's real recording, and reading it is the point.** `TRK80.WAV` decodes to a
valid SOLOS tape with **zero framing errors** — the header names `TRK80`, its `SIZE` is `1EA0`, and
its checksum is `D9`, all matching the archived `ENTER` script byte for byte. That did not used to
work: an earlier decoder garbled this exact recording (27 framing errors, most of the payload
wrong), and the example tape had to be *synthesized* from the `.ENT` instead. The demodulator now
reads the real cassette, so the real cassette is what ships.

The synthesis is kept as `make-trek80-tape.sh` because it demonstrates the one thing a SOLOS tape
cannot fake: a SOLOS cassette carries a header checksum and one after every 256-byte block, and a
tape whose checksums are wrong is *invisible* — `CA` lists nothing and `GE` never finds the file.
The script has SOLOS `SA`ve the image so the checksums are the machine's own arithmetic; it writes
to a separate `TRK80-solos.*` so it never overwrites the shipped recording, and it proves the two
agree — SOLOS computes the same `D9` header checksum the real tape carries.

See `docs/sources.md` for full provenance.
