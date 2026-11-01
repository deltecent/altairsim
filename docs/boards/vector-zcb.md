# Vector Graphic ZCB

**Status:** done for the processor, the reset path, the PROM sockets, the RAM, the 8251 and the mode 0 of the 8255. The block address jumpers, the memory disable pads and the handshake modes of the 8255 are not modeled (see *Limitations*).

## The real hardware

The **ZCB** is a Vector Graphic S-100 single board computer from 1980. One board holds a Z-80A at 4 MHz, 1K of RAM at `FC00` and three PROM sockets in the `E000` block. It also holds an Intel 8251 serial port and an Intel 8255 parallel port. Later boards have a Z-80B at 6 MHz. The board has no interrupt source of its own.

Jumpers set the board up. Areas E and G set the ports. Area F joins the PROMs to the 1K address lines. Areas H and I set the memory block. Jumpers B and C control the reset path. Area N sets the wait states. Switch S1 sets the baud rate.

## Sources

| Source | Path | Authority |
|---|---|---|
| Distilled emulation reference | `reference/ZCB CPU Manual.md` | The reset logic of sheet 4, the decode, the ports, S1, the jumpers and the conflicts in the manual |
| Bulletins and the revisions of the board | `reference/Vector Graphic ZCB.md` | Which PROM layout goes with which board |
| Intel 8255A data sheet, 1979 | `reference/Intel 8255.md` | The mode word, the port directions, the bit set and reset, the reset state |
| Intel 8251 data sheet | `reference/Intel 8251 USART.md` | The serial chip |
| Monitor 4.3 | `roms/VGMON43/VGMON43.BIN` | The check of the reset path and the serial init: it starts from 0000 and programs the 8251 through 05 and 07 |

The manual gives port 04 as control on p. 2-1 and as data on p. 2-7. Data at 04 won, because sheet 7 and `mon40c` show it. The Port Map lists a second serial port at 06 and 07. The manual and sheet 7 show one 8251 with A1 not decoded, and that won. The manual says that TxRDY can interrupt. Sheet 7 marks the pin as no connection, and that won. `reference/ZCB CPU Manual.md` lists each conflict.

## Register reference

Both chips are in one group of 32 ports. The port is `group` + `serial_port`, or `group` + `parallel_port`. These are the default ports:

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `04` | 8251 transmit data | 8251 receive data |
| `05` | 8251 mode word, then command | 8251 status |
| `06`, `07` | Same as `04`, `05`, because A1 does not reach the 8251 | Same as `04`, `05` |
| `08` | 8255 port A | 8255 port A |
| `09` | 8255 port B | 8255 port B |
| `0A` | 8255 port C | 8255 port C |
| `0B` | 8255 mode word (bit 7 = 1), or port C bit set and reset (bit 7 = 0) | `FF`. The data sheet calls this read illegal |

| Memory | Read | Write |
|---|---|---|
| `E000`-`EFFF` | PROM, where a socket has a chip. Other addresses go to the bus | Goes to the bus only |
| `F000`-`FBFF` | The bus | The bus |
| `FC00`-`FFFF` | On-board RAM | On-board RAM, and the bus |

## How it is simulated

- **Shape.** `ZcbBoard` is a `CpuZ80Board` that also answers bus cycles, so one `[[board]]` entry is the whole board. `clock_hz` and `idle` come from the base class. `clock_hz` is 0 by default, as on the `z80` board. The real crystal is 4000000.
- **Decodes** a memory read that hits a PROM or the RAM, and an I/O read or write at the ports of the two chips. A memory write is not decoded. `snoop()` stores the byte in the on-board RAM and the write continues on the bus.
- **Reset latch.** `armed_` is U27. Every reset sets it. While it is set, `boot` makes every address decode as `E000 | (addr & 1FFF)`, and `phantom` makes the board assert `PHANTOM*`. `snoop()` clears it on the first memory cycle whose bus address is `E000` or higher, and calls `decodeChanged()`. Nothing writes the program counter.
- **PROM.** Each `[[board.socket]]` has `at` (`E000`, `E400`, `E800` or `EC00`) and `mount`. The size of the image, 1K, 2K or 4K, sets the size of the chip. The chip must start on a boundary of its own size. Overlapping sockets are reported in the log.
- **Serial.** An embedded `Intel8251`, unit `serial`. The unit property `baud` is switch S1, with its eight labels. The clock at TxC and RxC is 2 MHz / 13, divided as the rocker sets. For example, the label 9600 runs at 9615 baud. The guest's clock factor divides this clock, so a factor of 64 gives a quarter of the label. A real serial port gets the label, divided by the factor.
- **Parallel.** An embedded `Intel8255`, with mode 0 in all 16 direction sets. Nothing connects to J1, so an input reads `FF`.
- **Wait states.** `wait` is area N. `m1` gives one T-state on each M1, `none` gives none and `all` gives one on each bus cycle. `snoop()` calls `holdReady()`, so a fetch from an off-board memory also gets its wait state.
- **Interrupts:** none. The board has no `interrupt` property. The processor still takes interrupts from other boards.
- **DMA:** the processor gives up the bus as on the `z80` board. The board adds no DMA of its own.
- **Properties:** `group`, `serial_port`, `parallel_port`, `boot`, `phantom`, `wait`, and the `clock_hz` and `idle` of the base class.

