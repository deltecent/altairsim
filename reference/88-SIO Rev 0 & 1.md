# MITS 88-SIO Serial I/O Board (Rev 0 & 1)

Source: [88-SIO Rev 0 & 1.pdf](#)

MITS, Inc. Serial I/O Board documentation (© 1975). The 88-SIO is the Altair
8800's parallel-to-serial interface, built around a single UART (IC M, a
COM2502 / AY-5-1013-class device). It provides one serial channel to the CPU
via two consecutive I/O port addresses, with programmable framing, jumper-set
baud rate, jumper-set port address, and both software-polled and hardware
(vectored) interrupt operation.

This file captures everything relevant to *emulating* the board. Assembly steps,
parts lists, resistor color codes, PC-land cut/jumper mechanics, and warranty
text from the PDF are intentionally omitted.

---

## 1. Board variants (A / B / C) — electrical interface only

The three suffixes describe only the line-level interface; the digital/CPU-side
logic is identical across them. For a software emulator these differ only in the
external signal levels, not in register behavior.

| Variant | Interface | Notes |
|---------|-----------|-------|
| 88-SIO **A** | RS-232 (±12 V) | Standard RS-232 level board. On-board logic levels: logic high ≈ **+3 V**, logic low ≈ **−12 V**. Output signals SBIN/SBOT/STSO are TTL internally and level-shifted; input signals SRSI/SROT/SRIN shifted from RS-232 to TTL. |
| 88-SIO **B** | TTL | Signals pass through **non-inverting** buffers (IC U). Input needs .5 mA worst case; output drives up to 20 TTL loads (48 mA). |
| 88-SIO **C** | TTY (20 mA current loop) | TTL signals **inverted** through IC U / IC V. "BIN" inverted, drives Q1 current switch; "SRSI" buffered+inverted then re-inverted so SRSI and RSI track. This is the Teletype board (a factory mod raises the TTY output-contact voltage). |

For an emulator the byte-level data path is the same for all three; you generally
model variant A/B behavior (clean serial bytes).

---

## 2. I/O port model — the board occupies TWO consecutive ports

The board is assigned a **single even base address**, jumper-selectable to any
even number **000–376 octal** (see §7). Address line **A0** then splits it into
two channels:

| A0 | Channel | Address parity | Purpose |
|----|---------|----------------|---------|
| 0 (low) | **Control / Status channel** | even (= base) | `IN` reads the status word; `OUT` writes the interrupt-control word |
| 1 (high) | **Data channel** | odd (= base + 1) | `IN` reads the received byte; `OUT` transmits a byte |

> Rule from the manual: **the control channel is always the even address; the
> data channel is always the odd address.** A0 low enables the control channel
> (IC J pin 4 high); A0 high enables the data channel (IC J pin 1 high).

### Device select logic
The 8 low-order address lines A0–A7 feed the on-board select logic (ICs H & J).
When A1–A7 match the jumpered address, IC I pin 8 goes low (board selected). A0
picks the channel as above.

### The four operations
| Instruction | Address | Action |
|-------------|---------|--------|
| `IN`  base (even)   | Control | Read **status word** onto the data-in bus (SINP gates it) |
| `OUT` base (even)   | Control | Write **interrupt-enable control word** (bits D0/D1) |
| `IN`  base+1 (odd)  | Data    | Read received byte; clears Data-Available / UART RDAV |
| `OUT` base+1 (odd)  | Data    | Load byte to UART transmit register; clears "buffer empty" (sets busy), starts serial shift-out |

---

## 3. Status word (read: `IN` from the control/even port)

**IMPORTANT — there are TWO status-word definitions.** The board shipped with an
"original" definition, and an errata + hardware-interrupt modification changes it
to the "modified" definition. The **modified definition is the one that matches
standard MITS software** (monitors, BASIC) and is the one an emulator should
present by default. Flag polarity is inverted on the two *ready* bits (active
**low** = ready).

### 3a. Modified status word (errata / hardware-interrupt mod — USE THIS)
Applies once the internal hardware-interrupt modification is implemented (that
mod applies to Rev 0 boards; see §8).

