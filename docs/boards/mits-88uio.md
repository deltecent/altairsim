# MITS 88-UIO Universal Input/Output

**Status:** done

## The real hardware

The 88-UIO is a single S-100 board that carries **two independent peripherals**: a serial
port and an audio-cassette (ACR) interface, separately addressed, sharing only the
address-decode PROM and the on-board voltage regulators. It is, in effect, one channel of an
88-2SIO and an 88-ACR mated onto one card — which is exactly how a period operator used it:
the serial port drove the console terminal (SW-2 OFF puts it at the standard 0x10, where
88-2SIO Port A lives), and the cassette section loaded and saved programs (SW-3 OFF puts it
at the standard 0x06, where an 88-ACR lives).

The chips:

- **Serial section — a Motorola 6850 ACIA** (schematic IC K). Standard 88-2SIO register model:
  status/control at the even base, data at base+1, true-sense status flags. A 7-position
  jumper (SK1) sets the baud rate (110–19200); a 20-pin socket (SK2) selects TTY 20 mA
  current loop or RS-232 line drivers.
- **Cassette section — an AY-5-1013A UART** (schematic IC L) — a 1602/COM2502-family part,
  the same one the 88-ACR and 88-SIO use, with the **inverted** ready flags — driving an FSK
  cassette modem. Strapped for 300 baud, 8N1.

What the 88-UIO adds over a plain 88-ACR, and what this board doc is really about, is two
switch-selected features the ACR lacks:

- **Motor control.** A relay wired to the recorder's "Remote" jack, driven by an `OUT` to
  the cassette status port: D7 low = motor ON (`OUT 6,127`), D6 low = motor OFF
  (`OUT 6,191`). Contacts are normally closed at power-up.
- **A modulation switch (SW-1).** OFF selects the **MITS** standard (2400 Hz mark / 1850 Hz
  space); ON selects the **Kansas City** standard (2400 Hz mark / 1200 Hz space). The card
  records and reads exactly the one the switch is set to.

Period configuration is the 4-position DIP: SW-1 modulation, SW-2 serial address (020/021 or
030/031 octal), SW-3 cassette address (006/007 or 016/017 octal), SW-4 unused.

## Sources

| Source | Path | Authority |
|---|---|---|
| MITS "Using the 88-UIO Board" + Attaché schematics (drawing 250272A) | `reference/88-UIO.md`, `docs/sources.md` | The switch functions, both port pairs, the baud jumper, the motor-control OUT semantics, and the SW-1 modulation standards (KCS reprinted from BYTE Feb 1976). |
| MITS 88-ACR Cassette Interface manual | `reference/Altair 88-ACR Cassette Interface.md`, `docs/boards/mits-88acr.md` | The cassette register model, inverted status word, 8N1 framing, and FSK tape format — shared verbatim, because the UIO's cassette section is an 88-ACR. |
| SMC COM2502 / AY-5-1013 data sheet | `reference/com2502.md`, `src/chips/uart1602.h` | The cassette UART (the manual names the same part an AY-5-1013). |
| Motorola MC6850 ACIA data sheet | `reference/6850.md`, `src/chips/mc6850.h` | The serial section's ACIA. |

## Register reference

Two disjoint port pairs, each split on A0. Defaults shown (SW-1 mits, SW-2/SW-3 off).

### Serial section — 6850, base 0x10 (`serial_port`)

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `0x10` | 6850 control register | 6850 status register (**true sense**: RDRF=bit0, TDRE=bit1 SET = ready) |
| `0x11` | transmit data | receive data |

### Cassette section — AY-5-1013A, base 0x06 (`port`)

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `0x06` | **motor relay** (D7 low = on, D6 low = off) **and** the two interrupt-enable bits (D0/D1) | status word (**inverted**: bit0 low = RX ready, bit7 low = TX empty) |
| `0x07` | transmit data (to tape) | receive data (from tape) |

The cassette status word is the Rev 1 88-SIO's, bit for bit (idle, no tape = `0x63`).

## How it is simulated

`UioBoard` **derives from `AcrBoard`** (`src/boards/mits-88uio.{h,cpp}`) — the cassette half
is an 88-ACR, verbatim, so all of the tape machinery is inherited: MOUNT/UNMOUNT, the
WIND/REWIND/EXTRACT verbs, the live tape counter, the WAV codec, and the SNAPSHOT of the head
position. The serial half is an **embedded `Sio2Port serial_`** — the same reusable 6850
section the MITS Turnkey Module carries (`src/chips/sio2port.h`) — forwarded exactly as the
Turnkey forwards it.

- **Bus cycles:** the two port ranges are disjoint. `decodes()`/`read()`/`write()` ask the
  serial section first, then delegate to the inherited `SioBoard` for the cassette pair.
- **Motor control:** `write()` intercepts an OUT to the cassette control port, latches
  `motorOn_` from D6/D7, and then **still** hands the write to `SioBoard::write` so the
  interrupt-enable bits (D0/D1) it also carries are set as usual. It reads only D6/D7 and
  swallows nothing — the UART and the tape underneath are never disturbed.
