# PMMI MM-103 — Modem and Communications Adapter

**Status:** **deferred** out of the implementation roadmap — the 88-2SIO already exercises every interface this board would (console, host serial, sockets, interrupts), and the PMMI adds a modem's *semantics*, not new simulator plumbing.

**But it is kept fully specified here and hand-traced against the board API**, because it is the hardest board in the catalog to express and therefore the **best available test of whether that API is right**. If the design can express the PMMI on paper with no special cases, it can express almost anything you build later.

Also: this register map was expensive to recover. Do not lose it.

## The real hardware

A **Bell 103** originate/answer modem on an S-100 card, with an on-board pulse dialer, dial-tone detector, ring detector, programmable rate generator, and maskable interrupts. Modem chip is a **Motorola MC6860L**. The UART is **never named in the manual** — its register set is the classic AY-5-1013 / TR1602 / 6402 shape, but that is inference, not documentation.

**Bell 103 frequencies:**

| Mode | Transmit | Receive |
|---|---|---|
| Answer | space 2025 Hz, mark 2225 Hz | space 1070 Hz, mark 1270 Hz |
| Originate | space 1070 Hz, mark 1270 Hz | space 2025 Hz, mark 2225 Hz |

## Sources

**The hardware reference is [`reference/PMMI MM-103 Modem and Comm Adapter.md`](../../reference/PMMI%20MM-103%20Modem%20and%20Comm%20Adapter.md).** It carries the register map, the rate generator, the dialing and answering procedures, the 6860 timing and the full errata list, verified bit by bit against the manual on 2026-07-27. This file is the *simulation* document; where the two disagree about the hardware, the reference wins.

| Source | Where |
|---|---|
| **Manual** — PMMI *MM-103 Modem and Communications Adapter Owner's Manual*, © 1982, 32 pp. Good OCR text layer for §7 (register spec), §9 (interrupts) and §10 (the MC6860 reprint); §8's listings and the figures OCR poorly and must be read as page images. | `reference/pmmi-mm-103-modem-and-comm-adapter.pdf` (untracked, 20 MB — see `docs/sources.md`); fetched from s100computers.com |
| **§8's ten worked programs** — period software written for real silicon, the only cross-check on the prose since **no schematic was ever shipped** | §8 of the same manual |

**No other simulator's source is used or needed** (`DESIGN.md` §0.1) — including where a prior emulation of this card exists and is Patrick's own, since §0.1's reasoning does not turn on authorship. Everything below is from the manual.

`MXO-PM22.ASM`, the MEX overlay the earlier draft of this file cited for corroborating equates, **is not present on this machine** and could not be found re-hosted. If a copy surfaces it is worth having; nothing here depends on it.

## Base address

A **6-position DIP** compared against A2–A7 (switch OFF = OPEN = 1). The board sits on any 4-port boundary. **Manual default is 0xC0** — §8.0 gives the switch setting explicitly as `OFF OFF ON ON ON ON` → `1100 00XX`. (An earlier draft recorded that MEX defaults to 0x80 by its own choice; the overlay source is not on this machine, so that is **unverified** and is not a hardware fact either way.)

The manual notes North Star DOS uses 0xC0 for memory-protect, so PMMI shipped its North Star software readdressed to **0xE0** — a period argument for the contention detector.

## Register reference

**Four addresses, but five output destinations and three input sources — read and write mean entirely different things at the same port.** This is why a naive port table is wrong, and why `IoIn` / `IoOut` are distinct cycle types in the bus model.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| BA+0 | UART format + modem control (SH/RI) + interrupt enable | **UART status** + aux inputs |
| BA+1 | Transmit data | Receive data |
| BA+2 | **Rate generator** — *and* the staging register for the interrupt mask | **Modem status** |
| BA+3 | 6860 modem control + Aux Out 3 | **No data.** The read is a *strobe* that copies the rate register into the interrupt-mask register. **The manual never states what appears on the bus** — nothing drives it, so a real machine reads a floating `0xFF`. That value is an inference, not documentation. |

