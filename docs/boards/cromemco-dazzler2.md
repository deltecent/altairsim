# Dazzler II — Color Graphics, On-Board RAM

**Status:** implemented, `type = "dazzler2"` — the Dazzler's two ports and its four display
modes, with the picture held in 4 KB of RAM on the card. Not bus-mastered. Several things on
the real board are **not** modeled (see *Limitations*).

## The real hardware

A **modern** board (s100computers.com, 2016), not a Cromemco product. It is
register-compatible with the original Cromemco Dazzler (ports `0E`/`0F`, the same bit
meanings, the same pixel encoding, the same video), but it **keeps the picture in 4 KB of
dual-port RAM on the card** instead of reading it out of main memory by DMA. It is a bus
slave: it watches memory writes and copies the ones inside its window into its own RAM. Two
ATF1508 programmable chips hold the logic: one for the bus side and the joystick circuit, one
for the video.

It exists so that the original Dazzler games run on a computer whose memory board cannot
take a DMA, or whose picture address has no RAM. The same board also carries a D+7A-style
joystick/DAC circuit and several video outputs. Those are not part of this model.

## Sources

| Source | Path | Authority |
|---|---|---|
| s100computers.com, *Dazzler II Board* and *Theory* pages | `reference/Cromemco Dazzler.md` §6 | The idea: on-board RAM, write-through, the subtracted base, the random picture before `OUT 0E`. |
| `Dazzler_V2.1d_Sch_fixed.pdf` (schematic) | `reference/Cromemco Dazzler.md` §6 | The P18 jumpers: pins 31–32 closed = second page at `+800H`; pins 3–4 closed = the alternate color map. The extra user port. |
| `BJL_ver4.pld`, `DISPLAY_FINL4CS.PLD` (the CPLD source, `Dazzler_CPLD_Files.zip`) | `reference/Cromemco Dazzler.md` §6 | **The exact logic.** The write window, the 4 KB RAM, the 2 KB scan with `ra11 = page`, the latches with no reset, the status port bits. |

These are a modern board's design files, not a period manual (`DESIGN.md` §0.1), so every fact
here is as good as that one designer's files. Where the files say nothing, this board does not
guess (see *Limitations*).

## Register reference

The same as the Dazzler (`docs/boards/cromemco-dazzler.md`): `OUT 0E` control (D7 on/off,
D6–D0 the base, A15–A9), `OUT 0F` format, `IN 0E` status (D7 odd/even line, D6 end of frame).
**One difference:** `IN 0E` bits D5–D0 read **0**, not 1.

## How it is simulated

`Dazzler2Board` derives from `DazzlerBoard`, as `TarbellDdBoard` derives from `TarbellBoard`.
The ports, the latches, the status clock, the palette and the render are inherited. The
derived board changes two things, through two small hooks in the base class:

- **Where the scan reads a byte** (`sample()`). The original reads main RAM with `Bus::peek`.
  This board reads its own 4 KB array.
- **What D5–D0 of the status port read** (`undrivenBits_`).

And it adds the part a real II has and the original does not:

- `wantsSnoop()` is true. `snoop()` sees every memory write. A write whose address, less the
  base set by `OUT 0E`, is `0000`–`0FFF` is also stored in card RAM. An address below the base
  does not wrap into the window. The display need not be on: the card captures from the moment
  a base is set. Main memory gets the same write as always, and answers every read. The card
  is never read back.
- **Properties.** `page` = `0` or `800`: which 2 KB half of the 4 KB the scan shows. `800` is
  jumper P18 pins 31–32 closed. `seed`: the undefined contents of the card RAM at power-on.
  The same seed gives the same contents.
- `power()` fills the card RAM from the seed. RESET does not touch it.
- Snapshot: the base class's latches, then the 4 KB array.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Card RAM holds only what was written **after** the base was set. Before that it is undefined | A game that draws first and does `OUT 0E` after shows a good picture here and a bad one on the board |
| The picture can sit where main memory has **no RAM** (a ROM, an empty slot) | The picture is blank, as on the original Dazzler |
| The window is `0000`–`0FFF` above the base, with no wrap past `FFFF` | A write low in memory lands in a picture whose base is high |
| The scan shows one 2 KB half of the 4 KB; `page` picks which | Software written for the second page shows the wrong half |
| `IN 0E` D5–D0 read **0** | A program that waits for `3F` (Cromemco's GOTCHA) may never leave its wait loop. It does on the original Dazzler |

## Limitations and deliberate departures

- **The alternate color map is not modeled.** P18 pins 3–4 closed (or extra port D2) selects a
  second set of color equations in the video logic. The board always uses the Dazzler's map.
- **The page bit cannot be set from software.** On the real card it can come from an extra user
  port (SW1, bits 0–2). The address of that port is not in the sources. `page` is the jumper
  only.
- **Setup time, and a base change during display, are not modeled.** The write gate
  (`nxtadrok`) is an input the CPLD source does not define. A base change takes effect at once,
  and the next frame shows it.
- **The latches are cleared at power-on and RESET forces the display off,** as on the original
  Dazzler. The CPLD source has no reset on them, so the real board probably powers up with an
  undefined base and display. This board does not model that.
- **The `IN 0E` D5–D0 = 0 finding comes from the CPLD source** and its own comment. It has not
  been checked on a board.
- **No joystick/DAC circuit and no extra video outputs.** The D+7A (`d7a`) already serves the
  joystick.
- **Not a bus master.** Nothing on the S-100 side can read a pixel back, so the original's DMA
  slowdown is not modeled here either, and the II has none.

## Verification

- `tests/test_dazzler2.cpp` (headless, `NullDisplay`): the type and decode; the window edges
  and the high-base wrap; capture with the display off; a picture drawn before `OUT 0E`; a
  picture where no main memory exists; reads staying in main memory; `page`; the status bits;
  RESET, POWER and the snapshot.

## References

- `reference/Cromemco Dazzler.md` §6.
- `src/boards/cromemco-dazzler2.{h,cpp}`, `src/boards/cromemco-dazzler.{h,cpp}`.
