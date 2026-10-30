# Vector Graphic Flashwriter II

**Status:** done for the screen, the keyboard port and the key interrupt. The wait states, the on-board PROM and the 50 Hz option are not modeled (see *Limitations*).

## The real hardware

The **Flashwriter II** is a Vector Graphic S-100 board. It gives a machine an 80-column by 24-row text screen and the port for the keyboard. Vector Graphic sold it from 1979. It shows 24 rows of 80 characters, and each character is a cell of 8 by 10 dots. The screen is 2K of RAM on the board. The processor reads and writes that RAM as ordinary memory, so a program puts a character on the screen when it stores a byte.

The keyboard enters through an 8212 latch on the board. The latch has two input ports. The first port gives status and the second gives the key. The board decodes only a read (`SINP`), so an `OUT` to either port reaches nothing.

The board holds the screen RAM, the character generator and the keyboard latch. It does not hold the Monitor. In a Vector Graphic machine the Monitor is on the 12K PROM/RAM board or on the ZCB board.

The jumpers set the board's addresses. Area F sets the 2K block of video RAM, anywhere from `C000` to `F800`. Rev 4-A of the manual ships it at `F000`. Rev 3 ships it at `D000`. Area J sets the ports, `00` and `01` as shipped. Area L sends the key flag to pin 73 of the bus. Vector Graphic fitted area L on every board from February 1980.

## Sources

| Source | Path | Authority |
|---|---|---|
| Distilled emulation reference | `reference/Flashwriter II Manual.md` | The memory map, the ports, the status bits, the raster, the jumper areas and the quirks |
| *Flashwriter II* manual, Rev 3 (1979) and Rev 4-A (1980) | `Flashwriter II Manual.pdf`, `VG Flashwriter II.pdf` | The character chart (page 4-1 of Rev 3 and 4-2 of Rev 4-A), the crystal (Table I) and the F000 default |
| Schematic, Rev 4 | `Flashwriter II Schematic Rev 4.tif` | The input-only decode, the status drivers, the `PRDY` gates and areas O, P and R |
| Service bulletins | `Flashwriter II - Misc.pdf` | Area L on all boards from February 4, 1980 (PIB 0055) |
| Port map, Rev A (1981) | `Port Map.pdf` | The keyboard at `00` and `01` |
| Monitor 4.3 | `roms/VGMON43/VGMON43.BIN` | The check of the whole board: it draws its screen at `F000` and reads the keyboard at ports `00` and `01` |

The Rev 3 manual says H7 goes high at the end of the displayed line. The Rev 4 schematic shows the opposite, and it won. The Rev 3 manual says 16 graphics characters on one page and 32 on another. The chart has 32, so 32 won. `reference/Flashwriter II Manual.md` lists each conflict and what settled it.

## Register reference

Both ports are input only. The status port is `port`. The data port is `port` + 1.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `00` | Not decoded | **Status.** Bit 6 = 1 when a key waits. Bit 0 = 0 when a key waits. Bit 5 = 0 during vertical blanking. Bits 1 to 4 and bit 7 read 1 |
| `01` | Not decoded | **Key.** All eight bits of the latch. The read clears the key flag |

The status byte has four values that a program can see:

| Key waiting | Blanking | Status |
|---|---|---|
| No | No | `BF` |
| No | Yes | `9F` |
| Yes | No | `FE` |
| Yes | Yes | `DE` |

Nothing on the board drives bits 1 to 4 and bit 7. They read 1 because the bus floats high.

Video RAM is 2K at `base` (default `F000`), read and write. The first 1920 bytes show on the screen. The last 128 bytes (`F780` to `F7FF` by default) are plain RAM. Bits 0 to 6 of a byte select one of 128 characters. Bit 7 = 1 shows the cell in reverse video. Codes `00` to `1F` are graphics blocks.

## How it is simulated

