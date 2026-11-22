# Cromemco D+7A I/O

**Status:** done — analog + parallel I/O, JS-1 joystick input, and JS-1 sound. Sound plays
when the machine has a crystal (`clock_hz` not 0); see
[The speakers](#the-speakers).

## The real hardware

The Cromemco **D+7AI/O** (1976) is an S-100 card carrying **seven 8-bit A/D input
channels, seven 8-bit D/A output channels, and one 8-bit parallel port** — a byte in and
a byte out. It occupies a block of eight consecutive I/O ports (five jumpers select
`A7..A3`; `A2..A0` pick the port), recommended base **030 octal = 0x18**. Analog values
are 8-bit **two's-complement**, 20 mV per LSB, spanning −2.56 V (`0x80`) to +2.54 V
(`0x7F`). Cromemco's applications: "joystick interfaces, oscilloscope graphics, music and
voice synthesis, and process control."

Its headline use is the input+sound end of a Cromemco Dazzler game console. A
[**JS-1 joystick console**](../../reference/JS-1.md) — a *peripheral*, not a board —
plugs into the D+7A over a 12-conductor cable; one D+7A carries one or two. Each JS-1 is
a two-axis joystick (X/Y pots → analog inputs), four push-buttons (→ parallel-input
bits), and a speaker + amplifier (driven from an analog D/A output). Games: Dazzle
Doodle, Track, Chase!.

## Sources

| Source | Path | Authority |
|---|---|---|
| Cromemco D+7AI/O manual, Rev C (+ Rev B/D, Rev B schematic) | `reference/D+7A.md` (scan on deramp.com) | The port block, the two's-complement scale, the calibration loops, the wait-state timing, the Dazzler flash note. |
| Cromemco JS-1 Joystick manual | `reference/JS-1.md` (scan on manx-docs.org) | The JS-1→D+7A port map (which ports carry which axes/buttons/speaker). |

**Where the manual is silent:** the JS-1 manual does **not** state the button *read
polarity* in prose. It was settled as **active-low** (pressed = 0) from David Hansel's
Arduino Altair 8800 simulator firmware, which drives the period Dazzler games (provided
by Patrick) — a behavioral tiebreaker, not a manifest source. See
[Quirks](#quirks-reproduced) and `reference/JS-1.md` §3.

## Register reference

Ports are `BASE+n`, default `BASE = 0x18`. Every analog port is **A/D on read, D/A on
write**, independently.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `BASE+0` (0x18) | parallel output latch (8 bits) | parallel input byte (8 bits — JS-1 buttons) |
| `BASE+1` (0x19) | analog channel 1 D/A (console 1 speaker) | analog channel 1 A/D (console 1 X axis) |
| `BASE+2` (0x1A) | analog channel 2 D/A | analog channel 2 A/D (console 1 Y axis) |
| `BASE+3` (0x1B) | analog channel 3 D/A (console 2 speaker) | analog channel 3 A/D (console 2 X axis) |
| `BASE+4` (0x1C) | analog channel 4 D/A | analog channel 4 A/D (console 2 Y axis) |
| `BASE+5..7` (0x1D–0x1F) | analog channels 5–7 D/A | analog channels 5–7 A/D |

Analog byte ↔ voltage: `0x7F` = +2.54 V, `0x00` = 0 V, `0xFF` = −0.02 V, `0x80` =
−2.56 V (two's-complement, 20 mV/LSB).

JS-1 button bits in the parallel input byte: console 1 SW1–SW4 → **D0–D3**, console 2
SW1–SW4 → **D4–D7**.

## How it is simulated

- **Decode.** `decodes()` claims `IoRead`/`IoWrite` for the eight ports `BASE..BASE+7`;
  no memory. The base strap must be a multiple of 8 (the `A7..A3` jumpers).
- **read()/write().** `BASE+0` reads/writes the parallel latches; `BASE+n` reads the
  A/D shadow for channel `n` and writes its D/A latch. Read and write are independent
  per port, so a JS-1's X-axis A/D input and its speaker D/A output share one port
  number (0x19 for console 1) with no conflict.
- **The host turn — `pump()`.** Once per slice (never inside a bus cycle) the board
  polls the injected **`Joystick`** service (`src/host/joystick.h`) and folds each
  mapped console into its A/D shadows and the parallel-input nibble. The `Joystick` is
  injected at the composition root exactly like a `Display`: an `SdlJoystick` (a USB
  gamepad, or the keyboard) in the shipping binary, a `NullJoystick` headless, a stub in
  tests. **The board never touches SDL.**
- **Axis mapping.** The SDL host maps gamepad readings from −4096 through +4096 to
  zero on each axis (a dead zone of one eighth of full deflection from center).
  Readings outside it pass through unchanged. A stick resting slightly off center
  therefore reads `0x00`, so GOTCHA's complement-of-rest adjustment is `0xFF` and
  cannot wrap a full deflection into the opposite direction. The keyboard is unchanged.
  A host stick axis (SDL range −32768…32767) is arithmetic-shifted
  `>>8` to the two's-complement A/D byte: center 0 → `0x00`, full positive → `0x7F`,
  full negative → `0x81`. The negative end is held at −127, one short of the A/D's
  `0x80`, so the range is the same each way; `reference/JS-1.md` §4.1 gives the reason
  (GOTCHA reads `0x80` as a move the opposite way). **Y is always inverted** after the
  shift: SDL's +Y is stick *down*, and the games read a positive byte as up.
- **Which controller drives which console.** `joystick1` / `joystick2` accept `none`,
  `auto`, `keyboard`, or a device index (`0`, `1`, …). Both default to `auto`, and `auto`
  is **per-console**: console 1 prefers gamepad 0, console 2 gamepad 1, each falling back
  to the keyboard when its gamepad is absent — so two controllers work unconfigured and
  two `auto` consoles never fight over one stick (`resolveStick(spec, autoIndex)`). Not
  `CONNECT` — a game controller is an enumerated host device, not a `ByteStream` endpoint,
  so it is a strap like `port`, set from TOML or `SET`.
- **`properties()`:** `port`, `joystick1`, `joystick2`, `speaker1`, `speaker2`.
- **`statusLines()`:** the live resolution for `SHOW <id>` — what each console's strap
  currently points at (a named gamepad, the keyboard, or nothing). Keyed on `count()`, the
  same test `resolveStick` uses to pick a source, so the report can't contradict the A/D.
  The monitor also has `SHOW JOYSTICKS` for the raw host inventory (SDL builds). Two more
  lines report the speakers (below).
- **No interrupts, no DMA.** A polling driver (the period norm) is complete.

### The speakers

A JS-1 makes sound with no sound chip: the CPU writes a waveform to the speaker's D/A
channel in a timed loop, and the amplifier plays the voltage. So the sound is the latch's
level over time, and the simulation is three steps. The first two are the shared `Speaker`
(`src/host/speaker.h`), which the Newtech Model 6 (`docs/boards/newtech-music.md`) uses too.

- **`write()` records.** A write that *changes* the level of a speaker's channel is noted
  as `(clock_->now(), level)`. Nothing else happens in the bus cycle.
- **`pump()` renders.** Once per slice the notes become 16-bit PCM (`LevelPcm`,
  `src/host/level_pcm.h`): each output sample is the average of the level over that
  sample's time, and the fractional sample position carries between slices, so slices
  join with no click.
- **The `Audio` service plays** (`src/host/audio.h`, DESIGN.md §7.4): an `SdlAudio` in the
  shipping binary, a `NullAudio` headless, a stub in tests. Each speaker is its own voice;
  the service mixes them. **The board never touches SDL.**

**Which channel is a speaker.** `speaker1` and `speaker2` are `none` or a channel `1`–`7`.
The defaults are the JS-1 manual's wiring: console 1 on channel 1 (port `19`), console 2
on channel 3 (port `1B`). `speaker2 = 2` is the Dazzler II board's wiring (port `1A`); see
`reference/JS-1.md` §2. A channel that is not a speaker latches and plays nothing.

**When a speaker pushes nothing.** A device plays `rate()` samples each real second, and
the board renders `rate()` samples each *emulated* second. Each rule is one way the two do
not agree, or one way there is nothing to play:

| Case | What the board does |
|---|---|
| Flat out (`clock_hz = 0`, `Clock::free()`) | The slice is dropped. Emulated time has no fixed relation to real time, so there is no right pitch to play. |
| The level has not changed for 0.5 emulated second | Nothing is pushed. A constant level is silence, and a guest that never writes the speaker never opens a sound device. |
| The service's queue is empty | 50 ms at the held level goes in first, so the device does not run dry between this slice and the next. |
| The queue holds more than 250 ms | The slice is dropped. The machine is ahead of the device (a crystal, but a run that nothing paces), and the delay must not grow. |
| No emulated time since the last `pump()` | Nothing. A stopped machine makes no sound. |
| More than 1 emulated second since the last render | The gap is skipped, not rendered. The clock was moved under the board (`RESTORE`). |

**What `SHOW <id>` reports.** One line for each speaker, with the first of these that is
true:

| Line ends | Meaning |
|---|---|
| `-> unwired` | The strap is `none`. |
| `-> (no audio service in this build)` | No service was injected. |
| `-> no sound device` | The host's sound device did not open. |
| `-> silent: the machine has no crystal (clock_hz = 0)` | Flat out. |
| `-> playing` | The level changed in the last 0.5 emulated second. |
| `-> silent` | It did not. |

`SdlAudio` opens the device on the first push, so `no sound device` can appear only after
a guest has made a sound. "Playing" is measured in emulated time, which stops with the
machine: at the monitor, just after a stop, the line still says `playing`.

**Snapshots.** The D/A latches are saved, as before. The pending edges and the render
position are host state, like the `Joystick*`, and are not: `deserialize()` and `power()`
restart both speakers at the restored level.

### Reset

- `Reset::PowerOn` (POC*, cold): all A/D shadows, D/A latches, and both parallel bytes
  cleared to 0.
- `Reset::Bus` (RESET*, warm): the D/A outputs and the parallel-output latch clear (0 V,
  0); the A/D shadows are re-read from the host on the next `pump()`. Straps and joystick
  assignments survive. A speaker hears its output go to 0 V at the moment of the reset.

### The READY hold

A real board holds the S-100 READY line low for 5.5 µs on every analog `IN` and `OUT`
(`reference/D+7A.md` §4): the A/D converts, or the D/A's sample-and-hold settles. The CPU
waits in Tw states until the board lets go. The parallel port, `BASE+0`, has no hold.

`read()` and `write()` call `Board::holdReady()` on an analog cycle, with 5.5 µs in
T-states at the machine's clock, rounded up: 11 at 2 MHz, 22 at 4 MHz. The bus adds up the
holds (`Bus::addWaitStates()`), and the run loop charges them to the `Clock` with the
instruction (`DESIGN.md` §7.5). So `OUT 19` takes 21 T-states at 2 MHz, and a tone loop
plays at the pitch a real board gives. With `clock_hz = 0` the divisor is 2 MHz, so 11.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Every analog port is **A/D on read, D/A on write**, independent | A JS-1's X-axis input and speaker output share port 0x19; conflating them makes reading the stick return the last speaker sample. |
| Analog values are **8-bit two's-complement** (`0x80` = most-negative, `0x7F` = most-positive) | Treating them as unsigned puts the joystick center at 0x80 instead of 0x00 and inverts which way is "up". |
| JS-1 buttons: console 1 in **D0–D3**, console 2 in **D4–D7** of the one parallel-input byte | Two consoles' buttons collide, or a game reads the wrong player's fire button. |
| **Buttons are ACTIVE-LOW** — released = `1`, pressed = `0`; the parallel input idles at all-1s | Invert it and every game reads fire-when-idle. Not in the manual — settled from the Altair-duino firmware (`reference/JS-1.md` §3). |

## Limitations and deliberate departures

- **Sound needs a crystal.** With the default flat-out clock (`clock_hz = 0`) the speakers
  are silent, and `SHOW` says so. Nothing throttles the machine to make sound possible;
  set `clock_hz` (4 MHz is the speed of a Cromemco Z-1).
- **No volume control and no device choice.** The gain is fixed at about a quarter of full
  scale for a full-swing square wave, and the sound goes to the host's default playback
  device. The host's own volume control serves.
- **The sound is late by the queue.** On the development Mac the device takes about 110 ms
  to start, and the queue then holds 135–200 ms. The 250 ms cap bounds it.
- **Dazzle Doodle draws only to half deflection.** Its listing draws for readings in
  `0xC0`…`0x3F` and treats the rest as "voltage out of range" (`reference/JS-1.md` §4.1).
  A stick pushed past half gives a reading outside that window, and Doodle stops drawing
  until the stick comes back. No one scale serves both Doodle and GOTCHA at full
  deflection: GOTCHA needs `0x40` or more.
- **The parallel port is joystick-buttons-in only.** Its general digital use (a byte
  `OUT`, arbitrary digital `IN`) latches and snapshots correctly, but the output byte is
  not wired to a host `ByteStream` — no `CONNECT`. Adding that (the 88-PIO pattern) is a
  possible later refinement; the JS-1 does not need it.
- **JS-1→D+7A wiring is fixed to Cromemco's recommended straps.** Only the physical
  controller *index* and the speaker channels are configurable; the stick and button
  assignments are the standard ones (X/Y → 0x19/0x1A and 0x1B/0x1C, buttons → D0–D3 /
  D4–D7). A program using a nonstandard
  strap would need per-channel override properties, deliberately not added for now.
- **Keyboard-as-a-joystick needs a focused SDL window.** The keyboard fallback reads
  live key state, which SDL only reports to a window that holds the keyboard — so it
  works with a Dazzler window focused (`SET DISPLAY focus=on`, or click it), and reads
  centered otherwise. A real USB controller has no such requirement.

## Verification

- **`tests/test_d7a.cpp`** (headless, with a `StubJoystick`): port decode and the
  8-aligned strap; parallel latch/read; analog D/A round-trip and A/D independence; the
  axis → two's-complement mapping for both consoles (0x19/0x1A and 0x1B/0x1C), with
  full scale at `0x7F` and `0x81` and the always-inverted Y; that a full deflection each
  way passes GOTCHA's large-move test and keeps its sign; button
  bits in the correct nibbles; per-console `auto` resolution (console 2 takes gamepad 1,
  falls back to the keyboard) and its `statusLines()` report; that the host
  is polled in `pump()` and not in a bus cycle; and a snapshot round-trip. For the
  speakers (with a `StubAudio` that records every push): a tone loop on port `19` at a
  crystal reaches the stub at the right frequency; flat out pushes nothing and
  `statusLines()` names the reason; `speaker1 = none` pushes nothing and `speaker2 = 2`
  routes port `1A`; a channel that is not a speaker pushes nothing; a deep queue drops the
  slice; pushes happen in `pump()` only; a bus reset returns the level to 0. For the
  READY hold (a real 8080 through the debugger's run loop): an analog `IN`/`OUT` is 10 + 11
  T-states at 2 MHz and the parallel port 10; 22 at 4 MHz, 17 at 3 MHz, 11 flat out; a
  monitor `OUT` between runs costs the next instruction nothing; and a CPU tone loop
  renders at the pitch the hold gives it.
- **`tests/test_d7a.cpp`** also drives an SDL virtual gamepad when SDL3 is available:
  both signs of center drift, both full deflections, and both axes reach the guest
  through the real host polling path. The checks cover GOTCHA's calibrated direction,
  both dead-zone edges and the unchanged Dazzle Doodle drawing boundaries.
- **`tests/test_level_pcm.cpp`**: a constant level, a square wave's zero crossings per
  second, no drift in the sample count across many small renders, and the weighted average
  of an edge inside a sample.
- **Smoke test:** a Dazzler machine with a D+7A boots; a program that does `IN 19` /
  `IN 18` runs, exercising the real `SdlJoystick` runtime path (SDL gamepad subsystem
  init on first pump, with or without a controller plugged in).
- **By ear** (the real `SdlAudio` path; no test can hear). In a Dazzler machine
  at 4 MHz:

  ```
  BOARDS ADD d7a d7a0
  DEPOSIT 0 3E 40 D3 19 06 80 05 C2 06 00 2F C3 02 00
  RUN 0
  ```

  The loop writes `40`, then `BF`, to port `19`, with a half period of
  31 + 22 + 15 × 128 = 1973 T-states (the 22 is the READY hold at 4 MHz): a steady
  **1014 Hz** square wave. `SHOW d7a0` says
  `-> playing`. After `SET cpu0 clock_hz=0`, `RUN 0` is silent and `SHOW d7a0` gives the
  no-crystal reason on both speaker lines.

## References

- `reference/D+7A.md`, `reference/JS-1.md` — the distilled hardware specs.
- `src/boards/cromemco-d7a.{h,cpp}`, `src/host/joystick.h`, `src/host/joystick_null.h`,
  `src/host/joystick_sdl.{h,cpp}`.
- `src/host/audio.h`, `src/host/audio_null.h`, `src/host/audio_sdl.{h,cpp}`,
  `src/host/speaker.{h,cpp}`, `src/host/level_pcm.{h,cpp}`.
- `docs/boards/cromemco-dazzler.md` — the picture half of a Dazzler game console.