| Bit | Mask | Logic LOW (0) means | Logic HIGH (1) means |
|-----|------|---------------------|----------------------|
| 7 | 0x80 | **Output Device Ready** (transmitter buffer empty — OK to send) | Not Ready |
| 6 | 0x40 | Not Used | Not Used |
| 5 | 0x20 | Not Used | Not Used |
| 4 | 0x10 | — | Data Overflow |
| 3 | 0x08 | — | Framing Error |
| 2 | 0x04 | — | Parity Error |
| 1 | 0x02 | Not Used | Not Used |
| 0 | 0x01 | **Input Device Ready** (a received byte is available for the computer) | Not Ready (no data) |

Emulator summary: **bit 0 = 0 → RX byte available**; **bit 7 = 0 → TX ready**.
Both are active-low. Error bits 2/3/4 are active-high.

### 3b. Original status word (pre-mod, from Theory of Operation)
| Bit | Mask | Logic LOW (0) means | Logic HIGH (1) means |
|-----|------|---------------------|----------------------|
| 7 | 0x80 | Output device Ready (device sent a ready pulse); **also raises a hardware interrupt if interrupts enabled** | Not Ready |
| 6 | 0x40 | Not Used | Not Used |
| 5 | 0x20 | — | Data Available (a word is in the I/O-board buffer) |
| 4 | 0x10 | — | Data Overflow (new word received before previous was read to accumulator) |
| 3 | 0x08 | — | Framing Error (no valid stop bit) |
| 2 | 0x04 | — | Parity Error (received parity ≠ selected parity) |
| 1 | 0x02 | — | X-mitter Buffer Empty (previous word sent; new word may be output) |
| 0 | 0x01 | Input device Ready (device sent a ready pulse) | — |

Note how the mod relocates "transmitter buffer empty" from bit 1 to bit 7 (as
the meaning of "Output Device Ready") and "data available" from bit 5 to bit 0
(as "Input Device Ready"), giving the familiar bit0/bit7 status layout.

---

## 4. Interrupt-control word (write: `OUT` to the control/even port)

An `OUT` to the control channel latches bits **D0** and **D1** into the
input/output interrupt flip-flops (IC B, gated through ICs E & A). Only these two
bits matter; D2–D7 are don't-care.

| D0 | D1 | Output-device interrupt | Input-device interrupt |
|----|----|-------------------------|------------------------|
| low (0)  | low (0)  | disabled | disabled |
| low (0)  | high (1) | enabled  | disabled |
| high (1) | low (0)  | disabled | enabled  |
| high (1) | high (1) | enabled  | enabled  |

So: **D0 = input(receiver) interrupt enable, D1 = output(transmitter) interrupt
enable.**

Example (manual): to enable the input device and disable the output device
interrupts, load the accumulator with `xxxxxx01` (D1=0, D0=1) and `OUT` to the
control-channel address.

---

## 5. Data channel operation (odd port)

**Transmit — `OUT` base+1:** the CPU places the byte on the data-out bus; the
board strobes it into the UART transmit register (TDS / Transmit Data Strobe,
IC M pin 23), which then shifts it out serially on STSO. The output-ready
flip-flop is reset (busy asserted to the device / "buffer empty" cleared) until
transmission completes.

**Receive — `IN` base+1:** a received serial byte on SRSI is assembled by the
UART; the byte is placed on the data-in bus (RDE / Received Data Enable), the CPU
strobes it into the accumulator during DBIN, and the input-ready flip-flop (IC F)
plus the UART **RDAV** flag (IC M pin 18) are reset.

Emulation model: transmit consumes the accumulator byte and (after the baud
delay) clears the TX-busy/sets TX-ready; receive returns the buffered byte and
clears the RX-available flag.

---

## 6. UART framing configuration (IC M)

The UART's frame format is set by hard-wire pads jumpered to GND or +V (five
optional pads next to IC M). These are *hardware* frame settings, not runtime
registers.

| UART pin | Name | Function |
|----------|------|----------|
| 35 | **NPB** | Tied **high** → parity bit is eliminated (no parity transmitted). See POE (pin 39). |
| 36 | **NSB** | **low → 1 stop bit**; **high → 2 stop bits**. |
| 37 | **NDB2** | Data-bits select (with NDB1) — see table. |
| 38 | **NDB1** | Data-bits select (with NDB2) — see table. |
| 39 | **POE** | If NPB is low, selects parity odd/even — see table. |

