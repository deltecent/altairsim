# The built-in terminal

`CONNECT sio0:a terminal` gives a serial line a windowed VT100 (or ADM-3A, VT52, H19) that
the simulator draws itself — no telnet client, no external emulator. This chapter is how that
is built, and how to add an emulation.

It is a **serial endpoint**, so read [Serial I/O](serial-io.md) first: the terminal is one
more `ByteStream` behind `resolveEndpoint`, and no board knows it exists. What is new is that
the stream has a **screen** and a **keyboard**, and both live in the host `Display`.

## Two halves: the screen engine, and the dialect

The terminal engine is `src/host/terminal/`, and it is deliberately split so that the part
that differs between terminals is small:

| Piece | File | Owns |
|---|---|---|
| `TerminalScreen` | `screen.h` | the attributed character grid and its primitive ops (`putGlyph`, `lineFeed`, `eraseToEol`, cursor). Sized `(rows, cols)` at construction — nothing is baked to 80×24. |
| `TerminalEmulator` | `emulator.h` | the **dialect**: a byte→ops state machine (`feed()`), plus the reply FIFO and key encoding. This is the only piece that changes per terminal. |
| `TerminalRenderer` | `renderer.h` | paints a `TerminalScreen` into a host `Display`, once per frame, using a `TerminalFont`. |
| `TerminalFont` | `font.h` | the glyph source. The bundled default is the authentic **DEC VT220** face (`boards/terminal-vt220font.h`, a 10×20 cell); the VDB-8024 supplies its own CGEN PROM through the same seam. |

