# MITS 88-ACR — Audio Cassette Recorder Interface

**Status:** implemented, `type = "acr"`

## The real hardware

The manual's first sentence is the whole design:

> The 88-ACR consists of two separate PC boards mated to each other to form a single unit.
> One of these is the ACR Modem Board and the other is the **88-SIO B, Serial TTL level I/O
> Board**. The Modem board is used to key the signals into the correct audio tones and back,
> while the SIO B board is used to interface with the computer itself.

…and the manual then **reprints the entire 88-SIO documentation** as the ACR's own assembly
chapter. So this is not a card that *resembles* an 88-SIO. On the bus side it **is** one: same
COM2502 UART, same two ports, same inverted status bits, same interrupt-enable flip-flops.

**The modem contributes nothing to the register model, because it is analog.** It is an FSK
pair — **2400 Hz for a logic 1, 1850 Hz for a logic 0** — made by dividing the 2 MHz clock by
104 or 135 and then by 8, with a phase-locked loop to get the bits back. The guest cannot
observe one thing about it. That is why this board is 250 lines.

### There is no motor control. None.

Not "we didn't model it" — **the card does not have it.** There is no transport register, no
motor bit, and nothing the guest can write that reaches the recorder. **The operator pressed
PLAY or RECORD with their finger.** Everything odd about this card downstream of here follows
from that one fact.

## Sources

| Source | Path | Authority |
|---|---|---|
| *88-ACR Assembly*, "Hardwire Connections" | `reference/Altair 88-ACR Cassette Interface.pdf` p. 21 | **Authoritative** for the straps. One sentence gives the whole configuration — see below. |
| *Theory of Operation*, Modulator/Demodulator | same, pp. 59–60 | **Authoritative** for the tones (2400/1850 Hz), the ÷104/÷135 counters, the PLL and the carrier detect. |
| *Bit Definition* chart | same, p. 5 | **Authoritative** for the status word — and it is the **Rev 1** word (see below). |
| Interrupt section | same, pp. 8, 21 | **Authoritative**: INT and VI0–VI7, independent IN/OUT, `BH` for both. *"If the 88-ACR is used with MITS software, interrupts are not used."* |
| Modem/SIO B interconnect | same, p. 51 | **Authoritative** for `RS`→`SRSI`, `XS`→`STSO`, `FT`→`SO`, and the two audio pads. |
| `docs/boards/mits-88sio.md` | — | The other half of this card, and where the status word actually lives. |

**The straps are SOURCED, not chosen.** This is the sentence:

> For the 88-ACR, wire address select for **006**. Wire BAUD Rate for **300** (max.), and wire
> UART options for **8 data bits, one stop bit, no parity bit**.

The 88-SIO's defaults are a *guess* and its `.md` says so. The ACR's are not, and
`tests/test_88acr.cpp` §1 is how they stay sourced.

**And it is a Rev 1, without our having to infer it.** The ACR manual's own Bit Definition
table puts TBMT at bit 7 and DAV at bit 0 and marks bits 5 and 1 *NOT USED* — which **is** the
post-errata status word (`SioRev` in `mits-88sio.h`). The card documents itself; we did not have
to date it from the printing.

## Register reference

Two ports — and they are the 88-SIO's, because this is an 88-SIO.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| 0x06 | **Interrupt enables only.** D0 = input, D1 = output. D2–D7 ignored. | Status word |
| 0x07 | Transmit data → modem → *Tape Record Out* | Receive data ← modem ← *Tape Play In* |

**The ready bits are INVERTED: clear means ready.** Bit 7 clear = the transmitter will take a
byte; bit 0 clear = a byte is waiting. Idle status is **`0x63`**. All of this is
`docs/boards/mits-88sio.md`'s and none of it is restated here, deliberately — see *How it is
simulated*.

## How it is simulated

### `AcrBoard : public SioBoard`, and that is a claim about the hardware

The bus half of this card is written **once**, in `SioBoard`, and inherited.

