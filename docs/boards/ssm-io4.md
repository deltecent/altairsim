# SSM IO-4 (2P + 2S) — two serial and four parallel ports

**Status:** done — both serial channels (real 1602-family UARTs, the full W1/W2 status strap-up,
port reversal, the word-format switches), all four 8212 parallel ports with their
service-request flip-flops and the §3.2.2 status/data console strap, the S3/S4 address
switches with their overlap rule, and the W4 interrupt header. Board type `io4`. See
`tests/test_io4.cpp`.

**Not modelled:** the current-loop and EIA electrical options, separate RX and TX rates on the
W3 header, the UART error flags (they read inactive), and the parallel *output* handshake. See
*Limitations*.

## The real hardware

The **IO-4** ("10-4, 2P + 2S" in its own manual) is an S-100 I/O board from **SSM
Microcomputer Products**, formerly **Solid State Music**. The board is © 1977 and the manual is
dated 3-19-79. It puts two unrelated sections on one board:

- **Serial:** two independent full-duplex asynchronous channels, each a 40-pin UART —
  **U9 = Serial A, U8 = Serial B**, a TMS6011 / AY5-1013 / TR-1602. Each channel has its own
  word-format DIP switch (**S2 = Serial A, S1 = Serial B**), its own status header (**W2 = A,
  W1 = B**), its own status buffer (**U18 = A, U16 = B**), and its own line interface: an
  opto-isolated 20/60 mA current loop or EIA RS-232 (DS1488 driver).
- **Parallel:** four latched 8-bit ports on **8212** latch-buffers — two in (**J4**, **J6**)
  and two out (**J3**, **J5**) — each input with a service-request flip-flop.

Each section has its own address switch: **S3** puts the serial section on a 4-port block
(A7–A2), **S4** puts the parallel section on a 2-port block (A7–A1). If the two blocks overlap,
**neither section answers** the shared ports.

What made the board popular is the **status-word strap-up**. Six UART status signals come to a
16-pin header and can be jumpered to any data-bus bit. The status buffer can be a 74367
(positive sense) or a 74368 (every bit inverted). A PR switch per channel swaps the status and
data addresses. Together these let one IO-4 answer like almost any other board's serial port,
and the manual's §4 gives the strapping for the common ones.

## Sources

| Source | Path | Authority |
|---|---|---|
| SSM *IO4 2 Parallel & 2 Serial I/O Board* manual, 3-19-79 (a scan with no text layer, read as page images) | `reference/SSM IO-4 2P+2S IO Board.md` | **Authoritative** for everything on this page: the switches, the W1–W4 header pinouts, the status signals, the address decodes, the parallel handshake, the §4 application recipes |
| SMC COM2502/COM2017 UART data sheet, the same 1602-family part | `reference/com2502.md` (`com2502.pdf` in `docs/sources.md`) | **Authoritative** for the chip: the status signals, `/RDAR`, `/TDS`, MR. Modelled in `src/chips/uart1602.{h,cpp}` |
| SSM 8080 System Monitor V1.0 | `roms/SSM-8080MON/` | **Period software** whose console driver sets the default profile: it waits while D0 = 1 for a byte and while D7 = 1 to send |

The reference is the hardware authority. Where this page and the reference disagree about the
hardware, the reference wins.

## Register reference

### Serial section — a 4-port block at `port` (switch S3, default `00`)

| Addr | OUT (write) | IN (read) |
|---|---|---|
| BASE+0 | Serial A control — **accepted and discarded** (the UART has no control register) | Serial A status byte, shaped by W2 + U18 |
| BASE+1 | Serial A transmit data (`/TDS`) | Serial A receive data (`/RDAR` — clears DAV) |
| BASE+2 | Serial B control — discarded | Serial B status byte, shaped by W1 + U16 |
| BASE+3 | Serial B transmit data | Serial B receive data (clears DAV) |

With a channel's **PR** switch on (`port_reversal`), that channel's two addresses swap: data
first, status second. Each channel reverses on its own.

### The status byte

Six UART signals, each jumpered to one data bit or left off (`stat_dav`, `stat_ror`,
`stat_rpe`, `stat_rfe`, `stat_teoc`, `stat_tbmt`: `0`–`7` or `none`):

| Signal | W1/W2 pin | Meaning |
|---|---|---|
| DAV (ODA) | 4 | a received character is waiting |
| ROR | 3 | receiver overrun |
| RPE | 2 | receiver parity error |
| RFE | 7 | receiver framing error |
| TEOC | 6 | transmitter end of character (the shift register is empty) |
| TBMT | 5 | transmitter buffer empty (ready for the next character) |

The polarity is set by the buffer chip: a 74367 reads asserted as 1, a 74368 reads asserted as
0 (`invert_status`). A bit that no signal is strapped to reads 0.

