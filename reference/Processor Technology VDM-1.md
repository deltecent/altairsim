# Processor Technology VDM-1 Video Display Module

Source: [VDM-1 Rev E Newer Manual.pdf](#)

Processor Technology Corporation, "VDM-1 Video Display Module User's Manual",
Manual Part No. 208003, Sixth Printing April 1978 (copyright 1976). The VDM-1 is
the S-100 card that made a raw memory-mapped video terminal practical on the
Altair/IMSAI bus: it is the display half of the Sol-20 and the board SOLOS/CUTER
drives. The CPU writes ASCII into a **1 KB screen memory** that lives in the
processor's address space, and the card scans that RAM against an MCM6574
character-generator ROM to paint **16 lines × 64 characters** of composite video.

This file captures everything needed to *emulate* the board: the memory map, the
one I/O port (scroll out / status in), the character/cursor byte encoding, and
the switch-selectable options. Assembly, tune-up, the dot/sync timing chains, and
the schematic-level IC walk-through are omitted — an emulator reproduces the
*observable* behavior, not the 14.318 MHz dot clock. See DESIGN.md §7.4 for how
this board fits the `Display` seam; the keyboard is a **separate** parallel board,
so keystrokes return through a `ByteStream`, never through the VDM-1.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| Screen memory | **1024 bytes**, memory-mapped, 8×(1024×1) RAM (IC41–48) |
| Layout | **16 rows × 64 columns**; byte `= row*64 + col`, row 0 top |
| Memory base | 1 KB page selected by jumper (ADR10–15); **default `0xCC00`** |
| Char code | **D0–D6** → MCM6574 glyph (128-char set) |
| Cursor/blink bit | **D7** — does *not* enter the ROM; marks the cursor position |
| I/O port | **one** port; 6 jumper bits = ADR7–2, **ADR1–0 forced 0** |
| Default port | `0xCC` (same 6-bit jumper as the memory page: `port = (base>>8) & 0xFC`) |
| `OUT port,A` | Latch **scroll** (top character row) + fire the 0.25–0.5 s one-shot |
| `IN port` → D0 | **STATUS one-shot** — high 0.25–0.5 s after the last `OUT` (scroll pacing) |
| `IN port` → D1 | **SCAN ADVANCE** — 1 = beam just past the right margin (flicker-free write window) |
| Video polarity | Whole-screen **normal or reverse**, switch-selected (SW1/SW2) |
| Field | 60 Hz default (50 Hz is a hardware mod, §2.7.7); 260 scan lines/field |
| Interrupt | Optional — the STATUS one-shot can be jumpered to a VI pin |

> **Note (Sol-20 / SOLOS):** in the Sol-20 the screen memory sits at `0xCC00` and
> the control port is `0xC8`. The port is jumper-independent of the page on some
> straps; a bare Altair VDM-1 commonly ends up at page `0xCC00` / port `0xCC`. Pin
> the pair down from the machine you are emulating, not from a single default.

---

## 2. Memory interface and address selection

Screen memory is eight 1024×1 static RAMs (IC41–48) giving **1024 bytes**. An
address multiplexer (IC23/24/28) feeds them from one of two sources: the internal
scan counter (normal) or the computer's address lines ADR0–9 when the CPU claims
the card. Write-enable to the RAMs is active only during an external (CPU) access
with `WRITE` low — i.e. an ordinary memory write into the 1 KB window.

**Page compare (IC29).** The card compares ADR10–15 (the six high address bits)
against a jumper-set value (the X/Y/GND *ADDRESS SELECT* straps). A match with no
`SINP`/`SOUT` asserted = a screen-memory access; the 1 KB page is therefore
`page = jumper`, base `= jumper << 10`. Default jumper `0x33` → base **`0xCC00`**,
spanning `0xCC00–0xCFFF`.

The same comparator serves the I/O port (next section) — the two low-order bits
of the port address are forced to zero, so `port = (base >> 8) & 0xFC` on the
common strap: base `0xCC00` → port `0xCC`.

**Emulation.** The board `decodes()` the 1 KB memory range for `MemRead`/`MemWrite`
and holds its own `uint8_t screen[1024]`. `write()` stores the byte; `peek()`
returns it (side-effect-free for DISASM/TRACE). A real CPU access injects a wait
state (XRDY) while the RAM settles — not observable to a T-state-accurate guest
beyond the cycle count, so no special modeling is required.

---

## 3. The I/O port — scroll (`OUT`) and status (`IN`)

The card answers **one** I/O port (ADR7–2 = jumper, ADR1–0 = 0). Both `IN` and
`OUT` to it are meaningful.

### 3.1 `OUT port, A` — load scroll / arm the timer

Writing the port does two things:

1. **Latches the scroll parameters** (IC31/IC32) from the data bus D0–D7. The
   value is a **character-row number**: it sets which of the 16 screen rows is
   drawn at the **top** of the display. The 16 rows are then shown in order and
   **wrap modulo 16**, so incrementing the value on each frame scrolls the screen
   up smoothly in hardware — no memory copy. For emulation, treat the low 4 bits
   (`A & 0x0F`) as the top row; display order is `row (top+k) mod 16` for k=0..15.
2. **Fires a one-shot timer** (STATUS), high for **0.25–0.5 s**. Software polls it
   (see below) to pace a slow scroll without a timing loop.

> IC32 presets the START-DISPLAY row and IC31 the "window-shade" duration; loading
> both from the same byte is what makes a full-screen scroll. Modeling the written
> byte's low nibble as the top row reproduces the observable scroll.

### 3.2 `IN port` — status byte

A port read places status on the low data bits (only two bits are defined):

| Bit | Name | Meaning |
|-----|------|---------|
| D0 | STATUS (one-shot) | **1** for 0.25–0.5 s after the last `OUT` to the port, then 0. Poll for 0 to time scroll steps. |
| D1 | SCAN ADVANCE | **1** when the beam is just past the **right-hand margin**. Software writes screen RAM while this is 1 for a flicker-free update. |

D2–D7 are undefined. The STATUS bit is also wired to a spare 7406 inverter whose
output may be strapped to a **vectored-interrupt (VI)** pin, so the timer can
raise an interrupt instead of being polled (§2.7.8).

**Emulation.** Derive both bits from the `Clock`, never a hidden counter:
- **D1 (SCAN ADVANCE)** is a reading off emulated time: from the field rate and
  line timing, it is 1 during the brief right-margin window each scan line. A
  simple faithful model toggles it from `Clock::now()` modulo the line period so a
  polling loop sees it cycle; exactness is not guest-critical (it is a flicker
  hint), but it must not depend on how often the guest polls.
- **D0 (STATUS)** is a deadline: an `OUT` schedules "clear at now + ~0.375 s".
  Return 1 until that `Clock` deadline passes. This mirrors the 6850's TDRE
  pattern (DESIGN.md §7.5) — answer "what time is it?" on read, schedule the clear.

---

## 4. Character and cursor encoding

Each screen byte is latched (IC5/6) and split:

- **D0–D6** address the **MCM6574** character-generator ROM (IC4): 7 address bits
  = 128 glyphs, 4 row-select inputs, 7 dot outputs per scan line. The glyph cell
  is 7 dots wide; character rows are built from the ROM's line patterns. The
  cleared latch presents a SPACE code to the ROM.
- **D7** is the **cursor** bit and **does not enter the ROM**. When D7=1 at a
  position, the cursor circuit marks it: displayed **inverted** (block cursor) and,
  if the blink option is on (SW4), blinking at ~0.5 s (blink oscillator, ~0.5 s
  period). Inversion is a per-character XOR of the video (IC14→IC12), independent
  of the whole-screen polarity in §5.

The MCM6574 is the standard VDM-1 font (Figure 3-1A); the MCM6575 (Fig 3-1B) and
MCM6576 (Fig 3-1C) are pin-compatible alternates with different glyph sets. For
emulation, embed a 6574 ROM dump (128 glyphs) via `cmake/embed_roms.cmake`, or
transcribe the pattern figures. The **cursor is bit 7**, not a separate register —
so the "blinking underscore/block" a terminal shows is just a byte with D7 set.

**Control characters.** Codes `0x00–0x1F` are control characters. Whether they
paint a glyph, blank to a space, or trigger line/screen blanking is switch-set
(§5). Two are special when the CR/VT option is enabled:
- **CR (`0x0D`)** blanks the rest of that character **row** (erase-to-end-of-line).
- **VT (`0x0B`)** blanks all **following rows** to end of screen (erase-to-end).

---

## 5. Switch-selectable options (DIP SW1–SW6, Table 3-1)

Six DIP switches (board area B-1,2) configure the display. These are **hardware
straps** — for emulation expose them as board `properties()` (e.g. `video`,
`cursor`, `blanking`) so a machine file can set them, defaulting to the common
"normal video / blinking cursor / control-chars-blanked" combination.

**SW1 / SW2 — video polarity:**

| SW1 | SW2 | Result |
|-----|-----|--------|
| OFF | OFF | No display |
| OFF | ON | **Normal** video (white on black) |
| ON | OFF | **Reverse** video (black on white) |
| ON | ON | *Not allowed* |

**SW3 / SW4 — cursor:**

| SW3 | SW4 | Result |
|-----|-----|--------|
| OFF | OFF | All cursors suppressed |
| OFF | ON | **Blinking** cursor |
| ON | OFF | **Non-blinking** cursor |
| ON | ON | *Not allowed* |

**SW5 / SW6 — control-character & CR/VT text blanking:**

| SW5 | SW6 | Result |
|-----|-----|--------|
| OFF | OFF | All characters suppressed (only cursor blocks show); CR→EOL & VT→EOScreen blanking **on** |
| OFF | ON | Control chars **blanked**; CR→EOL & VT→EOScreen **on** |
| ON | OFF | Control chars **displayable**; CR→EOL & VT→EOScreen **on** |
| ON | ON | Control chars **displayable**; CR/VT text blanking **off** |

---

## 6. Timing (context, not usually modeled)

- Dot clock 14.318 MHz (crystal); character clock 1.5 MHz; **64 µs** per scan line
  (102 character-clocks × 628 ns). 260 scan lines = one full field.
- 20 character-row *times* per field, 16 of them displayed (a mod, §2.7.7, gives
  50 Hz / different row counts). The display is 16×64 of actual text.
- Horizontal and vertical sync are one-shots (IC30/IC25); an XOR merges them into
  composite sync. None of this is observable to the guest — the guest sees only
  the 1 KB RAM, the scroll latch, and the two status bits.

For a `Display`-backed emulation the board renders 16×64 glyphs into a
`Surface` (e.g. `64*7 × 16*9` logical pixels) once per `pump()`, applies scroll
(§3.1), per-character cursor inversion (§4), and whole-screen polarity (§5), then
`present()`s it. The host window scales that surface with nearest-neighbor.