### Reset

- `Reset::PowerOn`: loads the PROMs, fills the RAM with a fixed random pattern, resets the processor, sets the latch, and resets both chips.
- `Reset::Bus`: resets the processor, sets the latch, and resets both chips. The RAM keeps its contents. Sheet 4 sends RESET* through U34 to the RESET pins of the 8251 and the 8255. A guest must program the 8251 again after each reset.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Every address reads as the `E000` block after a reset, until the first memory cycle in `E000`-`FFFF` | The Z-80 starts at 0000 and finds RAM, so Monitor 4.3 does not start |
| The latch follows the address on the bus, not the address that U5 sees | A read at 0000 reaches the PROM through the override, but it must not clear the latch. If it did, the second byte of the jump at 0000 would come from RAM |
| A write in `E000`-`FFFF` also clears the latch | A program that runs in the mirror and writes high in memory keeps the override too long |
| A RAM write also goes out on the bus | A memory board under `FC00` loses the bytes that the guest wrote there |
| A1 does not reach the 8251 | Monitor 4.3 sends its six setup bytes to 05 and then to 07, so the chip must take both passes. Without the mirror, a guest that uses 06 and 07 finds no port |
| The sync mode field 00 keeps the clock factor at 16 | Monitor 4.3 passes through sync mode on the way to the internal reset. |
| The ports reset as inputs in mode 0, and a mode set clears the output registers | The printer driver of Monitor 4.3 reads port B before it sets a mode |

## Limitations and deliberate departures

- **The memory block stays at `E000`-`FFFF`.** Areas H and I are not modeled. Area F is modeled only for the PROMs, through the socket address and the image size.
- **The memory disable pads of area F are not modeled.** Pads 4 and 5 assert `PHANTOM*` for a chosen range. The X-CPM versions of the board use them. No source says what X-CPM is.
- **Reset takes effect at once.** On the real board, PRESET* fires at the next M1. A guest cannot see the difference.
- **CTS does not hold the transmitter.** The 8251 sends even if the line drops RTS. DSR, status bit 7, comes from the line and is active while the line is open, as the pull-ups make it on the real board.
- **Modes 1 and 2 of the 8255 are stored and set the directions only.** J1 has no device, so the handshake lines have nothing to drive. Monitor 4.3 uses mode 0 only.
- **An open 8255 input reads `FF`.** No source gives this value (`reference/ZCB CPU Manual.md` Q7). It decides which path the printer driver of Monitor 4.3 takes.
- **An off-board memory that decodes `E000`-`FFFF` is bus contention here.** The real ZCB ignores the data-in lines on an on-board read. The bus reports contention and the first board in slot order wins. Keep off-board RAM at `0000`-`DFFF`, as the memory map of Vector Graphic does.
- **I/O address mirroring, area O, is not modeled.** A board that reads A8 to A15 during an I/O cycle sees the Z-80's own high byte.
- **All rockers open is not offered.** The real port then has no clock. The `baud` property has only the eight rockers.
- **Area D is not modeled.** The board always behaves as if it generates MWRITE.

## Verification

`tests/test_zcb.cpp` drives real bus cycles. It covers these items:

- the port map, the mirror at 06 and 07, the group rule and the status bits
- the decode, and the release of the latch by a read and by a write
- `boot` and `phantom` off, and a RAM write that reaches an off-board RAM
- reset, power, a snapshot and property validation

S1 is measured in T-states for each clock factor. `test_intel8255` covers the chip.

The real Monitor 4.3 starts from a reset at 0000 on a machine with the ZCB, the Flashwriter II and RAM. Its `E` command passes a byte both ways. A mutation check on the decode and the reset latch failed the suite each time.

## References

- `reference/ZCB CPU Manual.md`
- `reference/Vector Graphic ZCB.md`
- `reference/Intel 8255.md`
- `reference/Vector Graphic Memory Maps.md`
- `reference/Vector Graphic Port Map.md`
