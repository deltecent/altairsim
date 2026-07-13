# MITS 88-VI/RTC — Vectored Interrupt / Real Time Clock

**Status:** done (2026-07-13). `altairsim ps2int` boots MITS Programming System II with interrupts enabled and Ctrl-C breaks a runaway program back to the monitor. `ctest -R acceptance-ps2-int`.

## The real hardware

Two MITS options on one PC board, sold separately and documented separately (the manual has a "88-Vector Interrupt Theory of Operation" and an "88-Real Time Clock Theory of Operation"), but sharing a schematic, a slot, and **one control register**. February 1976.

**The 88-VI** is an **Intel 8214** priority interrupt control unit. The Altair bus carries eight vectored-interrupt request lines, **VI0–VI7**; an I/O card's interrupt pin is hardwired to one of them. The 8214 watches all eight, applies a priority mask, drives `pINT` (pin 73) itself, and then — during the CPU's interrupt-acknowledge cycle — **jams an `RST n` instruction onto the data bus**. The 8080 executes it, and the machine vectors to one of eight fixed addresses.

**The 88-RTC** is a divider chain off either the 60 Hz mains or a 10 kHz derivative of the 2 MHz system clock. When the interval elapses it sets a flip-flop whose output — **"RI"** on the schematic — is **jumpered to one of the eight VI levels**, exactly as if it were a separate card. It is not special-cased anywhere; it interrupts through the same priority encoder as everything else.

Straps: the RTC's source (jumper `CF` for 10 kHz, `LF` for line), its divide rate (`A`/`B`/`C`/`D` → ÷1, ÷10, ÷100, ÷1000), and **`RI` → one of the eight interrupt levels**. That last one is the one that matters — see *Quirks*.

## Sources

| Source | Path | Authority |
|---|---|---|
| MITS 88-VI/RTC manual (1976) — theory of operation, control register, RTC rates, schematic | `reference/88-VI-RTC.pdf` | **Authoritative** for ports, priority, vectors, RTC rates |
| MITS Programming System II monitor — its own RST 7 service routine, disassembled from `tapes/MitsPS2/PS2-MON.TAP` | in-tree artifact | **Authoritative where the manual contradicts itself** — see below |
| MITS Programming System II ReadMe | `tapes/MitsPS2/-ReadMe.pdf` | Authoritative for how the package expects to be configured |
| Intel 8214 datasheet | not in tree | Explains *why* bits 0–2 are inverted (active-low `B0–B2`) |

**THE MANUAL CONTRADICTS ITSELF, AND THE ARTIFACT BROKE THE TIE.**

Two bits of the control register are ambiguous in the manual, and both are load-bearing:

1. **Bits 0–2, the current level.** Page 3's prose says the level-4 routine "outputs a `100` for bits 2, 1 and 0" — that is 4, read straight. But page 5's *table* says level 4 → `MVI A,13Q` → bits 2–0 = `011` = 3, and its worked example for level 2 → `MVI A,15Q` → `101` = 5. Those are **7 − level**. Two data points against one.

2. **Bit 3.** "Data bit 3 disables the current level register… This bit should be output as a 0 (low) only during initialization… if data bit 3 is not set by the initialization, level 7 can not interrupt." That sentence parses both ways.

So we asked the only real client of this card that exists — **the PS2 monitor's own interrupt service routine**:

```
0038  F5        PUSH PSW          ; the RST 7 vector
0039  C3 DB 08  JMP 08DB
08DB  3E 08     MVI A,08          ; level 7: bit 3 SET, bits 0-2 = 000
08E0  F6 C0     ORI C0            ; + bit 7 (VI enable) + bit 6 (RTC enable)
08E2  D3 FE     OUT FE
08E4  DB 11     IN 11             ; read the 6850, dropping its IRQ
...
0921  AF        XRA A             ; the Ctrl-C break path
0925  F6 C0     ORI C0            ; 0xC0 -- bit 3 CLEAR
0927  D3 FE     OUT FE
```

