# Cromemco TU-ART Digital Interface

Source: [Cromemco TU-ART Digital Interface.pdf](#) (© Cromemco 1978)

The **Cromemco TU-ART** ("Twin Universal Asynchronous Receiver and Transmitter") is an
S-100 board carrying **two** independent I/O channels, each built around a **TMS 5501**
NMOS I/O controller. Each channel provides one full-duplex **serial** port (RS-232 + 20 mA
current loop), one 8-bit parallel port, and five interval timers, with on-board priority
encoding for both Z-80 (mode 2) and 8080 (mode 0) interrupts. The board has its own
crystal clock and interfaces to the S-100 bus asynchronously, so the CPU clock frequency is
not critical.

This is a distilled emulation reference: it keeps the programmer-visible register/port model
of the **serial** interface — what an emulator author needs to make a console channel talk.
The parallel-port timing, the current-loop/RS-232 connector pinouts (Tables 5/6, factory
**answer-mode** wiring), the timer detail and the full interrupt daisy-chain theory are
summarized only where they bear on the serial path.

The two chips are **Device A (IC 4)** and **Device B (IC 5)**. Device A is the leftmost
chip; it drives serial connector J4 and parallel J2. Device B is to its right and drives
serial J5 and parallel J3. The two devices are register-identical; a driver picks one by its
base address. Device A is internally prioritized over Device B.

---

## 1. Chip — TMS 5501

Each channel is a single **TMS 5501** ("Nmos I/O Controller"): a UART **plus** five interval
timers **plus** an eight-source priority interrupt controller on one chip. Only the UART half
is covered here. It is **double-buffered** on both transmit and receive, drivable **polled**
(read the status register) or **interrupt-driven** (RDA → interrupt request 4, TBE →
interrupt request 5). Framing is standard async: baud and 1/2 stop bits are set by the baud
register (below); word length is fixed by the chip at 8 data bits.

---

## 2. Port map

Each device occupies a 16-port block. The **base** (A7–A4) is set by the DIP switch; the low
address nibble **A3–A0** selects the register (Figure 2). *Read* (IN) and *write* (OUT) at the
same offset are usually **different registers** — note especially offset 0 (status on read,
**baud rate** on write) and the fact that the **command** register is a separate write-only
port at **offset 2**, not shared with status.

| Offset (A3–A0) | IN (read) | OUT (write) |
|:---:|---|---|
| **0** | **Status register** | **Baud-rate register** |
| **1** | **Receiver data** (RX) | **Transmitter data** (TX) |
| **2** | *not assigned* | **Command register** |
| **3** | Interrupt-address register | Interrupt-mask register |
| **4** | Parallel input port | Parallel output port |
| **5–9** | *not connected* | Timers 1–5 (OUT 5=T1 … OUT 9=T5) |
| **A–F** | *not connected* | *not connected* |

(Figure 2, p.5; Register Description, pp.6–9. "Bit assignment by PC Board traces.")

**Base address.** Device A base = DIP switch positions 6–3 → A7 A6 A5 A4; Device B base =
positions 10–7 → A7 A6 A5 A4. Switch **on = 0** in that bit. So a base is any multiple of
0x10. **The manual specifies no single factory default** — the examples use different values
(the metronome puts Device A at `0x80`, Device B at `0x50`; the init-subroutine listing uses
Device A `0x00`, Device B `0x50`). A **Caution** (Figure 1, p.2) reserves bases **00, 30H,
40H, 50H** for the Cromemco 4FDC floppy controller, bank-selected memory and the PR1 printer,
so a console channel is strapped clear of those. If both devices share a base, **Device A
overrides**.

---

## 3. Status register — offset 0, IN

Cromemco's software convention (and the default PC-board trace wiring through the "status"
solder socket between IC 8 and IC 9) is:

| Bit | Flag | Meaning | Polarity |
|:---:|------|---------|----------|
| **D7** | **TBE** — Transmitter Buffer Empty | Transmit holding register can take a new byte (double-buffered: rises as the shift-out begins). Also drives interrupt request 5. Cleared when the buffer is loaded; set by RESET. | **active-high** (1 = ready to send) |
| **D6** | **RDA** — Receiver Data Available | A byte is waiting in the receive buffer. Also drives interrupt request 4. Cleared when the data register is read, or by RESET. | **active-high** (1 = char available) |
| D5 | IPG — Interrupt Pending | One or more of the eight interrupt sources is active (mirrors the 5501 INT pin). | active-high |
| D4 | SBD — Start Bit Detect | Receiver saw a start bit; stays high until the full char is in. Test/diagnostic; cleared by RESET. | active-high |
| D3 | FBD — Full Bit Detect | Goes high one full bit-time after the start bit. Test/diagnostic; cleared by RESET. | active-high |
| D2 | SRV — Serial Receive | Reflects the live level on the serial-in line (high = mark/idle). Used for **break detection**. | active-high |
| D1 | ORE — Overrun Error | A new char overwrote one not yet read. Cleared after the status port is read, or by RESET. | active-high |
| D0 | FME — Frame Error | A stop bit was not where expected. Stays high until a valid char is received. | active-high |

(Register Description "0 IN Status Register", p.6; Figure 3, p.9.)

