# CompuPro System Support 1

Source: [System Support 1 Technical Manual.pdf](#) (© CompuPro 1981, Godbout Electronics,
Hayward CA; Document #11620, Board No. 162 Rev G; First printing July 1981, latest printing
December 1983)

The **CompuPro System Support 1** ("SS-1") is a Godbout/CompuPro multifunction S-100 board —
the company later traded as CompuPro, hence the "CompuPro" branding on the manual cover even
though the board number and copyright are Godbout's. It is sometimes miscalled the "Cromemco
System Support 1" in casual references; it has no connection to Cromemco. In one 16-port I/O
block plus an optional 4K memory block it packs: **two Intel 8259A** interrupt controllers
(master/slave, 15 sources), an **Intel 8253** interval timer (three independent 16-bit
counters), a **Signetics/National 2651** UART serial channel with full RS-232 handshaking, an
**OKI MSM5832** battery-backed real-time clock/calendar, a socket for an **AMD 9511A or 9512**
math coprocessor (register-compatible with the Intel 8231A/8232, whose datasheets the manual
reprints), and 4K of RAM/EPROM (two 2716-footprint sockets, one battery-backable in CMOS).

This is a distilled emulation reference: it keeps the programmer-visible port map, register
layouts, and status/control bit polarities for every function, plus the board-specific wiring
that differs from the plain Intel/OKI/AMD datasheet (the interrupt source assignments, the
math-chip END/ERROR jumper, the clock's wait-state-injecting handshake, and the 8080/Z80
PHANTOM* interrupt-acknowledge trick). The generic Intel 8259A/8253/8231A/8232 chip theory the
manual reprints wholesale from Intel application notes and datasheets is summarized to what an
emulator needs; the full mode/command semantics live in the chips' own behavior, not in
anything CompuPro added. Switch/jumper hardware-configuration mechanics (which physical DIP
position sets which address bit) are omitted — only the resulting port/vector/priority model
matters to software.

---

## 1. Port map and strapping

The board occupies one **16-port I/O block**, addressable on any 16-port boundary (DIP switch
3, positions 5–8), and optionally a **4K memory block** on any 4K boundary in any of the 256
64K "pages" (extended addressing) — the memory is unrelated to the I/O map and is not covered
further here. The table below gives the **relative port offsets**, fixed regardless of base,
and the actual address when strapped to CompuPro's documented standard base, **50H** (p.19):

| Offset | Function | Address at base 50H |
|:---:|---|:---:|
| +0 | Master 8259A lower port (A0=0) | 50H |
| +1 | Master 8259A upper port (A0=1) | 51H |
| +2 | Slave 8259A lower port (A0=0) | 52H |
| +3 | Slave 8259A upper port (A0=1) | 53H |
| +4 | Timer/Counter 0 | 54H |
| +5 | Timer/Counter 1 | 55H |
| +6 | Timer/Counter 2 | 56H |
| +7 | Timer/Counter Control Register | 57H |
| +8 | 9511A/9512 Data Port | 58H |
| +9 | 9511A/9512 Command Port | 59H |
| +10 | Clock/Calendar Command Port | 5AH |
| +11 | Clock/Calendar Data Port | 5BH |
| +12 | 2651 Data Register | 5CH |
| +13 | 2651 Status Register | 5DH |
| +14 | 2651 Mode Registers (1 then 2) | 5EH |
| +15 | 2651 Command Register | 5FH |

**No factory-default base is claimed beyond the documented CompuPro convention of 50H** — the
manual says all "software provided by CompuPro and other vendors will assume" that block, so
an emulator's default should match it, but the base is a hardware strap (p.7) like any other
S-100 board. A0 (bit 0 of the offset) is decoded at the board level for the paired-register
chips (8259A, timer, math chip, clock); the UART's four ports are direct addresses, not an
A0-split pair.

---

## 2. Serial channel — 2651 USART

The serial channel is a **2651-type UART, from National Semiconductor or Signetics** (parts
list: U5, marked "2651 or 2661-3", p.95). It provides full RS-232 handshaking (RTS, CTS, DSR,
DTR, DCD), an internal baud-rate generator, and can act as either a master (e.g. wired to a
modem) or a slave (wired to a terminal/printer) device via a jumper block (J2) — a hardware
wiring detail with no software effect.

### 2.1 Register map (p.21)

