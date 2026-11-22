# Newtech Model 6 Music Board

**Status:** done — the output port, the 6-bit D/A and the speaker. Sound plays when the
machine has a crystal (`clock_hz` not 0). The amplifier, the volume control and the output
jack are not modeled; see [Limitations](#limitations-and-deliberate-departures).

## The real hardware

The **Model 6 Music Board** (Newtech Computer Systems, Brooklyn, 1977) is an S-100 card
with **one write-only output port**: a 6-bit latch (74LS174), CMOS buffers (CD4050), a
6-bit **R/2R D/A converter**, an **LM380** audio amplifier, a 2-inch speaker, a volume
control and an RCA phono jack. It was sold assembled for $59.95.

The board has no oscillator and no timer. A program makes sound by writing the port in a
timed loop, so the pitch is the program's loop time. Newtech shipped two programs:
**MICROPLAY** (8080), which plays a score from memory, and **MICROSCORE** (North Star
BASIC), which writes that score from note names.

The address is set by jumpers J1–J8, which give A7–A4 true or inverted. A3 must be 0 and A2
must be 1; they are wired, not jumpered. A1 and A0 do not go to the board. As supplied, the
address is `24H`.

## Sources

| Source | Path | Authority |
|---|---|---|
| Newtech *Model 6 Music Board Users Manual*, Rev. A, June 1977 | `reference/Newtech Music Board.md` (scan on s100computers.com) | The decode and its jumper table, the latch and D/A, the audio path, the parts list, MICROPLAY, MICROSCORE and the two test routines. |
| Hal Chamberlin, "Computer Music", *Popular Electronics*, September 1976 | the same file, §7 | More programs for a D/A on a port. Not a source for the board. |

**Where the manual and its own listing disagree:**

- The manual gives **1005 Hz** for its square-wave test routine. The routine's instructions
  add up to 991 T-states a half cycle, which is **1009 Hz** at 2 MHz. The listing wins: the
  board plays whatever timing the program has, and the test checks the count.
- MICROPLAY's comments say the accumulator is complemented. The listing has `XRA M`. This is
  a fact about the program, not the board; `reference/Newtech Music Board.md` has the detail.

## Register reference

One port, at `BASE` to `BASE+3` (default `BASE = 24`).

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `BASE`…`BASE+3` (24–27) | the D/A latch: **DO7–DO2**, DO7 the most significant. DO1 and DO0 are ignored. | not decoded — the board has no input port |

Latch value ↔ voltage at the ladder: `00` = 0 V, `FC` = almost +5 V, 64 steps of about
78 mV. The output is unsigned.

There is no status, no interrupt and no timing requirement. The latch takes the value at
the write.

## How it is simulated

- **`decodes()`:** `IoWrite` only, where `(port & FC) == BASE`. Four addresses, one latch.
  No memory, no `IoRead`.
- **`write()`:** keeps `data & FC`. A write that *changes* the latch is noted as
  `(clock_->now(), level)`. Nothing else happens in the bus cycle.
- **The level** given to the speaker is the latch around the middle of its range
  (`latch ^ 80` as a signed byte), so `00` and `FC` are the two ends of the swing.
- **`pump()`:** the shared **`Speaker`** (`src/host/speaker.h`) renders the slice to PCM and
  pushes it to the host **`Audio`** service (DESIGN.md §7.4). It is the same code that plays
  the D+7A's JS-1 speakers, and the same rules apply: nothing flat out, nothing when the
  level has not moved for half an emulated second, a 50 ms cushion on an empty queue, and a
  dropped slice when the queue is deep. `docs/boards/cromemco-d7a.md` has the table.
- **The coupling capacitor.** C7 (0.1 µF) into R14 + R15 (2.025 MΩ) is a high-pass with its
  corner at about 0.8 Hz. The `Speaker` applies it (`setAcCoupled`), so a level that only
  sits there is 0 V, and a wave between a value and `00` (what MICROPLAY writes) starts and
  stops without a step.
- **No interrupts, no DMA, no units, no endpoint.**
- **`properties()`:** `port` — `04`, `14`, `24` … `F4`. Any other value is refused, with the
  reason.
- **`statusLines()`:** the latch, and the speaker's state: `-> playing`, `-> silent`, or the
  reason nothing can play (no crystal, no sound device, no audio service).

### Reset

- `Reset::PowerOn` (POC*, cold): the latch is set to `00` and the sound not yet played is
  dropped. The real 74LS174 comes up in no defined state; a guest cannot read it, so the
  choice is not visible to software.
- `Reset::Bus` (RESET*, warm): **nothing.** The latch's clear pin is tied to +5 V through
  R13, so the value stays.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The port answers at four addresses (A1 and A0 are not wired). | A program that writes `25`–`27` is silent. Another board at `25`–`27` would not show the conflict the real machine has. |
| DO1 and DO0 are ignored. | A "speaker on any bit" program (the *Popular Electronics* tone routine on bit 0) plays here and was silent on the real board. |
| No bus reset. | A tone loop stopped by RESET would click to 0 V, which the board does not do. |
| The output is AC-coupled. | A program that leaves the latch at a non-zero value would start and end each run with a step. |

## Limitations and deliberate departures

- **The D/A is ideal.** 1% resistors and the CD4050 output resistance are not modeled. No
  software can see them.
- **The amplifier is not modeled.** The LM380's gain, its clipping, the R16/C10 output
  filter and the 220 µF output capacitor are left out. A full-swing level uses the whole
  16-bit range at the default `volume` of 0 dB.
- **No R15 volume control.** The `volume` property (−40 to +24 dB) is a host setting in its
  place, and the guest cannot see it. A soft limiter (`src/host/volume.h`) follows it. The
  first edge after silence is about twice full scale until C7 settles, for about 0.2 s. The
  limiter holds that part of the wave at full scale.
- **No JPR1/JPR2**: there is one speaker, and it is the
  host's sound device.
- **The speaker's own response is not modeled.** A 2-inch speaker plays little below a few
  hundred hertz; the host plays all of it.
- **Sound needs a crystal.** Flat out (`clock_hz = 0`, the default) nothing is played. This
  is the `Audio` service's rule, not the board's (DESIGN.md §7.4).
- **Power-on is `00`**, where the real latch is undefined.

## Verification

- **`tests/test_music6.cpp`** (with a stub `Audio` that keeps every push): the four-address
  decode and that no read is decoded; the `port` strap and each refused value; that the low
  two bits are not latched and make no sound alone; that a bus reset leaves the latch and
  power clears it; **the manual's square-wave test routine, run on an 8080 for one emulated
  second, gives one zero crossing for each 991 T-states**; silence flat out, with the reason
  in `statusLines()`; pushes in `pump()` only; the coupling capacitor (rest is 0 V, and the
  first half wave is the full swing from rest); a Model 6 and a D+7A as two voices; a
  snapshot round-trip that does not play the jumped time.
- **`tests/test_d7a.cpp`** still passes with the speaker code moved to `Speaker`.
- **By measurement** (the real `SdlAudio` path): run the square-wave routine with
  `SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=cap.raw` and count the zero crossings in
  the capture (S16LE, 2 channels, 44100).

  ```
  BOARDS ADD music6 music0
  SET cpu0 clock_hz=2000000
  DEPOSIT 0 97 06 40 05 C2 03 00 2F D3 24 C3 01 00
  RUN 0
  ```

  A steady **1009 Hz** square wave. `SHOW music0` says `-> playing`.

## References

- `reference/Newtech Music Board.md` — the distilled manual and article.
- `src/boards/newtech-music.{h,cpp}`, `src/host/speaker.{h,cpp}`,
  `src/host/level_pcm.{h,cpp}`, `src/host/audio.h`.
- `docs/boards/cromemco-d7a.md` — the other board that plays through `Speaker`.
