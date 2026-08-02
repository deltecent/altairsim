# Altair BASIC off a cassette

Two Altair BASICs, both read off period cassette images by the bootstraps MITS shipped, all
unmodified. The 4K is the one-command boot; **BASIC 1.0 is the raw 1975 original**, and it takes
the two moves a human took in 1975 because its bootstrap knew no other way.

| | Boot | What it is |
|---|---|---|
| [4K BASIC 3.1](#altair-4k-basic-31) | `altairsim basic4k.toml` | Reaches `MEMORY SIZE?` on its own. |
| [BASIC 1.0](#altair-basic-10) | `altairsim basic1.toml`, then `^E`, then `RUN 0` | The first Altair BASIC. Reaches `MEMSIZ?`. |

## Altair 4K BASIC 3.1

```
altairsim basic4k.toml

MEMORY SIZE? <return>
TERMINAL WIDTH? <return>
WANT SIN? Y

ALTAIR BASIC VERSION 3.1
[FOUR-K VERSION]
742 BYTES FREE
OK
```

**Altair 4K BASIC 3.1**, read off a period cassette image by the bootstrap MITS shipped, both
unmodified. The machine is the bare 1975 Altair: 4K of RAM, an 88-2SIO for the Teletype, an 88-ACR
for the recorder, sense switches at `0x80`.

The three lines in `basic4k.toml`'s `startup` are the three things a human did in 1975 — put the
tape in and press PLAY (`MOUNT`), toggle in the bootstrap (`LOAD`), and run it from zero. The
bootstrap (`LDR4K31`) jumps into BASIC on its own when the tape runs out, so one command reaches
the prompt.

The tape comes off in about a second rather than the 110 the real 300-baud cassette took, because
the default clock is flat out. `SET cpu0 clock_hz=2000000` buys back the 2 MHz machine **and** its
110 seconds — BASIC cannot tell the difference either way.

## Altair BASIC 1.0

```
altairsim basic1.toml

tape: 00:15 / 02:30 (100%)      <- press ^E when the counter is done, then:
RUN 0

MEMSIZ? <return>
WANT SIN-COS-ATN? <return>

2000 BYTES FREE

8080 BASIC VER 1.0

READY
```

**"8080 BASIC VER 1.0"** — the oldest Altair BASIC there is, the interpreter Bill Gates and Paul
Allen wrote for the Altair in 1975. Same bare machine as the 4K above, but with the original
88-SIO console and 8K of RAM (BASIC 1.0's interpreter loads to `0000-117F`, so it does not fit in
a 4K machine at all).

**Why it takes two moves where the 4K takes one.** BASIC 1.0's bootstrap (`LOAD10`) is the
primitive one: it copies the tape into memory starting at `0000` and loops forever. It has no idea
how long the tape is, so it never stops and never jumps to what it loaded. That is genuinely how
you booted it — the operator watched the tape run out, hit STOP/RESET, and ran BASIC by hand from
`0`. So:

1. `altairsim basic1.toml` puts the tape in, toggles in the bootstrap, and starts it (`RUN 1800`).
2. When the tape is done, press **`^E`** (ATTN) — the STOP/RESET, which returns you to the monitor.
3. Type **`RUN 0`** to start BASIC.

`MEMSIZ?` really is spelled that way — Microsoft set the message-terminator bit on the `Z` rather
than spend a byte on a trailing `E`. Answer it and `WANT SIN-COS-ATN?` with `<return>`.

**Watching the tape load.** At the flat-out default the load is over in an instant. `SET
acr0:tape rate=real` before you run plays the tape at its true 300-baud speed — about two and a
half minutes — and the console tape counter climbs `00:00 → 02:30`; the counter reaching `100%` is
how you know it is time to press `^E`.

## The files

| File | What it is |
|---|---|
| `basic4k.toml` | The 4K operator. `base = "basic4k"` is the hardware (`machines/basic4k.toml`); this puts the tape in the recorder and runs it. |
| `4K BASIC Ver 3-1.tap` | The 4K cassette. |
| `LDR4K31.HEX` | The 4K bootstrap, assembled. |
| `basic1.toml` | The BASIC 1.0 machine and operator, in one self-contained file. |
| `BASIC Ver 1-0.tap` | The BASIC 1.0 cassette. |
| `LOAD10.HEX` | The BASIC 1.0 bootstrap, assembled. |

The bootstrap **sources** — `LDR4K31.ASM`/`.PRN` and `LOAD10.ASM`/`.PRN` — live in
`tapes/4KBasic31/` and `tapes/MSBasic10/`: source rather than product, so they stay in the
repository and are not in the package.
