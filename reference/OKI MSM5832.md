# OKI MSM5832

Source: [CompuPro System Support 1 Manual.pdf](#) (the OKI MSM5832 register model as
reprinted and documented in the System Support 1 manual, pp.27–30; provenance in
`docs/sources.md`). The standalone OKI datasheet (electricals, timing, package) is on
bitsavers if the AC/DC characteristics are ever needed; only the programmer-visible model is
kept here.

The **OKI MSM5832** is a CMOS **microprocessor real-time clock/calendar** — a battery-backable
chip, crystal-timed at 32.768 kHz, that keeps seconds/minutes/hours/day-of-week/day/month/year
in BCD and hands them to a CPU through a tiny latched interface. It appears on the
[CompuPro System Support 1](CompuPro%20System%20Support%201.md) and on many other S-100 and
micro boards; this is a distilled emulation reference for the register model, not the
electricals.

The chip presents **13 addressable 4-bit registers** (one BCD digit each) plus a handful of
control lines. A host board wires those control lines to two I/O ports — a **command** port
(which digit, and read/write/hold) and a **data** port (the 4-bit digit itself). The digit
data always appears in the **low nibble**; the upper nibble reads as 0.

## Address / digit map

The command register's low 4 bits select which digit the data port reads or writes:

| Sel (bits 3-0) | Digit | Range | Sel | Digit | Range |
|:--:|---|:--:|:--:|---|:--:|
| 0 | Seconds 1 | 0–9 | 7 | Days 1 | 0–9 |
| 1 | Seconds 10 | 0–5 | 8 | Days 10 `#` | 0–3 |
| 2 | Minutes 1 | 0–9 | 9 | Months 1 | 0–9 |
| 3 | Minutes 10 | 0–5 | 10 | Months 10 | 0–1 |
| 4 | Hours 1 | 0–9 | 11 | Years 1 | 0–9 |
| 5 | Hours 10 `*` | 0–2 | 12 | Years 10 | 0–9 |
| 6 | Day of Week | 0–6 | | | |

`*` Hours-10 also carries the AM/PM and 12/24-hour bits. `#` Days-10 also carries the
leap-year bit. See **Data register**.

## Command register (the board's command port)

| Bit | Name | Meaning |
|:--:|---|---|
| 7 | — | unused |
| 6 | **Hold** | 1 = counters inhibited. Required for all writes; optional for glitch-free reads. **Held > 1 second and the time drifts** (the counters are stopped). |
| 5 | **Write** | 1 = write the data register into the selected digit |
| 4 | **Read** | 1 = the data register reflects the selected digit |
| 3–0 | **Digit select** | which digit (table above) |

## Data register (the board's data port)

A BCD digit (0–9) in the low nibble; the upper nibble reads 0 and is don't-care on writes.
**Three digits are special:**

- **Hours-10 (digit 5):** bits 1:0 = the tens digit (0–2). **bit2 = AM(0)/PM(1)** in 12-hour
  mode; **bit3 = 12-hour(0)/24-hour(1)** mode select. Only the low two bits carry the digit.
- **Days-10 (digit 8):** bits 1:0 = the tens digit (0–3). **bit2 = leap-year** (1 = 29 days in
  February, 0 = 28).
- **Seconds (digits 0, 1):** **read the running seconds like any digit, but writes are
  ignored** — "Both seconds digits are not settable to anything but zeroes … an idiosyncrasy of
  the MSM5832" (manual p.28). Setting the clock therefore always zeroes the seconds.

## Programming sequences

**Read a digit:** write a command with Read=1 and the digit select (Hold optionally 1 for a
glitch-free read); read the data port; repeat; write 0 to release Hold when done.

**Write a digit (set the clock):** (1) raise Hold; (2) select the digit (Write=0); (3) write
the BCD value to the data port; (4) re-issue the select with Write=1 to strobe it in; (5)
re-issue the select with Write=0; repeat for each digit; (6) drop Hold to resume counting. Mode
Register discipline does not apply — this is a clock, not a UART.

## Emulation notes

- The chip has **no oscillator to model in emulated time**: back it with the host's wall clock
  plus a signed second offset that moves only when the guest sets it, and it keeps perfect time
  with no scheduled events. (This is how `src/chips/msm5832.{h,cpp}` does it.)
- Setting the clock composes the 13 digits back into a host `time_t` (e.g. `mktime`); the
  two-digit year needs a century supplied from elsewhere (the host's current century is a safe
  default).
- The day-of-week register is an independent 0–6 counter on real hardware; deriving it from the
  set date (rather than trusting a written weekday) avoids an inconsistent calendar.
- A host board latches command/data and inserts wait states (on the System Support 1, ~6 µs on a
  command write and ~150 µs while Hold is set); those are safely modelled as instant unless a
  program times them, which period software does not.
