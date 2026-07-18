# Processor Technology Sol-PC I/O — the Sol-20's integrated I/O

**Status:** implemented, `type = "sol"` — serial and keyboard fully modeled; the
parallel/printer port works when connected; the CUTS cassette is deferred (see
*Limitations*). Reuses the `vdm1` card for video and the `fp` card for the sense
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
| `tape` | FB data, FA D3/4/6/7 | cassette (deferred) | `null` |

```
CONNECT sol0:serial   socket:2323
CONNECT sol0:printer  file:out.txt
CONNECT sol0:keyboard console
```

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

- **The CUTS cassette is deferred.** Its ports decode and read idle (tape TX empty, no
  tape data), so SOLOS `SAVE` writes to a sink and `GET` simply finds nothing rather than
  hanging. Kansas-City audio framing and the motor timing are a later phase; the `tape`
  unit is present so it can be filled in without a config change.
- **The parallel/printer port is minimal.** `OUT 0FDH` goes to the `printer` line and
  `PXDR` (`FA` D2) reflects it; there is no strobe/ack handshake modeled.
- **The Helios disk ports `F0h`–`F7h`** (the `HELIOS`-conditional SOLOS build) are not
  part of a stand-alone Sol-20 and are not decoded.

## Verification

- `tests/test_sol.cpp` (headless, `NullDisplay`): the seven-port decode; the keyboard
  (KDR active-low, `FC` consuming the strobe, one char per host turn); the serial
  round-trip with active-high status; the idle `FA` bits; `OUT 0FEH` forwarding scroll to
  the VDM; the four named units and their `connect` properties; and **SOLOS itself
  cold-starting**, painting its `>` prompt into the VDM screen RAM, and *resting* on the
  keyboard poll (not looping).
- End-to-end: `altairsim sol20` cold-starts SOLOS; it clears the VDM and prints `>`
  (`DUMP CC40` shows `3E` at the prompt, and with SDL3 it appears in a window). Typing in
  the window or the terminal echoes to the screen.

## References

- `reference/Sol-20.md` — the distilled I/O map.
- `src/boards/proctech-sol.{h,cpp}`, `roms/SOLOS/`, `machines/sol20.toml`,
  `src/boards/proctech-vdm1.{h,cpp}` (reused for video), `src/boards/mits-frontpanel.*`
  (the sense switches).
