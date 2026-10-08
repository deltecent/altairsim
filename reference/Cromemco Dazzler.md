# Cromemco Dazzler Color Graphics Interface

Source: [Cromemco_Dazzler_Instruction_Manual.pdf](#)

Cromemco, Inc., "Cromemco Dazzler Instruction Manual", Part No. 023-0003,
November 1978 (U.S. Patent 4,121,283). The Dazzler is the first color graphics
card for the S-100 bus: a two-board set that uses **high-speed DMA** to read a
bitmap straight out of the host computer's memory and translate it into a standard
composite color-TV signal. It is the concrete reason DMA/bus-mastering exists in
this simulator (DESIGN.md §4.5, §7.4): the card is a **bus master**, not a slave.
(The later s100computers.com **Dazzler II** keeps the picture in its own RAM and does
not use DMA — see §6.)

This file captures everything needed to *emulate* the board: the two output ports
and one input port, the DMA framebuffer layout and scan order, and the byte→pixel
encoding for all four display modes. Assembly, tune-up, the color-subcarrier
analog chain, and the schematics are omitted. See the memory note
`dma-bus-mastering` for the `requestsBus()`/`busMaster()`/`serviceDma` path this
board rides.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| Ports | `OUT 0x0E` (control), `OUT 0x0F` (format), `IN 0x0E` (status) |
| DMA | Card masters the bus (~1 MB/s), reads framebuffer from main RAM; ~15% CPU slowdown |
| Framebuffer size | **512 bytes** (1 page) or **2 KB** (4 pages), set by `0x0F` D5 |
| Framebuffer base | `OUT 0x0E` bits D6–D0 = A15–A9 → **`base = (byte & 0x7F) << 9`** (512-byte aligned) |
| On/off | `OUT 0x0E` **D7**: 1 = Dazzler on, 0 = off (front-panel CLEAR also forces off) |
| Resolution | `0x0F` D6: 0 = normal (32×32 / 64×64), 1 = **X4** (64×64 / 128×128) |
| Color/mono | `0x0F` D4: 1 = color, 0 = black-and-white (16 greys) |
| Status D7 | ODD/EVEN line: 0 during odd lines, 1 during even; **0 while D6 is 0** |
| Status D6 | END OF FRAME: goes **0 for ~4 ms** between frames |
| Status D5–D0 | Not driven: read **1** (the port reads `3F` for the whole vertical blank) |
| Subcarrier | 3.579545 MHz crystal; 1 V neg-sync composite into 52 Ω |

Two cards can be synced (SYNC IN/OUT strap) to drive two TVs; not modeled.

---

## 2. Ports

### 2.1 `OUT 0x0E` — control / framebuffer address

```
 D7    D6   D5   D4   D3   D2   D1   D0
ON/OFF A15  A14  A13  A12  A11  A10  A9
```

- **D7 = ON/OFF.** 1 turns the Dazzler on (begins DMA + video), 0 turns it off.
  Depressing the front-panel CLEAR switch also forces it off.
- **D6–D0 = the high 7 bits of the framebuffer start address** (A15–A9). The
  picture therefore begins on a **512-byte boundary**: `base = (val & 0x7F) << 9`.
  The framebuffer must be static RAM with ≤1 µs access on real hardware (moot in
  emulation).

### 2.2 `OUT 0x0F` — format

| Bit | 0 | 1 |
|-----|---|---|
| D7 | *unused* | *unused* |
| D6 | Normal resolution | **Resolution X4** |
| D5 | Picture = **512 bytes** | Picture = **2 KB** |
| D4 | **Black-and-white** | **Color** |
| D3 | (X4 only) intensity/color: HI/LO or grey MSB | " |
| D2 | (X4 only) **Blue** off | Blue on |
| D1 | (X4 only) **Green** off | Green on |
| D0 | (X4 only) **Red** off | Red on |

- **D3–D0 are used only in X4 mode**, where they set the single color/intensity
  for the whole picture (each bit in the framebuffer is just on/off). In color X4
  the nibble is RGB + HI/LO intensity; in B&W X4 the nibble is one of 16 grey
  levels (D3 = MSB). In **normal** resolution D3–D0 are ignored — color lives in
  the framebuffer bytes (§4).
- **D6 (resolution) × D5 (size)** give the four element counts:

| D6 (res) | D5 (size) | Bytes | Picture |
|----------|-----------|-------|---------|
| 0 normal | 0 | 512 | **32 × 32** color/grey elements |
| 0 normal | 1 | 2 KB | **64 × 64** color/grey elements |
| 1 X4 | 0 | 512 | **64 × 64** on/off elements |
| 1 X4 | 1 | 2 KB | **128 × 128** on/off elements |

### 2.3 `IN 0x0E` — status (2 bits)

```
   D7                 D6
ODD/EVEN LINE      END OF FRAME
```

- **D7** = 0 during odd scan lines, 1 during even.
- **D6** = 0 for ~4 ms between frames (vertical blank), else 1.

**Two facts the manual's prose does not give.** Both are from David Hansel's Altair 8800
simulator firmware (`dazzler.cpp`, `dazzler_in`, github.com/dhansel/Altair8800), which
gives the Dazzler schematic as its source for the second:

- **D5–D0 are not driven.** The card drives only DI6 and DI7, so the other six bits read 1.
- **D7 is 0 while D6 is 0.** The 7493 counter that makes the D7 output is held in reset
  while D6 is low. So D7 does not toggle in the vertical blank, and the line count starts
  again with each frame.

Together: the port reads `3F` for the whole ~4 ms vertical blank, one time each frame, and
`FF` or `7F` during the picture. Cromemco's GOTCHA times itself on this. Its delay waits
for `3F` ("the end of frame"), then for a value that is not `3F` ("the start of frame"),
30 times for each move: 0.5 s.

**Emulation:** derive both from the `Clock` (frame ≈ 1/60 s), never from a
poll-driven counter — a spin loop on END-OF-FRAME must see it fall because
emulated time advanced, so a recorded session replays identically (cf. the
`Spindle` rule, DESIGN.md §7.5.1).

---

## 3. DMA and framebuffer scan order

The card issues HOLD, waits for the CPU's HLDA (end of the current machine cycle),
then reads the framebuffer over the bus at ~1 MB/s — the exact
`requestsBus()`/`busMaster()->step(Bus&)` path in `tests/test_dma.cpp`. Cycle-steal
(assert `requestsBus()` for a scan, drop it, re-arm a `Clock` deadline for the next
frame) reproduces the manual's "~15% slowdown" without monopolizing the bus.

**Quadrant layout ("Memory Map Of Dazzler Picture").** The framebuffer is scanned
as up to four **512-byte quadrants**, each a 32×32-element block, tiled 2×2:

```
 Quadrant 0 (bytes    0–511)  |  Quadrant 1 (bytes  512–1023)   ← top
 Quadrant 2 (bytes 1024–1535) |  Quadrant 3 (bytes 1536–2047)   ← bottom
```

- A **512-byte** picture displays **only quadrant 0** (top-left 32×32, or 64×64
  in X4).
- A **2 KB** picture displays all four quadrants (→ 64×64, or 128×128 in X4).
- **Within a quadrant** bytes run left-to-right, top-to-bottom, **16 bytes per
  row** (each byte = 2 horizontally-adjacent elements in normal mode): row 0 =
  bytes 0–15, row 1 = 16–31, …, row 31 = 496–511.

---

## 4. Byte → pixel/color encoding

### 4.1 Normal resolution — one byte = two adjacent elements

Each nibble is one element; the byte holds two horizontally-adjacent elements:

```
 D7   D6    D5   D4  | D3   D2    D1   D0
HI/LO BLUE GREEN RED | HI/LO BLUE GREEN RED
  adjacent element   |   first element
```

Per element (a nibble): D0/bit0 = **Red**, D1 = **Green**, D2 = **Blue**, D3 =
**HI/LO intensity**. So the low nibble (D0–D3) is the left element, the high
nibble (D4–D7) the element to its right. In **B&W** mode the 4 bits instead
encode one of **16 grey levels**. 32×32 = 512 bytes; 64×64 = 2 KB.

### 4.2 X4 resolution — one byte = eight adjacent on/off elements

Each bit turns one element on/off; the whole picture's color/intensity comes from
`0x0F` D3–D0. The eight bits map to a 4-wide × 2-tall cell, as two side-by-side
2×2 blocks (low nibble = left block, high nibble = right block):

```
 col:  0    1    2    3
 row0: D0   D1   D4   D5
 row1: D2   D3   D6   D7
```

i.e. left 2×2 = D0(tl) D1(tr) D2(bl) D3(br); right 2×2 = D4 D5 D6 D7. 512 bytes →
64×64; 2 KB → 128×128.

### 4.3 Color

The 4-bit element (normal mode) or the `0x0F` nibble (X4 color) is R/G/B + HI/LO:
16 colors as intensity × primary mix. For a `Display` palette, build 16 `Color`
entries: `{R?0xFF:0x00}` etc. scaled by HI/LO (e.g. full vs ~2/3), and in B&W map
the 4-bit value to a linear grey ramp. Push it with `Display::setPalette`.

---

## 5. Test program (front-panel driven — good acceptance test)

The manual's tune-up program (for a Cromemco Z-1 / Altair front panel at input
port `0xFF`) turns the Dazzler on at framebuffer `0x0000` and takes the format
live from the sense switches — a ready-made demo for our front-panel board:

