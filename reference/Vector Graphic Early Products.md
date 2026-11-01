# Vector Graphic Early Products — the first product sheets

Source: [Early VG Products.pdf](#)

Vector Graphic product sheets, undated, 11 PDF pages, one sheet for each product of the first years.
The sheets do not mention the ZCB. This file keeps the facts that software or a machine file can
depend on, and omits sales text and power figures.

**Reference only.** None of these boards is emulated.

---

## 1. Extended Monitor (the first sheet)

The Extended Monitor comes on one 2708 or four 1702A PROMs, or five with the video driver. It has 25
commands. The prompt is `*` ([Bit Streamer](Bitstreamer%20I.md)).

| Option | I/O |
|---|---|
| A | MITS SIO Rev 1, 3P+S with inverted status |
| B | MITS 2SIO |
| C | Vector Graphic Bitstreamer serial in and out, IMSAI SIO2 |
| D | Version 1.0 Poly VTI (obsolete) |
| E | 3P+S with no inverted status bit |
| AV | VDM-1 output, input as option A |
| EV | Vector Graphic Flashwriter, or 3P+S input and VDM-1 output, or Bitstreamer parallel input and VDM-1 output |
| P | Poly VTI version 1.2 |
| EP | 3P+S input and VTI 1.2 output, or Bitstreamer parallel input and VTI 1.2 output |
| CV | Bitstreamer serial in, Flashwriter or VDM-1 output |

| Key | Command | Key | Command |
|---|---|---|---|
| A | ASCII dump | O | Output to port |
| B | Jump to bootstrap loader (E900H) | P | Program memory |
| C | Compare blocks of memory | Q | Compute checksum |
| D | Dump memory in hex | R | Read cassette |
| E | Jump to extension PROM (C400H) | S | Search for single byte |
| F | Find two bytes | T | Test memory |
| G | Go to and execute | U | Tarbell sync |
| I | Input from port | V | Verify tape |
| J | Jump to loaded DOS (2028H) | W | Write cassette tape |
| L | Load Tarbell cassette and go | X | Exchange memory blocks |
| M | Move memory block | Y | Relocating loader (1702 PROMs) |
| N | Non destructive memory test | Y | Keyboard echo (2708 PROMs) |
| | | Z | Zero or fill memory |

---

## 2. Boards

| Product | What the sheet gives |
|---|---|
| Bitstreamer | Two parallel ports and one serial port, 8251, 110–9600 baud, 16 output lines, 16 input lines, 2 control lines |
| Vector 1, 1+, 1++ | 8080A at 2 MHz or Z-80A at 4 MHz, 18 slots, 64K. The 1+ has one 5" drive and the 1++ has two |
| Analog Interface | 4 A/D channels, 0 to +2.5 V, 8 parallel input bits with a latched strobe, tones of 450 and 800 Hz, 2 ports anywhere in 00–FF |
| S-100 motherboard | 18 slots |
| PROM/RAM ("reset and go") | 2K of 1702A, 1K of 2102 RAM, a 4K block on 4K boundaries |
| Z-80 CPU | MK-3880N, 2 or 4 MHz by jumper, automatic wait state by jumper, `MWRITE` by jumper, all three Z-80 interrupt modes |
| 12K PROM/RAM | Up to 12K of 2708 or 2704 and 1K of RAM. Block A up to 8K of PROM, block B up to 4K of PROM and the RAM. Blocks on 8K boundaries, the RAM on 1K boundaries in block B. See [Vector Graphic PROM-RAM III](Vector%20Graphic%20PROM-RAM%20III.md) |
| 8K Static RAM | 2102, 450 or 250 ns, 8K blocks, write protect by switch |
| 16K Static RAM | 2114, 350 ns, no wait at 4 MHz, addressed in 8K blocks, write protect by switch, **eight memory banks** |

---

## 3. The 16K Static RAM and memory banks

"Each 16K memory board can be assigned to 1 or more of 8 memory banks thus permitting up to 512K
bytes of memory in a chassis."

Two facts here bear on the OASIS bank question in
[Vector Graphic 64K Dynamic RAM](Vector%20Graphic%2064K%20Dynamic%20RAM.md) §7:

- One board can answer in **more than one** bank.
- The board is 16K and sets its address in 8K blocks, so it can sit at 0000H and stop at 3FFFH.

---

## Quirks worth carrying

- **⚠ The older letter B.** The Extended Monitor boots at E900H here. Monitor 4.3 boots from code at
  EB11. A letter keeps its meaning across versions, and the addresses change.
- **⚠ Y is listed twice.** The command depends on the PROM type.
- **⚠ Command `D`.** The sheet prints "JUMP MEMORY IN HEX", which is a slip for dump.
- **⚠ Early Bitstreamer text.** The sheet gives the parallel ports "latched strobe inputs". That is
  the status latch of the [Bit Streamer](Bitstreamer%20I.md) manual.
