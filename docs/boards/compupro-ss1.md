# CompuPro System Support 1

**Status:** feature-complete for issue #392 — the OKI MSM5832 real-time clock, the 2651 UART
serial channel, the 8253 interval timer, and the dual 8259A interrupt controllers are all
implemented. The math socket (issue #393) takes an AMD 9511A or an AMD 9512, and is empty by
default. The optional 4K RAM/EPROM block is out of scope (it is the one IEEE-696 part the
Altair bus cannot carry — #344).

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
| Intel 8259A (as reprinted in the SS-1 manual) | `reference/Intel 8259A.md` | Interrupt-controller registers, priority, cascade, the CALL vector |
| AMD 9511A (Intel 8231A), data sheet and AMD's Floating Point Processor Manual | `reference/AMD 9511A.md` | Commands, cycle counts, status bits, the float format, the ch.9 function algorithms |
| AMD 9512 (Intel 8232), data sheet and the same manual | `reference/AMD 9512.md` | Commands, cycle counts, status bits, the single and double formats, ERR |

The manual's own clock section (pp.27–30) is the authority for the digit encoding. Two facts in
it are easy to get wrong and were corrected against the manual text while implementing: the
seconds are **write-ignored, not read-as-zero**, and the Hours-10 / Days-10 mode bits sit in the
**upper two bits of the low nibble** (bit3/bit2), not bit1/bit0.

## Register reference

The whole board occupies 16 ports from `base` (default 50H). What is implemented is the four
8259A ports, the four timer ports, the two clock ports and the four UART ports:

| Addr (base 50H) | OUT (write) | IN (read) |
|---|---|---|
| base+0 (50) | master 8259A, A0=0: ICW1 / OCW2 / OCW3 | master IRR or ISR (per OCW3) |
| base+1 (51) | master 8259A, A0=1: ICW2-4 / OCW1 (mask) | master IMR (the mask) |
| base+2 (52) | slave 8259A, A0=0 | slave IRR or ISR |
| base+3 (53) | slave 8259A, A0=1 | slave IMR |
| base+4 (54) | 8253 counter 0: load per read/load format | counter 0 count |
| base+5 (55) | 8253 counter 1: load per read/load format | counter 1 count |
| base+6 (56) | 8253 counter 2: load per read/load format | counter 2 count |
| base+7 (57) | 8253 control word: SC(7-6) RL(5-4) mode(3-1) BCD(0) | — (write-only, floats 0xFF) |
| base+8 (58) | math chip: push a byte onto its stack | pop a byte off its stack |
| base+9 (59) | math chip command: SR(7), opcode(6-0) | math chip status: BUSY(7) SIGN(6) ZERO(5), the rest per chip |
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

The math socket `+8/+9` is decoded only when `math` is `9511a` or `9512`. With `math = none`
(the default, the stock empty socket) both ports float `0xFF`. The two chips' command sets and
status bits are in `reference/AMD 9511A.md` and `reference/AMD 9512.md`.

**The interrupt matrix** (the stock J7/J8 shunt) wires the two controllers like this:

| Master IR | Source | Slave IR | Source |
|:--:|---|:--:|---|
| 0–6 | S-100 VI0*–VI6* | 0 | S-100 VI7* (J8 pins 8–9) |
| 7 | slave INT (cascade) | 1–3 | Timer 0 / 1 / 2 OUT |
| | | 4 | math chip SVREQ |
| | | 5 | math chip END |
| | | 6 | 2651 TxRDY |
| | | 7 | 2651 RxRDY |

The two controllers are the machine's VI priority encoder, and the master's INT drives pin 73.
VI7* and the on-board sources reach the CPU through the slave and the cascade. With the socket
empty, IR4 and IR5 are inactive.

## How it is simulated

- **Decodes** the 8259A ports `base+0..+3`, the timer ports `base+4..+7` (control at +7
  write-only), the clock ports `base+10` (write-only) and `base+11`, and the UART ports
  `base+12..+15` (status at +13 read-only) as I/O cycles — and the **IntAck** cycle when the master
  would interrupt (or a CALL fetch is in progress). The math ports `base+8/+9` are decoded only
  with a chip in the socket.
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
- **The 2651 UART** (`src/chips/sig2651.h`) is modelled on the same lines as the 8251. A
  transmit character occupies the line for one frame-time (TxEMT clear), and TxRDY clears only
  when a second character waits behind it. TxRDY and TxEMT read 0 while the command register's
  TxEN bit (D0) is 0, as on a real SS-1. After TxEN is set, TxEMT stays 0 until a character has
  left. A byte written with TxEN at 0 waits in the chip and is sent when TxEN is set. A receive frame is clocked in over that same time
  before RxRDY rises. Its **baud rate comes from Mode Register
  2** (the on-chip generator), so the guest's MR2 write sets the line rate. The four ports remove
  the 8251's data-vs-control ambiguity; the only internal sequencing is the MR1/MR2 pointer. DCD
  and DSR status bits read asserted and PE/OE/FE read 0 — a byte-clean transport has no modem
  control to simulate and no line noise to report. The board owns the UART's clock deadline
  (`refresh()`/`nextEdge()`), the same shape as the SBC-100/200.
- **The 8253 interval timer** (`src/chips/intel8253.h`) is not stepped. Each counter remembers
  the counter-clock tick at which its count was loaded. The current count and OUT level are
  **derived from the ticks since then** (DESIGN.md 7.5). The three counter clocks are the S-100
  bus CLOCK (pin 49), so the board gives the chip the tick count of the bus's pin-49 model
  (`BusClock`, `src/core/busclock.h`). The count is 2 MHz of real time at every `clock_hz`. With
  a crystal set, one tick takes `cpu_hz / 2_000_000` T-states. Flat out, one tick is 500 ns of
  host time. Modes 0, 2, 3
  and 4 have their real OUT behaviour and their real count read-back. In mode 3 the count goes
  down by 2 on each tick and loads N again at each half period. This was measured on a real
  SS-1 (`reference/Intel 8253.md` §5). Modes 1 and 5 are gate-triggered and the
  board ties every gate high, so they idle (no gate edge ever arrives). The Counter Latch Command
  and BCD counting are modelled. Its OUT lines now feed the slave 8259A (see below).
- **The dual 8259A** (`src/chips/intel8259a.h`) are two controllers in the standard master/slave
  cascade. Each is a **pure function of its programmed state and the live IR input levels** — the
  card hands them the current levels (a timer OUT off the clock, a VI line off the bus, the UART's
  ready flags) on every query, so `assertsInt()` stays const and combinational while its inputs
  move on other chips' clocks (the same stance as the 88-VI). The master watches VI0–6, IR7 is the
  slave, and its INT output is the card's pin-73 driver; the slave takes VI7 (IR0), the timer
  OUTs (IR1–3) and the UART's TxRDY/RxRDY (IR6/IR7). The model covers ICW1–4 init, OCW1/2/3, fully-nested priority
  with rotation, the EOI family, edge/level triggering, and the acknowledge — which drives the
  **MCS-80/85 3-byte CALL** the CPU executes. The card owns a Clock deadline (`refresh()`/
  `nextEdge()`) that folds in each **unmasked** timer OUT's next edge, so a terminal count wakes
  the card to raise the interrupt with nobody polling.
- **No `PHANTOM*` interrupt-acknowledge trick, and that is a finding.** On real hardware an
  8080/Z80 issues one INTA pulse and reads the CALL's two address bytes from memory, so the board
  must assert `PHANTOM*` to blank memory during them. This simulator's CPU cores instead issue an
  INTA cycle for *every* injected byte (`cpu8080`/`cpu8085`/`cpuZ80` route each fetch to
  `Bus::intAck()` while taking an interrupt), so the CALL bytes come from the board, never from
  memory — there is nothing for `PHANTOM*` to suppress, and modelling it would be a behaviour with
  no observable effect (DESIGN.md 4.0). The card simply keeps claiming the IntAck cycle until it
  has driven all three CALL bytes.
- **The math chip** (`src/chips/am9511a.h`, `src/chips/am9512.h`). The bus side both chips share
  is `Am95xx` (`src/chips/am95xx.h`): the two ports, the 16-byte stack ring (push LS byte first,
  pop MS byte first), BUSY, END, SVREQ, ERR, reset and the snapshot. Each chip supplies its own
  command set. A command is worked out when it is written, and the result goes onto the stack
  at once. BUSY lasts for the command's cycle count, in pin-49 ticks (`BusClock`), and END,
  SVREQ and the status follow the end of BUSY.
  - **Cycle counts:** the data sheets give most counts as a range. The model takes the top of
    each range, which is the time a program that waits with a delay loop had to allow. For the
    9512, a command that its timing table does not list takes the count of the table's
    example.
  - **Wait states:** the board holds READY for 2 wait states on every math access (the SS-1
    manual: PAUSE comes too late to catch the first one). A data access or a command write
    while BUSY holds READY until the command ends, as PAUSE does. A status read never waits.
  - **The wiring** (schematic p.98): the chip's CLK is pin 49 (the stock J5 strap). Its RESET is
    the board's CLEAR (pRESET*, POC*, SLAVE CLR*). SVACK* and EACK* are tied to +5 V, so END
    clears only on a chip access or a reset, and SVREQ clears only when a command with SR = 0
    ends. J6 is taken as set to match the chip (9511A END is active low, 9512 END is active
    high), so END reaches slave IR5 active high. SVREQ goes to slave IR4.
  - **9511A arithmetic:** the model follows measurements of two real chips (#908,
    `reference/AMD 9511A.md` §6.1). Fixed point is two's complement. With the most negative
    value as an operand, SMUL and DMUL return the low half of the product and set overflow.
    The four float operations truncate, with no guard bits. FMUL is a shift-and-add, and FDIV
    gives 23 quotient bits when B's mantissa is smaller than A's. FLTD truncates, and PUPI
    pushes π cut to 24 bits (`02C90FDA`). The derived functions (SQRT … PWR) follow the ch.9
    algorithms of AMD's manual, with the steps that the real chip uses. Each step uses the
    four float operations above. An argument error, such as SQRT of a negative value, stops
    early with the real chip's result and status.
  - **9512 arithmetic:** single (24-bit mantissa, 8-bit exponent) and double (53-bit mantissa,
    11-bit exponent), rounded to nearest even. An exponent field of 0 is zero. The documents
    define no denormals, infinities or NaNs, so there are none. A zero result is all zero
    bits. A divide by zero gives R = B and sets the D bit.
  - **9512 ERR:** comes up at the end of a command that sets D, U or V, and stays up until a
    status read or a reset. The SS-1 sends ERROR only to the J7 header, not to either 8259A,
    so the board does not wire it.
- **Interrupts, the legacy path:** the 2651's RxRDY can still be routed straight to pINT or an
  S-100 VI line by the `serial` unit's `interrupt` jumper (default `none`) for a machine that does
  not program the on-board controllers. Leave it `none` when using the 8259As. **DMA:** none.
- **`properties()`:** `base` (the 16-port block base, default 50H), `math` (`none`, `9511a` or
  `9512`, default `none`), and read-only `clock`, `timer`, `pic` and `mathchip` strings. `pic`
  shows each controller's IRR/ISR/IMR and the master's INT pin. `mathchip` shows busy or idle,
  the stack pointer, the status and the top of the stack, and END, SVREQ and ERR when they are
  up. The
  `serial` **unit** carries `baud`, `interrupt` and `connect`.

### Reset

- `Reset::PowerOn` (POC*, cold): clears the command latches and puts the UART, the 8253 and both
  8259As into their power-on state (the 8259As come up **all-masked**, so an unprogrammed board
  drives no interrupt). `offset_` is **preserved** — the MSM5832 is battery-backed, so a fresh set
  survives a power cycle just as it survives RESET.
- `Reset::Bus` (RESET*, warm): clears the command latches, keeps the time. The 8259As have no
  bus-reset pin (as the 8253 does not), so they keep their programming across a front-panel RESET.
- The math chip's RESET is the board's CLEAR, so both resets reset it: status 0, the stack
  pointer to its start, END, SVREQ and ERR off. The stack bytes are kept (AMD: "may be
  affected"; Intel: "not affected"). A reset is the only way to realign a stack that a guest
  left misaligned.

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
| 8253 counter clock is 2 MHz of real time at every `clock_hz`, flat out too, not the CPU clock | A timing loop calibrated against the timer runs at the wrong rate on any machine whose CPU is not 2 MHz. Flat out, a timer interrupt, such as an MP/M tick from counter 0, comes as fast as the host runs the guest |
| 8253 control word for a counter stops it until a fresh count is loaded | A program that rewrites the mode and expects the old count to keep running reads a stale OUT |
| 8253 a count of 0 means the full modulus (65536, or 10000 in BCD) | A "divide by 65536" that loads 0 undercounts to nothing if 0 is taken literally |
| 8259A master IR0–6 = the S-100 VI lines; the master's INT drives pin 73 | An SS-1 that ignored the VI lines could not act as the machine's interrupt controller for other cards; one that did not drive pin 73 would never interrupt the CPU |
| 8259A cascade: a slave source vectors through master IR7, and BOTH chips latch an in-service bit | A handler that EOIs only one chip leaves the other's priority stuck, blocking that level (and, on the master, everything below IR7) forever |
| 8259A acknowledge drives an MCS-80/85 3-byte CALL (`CD`, addr-lo, addr-hi) | A model that drove a single RST byte would send the CPU to the wrong vector — the SS-1 uses CALL, not RST |
| Math chip stack: a 16-byte ring, pushed LS byte first and popped MS byte first | A guest's operands come back byte-swapped, or a misaligned stack realigns itself where the real chip stays misaligned until reset |
| Math chip: a status read clears END, and does not wait while BUSY | An END interrupt handler that reads the status returns to a second IR5 interrupt, or a guest that polls BUSY stalls in its first status read |
| Math chip: 2 wait states on every access, plus PAUSE while BUSY | A delay loop that does math accesses runs faster than on a real board |
| 9512: an exponent field of 0 is zero, whatever the mantissa holds | CHSS, PTOS and the Z bit treat a zero with a stray mantissa as a number |
| 8259A comes up all-masked until ICW1/OCW1 program it | An unprogrammed controller that let the timer or UART through would fire spurious interrupts on a machine that uses the SS-1 only for its clock |

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
  SS-1 uses normal async only.
- **8253 gate inputs are tied high (ungated).** The SS-1 pulls every gate high, so counting is
  always enabled and the gate-triggered one-shot modes (1 and 5) never trigger — no gate edge is
  modelled. The terminal-count timing
  folds the load clock into the write instant rather than modelling the extra load cycle.
- **The 8253 counts only the bus CLOCK.** The external clock input on J4, and a counter cascaded
  into another, are not modelled. Every counter counts pin 49.
- **8259A: the standard shunt only.** The master/slave source assignments are wired by
  construction (the factory J7/J8 shunt); the dip-header rewiring the manual allows (any source to
  any input) is not modelled. **Special fully-nested mode** (ICW4 D4) is accepted but treated as
  ordinary fully-nested — it only changes how a master handles two interrupts from the *same*
  slave, which the single-slave cascade never does. **Automatic EOI** (ICW4 D1) and **poll mode**
  (OCW3 P) are not modelled; the manual's own sample program uses neither, and Intel advises
  against auto-EOI in a cascade. In **edge-triggered** mode a VI-line edge is sensed at the card's
  next poll rather than the instant it moves (level-triggered, the SS-1 default, is exact).
  An edge request lasts only while its line stays high. A line that falls before the acknowledge
  removes the request, as on the real chip. An acknowledge with no request gives the **default
  IR7**: a CALL to the slave's IR7 address, with no ISR bit set on the slave. The board claims
  the INTA cycle only while the master drives INT, so the default IR7 does not occur from the bus.
- **Do not put an SS-1 (with its 8259As programmed) and an 88-VI in the same machine.** Both watch
  the VI lines and both claim the IntAck cycle, which is genuine bus contention — the contention
  detector reports it. A machine picks one interrupt controller.
- The optional 4K RAM/EPROM block is not modelled.
- ⚠ **9512 overflow and underflow results are not documented.** The model keeps the rounded
  mantissa, wraps the exponent field (by 256 or 2048) and sets V or U. That is what the 9511A
  data sheet says its own float operations do. No real 9512 was available to compare against.
- **9511A: some results differ from a real chip in the last bits.** The model gives the real
  status and result in 507 of 550 measured cases. In 35 cases of SIN, COS, ATAN, ASIN, ACOS, EXP
  and PWR, the result is 1 or 2 LSB from the real one. In 8 SIN and COS cases, the real chip
  keeps one more bit of the reduced argument, and no rule for that bit is known. SDIV and DDIV
  of the most negative value by a negative value give a real result that fits no known rule.
  The model returns the low bits of the quotient there (`reference/AMD 9511A.md` §6.1).
- **Cycle counts are the top of each range.** A real chip often ends sooner, for example an
  FADD of two close operands. A guest that polls BUSY or waits for END sees the longer time.
- **The scratch stack cells** that a command leaves "undefined" keep what was there. An
  undefined command byte does nothing and takes 4 cycles.
- **J6 is always correct for the chip,** and the J7 ERROR header is not modelled.

## Verification

`tests/test_ss1.cpp` drives the MSM5832 through real bus cycles: reads match the host wall clock
at power-on, a programmed set reads back exactly (including Feb 29 in a leap year), the seconds
are forced to 0 on a set, the Hours-10 mode bit and Days-10 leap bit are correct, the set time
survives RESET/power-on and a SNAPSHOT/RESTORE round-trip, and the `base` strap moves the block.
The 2651 is pinned both as a bare chip and over the bus: the MR1-then-MR2 pointer, the full MR2
baud table, the status polarity, TxRDY as a deadline, TxRDY and TxEMT gated by TxEN with the
status values of a real SS-1, a byte held while TxEN is 0, a character clocked in over a frame
time, the RxEN gate, snapshot of the programmed frame/baud, the four-port decode, an end-to-end
receive/transmit through the bus, and the RxRDY interrupt jumper. The 8253 is pinned as a bare
chip and over the bus: mode 0 interrupt-on-terminal-count, the Counter Latch Command freezing a
16-bit read, the mode 2 rate generator and mode 3 square wave (even and odd counts), the mode 3 read-back
against latched reads of a real 8253 (N = 1000, 1001 and 1003), `nextEdge`,
BCD counting, the 2 MHz counter clock being independent of the CPU clock, the counters keeping
real time flat out (a mode 0 terminal count interrupts through the slave 8259A after host time,
not T-states, reaches it), a snapshot round-trip,
the four-port decode, and programming/reading a counter over the bus. The 8259As are pinned as
bare chips and over the bus: the ICW init sequence and MCS-80/85 CALL address, priority/masking
and fully-nested in-service blocking, the EOI variants and priority rotation, edge- vs
level-triggered request sensing, the 8086 single vector byte, and the master/slave cascade. On the
bus: a timer terminal count and a UART RxRDY each interrupt through the cascade (returning the
slave's CALL vector over three INTA cycles), the master prioritizes an S-100 VI line (returning
the master's vector), VI7 reaches the CPU through slave IR0 and the cascade, slave IR6 waits for
TxEN, an interrupt **actually vectors a running CPU** to the CALL target, the board
never asserts `PHANTOM*`, and a snapshot carries the 8259A registers and in-service state.

`tests/test_am9511a.cpp` and `tests/test_am9512.cpp` pin the two math chips as bare chips:
every command against the data sheets' worked examples, the number formats, rounding (for the
9512: ties, the carry into a new exponent, and 32,000 random results compared with host IEEE
arithmetic), 550 results measured on real 9511A chips (status and result, with the cases that
still differ listed), the error bits, the stack ring and its byte order, the cycle counts, BUSY, END,
SVREQ, ERR and a snapshot. On the bus, `tests/test_ss1.cpp` checks the empty socket floats, the
2 wait states and PAUSE, END on slave IR5 vectoring an interrupt, reset, the snapshot, and the
SS-1 manual's 9512 test (1.0 + 1.0 = 2.0). `acceptance-ss1-math` boots CP/M 2.2N on the Disk 1
machine, types in the SS-1 manual's 9511A test program, assembles it with ASM and LOAD, and runs
it: it prints `OK` with a 9511A and `ERROR` with the empty socket.

## References

- `reference/CompuPro System Support 1.md` — the distilled board reference.
- `reference/OKI MSM5832.md` — the clock chip.
- `reference/Signetics 2651 USART.md` — the UART chip.
- `reference/Intel 8253.md` — the interval timer chip.
- `reference/Intel 8259A.md` — the interrupt-controller chip.
- `reference/AMD 9511A.md` — the 9511A math chip.
- `reference/AMD 9512.md` — the 9512 math chip.
- Issue #393 — the math socket.
- Issue #392 — the board's scope and the IEEE-696 finding.
