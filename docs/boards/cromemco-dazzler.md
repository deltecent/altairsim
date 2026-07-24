# Cromemco Dazzler — Color Graphics Interface

**Status:** implemented, `type = "dazzler"` — all four display modes, 16-color and
16-grey palettes, the two control ports and the status port. Rendered from a
framebuffer in main RAM; **not** bus-mastered (see *Limitations*).

## The real hardware

The first color graphics card for the S-100 bus (Cromemco, 1976; U.S. Patent
4,121,283). A two-board set that uses **high-speed DMA** to read a bitmap straight out
of the host computer's memory and turn it into a standard composite color-TV signal —
so the card is a **bus master**, not a slave, and it is the concrete reason DMA /
bus-mastering is in this simulator's bus model (DESIGN.md §4.5).

The picture is a **512-byte** (one page) or **2 KB** (four pages) framebuffer anywhere
in main RAM, on a 512-byte boundary. It scans as up to four 512-byte **quadrants**,
each a 32×32 element block tiled 2×2. Two output ports and one input port drive it:
control (on/off + base), format (resolution/size/color), and a two-bit status read
(odd/even scan line, end-of-frame). Four display modes fall out of the format byte:
32×32 or 64×64 color/grey elements (normal), and 64×64 or 128×128 on/off elements (X4,
where the whole picture takes one color from the format nibble).

## Sources

| Source | Path | Authority |
|---|---|---|
| *Cromemco Dazzler Instruction Manual*, Part No. 023-0003, Nov 1978 | `reference/Cromemco Dazzler.md` (distilled from `Cromemco_Dazzler_Instruction_Manual.pdf`) | **Authoritative.** The two output ports and one input port (§2), the DMA framebuffer layout and quadrant scan order (§3), and the byte→pixel encoding for all four modes (§4). |

## Register reference

Two ports at `port` (default `0x0E`): control/status at `BASE`, format at `BASE+1`.
**No memory is decoded** — the framebuffer is ordinary RAM owned by a memory board.

**`OUT BASE` — control**

| Bit | Meaning |
|---|---|
| D7 | ON/OFF — 1 begins video, 0 blanks it (front-panel CLEAR also forces off) |
| D6–D0 | A15–A9 of the framebuffer start → `base = (v & 0x7F) << 9` (512-byte aligned) |

**`OUT BASE+1` — format**

| Bit | Meaning |
|---|---|
| D6 | Resolution: 0 = normal (32×32 / 64×64), 1 = X4 (64×64 / 128×128 on/off) |
| D5 | Size: 0 = 512 bytes (quadrant 0 only), 1 = 2 KB (four quadrants) |
| D4 | 1 = color, 0 = black-and-white (16 greys) |
| D3–D0 | X4 only: the whole picture's single color/intensity (RGBI, or grey with D3 = MSB) |

**`IN BASE` — status**

| Bit | Meaning |
|---|---|
| D7 | ODD/EVEN scan line: 0 during odd lines, 1 during even |
| D6 | END OF FRAME: 0 for the ~4 ms vertical blank between frames, else 1 |

**Byte → pixels.** In **normal** mode each byte is two adjacent nibble-elements — low
nibble the left element, high nibble the right — and each nibble is `HI/LO(D3)
B(D2) G(D1) R(D0)`, or a 16-level grey in B&W. In **X4** mode each byte is eight
on/off bits laid out as two side-by-side 2×2 cells (low nibble left, high nibble
right); every lit bit takes the format-nibble color.

## How it is simulated

- **Decodes** `IoRead`/`IoWrite` at `port` and `port+1`, and nothing else — no memory,
  no interrupt wire, no `pHOLD`. `read(BASE)` returns the status byte; `read(BASE+1)`
  floats `0xFF` (format is write-only). `write` latches on/off + base, or the format.
- **`pump()`** reads the live 512-byte / 2 KB framebuffer out of main RAM with the
  side-effect-free `Bus::peek()` (no bus cycle, no strobe, no snoop — so a render never
  trips `BREAK MEM R` or a snooping card) and paints it into a `Surface` at the mode's
  logical resolution (32/64/128 square), through a 16-entry palette, then `present()`s
  it — on the main thread, never inside a bus cycle (DESIGN.md §7.4).
