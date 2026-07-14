<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# The built-in machines

A built-in is a machine file that lives **inside the binary** — the same format you
would write yourself, with nothing special about it. Name one and you get it in any
directory on earth:

```
$ altairsim basic4k
```

`altairsim --list` prints this table, and `altairsim -x 'SHOW MACHINE' <name>` shows
what is actually in one.

| Machine | What it is |
|---|---|
| `4k` | The Altair as it actually left Albuquerque. |
| `altmon` | An Altair with a monitor in ROM and a terminal on it. |
| `basic4k` | The machine Altair 4K BASIC was sold to run on: a cassette in the ACR, a Teletype |
| `basic8k` | The machine Altair 8K BASIC was sold to run on: a cassette in the ACR, and a terminal |
| `default` | The machine you get when you name none: 56K, and the DBL boot PROM at FF00. |
| `minidisk` | The Altair Minidisk: an 88-MDS at 08, the MDBL boot PROM, and CP/M 2.2b on a 5.25" disk. |
| `ps2` | The machine MITS Programming System II ran on. It is `basic8k`'s CARDS -- same 2SIO, same |
| `ps2int` | MITS Programming System II, WITH INTERRUPTS. `ps2` with A9 down and an 88-VI/RTC in it. |