This is *not* the "shared UART helper with a `bool invert`" that `mits-88sio.cpp` warns about at
length. That trap is about the **88-2SIO** — a different card, a different chip, bits the other
way up — and it stands. The trap **here** is the opposite one: two copies of **the same PCB**,
which drift the day somebody fixes a bug in one status word and not the other, leaving the
machine with two different 88-SIO Bs in it. `tests/test_88acr.cpp` §2 asserts the whole idle
status byte from *this* card, so a fork goes red.

What the ACR overrides is only what the modem and the cassette actually change:

| | |
|---|---|
| **the straps** | 006, 300 baud, 8N1 — sourced, above. |
| **the unit** | `UnitKind::Tape` (MOUNT), not `UnitKind::Serial` (CONNECT). |
| **`connect` is gone** | There is no connector. The UART's serial pins are soldered to the modem. `CONNECT` is refused *with the reason*. |
| **the transform chain is gone** | Below. |
| **`REWIND`** | The card brings its own verb (`Board::commands()`). |
| **`mode`** | The buttons on the recorder. |

### No `upper`, no `crlf`, no `bsdel` — and that is not tidiness

The filter chain (DESIGN.md §7.2) rewrites **characters** on a terminal line. A cassette carries
**binary**: a checksummed absolute image of 4K BASIC. A CRLF transform on that line does not
annoy you, it **silently corrupts the program** — and corrupts it on the way *onto* the tape as
well as off. The modem passes bits and has never heard of a newline. So the card does not offer
the knob. (The board reads the filter's *own* property names to decide what to drop, so a
transform added to the chain next year cannot arrive on a cassette because somebody forgot this
file.)

### `mode = play | record` — the buttons, and the bug they prevent

A **unit** property, not a board one, because it is not on the card. It is not on the card in
the simulator because **it is not on the card in reality**: no motor control, no transport
register, and the operator worked the buttons.

It is also the difference between a working card and one that silently corrupts every recording:

> A cassette has **one head**, so read and write share **one position** — they must; it is the
> same piece of tape. But the UART receives **eagerly**: it pulls a byte off its line the moment
> it has room, because that is how DAV and an interrupt-driven loader work. A tape that was
> readable *and* writable at once therefore had its first byte pulled away by the card **before
> the guest ever ran**, the head sat at 1, and every recording began at byte **one**.

**No load test would ever have found it.** Playback works perfectly while this is broken; only a
recording is wrong, and only in its first byte. Making PLAY and RECORD exclusive — which is what
a recorder *is* — makes the corruption **unrepresentable** rather than merely unlikely.
`tests/test_88acr.cpp` §4 and `tests/test_media.cpp` pin it. The `test_media.cpp` case used to
assert the **opposite** (`CHECK(bs.writable())` on a readable stream); that is how the bug got
in.

`MOUNT` puts the cassette in **and presses PLAY**, because that is what you are always about to
do. To record from the beginning, **REWIND first** — exactly as you would have.

### `REWIND` — the one board-injected verb in the machine

A tape is the only medium with a position you cannot seek: you can step a disk's head to any
track and its sector comes round in 5 ms whether you asked or not. So `REWIND` exists **only
while an 88-ACR is in a slot**, which is the entire reason `Board::commands()` exists
(DESIGN.md §5.4). Pull the card and it is an unknown command — correctly, because nothing in the
machine can then rewind anything.

It spells as **`REW`**: `RESET` already owns `R`, `RE` and `RES`, and **the static menu always
wins**. No card can shorten or shadow a built-in abbreviation by being plugged in.

Rewinding also **discards the byte the UART is still holding** — see *Limitations*.

### Pacing: the `baud` jumper, and no invented `speed` knob