- **Two gates decide whether that render happens**, the same economy as the VDM-1.
  `frameChanged()` asks *would the picture look any different?* — but because the
  framebuffer is in main RAM and the guest never writes **through** this card, there is
  no write to latch a dirty flag on, so it **polls**: it compares the live bytes (and
  the on/base/format latches) against a shadow of what it last painted.
  `Display::wantsFrame()` then asks *do I want a frame right now?* — wall-clock, capped
  at 60 Hz in `src/main.cpp`, unlimited in `tests/main.cpp` so tests stay deterministic.
- **Palette**: 16 `Color`s built from the RGBI nibble (intensity × primary mix) in
  color mode, or a linear 16-step grey ramp in B&W, pushed with `Display::setPalette`.
  An Indexed8 `Surface` carries the element value as its pixel, so a color change is a
  `setPalette` away with no re-render.
- **Display**: uses a `Display` injected at the composition root
  (`DazzlerBoard::setDisplay`) — an `SdlDisplay` in the shipping binary, a
  `NullDisplay` headless. The card never `#include`s SDL.
- **Status** (D6/D7) is derived from the `Clock`, never a poll-driven counter, so a
  spin loop pacing to the frame sees it move because emulated time advanced.
- **`properties()`**: `port` (the even I/O base; the card owns `BASE` and `BASE+1`).

### Reset

- `Reset::PowerOn` (`power()`, cold): on/off, base and format cleared; the shadow
  blanked. A cold card shows nothing until the guest turns it on.
- `Reset::Bus` (RESET*, warm): forces the card **off** — the front-panel CLEAR the
  manual describes (§2.1). It does not touch the framebuffer (that is RAM).

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The framebuffer is **main RAM**, not on-card — the base is `(control & 0x7F) << 9` | A program that moves its picture with `OUT 0x0E` shows garbage, or the old page |
| **Quadrant tiling**: a 2 KB picture is four 512-byte blocks tiled 2×2 (Q0 TL, Q1 TR, Q2 BL, Q3 BR) | A kaleidoscope that mirrors by negating an axis (KSCOPE) draws four unrelated corners |
| Normal byte = **two nibble-elements**, low nibble on the **left** | Every pair of elements is swapped left-for-right |
| X4 byte = **eight on/off bits** in two 2×2 cells; color comes from the format nibble | High-res pictures are scrambled, or monochrome pictures come out one flat color |
| Format **D4** picks color vs 16 greys; **D3–D0** matter **only** in X4 | A B&W picture comes out colored, or an X4 picture ignores its color |

## Limitations and deliberate departures

- **Not a bus master — rendered from RAM instead.** The real card cycle-steals the
  frame out of memory (~1 MB/s, ~15 % CPU slowdown). This board does **not** implement
  `requestsBus()`/`busMaster()` and does not model that slowdown (Patrick, 2026-07-23).
  A correct picture needs only to **read** the current framebuffer each frame, which
  `Bus::peek()` does deterministically and headlessly; nothing on the S-100 side can
  read a pixel back, so a guest cannot tell the difference. The two status bits it
  *can* time come off the `Clock`. The DMA mechanism (`tests/test_dma.cpp`, DESIGN.md
  §4.5) remains available for a future card that wants it.
- **Status timing is a 2 MHz-based approximation**, not a cycle-exact raster position.
  The line and frame windows are fixed T-state counts (like the VDM-1's), so a guest
  polling END-OF-FRAME to avoid tearing sees it move; no guest depends on its exact phase.
- **The joystick / D+7A paddle is a separate board**, not part of the Dazzler; this
  card is output-plus-status only.

## Verification

- `tests/test_dazzler.cpp` (headless, `NullDisplay`): port decode; the control/format
  latches; normal-mode nibble-elements (low-left / high-right, 16 bytes per row);
  the 2×2 quadrant tiling of a 2 KB picture; X4 mode's 4×2 on/off cell and its exact
  bit→position map; the color-vs-grey palette; the Clock-derived status bits; the
  poll-based dirty economy (an unchanged framebuffer is not repainted); off blanks the
  screen; and `port` strap validation. It reads the rendered pixels straight back out
  of the `NullDisplay` surface.
- End-to-end: `altairsim examples/dazzler/kscope.toml` comes up drawing — Li-Chen Wang's
  Kaleidoscope paints a four-way-mirrored pattern into a 2 KB 64×64 color picture, which
  appears in a window on an SDL3 build (ATTN / Ctrl-E breaks back to the monitor).

## References

- `reference/Cromemco Dazzler.md` — the distilled manual.
- `src/boards/cromemco-dazzler.{h,cpp}`, `src/host/display.h`,
  `src/host/display_sdl.{h,cpp}`, `machines/dazzler.toml`, `examples/dazzler/`.
