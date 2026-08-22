# CompuPro System Support 1

**Status:** milestone 3 of 4 (issue #392) — the OKI MSM5832 real-time clock, the 2651 UART
serial channel, and the 8253 interval timer are implemented. The dual 8259A interrupt controllers
are being added in the last phase; the 9511A/9512 math-chip socket is deferred to its own issue
(#393 — an empty socket is the real board's default).

## The real hardware

The **System Support 1** ("SS-1") is a Godbout/CompuPro multifunction S-100 board (Document
#11620, Board No. 162, 1981). In one 16-port I/O block it packs a **2651 UART** serial channel,
an **Intel 8253** interval timer (three 16-bit counters), two cascaded **Intel 8259A** interrupt
controllers (15 sources), an **OKI MSM5832** battery-backed real-time clock/calendar, and a
socket for an **AMD 9511A/9512** math coprocessor. An optional 4K RAM/EPROM block uses IEEE-696
extended addressing and is out of scope here (it is the one part of the board the Altair bus
cannot carry — see #392, #344).

The block sits on any 16-port boundary; CompuPro's documented standard base is **50H**, and all
CompuPro software assumes it, so that is this board's default. The MSM5832 is crystal-timed
(32.768 kHz) and battery-backable, so it keeps time with the machine powered off.

## Sources

| Source | Path | Authority |
|---|---|---|
| System Support 1 Technical Manual (1981) | `reference/CompuPro System Support 1.md` | Port map, register bits, sample programs |
| OKI MSM5832 (as reprinted in the SS-1 manual) | `reference/OKI MSM5832.md` | Clock register/digit encoding |
| Signetics 2651 (as documented in the SS-1 manual) | `reference/Signetics 2651 USART.md` | UART register model, baud table, status/command polarity |
| Intel 8253 (as reprinted in the SS-1 manual) | `reference/Intel 8253.md` | Timer register map, control word, mode behaviour |

The manual's own clock section (pp.27–30) is the authority for the digit encoding. Two facts in
it are easy to get wrong and were corrected against the manual text while implementing: the
seconds are **write-ignored, not read-as-zero**, and the Hours-10 / Days-10 mode bits sit in the
**upper two bits of the low nibble** (bit3/bit2), not bit1/bit0.

## Register reference

The whole board occupies 16 ports from `base` (default 50H). This milestone implements the four
timer ports, the two clock ports and the four UART ports:

| Addr (base 50H) | OUT (write) | IN (read) |
|---|---|---|
| base+4 (54) | 8253 counter 0: load per read/load format | counter 0 count |
| base+5 (55) | 8253 counter 1: load per read/load format | counter 1 count |
| base+6 (56) | 8253 counter 2: load per read/load format | counter 2 count |
| base+7 (57) | 8253 control word: SC(7-6) RL(5-4) mode(3-1) BCD(0) | — (write-only, floats 0xFF) |
| base+10 (5A) | MSM5832 command: Hold(6) / Write(5) / Read(4) / digit-select(3-0) | — (write-only, floats 0xFF) |
| base+11 (5B) | MSM5832 data: BCD digit in the low nibble | the selected digit |
| base+12 (5C) | 2651 data: byte to transmit | received byte (clears RxRDY) |
| base+13 (5D) | — (the SYN register, unused; not decoded) | 2651 status: TxRDY(0) RxRDY(1) TxEMT(2) PE/OE/FE(3-5) DCD(6) DSR(7) |
| base+14 (5E) | 2651 mode: MR1 then MR2 (pointer-sequenced) | current mode register |
| base+15 (5F) | 2651 command: TxEN(0) DTR(1) RxEN(2) break(3) reset-err(4) RTS(5) | current command |

Digit select (command bits 3-0): 0/1 Seconds 1/10, 2/3 Minutes 1/10, 4/5 Hours 1/10, 6 Day of
Week, 7/8 Days 1/10, 9/10 Months 1/10, 11/12 Years 1/10. **Hours-10 (5)** carries the digit in
bits 1:0, PM in bit2, and the 12/24-hour mode in bit3 (this model runs in 24-hour mode).
**Days-10 (8)** carries the digit in bits 1:0 and the leap-year flag in bit2.

The remaining ports of the block (8259A `+0..+3`, math socket `+8/+9`) are not decoded yet and
float `0xFF`.

## How it is simulated

- **Decodes** the timer ports `base+4..+7` (control at +7 write-only), the clock ports `base+10`
  (write-only) and `base+11`, and the UART ports `base+12..+15` (status at +13 read-only) as I/O
  cycles. Everything else in the block floats until a later phase claims it.
- The clock is the host's own time-of-day. The `Msm5832` chip (`src/chips/msm5832.h`) reads
  `platform::localCalendar(std::time(nullptr) + offset_)` and returns the requested BCD digit.
  `offset_` is a signed second count, 0 at power-on (the guest sees real wall time) and moved
  only when the guest sets the clock.
- **Setting the clock** follows the datasheet Hold sequence: raising Hold snapshots the display
  into an edit buffer, each Write strobe updates one digit, and dropping Hold composes the
  buffer back to a host `time_t` (via `mktime`) and stores the new `offset_`. A Hold that only
  fenced a glitch-free read writes nothing and changes nothing.
- **No Clock/EventQueue for the clock:** the MSM5832 has nothing to schedule; the board's 6 µs /
  150 µs wait states are modelled as instant (the reference notes this is safe unless a program
  times the wait, which none does).
- **The 2651 UART** (`src/chips/sig2651.h`) is modelled on the same lines as the 8251: a
  transmit character occupies the line for one frame-time (a TxRDY deadline), and a receive frame
  is clocked in over that same time before RxRDY rises. Its **baud rate comes from Mode Register
  2** (the on-chip generator), so the guest's MR2 write sets the line rate. The four ports remove
  the 8251's data-vs-control ambiguity; the only internal sequencing is the MR1/MR2 pointer. DCD
  and DSR status bits read asserted and PE/OE/FE read 0 — a byte-clean transport has no modem
  control to simulate and no line noise to report. The board owns the UART's clock deadline
  (`refresh()`/`nextEdge()`), the same shape as the SBC-100/200.
- **The 8253 interval timer** (`src/chips/intel8253.h`) is not stepped — each counter remembers
  the T-state its count was loaded and the current count and OUT level are **derived from the
  elapsed clock ticks** (DESIGN.md 7.5). The counter clock is 2 MHz (the S-100 bus signal),
  independent of the CPU clock, so one tick takes `cpu_hz / 2_000_000` T-states. Modes 0, 2, 3
  and 4 have their real OUT behaviour; mode 3's count read-back is approximate (a real chip
  decrements it by two per tick), but OUT is exact. Modes 1 and 5 are gate-triggered and the
  board ties every gate high, so they idle (no gate edge ever arrives). The Counter Latch Command
  and BCD counting are modelled. Its OUT lines are readable now; wiring them to the 8259A is the
  next phase, so the timer drives no interrupt yet and its accesses do not go through `refresh()`.
- **Interrupts:** the 2651's RxRDY can be routed to pINT or an S-100 VI line by the `serial`
  unit's `interrupt` jumper (default `none`). On the standard board the UART and the timer feed
  the on-board 8259A instead — that path lands with the 8259A phase. **DMA:** none.
- **`properties()`:** `base` (the 16-port block base, default 50H), a read-only `clock` string
  and a read-only `timer` string (each counter's mode, count and OUT). The `serial` **unit**
  carries `baud`, `interrupt` and `connect`.

### Reset

- `Reset::PowerOn` (POC*, cold): clears the command latches. `offset_` is **preserved** — the
  MSM5832 is battery-backed, so a fresh set survives a power cycle just as it survives RESET.
- `Reset::Bus` (RESET*, warm): same — clears the command latches, keeps the time.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Seconds are write-ignored (forced to 0), but **read** the running seconds | A clock that read seconds as 0 would timestamp everything at `:00`; one that honored a seconds write would let a program set a seconds value the chip never accepts |
| Setting the clock zeroes the seconds | A program that sets HH:MM expects the seconds to restart at 0, as on the real chip |
| Hours-10 digit in bits 1:0, 24-hour flag in bit3, PM in bit2 | Software masking `& 0x03` for the tens digit reads garbage if the digit is placed elsewhere; the 20-hour range (20-23) decodes wrong |
| Days-10 leap-year flag in bit2 | Feb 29 handling and any software that reads the leap bit misbehaves |
| Battery-backed: the set time survives RESET and power-on | A clock that reverted to host time on RESET would lose a guest-set time that real hardware keeps |
| 2651 status TxRDY=D0/RxRDY=D1 active-high; DCD/DSR (D6/D7) and DTR/RTS command bits **inverting** | A polled driver reads the ready flags in the wrong bit; a modem-control test sees the RS-232 sense backwards |
| 2651 baud comes from Mode Register 2, not a strap | A guest that sets 300 baud via MR2 still runs at the wrong rate if the model ignores MR2 |
| 2651 MR1 must be written before MR2 (shared address, one-bit pointer) | The frame and the baud land in each other's register |
| 8253 counter clock is 2 MHz, not the CPU clock | A timing loop calibrated against the timer runs at the wrong rate on any machine whose CPU is not 2 MHz |
| 8253 control word for a counter stops it until a fresh count is loaded | A program that rewrites the mode and expects the old count to keep running reads a stale OUT |
| 8253 a count of 0 means the full modulus (65536, or 10000 in BCD) | A "divide by 65536" that loads 0 undercounts to nothing if 0 is taken literally |

## Limitations and deliberate departures

- **24-hour mode only.** The chip always reports the 24-hour mode bit and never AM/PM; a program
  that programs 12-hour mode still reads a 24-hour clock. No period software here depends on
  12-hour mode.
- **Two-digit year, host century.** The MSM5832 holds only a two-digit year; when the guest sets
  the year, the century is pinned from the host's current year.
- **Weekday is derived, not stored.** A weekday the guest writes inconsistent with the date is
  ignored — `mktime` recomputes the day of week from the date.
- **2651 modem-control and line noise are not simulated.** DCD/DSR read asserted and PE/OE/FE
  read 0 for a byte-clean transport; a real serial-port endpoint would make those real events.
  Sync mode, auto-echo and the loopback operating modes (command bits 6/7) are not modelled — the
  SS-1 uses normal async only. The transmitter is not gated on TxEN (a guest polls TxRDY first).
- **8253 gate inputs are tied high (ungated).** The SS-1 pulls every gate high, so counting is
  always enabled and the gate-triggered one-shot modes (1 and 5) never trigger — no gate edge is
  modelled. **Mode 3's count read-back is approximate** (OUT is exact); the terminal-count timing
  folds the load clock into the write instant rather than modelling the extra load cycle.
- **The 8253 OUT lines drive no interrupt yet.** They are readable, but routing them to the
  on-board 8259A (the standard interrupt use) lands with the interrupt-controller phase.
- The rest of the board (interrupts, math socket, 4K RAM/EPROM) is not modelled yet; those ports
  float.

## Verification

`tests/test_ss1.cpp` drives the MSM5832 through real bus cycles: reads match the host wall clock
at power-on, a programmed set reads back exactly (including Feb 29 in a leap year), the seconds
are forced to 0 on a set, the Hours-10 mode bit and Days-10 leap bit are correct, the set time
survives RESET/power-on and a SNAPSHOT/RESTORE round-trip, and the `base` strap moves the block.
The 2651 is pinned both as a bare chip and over the bus: the MR1-then-MR2 pointer, the full MR2
baud table, the status polarity, TxRDY as a deadline, a character clocked in over a frame time,
the RxEN gate, snapshot of the programmed frame/baud, the four-port decode, an end-to-end
receive/transmit through the bus, and the RxRDY interrupt jumper. The 8253 is pinned as a bare
chip and over the bus: mode 0 interrupt-on-terminal-count, the Counter Latch Command freezing a
16-bit read, the mode 2 rate generator and mode 3 square wave (even and odd counts), `nextEdge`,
BCD counting, the 2 MHz counter clock being independent of the CPU clock, a snapshot round-trip,
the four-port decode, and programming/reading a counter over the bus.

## References

- `reference/CompuPro System Support 1.md` — the distilled board reference.
- `reference/OKI MSM5832.md` — the clock chip.
- `reference/Signetics 2651 USART.md` — the UART chip.
- `reference/Intel 8253.md` — the interval timer chip.
- Issue #392 — the board's scope and the IEEE-696 finding.
