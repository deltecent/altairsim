# Cromemco JS-1 Joystick Console

Source: [Cromemco JS-1 Joystick Manual.pdf](#)

Cromemco, Inc., "JS-1 Joystick Console", fetched from manx-docs.org
(`.../harte/Cromemco/Cromemco JS-1 Joystick Manual.pdf`). **Not an S-100 board** — a
peripheral that plugs into the [D+7A analog I/O board](D+7A.md) over a 12-conductor
cable. One D+7A interfaces **one or two** JS-1 consoles. Each console is a two-axis
joystick, four push-button switches, and an audio amplifier + 100 Ω speaker in one
enclosure. Cromemco shipped the Dazzler games **Dazzle Doodle**, **Track**, and
**Chase!** to use it.

This file captures how a JS-1 presents to software — which D+7A ports carry the axes,
buttons, and speaker — so the emulator can map a host USB gamepad (or the keyboard)
onto the same ports. The pots, diodes, and speaker-amp transistor are hardware detail.

---

## 1. What a JS-1 is, electrically

- **Two-axis joystick**: an X pot and a Y pot across ±12 V (clamped by two 1N5242
  12 V zeners). Each wiper is a D+7A **analog input** — the A/D reads the stick
  position as a two's-complement byte (centered ≈ 0x00).
- **Four push-buttons** (SW1–SW4), each with a 10 kΩ resistor, wired to four bits of
  the D+7A **parallel input** port.
- **Speaker + amp**: a 2N3904 driving a 100 Ω speaker, fed from a D+7A **analog
  output** (D/A). Sound is made by the CPU writing a waveform to that D/A in a timed
  loop.

## 2. Recommended D+7A port assignment

Cromemco's recommended straps (D+7A base 030 octal = 0x18), from the manual:

**Console 1**

| Signal | D+7A port | Notes |
|--------|-----------|-------|
| Joystick **X** axis | analog input **031 (0x19)** | A/D, two's-complement |
| Joystick **Y** axis | analog input **032 (0x1A)** | A/D, two's-complement |
| SW1 | parallel input **030 (0x18)** bit **D0** | |
| SW2 | parallel input 030 bit **D1** | |
| SW3 | parallel input 030 bit **D2** | |
| SW4 | parallel input 030 bit **D3** | |
| Speaker | analog **output** **031 (0x19)** | D/A — same port # as X-axis, opposite direction |

**Console 2**

| Signal | D+7A port | Notes |
|--------|-----------|-------|
| Joystick **X** axis | analog input **033 (0x1B)** | |
| Joystick **Y** axis | analog input **034 (0x1C)** | |
| SW1–SW4 | parallel input 030 (0x18) bits **D4–D7** | the high nibble |
| Speaker | analog **output** **033 (0x1B)** | |

Note the elegance the D+7A's independent read/write per port buys: console 1's X-axis
A/D **input** and its speaker D/A **output** are the *same port number* 0x19, read one
way and written the other. So both consoles fit in two analog ports each plus the one
shared parallel input byte.

**One source disagreement — output only, matters only for sound.** The JS-1 manual routes
console 2's speaker to analog output **1BH** (033 octal), the same port as its X-axis A/D
*input*. The s100computers.com Dazzler II board instead drives *both* speakers from the
low two outputs — JS1 speaker = `OUT 19H`, **JS2 speaker = `OUT 1AH`** — and implements no
other analog outputs. Reading the joysticks is identical either way; the disagreement is
purely which port carries console 2's D/A. altairsim latches all seven D/A channels, so a
future sound path can read whichever port the target software writes.

The manual adds a hardware note for two-console use: D+7A resistors R29/R30 must be
10 Ω (jumpers), not 100 Ω. That is a board strap, not software-visible.

## 3. Button polarity — ACTIVE-LOW (pressed = 0)

The JS-1 manual is clear on the *wiring* (SW1→D0 … SW4→D3, each with a 10 kΩ resistor)
but does **not** state the read polarity in prose. It is **active-low**: the switches are
pulled to +5 V through 10 kΩ, so a bit reads **1 when released and 0 when pressed**. An
idle (or absent) console reads its nibble all-1s; a press pulls that bit to 0.

Settled from three independent sources (Patrick, 2026-07-23), all agreeing:

- **The s100computers.com "Dazzler II" board**, a modern S-100 card that implements the
  D+7A's joystick core circuit, describes port 18H's button bits as *"Low if pressed"*
  with *"+5V from 10K source"* — a hardware statement of active-low. It also confirms the
  two's-complement analog scale (80H = −, 00H = center, 7FH = +) and the input port map.
- **ADCTEST** (`adctest.hex`, a period D+7A/joystick diagnostic, disassembled): reads the
  buttons at `IN 18H` and the four axes at `IN 19H`/`1AH` (console 1) and `IN 1BH`/`1CH`
  (console 2) — a first-hand artifact confirming §2's port map exactly.
- **David Hansel's Arduino Altair 8800** simulator firmware builds the joystick byte as
  `if (!joy->b1) daz_msg[0] |= 1;` — the bit is set (=1) when the button is *not* pressed
  — and sends X/Y as signed bytes centered at 0.

## 4. Emulating the JS-1 with a host gamepad

The [D+7A board](../docs/boards/cromemco-d7a.md) maps a host stick onto these ports:
a gamepad's left-stick X/Y (SDL range −32768…32767) is arithmetic-shifted to the
two's-complement byte the A/D returns (`>>8`: center→0x00, extremes→0x80/0x7F), and up
to four face buttons become the four parallel-input bits. The D/A speaker output is
captured for the (future) SDL audio path — see the sound-feasibility section of the
board doc. A JS-1's own oscillator-free design means everything here is a value a guest
reads back, which is exactly what makes it emulable from a USB controller.

## 5. Parts (informative)

Two-axis joystick, 4 push-button switches, 100 Ω speaker, 12-conductor cable, two
1N5242 zeners, 2N3904, five 10 kΩ resistors, two 150 Ω resistors, a 10 µF cap — the
BOM of a passive pot-and-switch box with a one-transistor audio amp. Nothing here has
state a snapshot must carry; the state is on the [D+7A](D+7A.md).