```
ADDR  OBJECT     MNEMONIC        COMMENT
0000  3E 80    TEST: LD  A,80H   ; 1000_0000b: ON, base = 0x0000
0002  D3 0E          OUT 0EH,A   ; control port
0004  DB FF          IN  A,0FFH  ; read sense switches (port FF)
0006  D3 0F          OUT 0FH,A   ; format = sense switches
0008  C3 00 00       JP  TEST    ; loop
```

Raise sense-switch A12 (→ `0x0F` D4 = color) for a color "quilt" pattern. With a
2 KB pattern in RAM from `0x0000`, this exercises the on/off enable, the framebuffer
base, resolution/size/color decode, and the DMA scan in one loop — pair it with a
seeded framebuffer for a headless `test_dazzler` assertion, or run it live under
the `fp` front-panel board for a visual check.

---

## 6. Dazzler II (on-board RAM, no DMA)

Source: s100computers.com, *Dazzler II Board* and *Dazzler II Board Theory* (a modern
board, not a Cromemco document). It is register-compatible with the original (ports
`0E`/`0F`, same bit meanings, §2) but **holds the picture in its own RAM**, so it never
takes the bus.

| | Original Dazzler (§1–§5) | Dazzler II |
|---|---|---|
| Picture memory | Main RAM, read by the card | 4K dual-port SRAM **on the card** |
| Bus role | Bus master: HOLD/HLDA DMA | Bus slave: only watches writes |
| CPU slowdown | ~15% | none |
| Reading the picture back | Reads main RAM | Reads **main RAM** (card RAM is write-only) |
| Needs main RAM at the picture address | Yes | No — any memory size or type |