The `profile` property presets the map, the polarity and PR together from the manual's §4:

| Profile | Manual | DAV | TBMT | Others | Sense | PR |
|---|---|---|---|---|---|---|
| **`altair-rev1`** (default) | §4.2 | D0 | D7 | — | inverted (74368) | off |
| `altair-rev0` | §4.3 | D5 | D1 | — | positive | off |
| `i8251` | §4.1 | D1 | D0 | TEOC D2, RPE D3, ROR D4, RFE D5 | positive | off |
| `proctech` | §4.4 | D6 | D7 | — | positive | off |
| `imsai` | §4.5 | D1 | D0 | — | positive | **on** |

`profile` reads back whichever row the live straps match, or `custom`. The straps are the real
settings; the profile name is only read back from them.

### Parallel section — a 2-port block at `par_port` (switch S4, default `04`)

| Addr | OUT (write) | IN (read) |
|---|---|---|
| PAR+0 | Parallel A output latch (J5, U12) | Parallel A input latch (J6, U13) — **clears its service request** |
| PAR+1 | Parallel B output latch (J3, U10) | Parallel B input latch (J4, U11) — clears its service request |

The manual's §3.2.2 status/data idiom puts a data-available flag on one bit of a parallel
port's read, taken from that port's service request or its sibling's. That is wiring on the J
connectors, so it is a set of per-port straps: `dav_bit` (`0`–`7` or `none`), `dav_source`
(`self` or `sibling`) and `dav_active_low`.

### Interrupts — header W4

| Source | Unit property | Canonical W4 line | Asserted while |
|---|---|---|---|
| Serial A receive | `a` `rx_int` | VI1 | DAV |
| Serial B receive | `b` `rx_int` | VI0 | DAV |
| Serial A transmit | `a` `tx_int` | VI2 | TBMT |
| Serial B transmit | `b` `tx_int` | VI3 | TBMT |
| Parallel in A | `pa` `int` | VI6 | the service request is set |
| Parallel in B | `pb` `int` | VI5 | the service request is set |

Each strap takes `none` (the default), `int` (pin 73, PINT) or `vi0`–`vi7`. The canonical
column is the manual's W4 map; the simulator does not force it, because W4 is a header of
jumpers.

## How it is simulated

- **Bus cycles:** `IoRead` and `IoWrite` only, never memory. `sectionAt()` decides which section
  owns a port and returns *neither* for a port both blocks cover. It is the only place the
  overlap rule is written.
- **The UARTs** are `Uart1602` chips (`src/chips/uart1602.h`), one per channel, in true sense.
  Everything that makes the status byte look like another board — the map, the polarity, PR —
  belongs to the board and is applied in `Io4Board::statusByte()` (`DESIGN.md` §7.8, *A chip
  is not a card*).
- **Word format** (`data_bits` 5–8, `parity` none/odd/even, `stop_bits` 1–2 — the S1/S2
  switches) and **`baud`** are per channel. They pace the emulated line, and they are
  programmed onto a real serial port when one is connected. After a write to an idle line, TBMT
  is clear for one period of the 16x clock, until the byte moves into the shift register. TEOC
  then clears for one character time. Received characters arrive at no more than the line rate.
- **Parallel ports** are units `pa` and `pb`, one bidirectional `ByteStream` each. A byte the
  far end sends *is* the external strobe: `pump()` latches it into the input latch and sets the
  service request. The latch is one byte deep, so the next byte waits until the guest reads
  this one. A guest write sets the output latch and sends the byte down the line.
- **Streams:** four units (`a`, `b`, `pa`, `pb`), each a `ByteStream` — so a file, a socket, a
  real serial port, `in:`/`out:`, `null` or `loopback` all work. An unconnected unit holds a
  `NullStream`, never a null pointer.
- **Interrupts** are combinational: `assertsInt()` and `assertsVi()` read the live UART and
  service-request state through the W4 straps. There is **no software enable** on this board,
  so a strap wires the source's level straight to the bus. The board's own `Clock` alarm
  (`refresh()`/`nextEdge()`) wakes it when a strapped source can change with nobody touching
  the board: a transmitter finishing, or a paced character arriving. The alarm is cancelled in
  the destructor.
- **No bus mastering. No media.**
- **Properties:** board-level `port` (must be a multiple of 4) and `par_port` (must be a
  multiple of 2); per serial unit `profile`, the six `stat_*`, `invert_status`,
  `port_reversal`, `baud`, `data_bits`, `stop_bits`, `parity`, `rx_int`, `tx_int`, `connect`;
  per parallel unit `dav_bit`, `dav_source`, `dav_active_low`, `int`, `connect`.

### Reset

`Reset::PowerOn` and `Reset::Bus` do the same thing:

- Each UART takes **MR** (pin 21): TBMT and TEOC set, DAV and the error flags cleared.
- Each parallel port's input latch, output latch and service request are cleared.
- **The lines stay connected.** A warm reset does not unplug a terminal or a ribbon cable.
- Every interrupt source is now inactive; the wires are driven again and the alarm is re-armed.

SNAPSHOT carries the UARTs, the four 8212 latches and the service requests. The straps are
config and are re-applied from the machine file.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The default profile is `altair-rev1`: DAV on D0, TBMT on D7, **both inverted** | The SSM 8080 monitor waits while D0 = 1 and while D7 = 1. With positive sense its console hangs at power-on, before printing anything |
| Reading the serial **data** port clears DAV; reading the status port does not | A driver that polls status in a loop would either see one character forever or lose characters |
| A write to the serial **status** port is ignored | The 1602 has no control register. Treating the write as one would let init code that "programs a UART" change the format, which the real board sets with DIP switches |
| PR swaps a channel's two addresses | IMSAI software reads status at the higher address. Without PR it reads the data port as status |
| Overlapping S3 and S4 blocks: **neither** section answers the shared ports | Letting one section win would make a mis-strapped machine work when the real board would go silent |
| A parallel input raises its interrupt **even when the port is not addressed** | An interrupt-driven parallel reader would never be woken |
| The transmit interrupt is a **level**, held while TBMT is set | Treating it as an edge loses the "ready for the next character" request that interrupt-driven output relies on |
| No software interrupt enable — the W4 strap is the enable | Adding an enable bit would give the board a register it never had (`DESIGN.md` §0.1) |
| A bit no signal is strapped to reads **0**, even with `invert_status` on | Inverting the undriven bits too would put 1s in positions a driver masks off and never expects to see set |

## Limitations and deliberate departures

- **ROR, RPE and RFE always read inactive.** They report line noise, and a `ByteStream`
  delivers the byte that was sent or nothing. Making up a noise rate would mean inventing a
  number (`DESIGN.md` §0.1). ROR has a second reason: a stream holds a byte until the board
  takes it, so an overrun would *manufacture* data loss that the host link does not have (see
  `Uart1602::poll()`). They can still be strapped, so a driver that masks them (the `i8251`
  profile) sees the right byte shape. Software that tests its own error handling by forcing
  a framing error cannot do that here.
- **One `baud` per channel.** W3 straps RX and TX separately, so a real channel could send
  and receive at different rates. A host serial port cannot be split that way, so both
  directions share one rate. `baud` accepts any rate from 50 to 25000, not only the ten W3
  rates (55–9600), and the rate is counted in the guest's T-states rather than divided down
  from the bus's 2 MHz Φ2. The §4.7 (Selectric 133.5 baud) and §4.11 (PolyMorphic ÷12) board
  mods come for free as a `baud` value.
- **Current loop, EIA, and the §4.8–§4.10 line mods are not modelled.** They are electrical
  choices, not something a program can see.
- **The parallel output handshake is not modelled.** The manual gives the output ports a
  service-request flip-flop worked by the strobe line, but an output here only latches and
  sends. Software that waits for a printer's ACK on an output port's flip-flop would not see it.
  The input side is complete.
- **Each parallel port is one bidirectional line.** On the real board, J6 (in) and J5 (out) are
  separate ribbon cables. Here both directions of Parallel A share one endpoint, as a serial
  line does.

## Verification

- `test_io4` (unit, `./build/altair_tests io4`): the four units; the S3 and S4 block rules;
  every profile's byte shape; `invert_status` and `port_reversal`; a custom map; the data path
  both ways and the discarded control write; channel independence; per-channel word format;
  the single `baud`; the parallel strobe/ack, output latch, the §3.2.2 console strap and the
  overlap rule; reset clearing the latches. It then **boots the SSM 8080 System Monitor** on
  Serial A of a hand-built machine and reads its sign-on back. The interrupt sections cover W4
  unstrapped, a receive on pin 73 and on a VI line, the transmit level, an unaddressed parallel
  strobe, several straps at once, a board with no clock, and two end-to-end runs where an 8080
  behind an 88-VI vectors to `RST 2` (Serial A receive on VI2) and `RST 6` (a parallel strobe
  on VI6).
- `acceptance-io4`: boots `tests/media/io4/io4.toml`, copied out of the tree, through the
  real CLI, and checks the SSM monitor's sign-on on the IO-4 console.

## References

- `reference/SSM IO-4 2P+2S IO Board.md` — the distilled manual.
- `docs/manual/boards.md` §`io4` — the user's view.
- `docs/devguide/serial-io.md` — *Adding an IO-4 profile*, and the two kinds of serial board.
- `docs/boards/mits-88sio.md` — the board the `altair-rev1` and `altair-rev0` profiles imitate.