- **Modulation:** `modem()` overrides `AcrBoard`'s (which is why that method was made
  virtual) to return a **single** `TapeFormat` chosen by SW-1 — `fsk300_1850` for `mits`,
  `kcs300` for `kansas`. Both constants already ship (`src/host/tapemodem.h`); the 88-UIO is
  the "future card that adds a TapeFormat and no code at all" that header anticipates.
- **Units:** the inherited `tape` (MOUNT) plus the serial section's `serial` (CONNECT).
  `connect()` routes the serial unit to `serial_`; the tape unit's CONNECT is refused with
  the ACR's reason (the recorder is soldered to the modem).
- **Interrupts:** `assertsInt()`/`assertsVi()` OR the two sections — either can be strapped
  and asking at once.
- **Clock / host:** `clockAttached()` forwards the clock to `serial_`; `pump()` and
  `configChanged()` service both halves.
- **Properties:** the cassette's (from `AcrBoard`), plus `serial_port` (SW-2), `standard`
  (SW-1), and a read-only `motor`. Both base setters reject a value that would make the two
  sections overlap — `decodes()` asks the serial half first, so an intra-board overlap
  would silently shadow the cassette, and the bus's cross-*board* conflict check cannot see
  a clash inside one card.

### Reset

- `Reset::PowerOn` (POC*, cold): both UARTs are put in a known state (the cassette UART's MR
  pin; the 6850 has none but its section powers on), the interrupt-enable flip-flops clear,
  and the motor relay comes up **closed** (`motorOn_ = true`).
- `Reset::Bus` (RESET*, warm): the same — memory and the mounted cassette survive, the tape
  is not rewound (RESET* cannot reach into the recorder and press a button), and the byte in
  flight in the cassette UART is lost to its MR pin, exactly as on the 88-ACR.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Cassette status is **inverted** (bit0 low = RX ready, bit7 low = TX empty); serial status is **true sense** | A loader polling the wrong polarity hangs, or reads garbage forever. The two halves must keep their own conventions. |
| The cassette control port at 0x06 is **shared**: motor relay (D6/D7) **and** interrupt enables (D0/D1) | Swallowing the OUT to latch the motor, and not passing it through, would silently drop the interrupt-enable write. Reading a data byte off 0x06 (it is the status/control port, not data) corrupts the tape stream. |
| The motor relay is **normally closed at power-up** | A guest that assumes the motor is off until told would mis-cue the tape. |
| SW-1 selects **one** modulation; the other is **refused**, not decoded | Decoding a Kansas City tape on the `mits` setting (or vice-versa) hands the guest bytes no real UIO could recover — the "never invent hardware" rule (`DESIGN.md`). |
| The serial half is **one** channel (0x10/0x11), not a full 2SIO | Decoding 0x12/0x13 would steal ports from the next card. |

## Limitations and deliberate departures

- **The motor is cosmetic at the default `rate=full`.** Tape motion is byte-driven in this
  simulator (`host/tape.h`): the cassette advances as the guest reads/writes, not because a
  motor is running. The motor register is faithfully latched, exposed (`SHOW`, SNAPSHOT), and
  never corrupts the data path — but at `rate=full` a stopped motor does not stop the tape,
  because nothing is pacing it to a motor. This matches how the 88-ACR (which has no motor at
  all) already behaves, and no MITS cassette loader depends on the difference. (`rate=real`
  paces playback in wall time; gating that on the motor was considered and deferred as
  unobservable to period software.)
- **Motor OUT values that set D0/D1 also toggle the interrupt enables.** `OUT 6,127` and
  `OUT 6,191` both carry D0=D1=1, so they enable both cassette interrupts as a side effect.
  This is the real hardware's behavior (the bits share the register), not a bug — MITS
  cassette software does not use interrupts, so nothing period-correct notices.
- **Tape-codec log lines are prefixed `acr:`.** They come from the inherited `AcrBoard`
  implementation. A cosmetic artifact of the shared code, not a second card.

## Verification

- `tests/test_88uio.cpp` drives both halves on a real machine: the two disjoint port ranges
  and the default addresses; the cassette's inverted status word and byte readback (guarding
  that the ACR inheritance is real); motor latching with the data path proven untouched; a
  live, independent 6850 on the serial side (a byte fed to it arrives on 0x11 while the
  cassette status does not move); the SW-1 modulation switch accepting and refusing MITS vs
  Kansas City WAV tapes in both directions; and a SNAPSHOT round-trip of the motor relay.
- `tests/acceptance/uio-basic8k.cmake` boots 8K BASIC over the UIO's serial port end to end
  — the strongest proof that the serial half is a drop-in 2SIO Port A.
- The cassette machinery itself is exhaustively pinned by `tests/test_88acr.cpp`, which the
  UIO inherits unchanged.

## References

- `reference/88-UIO.md` — the hardware reference distilled from the deramp scan.
- `docs/boards/mits-88acr.md` — the cassette section, which this card is.
- `src/chips/sio2port.h` — the reusable 6850 serial section the serial half embeds.
- `src/host/tapemodem.h` — the `fsk300_1850` and `kcs300` formats SW-1 chooses between.