**Write-through.** A memory write inside the picture window goes to main memory **and**
into the card's RAM. A read always comes from main memory, never from the card. Dual-port
RAM means the display readout and a bus write never conflict, so the bus speed does not
matter.

**Window.** `OUT 0E` D6–D0 is the base address of the upper-left pixel, as before. The page
says the card subtracts this base from S-100 `A15–A0`; if the result is `0000`–`0FFF`, the
data is also written to card RAM. The page also calls the overlay a "2K window". The card
holds 4K and shows 2K of it. Which 2K is shown: remove jumper P18 pins 31–32 to move the
display up `800H`, or fit the optional U18 (74LS574) and set its bit 0 from software. The
card decodes `A0–A23`; switch SW6 sets the upper lines (all closed = lowest 64K). With the
IDT 7132 chip the card holds only 2K, which is always shown.

**Trap for emulation.** Card RAM holds only what was **written after** the base was set.
Original games that cleared or drew the picture *before* they turned the Dazzler on and
did `OUT 0E` show **random data** on the II. Software that re-initializes the picture area
after the base is set works. A model of the II must therefore start its card RAM with
undefined contents and fill it from bus writes only — it must not read main RAM at scan
time.

**Emulation consequence.** No `requestsBus()`. The board watches memory writes, keeps its
own 4K array, and the frame scan reads that array. Registers, status bits (§2.3) and the
pixel encoding (§4) do not change.

**Not stated by the page:** the setup time of the base before the first write, the exact
subtract logic, and what happens if the base changes while the picture is displayed.

**Also on the II board, outside this file:** four video outputs (RGB or B/W at TV or VGA
rates, YPbPr, NTSC composite) and a built-in joystick/DAC circuit like the D+7A, so one
board runs the Dazzler games (`reference/JS-1.md`).
