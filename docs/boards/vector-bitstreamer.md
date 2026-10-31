# Vector Graphic Bitstreamer II

**Status:** done for the serial channels, the interrupts and the clock. The parallel channels have no cable (see *Limitations*).

## The real hardware

The **Bitstreamer II** is a Vector Graphic S-100 I/O board. This model is Rev 1, from 1979. The board has three 8251 serial channels, two parallel channels and a 55 Hz real-time clock.

Area W sets the upper digit of the base address, so a base is `02`, `12` and so on to `72`. The board decodes eight ports from there. Interrupts are an option that a technician wires with jumpers in area A. The board comes with no interrupt jumpers.

## Sources

| Source | Path | Authority |
|---|---|---|
| Distilled emulation reference | `reference/Bitstreamer II.md` | The ports, the mask, the clock latch, the jumpers and the open questions |
| *Bitstreamer II Board*, Rev 1, manual Rev A, January 1980 | `Bitstreamer II.pdf` | The text |
| Schematic, Rev 1, 9-1-79 | `Bitstreamer_II_Schematic_Rev1.tif` | The clock latch, the mask gates and the parallel input buffers |
| Intel 8251 | `reference/Intel 8251 USART.md` | The serial channels |
| OASIS 5.5A nucleus | a disk image, not in the tree | `j4_d0`, which no Vector Graphic document shows |

## Register reference

The base is `port`, and it ends in 2. This table uses base `22`.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `22` | Serial A data | Serial A data |
| `23` | Serial A mode, then command | Serial A status |
| `24` | Serial B data | Serial B data |
| `25` | Serial B mode, then command | Serial B status |
| `26` | Serial C data | Serial C data |
| `27` | Serial C mode, then command | Serial C status |
| `28` | Parallel A latch, which is also the interrupt mask | The J4 lines |
| `29` | Parallel B latch | The J5 lines. The read also clears the clock latch |

| Mask bit | Source |
|---|---|
| 0 | Serial A TxRDY |
| 1 | Serial A RxRDY |
| 2 | Serial B TxRDY |
| 3 | Serial B RxRDY |
| 4 | Serial C TxRDY |
| 5 | Serial C RxRDY |
| 6 | The clock latch |

## How it is simulated

- **Decodes** an `IN` and an `OUT` at the eight ports.
- **Serial.** Three `Intel8251` chips, units `a`, `b` and `c`. `baud` is the eight-position switch of each channel.
- **The clock latch.** The clock is the 2 MHz bus clock divided by 13, 4, 22, 16 and 2. That is 54.6 Hz. The manual calls it 55 Hz, and 110 baud is really 109.3 baud. A `HostTicker` keeps the rate in host time at any `clock_hz` (issue #825). The latch sets on each tick and only an `IN` from `x9` clears it.
- **The mask does not gate the latch.** The ticker runs when `clock_int` or `j4_d0` is wired, whatever the mask holds. A set latch interrupts as soon as the guest unmasks it. With nothing wired there is no deadline, so a `HLT` can end a run.
- **Interrupts.** `clock_int`, and `rx_int` and `tx_int` of each channel, are the jumpers of area A. Each one is `none`, `int` or `vi0` to `vi7`, and the default is `none`. A source passes its mask bit first. No board answers the acknowledge, so the bus reads `FF`, which is `RST 7`.
- **TxRDY** as an interrupt also needs TxEN. The status bit does not.
- **`j4_d0`.** With `clock-latch`, bit 0 of `IN x8` reads pin 5 of U4, the Q output of the clock latch. It reads 1 while the latch is set, and the mask does not change it. With `open`, all bits read 1.
- **A machine file** is built on a scratch Clock. `clockAttached()` starts the ticker again on the machine's Clock.
- **DMA:** none.

## Properties and units

| Name | Meaning |
|---|---|
| `port` | Area W: `02`, `12`, `22`, `32`, `42`, `52`, `62` or `72`. Default `02` |
| `clock_int` | Where the clock latch goes. Default `none` |
| `j4_d0` | What drives pin 11 of J4: `open` (default) or `clock-latch` |
| `a`, `b`, `c` | Units for `CONNECT` |
| `baud` | Of a unit: 110 to 9600, the eight switch positions |
| `rx_int`, `tx_int` | Of a unit: where RxRDY and TxRDY go. Default `none` |

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Only an `IN` from `x9` clears the latch, and the data does not matter | The interrupt comes back at once, in a storm |
| The mask gates the output of the latch, and not the latch | A latch set while masked is lost, and the first interrupt comes late |
| The output of `x8` is both the parallel latch and the mask | A program that prints to parallel A changes the interrupts |
| RESET* reaches nothing on this board. POC* resets the 8251s | A reset that clears the mask stops the clock of a guest that survives a reset |

## Limitations and deliberate departures

- **`j4_d0` is inferred, and no Vector Graphic document shows it.** OASIS 5.5A reads bit 0 of `IN 28` to tell its clock from a key, and bit 0 = 1 means the clock. On the board, `IN x8` reads only the J4 cable. The *Time-Share Multi-User System B conversion package* may explain the wire. We do not have a copy. Pin 5 of U4 has the sense that OASIS needs, and the schematic leaves it with no connection. Pad B-3 has that sense only when pad A-1 feeds its gate. That gate then holds `PINT` low while the latch is clear, and a machine with the Flashwriter needs an open-collector clock. The gate gives the right `PINT` only with pin 5 of U4 at pad B-1, and B-3 then reads backward for OASIS (reference Q2).
- **Pin 73 follows the text of page 1-7, and not the gates of area A.** The text says that the latch interrupts while it is set. The gate in the manual's third wiring reads the other way, and the manual says that the difference has no effect (reference Q2).
- **No cable can be attached to J4 or J5.** The inputs read as open TTL inputs, which is 1. The output latches hold their bytes. The manual gives no strobe, so a byte stream does not fit.
- **The VI jumpers** are offered because area K has them. No Vector Graphic board used them at this date.
- **A byte vector with area L is not offered.** The acknowledge always reads `FF`.
- **The power-on state of the mask and the latches** is not in the manual. They come up clear.
- **The modem pins.** CTS is at ground as shipped, and the 8251 model gives the same result.

## Verification

`./build/altair_tests bitstreamer` checks the board with real bus cycles. It covers the port map, the period of the clock, each mask bit, the snapshot and the move from a scratch machine. In one test, a real Z80 takes the clock with `RST 7` once for each tick. A mutation of the `IN x9` clear and of each mask gate fails the suite.

OASIS 5.5A was checked once by hand on the `vector` machine. The routine at `26DF` ran 164 times in 3.0 seconds, which is 54.7 times a second. On that disk, `SHOW CLKFRQ` reads `55, 0`, and `SHOW RTCAVAIL` reads `ON`.

## References

- `docs/boards/vector-dualmode.md`, the disk controller.
- `docs/boards/vector-fwii.md`, the screen and the keyboard, which shares `RST 7`.
- `docs/boards/bankmem.md`, the banked RAM that OASIS needs.
