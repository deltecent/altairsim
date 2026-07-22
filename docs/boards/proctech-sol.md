# Processor Technology Sol-PC I/O — the Sol-20's integrated I/O

**Status:** implemented, `type = "sol"` — serial, keyboard and both CUTS cassette decks
are modeled; the parallel/printer port works when connected. Reuses the `vdm1` card for
video and the `fp` card for the sense
switches, so the three together make a Sol-20 (`machines/sol20.toml`).

## The real hardware

The **Sol-20** (Processor Technology, 1976) was not a bag of S-100 cards — it was an
integrated computer on one board, the **Sol-PC**: an 8080, a VDM-1-style video
section, a parallel ASCII keyboard, an RS-232 serial port, a parallel port, and a CUTS
audio-cassette interface, with the SOLOS/SOLED personality-module ROM socketed on top
and S-100 slots for expansion. All of its onboard I/O lives at fixed ports **`F8h`–`FFh`**.

The decisive fact for modeling: the readiness of the keyboard, the parallel port and
the cassette is **multiplexed into one physical status register at `FAh`**. On our
single-driver bus one board must answer `IN 0FAH`, so those three functions cannot be
separate cards — they are one composite `sol` board, and each is exposed as a named
**unit** you `CONNECT` independently.

## Sources

| Source | Path | Authority |
|---|---|---|
| *Sol Systems Manual*, Processor Technology, Jan 1978, and the **SOLOS 1.3** source | `reference/Sol-20.md` (distilled from `730000_Sol_Systems_Manual_Jan1978.pdf` + `roms/SOLOS/SOLOS13.ASM`) | **Authoritative.** The `F8h`–`FFh` port map, every status bit and its polarity (settled from the SOLOS driver code, not the `EQU` names), and the memory map. |
| deramp.com *Sol-20 Restoration* | (notes) | Corroborates the video RAM address, the serial word-length DIP, and the `OUT 0FAH` cassette motor/baud bits. |

## Register reference (the `sol` card: ports `F8h`–`FEh`)

| Port | OUT (write) | IN (read) |
|---|---|---|
| `F8` | — | **Serial status**, active HIGH: **D6** = RX data ready, **D7** = TX buffer empty |
| `F9` | Serial data out | Serial data in (clears RX-ready) |
| `FA` | Cassette control: **D7/D6** tape-1/tape-2 motor on, **D5** = 1 → 300 baud | **General status** (mixed polarity): **D0** keyboard, **D1/D2** parallel — active **LOW** (0 = ready); **D3/D4** tape errors, **D6** tape data ready, **D7** tape TX empty — active **HIGH** |
| `FB` | Tape (CUTS) data out | Tape data in |
| `FC` | — | **Keyboard data** (ready = `FA` D0, active low; the read clears the strobe) |
| `FD` | Parallel (printer) data out | Parallel data in |
| `FE` | **VDM display parameter** = low-nibble scroll row (forwarded to the `vdm1`) | — (write-only) |

`FFh` (sense switches) is the **`fp`** board; the VDM **screen RAM** (`CC00`–`CFFF`)
is the **`vdm1`** board.

## Units

| Unit | Port(s) | Kind | Default |
|---|---|---|---|
| `serial` | F8/F9 | serial line (a UART) | `null` |
| `keyboard` | FC data, FA D0 | input line | `console` (in `sol20.toml`) |
| `printer` | FD data, FA D1/D2 | output line | `null` |
| `tape1` | FB data, FA D3/4/6/7 · motor FA D7 | cassette (MOUNT) | *(empty)* |
| `tape2` | FB data, FA D3/4/6/7 · motor FA D6 | cassette (MOUNT) | *(empty)* |

```
CONNECT sol0:serial   socket:2323
CONNECT sol0:printer  file:out.txt
CONNECT sol0:keyboard console
MOUNT   sol0:tape1    "tapes/trk80.tap"
REW     sol0:tape1
```

**The decks are MOUNTed, not CONNECTed.** A cassette has a *position* — the head is
where it is, and the only way back to the start of the program is `REWIND` — which a
byte stream has nowhere to keep. `tape` is accepted as a name for `tape1`, so a machine
file written against the older single line still resolves.

`SET sol0:tape1 mode=record` is the button on the front of the recorder, exactly as on
the [88-ACR](mits-88acr.md): a tape that is playing cannot be written over, and a deck
that is recording does not hand back what used to be on it.

