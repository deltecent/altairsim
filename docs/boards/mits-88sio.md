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
| Serial I/O Board **Parts List**, April 1975, BAG 1 | same, p. 3 | **Authoritative** — and the *only* place in 58 pages that names the UART: `1  COM2502  101065` (MITS stock number), with its 40-pin socket in BAG 4. The schematic, the assembly steps and the theory of operation call it "IC M" and nothing else. |
| COM2502/COM2017 data sheet (SMC) | `reference/com2502.pdf` | **Authoritative** for the chip: the 40-pin pinout, the `/SWE`-gated status word (RPE, RFE, ROR, RDA, TBMT), the format pins (NDB1/NDB2, NSB, NPB, POE), `/RDAR`, `/TDS`, and **MR (pin 21)**. Modeled in `src/chips/uart1602.{h,cpp}`. |
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

### Where the chip stops and the card starts

The COM2502 lives in **`src/chips/uart1602.{h,cpp}`** (DESIGN.md §7.8 — a chip is not a card). The 88-ACR has the same part on it, under the name its own manual uses (AY-5-1013/TR1602), and it will get this one rather than a second hand-rolled half of it.

**The chip owns** the line, the receive holding register, Data Available (RDA), the transmit deadline (TBMT), the format pins (NDB1/NDB2, NSB, NPB/POE) and the master-reset pin (MR).

**The card owns everything that makes the status word look like *this* card's status word** — and that is the whole point of the split:

| | |
|---|---|
| **the inversion** | At the chip's pins the status word is **true sense**: RDA high = a character is waiting, TBMT high = the buffer is free. This card runs both through inverting buffers on the way to the bus. The 88-2SIO (a different chip) does not invert at all. **A shared "UART" helper with a `bool invert` is exactly the bug this split exists to prevent.** |
| **where the bits land** | Bit 7 and bit 0 here; the Rev 0 duplicates at bits 5 and 1; the error bits at 4/3/2. All PCB wiring. |
| **Rev 0 vs Rev 1** | Two revisions of the *card*, one unchanged chip. |
| **the interrupt enables** | A separate IC (IC B). **The COM2502 has no interrupt pin at all** — the card derives its two requests from RDA and TBMT and gates them with these flip-flops. Which is why the chip publishes its raw deadlines (`txFreeAt()`, `rxNextAt()`) and the *board* computes `nextEdge()`. |
| **the port decode** | Obviously. |

### MITS BASIC's high bit: the TERMINAL ignores it, the CARD does not strip it

> **MITS BASIC ends a message by setting bit 7 of its LAST character.** `MEMORY SIZE?` leaves
> BASIC as `...'S','I','Z','E'|0x80` — the high bit is the *string terminator*, not data. Send it
> to a modern terminal and the prompt reads **`MEMORY SIZ?`**, and so does every other prompt in
> the machine.

The obvious fix is the wrong one, and it is worth writing down why, because it is *very*
tempting: `tapes/Basic Versions.pdf` says **"7 bit serial comm"** against every one of these
builds, the ticked boxes on the scanned card are **7 data bits / 2 stop bits**, and strapping
`data_bits = 7` makes the prompt come out clean.

**Don't.** A 7-bit strap that masks the data inside the UART would fix BASIC and silently corrupt
everything else the port ever carries — **XMODEM above all, which is 8-bit binary.** The port is
not BASIC's. And it isn't what the hardware did anyway: the card sent all eight bits, and the
**Teletype ignored the eighth** — on a Model 33 that is the parity position and the printing
mechanism never decodes it. Nothing was stripped; something on the far end simply didn't look.

So the ignoring belongs to the **terminal**, and it is spelled `strip7out` — **a property of the
CONSOLE** (`SET CONSOLE STRIP7OUT=ON`, `[console]` in `machines/basic4k.toml`, DESIGN.md §7.2).
Not a strap on the chip, and **not a transform on the line either**: this card's connector is a
general-purpose serial port, the next thing through it is XMODEM, and a mask anywhere on that
path corrupts the transfer silently. The only thing in the simulator permitted to alter a byte is
the console, because the only thing with a human on the end of it is the console.
`tests/acceptance/basic4k.cmake` fails if the high bit ever reaches the terminal again.

