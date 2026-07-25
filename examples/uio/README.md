# 88-UIO — two cards in one

```
cd examples/uio
altairsim uio.toml
```

You will land at Altair **8K BASIC 3.2**:

```
MEMORY SIZE? ⏎
TERMINAL WIDTH? ⏎
WANT SIN-COS-TAN-ATN? Y

10312 BYTES FREE

ALTAIR BASIC VERSION 3.2
[EIGHT-K VERSION]

OK
```

## What makes this different

The [`basic8k`](../../machines) machine needs **two** boards — an 88-2SIO for the console
terminal and an 88-ACR for the cassette. This machine has **one**: the MITS **88-UIO**, a
board that carries both.

- Its **6850 serial port** sits at **0x10** — exactly where 88-2SIO Port A lives — so it is
  the console.
- Its **cassette section** sits at **0x06** — exactly where an 88-ACR lives — so it is the
  tape.

Because both halves land at the standard addresses, the bootstrap MITS shipped
(`LDR8K32.HEX`, toggled in at address 0) runs **unmodified**, and 8K BASIC never learns it
is talking to a combined card. The sense switches are `0x8C` (A15 = load from cassette,
A11+A10 = 2SIO-style terminal), straight from the loader's own header.

## Try it

`⏎` at each prompt takes the default, `Y` enables the math functions, then type a program:

```basic
10 PRINT 6*7
20 PRINT "TAPE OK"
RUN
```

Things the UIO can do that a plain ACR cannot:

```
SHOW uio0                 ; see both halves: serial_port, port, standard, motor
SET uio0 standard=kansas  ; SW-1: switch the modem to the Kansas City standard (2400/1200)
REWIND uio0:tape          ; the cassette has motor control, but rewinding is still your finger
```

`⌃E` (ATTN) returns you to the `altairsim>` monitor from BASIC at any time.

## What is in here

| File | What it is |
|---|---|
| `uio.toml` | the machine — one 88-UIO, 16K of RAM, the front panel, the console |
| `8K BASIC Ver 3-2.tap` | Altair 8K BASIC 3.2, a period cassette image |
| `LDR8K32.HEX` | the 8K bootstrap loader, toggled in at 0 |

The tape comes off in about a second because `clock_hz = 0` is flat out; `SET cpu0
clock_hz=2000000` buys back the real 2 MHz machine and the ~90 seconds a 300-baud cassette
actually took.
