# The SSM IO-4 as a console

A machine whose console is an **SSM IO-4 (2P + 2S) I/O board** — SSM's combined
S-100 card carrying **two full-duplex serial channels *and* four latched 8212
parallel ports** on one board. Here the board's Serial A channel is the console,
and the machine boots the **SSM 8080 System Monitor** on it.

```
cd examples/io4
altairsim io4.toml
```

You land in the monitor:

```
MONITOR V1.0
.
```

The `.` is the monitor's prompt. Try its commands — for example `X` examines the
CPU registers. `^E` returns to the `altairsim>` prompt.

## Why it just works — the console straps

The SSM 8080 monitor's console driver expects a **MITS SIO Rev-0** serial port:
status and data at ports `0`/`1`, with "byte ready" on data bit 0 and the status
byte inverted. That is exactly the IO-4's **default profile, `altair-rev1`** (DAV
→ D0, TBMT → D7, status inverted through the 74LS368 buffer), so this machine sets
no serial straps at all — a stock `io4` is already strapped for this console.

The IO-4 is heavily jumpered on the real card, and every strap is a property here.
Had the monitor wanted a different port map or polarity you would pick another
`profile` (`i8251`, `altair-rev0`, `proctech`, `imsai`, or `custom`), or set the
individual `stat_*`, `invert_status` and `port_reversal` straps by hand.

## The rest of the board is along for the ride

`SHOW io40` (at the `altairsim>` prompt, before the machine runs) lists every unit
the one board provides:

- **Serial A** (`a`) — the console, at ports `0`/`1`.
- **Serial B** (`b`) — a second full-duplex channel, at ports `2`/`3`. Here it is
  wired to `null`; `CONNECT io40:b …` gives it a terminal, a file, a socket or a
  real serial port.
- **Parallel A / B** (`pa` / `pb`) — the two 8212 latched input/output ports, on
  their own 2-port block at `4`/`5`. `CONNECT` them to a byte source or sink the
  same way; each input carries a service-request flip-flop.

Each serial channel has real, programmable **word length, parity, stop bits and
baud** (the 1602-family UART's format pins), and the board's **W4 header** can
strap any channel's receive or transmit, or either parallel input, to a vectored
interrupt line — none of which this simple console demo needs.

## Notes

- The three UART error flags (parity, framing, overrun) are strappable to the
  status byte but always read inactive: the emulated serial line carries exact
  bytes and models no line noise, so there is nothing to flag.
- The current-loop and EIA/RS-232 electrical options on the real card are an
  electrical choice, not a programming one — they are documented but not modeled.
- The machine gives the monitor 60K of RAM (`0000`–`EFFF`) and the monitor's own
  2K EPROM at `F000`. The top of RAM must stop below the ROM: the monitor sizes
  memory at cold start and parks its stack at the top it finds, so an all-RAM
  machine would let it climb into its own image.

See the [SSM IO-4 reference](../../reference/SSM%20IO-4%202P%2B2S%20IO%20Board.md)
and the `io4` section of the User Manual's board chapter.
