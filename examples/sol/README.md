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
| `TREK80.ENT` | **The source.** A SOLOS `ENTER` script from the archive — 7,840 bytes loading at `0000`, entry `AF C3 5C 1D`. |
| `TRK80.TAP` | **Derived.** The cassette as a byte stream. |
| `TRK80.WAV` | **Derived.** The same cassette as CUTS audio at 1200 baud — real tones, decoded the way the hardware did. `MOUNT sol0:tape1 "TRK80.WAV"` and it loads, slower. |
| `Trek80 Manual.pdf` | Processor Technology's manual for the game. |
| `make-trek80-tape.sh` | The derivation: `.ENT` → `.TAP` → `.WAV`. |

**The tape is synthesized, and that is not a compromise.** Both archived recordings of TREK80 are
unusable as *data*: deramp's `TRK80.WAV` decodes with 27 framing errors, and those errors
desynchronize the byte stream badly enough that 6,778 of its 7,840 payload bytes come out wrong.
(It is still a historically important recording — it is the tape the simulator's CUTS timing
parameters were measured from.) The archived `ENTER` script is intact, so the tape here is written
**by SOLOS itself** from that image: mount a blank tape in record mode and `SA`ve. A SOLOS cassette
carries a header checksum and one after every 256-byte block, and a hand-assembled tape with the
wrong checksums is *invisible* — `CA` lists nothing and `GE` never finds the file.

The cross-check that this is the real artifact rather than a plausible one: the header checksum
SOLOS computes here is `D9`, the same byte in the same position as on the genuine archived tape.

See `docs/sources.md` for provenance, and run `make-trek80-tape.sh` to rebuild both derived files
from `TREK80.ENT`.