**What `data_bits` IS, since it is not a mask.** It is **line coding** — the NDB1/NDB2 pads, real
hardware, a jumper somebody soldered. It sets how long a character occupies the wire (and so every
deadline this card sets), and on a **real serial port it is programmed into the real port**
(`Uart1602::programLine()`): strap the card 7E2 and the cable opens at 7E2, because that is what a
COM2502 jumpered that way actually does. What it never does is AND the guest's data with the word
length. A frame is not a filter — if the card is strapped for 7 bits the eighth does not *travel*;
if it is strapped for 8, all eight arrive, BASIC's terminator included, and the Teletype ignores it.

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

**Unlike the 88-2SIO, this card's UART really can be reset from the backplane.** The COM2502 has an **MR pin (21)**, and the data sheet says what it does: *"sets TSO, TEOC and TBMT high, and clears RDA, RPE, RFE, ROR."* So the transmitter comes up ready and the receiver comes up empty — which is `Uart1602::masterReset()`, and it is what both resets call. (Contrast `docs/boards/mits-2sio.md`: the MC6850 has **no** reset pin, so a bus reset there does nothing to the chip at all. Same slot, same backplane, opposite answer — because they are different chips.)

- **`Reset::PowerOn`** (POC\*, cold) — UART master reset (DAV cleared, transmitter immediately ready), **interrupt enables cleared**. The endpoint stays connected.
- **`Reset::Bus`** (RESET\*, warm) — identical. **The endpoint stays connected**: a bus reset does not unplug the terminal, and a guest that reset its UART and found the console gone would be a baffling thing to debug.

> **Two assumptions, flagged rather than buried.** (1) The manual documents the interrupt-enable flip-flops (IC B) but not their clear line; we clear them on both resets. (2) It does not say in so many words that `RESET*` is strapped to the UART's MR pin — the chip *has* the pin, and a card that came out of a reset with a stale byte in the receiver is a card nobody built, so we drive it. Every period driver enables its own interrupts and initializes its own UART after a reset, so nothing period-correct should be able to tell either way.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **The ready bits are INVERTED.** Bit 7 clear = ready to send; bit 0 clear = a byte is waiting. The 88-2SIO's are true-sense, and both cards can be in the same machine. | A driver polls forever, or transmits into a busy register and drops every other character. This is *the* trap of this board and it is why it shares no code with the 2SIO. |
| **There is no control register.** The only thing an `OUT` to the control channel can change is the two interrupt-enable flip-flops. Word format and baud are soldered pads. | A port of a 2SIO driver "configures" the UART with a control byte, silently enables an interrupt it never meant to (bits 0/1 of whatever it wrote), and hangs on an unhandled `RST 7`. |
| **TBMT is a deadline, not a flag.** The transmitter is busy for one character time. | A BIOS that *times* the line to work out its speed reaches a different conclusion. Hardwiring "always ready" is the easy lie. |
| **MITS software sets bit 7 of the last character of a message.** It is a string terminator. The card passes it; a Teletype ignores it. Use `strip7out` on the line — **not** `data_bits = 7`. | Every prompt in the machine ends in a garbage byte (`MEMORY SIZ?`) — or, if you "fix" it in the UART, it comes out clean and every 8-bit binary transfer through the port (XMODEM) is silently corrupted instead. Caught by `tests/acceptance/basic4k.cmake`. |
| **The interrupt fires with nobody touching the card.** A driver that enables the output interrupt, sends a character and `HLT`s is ordinary, and only a deadline the card set for itself can wake it. | The run loop declares the machine *finished* ~2000 T-states before its interrupt arrives. This is a real bug we shipped and fixed; see DESIGN.md §4.4.1. |
| **A `vi*` strap goes nowhere unless an 88-VI is in the machine** — correctly, exactly as a wire into an empty slot does. (The card exists as of 2026-07-13 — `docs/boards/mits-88virtc.md` — so the strap now *arrives* somewhere when one is plugged in. With no VI card present the behaviour is unchanged.) | Fabricated vectored behaviour on a machine that has no VI card in it. |
| **This card has TWO straps, and they may sit at DIFFERENT VI priorities** — one for the input device, one for the output device — so it can be pulling two VI lines at once. It is the reason `Board::assertsVi()` returns a *bitmask* and not a level (DESIGN.md §4.4.2). | A single "which level am I on" drops one of the two requests — silently, and only when both fire at once. Rare, timing-dependent, invisible: the worst possible way to lose an interrupt. |

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
