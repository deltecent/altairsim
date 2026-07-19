# Altair 4K BASIC off a cassette

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
tape in and press PLAY (`MOUNT`), toggle in the bootstrap (`LOAD`), and run it from zero.

## The files

| File | What it is |
|---|---|
| `basic4k.toml` | The operator. `base = "basic4k"` is the hardware (see `machines/basic4k.toml`); this file puts the tape in the recorder. |
| `4K BASIC Ver 3-1.tap` | The cassette. |
| `LDR4K31.HEX` | The period bootstrap, assembled. |

The tape comes off in about a second rather than the 110 the real 300-baud cassette took, because
the default clock is flat out. `SET cpu0 clock_hz=2000000` buys back the 2 MHz machine **and** its
110 seconds — BASIC cannot tell the difference either way.

`LDR4K31.ASM` and its listing are in `tapes/4KBasic31/`: source rather than product, so they stay
in the repository and are not in the package.
