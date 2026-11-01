# Vector Graphic Monitor 4.1 — Extended Systems Monitor

Source: [Vector Monitor 4.1.pdf](#)

Vector Graphic *Extended Systems Monitor User's Manual*, Rev. 4.1-A, 9/5/80, with the assembly
listing, modified 6/1/80. 28 pp. The listing is on PDF pages 18–28. Monitor 4.1 is Z80 code in one
2716 PROM. **Emulated** as `builtin:vgmon41`, assembled from the listing. See `roms/VGMON41/`.

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Address | `E000`–`E7FF` |
| Prompt | `Mon>` |
| Screen memory | `F000`, Flashwriter II |
| Keyboard | port `00` status (bit 6), port `01` data, from the listing |
| Stack | `FFD0` |
| USARTs set up at start | 8251 at ports 3, 5 and 7 |

## 2. Entry points (p. 8)

| Address | Use |
|---|---|
| `E000` | Cold entry. Clears the screen and sets up the USARTs at ports 3, 5 and 7, X16 clock |
| `E003` | Keyboard status. Zero flag set: no key |
| `E006` | Keyboard input to A, with code conversion. No ESC check |
| `E009` | Video driver: shows the character in A |
| `E00C` | ESCAPE routine: returns zero if no key, else the character. ESC goes to the Monitor |

Port 3, 5 and 7 setup fits a Bitstreamer I at port 4, a Bitstreamer II at port 2 and a ZCB at port 5.

## 3. Commands (pp. 3–7)

| Key | Arguments | Action |
|---|---|---|
| A | adr1 adr2 | ASCII dump |
| B | | Jump to `F800`, the 5-1/4" disk boot |
| C | adr1 adr2 adr3 | Compare blocks |
| D | adr1 adr2 | Hex dump |
| E | | Terminal mode through port 4 |
| F | adr1 adr2 b1 b2 | Find two bytes |
| G | adr1 | Go to the address |
| H | | Jump to `FC00`, the 1K scratchpad RAM |
| I | port | Input from a port |
| J | | Jump to `04E7` (MDOS) if it holds `C3`, else `0000` |
| K | adr | Set a breakpoint: RST 7 (`FF`), vector at `0038` |
| L | | Jump to `0000` |
| M | adr1 adr2 adr3 | Move a block |
| N | | Non-destructive memory test from `0000` |
| O | port data | Output to a port |
| P | adr | Program memory, 16 bytes at a time |
| Q | adr1 adr2 | Checksum, MOD 256 |
| R | | Register dump |
| S | adr1 adr2 byte | Find one byte |
| T | adr1 adr2 | Pseudorandom memory test, runs until interrupted |
| U | | Jump to the user area. See the quirks |
| V | | Jump to `E800`, the 8" boot on the Disk Boot #3 PROM |
| W | | Jump to `E802`, the Winchester boot on the same PROM |
| X | adr1 adr2 adr3 | Exchange blocks |
| Y | | Echo keys to the video driver. ESC ends |
| Z | adr1 adr2 data | Fill memory |

## 4. Video control codes (p. 9)

| Hex | Function |
|---|---|
| 02 | Home the cursor |
| 04 | Clear the screen, home, set normal video |
| 05 | Display the code in B |
| 08 | Destructive backspace |
| 09 | Tab to the next multiple of 8 |
| 0A | Linefeed |
| 0D | Carriage return |
| 0E | Toggle the cursor |
| 10 | Clear to end of screen |
| 11 | Clear to end of line |
| 12 | Cursor down |
| 14 | Toggle reverse video |
| 15 | Cursor up |
| 17 | Cursor left |
| 18 | Clear to start of line |
| 1A | Cursor right |
| 1B | Cursor X Y lead-in. X is first, and the top left is 0,0 |

The keyboard routine changes `E1`→`5D`, `F1`→`5B`, `A1`→`7E`, `B1`→`5C` and `60`→`15` (table at `E15B`).

## Quirks worth carrying

- ⚠ **U command.** The manual says `2B00`. The listing has `JMP 0100H`, and the PROM is built from the
  listing. The listing won.
- ⚠ **Sum.** Vector's PROM sheet gives sum 16 for 4.1. The image assembled from this listing gives 17
  over `E000`–`E7FF`. The listing does not print `E7FB`–`E7FF`, so those bytes are zero here. The cause
  of the difference of 1 is not known.
- The G text names `C00BH` as the return address and the T text names a 2708 PROM/RAM board. These
  are text from an earlier Monitor. The code at `E040` is the return point.
