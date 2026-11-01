# Vector Graphic Bit Streamer

**Status:** done for the 8251, the parallel output latches and the port address. See *Limitations*.

## The real hardware

The **Bit Streamer** is an S-100 I/O board with one Intel 8251 and two parallel ports. It is the
board before the Bitstreamer II. It has no clock and no interrupt logic. An eight-position switch
selects the baud rate.

## Sources

| Source | Path | Authority |
|---|---|---|
| Distilled emulation reference | `reference/Bitstreamer I.md` | The ports, the baud chain, the serial setup and the connectors |
| Intel 8251 data sheet | `reference/Intel 8251 USART.md` | The serial chip |

## Register reference

| Addr | OUT (write) | IN (read) |
|---|---|---|
| base+0 | Parallel latch A | J2 port A lines |
| base+1 | Parallel latch B | J2 port B lines |
| base+2 | 8251 transmit data | 8251 receive data |
| base+3 | 8251 mode word, then command | 8251 status |

## Properties

| Property | Values | Default | Meaning |
|---|---|---|---|
| `port` | A multiple of 4 | `00` | The base, set on the board by the pads beside U7 |
| `serial` `baud` | 110 to 9600 | 9600 | The switch, for a clock factor of 16 |
| `serial` `connect` | An endpoint | none | The serial line |

## Implementation

The baud chain is the same as on the ZCB, so `src/boards/vector-baud.h` holds it for both boards.

## Limitations

- No cable can be attached to J2, so the parallel inputs read `FF`.
- The U18 keyboard latch is not modeled, because it reaches port A through a wire on J2.
- The 20 mA current loop is the same line as RS-232 here.
- The two-output modification on p. 14 of the manual is not modeled.

## Quirks

- The output latches are 74LS75s with no clear, so a reset leaves them unchanged.
- The default base `00` puts the 8251 at `02` and `03`, the console ports of Vector Monitor 4.0c and of the Port Map. The pads cannot set A0 or A1, so a base of `02` is not possible.
