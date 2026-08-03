# Cromemco D+7A I/O

**Status:** done — analog + parallel I/O and JS-1 joystick input. Sound is designed but
not built (see [Limitations](#limitations-and-deliberate-departures)).

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
- **Axis mapping.** A host stick axis (SDL range −32768…32767) is arithmetic-shifted
  `>>8` to the two's-complement A/D byte: center 0 → `0x00`, full positive → `0x7F`,
  full negative → `0x80`. `js1_invert_y` / `js2_invert_y` flip a console's Y sense
  first, for a stick whose pot orientation opposes SDL's up = negative.
- **Which controller drives which console.** `joystick1` / `joystick2` accept `none`,
  `auto`, `keyboard`, or a device index (`0`, `1`, …). Both default to `auto`, and `auto`
  is **per-console**: console 1 prefers gamepad 0, console 2 gamepad 1, each falling back
  to the keyboard when its gamepad is absent — so two controllers work unconfigured and
  two `auto` consoles never fight over one stick (`resolveStick(spec, autoIndex)`). Not
  `CONNECT` — a game controller is an enumerated host device, not a `ByteStream` endpoint,
  so it is a strap like `port`, set from TOML or `SET`.
- **`properties()`:** `port`, `joystick1`, `joystick2`, `js1_invert_y`, `js2_invert_y`.
- **`statusLines()`:** the live resolution for `SHOW <id>` — what each console's strap
  currently points at (a named gamepad, the keyboard, or nothing). Keyed on `count()`, the
  same test `resolveStick` uses to pick a source, so the report can't contradict the A/D.
  The monitor also has `SHOW JOYSTICKS` for the raw host inventory (SDL builds).
- **No interrupts, no DMA.** A polling driver (the period norm) is complete.

### Reset

- `Reset::PowerOn` (POC*, cold): all A/D shadows, D/A latches, and both parallel bytes
  cleared to 0.
- `Reset::Bus` (RESET*, warm): the D/A outputs and the parallel-output latch clear (0 V,
  0); the A/D shadows are re-read from the host on the next `pump()`. Straps and joystick
  assignments survive.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Every analog port is **A/D on read, D/A on write**, independent | A JS-1's X-axis input and speaker output share port 0x19; conflating them makes reading the stick return the last speaker sample. |
| Analog values are **8-bit two's-complement** (`0x80` = most-negative, `0x7F` = most-positive) | Treating them as unsigned puts the joystick center at 0x80 instead of 0x00 and inverts which way is "up". |
| JS-1 buttons: console 1 in **D0–D3**, console 2 in **D4–D7** of the one parallel-input byte | Two consoles' buttons collide, or a game reads the wrong player's fire button. |
| **Buttons are ACTIVE-LOW** — released = `1`, pressed = `0`; the parallel input idles at all-1s | Invert it and every game reads fire-when-idle. Not in the manual — settled from the Altair-duino firmware (`reference/JS-1.md` §3). |

## Limitations and deliberate departures

- **Sound is NOT produced (designed, not built).** A JS-1 makes sound by the CPU writing
  a waveform to a speaker's analog-**output** port (0x19 for console 1; console 2 is
  `OUT 0x1B` per the JS-1 manual or `OUT 0x1A` per the Dazzler II board — see
  `reference/JS-1.md` §2) in a timed loop. The D/A writes for all seven channels are
  latched here so an audio path can read whichever the target software uses, but nothing
  plays them yet. **SDL3 is fully capable of the playback** (`SDL_INIT_AUDIO`,
  `SDL_OpenAudioDeviceStream`, `SDL_PutAudioStreamData`); the hard part is **not** SDL —
  it is reconciling *emulated* time with *wall-clock* audio time. Each D/A write happens
  at an emulated instant (`clock_->now()`, in T-states) but the device plays at 44100 Hz,
  so each write must be timestamped and sample-and-hold resampled to the device rate.
  That only sounds right when the machine runs **at or near real time** — a *paced*
  clock (`Clock::free() == false`, a real `clock_hz`, as `machines/d7a.toml` uses);
  under the default flat-out clock emulated time compresses arbitrarily and the audio is
  garbage. The recommended shape mirrors the display/joystick seams: an injected
  `Audio` host service (`src/host/audio.h`) + `NullAudio` + `SdlAudio`, with the board
  appending `(clock_->now(), value)` to a ring buffer on each speaker-port write and
  `pump()` resampling it to the device (keep ~50–100 ms buffered for latency vs.
  underrun). The project already turns emulated-time-tagged samples into fixed-rate PCM
  for WAV cassette work (`src/host/tapemodem.cpp`), so the conversion pattern exists to
  copy. This is a separable follow-up milestone; committing a machine to a throttled
  clock is the load-bearing decision it needs.
- **The 5.5 µs / 11-wait-state READY hold on analog cycles is not modeled.** Like the
  Dazzler's DMA slowdown, `read()`/`write()` are pure over state and the bus does not
  charge per-cycle wait states to the `Clock`. It matters only for bit-exact JS-1 *sound
  pitch* (it lengthens the CPU's sample-output loop), which belongs with the sound
  follow-up.
- **The parallel port is joystick-buttons-in only.** Its general digital use (a byte
  `OUT`, arbitrary digital `IN`) latches and snapshots correctly, but the output byte is
  not wired to a host `ByteStream` — no `CONNECT`. Adding that (the 88-PIO pattern) is a
  possible later refinement; the JS-1 does not need it.
- **JS-1→D+7A wiring is fixed to Cromemco's recommended straps.** Only the physical
  controller *index* is configurable; the channel/bit assignments are the standard ones
  (X/Y → 0x19/0x1A and 0x1B/0x1C, buttons → D0–D3 / D4–D7). A program using a nonstandard
  strap would need per-channel override properties, deliberately not added for now.
- **Keyboard-as-a-joystick needs a focused SDL window.** The keyboard fallback reads
  live key state, which SDL only reports to a window that holds the keyboard — so it
  works with a Dazzler window focused (`SET DISPLAY focus=on`, or click it), and reads
  centered otherwise. A real USB controller has no such requirement.

## Verification

- **`tests/test_d7a.cpp`** (headless, with a `StubJoystick`): port decode and the
  8-aligned strap; parallel latch/read; analog D/A round-trip and A/D independence; the
  axis → two's-complement mapping for both consoles (0x19/0x1A and 0x1B/0x1C); button
  bits in the correct nibbles; per-console `auto` resolution (console 2 takes gamepad 1,
  falls back to the keyboard) and its `statusLines()` report; `js_invert_y`; that the host
  is polled in `pump()` and not in a bus cycle; and a snapshot round-trip.
- **Smoke test:** `altairsim -f machines/d7a.toml` boots; a program that does `IN 19` /
  `IN 18` runs, exercising the real `SdlJoystick` runtime path (SDL gamepad subsystem
  init on first pump, with or without a controller plugged in).

## References

- `reference/D+7A.md`, `reference/JS-1.md` — the distilled hardware specs.
- `src/boards/cromemco-d7a.{h,cpp}`, `src/host/joystick.h`, `src/host/joystick_null.h`,
  `src/host/joystick_sdl.{h,cpp}`, `machines/d7a.toml`.
- `docs/boards/cromemco-dazzler.md` — the picture half of a Dazzler game console.