| Port | Read | Write |
|:---:|---|---|
| Base+12 (`5C`) | Data — received data word | Data — word to transmit |
| Base+13 (`5D`) | Status register | *not used* |
| Base+14 (`5E`) | Mode registers (current mode) | Mode registers (write 1, then 2) |
| Base+15 (`5F`) | Command register (current command) | Command register |

The two mode registers share one port address; the UART has internal sequencing that routes
the **first** write after reset (or after a command with a mode-reset side effect) to Mode
Register 1, and the next write to Mode Register 2. **Mode Register 1 must always be written
before Mode Register 2** (p.22).

### 2.2 Status register — Base+13, read (p.22)

| Bit | Flag | Meaning | Polarity |
|:---:|------|---------|----------|
| 0 | **TxRDY** | Transmitter ready for a new character | **active-high** (1 = ready) |
| 1 | **RxRDY** | A received character is waiting | **active-high** (1 = available) |
| 2 | TxEMT/DSCHG | DCD or DSR changed, **or** the transmitter shift register is empty | active-high; "unless you really need this... just ignore this bit" |
| 3 | PE | Parity error | active-high |
| 4 | Overrun | A character arrived before the previous one was read | active-high |
| 5 | FE | Framing error (no stop bit found) | active-high |
| 6 | Data Carrier Detect | **High means the DCD line is LOW** (asserted) | status bit active-high represents an **active-low** signal line |
| 7 | Data Set Ready | **High means the DSR line is LOW** (asserted) | status bit active-high represents an **active-low** signal line |

Bits 6 and 7 are the one subtlety: unlike TxRDY/RxRDY/PE/OE/FE (where the status bit's sense
matches the condition it names), the DCD and DSR status bits are **inverting reporters of
active-low RS-232 lines** — bit set (1) means the modem-control input is asserted (driven
low), bit clear (0) means it's deasserted (high/idle). For a byte-clean transport with no
modem-control simulation, model bits 6/7 as asserted (1) and PE/OE/FE as 0.

### 2.3 Mode Register 1 — write (p.22–23)

| Bits | Field | Values |
|:---:|---|---|
| 1,0 | Mode/baud factor | Must be `10` (bit1=1, bit0=0) for 16× async — the only mode this board supports |
| 3,2 | Character length | `00`=5, `01`=6, `10`=7, `11`=8 bits |
| 4 | Parity control | 0 = no parity, 1 = parity generated |
| 5 | Parity type | 0 = odd, 1 = even (ignored if bit 4 = 0) |
| 7,6 | Stop bits | `01`=1, `10`=1½, `11`=2 (`00` invalid) |

### 2.4 Mode Register 2 — write (p.24)

| Bits 3–0 | Baud rate | Bits 3–0 | Baud rate |
|:---:|:---:|:---:|:---:|
| 0000 | 50 | 1000 | 1800 |
| 0001 | 75 | 1001 | 2000 |
| 0010 | 110 | 1010 | 2400 |
| 0011 | 134.5 | 1011 | 3600 |
| 0100 | 150 | 1100 | 4800 |
| 0101 | 300 | 1101 | 7200 |
| 0110 | 600 | 1110 | 9600 |
| 0111 | 1200 | 1111 | 19200 |

Bits 7–4 must always be written as `0111` (bit7=0, bit6=1, bit5=1, bit4=1) "for proper UART
operation in the System Support 1" — this is a board convention, not a general 2651
requirement.

### 2.5 Command register — Base+15, read/write (p.25)

| Bit | Name | 1 | 0 |
|:---:|------|---|---|
| 0 | Transmit Control | transmitter enabled | transmitter disabled |
| 1 | Data Terminal Ready | DTR output forced **low** (asserted) | DTR forced high |
| 2 | Receive Control | receiver enabled | receiver disabled |
| 3 | Force Break | break condition forced (TxD held marking→spacing per break def.) | normal |
| 4 | Reset Error | error flags in status register reset | normal operation |
| 5 | Request To Send | RTS output forced **low** (asserted) | RTS forced high |
| 6,7 | *(unused)* | must be 0 | — |

Note bits 1 and 5 follow the same inverted-active-low convention as status bits 6/7: setting
the command bit **high** drives the modem-control **output low** (asserted).

### 2.6 Initialization sequence (p.26)

1. Write Mode Register 1.
2. Write Mode Register 2.
3. Write Command Register.
4. Begin normal operation (poll status or enable interrupts).

The manual's own sample init for "9600 baud, 8 data bits, 2 stop bits, no parity, RTS low, DTR
low" writes `MODE1=0xEE`, `MODE2=0x7E`, `CMND=0x27` (p.26) — cross-checked bit-by-bit against
the tables above, it matches exactly.

