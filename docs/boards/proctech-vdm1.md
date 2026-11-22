# Processor Technology VDM-1 — Video Display Module

**Status:** implemented, `type = "vdm1"` — memory-mapped text with the real VDM-1
character-generator ROM (the MCM6576) and the SW5/SW6 blanking options; the keyboard is
deferred (see *Limitations*).

## The real hardware

The S-100 card that gave the Altair/IMSAI a video terminal (Processor Technology,
1976; the display half of the Sol-20, and the board SOLOS/CUTER drives). The CPU
writes ASCII into a **1 KB screen RAM** mapped into its own address space, and the
card scans that RAM against a character-generator ROM to paint **16
lines × 64 characters** of 1 V composite video. Eight 1024×1 static RAMs hold the
screen; a 14.318 MHz dot clock and one-shot sync generators produce the raster.

Configuration is jumpers and a six-switch DIP: a comparison value on ADR10–15 sets
the 1 KB **screen page** (default `0xCC00`), the same six bits set the **I/O port**
(default `0xCC`, low two bits forced zero), and SW1–SW6 pick video polarity, cursor
behavior, and control-character blanking. The keyboard was a **separate parallel
board**, not part of the VDM-1.

The ROM socket takes an **MCM6574, MCM6575 or MCM6576** (manual Figures 3-1A–C); they
are pin-compatible and differ in their glyphs. We emulate the **MCM6576**, which has a
graphics glyph for every control code 0x00–0x1F (0x00 is a box, 0x0D a left arrow).

## Sources

| Source | Path | Authority |
|---|---|---|
| *VDM-1 Video Display Module User's Manual*, Processor Technology, 6th printing Apr 1978 | `reference/Processor Technology VDM-1.md` (distilled from `VDM-1 Rev E Newer Manual.pdf`) | **Authoritative.** Screen memory (§3.1.3), character/cursor split (§3.1.4), scroll (§3.1.7), the computer interface — page + port compare, scroll `OUT`, status `IN` (§3.1.8), and the SW1–SW6 option table (§3.2, Table 3-1). |

## Register reference

**Memory:** 1 KB at `base` (default `0xCC00`, 1 KB-aligned), `byte = row*64 + col`.

| Bit | Meaning |
|---|---|
| D0–D6 | Character code → MCM6576 glyph (128-char set) |
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
  cursor inversion, and whole-screen polarity, then `present()`s it — on the main
  thread, never inside a bus cycle (DESIGN.md §7.4).
- **Two gates decide whether that render happens at all**, and they answer different
  questions. `frameChanged()` asks *would this frame look any different?* — a
  deterministic dirty flag set by screen writes that change a byte, a scroll landing
  on a new row, and the `video`/`cursor` properties, plus a blink-phase compare when
  a cursor is actually on screen. `Display::wantsFrame()` then asks *do I want a
  frame right now?* — wall-clock, capped at 60 Hz in `src/main.cpp`, unlimited in
  `tests/main.cpp` so tests stay deterministic. Only the first gate exists headless.
  Neither gate clears `dirty_`: a frame deferred by the limiter is still owed, and
  `render()` is what discharges it. Without them the card repainted all 106,496
  pixels every time slice and cost a machine **94×** its speed (#63).
- **Display**: uses a `Display` injected at the composition root
  (`VdmBoard::setDisplay`) — an `SdlDisplay` in the shipping binary, a `NullDisplay`
  headless. The card never `#include`s SDL.
- **Status** (D0/D1) is derived from the `Clock`, never a poll-driven counter, so a
  spin loop sees it move because emulated time advanced (replay-safe).
- **No media, no interrupt wire, no DMA.** `properties()`: `base`, `port`, `video`
  (`normal`/`reverse`), `cursor` (`off`/`blink`/`steady`), `blanking`
  (`none`/`crvt`/`control`/`all`), `fill` (`zero`/`random`), `seed`.

### Blanking (SW5/SW6)

The ROM draws every code; blanking is the board's, in `render()`. The `blanking`
property is Table 3-1's four switch settings:

| `blanking` | SW5 | SW6 | Control codes 00–1F | CR/VT text blanking |
|---|---|---|---|---|
| `none` (default) | ON | ON | shown | off |
| `crvt` | ON | OFF | shown | on |
| `control` | OFF | ON | blanked | on |
| `all` | OFF | OFF | every character blanked; only cursor blocks show | on |

The default is the factory setting: the manual's installation note (§2.7.1) ships the
board with "unblanked control characters". CR/VT blanking follows the manual's test
(§2.7.5, Figures 2-13 and 2-14): a **CR** blanks from the next cell to the end of its
line; a **VT** blanks from the next cell to the end of the screen. The CR or VT itself
is drawn unless control codes are blanked too. Blanking works in **display order**,
after the scroll, as the beam scans. A blanked cell still shows its cursor block.

### Reset

- `Reset::PowerOn` (POC*, cold): fills screen RAM by `fill`, scroll to 0, timer off.
  The default `fill = random` is what real RAM does, and it shows: a cold screen is
  junk until software clears it, as SOLOS and CUTER do. `seed` makes it repeatable,
  like the RAM boards. `fill = zero` gives all 0x00, which is a screen of boxes, not
  a blank one.
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
- **Only the MCM6576 is emulated.** The 6574 and 6575 glyph sets are not in the tree.
- **SCAN ADVANCE (D1) is a time-derived approximation**, not a cycle-exact raster
  position. It cycles so a polling flicker-free writer sees it move; it is a hint,
  and no guest depends on its exact phase.

## Verification

- `tests/test_vdm1.cpp` (headless, `NullDisplay`): decode of the screen range and
  the port, guest writes landing in screen RAM and reading back via `peek`, the
  render lighting the right cell for a written character and nothing elsewhere,
  hardware scroll moving the top row, the `OUT` one-shot (D0), the reverse-video
  palette swap, property validation (1 KB / 4-port alignment), control codes drawn
  by default, each `blanking` setting (CR/VT in display order, cursor blocks under
  `all`), and the `fill`/`seed` power-on screen.
- End-to-end: `roms/VDM1DEMO/README.md` gives a machine file that runs the demo ROM, which
  writes a banner into `0xCC00`; `DUMP CC00` shows it, and with SDL3 the banner appears in a
  window. The `sol20` machine draws SOLOS's screen on the same board.

## References

- `reference/Processor Technology VDM-1.md` — the distilled manual.
- `src/boards/proctech-vdm1.{h,cpp}`, `src/boards/proctech-vdm1-font.h`,
  `src/host/display.h`, `src/host/display_sdl.{h,cpp}`, `roms/VDM1DEMO/README.md` (a machine file).