`0x08` is exactly the manual's *table* entry for level 7 (`MVI A,10Q`). **The table is right and the prose is wrong: bits 0–2 are the ones-complement of the level.** And the ISR **sets** bit 3 on entry (so another level 7 cannot interrupt the handler) and **clears** it on exit (re-opening the machine to level 7), which settles bit 3: **1 = the current-level comparison is live; 0 = disabled, anything may interrupt.** That also explains the manual's warning — a card that was never initialized sits at level 0 with the comparison live, and nothing outranks level 0.

**When the document and the artifact disagree, disassemble the artifact.** (The same rule recovered the PS2 bootstrap loader; see `machines/ps2.toml`.)

## Register reference

**One port: 376 octal = 254 decimal = `0xFE`. WRITE ONLY.** The card drives the data bus during `IntAck` and at no other time — there is no `IN`.

> Note: this is the port AltairZ80/SIMH squats on for its "pseudo device". It is not a free port; it belongs to this card. See DESIGN.md §0.2.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `0xFE` | control register (below) | *nothing — the card does not decode a read* |

| Bit | Meaning |
|---|---|
| 7 | `1` = enable the 88-VI structure. **POC leaves this LOW** — the card comes up disabled. |
| 6 | `1` = enable the RTC interrupt; `0` = disable it. |
| 5 | `1` = clear the RTC divider chain (restart it at time zero). |
| 4 | `1` = clear the RTC's interrupt flip-flop. **This is the only thing that clears it.** |
| 3 | `1` = the current-level register is live. `0` = comparison disabled, anything may interrupt. |
| 2–0 | The current interrupt level, **ones-complement**: the value written is `7 − level`. |

**Priority: VI0 is highest, VI7 is lowest.** Level *n* vectors through `RST n` (opcode `0xC7 | n<<3`) to octal *n*0:

| Level | RST | Vector | Handler byte (`ORI C0` applied) |
|---|---|---|---|
| 0 | RST 0 | `0x0000` | `0xCF` |
| 2 | RST 2 | `0x0010` | `0xCD` |
| 4 | RST 4 | `0x0020` | `0xCB` |
| 7 | RST 7 | `0x0038` | `0xC8` |

**VI7 → RST 7 → `0x0038` is what the PS2 ReadMe means by "vector 7".**

## How it is simulated

`src/boards/mits-88virtc.{h,cpp}`, board type **`virtc`**.

- **Decodes** `Cycle::IoWrite` at `port`, **and `Cycle::IntAck`** — but *only* when the VI structure is enabled and a line is asking that the mask admits. A disabled or idle 88-VI **does not claim the acknowledge**, the cycle goes unclaimed, and the bus floats `0xFF` — which the 8080 executes as `RST 7`. That is not a fallback; it is the same floating-bus rule as unmapped memory, and it is exactly what an Altair with no VI card does.
- **`assertsInt()`** — pin 73, pulled when the structure is enabled and `winner() >= 0`.
- **`assertsVi()`** — the card **pulls a VI line itself**, when the RTC fires and `RI` is strapped. The RTC's interrupt goes out onto the backplane and comes back in through the priority encoder like any other card's, which is what the schematic does. No back door.
- **`watchesVi()`** — the only board in the tree that returns `true`. The bus calls `intChanged()` on it whenever any VI line moves, because its `pINT` is a function of eight wires it does not own.
- **The RTC timer** is a cancel-and-re-arm one-shot on the `Clock` queue (the `Sio2Board::refresh()` idiom). Period is computed in **T-states**, not hertz: `tStatesPer(base) * divide`, because three of the eight jumper combinations are fractional (60 Hz ÷ 1000 = 0.06 Hz) and would round to nothing.
- **Interrupts:** this card *is* the interrupt hardware. Its own `rtc_interrupt` strap accepts `none | vi0..vi7` — **not `int`**, because RI does not go to pin 73 and the manual forbids mixing (see *Quirks*).

### `properties()`