### 2.7 Clock and interrupts

The UART's internal baud generator is driven from an on-board **5.0688 MHz** crystal
oscillator (X2), independent of the S-100 bus clock. TxRDY and RxRDY (inverted) feed the
on-board interrupt controllers for interrupt-driven operation (§4).

---

## 3. Interval timer — Intel 8253

Three independent 16-bit down-counters (p.62–69, reprinted 8253 datasheet). Each has a clock
input, gate input, and output, all normally wired to a 2 MHz source (S-100 bus pin 49) but
brought out at jumper J4 for external clocking or cascading sections (p.14–15, p.90).

### 3.1 Port map

| Port | Function |
|:---:|---|
| Base+4 | Counter 0 (load/read) |
| Base+5 | Counter 1 (load/read) |
| Base+6 | Counter 2 (load/read) |
| Base+7 | Control Word Register (write-only) |

### 3.2 Control word format (write, Base+7)

| D7 D6 | D5 D4 | D3 D2 D1 | D0 |
|---|---|---|---|
| SC1 SC0 — select counter (`00`=0,`01`=1,`10`=2, `11` illegal) | RL1 RL0 — read/load order (`00`=latch,`10`=MSB only,`01`=LSB only,`11`=LSB then MSB) | M2 M1 M0 — mode 0–5 | BCD (0=binary 16-bit, 1=BCD) |

### 3.3 Modes

| Mode | Name | Behavior |
|:---:|---|---|
| 0 | Interrupt on Terminal Count | Output low after load, goes high and stays high at terminal count. **This is the mode the System Support 1's interrupt wiring is designed for** (p.62). |
| 1 | Programmable One-Shot | Output goes low on the gate's rising edge, high at terminal count; retriggerable. |
| 2 | Rate Generator | Periodic low pulse, one clock wide, every N counts. |
| 3 | Square Wave Generator | 50% duty square wave (N/2 high, N/2 low; odd N skews by one count). |
| 4 | Software Triggered Strobe | One-clock-wide low pulse at terminal count after load. |
| 5 | Hardware Triggered Strobe | Like mode 4 but started by the gate's rising edge; retriggerable. |

Each counter's outputs and clock/gate lines all appear at jumper block J4 (pins 1–3 inverted
outputs, 4–6 clock inputs tied to 2 MHz by default, 7–8 gate inputs, 9 = timer-2 gate). Gate
inputs are pulled up (not gated off) unless jumpered otherwise. Timer outputs also feed the
interrupt jumper matrix (J7/J8, §4).

---

## 4. Interrupt controllers — dual Intel 8259A

Two Intel (or NEC) **8259A** Programmable Interrupt Controllers in a **master/slave cascade**
(p.38, p.89–90), giving 15 usable interrupt sources: the master's 7 lower inputs are wired to
7 of the 8 S-100 bus vectored interrupts (VI0*–VI6*), the master's IR7 is wired to the slave's
INT output, and the slave's 8 inputs are all **on-board** sources. This is the standard
"factory" jumper configuration (a dip-shunt at J8, J7 left open); a dip-header instead of the
shunt lets every source/destination be rewired arbitrarily (p.13–14).

### 4.1 Port map

| Port | Function |
|:---:|---|
| Base+0 | Master 8259A, A0=0 (ICW1 / OCW2 / OCW3, depending on write sequence state) |
| Base+1 | Master 8259A, A0=1 (ICW2–4 / OCW1) |
| Base+2 | Slave 8259A, A0=0 |
| Base+3 | Slave 8259A, A0=1 |

### 4.2 Standard interrupt source assignment (p.13)

| Master IRQ | Source | Slave IRQ | Source |
|:---:|---|:---:|---|
| 0–6 | S-100 VI0*–VI6* | 0–3 | Timer 0 OUT, Timer 1 OUT, Timer 2 OUT (IRQ1–3) |
| 7 | Slave INT output | 4 | 9511/9512 SVRQ |
| | | 5 | 9511/9512 END |
| | | 6 | 2651 TxRDY |
| | | 7 | 2651 RxRDY |

(Slave IRQ0 is unassigned in the standard wiring — see the jumper diagram, p.13, which shows
`|---|IRQ0` with no source line feeding it.)

### 4.3 Chip programming