**One UART, two transports.** The Sol-PC has a single CUTS modem and a single data
register at `FBh`; `OUT 0FAh` D7/D6 decide which deck is turning in front of it. So a
deck with its motor off is not a slow line, it is *no* line: nothing comes off it, and
`TDR` never rises. If both motors are on, deck 1 wins — see *Limitations*.

## How it is simulated

- **Decodes** `IoRead`/`IoWrite` on the seven ports `F8`–`FE` (base `0xF8`, a property;
  fixed on real hardware). Offset-dispatched, exactly as the 88-2SIO decodes its four.
- **Serial** is a real `Uart1602` (the same COM2502-family chip the 88-SIO uses) so baud
  and the transmit deadline behave; the card builds the `F8` status word itself in the
  Sol's active-high sense (not the 88-SIO's inverted one). `baud`/`data_bits` are unit
  properties (the Sol-PC word-length DIP).
- **Keyboard** latches one character from its line into a holding register in `pump()`
  — never inside a bus cycle — and `IN 0FCH` returns it and clears the strobe. `FA` D0
  reflects the register, **active low**.
- **`OUT 0FEH`** forwards the scroll row to the VDM by locating the `vdm1` on the bus and
  calling `VdmBoard::setScroll` — so the Sol's separate display port drives the reused
  video card. If there is no VDM, the write is a harmless no-op.
- **Keyboard from two windows:** the `keyboard` unit reads the one host **Console**; the
  SDL VDM-1 window's keystrokes are injected into that same Console (via the `Display`
  key sink wired at the composition root), so a key typed in the video window or the
  terminal reaches SOLOS through one recorded input queue (DESIGN.md §7.4).
- **No interrupts.** SOLOS is polled, so the card raises none.

### Reset

- `Reset::PowerOn` / `Reset::Bus`: master-resets the serial UART, clears the keyboard
  strobe and the held cassette-control byte. Connected endpoints stay connected.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| `FAh` is **one status register for three devices**, with **mixed polarity** (kbd/parallel active low, tape active high) | SOLOS's `CMA`-then-`ANI` keyboard poll never sees a key, or sees one that is not there |
| `FAh` is **read and written to different ends** (status in, tape motor/baud out) | Turning the cassette motor on clobbers what a reader expected, or vice-versa |
| The **VDM control port is `FEh`** on the Sol, not the stand-alone VDM-1's page-tied port | SOLOS's `OUT DSTAT` scroll does nothing and the screen garbles past 16 lines |
| Serial status is **active high** here, the opposite of the 88-SIO's | A driver written against the wrong card polls the ready bit inverted and hangs |

## Limitations and deliberate departures

- **A `.WAV` mounts, and this board still does not know a tone exists.** Cassette audio is
  demodulated once, at MOUNT, by a seam *below* the card (`src/host/tapecodec.h`), so what
  reaches the CUTS UART is bytes either way. A byte tape is read as bytes; the magic —
  never the extension — decides which is which.
- **Recording back to audio works, and this card knows when a deck stopped.** When the
  transport stops the recording is re-modulated in the tape's own format and rate and
  written back over the whole file. Uniquely among our cassette cards, the Sol can *see*
  that happen: the guest drops the motor line at `OUT 0FAh`, and the 88-ACR has no motor
  line at all, so there the operator's finger is the only stop there is. `UNMOUNT`,
  `REWIND` and `mode` leaving `record` are stops here too.
- **Leader and trailer are synthesized, because time cannot survive a byte image.** Per
  deck, in seconds, defaulting to **3 and 2** — *measured off TRK80.WAV*, the one genuine
  cassette dub we hold, which carries 3.05 s of leading mark and 1.93 s of trailing. Not
  the 88-ACR's 15, which is what a manual asks an operator to do rather than what a Sol
  tape turned out to be. A multi-file tape re-recorded here comes back as one continuous
  run: the decoded byte stream holds no file boundaries, so the gaps between SOLOS files
  cannot be recovered — only put back deliberately.
- **The recorded carrier is shaped by `waveform`** — `square` (default) or `sine`, per deck.
  A real Sol-PC modem is a flip-flop into an RC filter, so `square` (a band-limited square,
  rounded the way that filter rounds it) is both the authentic shape and the louder, fuller
  sound of a genuine dub; `sine` is the smoother, quieter tone. It is audible only: either shape
  demodulates back to the same bytes, so it changes how a recording sounds and not what it holds.
  An *ideal* square is not offered — its harmonics alias at cassette rates and its half-cycle
  CUTS *0* becomes a click, both of which break the round trip (`src/host/tapemodem.cpp`).
