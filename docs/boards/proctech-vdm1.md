# Processor Technology VDM-1 — Video Display Module

**Status:** implemented, `type = "vdm1"` — memory-mapped text with the real VDM-1
character-generator ROM; keyboard and CR/VT blanking are deferred (see *Limitations*).

## The real hardware

The S-100 card that gave the Altair/IMSAI a video terminal (Processor Technology,
1976; the display half of the Sol-20, and the board SOLOS/CUTER drives). The CPU
writes ASCII into a **1 KB screen RAM** mapped into its own address space, and the
card scans that RAM against an **MCM6574** character-generator ROM to paint **16
lines × 64 characters** of 1 V composite video. Eight 1024×1 static RAMs hold the
screen; a 14.318 MHz dot clock and one-shot sync generators produce the raster.

Configuration is jumpers and a six-switch DIP: a comparison value on ADR10–15 sets
the 1 KB **screen page** (default `0xCC00`), the same six bits set the **I/O port**
(default `0xCC`, low two bits forced zero), and SW1–SW6 pick video polarity, cursor
behavior, and control-character blanking. The keyboard was a **separate parallel
board**, not part of the VDM-1.

## Sources

| Source | Path | Authority |
|---|---|---|
| *VDM-1 Video Display Module User's Manual*, Processor Technology, 6th printing Apr 1978 | `reference/Processor Technology VDM-1.md` (distilled from `VDM-1 Rev E Newer Manual.pdf`) | **Authoritative.** Screen memory (§3.1.3), character/cursor split (§3.1.4), scroll (§3.1.7), the computer interface — page + port compare, scroll `OUT`, status `IN` (§3.1.8), and the SW1–SW6 option table (§3.2, Table 3-1). |

## Register reference

**Memory:** 1 KB at `base` (default `0xCC00`, 1 KB-aligned), `byte = row*64 + col`.

| Bit | Meaning |
|---|---|
| D0–D6 | Character code → MCM6574 glyph (128-char set) |
| D7 | Cursor: this cell is shown inverted, and blinks if the blink option is on |

**I/O:** one port at `port` (default `0xCC`, a multiple of 4).

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `port` | Latch **scroll** = low nibble = the character row shown at the top (rows wrap mod 16); fires a ~0.375 s one-shot | **D0** = one-shot busy (poll to pace a slow scroll); **D1** = SCAN ADVANCE (1 = beam past the right margin, the flicker-free write window) |

## How it is simulated

- **Decodes** `MemRead`/`MemWrite` in `[base, base+0x3FF]` and `IoRead`/`IoWrite`
  at `port`. Holds its own `uint8_t screen[1024]`; `peek()` returns a screen byte
  side-effect-free (so `DUMP`/`DISASM`/`TRACE` see the screen). Page-uniform decode.
- **`pump()`** renders the 16×64 screen into a `Surface` (512×208 logical pixels,
  8×13 per cell) through the character-generator ROM, applies scroll, per-cell
  cursor inversion, and whole-screen polarity, then `present()`s it — once per time
  slice, on the main thread, never inside a bus cycle (DESIGN.md §7.4).
- **Display**: uses a `Display` injected at the composition root
  (`VdmBoard::setDisplay`) — an `SdlDisplay` in the shipping binary, a `NullDisplay`
  headless. The card never `#include`s SDL.
- **Status** (D0/D1) is derived from the `Clock`, never a poll-driven counter, so a
  spin loop sees it move because emulated time advanced (replay-safe).
- **No media, no interrupt wire, no DMA.** `properties()`: `base`, `port`, `video`
  (`normal`/`reverse`), `cursor` (`off`/`blink`/`steady`).

### Reset

- `Reset::PowerOn` (POC*, cold): clears screen RAM to 0x00, scroll to 0, timer off.
  (Real RAM powers up random; we blank it so a cold machine shows an empty screen.)
- `Reset::Bus` (RESET*, warm): nothing — a warm reset does not clear the screen or
  move the scroll latch (RAM has no POC* pin).

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Bit 7 is the **cursor**, not part of the glyph (D0–D6 address the ROM) | A screen with the cursor bit set shows the wrong characters, or the cursor never appears |
| **Hardware scroll**: `OUT` sets the top row; the 16 rows wrap mod 16 | Software that scrolls by writing the port (SOLOS) redraws nothing, or the screen jumps instead of scrolling |
| The `OUT` also fires a **0.25–0.5 s one-shot** the guest polls on D0 | A scroll-pacing loop that waits on D0 hangs forever, or runs full-tilt |
| The screen page and the I/O port share the **same six jumper bits** | A machine file that sets one and not the other lands the card at an address period software will not find |

## Limitations and deliberate departures

- **The keyboard is not here.** The VDM-1's keyboard was a separate parallel board;
  this card is output-only. A host-window keystroke path will arrive with that
  board and route through a `ByteStream` (DESIGN.md §7.4), not through the VDM-1.
- **CR→end-of-line / VT→end-of-screen blanking is not modeled.** Control codes
  0x00–0x1F render blank (the common SW5/SW6 setting); software relying on the
  hardware's CR/VT erase would see stray blanks, not erasure. (The reference file
  §5 has the exact behavior for when it is added.)
- **Control codes 0x00–0x1F render blank.** The character-generator ROM
  (`proctech-vdm1-font.h`) actually carries the VDM-1's own graphics glyphs for
  those codes, but the board blanks them — the common SW5/SW6 control-blanking
  option — so a cleared (0x00) screen is blank. Making that switchable is one flag;
  the glyphs are already in the ROM.
- **SCAN ADVANCE (D1) is a time-derived approximation**, not a cycle-exact raster
  position. It cycles so a polling flicker-free writer sees it move; it is a hint,
  and no guest depends on its exact phase.

## Verification

- `tests/test_vdm1.cpp` (headless, `NullDisplay`): decode of the screen range and
  the port, guest writes landing in screen RAM and reading back via `peek`, the
  render lighting the right cell for a written character and nothing elsewhere,
  hardware scroll moving the top row, the `OUT` one-shot (D0), the reverse-video
  palette swap, and property validation (1 KB / 4-port alignment).
- End-to-end: `altairsim vdm1` runs `roms/VDM1DEMO`, which writes a banner into
  `0xCC00`; `DUMP CC00` shows it, and with SDL3 the banner appears in a window.

## References

- `reference/Processor Technology VDM-1.md` — the distilled manual.
- `src/boards/proctech-vdm1.{h,cpp}`, `src/boards/proctech-vdm1-font.h`,
  `src/host/display.h`, `src/host/display_sdl.{h,cpp}`, `machines/vdm1.toml`.
