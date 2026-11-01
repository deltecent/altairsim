# Vector Graphic Port Map — the I/O ports of each Vector Graphic board

Source: [Port Map.pdf](#)

Vector Graphic Inc., *Port Map*, Revision A, July 10, 1981, 4 PDF pages. This list gives the
port assignments of the Vector Graphic boards at their factory addresses. A board with address
jumpers can be set to another address, and this list does not show that. The
[Flashwriter II Manual](Flashwriter%20II%20Manual.md) §3 has a short form of this table.

**Reference only.** The list is not a board.

---

## 1. Port assignments

| Port | Board | Description |
|---|---|---|
| 00 | Flashwriter I, Flashwriter II | Console or keyboard status |
| 00 | Bitstreamer I | Parallel port A, bidirectional |
| 00 | Analog Interface | Input: look at comparators |
| 01 | Flashwriter I, Flashwriter II | Keyboard data |
| 01 | Bitstreamer I | Parallel port A, bidirectional |
| 01 | Analog Interface | Output: reset |
| 02 | Bitstreamer I, Bitstreamer II | Serial port A, data |
| 03 | Bitstreamer I, Bitstreamer II | Serial port A, status |
| 04 | Bitstreamer II | Serial port B, data |
| 04 | ZCB | Serial port A, data |
| 05 | Bitstreamer II | Serial port B, status |
| 05 | ZCB | Serial port A, status |
| 06 | Bitstreamer II | Serial port C, data |
| 06 | ZCB | Serial port A1, data |
| 07 | Bitstreamer II | Serial port C, status |
| 07 | ZCB | Serial port A1, status |
| 08 | Bitstreamer II | Parallel port, bidirectional |
| 08 | ZCB | Parallel port A, programmable |
| 09 | Bitstreamer II | Parallel port, bidirectional |
| 09 | ZCB | Parallel port B, programmable |
| 0A | ZCB | Parallel port C, programmable |
| 0B | ZCB | Parallel port, control |
| 10 | Precision Analog | Output: MSB of channel A. Input: comparator status |
| 11 | Precision Analog | Lower 4 bits, MSB of channel A |
| 12 | Precision Analog | Output: MSB of channel B |
| 13 | Precision Analog | Output: LSB of channel B |
| 14 | Precision Analog | Control |
| 40 | 64K Dynamic RAM, 16K Static RAM | Memory bank select |
| 7E | Video Digitizer | Output: T count high. Input: status |
| 7F | Video Digitizer | Output: T count low. Input: A data |
| 80, 81 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 82 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 82 | Clock/Calendar | Serial data |
| 83 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 83 | Clock/Calendar | Serial status |
| 84 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 84 | Clock/Calendar | Output: write data to clock. Input: read data from clock |
| 85 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 85 | Clock/Calendar | Output: incrementing clock |
| 86 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 86 | Clock/Calendar | Output: initializes or sets clock |
| 87 | 8 Port Parallel I/O | Parallel port, bidirectional |
| 87 | Clock/Calendar | Output: control |
| C0, C1 | Dual-Mode Disk Controller | Output: control ports. Input: status |
| C2 | Dual-Mode Disk Controller | Bidirectional data port |
| C3 | Dual-Mode Disk Controller | Output: start. Input: reset |
| D0 | Network Controller | Input: control hardware address |
| D1 | Network Controller | Output: load controller word. Input: read controller status word |
| D2 | Network Controller | Output: load buffer RAM. Input: read buffer RAM |
| D3 | Network Controller | Output: data, do not care. Input: reset |
| E0 | Floppy Disk Controller | Output: communication. Input: status |
| E1 | Floppy Disk Controller | Output and input: track |
| E2 | Floppy Disk Controller | Output and input: sector |
| E3 | Floppy Disk Controller | Output and input: data |
| E4 | Floppy Disk Controller | Output: drive select. Input: data request and interrupt request |
| E5 | Floppy Disk Controller | Input: data request wait |
| E6 | Floppy Disk Controller | Serial port A, data |
| E7 | Floppy Disk Controller | Serial port A, status |

**Note from the map:** the version 3 multiuser time share system occupies parallel ports 08–0F on
the Flashwriter board, in a five-user system (page 4).

---

## Quirks worth carrying

- **⚠ Q1. Nothing is listed from 15 to 3F.** The ports that OASIS 5.5A writes at 23–29 and 33–38
  are not factory addresses. They fit a [Bitstreamer II](Bitstreamer%20II.md) with its jumpers set
  to base 22 and base 32.
- **⚠ Q2. The Clock/Calendar and the 8 Port Parallel I/O share 82–87.** The two boards cannot both
  be at their factory address in one machine.
- **⚠ Q3. Time share ports.** The note gives 08–0F. The
  [Flashwriter II Manual](Flashwriter%20II%20Manual.md) §12 gives 0, 1, A, B, C, D, E and F for
  four boards, each with its own bank.
- **⚠ Q4. "Serial port A1" on the ZCB is not a second port.** The ZCB decodes no A1 at its 8251, so 06
  and 07 are copies of 04 and 05. See [ZCB CPU Manual](ZCB%20CPU%20Manual.md) §6.