- **Decodes** a memory read or write in the 2K block at `base`. It decodes an `IN` at `port` and `port` + 1, and nothing else. It does not look at `PHANTOM*`.
- **`write()` to video RAM** stores the byte. The board marks the picture changed only when a byte that shows has changed.
- **`pump()`** is the host turn. It takes one key from the `keyboard` unit when the key flag is clear. After that, it paints the picture into the injected `Display`, but only when a byte that shows has changed. The board never touches SDL. A headless build paints into a `NullDisplay`.
- **Picture.** 640 by 480 pixels. The board draws each of the 240 dot lines twice, to give the shape of a 4:3 tube. The color is the VDM-1's green, because the manual names no color. `width` sets the window size, as on the other video boards.
- **Raster.** The vertical counter runs from the board's own 14.3347 MHz crystal and host time, not from the machine's clock (issue #825). The counter counts 262 lines of 114 character times, which is 59.99 Hz. Bit 5 of the status port is 0 on lines 240 to 261. A guest sees the 60 Hz clock at any `clock_hz`, including flat out.
- **Interrupts.** With `interrupt = int`, the board holds pin 73 low while a key waits, until the guest reads the data port. The board has no vector. Nothing answers the interrupt acknowledge, so the bus reads `FF` and a Z80 or an 8080 runs `RST 7`.
- **DMA:** none.
- **`peek()`** reads video RAM without effect, so the debugger and `DUMP` do not disturb the board.

Properties:

| Property | Default | Meaning |
|---|---|---|
| `base` | `F000` | The address of the video RAM, a multiple of 2K from `C000` to `F800` (area F) |
| `port` | `00` | The status port. Even, `00` to `0E` (area J) |
| `interrupt` | `int` | `int` fits area L. `none` leaves it open |
| `fill` | `random` | The video RAM at power-on, `zero` or `random` |
| `seed` | `1` | The seed for `fill = random` |
| `width` | `auto` | The window width in pixels |

The unit `keyboard` takes `connect`. A machine file connects it to `console`, so the keys of the video window reach the guest.

### Reset

- `Reset::PowerOn` (POC*, cold): clears the 8212 latch and the key flag, and fills the video RAM by `fill`.
- `Reset::Bus` (RESET*, warm): changes nothing. The board has no `RESET*` wire. The screen and a waiting key stay.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Only bits 0, 5 and 6 of the status are driven. The rest read 1 | A program that compares the whole byte (`BF`, `FE`) does not see its key |
| `IN` from the status port keeps the key flag. `IN` from the data port clears it | A program that polls the status port twice loses its key |
| The ports are input only | An `OUT` to `00` or `01` must reach nothing. Monitor 4.3 writes its 8251 setup to `03`, `05` and `07`, and those are not this board's |
| Bit 7 of a byte is reverse video, and there is no hardware cursor | Monitor 4.3 shows its cursor by flipping bit 7 of the cell. A board with a cursor of its own shows two |
| The last 128 bytes of the 2K do not show | The Monitor stops its scroll at `F780`. A guest can keep data in those bytes |
| All eight bits of the key pass through | A program that sends bit 7 as a key flag still gets it |
| Bit 5 follows host time | A guest that waits for vertical blanking to draw without tearing sees the 60 Hz clock |
| A status read counts as a poll of an empty line | The idle policy lets a machine stand down at the Monitor prompt, instead of using a host core |

## Limitations and deliberate departures

- **The wait states are not modeled.** On the real board, a processor access to video RAM holds `PRDY` low until horizontal retrace. A block move in video RAM is slower there than in plain RAM. The guest has no flag for the wait, so a guest still runs correctly. With `clock_hz` set, screen writes run faster here than on the real board.
- **The on-board PROM is not modeled.** Socket U42, the jump on reset (areas C and K) and `PHANTOM*` from the board are absent. The `vector` machine puts the Monitor on a generic `memory` board, and that board stands in for the 12K PROM/RAM board or the ZCB board. Neither of those exists here yet.
- **256 characters are not offered.** Areas B, E and I give 256 characters on the real board. This board has the 128 of the shipped setting.
- **Low intensity is not modeled.** Areas O and P show bit 7 as low intensity on the real board. This board shows reverse video only.
- **50 Hz is not offered.** The 50 Hz board has a different crystal, 314 lines and a cell of 8 by 12 dots. This board is the 60 Hz board.
- **The bank select for multi-user use is not modeled.** Area S and port `40` are not here.
- **The keyboard sends ASCII.** The unit passes the host's bytes through. The special keys of the Vector Graphic keyboard have no host mapping.
- **A new key waits for the old one.** The real latch takes a new key over an old key. Here the host line holds the new key back until the guest reads the old one. Text sent ahead of the guest is not lost.
- **The character set comes from the printed chart.** No scan has a dump of the character PROM. The dots come from the chart, read by a program and checked by eye. The two scans agree on 124 of 128 cells. The Rev 3 scan and the reverse chart settled the other four, and `src/boards/vector-fwii-font.h` lists each one.
- **A snapshot does not save the raster phase.** A snapshot saves the video RAM and the key latch. It does not save the raster phase, because host time does not travel in a snapshot.

## Verification

`tests/test_fwii.cpp` drives real bus cycles. It checks these facts:

- The decode at each `base`, the 2K edge, the input-only ports and each `port`.
- The four status bytes, with host time set by the test.
- The key flag, the eight key bits, the level on pin 73 and the `RST 7` from a real Z80 run.
- Both resets, the snapshot round trip and the property checks.

With a `NullDisplay`, the test also checks the dots of a known glyph, the reverse cell and the last 128 bytes. It checks that a frame is owed only when a byte that shows has changed.

`tests/acceptance/vector.cmake` boots the real Monitor 4.3 on the `vector` machine over `--mcp`. It reads the banner and the `Mon>` prompt off `F000` to `F77F`. It types `R` and `DE000E03F`, and it reads the register dump and the hex dump back off the screen. Each `run` must stop as idle.

The maintainer also ran `altairsim vector` in a real window with SDL3. The banner, the glyph shapes and the typed `R` and `DE000E03F` were correct.

## References

- `reference/Flashwriter II Manual.md`, the distilled emulation reference.
- `machines/vector.toml`, the machine.
- `roms/VGMON43/README.md`, the Monitor.
- `docs/boards/proctech-vdm1.md` and `docs/boards/sd-vdb8024.md`, the other video boards.
