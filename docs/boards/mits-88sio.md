# MITS 88-SIO — Serial I/O Board

**Status:** milestone 1b — implemented, `type = "sio"`

## The real hardware

MITS's *first* serial card, 1975, and the one the Altair shipped with before the 88-2SIO
existed. **One** serial port, built around a **COM2502** UART (a 40-pin AY-5-1013-class part)
with discrete 74-series glue: 8T97 tri-state bus buffers, 74L193 counters for the baud clock,
a 9601 one-shot.

It came in three interface flavours — **88-SIO A** (RS-232), **88-SIO B** (TTL) and
**88-SIO C** (TTY current loop) — which differ **only** in the line drivers hanging off the
UART's serial pins. Nothing on the bus side changes, so *this board models all three*: the
transport is a `ByteStream`, and what voltage it would have been at is not a fact the guest
can observe.

Everything about the card's configuration is **soldered**. The address, the baud rate, the
word format and the interrupt straps are all pads you run a wire between; there is no DIP
switch and, crucially, **no software-writable control register in the 6850 sense**. See
*Quirks*.

### Revision 0 and Revision 1

The manual ships with an errata sheet headed:

> MODIFICATION FOR INTERNAL HARDWARE INTERRUPT (for devices with no external "handshake"
> capability). **THIS MODIFICATION APPLIES TO REVISION 0 BOARDS ONLY.**

**Rev 1 is that modification, done at the factory** (Patrick, 2026-07-12). It cuts the UART's
TBMT and DAV lines away from the status buffers at bits 1 and 5 and reroutes them to bits 7
and 0, where the *external handshake* flip-flops used to be. That is the entire difference,
and it is the whole reason the `rev` property exists.

`rev = 1` is the default.

## Sources

