# MITS 88-PIO — Parallel I/O Board

Source: [MITS 88-PIO.pdf](#)

MITS, Inc., June 1975. The 88-PIO interfaces any 8-bit parallel device to the
Altair 8800. It is built from discrete TTL (a pair of 8212 8-bit latches plus
74Lxx gating), *not* from a PIA — so unlike the later [[MITS 88-4PIO]] there are
no software-programmable data-direction or control registers. One 8212 buffers
the byte going out, the other buffers the byte coming in; the direction of each
port is fixed by the hardware. This document extracts what is needed to emulate
the board; assembly, parts lists, and the schematic are omitted.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| Ports occupied | **2 consecutive** — even = control/status, odd = data |
| Base address | Jumper-selected to any **even** address 000–376 octal (128 boards) |
| Channel select | **A0 = 0 → control channel**, **A0 = 1 → data channel** |
| Data width | 8 bits, latched each direction (8212 IC G = out, IC H = in) |
| Status bits (IN control) | **DI0 = output device ready**, **DI1 = input device has data** |
| Interrupt-enable bits (OUT control) | **DO0 = enable output-device interrupt**, **DO1 = enable input-device interrupt** |
| Interrupt | Optional: vectored via [[88-VI-RTC]], or single-level (RST 7 → 0x38) |
| Handshake | External `SBO`/`SBI` strobes latch data; `INT` (`BO`/`BIN`) signals ready back to the device |

The board is two ports. The **even** address is the control/status channel; the
**odd** address is the data channel. This is the same even-Status / odd-Data
split used by the [[88-C700 Centronics Printer Controller]].

---

## 2. Card and channel select

**Card select.** Address lines A1–A7 (and their inversions) are jumpered through
pads I1–I7 per the Address Selection Chart (§5). The board responds only when all
seven selected lines are logic 1 at once. A0 is *not* part of the card decode — it
selects the channel, so the board always occupies an even/odd pair.

**Channel select.**

| A0 | Address | Channel |
|----|---------|---------|
| 0 | even (base) | Control / status |
| 1 | odd (base+1) | Data |

---

## 3. The two channels

### Control channel (even address)

Used to read device status and to arm the interrupt-enable circuitry.

**`IN` (read status).** Two status bits are placed on the bus:

| Bit | Meaning when high |
|-----|-------------------|
| DI0 | Output device is ready to **receive** new data from the computer |
| DI1 | Input device has **sent** data; the computer may now access it |

**`OUT` (write interrupt enable).** Two data bits arm the two device interrupts:

| Bit | Meaning when high |
|-----|-------------------|
| DO0 | Output-device interrupt enabled |
| DO1 | Input-device interrupt enabled |

If the board is strapped with the **"BOTH"** interrupt jumper, either bit being
high will cause an interrupt.

### Data channel (odd address)

- **`IN`** gates the input latch (8212 IC H) onto the bus as DI0–DI7.
- **`OUT`** latches the bus DO0–DO7 into the output latch (8212 IC G).

---

## 4. Handshake and interrupt

**Handshake.** Each 8212 latch (G output, H input) contains an internal flip-flop
that is set by its strobe input: `SBO` for the output device, `SBI` for the input
device. When a device pulses its strobe, that latch's `INT` output goes low,
signifying "ready" for that direction. These two `INT` signals are buffered back
out to the external devices on lines `BO` (output) and `BIN` (input) as
additional handshake.

**Interrupt (optional; the board runs fine polled).**

- **Vectored.** Jumper the `OUT`, `IN`, or `BOTH` interrupt pad to one of the
  eight `VI0`–`VI7` pads, which wire to the [[88-VI-RTC]] vectored-interrupt
  board. The output and input devices may each be given a different priority, or
  share one (via `BOTH`).

  > The 88-PIO manual labels these pads "0 lowest … 7 highest". Note this is the
  > *opposite* of the 88-VI board's own convention, where **VI0 is the highest**
  > priority (see [[88-VI-RTC]]). The line a pad is wired to is what determines
  > priority; the 88-VI decides the ordering. Trust [[88-VI-RTC]] for which VI
  > level outranks which.

- **Single-level (no 88-VI in the system).** Jumper the board's `INT` pad to
  **one** of the `OUT`/`IN`/`BOTH` pads. The interrupt then drives the CPU's INT
  line directly: on acknowledge the 8080 executes **RST 7**, jumping to location
  **70 octal (0x38)**. The service routine lives at 70–77 octal. Only **one** I/O
  board in the system may use this method, and only one of the three pads.

---

## 5. Address Selection Chart

Base addresses are even, 000 through 376 octal. Each row jumpers pads I1–I7 to
either the true or the complemented address line (a bar denotes the complement),
so that the selected combination is all-ones only at that base address. The board
then also answers base+1 (the odd data channel).

| Base (octal) | I7 | I6 | I5 | I4 | I3 | I2 | I1 |
|--------------|----|----|----|----|----|----|----|
| 000 | A̅7 | A̅6 | A̅5 | A̅4 | A̅3 | A̅2 | A̅1 |
| 002 | A̅7 | A̅6 | A̅5 | A̅4 | A̅3 | A̅2 | A1 |
| … | | | | | | | |
| 374 | A7 | A6 | A5 | A4 | A3 | A2 | A̅1 |
| 376 | A7 | A6 | A5 | A4 | A3 | A2 | A1 |

The pattern is a straight binary count on A1–A7: a true line where that address
bit is 1, a complemented line where it is 0. (The full 128-row chart is in the
scan; the rule above reproduces every row.)