The 8259A's full mode/command semantics (fully-nested mode, EOI variants — non-specific,
specific, automatic; rotation; special mask mode; edge vs. level triggering; polling; cascade
setup via ICW1–4/OCW1–3) are the standard Intel 8259A model, reprinted verbatim from Intel's
AP-59 (pp.40–59) — not repeated here; see `reference/`'s own future 8259A datasheet extract if
one exists, or any 8259A reference. What the System Support 1 fixes rather than leaves
programmable:

- **Master is hardwired as master** (`SP/EN` pulled high via R21); **slave is hardwired as
  slave** (`SP/EN` tied low). The three cascade lines are wired between the two chips.
- The manual's own sample init program (p.60–61) programs: ICW1 = `1D`H (cascade mode,
  address interval 4, **level-triggered**, ICW4 needed); ICW2 = `02`H (vector base 200H for
  the master, 220H for the slave); master ICW3 = `80`H (IR7 has a slave); slave ICW3 = `07`H
  (slave ID 7, i.e. wired to the master's IR7); ICW4 = `10`H (8085/8080 mode\*, normal EOI,
  non-buffered, **special fully nested mode**); OCW1 = `00`H (all unmasked); OCW2 = `A0`H
  (rotate on non-specific EOI). \* — see §4.4 on the μPM/8080 vs 8086 selection bit, which
  the manual calls "8085 mode" for what ICW4 bit 0 documents as MCS-80/85.
- Intel advises **against** the automatic-EOI mode in a master/slave setup (p.60) — the
  sample program uses normal EOI with rotation instead.
- **Two wait states are automatically inserted on every interrupt-acknowledge cycle**
  regardless of the wait-state switch setting (p.93), to guarantee margin.

### 4.4 The 8080/Z80 PHANTOM* trick (important emulation gotcha)

The 8259A responds to an interrupt acknowledge by placing a 3-byte **CALL** opcode sequence on
the bus over three INTA pulses — the format the 8085 (and 8086/8088, differently) natively
generate. An **8080 or Z80** CPU issues only **one** INTA pulse and then fetches the CALL's
remaining two bytes as ordinary **memory read cycles at the current PC**, not further INTA
cycles. If real system memory answers those two reads, the CPU gets whatever is actually
stored there instead of the 8259A's vector bytes and the interrupt response is corrupted.