> **All three control registers are write-only** — the port at the same address reads something else entirely. The board must **shadow every value written**.

### OUT BA+0 — UART format / modem control / interrupt enable

| Bit | Name | Meaning |
|---|---|---|
| 0 | **SH** Switch Hook | 1 = originate mode; drives the line switch-hook relay (1 = off-hook / "make"). Pulsed for dial pulsing. **Inverted vs. the 6860 datasheet.** |
| 1 | **RI** Ring Indicator | 1 = put the 6860 in Answer mode → drives Answer Phone low → phone answered. **Inverted vs. the 6860 datasheet.** *Not* the ring signal from the line. |
| 2–3 | NB1 / NB2 | Data bits: 00→5, 01→6, 10→7, 11→8 |
| 4 | NP | 1 = no parity |
| 5 | EPS | 0 = odd, 1 = even |
| 6 | TSB | 0 = 1 stop bit, 1 = 2 (1.5 if 5-bit char) |
| 7 | **Enable Interrupt** | 1 = allow the board to drive the S-100 interrupt request |

Every write rewrites **all** of it at once, so software must OR in the full desired state.

### OUT BA+2 — rate generator (8-bit divisor). **Dual-purpose.** See below.

### OUT BA+2 (as interrupt mask) — latched into the mask register by a subsequent `IN BA+3`

| Bit | Meaning |
|---|---|
| 0 | TBMT interrupt enable |
| 1 | DAV interrupt enable |
| 2 | Ring **OR** Dial Tone interrupt enable (wired-OR; the manual says they are mutually exclusive) |
| 3 | Timer Pulses interrupt enable |
| 4 | AUX OUT 1 (open-collector pin, not an interrupt) |
| 5 | AUX OUT 2 (open-collector pin) |
| 6–7 | unused |

### OUT BA+3 — MC6860 modem control

| Bit | Name | Meaning |
|---|---|---|
| 0 | ESS/ELS | 1 = **Short** space disconnect (0.3 s); 0 = **Long** (1.5 s) |
| 1 | **ESD** | **ACTIVE LOW.** Enable Space Disconnect. Barred in Figure 7.0; §10.3.1 says "provided the Enable Space Disconnect is low" |
| 2 | **Tx Brk** | **ACTIVE LOW.** Held low > 34 ms → 233 ms continuous space. Must be high ≥ 34 ms before, and only after CTS. |
| 3 | Brk Rel | Break Release — hold **low** ≥ 20 µs to clear the Rx Break status bit |
| 4 | **ST** Self Test | **ACTIVE LOW (0 = testing).** Answer mode only; line must be disconnected. |
| 5 | Rx Rate | **0 = 301–600 bps, 1 = 0–300 bps** |
| 6 | **DTR** | Must be **1** to enable the modem. Must **not** be active during dialing. |
| 7 | Aux Out 3 | TTL line on the aux connector |

Idle value used throughout the manual: `OUT BA+3, 0`. Fully enabled: `0x7F`.

### IN BA+0 — UART status (**active high**)

| Bit | Name |
|---|---|
| 0 | **TBMT** — transmit buffer empty |
| 1 | **DAV** — received char available |
| 2 | TEOC — transmitter serializer finished |
| 3 | RPE — receive parity error |
| 4 | OR — overrun |
| 5 | FE — framing error (**also fires on a received BREAK**) |
| 6 | Aux In 1 |
| 7 | Aux In 2 |

### IN BA+2 — modem status. **Note the active-low bits — this is the classic place to get it backwards.**

