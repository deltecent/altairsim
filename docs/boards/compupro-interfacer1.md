# CompuPro Interfacer 1

**Status:** done for the serial channels as software sees them. The current-loop and TTL line
options, and the baud generator as a clock source, are not modeled (see Limitations).

## The real hardware

The Interfacer 1 (Godbout Electronics / CompuPro, board 133H, manual dated 1980-81) is a
two-channel serial board. Each channel is a 1602-family UART ("1602, AY-5-1013 OR SIMILAR" on
the schematic; the parts list gives TR1863-P) with RS-232, current-loop and TTL line drivers.
A BR1941/2941 baud generator with a 5.0688 MHz crystal gives each channel one of sixteen rates.

It was the usual console board of a CompuPro system before the Interfacer II/3/4 and the System
Support 1 serial channel. The CompuPro CP/M 2.2 BIOS polls channel A at ports 00/01.

Configuration in period:

- **S2 (A) / S3 (B):** positions 1-7 compare against A1-A7, so each channel is a 2-port block
  at any even address. Position 8 ON removes the channel from the I/O space.
- **J14:** factory trace B-C puts data at the switch address and status at +1. Cut it and
  jumper A-B to swap them, for both channels at once.
- **S1:** positions 1-4 select channel A's rate and 5-8 channel B's.
- **J7/J9 (A), J8/J10 (B):** for each of the eight control lines, one of two traces is cut.
  The trace left in sets the line's power-up level.
- **J12 (A) / J13 (B):** status bit D2 is the RS-232 CF input (B-A) or the UART's End Of
  Character (B-C).
- **J11:** each of the four interrupt outputs can be jumpered to one of VI0-VI7.
- **J3-J6:** RS-232 master/slave (DTE/DCE) crossings, or current loop.

## Sources

| Source | Path | Authority |
|---|---|---|
| *Interfacer 1* manual, CompuPro/Godbout, 133H, 16 pp. with the two-sheet schematic | `reference/CompuPro Interfacer 1.md` (distilled from `CompuPro Interfacer 1.pdf`) | Every fact on this page. Port and bit tables pp. 4-5, straps p. 5, interrupts pp. 5 and 13, schematic pp. 8-10 |

The manual's own tables do not give the polarity of the RS-232 input bits. The schematic
settles it: CB, CC and CF pass through a 1489 receiver (U2/U6, inverting) and then the 81LS95
status buffers (U27/U28, non-inverting), so an asserted (ON) line reads 0. A printed copy of the manual confirms it: U27/U28 are 81LS95/97, with bubbles only on the enables at pins 1 and 19. The same buffers
carry DAV and TBMT without inversion, which agrees with the CompuPro BIOS (`ANI 01H` for
transmit ready, `ANI 02H` for receive ready).

## Register reference

Per channel, at the factory J14 setting (`port` = the switch address P):

| Addr | OUT (write) | IN (read) |
|---|---|---|
| P | transmit a character | the received character; clears DAV |
| P+1 | control latch | status byte |

Control latch (each 1 inverts the line from its strap; each 0 returns it to the strap):

| Bit | Line | Level 0 | Level 1 |
|---|---|---|---|
| D0 | RXINTE | receive interrupt off | on |
| D1 | TXINTE | transmit interrupt off | on |
| D2 | CD (DTR) | spacing (ON) | marking (OFF) |
| D3 | CA (RTS) | spacing (ON) | marking (OFF) |
| D4 | TSB | 1 stop bit | 2 stop bits |
| D5 | NP | parity | no parity |
| D6 | EPS | odd | even |
| D7 | NBI | 7 data bits | 8 data bits |

Status byte:

| Bit | Signal |
|---|---|
| D0 | TBMT, transmit buffer empty (1 = ready) |
| D1 | DAV, data available (1 = a character waits) |
| D2 | OPT: CF (0 = ON) or EOC (1 = idle), by `opt` |
| D3 | PE, parity error |
| D4 | OR, overrun |
| D5 | FE, framing error |
| D6 | CC, RS-232 DSR in master mode (0 = ON) |
| D7 | CB, RS-232 CTS in master mode (0 = ON) |

## How it is simulated

