# SciTronics RTC-100 Real-Time Clock

**Status:** done — the clock and the once-a-second interrupt are modeled. The 6821 is
modeled only as far as the clock needs it; see **Limitations**.

Board type: `rtc100`.

## The real hardware

SciTronics Inc. of Bethlehem, PA shipped the RTC-100 in 1980: an S-100 battery-backed
calendar clock for a machine that had none. An **OKI MSM5832** clock/calendar chip (U2),
crystal-timed at 32.768 kHz and kept alive by a 3 V lithium cell, sits behind a **6821
PIA** (U5) which presents it to the bus as four consecutive I/O ports. The card keeps
seconds through years plus day-of-week, and it keeps them while the machine is switched
off.

Two DIP switches configure it:

- **PORT** (SW1) — six poles decoding **A2–A7**. A0 and A1 pick which of the card's four
  functions is addressed, so the base must be a **multiple of 4** (0 … 252).
- **INT** (SW2) — three poles selecting which `RST` the card jams when its interrupt is
  acknowledged. **Negative logic: ON = 0.** The manual advises against all-on (`RST 0`)
  and all-off (`RST 7`) because both are commonly taken by other devices.

SciTronics shipped BASIC programs (`RTCREAD`, `RTCSET`, a North Star pair) and an 8080
driver, `READ.ASM`, meant to be dropped into a boot PROM or a BIOS.

## Sources

| Source | Path | Authority |
|---|---|---|
| RTC-100 User's Manual | `reference/SciTronics RTC-100 Real-Time Clock.md` | Ports, PIA sequences, interrupt vectoring, parts list. **Its p5 prose contradicts its own shipped drivers** — see below. |
| OKI MSM5832 register model | `reference/OKI MSM5832.md` | The 13 BCD digits, the mode bits on Hours-10 and Days-10, write-ignored seconds. |
| `READ.ASM`, Appendix V | `reference/SciTronics RTC-100 Real-Time Clock.md` §9 | The 8080 driver, transcribed in full. **The oracle for the read sequence.** |

> **The manual contradicts itself, and the artifact won.** Page 5's "sequence to enable
> clock read" prints `port 3 = 248 = F8H` and orders the last two writes the other way
> round. Both shipped drivers — `READ.ASM`'s `TSTART` and the North Star `RTCREAD` —
> write **252 (`FCH`)** to `base+3` and *then* 244 to `base+1`, and so does a pencil
> correction in the margin of the scan. The listings ran on the hardware; the paragraph
> did not. `tests/test_rtc100.cpp` drives the listings' sequence.

## Register reference

Four consecutive ports from the base. A0/A1 select the PIA register.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `base+0` | CRA bit 2 set: port A data — **digit address in bits 3–0, digit data in bits 7–4**. CRA bit 2 clear: DDRA (`0FH` = read set-up, `FFH` = write set-up). | CRA bit 2 set: **clock digit in bits 7–4**, the digit code still in bits 3–0. CRA bit 2 clear: DDRA reads back. |
| `base+1` | PIA A control. Bit 2 = data/DDR select; bit 3 = **CA2 level = the clock's Hold** (**low = Hold asserted = clock stopped**). | Reads back. |
| `base+2` | CRB bit 2 set: port B — **bit 0 is the clock's Write strobe**, pulsed low then high per digit. CRB bit 2 clear: DDRB. | Reads back the latch. |
| `base+3` | PIA B control. Bit 3 = CB2 level = the clock's Read line. | Reads back. |

Digit codes are the MSM5832 register numbers: 0 = seconds units, 1 = seconds tens,
2/3 = minutes, 4/5 = hours, 6 = day of week, 7/8 = day, 9/10 = month, 11/12 = year.

**Two digits carry a flag above the digit, and a driver must mask with `AND 3`:**
Hours-10 (5) has the 24-hour/AM-PM bit, Days-10 (8) has the leap-year bit.

## How it is simulated

- **Decodes** the four I/O ports, and the **IntAck** cycle while it is requesting.
- **The clock chip is not this board's code.** `src/chips/msm5832.{h,cpp}` already models
  the MSM5832 for the CompuPro System Support 1 — host wall time plus a guest-set offset,
  battery-backed, snapshotted. This board adds no clock logic; it translates PIA pins into
  that chip's command/data interface (`driveClock()`).
- **The translation**, in one line: Hold = CA2 low, digit select = port A's low nibble,
  data = port A's high nibble, Write = the falling edge of PB0.