The board's fix: a flip-flop (U44a/b) recognizes "status valid + INTA asserted" and asserts
**PHANTOM\*** (S-100 bus pin 67) for the duration of the CALL's own memory-read follow-on
cycles, which must cause **all system memory** to disable itself. **This is why the manual
requires every board of RAM/ROM in the system to be configured to respond to PHANTOM\*** when
using the on-board interrupt controllers with an 8080 or Z80 (pp.3, 12) — an emulator that
models an interrupt-acknowledge sequence for a Z80/8080 system with this board must replicate
the same phantom-memory suppression during the CALL byte fetches, or vector bytes will be read
from the wrong place. Jumper **J13** selects which INTA behavior the board expects: pins "8"–"C"
shorted for 8085/8088/8086 CPUs (no phantom trick needed — one INTA cycle apiece), pins
"Z"–"C" shorted for Z-80/8080 CPUs (needs the phantom trick above, keyed off the S-100 sW0*
write-strobe signal that follows the CALL's implicit stack push).

---

## 5. Real-time clock — OKI MSM5832

A CMOS BCD clock/calendar chip, crystal-timed (32.768 kHz, X3) and battery-backable, giving
hours/minutes/seconds/day-of-week/day/month/year with no CPU polling overhead (p.27, p.92).

### 5.1 Command register — Base+10, write (p.27)

| Bit | Name | Meaning |
|:---:|------|---------|
| 7 | *(unused)* | — |
| 6 | **Hold** | 1 = clock counters inhibited (required for all writes; may optionally be set for reads). **Do not leave set for more than one second** or the time will drift. |
| 5 | **Write** | 1 = write the data register's value into the selected digit |
| 4 | **Read** | 1 = the data register reflects the selected digit |
| 3–0 | **Digit select** | selects which BCD digit (table below) |

### 5.2 Digit select (p.27–28)

| Bits 3210 | Digit | Bits 3210 | Digit |
|:---:|---|:---:|---|
| 0000 | Seconds 1 | 0111 | Days 1 |
| 0001 | Seconds 10 | 1000 | Days 10 # |
| 0010 | Minutes 1 | 1001 | Months 1 |
| 0011 | Minutes 10 | 1010 | Months 10 |
| 0100 | Hours 1 | 1011 | Years 1 |
| 0101 | Hours 10 * | 1100 | Years 10 |
| 0110 | Day of Week | | |

`*` Hours-10 also carries the 12/24-hour mode bit and AM/PM. `#` Days-10 also carries the
leap-year (28/29 days in February) bit.

### 5.3 Data register — Base+11 (p.28)

A BCD digit (0–9) occupies the low nibble; the upper nibble reads as 0 (write value is don't
care). For the Hours-10 and Days-10 digits, **only the low two bits (bit1, bit0) carry the
digit value** — the digit itself never exceeds 2 (hours) or 3 (days), so two bits suffice, and
the **upper two bits of the low nibble carry a mode flag instead** (p.28, "The lower two bits
… are the only ones that convey any digit information … the next two bits are used to convey
other kinds of information"):

- **Hours-10 digit**: bit1,bit0 = the tens digit (0–2). **bit2 = AM(0)/PM(1)** in 12-hour
  mode; **bit3 = 12-hour(0)/24-hour(1) mode select**.
- **Days-10 digit**: bit1,bit0 = the tens digit (0–3). **bit2 = leap-year flag** — 1 selects
  29 days in February, 0 selects 28.
- **The seconds digits are write-ignored, not read-as-zero**: reads return the running
  seconds like any other digit; only *writes* to Seconds-1/Seconds-10 are discarded and the
  digit forced to 0 — "Both seconds digits are not settable to anything but zeroes … an
  idiosyncrasy of the MSM 5832 clock chip" (p.28). So setting the clock always zeroes seconds.

### 5.4 Programming sequences (p.29–30)

**Write** (per digit): (1) write `0x40` (hold=1) to command; (2) write digit-select command
(hold=1, read/write=0); (3) write the BCD digit to the data register; (4) write digit-select
command with hold=1 **and** write=1; (5) write digit-select command with hold=1, write=0
again; repeat for remaining digits; (6) write `0x00` to command to release hold and restart
counting.

**Read** (per digit): (1) write digit-select command with read=1 (hold/write optionally set
for glitch-free reads, but keeping hold set longer than 1 second stops the clock); (2) read
the data register; repeat; (3) write `0x00` to command when done.

### 5.5 Board-level handshake (p.92, Theory of Operation)

Because the MSM5832 is a slow CMOS part, the command and data lines are latched (U40/U42) and
the board **automatically inserts wait states**: 6 µs whenever the command register is
written, and 150 µs whenever the Hold bit is set — this frees software from needing explicit
wait loops around clock accesses. The clock's own chip-select is held (and the clock
battery-switched) by a power-good comparator (Q3/Q5) independent of the CPU.

---

## 6. Interrupt controllers/timer/clock: chip select interaction

Not a separate function, but worth stating once: **the memory on this board is always
disabled during any I/O cycle** (an AND-gated condition in the address decoder, p.87–88) and
**is unconditionally disabled during interrupt-acknowledge cycles regardless of the PHD/PHE
switch settings** (p.89) — the board's own PHANTOM\* generation (§4.4) overrides the
user-selected phantom-response policy for its own memory during INTA.

---

## 7. Math coprocessor socket — AMD 9511A/9512 (register-compatible with Intel 8231A/8232)

An **empty socket**, not populated on the standard board (p.5, p.70) — the user supplies either
an AMD 9511A (fixed+floating point, single precision) or AMD 9512 (floating point, single and
double precision); these are pin/register compatible with Intel's 8231A and 8232 respectively,
whose datasheets the manual reprints in full (pp.73–86) as the only available programming
reference. **The two chips are not software-compatible with each other** in their number
formats/command sets (p.70) — an emulator must pick one model to implement per configuration.

### 7.1 Port map (p.70)

| Port | Function |
|:---:|---|
| Base+8 | Data port (push/pop the operand stack; A0=0) |
| Base+9 | Command port (write a command; read status; A0=1) |

This matches the Intel chips' own `A0`-encoded read/write shape (p.74, p.80): `A0=0,WR=0` push
data; `A0=0,RD=0` pop data; `A0=1,WR=0` issue command; `A0=1,RD=0` read status.

### 7.2 Stack discipline (important emulation trap)

Both chips maintain an internal LIFO operand stack with **no realignment instruction** — if a
calculation is fed too few or too many operand bytes, or if too few/many result bytes are
read, the stack becomes misaligned and **the only documented recovery is a full chip reset**
(p.70): "The quickest and surest way to re-align the math processor stack is to reset the
system." An emulator implementing one of these chips must track byte-count discipline
precisely per command (single precision = 4 bytes/operand, double precision = 8 bytes, per
the 8231A/8232 datasheets, pp.76, 82) since there is no other recovery path.

### 7.3 Board wiring specifics (p.91–92)

- Standard clock is the 2 MHz S-100 signal (bus pin 49); an on-board crystal oscillator (X1,
  divided by 2) can drive higher-speed 3 MHz (AMD) or 4 MHz (Intel) parts instead — a hardware
  strap (J5), not a software-visible difference.
- The chip's **PAUSE** output (called READY in the Intel datasheets) forces CPU wait states
  when the math chip isn't ready; **the System Support 1 always inserts 2 wait states on every
  math-chip access regardless**, because PAUSE arrives too late to reliably gate the very
  first wait state, and lets PAUSE extend the wait further if still asserted (p.93).
- **END** and **SVRQ** (service request) feed the interrupt controller (slave IRQ4/5, §4.2)
  for interrupt-driven math operation.
- **The END signal's polarity differs between the 9511A and the 9512** — jumper **J6** must be
  set to the "A" block for a 9511A or the "B" block for a 9512 (only needed if running
  interrupt-driven).
- **ERROR is only available on the 9512** (8232 equivalent) — the 9511A has no such output.
  Unlike END, the ERROR line is only brought out to jumper **J7**, not to J8, so it cannot be
  routed to the slave 8259A in the standard configuration — only to an S-100 vectored
  interrupt via the master.

---

## Emulation checklist

- **Port map** (base 50H standard, any 16-port boundary strapped): 8259A master `+0/+1`, slave
  `+2/+3`; 8253 timer `+4/+5/+6` counters, `+7` control; math chip `+8` data / `+9` command;
  MSM5832 clock `+10` command / `+11` data; 2651 UART `+12` data / `+13` status / `+14` mode ×2
  / `+15` command.
- **2651 UART**: TxRDY = status D0, RxRDY = D1, **both active-high**. DCD/DSR status bits (D6/D7)
  and the DTR/RTS command bits (D1/D5) are **inverting** — bit set means the RS-232 line is
  driven low (asserted). Mode Register 1 must be written before Mode Register 2. Baud table is
  4-bit one-of-sixteen (50–19200), not one-hot.
- **8253 timer**: standard Intel 8253, three counters + control word at `+4..+7`. Board wiring
  favors **Mode 0 (Interrupt on Terminal Count)** for interrupt use; clock/gate default to
  2 MHz, cut-and-jumper (J4) to cascade or externally clock.
- **Dual 8259A**: standard cascade, master IR0–6 = S-100 VI0*–VI6*, master IR7 = slave INT,
  slave IR0–7 = Timer0/1/2 OUT, math-chip SVRQ, math-chip END, 2651 TxRDY, 2651 RxRDY (slave
  IRQ0 unassigned in the stock jumpering) — but this is a dip-shunt convention, fully
  rewireable via dip-headers at J7/J8. **2 wait states auto-inserted on every INTA cycle.**
  **8080/Z80 systems must disable all system RAM/ROM during the CALL-opcode follow-on memory
  reads of an interrupt acknowledge (PHANTOM\*)** — 8085/8088/8086 systems don't need this
  (selected by jumper J13).
- **MSM5832 clock**: command register bits Hold(6)/Write(5)/Read(4)/digit-select(3–0); BCD
  digits in the data register's low nibble; **seconds digits are write-ignored (forced to 0),
  but reads return the running seconds**; hours-10 and days-10 carry the digit in bits 1:0 and
  a mode flag in bits 3:2 (H10: bit2=PM, bit3=24-hour; D10: bit2=leap-year). Board auto-inserts
  6 µs (command write) / 150 µs (Hold set) wait states — model as "instant" unless
  timing-accurate wait-state injection matters.
- **Math chip socket**: empty by default; if modeled, pick 9511A (Intel 8231A registers) or
  9512 (Intel 8232 registers) — **not mutually compatible**. Stack-alignment errors are
  unrecoverable except by reset. END polarity (J6) and ERROR-only-on-9512 are board-level
  differences to expose as configuration, not auto-detect.
- **No manual erratum was found** in this document — the port map (p.19), the per-function
  descriptions, and the sample initialization/test programs (UART, clock, interrupts, math
  chip) all cross-check consistently bit-for-bit against each other.