- **Decode:** I/O read and write cycles whose A7-A1 match an enabled channel's `port`. If both
  channels are set to the same block, the real board drives the bus from both UARTs. The bus
  sees one board and cannot report that, so channel A answers, B is unreachable, and the board
  writes a warning to the log.
- **The UART** is the shared `Uart1602` chip model. The live control levels drive its format
  pins: NB1 (NB2 is tied high, so 7 or 8 bits), EPS, NP and TSB. A change in format
  reprograms a real serial port on the far end.
- **The modem lines** go to the channel's `ByteStream`: CA to RTS and CD to DTR through
  `setControl()`, and CB/CC/CF from CTS/DSR/carrier in `status()`.
- **Interrupts:** RxINT = DAV AND RXINTE, TxINT = TBMT AND TXINTE, each strapped by `rx_int` or
  `tx_int` to a VI line. The board sets a clock deadline for the next moment an enabled,
  strapped source can change by itself (a character that finishes leaving, or one that
  finishes arriving).
- **Properties:** board `swap_ports`; per unit (`[board.unit.a]`, `[board.unit.b]`) `port`,
  `enabled`, `baud`, the eight straps (`data_bits`, `parity_sense`, `parity`, `stop_bits`,
  `ca`, `cd`, `tx_int_enable`, `rx_int_enable`), `opt`, `rx_int`, `tx_int`, `control`
  (read-only) and `connect`.

### Reset

- `Reset::PowerOn` and `Reset::Bus` do the same thing, because POC and pRESET both clear the
  74LS175 latches: every line returns to its strap, and each UART gets a master reset (DAV
  cleared, transmitter free). The lines stay connected.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The control port **flips** the straps; it does not set the levels. Writing 0 restores the power-up setting. | The manual's own test program starts with `OUT 01H` of `00` to put the port back to its jumpers. If the board set the levels instead, that write would select 7 bits, odd parity and RTS/DTR on, whatever the jumpers say. |
| An asserted RS-232 input reads **0**. | A driver that waits for CTS would hang, or never wait. |
| "Spacing" on CA/CD is ON. | Writing a 1 to D2 drops DTR, which closes a socket or hangs up a modem on the far end. |
| J11 reaches VI0-VI7 only. `rx_int`/`tx_int` do not offer `int`, so a machine file that asks for it gets the usual "must be one of" error. | A machine file could ask for pin-73 interrupts that the board cannot make. |
| The transmit interrupt is a level held while TBMT is set. | A guest that enables TXINTE with nothing to send takes the interrupt at once and again after every return. |

## Limitations and deliberate departures

- **No line errors.** PE, OR and FE always read 0, as in every `Uart1602` board: a
  `ByteStream` delivers a byte or nothing.
- **An unconnected RS-232 input reads ON (0).** On the real board an open 1489 input reads
  OFF (1). The streams report a missing line as asserted, which keeps a console with nothing
  on CTS working. A driver that waits for CTS to drop before it sends would notice.
- **Current loop, TTL and the master/slave crossings (J3-J6) are not modeled.** The board
  always behaves as an RS-232 port in master mode: CB = CTS, CC = DSR, CF = carrier.
- **134.5 baud runs at 134.** The UART model takes a whole number of baud.
- **Overlapping channels are not a bus fight.** See Decode above: A answers and a log line
  warns.
- **No `sINTA`.** The manual says the board does not use it. An interrupt drops when the guest
  reads the character or writes one, as on the real board.

## Verification

- `tests/test_interfacer1.cpp` (`altair_tests interfacer1`) drives real bus cycles: decode,
  S2/S3, J14, channel disable; status bits and DAV clear on read; the XOR latch, its restore
  and its reset; CA/CD to CB/CC/CF through a loopback plug, and EOC on J12; RX and TX
  interrupts gated by the enables on VI lines, `int` refused; a snapshot round trip, and a
  RESTORE that reprograms the host line; reset returning the modem lines; the A/B overlap
  warning, once; the far end's messages in the log; and a machine file that loads, drives VI2
  on the bus and comes back out of CONFIG SAVE.
- CompuPro CP/M 2.2LD and a custom CP/M 2.2N BIOS boot with an `interfacer1` in place of the
  `gsio` console in the `disk1` machine.

## References

- `docs/boards/compupro-disk1.md`, the floppy controller this board's console serves.
- `src/chips/uart1602.h`, the UART model.
