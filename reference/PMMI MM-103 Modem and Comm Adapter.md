# PMMI MM-103 Modem and Communications Adapter

Source: [pmmi-mm-103-modem-and-comm-adapter.pdf](#)

PMMI Communications, Falls Church VA. © 1982 (warranty effective March 1, 1979). A **Bell 103
originate/answer modem on a single S-100 board** — the first S-100 modem FCC-approved for direct
connection to the public switched telephone network, so it needs no external DAA. On one card:
a Motorola **MC6860L** modem chip, an unnamed 1602-family **UART**, a programmable **rate
generator**, a **pulse dialer**, a **dial-tone detector**, a **ring detector**, a maskable
**interrupt** system, and an auxiliary I/O connector. This is a distilled emulation reference:
the FCC/telephone-company prose, the modem-theory tutorial (§1), adjustment procedures (§5) and
the marketing sections are intentionally omitted; only what is needed to emulate the board in
software is kept.

**The board has no schematic.** None was shipped with this manual — §3.0 *is* the theory of
operation, and §10.0 reprints Motorola's MC6860 description. There is therefore **no second
source to check the prose against**, which is why the ten worked programs in §8 matter so much
here: they are period artifacts written for real silicon by the people who built the card, and
in three places below they are the tiebreaker.

---

## 1. What the board is

- **Modem: Motorola MC6860L** digital modem chip (§7.2.2), Bell 103 FSK, up to 600 bps.
- **UART: never named in the manual.** §7.2.3 describes only "a universal asynchronous
  receiver/transmitter". Its status set (TBMT/DAV/TEOC/RPE/OR/FE) and its control word are the
  classic AY-5-1013 / TR1602 / COM2502 / IM6402 shape — see [`com2502`](com2502.md) — but the
  part number **is not stated anywhere**, and that is not an OCR failure.
- **Rate generator:** on-board **10.00 MHz** crystal, divided down to clock both the 6860 and the
  UART, and to time dialing (§3.2.8, §7.3.3.1).
- **Dialer: pulse only.** §3.2.9's dialer is a relay driver, and §3.2.5 item 5 says the board
  "Dials the telephone using dial pulsing (**as opposed to touch tone**)". There is no DTMF
  generator on this board.
- **Interrupts:** four maskable sources, **no vector generated** (§3.2.12).
- **Telephone line coupler:** a separate FCC-approved miniature coupler carrying the switch-hook
  relay and supplying the half-wave-rectified ring signal (§3.2.7, §3.2.11).

**Bell 103 frequencies (§2.0):**

| Mode | Transmit | Receive |
|---|---|---|
| Originate | 1070 Hz space / 1270 Hz mark | 2025 Hz space / 2225 Hz mark |
| Answer | 2025 Hz space / 2225 Hz mark | 1070 Hz space / 1270 Hz mark |

Other §2.0 characteristics: sensitivity −50 dBm, dynamic range 50 dB, **baud rate 61 to 600
(software controlled)**, modes originate and answer (software controlled), frequency/rate control
from the on-board 10.00 MHz crystal oscillator.

**S-100 bus lines used (§7.2.1 sidebar):**

| Signal | Pins |
|---|---|
| Address A0–A7 | 79, 80, 81, 31, 30, 29, 82, 83 |
| Data Out DO0–DO7 | 36, 35, 88, 89, 38, 39, 40, 90 |
| Data In DI0–DI7 | 95, 94, 41, 42, 91, 92, 93, 43 |
| Vectored interrupts | 4 through 11 |
| Interrupt request (INT) | 73 |
| **Interrupt acknowledge (INTA)** | **96** |
| I/O control | sOUT 45, sINP 46, PDBIN 78, PWR 77 |
| Power on clear (POC) | 99 |
| Power | +8 V pins 1 & 51, +16 V pin 2, −16 V pin 52 |

---

## 2. Addressing

The board occupies **4 consecutive I/O addresses** and can sit on any 4-address boundary. A
**6-position DIP switch** in the center of the board is compared against the **6 high-order
address bits (A2–A7)**; the low two bits select the register (§7.2.1, §8.0, p3).

> **`OPEN` / `OFF` = a 1 bit** (§7.2.1: *"'OPEN' indicates a 1 bit"*; §8.0: *"A switch set to
> OFF = 1 and set to ON = 0"*).

- **Default base `0xC0`** (192 decimal) — `OFF OFF ON ON ON ON` → `1100 00XX` (§8.0). Every §8
  program except §8.10 uses it.
- **Alternate base `0xE0`** (224 decimal) — **manual-sanctioned, not folklore.** p3: *"the North
  Star DOS system uses C0 for memory protect control; therefore, PMMI North Star software has
  been readdressed to E0."* The §8.10 North Star listings open with `P0=224`.

### The four addresses — read and write are different registers

**Read and write at the same address mean entirely different things**, and there are **five
output destinations behind four output addresses** (§7.3: *"The MM-103 uses 5 output control
registers, two of which are addressed via a single out address"*).

| Addr | `OUT` (write) | `IN` (read) |
|---|---|---|
| BA+0 | UART format + modem control (SH/RI) + interrupt enable | UART status + Aux In 1/2 |
| BA+1 | Transmit data | Receive data |
| BA+2 | **Rate generator** *and* the staging register for the interrupt mask | Modem status |
| BA+3 | 6860 modem control + Aux Out 3 | **No data.** A *strobe* that copies the rate register into the interrupt-mask register |

> **All three control registers are write-only.** The port at the same address reads something
> else entirely, so an emulation **must shadow every value written**.

§7.4.1: *"Of the 4 input addresses assigned, only the first 3 actually bring data into the
computer."*

---

## 3. The overbar convention — read this before any bit table

Figure 7.0 marks some bit names with an **overbar**. The bars are load-bearing: they are the
only place the board's active-low bits are marked as a set.

> **⚠ §7.3's stated convention is backwards, and its own example contradicts it.** The text
> reads: *"A bar over the bit description indicates that the bit is to be set to a 1 to activate
> the function. For example, if Bit 6 is on OUT 3, then DTR (Data Terminal Ready) will be on, or
> active."*
>
> **Bit 6 of OUT 3 (DTR) carries no bar in Figure 7.0.** The example is of an *unbarred* bit
> activating at 1. Meanwhile every *barred* bit's own subsection says it activates at **0** —
> ST (§7.3.4.5 *"set to 0"*), Brk Rel (§7.3.4.4 *"holding Break Release low"*), Tx Brk
> (§7.3.4.3 *"A Transmit Break (low)"*), ESD (§10.3.1 *"provided the Enable Space Disconnect is
> low"*).
>
> **A bar means ACTIVE LOW**, the ordinary convention. The sentence in §7.3 is simply inverted.
> Taking it at face value produces a board whose Self Test, Break and Space Disconnect are all
> backwards — and it would still pass a naive data test.

The bars were read **from the page image** of Figure 7.0, not from the text layer; overbars do
not survive OCR.

---

## 4. Output registers

### `OUT BA+0` — UART format / modem control / interrupt enable

Figure 7.0 "OUT ADDRESS 0". **No barred bits — all active high.**

| Bit | Manual's name | Meaning |
|---|---|---|
| 0 | **SWITCH HOOK (ORG)** | 1 = Originate mode; **directly drives the line switch-hook relay** (1 = off-hook = loop closed). Pulsed for dial pulsing. **Inverted vs. the 6860 datasheet.** |
| 1 | **RING INDICATOR (ANS)** | 1 = put the 6860 in Answer mode → Answer Phone goes low → phone answered. **Inverted vs. the 6860 datasheet.** *Not* the ring signal from the line. |
| 2 | NB1 | data-bits select, LSB |
| 3 | NB2 | data-bits select, MSB |
| 4 | NP | 1 = no parity |
| 5 | EPS | 0 = odd, 1 = even |
| 6 | TSB | 0 = 1 stop bit, 1 = 2 stop bits (1½ if a 5-bit character) |
| 7 | **ENABLE INTERRUPT** | 1 = allow the board to drive the S-100 interrupt request |

**The inversion is deliberate and documented** (§7.3.1): *"The MM-103 logic inverts these bits
from that indicated in the 6860 manual. This was necessary to simplify certain software functions
and to allow for better power-on-clear reset for most S-100 bus computers."* Figure 10.0-1 marks
exactly two 6860 pins with `*NOTE: THESE BITS HAVE BEEN INVERTED IN THE MM-103 LOGIC` — **pin 19
Ring Indicator and pin 21 Switch Hook**, and no others.

**Every write rewrites all of it at once** (§7.3.1): *"Because each register is 'multipurpose,' it
is necessary that the setting for all control bits be in the required state for the desired
operation before the register is loaded."* Software must OR in the full desired state.

**Character format (§7.3.1.7)** — bits 3:2 as a 2-bit value:

| NB2 NB1 | Data bits |
|---|---|
| 00 | 5 |
| 01 | 6 |
| 10 | 7 |
| 11 | 8 |

with bit 4 = 1 → no parity (bit 5 then a don't-care), bit 5 = 0 odd / 1 even, and bit 6 = 0 → 1
stop bit / 1 → 2 stop bits (1½ when 5 data bits are selected). The manual prints all 24
combinations as a table; the four fields are independent, so the rule above reproduces it.

**Observed values in the §8 programs** (the emulation's test vectors):

| Value | Meaning |
|---|---|
| `0x01` | SH on, go off-hook (dial tone test §8.2, dialer §8.3, originate §8.5) |
| `0x02` | RI on, answer the phone (§8.4) |
| `0x5C` | 8 data bits, no parity, 2 stop bits, SH and RI **cleared**, interrupts off (§8.5, §8.6, §8.7) |
| `0x5D` | the same with SH still set (§8.7, during origination) |
| `0x5E` | the same with RI set — "SET UP UART AND ANSWER MODE" (§8.8, §8.10.1) |
| `0x00` | everything off / on-hook |

### `OUT BA+1` — transmit data

The character to send (§7.3.2). Load only when TBMT is set, since the register can be loaded
faster than the baud rate.

### `OUT BA+2` — rate generator **and** interrupt-mask staging

**One register with two jobs** (§7.3.3): *"This register has a dual function. First, it directly
controls the rate generator… Second, its contents may be transferred to the Interrupt Mask
holding register by an input command to Relative Address 3."*

As the **rate generator** it is an 8-bit divisor `B7..B0` (§5 below).

As the **interrupt mask** (Figure 7.0 "OUT 2B"), staged here and latched by a subsequent
`IN BA+3`:

| Bit | Meaning |
|---|---|
| 0 | TBMT interrupt enable |
| 1 | DAV interrupt enable |
| 2 | Ring **OR** Dial Tone interrupt enable (wired-OR — §3.2.11 says the two are mutually exclusive) |
| 3 | Timer Pulses interrupt enable |
| 4 | **AUX OUT 1** (open-collector output pin, not an interrupt) |
| 5 | **AUX OUT 2** (open-collector output pin, not an interrupt) |
| 6–7 | unused (crossed out in Figure 7.0) |

A 1 selects the interrupt (§7.3.3.2). Bits 4–5 are not interrupts at all — they set two
open-collector pins on the auxiliary connector (§7.3.3.3).

> **Loading the mask destroys the baud rate.** §7.3.3: *"Since this also loads the rate generator
> with the interrupt mask pattern, the mask is loaded first (if it is to be used) and then the
> desired rate is loaded for normal operation."* §9.0 step 2 repeats it: *"Relative address 2
> must be reloaded with the Baud rate or dial rate."*

### `OUT BA+3` — MC6860 modem control

Figure 7.0 "OUT 3 MODEM CONTROL". **Bits 1, 2, 3 and 4 carry overbars — they are ACTIVE LOW.**

| Bit | Manual's name | Polarity | Meaning |
|---|---|---|---|
| 0 | ESS = 1 / ELS = 0 | active high | 1 = **Short** space disconnect (hang up on 0.3 s of space); 0 = **Long** (1.5 s) |
| 1 | **ESD** | **ACTIVE LOW** | Enable Space Disconnect. When active and DTR is pulsed to disconnect, the modem sends space for 3 s or until loss of threshold, whichever first; if inactive, it sends data instead. Disconnect occurs at 3 s either way (§7.3.4.2) |
| 2 | **Tx Brk** | **ACTIVE LOW** | Held low > 34 ms → 233 ms continuous space. Must be held high ≥ 34 ms beforehand, and only issued **after CTS** (§7.3.4.3) |
| 3 | **Brk Rel** | **ACTIVE LOW** | Break Release — hold low ≥ 20 µs to clear the Rx Break status bit (§7.3.4.4) |
| 4 | **ST** | **ACTIVE LOW** (0 = testing) | Self Test: demodulator switched to the modulator frequency. **Answer mode only**, and the telephone line must be disconnected (§7.3.4.5) |
| 5 | Rx Rate | active high | **0 = 301–600 bps, 1 = 0–300 bps** (§7.3.4.6) |
| 6 | **DTR** | active high | Must be **1** to enable the modem. Hold low ≥ 34 ms to initiate a disconnect; disconnect occurs 3 s later. **Must not be active during dialing**, since DTR also controls the switch hook via the Answer Phone bit (§7.3.4.7) |
| 7 | Aux Out 3 | active high | TTL output on the auxiliary connector (§7.3.4.8) |

**`0x7F` is the "modem enabled, nothing asserted" value** and is what the working programs write
(§8.5 line 70, §8.6 line 420, §8.7 `MVI A,7FH`): DTR on, Rx Rate = 0–300 bps, and ST / Brk Rel /
Tx Brk / ESD all left high = inactive. **`0x00` is the idle/clear value** (§8.2 line 100 *"CLEAR
MODEM CHIP TO IDLE"*, §8.4 line 90, §8.6 line 140) — DTR off, which is exactly the state dialing
requires.

That `0x7F` works, and that §8.8's self-test writes `0x40` (DTR on, **ST = 0**), together confirm
the active-low reading of bit 4 independently of the overbars.

---

## 5. Input registers

### `IN BA+0` — UART status. **All active high, no bars.**

| Bit | Name | Meaning |
|---|---|---|
| 0 | **TBMT** | transmit buffer empty — another character may be sent (§7.4.2) |
| 1 | **DAV** | a received character is available at `IN BA+1` (§7.4.2.1) |
| 2 | TEOC | transmitter serializer has sent the last bit. "Not normally used" (§7.4.2.2) |
| 3 | RPE | received parity error (§7.4.2.3) |
| 4 | OR | over-run — a character arrived before the previous one was read (§7.4.2.4) |
| 5 | FE | framing error — **also set by a received BREAK** (§7.4.2.5) |
| 6 | Aux In 1 | TTL input on the auxiliary connector |
| 7 | Aux In 2 | TTL input on the auxiliary connector |

§7.4.2.5 is explicit that FE is the *wrong* way to detect a break: *"This bit will also be set
whenever the MM-103 receives a 'BREAK' from the distant end… Since the modem break is a more
reliable method of detecting a break, its bit should be used for this purpose"* — i.e. use
Rx Break at `IN BA+2` bit 3.

### `IN BA+1` — receive data

The character sent by the distant modem (§7.4.3). Format per the `OUT BA+0` control word.

### `IN BA+2` — modem status. **Bits 0, 1, 2 and 4 carry overbars — ACTIVE LOW.**

This is the classic place to get a modem card backwards: four active-low bits sit among four
active-high ones.

| Bit | Manual's name | Polarity | Meaning |
|---|---|---|---|
| 0 | **DIAL TONE** | **0 = dial tone present** | Sufficient signal in the 200–600 Hz dial-tone filter band (§7.4.4.1) |
| 1 | **RINGING** | **0 = ringing** | Integrated over ~0.1 s, so it does **not** follow the cyclic ring pulses — it "will pulse 0 and 1 following the ring interval". **Software counts rings by counting transitions** (§7.4.4.2) |
| 2 | **CTS** | **0 = clear to send** | Transmit data unclamped from a steady mark. Going to 1 mid-session means the connection was broken (§7.4.4.3) |
| 3 | RX BRK | active high | 1 = a continuous 150 ms space was received. **Latched** — clamped high by the modem chip until CTS is established; cleared with Brk Rel (§7.4.4.4) |
| 4 | **ANS PHONE** | **0 = off-hook** | Set low by the modem chip on receipt of RI **or** SH **and** DTR. The telephone line is held off-hook for data transmission. Reset ~1.5 s after CTS resets (§7.4.4.5) |
| 5 | DIGITAL FO | active high | Raw digital carrier signal — "monitored for test and diagnostic purposes only" (§7.4.4.6) |
| 6 | MODE | active high | **1 = originate, 0 = answer.** Changes state when ST is applied. Diagnostics only (§7.4.4.7) |
| 7 | TIMER PULSES | active high | The rate-generator-derived timing signal, **40% one / 60% zero** (§7.4.4.8) |

> **There is no carrier-detect bit.** The manual never provides one; carrier presence is inferred
> from CTS (bit 2) and Answer Phone (bit 4). Digital FO is explicitly diagnostics-only.

Two cautions the manual gives about the Dial Tone bit (§7.4.4.1): busy signals, ringing and
"off-hook-too-long" tones in the same band will also pull it to 0, and voice will make it change
state — *"it is not a reliable ring detector. The Ringing bit should be used for this purpose."*

### `IN BA+3` — the strobe

§7.4.5: *"An input to this address does not provide input data. Its purpose is to allow the
loading of the interrupt mask register from the rate generator. The transfer occurs at the time
the instruction is executed."* §7.3.3.2: *"IN 3 has no other purpose and does not load data into
the computer."*

> **⚠ INFERENCE:** the manual never says **what value appears on the data bus** for `IN BA+3`.
> Nothing on the board drives it, so on a real S-100 machine the CPU reads the floating bus —
> conventionally `0xFF`. That is an inference from the bus, not a documented value.

---

## 6. The rate generator and the timer pulses

All timing on the board comes from the 10.00 MHz crystal. **The manual describes the divider
chain twice, differently, but both reduce to the same number:**

- **§3.2.8:** ÷10 → 1 MHz (to the 6860); 1 MHz ÷2 → into a 74193 down-counter divider ÷N; the
  divider output ÷2 → the UART transmit/receive clocks.
- **§7.3.3.1:** ÷10 → 1 MHz (to the 6860); 1 MHz ÷4 → **250,000 Hz** into the rate generator ÷N
  → the UART clock.

Both give a UART clock of **250,000 / N**, and the UART divides by 16 internally:

> **Baud = 250,000 / (N × 16)**, where N is the byte written to `OUT BA+2`.
> **Timer pulses = 250,000 / (N × 100) Hz**, at **40% high / 60% low**.

The manual prints the baud formula itself (§7.3.3.1: *"Rate = 250,000/(Reg × 16)"*), and **§8.8's
loopback program computes the divisor in source**: `160 S=250000!/BR/16`.

| N (decimal) | Baud | | N (decimal) | Dial rate |
|---|---|---|---|---|
| 142 | 110 | | 250 | 10 pps |
| 52 | 300 | | 125 | 20 pps |
| 26 | 600 | | | |

Usable range **61 to just over 600 baud** (§7.3.3.1, §2.0). Non-standard rates work only
MM-103 ↔ MM-103.

**The ÷100 timer output is confirmed arithmetically by two independent artifacts:**

- §8.1's clock program writes `OUT 194,250` and counts **10 ticks per second** (`70 IF B=10 THEN
  S=S+1`), with the description *"loads the timer to produce 0.1 second clock ticks"*.
  250,000/(250 × 100) = **10.0 Hz** ✓
- §7.3.3.1's own stated low end, *"down to 9.8 pulses per second"*, is 250,000/(255 × 100) =
  **9.80 Hz** ✓

§7.4.4.8 gives the same relation from the other direction: *"When communications are taking
place, this bit will be pulsing at 16 times the data Baud rate divided by 100."* At 300 baud
(N=52) that is **48 Hz, not 10 Hz** — the manual flags this as a trap for anyone using the timer
bit as a clock during a session.

### The coupling is the point, and it bites twice

1. **One divisor sets the baud rate *and* the dial rate *and* the timer rate.** §7.3.3.1:
   *"Because the same rate generator is used for both dialing and Baud rate generation, it must
   be changed each time a new rate is required, and be left at the Baud rate during
   communications."*
2. **The same write port stages the interrupt mask**, so loading a mask destroys the divisor and
   it must be reloaded (§7.3.3, §9.0 step 2).

Model these as the single shared resource they are. Split into independent registers, period
software still appears to work while being silently wrong, and the whole class of
"the program forgot to restore the divisor" bug becomes unreproducible.

---

## 7. Pulse dialing

Hardware-timed, software-counted. The relay is driven **directly by SH** (`OUT BA+0` bit 0):
1 = off-hook = loop closed = **make**; 0 = on-hook = loop open = **break**. §7.3.1.1: *"The SH
bit directly controls the telephone line switch hook relay."* **DTR must be off during dialing**
(§7.3.4.7).

The procedure, from §8.2/§8.3/§8.6:

1. `OUT BA+3, 0` — clear the 6860 to idle (DTR off).
2. `OUT BA+2, 250` — 10 pps (or 125 for 20 pps).
3. `OUT BA+0, 1` — SH = 1, go off-hook.
4. Poll `IN BA+2 AND 1` until it reads **0** — dial tone present.
5. **Sync to the timer bit**, then per pulse (digit 0 = 10 pulses) alternate SH = 0 (break) and
   SH = 1 (make), timing each against one phase of the timer bit.
6. **Interdigit delay: 7 full timer-pulse cycles** (§8.6 lines 350–380).
7. Digit list terminated by a sentinel **11** (§8.3, §8.6).

> **⚠ THE TWO DIALER PROGRAMS DIAL OPPOSITE PHASES.** This is the sharpest contradiction in the
> manual, and it is between two *artifacts*, not two pieces of prose.
>
> - **§8.3** (manual dialer demo) sets SH = 0 and waits **while the timer bit is HIGH**, then
>   SH = 1 and waits while LOW. Its own prose says line 150 *"waits for the on-hook period (60
>   percent of the dial pulse)"*.
> - **§8.6** (auto-dialer) sets SH = 0 and waits **while the timer bit is LOW** (line 300
>   `IF (INP(A)AND B)=E THEN 300`), then SH = 1 and waits while HIGH (line 320).
>
> **§8.6 wins.** §7.3.3.1 and §7.4.4.8 independently state the bit is **40% high / 60% low**, and
> telephone pulse dialing requires the *break* to be the ~60% interval. Only §8.6 produces that.
> §8.3 produces a 40% break, and its prose calling the high phase "60 percent" directly
> contradicts §7.4.4.8's *"40% One and 60% Zero"*.
>
> The 40/60 split **is** the break/make ratio a telephone needs — §7.3.3.1 calls it *"the correct
> characteristics for dialing the telephone"*. That is the design, not a coincidence.

For an emulation that decodes dialed digits by counting SH transitions, both programs dial
correctly and the phase choice does not matter. It matters only to an implementation that
validates the ratio — which must not take §8.3 as its oracle.

---

## 8. Answering, originating, and 6860 timing

### Answering an incoming call (§7.4.4.2, §10.2)

**Auto-answer on this board is a software procedure the hardware merely enables.** There is no
"answer" bit that does the whole job:

1. The line rings. `IN BA+2` bit 1 goes **low**, integrated over ~0.1 s so it holds across a
   burst and toggles *between* bursts. Software counts rings by counting transitions.
2. §7.4.4.2: *"The phone is not actually answered without an OUT to Address 0 with Bit 1 set to
   1, which places the modem in the answer mode."* Setting **RI** drives Answer Phone low and
   takes the line off-hook. (§7.4.4.2 also notes a special case: *"where it is desired to go into
   the originate mode upon ring, Bit 0 of OUT Address 0 would be set to 1."*)
3. **AP** (`IN BA+2` bit 4) reads 0. RI should then be reset to 0 (§7.3.1.2) — AP now holds the
   line, and leaving RI set defeats automatic disconnect.
4. **Billing delay:** §3.2.6 item 7 — *"When answering an incoming call, the transmit and receive
   signals are inhibited for **two seconds** after the phone goes off-hook."* (This is in the
   theory of operation and in Figure 3.1-1's block diagram, **not** in the §10 reprint.)
5. Answer transmits 2225 Hz. The far originate modem replies with 1270 Hz after a 450 ms echo-
   suppressor delay. After 1270 Hz has been present **150 ms**, receive data is unclamped;
   **CTS goes low 450 ms after receipt of carrier** (§10.2).
6. **17 second timeout**: hang-up occurs 17 s after RI is released if the handshake never
   completes (§7.3.1.1, §10.2).

### Originating (§10.3)

SH places the board in Originate mode; it looks for 2225 Hz until 17 s after SH is released.
All three delays below are measured **from receipt of the 2225 Hz carrier**, not cumulatively:

| Elapsed | Event |
|---|---|
| 150 ms | Receive Data unclamped from mark — data can be received |
| 450 ms | The board transmits 1270 Hz to the remote modem |
| 750 ms | **CTS taken low** — data can now be transmitted as well |

### The rest of the timing

- **SH or RI must be held ≥ 51 ms** to be accepted by the 6860 (§7.3.1.2). Software provides this
  with the Timer Pulse bit.
- **Software should reset SH or RI 51 ms after CTS** (§7.4.4.5) so that automatic disconnect can
  still work. *"Monitoring AP will show this delay."*
- **AP resets ~1.5 s after CTS resets** (§7.4.4.5).
- **Threshold detect** (§10.2): must be low for 20 µs at least once every 32 ms; absence for
  > 51 ms denotes loss of receive carrier and starts hang-up. Frequency tolerance during
  handshake ±100 Hz from the mark frequency.
- **Space disconnect:** ELS 1.5 s of continuous space, ESS 0.3 s (§7.3.4.1, §10.2.1).
- **Initiating a disconnect:** pulse DTR for > 34 ms; 3 s of continuous space follows, then
  hang-up (§7.3.4.7, §10.3.1).
- **Rx Break** is clamped high from receipt of a 150 ms space until a Break Release is issued
  (§10.2.1).

---

## 9. Interrupts

**Four maskable sources** — TBMT, DAV, Ring-OR-Dial-Tone, Timer Pulses — gated by the mask
(`OUT BA+2` → `IN BA+3`) and by `OUT BA+0` bit 7, plus the CPU's own interrupt enable.

**Setup sequence (§9.0), in this order:**

1. `OUT BA+2, mask` — stage the mask.
2. `IN BA+3` — transfer it to the mask register.
3. **Reload `OUT BA+2`** with the baud or dial divisor — step 2 destroyed it.
4. `OUT BA+0` with bit 7 set (together with the UART control bits).
5. Enable CPU interrupts.

**No vector is generated.** §3.2.12: *"The interrupt system does not generate interrupt vector
addresses. Therefore, the interrupt routine must read the status register to determine what has
occurred."* §7.3.3.2: *"The interrupt system does not stack or queue interrupts, so if more than
one has been enabled it is necessary to read in the modem status register or the UART status
register to determine which of the multiple interrupts are requiring attention."*

**As shipped**, a jumper joins **E10 → E9**: E9 is the S-100 INT line (pin 73), E10 is the
board's interrupt request. With nothing driving the data bus during the acknowledge, the CPU
reads a floating `0xFF` = **RST 7 → 0x38** — which §9.0 names outright as the shipped behavior
(*"this is for RST7, 8080 interrupt mode"*).

> **⚠ The E-hole count is inconsistent, and the VI mapping is never given.** §9.0 says *"there
> are 8 unsoldered holes marked E1 to E8"*, then refers only to **E1–E7** as the S-100 vectored
> interrupt lines. More importantly, the manual **never states which E-pad corresponds to which
> VI level** — it says only *"Refer to the manual that accompanied your CPU or vectored interrupt
> card."* **Do not guess this mapping.**

### The INTA field modification (§9.0)

The board answers **INTA on S-100 pin 96**. PMMI documents a no-charge modification for two
cases: a CPU that does not generate INTA on pin 96, or **more than one MM-103 in one machine**
with full interrupt control over each.

1. Cut the track from edge connector **pin 96**, near the first feed-through hole at the lower
   edge of the board.
2. Jumper **U12 pin 5** (a 7406) to **pin 13 of the AUX OUT socket (P3)** with #30 wire-wrap wire.

After the mod the board ignores bus INTA, and *"it will be necessary to provide the INTA via
software by controlling the level of AUX OUT 3, which is bit 7 of the modem control word at
relative address 3"*, inside the interrupt service routine. **This is the only stated purpose for
Aux Out 3, and U12 is the only IC designator the manual ever gives.**

---

## 10. Auxiliary interface (§6.0)

A 14-pin DIP connector (**P3**) on the top edge of the board:

- **1 TTL output** — Aux Out 3 (`OUT BA+3` bit 7).
- **2 buffered open-collector outputs** (inverting drivers) — Aux Out 1 and 2 (`OUT BA+2` bits 4
  and 5, latched as part of the interrupt mask).
- **2 inputs the computer can sense** — Aux In 1 and 2 (`IN BA+0` bits 6 and 7).
- Telephone line (tip and ring), the coupler's ring signal, and a line letting the coupler be
  powered externally while the computer is off.

Intended uses (§3.2.13): powering the computer up on a telephone ring or external alarm,
controlling external devices, and switching voice announcement/recording equipment onto the line.

---

## 11. Errata and self-contradictions

Recorded so nobody "fixes" an emulation to match a typo. The first three are new to this pass;
the rest were carried in and are re-confirmed here against the pages named.

1. **The overbar convention in §7.3 is stated backwards** and its own example uses an unbarred
   bit. Bars mean **active low**. See §3 above — this is the most dangerous one in the document.
2. **The two dialer programs dial opposite phases of the timer bit** (§8.3 vs §8.6). §8.6 is
   correct; §8.3 produces a 40% break. See §7 above.
3. **The divisor's low limit is given twice, differently.** §3.2.8: *"any integral value from 1
   to 255, inclusive."* §7.3.3.1: *"may be divided by any value from 0 to 255."* Division by zero
   is meaningless; a 74193 down-counter loaded with 0 wraps, giving an effective 256. **Treat
   1–255 as the usable range** and 0 as undefined — no program in §8 writes 0.
4. **§7.3.4's body says "OUT Address 4"** (*"OUT Address 4 latches the 6860 chip control
   registers"*) in a subsection titled *"Register at Relative Address 3"*. There is no address 4;
   the register is at **BA+3**, as every §8 program confirms.
5. **§3.2.12 says five interrupt sources, then lists four.** *"a maskable interrupt capability
   that accomodates five status signals: 1. Transmit buffer empty 2. Receive data available
   3. Ring detect 'OR' dial tone detect 4. Last output on Baud Rate Generator divider chain."*
   **Four is right** — §2.0, §7.3.3.2 and Figure 7.0's four mask bits all agree.
6. **The stated timer ceiling of 25,000 pps is arithmetically impossible.** §7.3.3.1: *"This
   signal ranges from 25,000 down to 9.8 pulses per second."* 250,000/(1 × 100) = **2,500**.
   The **low** end derives exactly — 250,000/(255 × 100) = 9.80 — which both proves the formula
   and identifies 25,000 as a slipped decimal point for 2,500.
7. **The duty cycle is written both ways round.** §2.0 (*"60/40 Duty Cycle"*) and §3.2.8 (*"the
   dial pulse 60-40 duty cycle"*) versus §7.3.3.1 (*"40% ON and 60% OFF"*) and §7.4.4.8 (*"40%
   One and 60% Zero"*). **The status bit is 40% high / 60% low** — the two §7 sections are the
   ones speaking specifically about the bit, and they agree with each other.
8. **DTR polarity is contradictory between §7 and the §10 reprint.** §7.3.4.7 says DTR *"must be
   set to 1 before the modem function will be enabled"*; §10.2 agrees (*"if the Data Terminal
   Ready line is high"*); but §10.3 says *"If the Data Terminal Ready input is enabled (low)"*
   and §10.3.1 calls it *"the normally low Data Terminal Ready"*. §10 is reproduced Motorola text
   describing the raw 6860. **Implement the §7.3.4.7 polarity** — DTR is unbarred in Figure 7.0,
   and every working program writes `0x7F` with bit 6 set to enable the modem.
9. **§8.8's self-test writes `0x40` to `OUT BA+3`**, which asserts ST (bit 4 = 0) as intended but
   also leaves Tx Brk, Brk Rel and ESD at 0 — their active states. The manual does not explain
   this, and every other program uses `0x7F`. Treat `0x40` as evidence for ST's polarity only.
10. **The UART part number appears nowhere.** Not an OCR failure — it simply is not stated.

---

## 12. Emulation checklist

The load-bearing facts, restated. Test files should cite this document by name beside their bit
constants.

**Decode**

- Four consecutive ports; base = a 6-position DIP compared against **A2–A7**, **OPEN/OFF = 1**.
  Default **`0xC0`**; **`0xE0`** is the manual's own North Star alternative. Both deserve a test.
- **Read and write are different registers at every address.** Five output destinations behind
  four output addresses.
- **All three control registers are write-only** — shadow every write.
- **`IN BA+3` is a side-effect strobe**: it copies the rate register into the interrupt mask and
  returns no data. The value on the bus is **inferred** `0xFF` (floating), not documented.

**Registers**

- `OUT BA+0` — all active high: SH(0), RI(1), NB1(2), NB2(3), NP(4), EPS(5), TSB(6), ENABLE
  INT(7). Every write rewrites all of it.
- `OUT BA+2` — **one register, three jobs**: baud divisor, dial rate, interrupt-mask staging.
- `OUT BA+3` — **bits 1 (ESD), 2 (Tx Brk), 3 (Brk Rel), 4 (ST) are ACTIVE LOW**; DTR (6),
  Rx Rate (5), Aux Out 3 (7) and ESS/ELS (0) are active high. `0x7F` = enabled and idle.
- `IN BA+0` — all active high: TBMT(0), DAV(1), TEOC(2), RPE(3), OR(4), FE(5), Aux In 1/2(6,7).
- `IN BA+2` — **bits 0 (Dial Tone), 1 (Ringing), 2 (CTS), 4 (Ans Phone) are ACTIVE LOW**;
  Rx Brk(3), Digital FO(5), Mode(6), Timer Pulses(7) are active high.
- **There is no carrier-detect bit.** Infer carrier from CTS and AP.

**Timing**

- **Baud = 250,000 / (N × 16)**; **timer = 250,000 / (N × 100)**, **40% high / 60% low**.
- N = 142/52/26 → 110/300/600 baud; N = 250/125 → 10/20 pps. Usable 61–600 baud.
- Changing the baud rate changes the timer rate — at 300 baud the timer runs at **48 Hz**.
- Loading the interrupt mask **destroys the baud divisor**; it must be reloaded.
- **SH/RI must be held ≥ 51 ms**; software resets them 51 ms after CTS.
- **2 s billing delay** inhibits transmit and receive on an incoming answer (§3.2.6).
- **17 s** handshake timeout; **AP resets ~1.5 s** after CTS is lost.
- Originate handshake from carrier receipt: **150 ms** Rx unclamped, **450 ms** transmits,
  **750 ms** CTS. Answer: **150 ms** Rx unclamped, **450 ms** CTS.
- Space disconnect: **ELS 1.5 s**, **ESS 0.3 s**. Tx Brk low > 34 ms → 233 ms space.
  Brk Rel low ≥ 20 µs clears the latched Rx Break.

**Behavior**

- **SH and RI are inverted** relative to the 6860 datasheet — and only those two (Figure 10.0-1).
- **SH drives the switch-hook relay directly**: 1 = off-hook = make, 0 = break. **DTR must be off
  while dialing.**
- **Ringing must be produced as real bursts with real gaps**, not a level — software counts
  transitions, not a state.
- **Answering is a guest procedure**: ring → set RI → AP low → billing delay → handshake → CTS.
  An emulation must not shortcut to a connected state.
- **Rx Break is latched** and clamped high until CTS; **FE also fires on a received BREAK** and
  the manual says to prefer Rx Break.
- **Interrupts do not stack or queue** — the handler must read both status ports to find the
  cause. Four sources; **no vector**; factory jumper E9–E10 → plain INT → floating `0xFF` →
  **RST 7**.
- **The E-pad ↔ VI-level mapping is not in the manual.** Do not guess it.
- **Dialing is pulse only.** No DTMF anywhere on this board.