**Data bits per character:**
| NDB2 | NDB1 | # bits |
|------|------|--------|
| low  | low  | 5 |
| low  | high | 6 |
| high | low  | 7 |
| high | high | 8 |

**Parity:**
| POE | NPB | Parity |
|-----|-----|--------|
| low  | low  | odd |
| high | low  | even |
| X    | high | none (no parity) |

Pad-to-rail mapping used in the hardwire step (equivalent, from the assembly errata):

| Pad | to GND | to +V |
|-----|--------|-------|
| NSB | 1 stop bit | 2 stop bits |
| NDB1 / NDB2 | see below | |
| NPB / POE | odd (both GND) / even (NPB GND, POE +V) / none (NPB +V) | |

NDB1/NDB2 → data bits: GND/GND = 5, +V/GND = 6, GND/+V = 7, +V/+V = 8
(order NDB1, NDB2).

---

## 7. Baud-rate generation and selection

Both receiver and transmitter need a clock at **16× the baud rate**. This clock
is produced by a **12-bit presettable counter** (ICs P, Q, R — 74191s) plus a
single-shot (IC O). The preset value determines the divide ratio.

- **Maximum clock frequency:** 400 KHz → **maximum baud = 400K / 16 = 25,000 baud.**
- Overall board range stated as 0 through 25,000 baud.

**Preset count formula** (with the errata correction — the Theory-of-Operation
printed "4096", which is **wrong; use 4100**):

```
Preset Count = 4100 − ( Period of Output Frequency (µs) / 0.5 µs )
```

### Baud-rate selection chart (ERRATA version — use this, not the manual's original)
12-bit preset count, bit 11 (MSB) → bit 0 (LSB):

| Baud | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|------|----|----|---|---|---|---|---|---|---|---|---|---|
| 110   | 1 | 0 | 1 | 1 | 1 | 0 | 0 | 1 | 0 | 1 | 0 | 0 |
| 150   | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 1 |
| 300   | 1 | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | 1 | 1 |
| 600   | 1 | 1 | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 | 0 | 0 |
| 1200  | 1 | 1 | 1 | 1 | 1 | 0 | 0 | 1 | 1 | 1 | 0 | 0 |
| 2400  | 1 | 1 | 1 | 1 | 1 | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| 4800  | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 0 | 1 | 0 | 1 | 0 |
| 9600  | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 0 | 0 | 0 |
| 19200 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 0 |

---

## 8. Address selection (port jumpers)

The base (even) address is set by wiring seven jumper pads **I1–I7** to the true
or complemented address lines **A1–A7**. The board provides both polarities of
each address line (An and An̄). For each address bit:

- To decode a **1** in that bit position, jumper the I-pad to the **true** line `An`.
- To decode a **0** in that bit position, jumper the I-pad to the **complement** line `An̄` (shown barred in the chart).

A0 is not jumpered — it selects control vs. data channel (§2). The full I/O
Address Selection Chart in the PDF lists every even octal address 000–376 with
its I1–I7 wiring. Representative rows (bar = complemented line):

| Octal | I7 | I6 | I5 | I4 | I3 | I2 | I1 |
|-------|----|----|----|----|----|----|----|
| 000 | Ā7 | Ā6 | Ā5 | Ā4 | Ā3 | Ā2 | Ā1 |
| 002 | Ā7 | Ā6 | Ā5 | Ā4 | Ā3 | Ā2 | A1 |
| 004 | Ā7 | Ā6 | Ā5 | Ā4 | Ā3 | A2 | Ā1 |
| 006 | Ā7 | Ā6 | Ā5 | Ā4 | Ā3 | A2 | A1 |
| 010 | Ā7 | Ā6 | Ā5 | Ā4 | A3 | Ā2 | Ā1 |
| … | | | | | | | |
| 200 | A7 | Ā6 | Ā5 | Ā4 | Ā3 | Ā2 | Ā1 |
| … | | | | | | | |
| 370 | A7 | A6 | A5 | A4 | A3 | Ā2 | Ā1 |

(The pattern is simply the binary of the octal value across A7…A1, with A0=0 for
the control channel. Any even address is reachable; e.g. the common console
address 000, or 020, etc.)