| Source | Path | Authority |
|---|---|---|
| Serial I/O Board Documentation, Theory of Operation, "Bit Definition" | `reference/88-SIO Rev 0 & 1.pdf` pp. 9–13 | **Authoritative.** The status word, the interrupt-enable bits, the address decode, the UART pads. |
| Serial I/O Board Errata (internal hardware interrupt) | same, p. 5 and p. 13 | **Authoritative** for the Rev 1 status word. |
| Serial I/O Board Assembly Procedure, "Vectored Interrupt" (doc p. 13) | same, p. 45 | **Authoritative** for the `OUT`/`IN`/`BH` straps and VI0–VI7. |
| Serial I/O Board Assembly Procedure, "Hardwire Connections" (doc p. 12) | same, p. 43 | **Authoritative** for NDB1/NDB2, NSB, NPB/POE. |
| Serial I/O Board Assembly Procedure, "I/O Device Interconnections" (doc p. 19) | same, p. 51 | **Authoritative** for the handshake lines (`SRIN`/`SROT`/`SBIN`/`SBOT`). |
| I/O Address Selection Chart | same, pp. 53, 55, 57 | **Partial.** Chart pages 2 and 4 were not scanned; addresses 062–142 and 226–306 are missing from the file. The encoding is mechanical (pad `In` → `An` for a 1 bit, `Ān` for a 0) and the surviving pages agree with it, so nothing is lost. |
| I/O Baud Rate Selection Chart | same, p. 15 (errata) | **Authoritative**, and *only* on the errata sheet — the chart in the manual body is the one the errata says to discard. 110, 150, 300, 600, 1200, 2400, 4800, 9600, 19200. |
| `SIOECHO.ASM`, `SIOINT.ASM` | [deramp.com](https://deramp.com/downloads/altair/software/utilities/other/) | **Period software, written by someone who owns the hardware.** Independently corroborates the ports (00/01), the inverted bit 0, D0 = receive interrupt enable, and RST 7. Both run in `tests/test_88sio.cpp`, unmodified. |

**The manual states no default address and no factory word format.** The ticked boxes in our
scan (2 stop bits, 7 data bits) are a *previous owner's build*, not a MITS default. Our
default of **`port = 0x00`** is not from the manual either — but both period test programs
above are written against ports 00/01 without comment, which is about as close to a
convention as this card has.

## Register reference

Two ports. Control/status at an **even** base, data at base+1.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| BASE+0 | **Interrupt enables only.** D0 = input interrupt enable, D1 = output interrupt enable. D2–D7 ignored. | Status word (below) |
| BASE+1 | Transmit data | Receive data (clears DAV) |

### The status word — **READ THIS BEFORE YOU WRITE A DRIVER**

The two *ready* bits are **INVERTED**: **clear means ready.**

| bit | Rev 0 (as shipped) | Rev 1 (default) | sense |
|---|---|---|---|
| 7 | Output device ready | Output device ready — **TBMT** | **LOW = ready** |
| 6 | *not used* | *not used* | — |
| 5 | Data Available | *not used* | HIGH = true |
| 4 | Data Overflow | Data Overflow | HIGH = true |
| 3 | Framing Error | Framing Error | HIGH = true |
| 2 | Parity Error | Parity Error | HIGH = true |
| 1 | X-mitter Buffer Empty | *not used* | HIGH = true |
| 0 | Input device ready | Input device ready — **DAV** | **LOW = ready** |

Concretely, an idle card with nothing typed reads **`0x63` on a Rev 1** and **`0x43` on a Rev
0** — a different byte, which is the point.

### Interrupt enables

| D1 | D0 | output interrupt | input interrupt |
|---|---|---|---|
| 0 | 0 | disabled | disabled |
| 0 | 1 | disabled | **enabled** |
| 1 | 0 | **enabled** | disabled |
| 1 | 1 | **enabled** | **enabled** |

## How it is simulated

- **Decodes** `IoRead`/`IoWrite` at `BASE` and `BASE+1`. No memory. `port` **must be even** —
  the decode ignores A0 and uses it to select the channel, so an odd base is not a card you
  could build, and setting one is refused with a sentence saying why.
- **Interrupts:** two straps, `in_int` and `out_int`, each `none | int | vi0..vi7`. It pulls
  **pin 73** (combinational and pure, DESIGN.md §4.4.1) when *the UART is asking* **and**
  *software enabled that interrupt* **and** *that pad is soldered to pin 73*. With no 88-VI
  card in the machine, nobody claims the `IntAck` cycle, the bus floats to `0xFF`, and the
  8080 executes `RST 7` — which is the manual's own "processor will immediately jump to
  location 70 (octal)".
- **Schedules on the `Clock`:** one deadline, for whichever edge comes first — the transmit
  buffer draining (`TBMT` rising) or the next character finishing its arrival. Usually there
  is **no deadline at all**: a quiet line with an idle transmitter has nothing to do next.
- **`pump()`** is how a host keystroke gets in; nothing in emulated time could have predicted
  it, so no timer could have been set for it (DESIGN.md §7.1, §7.5).
- **`ByteStream`**, through a `FilterStream`, so every transform (`upper`, `crlf`, `bsdel`, …)
  works on this card's line exactly as it does on any other.
- Does **not** master the bus.

### Why the two straps, and not a `source = rx | tx | both`

The assembly manual is explicit:

> You may connect the "OUT" (output device) pad to some priority level, and the "IN" (input
> device) pad to some priority level; or you may connect the "BH" (both devices) pad to a
> desired priority level for both devices.

So the input and output devices can sit at **different VI priorities**, and `BH` is not a
third mode — it is *one wire instead of two* for the case where both go to the same place.
A `source` enum would have been fewer properties and would have made the manual's own example
unrepresentable.

### Reset

- **`Reset::PowerOn`** (POC*, cold) — UART cleared, DAV cleared, transmitter immediately
  ready, **interrupt enables cleared**. The endpoint stays connected.
- **`Reset::Bus`** (RESET*, warm) — identical. **The endpoint stays connected**: a warm reset
  does not unplug the terminal, and a guest that reset its UART and found the console gone
  would be a baffling thing to debug.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **The ready bits are INVERTED.** Bit 7 clear = ready to send; bit 0 clear = a byte is waiting. The 88-2SIO's are true-sense, and both cards can be in the same machine. | A driver polls forever, or transmits into a busy register and drops every other character. This is *the* trap of this board and it is why it shares no code with the 2SIO. |
| **There is no control register.** The only thing an `OUT` to the control channel can change is the two interrupt-enable flip-flops. Word format and baud are soldered pads. | A port of a 2SIO driver "configures" the UART with a control byte, silently enables an interrupt it never meant to (bits 0/1 of whatever it wrote), and hangs on an unhandled `RST 7`. |
| **TBMT is a deadline, not a flag.** The transmitter is busy for one character time. | A BIOS that *times* the line to work out its speed reaches a different conclusion. Hardwiring "always ready" is the easy lie. |
| **The interrupt fires with nobody touching the card.** A driver that enables the output interrupt, sends a character and `HLT`s is ordinary, and only a deadline the card set for itself can wake it. | The run loop declares the machine *finished* ~2000 T-states before its interrupt arrives. This is a real bug we shipped and fixed; see DESIGN.md §4.4.1. |
| **A `vi*` strap goes nowhere** until an 88-VI card exists — correctly, exactly as a wire into an empty slot does. | Fabricated vectored behaviour on a machine that has no VI card in it. |

## Limitations and deliberate departures

**Undriven status bits read as `1`** (Patrick, 2026-07-12 — asked, because the manual says
"not used" and stops). It is also what the hardware says: these bits come from an 8T97
tri-state buffer whose input pad is unconnected, and an unconnected TTL input floats high.
Asserted as a **whole byte** in `tests/test_88sio.cpp` (`0x63` idle on a Rev 1, `0x43` on a
Rev 0), not a mask, so the convention cannot be changed by accident. The visible consequence:
a Rev 0 driver run against a Rev 1 board sees bit 5 stuck high — "data always available" — and
reads garbage forever. That is the right outcome for installing the wrong card; had the sense
been the other way it would *hang* instead. Both are broken, but **differently**, which is why
it was worth asking rather than picking one.

**The external handshake lines are not modeled.** `SRIN`/`SROT` (ready pulses in from the
device) and `SBIN`/`SBOT` (busy back out to it) exist on the real Rev 0's 10-pin connector.
A `ByteStream` has no such pins, and it does not need them: it is a buffered, flow-controlled
source that is *always* ready, so the ready latches would have exactly the same edges as the
UART's own TBMT and DAV. Software cannot tell the difference (Patrick, 2026-07-12), which is
why Rev 0's bits 7 and 0 behave here identically to Rev 1's. **The revision difference we do
model is real and complete: Rev 0 *also* drives DAV at bit 5 and TBMT at bit 1.**

**Data Overflow (bit 4) is always zero.** Same reason as the 2SIO's `OVRN`: a `ByteStream` is
**not a serial line**. It is a pipe, a socket or an OS keyboard queue, and it holds the byte
until we take it. Synthesizing an overrun from it does not reproduce a hardware behaviour — it
*manufactures data loss the host transport does not have*. The first 2SIO did exactly that and
lost typed characters immediately. If a host serial-port endpoint ever lands, an overrun there
is a genuine hardware event and the stream can report it, from the place that actually knows.

**Framing Error (bit 3) and Parity Error (bit 2) are always zero.** They report line noise and
there is no line. Synthesizing them means inventing a noise model, which means inventing a
probability — the exact kind of number DESIGN.md §0.1 says to ask about rather than make up.

**RESET\* clears the interrupt enables — an assumption.** The manual documents the
interrupt-enable flip-flops (IC B) but not their clear line; POC\* clearly clears them. Every
driver enables its own interrupts after a reset, so nothing period-correct should be able to
tell.

**The defaults are a choice, not a source.** `port = 0x00`, `baud = 9600`, `8N1`. The manual
marks no standard address and no factory word format (though the period test programs assume
00/01). Set them to whatever your software expects.

## Verification

`tests/test_88sio.cpp`, ten sections, with `Bus::setVerify(true)` on permanently — every
decode and every interrupt re-derived from the board and compared against the backplane's
cached wire on every instruction.

1. **The card** — one unit, two ports and not a third, no memory, and an **odd base is refused
   with a reason**.
2. **Status is inverted** — the trap, asserted directly.
3. **Rev 1 is the default**, and the whole idle status byte is pinned at `0x63`.
4. **Rev 0 also exposes DAV at bit 5 and TBMT at bit 1**, and its idle byte is a *different*
   `0x43`.
5. **TBMT is a deadline** — bit 7 sets the instant a character goes out and clears one
   character time later, with nobody touching the card.
6. **The control channel holds two bits** — a waiting character does *not* pull pin 73 until
   software enables the interrupt, and D1 enables a *different* wire from D0.
7. **`IN` and `OUT` are two straps** — the receiver on `vi3` and the transmitter on `int`, and
   only the transmitter reaches pin 73.
8. **Nobody is asking** — a guest enables the output interrupt, sends a character, `EI; HLT`s,
   and is woken by a deadline the card set for itself. This is the test the old
   poll-every-instruction model could not pass.
9. **An interrupt-driven echo, end to end, with no VI board** — the acceptance test. Characters
   come back via `RST 7`, and the vector is not *chosen* by anything: it is what an empty
   backplane reads as.
10. **`SIOECHO.ASM`** — a *period* polled-echo program, run unmodified. Its author's own
    comment is `;nothing yet (negative logic)`, and it loops on carry after an `RRC`. On a
    true-sense card it would spin forever with a character waiting, so had we copied the
    2SIO's polarity, this is the test that would have caught it.
11. **`SIOINT.ASM`** — the *period* interrupt-driven echo, run unmodified. Its handler sits at
    `038h` with no explanation, because on a real Altair with no 88-VI card that is simply
    where the interrupt goes. Nothing in our bus chooses it either. It also saves and restores
    the accumulator to verify the CPU's interrupt path does not corrupt it.
12. **A reset does not unplug the terminal**, but it does clear the interrupt enables.

Neither of the last two is a test we wrote, and neither may be adjusted if it fails
(DESIGN.md §0.1). They are the closest thing this board has to a jury.

## References

- `reference/88-SIO Rev 0 & 1.pdf` — MITS, *Serial I/O Board Documentation*, © 1975; and the
  *Serial I/O Board Assembly Procedure*, April 1975. 58 pages: theory of operation, errata,
  four schematic sheets, assembly, and a partial address chart.
- `docs/boards/mits-2sio.md` — the *other* serial card, and the one whose status bits are the
  right way up.