**All status flags are active-high.** This is confirmed by the manual's own polled drivers
(p.25): character-out spins on `IN status / AND 80H / JR Z` (loop **while** TBE=0, i.e. wait
for D7=1), and character-in spins on `IN status / AND 40H / JR Z` (loop while RDA=0, wait for
D6=1). So `TBE = 0x80` (bit 7) and `RDA = 0x40` (bit 6), both **1 = asserted**.

> **Status bits are trace-strapped, not fixed.** The eight flags reach data bits through a
> 16-pin solder socket; a builder *can* re-map them. Every Cromemco software build assumes the
> default above (**D7 = TBE, D6 = RDA**), and an emulation should implement that convention.

---

## 4. Baud-rate register — offset 0, OUT

Writing this register sets the serial baud rate **and** the stop-bit count for both TX and RX.
One-hot in the low seven bits (if several bits are set the **highest** rate wins; if none, the
serial channel is **disabled**):

| Bit | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
|-----|----|----|----|----|----|----|----|----|
| Meaning | STOP | 9600 | 4800 | 2400 | 1200 | 300 | 150 | 110 |

- **D7 STOP:** 1 = one stop bit, 0 = two stop bits.
- The command register's **HBD** bit (below) octuples every rate, giving up to 76.8 k.

(Register Description "0 OUT Baud Rate Register", p.7; Figure 3, p.9.)

---

## 5. Command register — offset 2, OUT

Write-only control byte (D6/D7 unused):

| Bit | Name | Effect |
|:---:|------|--------|
| D0 | **RES** — Reset | 1 = receiver → search mode; TX output → mark; RDA/SBD/FBD/ORE cleared; TBE set; timers cleared; interrupt latch cleared (except TBE). Not latched. (RES overrides BRK.) |
| D1 | **BRK** — Break | 1 holds the TX output spacing (low). 0 for normal operation. |
| D2 | **RS7** — RST7 Select | 1 = parallel-input MSB (PI7) drives the lowest-priority interrupt latch; 0 = Timer 5 drives it. |
| D3 | **INE** — INTA Enable | 1 lets the 5501 answer an Interrupt Acknowledge (gate a Restart onto the bus). Must be **high** for normal interrupt operation. |
| D4 | **HBD** — High Baud | 1 octuples the internal clock: timers 8× faster, serial rates 8× (up to 76.8 k). |
| D5 | **TB5** — Test Bit | 1 disables internal priority logic and exposes the internal clock on INT (test only; keep low). |

(Register Description "2 OUT Command Register", pp.7–8; Figure 3, p.9. A common reset/enable
value is `9` = INE + RES.)

---

## 6. Data registers — offset 1

- **IN (RX):** the assembled byte from the serial receiver. Reading it clears RDA.
- **OUT (TX):** loads the transmitter holding register. Writing it clears TBE.

(Register Description, p.7.)

---

## 7. Interrupts (brief — not emulated here)

Each device is a full eight-source priority interrupt controller (Timers 1–3, SENS, RDA, TBE,
Timer 4, Timer 5/PI7). Two devices chain into 16 sources. The board supports two CPU
interrupt models, selected by DIP switch position 1:

- **Z-80 mode 2** (switch off): the board gates a vector byte onto the bus during INTA; the
  high nibble is set by Device A address bits A7–A5 (Table 1, p.10).
- **8080 mode 0** (switch on): the board gates one of eight `RST` opcodes during INTA; Device B
  is chained through Device A's SENS input (Table 2, p.11).

The **interrupt-mask register** (offset 3, OUT) is `[T5/PI7 | T4 | TBE | RDA | T3 | SENS | T2 |
T1]` — a 1 enables that source. The **interrupt-address register** (offset 3, IN) returns the
encoded RST vector of the highest-priority pending source (`E7H` = RDA, `EFH` = TBE …), or
`FFH` when none is pending; reading it clears that source's request latch. **An emulator that
implements only the serial console can ignore all of this and poll the status register** — RDA
(D6) and TBE (D7) carry everything a polled driver needs.

---

## 8. Emulation checklist

- **Chip = TMS 5501, one per channel.** Two channels: Device A (IC 4, base = switches 6–3) and
  Device B (IC 5, base = switches 10–7). Device A wins on a base tie.
- **Serial console needs four ports:** status `IN base+0`, RX `IN base+1`, TX `OUT base+1`,
  command `OUT base+2`; baud is `OUT base+0`. Status-read and baud-write share offset 0;
  **command is its own port at offset 2** — *not* the 8251/6850 "one address, read vs write"
  shape.
- **Status is active-high, all bits.** For a byte-clean transport, model ORE/FME/SBD/FBD as
  0, SRV as 1 (line idle/mark). Drive **RDA = D6 (0x40)** when a byte is waiting and **TBE =
  D7 (0x80)** whenever the transmitter can take a byte. That reproduces the manual's polled
  console drivers exactly.
- **Baud register one-hot**, D7 = one/two stop bits; HBD (command D4) ×8. `0` disables serial.
- **No factory-default base.** Pick one clear of the reserved `00/30H/40H/50H`; the manual's
  examples use A=`0x80`/`0x00`, B=`0x50`.

**Manual discrepancy (erratum).** The *Initialization Subroutine* listing (p.24) equates the
baud-rate port to `ABASE+3` (`ABDR EQU ABASE+3`), colliding with the interrupt-mask port at
the same offset. The authoritative Register Description, Figure 2 and Figure 3 all place the
**baud-rate register at offset 0 (OUT)**. Trust offset 0; the example EQU is wrong.
