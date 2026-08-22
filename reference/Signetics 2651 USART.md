# Signetics 2651 USART

Source: [CompuPro System Support 1 Manual.pdf](#) (the 2651 programmer-visible model as
documented in the System Support 1 manual, pp.21–26; provenance in `docs/sources.md`). The
standalone Signetics/National 2651 (2661) datasheet — electricals, timing, the sync-mode and
loopback operating modes — is on bitsavers if ever needed; only the async register model the
host board uses is kept here.

The **2651** (and the pin/register-compatible **2661**) is a CMOS **Programmable Communications
Interface** — a UART with an **on-chip baud-rate generator** and full RS-232 handshaking
(RTS, CTS, DSR, DTR, DCD). It appears on the
[CompuPro System Support 1](CompuPro%20System%20Support%201.md) as the serial channel; the
part is marked "2651 or 2661-3" in the SS-1 parts list. This is a distilled emulation reference
for the asynchronous register model, not the electricals.

Unlike the [Intel 8251](Intel%208251%20USART.md), the 2651 decodes **A1/A0 into four separate
port addresses** — data, status, mode and command — so there is no data/status write-target
ambiguity. The one piece of internal sequencing is the **mode register pointer**: MR1 and MR2
share the mode address, and a one-bit pointer routes the first mode access to MR1, the next to
MR2, then wraps. The pointer is reset to MR1 by chip reset.

## Register map (A1/A0)

| A1 A0 | Read | Write |
|:---:|---|---|
| 0 0 | Data — received word | Data — word to transmit |
| 0 1 | Status register | SYN1/SYN2/DLE (sync only; unused in async) |
| 1 0 | Mode register (MR1 then MR2, pointer-sequenced) | Mode register (MR1 then MR2) |
| 1 1 | Command register | Command register |

## Status register (read)

| Bit | Flag | Meaning | Polarity |
|:--:|---|---|---|
| 0 | **TxRDY** | Transmit holding register empty | **active-high** (1 = ready) |
| 1 | **RxRDY** | A received character is waiting | **active-high** (1 = available) |
| 2 | TxEMT/DSCHG | Transmit shift register empty, **or** DCD/DSR changed | active-high |
| 3 | PE | Parity error | active-high |
| 4 | Overrun | A character arrived before the last was read | active-high |
| 5 | FE | Framing error (no stop bit) | active-high |
| 6 | **DCD** | **1 = the DCD line is asserted (driven low)** | inverting reporter of an active-low line |
| 7 | **DSR** | **1 = the DSR line is asserted (driven low)** | inverting reporter of an active-low line |

The subtlety is bits 6/7: unlike TxRDY/RxRDY/PE/OE/FE, the DCD and DSR status bits are
**inverting reporters of active-low RS-232 lines** — a set bit means the modem-control input is
asserted (driven low). For a byte-clean transport with no modem-control simulation, model bits
6/7 as **asserted (1)** and PE/OE/FE as **0** (there is no line noise to report).

## Mode Register 1 (the frame)

| Bits | Field | Values |
|:--:|---|---|
| 1,0 | Mode / baud factor | `10` = 16× async (the only mode this board uses) |
| 3,2 | Character length | `00`=5, `01`=6, `10`=7, `11`=8 |
| 4 | Parity enable | 0 = none, 1 = parity generated/checked |
| 5 | Parity type | 0 = odd, 1 = even (ignored if bit 4 = 0) |
| 7,6 | Stop bits | `01`=1, `10`=1½, `11`=2 (`00` invalid) |

## Mode Register 2 (the baud rate)

Bits 3–0 select the on-chip baud-rate generator; bits 7–4 are a board-fixed `0111` with no
software effect (a System Support 1 convention).

| 3–0 | Baud | 3–0 | Baud |
|:--:|:--:|:--:|:--:|
| 0000 | 50 | 1000 | 1800 |
| 0001 | 75 | 1001 | 2000 |
| 0010 | 110 | 1010 | 2400 |
| 0011 | 134.5 | 1011 | 3600 |
| 0100 | 150 | 1100 | 4800 |
| 0101 | 300 | 1101 | 7200 |
| 0110 | 600 | 1110 | 9600 |
| 0111 | 1200 | 1111 | 19200 |

## Command register (read/write)

| Bit | Name | 1 | 0 |
|:--:|---|---|---|
| 0 | Transmit Control | transmitter enabled | disabled |
| 1 | **DTR** | /DTR output driven **low** (asserted) | /DTR high |
| 2 | Receive Control | receiver enabled | disabled |
| 3 | Force Break | TxD held spacing | normal |
| 4 | Reset Error | clear PE/OE/FE | normal |
| 5 | **RTS** | /RTS output driven **low** (asserted) | /RTS high |
| 7,6 | Operating mode | `00` = normal (async); other codes select echo/loopback |

Command bits 1 (DTR) and 5 (RTS) follow the same inverted convention as status bits 6/7:
setting the command bit **high** drives the modem-control **output low** (asserted).

## Initialization sequence

1. Write **Mode Register 1** (frame). 2. Write **Mode Register 2** (baud). 3. Write the
**Command** register (enable Tx/Rx, drive DTR/RTS). 4. Poll status or enable interrupts.

The SS-1 manual's own sample for 9600-8N... 8 data bits, 2 stop, no parity, RTS and DTR low is
`MR1 = 0xEE`, `MR2 = 0x7E`, `CMND = 0x27` (cross-checked bit-by-bit against the tables above).

## Emulation notes

- **The baud rate is on the chip**: the guest's MR2 write sets the line rate, so pacing follows
  MR2 — unlike an 8251 fronted by an external baud generator. (This is how
  `src/chips/sig2651.{h,cpp}` does it; the board's `baud` unit property seeds the line until the
  guest programs MR2, then MR2 overwrites it.)
- **Dispatch by address, not by an internal write-target flag** — the four ports remove the
  8251's data/control ambiguity. The only internal state is the MR1/MR2 pointer.
- A character occupies the line for one frame-time (1 start + data + parity? + stop bits) at the
  programmed baud; model TxRDY as a deadline and clock a receive frame in over that same time,
  exactly as the [Intel 8251](Intel%208251%20USART.md) model does. Do not synthesize overrun —
  hold an unread byte in the flow-controlled stream instead of losing it.
- TxRDY/RxRDY drive the interrupt logic; on the System Support 1 they feed the on-board 8259A
  controllers rather than an S-100 interrupt line directly.