| Bit | Name | Polarity |
|---|---|---|
| 0 | **Dial Tone** | **0 = dial tone present** (energy in the 200–600 Hz band) |
| 1 | **Ringing** | **0 = ringing.** Integrated ~0.1 s, so it stays 0 across a burst and toggles between bursts — software counts rings by counting transitions. |
| 2 | **CTS** | **0 = clear to send.** Going to 1 mid-session means the connection dropped. |
| 3 | Rx Break | **1 = break received** (150 ms continuous space). *Latched*; clamped high until CTS; cleared with Brk Rel. |
| 4 | **AP** Answer Phone | **0 = off-hook / modem holding the line.** Auto-resets ~1.5 s after CTS drops, or on a 17 s no-handshake timeout. |
| 5 | Digital FO | Raw carrier — diagnostics only |
| 6 | Mode | 1 = originate, 0 = answer |
| 7 | **Timer Pulses** | 40% high / 60% low |

**There is no carrier-detect bit.** Carrier is inferred from CTS (bit 2) and AP (bit 4).

### IN BA+3 — no data. Strobes rate register → interrupt mask register.

## The rate generator — the heart of the board

```
10 MHz crystal → ÷10 = 1 MHz (feeds the 6860)
              → ÷4  = 250,000 Hz
              → ÷N  (N = the value written to BA+2, 1–255)   → UART 16× clock
              → ÷100                                          → Timer Pulses (IN BA+2 bit 7)
```