| Property | Legal | Default | Notes |
|---|---|---|---|
| `port` | `00`–`FF` (hex) | `FE` | 376 octal. Write-only. |
| `rtc_source` | `line` \| `clock` | `line` | 60 Hz mains, or 10 kHz off the 2 MHz clock |
| `rtc_divide` | `1` \| `10` \| `100` \| `1000` | `1` | |
| `rtc_interrupt` | `none` \| `vi0`…`vi7` | **`none`** | The `RI` strap. `none` is what MITS Programming System II needs. |

Those four are **straps** — things you set with a soldering iron. The card also publishes its live control register as four **read-only** properties, because port `0xFE` is write-only and there is otherwise no way to see what the guest has programmed into it:

| Property | Meaning |
|---|---|
| `vi_enabled` | Is the VI structure enabled? (control bit 7; POC clears it) |
| `level_live` | Is the current-level comparison in circuit? (control bit 3) |
| `current_level` | The current interrupt level, decoded (control bits 0–2) |
| `rtc_pending` | Has the RTC's interrupt flip-flop set? (cleared by control bit 4) |

They have no setter, so `SHOW vi0` prints them `(read-only)` and `CONFIG SAVE` skips them — a saved machine round-trips to the same *straps*, and none of this live state leaks into it. Setting one would be the monitor reaching into the 8214 and pretending the guest had done it.

### Seeing the wiring: `SHOW BUS IRQ`

This card is the reason `SHOW BUS IRQ` exists (`docs/cli-commands.md`). It prints the eight lines, what is strapped to each, which are being pulled, and which one **wins** — the last of those coming from `Board::intWinner()`, which only a priority encoder answers, so the monitor never has to know what an 88-VI is. A line that is pulled but refused by the current-level register reads `MASKED`, which is otherwise indistinguishable from a lost interrupt.

It also enforces the manual's rule from the *other* side: strap any card to `int` while this one is in the backplane and the view says so, quoting the manual.

### Reset

- **`Reset::PowerOn` (POC*, cold):** everything off. *"POC (power-on-clear) ensures that all functions on the 88-VI (RTC) are disabled when power is first applied."* VI structure disabled, RTC interrupt disabled, the flip-flop cleared, the divider stopped.
- **`Reset::Bus` (RESET*, warm):** **nothing.** The manual runs POC to this logic and says nothing about the front-panel RESET* reaching it, so it deliberately does not. A warm reset leaves the VI structure exactly as the guest programmed it.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **Bits 0–2 are the ones-complement of the level** (`7 − level`), not the level. The manual's prose says otherwise; its table and the shipped monitor say this. | Every priority comparison is inverted. Level 0 (highest) looks like level 7 (lowest). A level-7 handler appears to be running at level 0 and nothing can ever interrupt it. |
| **Bit 3 = 1 makes the comparison live; 0 disables it.** | Get it backwards and an uninitialized card sits at level 0 with the compare on, so **nothing can ever interrupt** — the machine boots and then simply never responds to a key. |
| **VI0 is HIGHEST, VI7 is LOWEST.** | Priority inverts. In a one-device machine you will not notice, which is worse — it will bite the first time a second card is strapped. |
| **The RTC's interrupt is cleared ONLY by writing bit 4.** *"Interrupt on most I/O boards is cleared by reading or writing the data channel of the board. The RTC does not operate in this manner."* | The flip-flop stays set, the line stays low, and the machine re-enters the RTC handler forever. |
| **`RI` unstrapped means the RTC interrupts nobody, however enabled it is.** This is a *missing wire*, not a disabled register. | See below — this is the whole reason MITS Programming System II runs at all. |
| **A disabled card must not claim `IntAck`.** Off is off: it drives nothing, not a "safe" vector and not zero. | The bus stops floating to `0xFF`, and a machine that should have taken `RST 7` takes whatever you invented instead. |
| **An 88-VI system may have NO board on a single-level interrupt.** *"Interrupts on I/O boards must be hardwire connected to one of the eight 88-VI interrupt levels."* Enforced: `rtc_interrupt` refuses `int`. | Two cards drive the acknowledge cycle, or the VI board's vector is overridden by a floating bus. |