- **Interrupt**: once a second of **real time** (issue #827). The second comes from the
  chip's own 32.768 kHz crystal, not from the processor's crystal. A `HostTicker`
  (`src/core/hostticker.h`) keeps the schedule in host time. The board reads host time
  through `setHostNs()`, and a test sets it. The tick gets to the guest in one of two ways:
  - **Paced** (`clock_hz` set): a `Clock` deadline fires the tick at the exact T-state.
    The run loop holds T-states to real time, so one second is `clock_hz` T-states.
  - **Flat out, or late:** `pump()` reads host time once a run slice and sets the request
    when the second is due. Paced, `pump()` does this only when host time is one second or
    more past the schedule.

  The request rides **pin 73** (`assertsInt()`). On acknowledge the card claims the cycle
  and returns `rstOpcode(restart_)`. The acknowledge is the dismissal. No driver writes a
  port to clear it. A second that passes while a request is pending is lost.
- **Properties**: `port` (multiple of 4, default `F0H`), `interrupt` (`none | int`),
  `restart` (0–7), and a live show-only `time`.

### Reset

- `Reset::PowerOn` (POC*, cold): the 6821 comes up cleared, the pending interrupt drops,
  the seconds tick restarts. **The time does not change** — the cell kept it.
- `Reset::Bus` (RESET*, warm): the same. `Msm5832::reset()` preserves the offset by design.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The digit reads back in the **high** nibble with the digit code still in the low one. | Every driver's `ANI 0F0H` + four `RRC` yields the code instead of the time, and the clock reads as a constant. |
| **CA2 low = Hold asserted** (the inversion is easy to reverse). | Hold never rises, the display is never frozen, and a set is never committed. |
| The write strobe commits on the **falling** edge of PB0, once per pulse. | Each digit is written twice, or not at all. |
| Seconds are **write-ignored**, not read-as-zero: a set zeroes them, a read returns them running. | `RTCSET`'s "push CR on the zero second" ritual silently drifts. |
| Hours-10 and Days-10 carry mode bits above the digit. | An unmasked hour reads 8 higher than it is; February 29 looks like day 69. |
| The time survives RESET and power-on. | The lithium cell is decorative, and a guest that reboots loses the date. |

## Limitations and deliberate departures

- **This is not a general 6821.** Modeled: the DDR/data select (control bit 2), the CA2/CB2
  output level (control bit 3), the two direction registers and the two data registers.
  **Not modeled:** CA1/CB1 inputs, the IRQA/IRQB flags and their read-clears, the
  pulse/handshake CA2 output modes, and any input path on port B. Nothing on the card is
  wired to them — the clock's Hold/Read/Write are the only loads — so a guest that programs
  them observes nothing. Software that drove this PIA as a general parallel port would
  notice; no such software exists, because the clock is soldered to it.
- **The CB2 Read line gates nothing.** `chips/msm5832.h` answers a data read with the
  selected digit regardless of the Read strobe, because that is what every real read
  sequence relies on. The pin is modeled so a guest can read it back. A driver that read
  the port *without* raising Read would get a digit here where real silicon would give it
  nothing — no shipped driver does that.
- **The 150 µs and ~6 µs settling delays are not enforced.** A guest that skips them reads
  correctly here; on real hardware it would read a digit that had not settled. The manual's
  own driver implements them with an untimed `INR A` / `JNZ` spin, so there is no exact
  number to enforce even if we wanted to.
- **The century is pinned from the host year** (the chip holds two digits). `READ.ASM`
  hard-codes `19`; we do not.
- **The interrupt follows the host's clock**, so a tick is as accurate as the host's
  monotonic clock. Flat out, a tick lands at the first run slice after it is due. The
  tick is not in step with the second that the chip shows, which counts host wall time.
- **The interrupt runs only when `interrupt = int`.** With `none`, nothing is on the
  `Clock` queue, so a guest that runs `HLT` with nothing else to wait for still ends the run.

## Verification

`tests/test_rtc100.cpp` (`./build/altair_tests rtc100`) drives **real bus cycles** through
`READ.ASM`'s own `TSTART`/`GETDIG1`/`TIMED` byte sequences and asserts:

- the composed 13 digits equal the host wall clock;
- the raw read carries data high / code low;
- a full `RTCSET`-style set reads back exactly, with seconds forced to zero;
- the 24-hour and leap-year flag bits appear where a driver must mask them;
- the time survives `reset()` and `power()`;
- the interrupt fires once per second, the acknowledge yields `RST n`, and the acknowledge
  dismisses it; `interrupt = none` never requests while timekeeping continues;
- the tick keeps real time: flat out, T-states do not move it and host time does (ten ticks
  in ten host seconds); paced, it lands at exactly `clock_hz` T-states; a host that is late
  gets the tick from `pump()`; `interrupt = none` leaves the `Clock` queue empty;
- a base that is not a multiple of 4 is rejected, and moving the base moves all four ports;
- snapshot/restore carries the set time.

## References

- `reference/SciTronics RTC-100 Real-Time Clock.md`
- `reference/OKI MSM5832.md`
- `docs/boards/compupro-ss1.md` — the other card carrying this chip