- **Two speeds, honestly, and nothing else.** `auto` picks between `cuts1200` and `kcs300`
  by which one the tape actually carries, because both really are this hardware — the
  guest chooses between them at `OUT 0FAh` D5. An 88-ACR tape (2400/1850 Hz FSK) is
  *refused*, with its measured tones named: this demodulator is built around the CUTS tones
  (1200/600 Hz at 1200 baud, 2400/1200 Hz for the Kansas City mode), and a real Sol fed an
  ACR tape — with its 1850 Hz space — would read nothing at all.
- **Both motors on is not modeled; deck 1 wins.** On the real machine both transports
  move and both preamps drive one audio bus, which the manual does not describe and no
  program does on purpose. Picking one beats inventing what a shorted line sounds like.
- **No dropouts, and no runaway.** The transport advances at the speed the guest reads
  it — the UART pulls a byte only when it has room — where a real deck keeps rolling and
  puts data on the floor. The tape framing (`FA` D3) and overrun (`FA` D4) error bits
  therefore exist and always read 0: there is no line to have noise on.
- **The cassette runs on its own clock, not the CPU's** — a per-deck `rate` property.
  `full` (default) empties the tape as fast as the guest reads it, at any `clock_hz`; `real`
  paces playback in wall time at the guest-selected baud (`FA` D5). This is the decoupling
  the 88-ACR grew for the same reason (see its `.md`): the old emulated-`charTStates` pacing
  fused the tape's wall-clock speed to the crystal, so a Sol at its authentic 2.045 MHz was
  made to sit through a ~40 s load. The bit that reads the tape (`IN 0FBH`) and the *status*
  read (`IN 0FAH`) both advance the receiver now, exactly as the serial half already did — so
  the loader's own polling clocks the tape at full speed, and `real` holds each byte to its
  baud on the wall clock instead. D5 still **selects** 300 vs 1200; it is what `real` paces to.
- **`RESET` stops both motors** (the latch behind `FAh` clears) but does not eject a
  cassette or move a head. Pressing RESET is not the same as opening the deck.
- **There is no rewind bit, and there was none.** The guest can start and stop a motor;
  it cannot wind one back. `REWIND sol0:tape1` is the operator's finger, and it names its
  deck because with two of them there is no safe default.
- **The parallel/printer port is minimal.** `OUT 0FDH` goes to the `printer` line and
  `PXDR` (`FA` D2) reflects it; there is no strobe/ack handshake modeled.
- **The Helios disk ports `F0h`–`F7h`** (the `HELIOS`-conditional SOLOS build) are not
  part of a stand-alone Sol-20 and are not decoded.

## Verification

- `tests/test_sol.cpp` (headless, `NullDisplay`): the seven-port decode; the keyboard
  (KDR active-low, `FC` consuming the strobe, one char per host turn); the serial
  round-trip with active-high status; the idle `FA` bits; `OUT 0FEH` forwarding scroll to
  the VDM; the five named units and their `connect` properties; and **SOLOS itself
  cold-starting**, painting its `>` prompt into the VDM screen RAM, and *resting* on the
  keyboard poll (not looping).
- The cassette, in the same file: a tape plays only while its motor turns; the motor bits
  pick which of the two decks is on the line; `RECORD` writes through to the host file;
  a deck refuses `CONNECT` and says to `MOUNT` instead; the default `rate = full` empties
  the tape with the emulated clock at rest; and `rate = real` holds the next byte to a
  **wall clock** that `clock.now()` cannot hurry (`test_88acr.cpp` pins the baud math — 300
  really is four times slower than 1200 — with an injected clock, so nothing sleeps).
- **And the round trip, which is the acceptance test for the whole cassette path:** SOLOS
  `SA`ves sixteen bytes to a mounted tape, the operator rewinds, and SOLOS `GE`ts them
  back *to a different address*. SOLOS's own driver writes the leader and header and
  SOLOS's own reader finds them again — nothing in the test knows the file layout, so a
  wrong motor bit, status flag, head position or `REWIND` shows up as bytes that do not
  come home.
- End-to-end: `altairsim sol20` cold-starts SOLOS; it clears the VDM and prints `>`
  (`DUMP CC40` shows `3E` at the prompt, and with SDL3 it appears in a window). Typing in
  the window or the terminal echoes to the screen.

## References

- `reference/Sol-20.md` — the distilled I/O map.
- `src/boards/proctech-sol.{h,cpp}`, `roms/SOLOS/`, `machines/sol20.toml`,
  `src/boards/proctech-vdm1.{h,cpp}` (reused for video), `src/boards/mits-frontpanel.*`
  (the sense switches).