### The unsoldered wire that makes PS2 work

The ReadMe says:

> The programming package assumes the 88-VI/RTC board is present and **enables real-time clock interrupts from the board, yet doesn't provide an interrupt handler for the RTC.** In order to run with interrupts enabled, interrupts from the RTC on the 88-VI/RTC must be **disabled**…

And the monitor really does enable them — its ISR runs `ORI C0` on *every* pass, which sets bit 6 unconditionally. **You cannot talk the software out of it.** So how do you disable an interrupt that the software insists on enabling?

**You don't solder the wire.** On the real card, `RI` is jumpered to one of the eight levels by hand; leave it unjumpered and the flip-flop sets, and sets, and nothing is listening. That is `rtc_interrupt = "none"`, and it is why **no special case for the RTC appears anywhere in the C++**. The hardware already had the answer.

## Limitations and deliberate departures

- **The 8214's `ELR`/`ECS`/`SGS` pins are not modeled as pins.** We model the *documented behavior* of the control register, not the chip's internal strobes. Nothing on the Altair bus can observe them.
- **`rtc_source = "line"` is 60 Hz of *emulated* time**, derived from `Clock::tStatesPer(60)` — not from the host's wall clock or mains. With `clock_hz = 0` (the default, flat out) the guest still sees exactly 33,333 T-states between ticks; it just gets them sooner. That is the same bargain the whole simulator makes. Software that tries to use the RTC to measure *real* elapsed time will be wrong, and would have been wrong on any emulator.
- **The RTC timer is armed only when it can be observed** (interrupt enabled *and* `RI` strapped). The divider is otherwise a counter nobody can read. This is not just an optimization: a permanently-armed periodic timer leaves `Clock::queued()` non-zero forever, and that is one of the two conditions the run loop uses to decide a `HLT` has finished (`src/core/debug.cpp`) — so an always-ticking RTC would mean a machine with this card in it could never stop on a HLT again.
- **No 8-level nesting test.** The card supports it and the code implements the comparison, but no period software in the tree nests interrupts, so it is exercised only by unit tests.

## Verification

- **`tests/test_virtc.cpp`** — the vector table, VI0-beats-VI7 priority, the current-level mask, the ones-complement encoding (pinned *because the manual is wrong about it*), a disabled card not claiming `IntAck`, the RTC firing into an unstrapped `RI` and interrupting nobody, and a 6850 on VI2 producing `RST 2` = `0xD7` — a byte a floating bus could never produce, which is what makes that assertion worth anything.
- **`tests/acceptance/ps2-int.exp`** — **MITS Programming System II, with interrupts.** It springs the ReadMe's own booby trap (type a word the monitor doesn't know and it hunts the cassette for a program by that name, forever) and then rescues the machine with **Ctrl-C**, which is the one thing the ReadMe says interrupts are *for*. Every byte of that rescue crosses the new hardware: the 6850 pulls VI7 → the 88-VI prioritizes it and pulls pin 73 → the 8080 acknowledges → the 88-VI claims the `IntAck` cycle and jams `RST 7` → the monitor's handler at `0x0038` finds `0x03` and longjmps home.
- **`acceptance-ps2-int-control`** — the same script against `ps2` (A9 up, no VI card), registered `WILL_FAIL`. The machine must stay hung. **If that control ever passes, the test above is passing on something that is not an interrupt.**
- `Bus::setVerify(true)` re-derives all nine interrupt wires every instruction and aborts on the first board that forgets to announce a change.

## References

- MITS, *88-VI Vector Interrupt / 88-RTC Real Time Clock* (1976) — `reference/88-VI-RTC.pdf`
- MITS, *Programming System II* ReadMe — `tapes/MitsPS2/-ReadMe.pdf`
- `machines/ps2int.toml` — the ReadMe's machine, exactly
- DESIGN.md §4.4, §4.4.2 — the two interrupt models, and why `assertsVi()` is a bitmask