The UART already paces the line at the strapped rate (`Uart1602::poll` — one byte, held, one
character time apart), and the `Clock` is a T-state counter with no wall-clock throttle. So
**300 baud costs essentially nothing** and there is no reason to fake it. The plan proposed a
`speed = fast | real` property; it was dropped, because `baud` is a **real jumper** that already
does exactly that job.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **The ready bits are INVERTED** (bit 0 clear = a byte is waiting). Inherited, not restated. | The loader polls forever, or reads garbage. |
| **PLAY and RECORD are exclusive.** One head, one position. | **Every recording starts at byte one.** Silently. Playback still works, so nothing tells you. |
| **There is no motor control, and no `connect`.** | You model a transport the card cannot reach, or offer a socket where the hardware has a cassette. |
| **`REWIND` drops the byte in flight.** | The guest reads a byte from *before* the rewind, then reads it **again** off the tape: a duplicated byte in the middle of a program image. |
| **A reset does not eject or rewind the tape** — but it *does* lose the byte in flight, because this card really does drive the COM2502's MR pin. | A tape that rewinds itself when the guest resets, which is a machine nobody built. |
| **The straps are sourced.** 006 / 300 / 8N1. | The period software does not load, and you go looking in the wrong place. |

## Limitations and deliberate departures

**The tape is non-lossy, and a real one is not.** The UART pulls a byte only when it has room,
so the tape advances at the speed the guest reads it. A real recorder keeps rolling and drops
data on the floor if the guest is slow. We do not model that, for the reason DESIGN.md §7.1
gives: a `ByteStream` that manufactured data loss the host transport does not have would be
inventing a failure, and inventing a failure means inventing a probability.

**`REWIND` discards the byte in the UART's receive register.** A deliberate departure, and a
small one. On the real card the receiver sits behind a **carrier detect** (Q2/Q3/Q4 gate the
PLL), so a stopped tape delivers it nothing and the question never arises. We have no carrier to
lose, so we drop the byte by hand — via the chip's own `/RDAR` strobe, exactly as a guest would
have dropped it, so nothing here reaches past the pins. Without it, the next read returns a byte
from tape that has just been wound past, and then the tape replays it: **a byte duplicated by
us** inside a program image. Pressing the other button does the same thing, for the same reason.

**Above 300 baud is a card nobody could build.** The `baud` jumper is the SIO B's and goes to
25,000; the *modem's* FSK pair cannot carry it, which is what the manual's "300 (**max.**)"
means. We allow it — it is a real jumper, and a faster tape is a genuinely useful thing to
have — but no real 88-ACR could do it, and no period recorder could follow it.

**Data Overflow, Framing Error and Parity Error are always zero.** Same reason as the 88-SIO's
and the 2SIO's: they report line noise, and there is no line. See `docs/boards/mits-88sio.md`.

**The audio is not modeled at all, and cannot be.** A `.TAP` file holds the **bytes** the UART
sent or received, not the tones. There is no sample rate, no waveform and no volume control, and
so none of the alignment procedure in the manual's checkout section (R29, the tone and volume
pots) has any meaning here. A tape recorded by this simulator will not play into a real Altair
through a speaker.

**The `P/R` pad is not Play/Record — it is a trap.** It is the **audio input**: the manual's
own cabling step says *"Label the other cable (`P/R`) with the words **Play In**."* It goes to a
*Spare* pin on the SIO B's wafer connector — i.e. straight through to the back panel — and the
demodulator's input. Anyone reasoning from the name would model a play/record control line that
does not exist, and would then "discover" the motor control this card does not have.

## Verification

`tests/test_88acr.cpp`, eight sections, `Bus::setVerify(true)` on permanently. No filesystem —
`MemoryMedia` through `setMediaResolver`.

1. **The straps are the manual's** — 006, 300, 8N1, Rev 1, interrupt pads bare. Two ports and
   not a third.
2. **The status word is the 88-SIO's, bit for bit** — the whole idle byte, `0x63`. **This is the
   test that guards the inheritance**: fork the card and it goes red.
3. **A tape plays back what was recorded on it**, in order, and then ends — which is not an
   error, it is the end of the tape.
4. 🔴 **One head, one position** — RECORD cannot be read from, the read-ahead cannot steal the
   write position, and a recording lands at byte **zero**.