(That is §7.3.3.1's account. §3.2.8 describes the same chain as ÷10, then ÷2 into the divider and ÷2 out of it — structurally different, arithmetically identical at 250,000/N. Neither is a contradiction in result.)

> **Baud = 250,000 / (N × 16)**
> **Timer pulses = 250,000 / (N × 100) Hz, 40% high / 60% low**

| N | Baud | | N | Dial rate |
|---|---|---|---|---|
| 142 | 110 | | 250 | 10 pps |
| 52 | 300 | | 125 | 20 pps |
| 26 | 600 | | | |
| 255 | ~61 | | | |

Usable baud range **61–600**. Non-standard rates only interoperate MM-103 ↔ MM-103.

The 40/60 duty cycle is exactly the break/make ratio for pulse-dialing a telephone. That is not a coincidence — it is the design.

### The coupling is the point, and it bites twice

1. **One divider sets both the baud rate and the dial rate.** Software must load the dial rate, dial, then restore the baud divisor. During a 300-baud session (N=52) the timer bit runs at **48 Hz, not 10 Hz** — the manual warns about this explicitly.
2. **The same write port stages the interrupt mask.** Loading the mask (`OUT BA+2, mask` then `IN BA+3`) **destroys the baud rate**, so it must be reloaded afterward.

**Model these as the single shared resource they are.** Split them into independent registers and you will run software that appears to work but is silently wrong — and you will never reproduce the whole class of bug where a program forgets to restore the divisor.

## Pulse dialing

Hardware-timed, software-counted. The relay is driven directly by **SH** (`OUT BA+0` bit 0): 1 = off-hook/loop closed = "make"; 0 = on-hook/loop open = "break". **DTR must be off during dialing.**

> **This section describes the hardware and the guest software, not a simulator feature.** The card models SH as the relay it is; it does not decode these pulses into digits. See *How it would be simulated*.

1. `OUT BA+3, 0` — clear the 6860 (DTR off).
2. `OUT BA+2, 250` — 10 pps (or 125 for 20 pps).
3. `OUT BA+0, 1` — go off-hook (SH = 1).
4. Poll `IN BA+2 AND 1` until it reads **0** (dial tone present).
5. **Sync to the timer:** wait until the Timer Pulse bit is 1, then until it is 0 — i.e. sync to a falling edge (§8.6 lines 260–270).
6. Per pulse (digit 0 = 10 pulses):
   - `OUT BA+0, 0` (break) — wait while Timer Pulse is **low** → times the **60% break**
   - `OUT BA+0, 1` (make) — wait while Timer Pulse is **high** → times the **40% make**

   > This is **§8.6's** phase, not §8.3's. The two listings are opposites; only this one produces the ~60% break a telephone needs. See the errata below.
7. **Interdigit delay:** 7 full timer-pulse cycles (§8.6 lines 350–380) — 700 ms at 10 pps, 350 ms at the 20 pps §8.6 itself uses.
8. Next digit. Digit list terminated by sentinel 11.

## Interrupts

Four maskable sources: TBMT, DAV, Ring-OR-Dial-Tone, Timer Pulses. Gated by `OUT BA+0` bit 7 plus CPU interrupt enable.

**Setup sequence:** `OUT BA+2, mask` → `IN BA+3` (transfers) → **reload `OUT BA+2` with the baud/dial divisor** (destructive!) → set BA+0 bit 7 → enable CPU interrupts.

**No vector is generated.** As shipped, a solder jumper **E10 → E9** ties the request to plain S-100 **INT (pin 73)**, which with nothing driving the data bus during `IntAck` means the CPU reads a floating `0xFF` = **RST 7 (0x38)**. To use a vectored interrupt instead, move the jumper to **E1–E7** (the S-100 vectored-interrupt lines, bus pins 4–11).

> **Open item:** the manual **never states the E-number ↔ VI*n* correspondence.** It just says to consult your CPU/VI card manual. Resolve before implementing vectored operation.

**Interrupts do not stack or queue** — with more than one enabled, the handler must read `IN BA+0` and `IN BA+2` to find the cause. Reproduce that faithfully; software depends on the polling.

## 6860 timing to model

- **SH/RI must be held ≥ 51 ms** to be accepted by the 6860. Software provides this delay using the Timer Pulse bit.
- **2 s billing delay** — on an *incoming* answer, transmit and receive are inhibited for 2 seconds after off-hook.
- **17 s handshake timeout** from release of SH/RI.
- Originate handshake, all measured **from receipt of the 2225 Hz carrier** (not cumulative): **150 ms** → Rx unclamped; **450 ms** → the board transmits 1270 Hz; **750 ms** → **CTS asserts**. On an *answer*-mode connection CTS instead goes low **450 ms** after receipt of carrier (§10.2).
- AP drops ~1.5 s after CTS is lost. Space disconnect: ELS 1.5 s / ESS 0.3 s.
- Software should reset SH/RI to 0 **51 ms after CTS** so automatic disconnect can work. Leaving them set defeats it.

## How it would be simulated

- The phone line is a **`ByteStream`**. `CONNECT pmmi socket:host:port` places a call; `CONNECT pmmi serial:/dev/cu.usbserial-X` drives a real modem; a **listening** socket lets the board *be* called (incoming connection → ring detect → answer-on-ring).
- **SH is a relay pin, and only a relay pin.** It goes on- and off-hook, and going on-hook hangs up. The make/break pulses a guest's dialer program produces are *not* decoded into digits, and no property maps a dialed number onto a host and port — **placing the call is `CONNECT`'s job.** Reading a number out of the hook relay and steering it at an IP address would be a behavior this card never had; the MM-103 did not know what it was dialing either. A period dialer program still runs, still pulses the hook, and still works in the only sense the hardware ever offered.
- Timer pulses and all 6860 timing come from the **`EventQueue`**, in T-states.
- Properties: `port`, `interrupt` (`int` | `vi0..vi7`), `answer` (auto-answer), `carrier_delay`, `self_test`.

Note that this puts the far end's address on the **host** side, where the operator sets it, rather than behind a dial tone the guest has to produce.

## Quirks reproduced

1. **SH and RI are inverted** relative to the 6860 datasheet — the MM-103's own logic inverts them.
2. **Active-low status bits** on IN BA+2: Dial Tone, Ringing, CTS, AP. Everything else on that port is active high.
3. **Read of BA+3 is a side-effect strobe**, returns 0xFF. Never treat it as a status read.
4. **Write to BA+2 is doubly-loaded** — rate register *and* interrupt-mask staging.
5. **Registers are write-only**; shadow every written value.
6. **The BA+0 control word is multipurpose** — every write rewrites SH, RI, UART format, and interrupt-enable together.
7. **51 ms minimum pulse width** on SH/RI.
8. **Tx Brk and Self Test are active low.**
9. **Rx Break is latched** until Brk Rel.
10. **Timer rate changes when you change baud rate** (same divisor).
11. **FE also fires on a received BREAK** — the manual says to use Rx Break (BA+2 bit 3) instead, as it is more reliable.

## Manual errata — record these so nobody "fixes" the simulator to match a typo

**The full, verified list is in [the reference doc](../../reference/PMMI%20MM-103%20Modem%20and%20Comm%20Adapter.md) §11** — ten items, each with its page and section. Every erratum below was re-checked against the manual on 2026-07-27 and **all of them stand**, including the three whose citations had been in doubt. The two most dangerous are new to that pass:

- **⚠ The overbar convention in §7.3 is stated backwards.** *"A bar over the bit description indicates that the bit is to be set to a 1 to activate the function"* — but its own example bit (DTR) carries no bar, and every barred bit's subsection says it activates at **0**. **A bar means active low.** Believing the sentence inverts Self Test, Tx Brk, Brk Rel and ESD, none of which is on the data path — so it would still pass a naive send/receive test.
- **⚠ §8.3 and §8.6 dial opposite phases of the timer bit.** §8.3 breaks while the bit is high, §8.6 while it is low. **§8.6 is the working dialer**; §8.3 emits a 40% break and its prose miscalls the high phase "60 percent".
- §3.2.12 says **five** interrupt sources, then lists **four**. Four is right. **Citation confirmed** — the "five" claim and the four-item list are both in §3.2.12, and §2.0, §7.3.3.2 and Figure 7.0 all agree on four.
- The stated **25,000 pps** timer ceiling is arithmetically impossible (250,000 / 100 = **2,500**). The manual's own *low* end — "down to 9.8 pulses per second" — derives exactly as 250,000/(255 × 100), which both proves the formula and identifies 25,000 as a slipped decimal point.
- Duty cycle is given as "60/40" in §2.0 and "40% ON, 60% OFF" in §7.3.3.1 / §7.4.4.8. **40% high / 60% low** is correct. **Citation confirmed** — §2.0 item 13 reads *"Rate and 60/40 Duty Cycle Under Software Control"*, and §3.2.8 says "60-40" as well; the two §7 sections are the ones speaking specifically about the status bit.
- The **divisor's low limit** is given as 1–255 in §3.2.8 and 0–255 in §7.3.3.1. Treat **1–255** as usable; no §8 program writes 0.
- §7.3.4's body says **"OUT Address 4"** in a subsection titled "Register at Relative Address 3". There is no address 4.
- **DTR polarity is contradictory:** §7.3.4.7 and the example code say bit 6 = 1 enables the modem; §10.3 (reproduced Motorola text) describes the un-inverted 6860 view. **Implement the §7.3.4 / example-code polarity** — DTR is unbarred in Figure 7.0, and every working program writes `0x7F`.
- The UART part number appears nowhere. Not an OCR failure — it simply is not stated.

**The 2 second billing delay is confirmed and is not an inference.** It is stated in **§3.2.6** item 7 — *"When answering an incoming call, the transmit and receive signals are inhibited for two seconds after the phone goes off-hook"* — and appears as a block in Figure 3.1-1. It is simply absent from the §10.2/§10.3 Motorola reprint, which is where it had been looked for.

## Limitations (when built)

- No modulation is simulated; the "phone line" is a byte stream, so the Bell 103 frequencies are documentation, not signal processing.
- Dial-tone detection is synthetic (asserted when a `ByteStream` endpoint is available).
- **Dialed digits are not decoded.** The hook relay follows SH faithfully, but nothing counts the pulses and no number selects a far end — you attach one with `CONNECT`. This is a deliberate limit, not a gap waiting to be filled: decoding a phone number and resolving it to a host would be an invented feature.
- Self-test / loopback should be honored, since diagnostics software uses it.