**This engine was extracted from the VDB-8024 board** (issue #244, Task 1). The SD Systems
VDB-8024 is a built-in terminal that happens to be reached through I/O ports and speaks its
own firmware dialect; `Vdb8024Board` is now a client of this engine, with an `SdVdb16Emulator`
(`src/boards/sd-vdb8024.cpp`) as its dialect. The generic `terminal:` endpoint is the same
engine reached through a serial `ByteStream` with a pluggable dialect instead.

`TerminalFont::glyphRow()` returns a **`uint16_t`** scan line with **bit 15 the leftmost dot**, so
a cell may be wider than eight dots — the VT220 face is 10 wide. An eight-dot font (the VDM-1, the
VDB CGEN) MSB-aligns its byte into the top of the word, keeping bit 15 leftmost for every font. The
built-in terminal's phosphor palette (green default, amber) is set on its `TerminalRenderer` by
`TerminalStream::setPhosphor()`, driven by the `phosphor=` connect-string option.

## The emulator contract

`TerminalEmulator` (`emulator.h`) is short and worth reading in full. The load-bearing part is
that a terminal is **bidirectional**, and everything flowing toward the guest goes through one
FIFO:

- `feed(byte, screen)` — apply one display byte; advance any multi-byte escape sequence. May
  enqueue a **reply** (a report the guest asked for, e.g. a VT100's `ESC[6n` cursor position).
- `keyAscii(byte)` — a host keystroke with an ASCII code. The base passes it straight through;
  an emulator overrides only to remap.
- `keySpecial(Key)` — a host key with **no** ASCII (`Key::{Up,Down,Left,Right,Home}`). This is
  the classic difference: an arrow is `ESC[A` on a VT100, `ESC A` on a VT52, `Ctrl-K` on an
  ADM-3A. The base sends nothing; an emulator with arrows overrides it.

Reports and keystrokes both land in the **reply FIFO**, drained with `takeReply()`. That is
correct: to a UART, a report and a keystroke are both just characters arriving on the line, and
the guest cannot — must not — tell them apart. `reset()` abandons any partial sequence and
clears the FIFO, as an S-100 RESET does.

## Registering an emulation

The dialects are a name→factory table, `emulations.{h,cpp}` — the one place that knows which
terminals exist, mirroring `usioBuiltins()`. Adding a terminal is:

1. Write the `TerminalEmulator` subclass (e.g. `src/host/terminal/vt52.h`).
2. Add one row to the table in `emulations.cpp` (`{"vt52", "DEC VT52", &makeVt52}`).
3. Give it grid-assertion tests in `tests/test_terminal.cpp`.

The endpoint grammar, the `HELP CONNECT` gloss and the error messages all read this table, so
nothing else changes. **The first row is the default** when a `terminal:` names no emulation
(`vt100`). Names match case-insensitively, like a board id or a property.

The four shipped dialects: `vt100`/`ansi` (a working editor subset — CSI moves, ED/EL, SGR,
`ESC[6n`, DECSC/DECRC, DECCKM arrow flip); `adm3a` (the dumb CP/M terminal — control-code
moves, `ESC =` cursor addressing, **no reports**, because the hardware had none); `vt52`; and
`h19`, which is a VT52 superset in Heath mode and **delegates to `Vt100Emulator`** in ANSI mode
(`ESC <` enters it, `ESC[?2l` DECANM returns).

## The stream and the endpoint

`TerminalStream` (`stream.{h,cpp}`) is the engine wearing a `ByteStream` face:

- `write()` — guest → screen: feed each byte to the emulator.
- `read()` — guest ← the emulator's reply FIFO (reports **and** the keystrokes routed to this
  line).
- `pump()` — paint one frame into the host `Display`, on the main thread.

The `Display` and `TerminalFont` are **injected once at the composition root** (`setDisplay`/
`setFont` in `src/main.cpp`), the same way the boards' `Display` and the endpoint resolver are —
a stream built deep inside `resolveEndpoint` cannot be handed them, and they are
session-lifetime host resources it only borrows.

The `terminal:` scheme is parsed in `resolveEndpoint` (`src/host/endpoint.cpp`): grammar
`terminal[?emulation=vt100&size=80x24]` (`size` also spelled `windowsize`), validated **before**
the window check, so a typo is a grammar error rather than a mysterious refusal. Then capability:
a terminal needs a window, so `TerminalStream::hasWindow()` gates it — `SdlDisplay` answers
`isWindowed()` true, `NullDisplay` false, and a headless build refuses `terminal:` cleanly at
`CONNECT` rather than opening a serial line nobody can see. SDL is optional throughout: absent
it, the endpoint is simply unavailable, never a compile break.

## The one keyboard, and where it goes

Here is the constraint that makes this bigger than the printing endpoint. Today there is exactly
**one** `SdlDisplay g_display`, shared by every video board, with **one** global key sink. The
model is "one window, one keyboard." A `terminal:` line renders in that window and must receive
its keystrokes — but so does the monitor, and so does a VDM-1 if the machine has one.

The routing (issue #244, Task 4):

- `TerminalStream::keyTarget()` names which terminal line owns the keyboard. A `terminal:`
  claims it on construction — **last one wins** — and its destructor releases it *only if it is
  still the owner*. That ordering matters: a `CONFIG LOAD` constructs the replacement line
  (which claims the target) **before** destroying the old one, so a blind clear in the
  destructor would strand the new terminal (see the config-load-replaces rule in `DESIGN.md`).
- The composition root's key sink (`src/main.cpp`) routes regular keys to
  `keyTarget()->keyAscii()`, falling back to `Console::instance().inject()` when no terminal is
  present — so the monitor and the VDM/Sol keyboard behave exactly as before.
- Arrows/Home carry no ASCII, and the `Display` normally collapses them to a single Sol-20
  *byte* before any sink sees them (`emitSpecialKey` in `src/host/display.h`) — which a terminal
  could never re-encode in its own dialect. So there is a second seam, `Display::SpecialKeySink`,
  delivering the key **symbolically**; when set it takes precedence over the byte table, and the
  composition root routes it to `keyTarget()->keySpecial()`, falling back to injecting the table
  byte into the `Console`.

**Deferred:** independent windows with per-window keyboard routing. That is the real fix for two
concurrent terminals — and for the keyboard contention that already exists between a VDM-1 and a
terminal in one machine — and it is the last unbuilt piece of the plan. Until a two-terminal
machine needs it, one host window drives whichever line claimed it last.

## The `[terminal]` transform chain

`src/host/filter.h` argues at length that the console's fold-the-bytes transforms (`strip7out`,
`upper`, `bsdel`, …) must **not** live on a serial line: a line also carries XMODEM, and a
`strip7out` there corrupts binary silently. But that file names one exception in passing — *only
the console transforms, because only the console has a human on the end of it, and every one of
these transforms is a fact about a **terminal**.* The built-in terminal is exactly that: a
terminal with a human on the end. So it carries the same chain, as the `[terminal]` config
section.

It exists because of a concrete bug (issue #244). An even-parity monitor — the MITS Programming
System II — computes parity **into bit 7** of every character it prints. A carriage return `0x0D`
goes out as `0x8D`; a line feed `0x0A` is already even and stays `0x0A`. A `terminal:` line is
raw 8-bit, so `Vt100Emulator::feed` sees `0x8D`, which is `>= 0x20`, and prints it as a glyph
instead of homing the cursor — while the `0x0A` still line-feeds. The symptom is "Enter feeds a
line but never returns." `strip7out` masks bit 7 and both bugs vanish at once.

Design points, all in `src/host/terminal/stream.{h,cpp}`:

- **Modeled on `[display]`, not on a board.** `TerminalStream::Settings` is a `static`, and
  `TerminalStream::properties()` publishes it through the one `Property` layer — so SET, SHOW,
  TOML load and tab-completion all pick it up with no new plumbing. Config binding is at
  `src/config/toml.cpp` (a `[terminal]` branch beside `[console]`/`[display]`), and the monitor
  SET/SHOW/`showTerminal` sites mirror `display`. Like `[console]`, it does **not** round-trip
  through `CONFIG SAVE`, and it is accepted on a headless build (the setting is about what the
  machine wants).
- **A live-read `static`, global.** One section for the machine, like `[console]`; `SET terminal
  strip7out=on` reaches the running window because `write()`/`keyAscii()` read the static each
  time. `BsMap` is reused from `host/filter.h` rather than re-declared.
- **Outbound folds in `write()`, inbound folds in `keyAscii()` — NOT `read()`.** This is the one
  non-obvious call. `read()` drains the emulator's reply FIFO, and that FIFO carries the cursor
  reports the guest asked for (`ESC[6n` → `ESC[r;cR`) as well as encoded keystrokes. Folding in
  `read()` would mangle a report — an H19 `ESC n` reply can contain a lowercase coordinate byte,
  which `upper` would corrupt. Keystrokes enter through `keyAscii()`, so that is where `upper`,
  `strip7in` and `bsdel` belong; reports never pass through it. `test_terminal.cpp` has a guard
  for exactly this.
- **`cr = cr | crlf`** is the terminal's one line-ending knob (the console spells the same idea
  `crlf=on/off`). Default `cr` passes the guest's CR through untouched; `crlf` appends an LF
  after each CR, applied *after* `strip7out` so it triggers on the real `0x0D`.

## The run banner and a non-stdio console

A related fix rode along. `Monitor::runFrom` announced `(no console connected)` whenever the
console was not the **stdio** one — so a machine consoled on a `terminal:` window or a `socket:`
was mislabeled as having no console at all. The banner loop already computed `anyRemoteLine` (a
live line that is not the host terminal); it now also captures that line's scheme and prints
`(console on terminal)` / `(console on socket)`. The honest `(no console connected)` is kept for
the genuinely bare backplane — the ROM-talking-to-a-disk case — where no serial line is live.

## Testing

`tests/test_terminal.cpp` drives the engine directly through a `NullDisplay`: feed a dialect's
escape sequences, assert the grid (`screen()`), and assert `read()` returns the expected reports
and key encodings. It needs no window, because the display seam is only exercised in `main.cpp`.
The `[terminal]` transforms have their own section there (including the report-not-folded guard).
Run it with `./build/altair_tests terminal`; the CONNECT grammar, the `[terminal]` SET/SHOW
surface and the run-banner label are in `./build/altair_tests cli`.