5. **PLAY and RECORD are exclusive**, and the write-protect tab is a *second*, independent
   reason to refuse.
6. **`REWIND`** — declared, reachable, refuses politely with no tape in, winds the head back,
   and **does not duplicate a byte at the seam**.
7. **No connector and no transform chain** — `CONNECT` is refused with the reason, and there is
   no `connect` property to reach around it with.
8. **A reset does not eject or rewind the cassette** — but the byte in flight is lost to the MR
   pin, and the tape rolls on.

`tests/test_cli.cpp` covers it from the monitor's side: **`REW` is not a command at all** until
an 88-ACR is in a slot, `RE` is still `RESET` with one plugged in, and pulling the card takes
the verb with it.

## ✅ The acceptance test, and it PASSES

**Altair 4K BASIC v3.1 boots off a period cassette through this card, and runs a program.**
`ctest -R acceptance` — `tests/acceptance/basic4k.cmake`, the machine `basic4k`, the tape
`tapes/4K BASIC Ver 3-1.tap`, and the bootstrap `tapes/LDR4K31.ASM`. **Nothing was modified.**

```
MEMORY SIZE?
TERMINAL WIDTH?
WANT SIN? Y
 742 BYTES FREE
ALTAIR BASIC VERSION 3.1
[FOUR-K VERSION]
OK
```

**The straps in the manual are the straps that work.** The card sits at **006** — not the 000
this document's plan once feared it would have to be jumpered to. The bootstrap does `IN 06` /
`IN 07`; the manual says "wire address select for 006"; they agree, and *because* they agree the
console can be an 88-SIO at **000**, which is where 4K BASIC looks for it. There was never a port
collision — the collision was in a paper-tape loader that had been mistaken for the cassette one.

The sense switches are **`0x80`** — SA15 up — and that is the bootstrap's own header
(*"Set A15 on (cassette load), all other switches off"*), not a guess. See
`docs/boards/mits-frontpanel.md`.

**What the test actually proved about this card**, beyond "it works": the inverted status bit and
the leader skip are exercised for 4,439 real bytes rather than the handful a unit test feeds
them; the image arrives byte-exact (a tape that dropped or duplicated **one** byte does not print
its own banner); and `REW` genuinely rewinds — the same tape loads a second time in the same
session.

**It found three bugs that no unit test could have.** All three were in the machine around the
card, not in the card, which is the entire reason an acceptance test exists:

- **`CONNECT` silently reset the transform chain.** Both chips built a *fresh* `FilterStream` for
  every endpoint, so `upper`, `strip7*`, `crlf`, `echo`, `bell` and `bsdel` all snapped back to
  their defaults the moment anything was plugged in — and a machine file that set a transform
  before `connect` (the loader applies keys in file order) had it thrown away before the machine
  ever started. `FilterStream::reconnect()` now swaps the endpoint and keeps the chain, which is
  what DESIGN.md §7.2 claimed all along. **This is the one that mattered**: the transforms belong
  to the line, not to what is on the far end of it.
- **A scripted run killed the machine three slices into the load.** The monitor took "the guest
  has stopped printing" for "the guest has finished" — and a cassette bootstrap prints *nothing*
  for the length of a tape.
- **A quoted path could not be opened.** `MOUNT`/`LOAD`/`SAVE` never stripped the quote, so no
  file with a space in its name would open — which is every artifact in `tapes/`.

It also surfaced MITS BASIC's **bit-7 string terminator**, and the *wrong* fix for it is so
tempting that it has its own section in `docs/boards/mits-88sio.md`. Short version: the terminal
ignores the high bit (`strip7out`); the card does not strip it (`data_bits = 7` would corrupt
XMODEM).

## References

- `reference/Altair 88-ACR Cassette Interface.pdf` — MITS, 2nd printing, February 1977. 97 pages:
  the SIO B manual entire, the modem assembly, the theory of operation, and the alignment
  procedure.
- `docs/boards/mits-88sio.md` — **the other half of this card.** The status word lives there.