---

## 9. Hardware / vectored interrupts

The board has hardware interrupt capability, optional and independent of polled
operation.

- **Interrupt source pads:** three pads labeled **OUT** (output device), **IN**
  (input device), **BH** (both). These represent which condition asserts the
  interrupt. An interrupt on bit-7 (output ready) occurs if enabled by the
  control word (§4).
- **Vectored mode (with 88-VI board):** eight pads labeled **VI 0–7** are the 8
  vectored-interrupt priority lines to the 88-VI Vectored Interrupt Board. **0 =
  lowest priority, 7 = highest.** Jumper OUT / IN / BH each to a desired VI level.
  Input and output devices can be given different priorities, or a single
  priority via BH (in which case OUT and IN pads are unused).
- **Single-level mode (no 88-VI):** jumper **one** of OUT/IN/BH to the small
  unlabeled processor-interrupt pad (to the right of pad "I", near IC H). Only
  one I/O board in the system may use this, and only one of the three pads.
  On interrupt the processor **jumps to location 070 octal** — place the ISR at
  **070–077 octal**.

---

## 10. External device signals (10-pin wafer connector)

Board-edge signals available at the 10-pin connector / cable:

| Signal | Direction | Meaning | Polarity / timing |
|--------|-----------|---------|-------------------|
| **STSO** | out | Serial Data Output | serial TxD |
| **SRSI** | in  | Serial Data Input | serial RxD |
| **SBIN** | out | Busy to input device | **active low**; goes active immediately from the READY pulse and stays active until the CPU services the device |
| **SBOT** | out | Busy to output device | **active low**; same behavior as SBIN |
| **SRIN** | in  | Ready from input device | **active high**; ready pulse must be **> 100 ns and < 1 µs** |
| **SROT** | in  | Ready from output device | **active high**; same pulse spec |
| **GND** | — | Ground | — |
| **+5 V** | — | +5 V | — |
| **SPARE** | — | unused pad(s) | — |

On-board signal levels (variant A): logic high ≈ **+3 V**, logic low ≈ **−12 V**.

Connector pin order (from the wiring drawing, female connector):
SPARE, +5V, GND, SROT, SRIN, SRSI, STSO, SBOT, SBIN (10-pin, one spare).

---

## 11. Rev 0 vs. Rev 1 differences

The document covers both revisions; the concrete differences it documents are:

- **Internal hardware-interrupt modification** (for devices with no external
  "handshake"): explicitly **"applies to Revision 0 boards only."** It involves
  cutting PC lands and adding jumpers around ICs M, F, C, T so the transmitter
  "buffer empty" drives the interrupt internally. On Rev 1 this capability is
  present without the field mod.
- **Modified status-word definition (§3a)** is the result of that modification.
  A Rev 1 board (or a modified Rev 0) presents the bit0/bit7 status layout that
  standard MITS software expects; an unmodified Rev 0 presents the original
  layout (§3b).
- **Errata common to both:** baud "4096" → **4100** in the preset formula;
  replace the manual's baud-rate chart with the errata chart (§7); the TTY (C)
  board's output-contact voltage is raised (factory installed); resistor
  R4/R25 value changes (assembly-only, not emulation-relevant).

Beyond these, the CPU-side register semantics, port model, UART framing, and
baud generation are identical between Rev 0 and Rev 1.

---

## 12. Emulator quick-reference

- **Ports:** even base = control/status, odd (base+1) = data. Base jumperable to
  any even 000–376 octal.
- **Status read (even, `IN`):** bit 0 = 0 → RX byte ready; bit 7 = 0 → TX ready
  (both active-low). Bits 2/3/4 = parity/framing/overflow errors (active-high).
  Use the §3a modified layout by default.
- **Control write (even, `OUT`):** D0 = RX-interrupt enable, D1 = TX-interrupt
  enable.
- **Data read (odd, `IN`):** returns received byte, clears RX-ready.
- **Data write (odd, `OUT`):** transmits byte, clears TX-ready until sent.
- **Framing/baud** are static hardware settings (jumpers), not runtime-writable.
- **Interrupt vector:** single-level mode → RST to **070 octal**.
