# MITS 88-4PIO — 4-Port Parallel Input/Output Board

Source: [MITS 88-4PIO.pdf](#)

MITS, Inc., ©1975 (Third Printing, March 1977). The 88-4PIO is built around the
**Motorola 6820 PIA** (Peripheral Interface Adapter). Because the 6820 holds all
of its own control and data registers, nearly every option is *software*-selectable
at run time — data direction (each of the 8 data lines can be an input or an
output) and the interrupt/control structure. The board can be populated with up to
**four 6820s ("PORTS")**, ICs J, K, L, M. This is the fully programmable cousin of
the fixed-direction [[MITS 88-PIO]]. This document extracts what is needed to
emulate the board; assembly, parts lists, and the schematic are omitted.

The same 4PIO port model underlies the [[88-HDSK]] "Datakeeper" controller.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| Device | Motorola **6820 PIA**, up to 4 per board |
| Addresses per board | **16** (4 per port × 4 ports) |
| Board select | Jumper A4–A7 (and complements) → one of 16 boards |
| Port select | **A3,A2 → port**: 00 = J, 01 = K, 10 = L, 11 = M |
| Section / register | **A1,A0**: 00 = A control, 01 = A data, 10 = B control, 11 = B data |
| Registers per section | 3 — control/status, data-direction (DDR), data — DDR/data shared, selected by control-reg bit 2 |
| DDR bit | 0 = line is **input**, 1 = line is **output** |
| Power-on reset (POC) | All registers cleared → all data lines inputs, C2 lines inputs |
| Interrupt | Optional: vectored via [[88-VI-RTC]], or single-level (RST → 0x38) |

---

## 2. Addressing

A board occupies 16 consecutive addresses. Within them:

**Port (which 6820), from A3 A2:**

| A3 | A2 | Port (IC) |
|----|----|-----------|
| 0 | 0 | J |
| 0 | 1 | K |
| 1 | 0 | L |
| 1 | 1 | M |

**Section and register within a port, from A1 A0:**

| A1 | A0 | Section | Register |
|----|----|---------|----------|
| 0 | 0 | A | Control / status |
| 0 | 1 | A | Data (or DDR — see §3) |
| 1 | 0 | B | Control / status |
| 1 | 1 | B | Data (or DDR — see §3) |

Each of the four ports is one 6820, and each 6820 has two independent **sections**
A and B. A section has an 8-bit data path, two peripheral control lines (C1 input,
C2 in/out), and an interrupt-request output.

**Board select** (which of 16 boards) is jumpered on A4–A7 per the Address
Selection Chart in §6. Base addresses fall on 16-address (20-octal) boundaries.

**Wait state.** On an **input** operation the board inserts a ~500 ns CPU wait
(PRDY pulled low) to allow address setup to the ports. Output operations do not
wait. An `E` (enable) strobe — PDBIN ORed with PWR, one pulse per machine cycle —
gates every register read/write.

---

## 3. The control/status register

Each section's control/status register is **read/write**. Layout:

| Bit | 7 | 6 | 5 4 3 | 2 | 1 0 |
|-----|---|---|-------|---|-----|
| Function | IRQ status (C1) | IRQ status (C2) | C2 control | DDR control | C1 control |

- **Bits 7 and 6** are status only — *unaffected by a write*. Bit 7 reflects the
  C1 control-line activity; bit 6 reflects C2. Bit 7 (and the `IRQ` output) is
  **reset when the data register is read** by the CPU.
- **Bit 2 selects DDR vs data** at the section's data address: bit 2 = 0 → the
  **data-direction register (DDR)** is accessed; bit 2 = 1 → the **data register**
  is accessed. Writing 0 to a DDR bit makes that line an input; writing 1 makes it
  an output.

### C1 (peripheral input control line), from control bits 1,0

| Bit1 | Bit0 | C1 active edge | Status bit 7 | `IRQ` output |
|------|------|----------------|--------------|--------------|
| 0 | 0 | Active low | Set high when C1 active | Disabled — stays high |
| 0 | 1 | Active low | Set high when C1 active | Goes low when bit 7 goes high |
| 1 | 0 | Active high | Set high when C1 active | Disabled — stays high |
| 1 | 1 | Active high | Set high when C1 active | Goes low when bit 7 goes high |

So bit 1 chooses the active edge and bit 0 enables the interrupt.

### C2 as input, from control bits 5,4,3 (bit 5 = 0)

| Bit5 | Bit4 | Bit3 | C2 active edge | Status bit 6 | `IRQ` output |
|------|------|------|----------------|--------------|--------------|
| 0 | 0 | 0 | Active low | Set high when C2 active | Disabled — stays high |
| 0 | 0 | 1 | Active low | Set high when C2 active | Goes low when bit 6 goes high |
| 0 | 1 | 0 | Active high | Set high when C2 active | Disabled — stays high |
| 0 | 1 | 1 | Active high | Set high when C2 active | Goes low when bit 6 goes high |

Sections A and B behave identically when C2 is an input.

### C2 as output, from control bits 5,4,3 (bit 5 = 1)

C2 as an output behaves **differently for section A (CA2) and section B (CB2)** —
A is a read-strobe/handshake, B is a write-strobe/handshake.

**Section A — CA2:**

| Bit4 | Bit3 | CA2 cleared | CA2 set |
|------|------|-------------|---------|
| 0 | 0 | Low after the `E` pulse following a **read of A data** | High when CA1 goes active (data-taken handshake) |
| 0 | 1 | Low after a read of A data | High following the next `E` pulse (pulse strobe) |
| 1 | 0 | Always low while bit 3 = 0 (manual low) | — |
| 1 | 1 | — | Always high while bit 3 = 1 (manual high) |

**Section B — CB2:**

| Bit4 | Bit3 | CB2 cleared | CB2 set |
|------|------|-------------|---------|
| 0 | 0 | Low when `E` goes high following a **write of B data** | High when CB1 goes active (data-ready handshake) |
| 0 | 1 | Low when `E` goes high following a write of B data | High when the next `E` pulse goes high (pulse strobe) |
| 1 | 0 | Always low while bit 3 = 0 (manual low) | — |
| 1 | 1 | — | Always high while bit 3 = 1 (manual high) |

> The `E` pulse is the port enable, one strobe per machine cycle. The CB2 output
> pulse (write-strobe modes) is ~1.5–3.5 µs wide depending on the instruction mix.

---

## 4. Initialization and handshake protocol

All ports reset to "all inputs" at power-on, so software must program the DDRs and
control registers before use. To reach a DDR, first write control-register bit 2 = 0;
then write the DDR (0 = input, 1 = output); then set bit 2 = 1 and program the
control bits. The manual's worked example puts one board at **020–037 octal (16–31
decimal)** — 020 = A control, 021 = A data, 022 = B control, 023 = B data — with
section A as input and section B as output:

| Addr (octal) | Write | Effect |
|--------------|-------|--------|
| 020, 022 | control bit 2 = 0 | select DDRs of both sections |
| 021 | DDR = 000 | A data lines → inputs |
| 023 | DDR = 377 | B data lines → outputs |
| 020 | control = 045 | A: C2 out, CA1 interrupt enabled |
| 022 | control = 054 | B: C2 out |

**Handshake once running:**

- **Input (section A).** The device pulls CA1 low when it has valid data →
  status bit 7 goes high, `IRQA` goes low, CA2 goes high (a "busy" signal to the
  device). The CPU reads the A data register, which resets bit 7, `IRQA`, and CA2;
  the CA2 transition tells the device new data may be entered.
- **Output (section B).** The device pulls CB1 low when ready to receive → status
  bit 7 goes high. The CPU writes the B data register; CB2 pulses low (strobe) to
  clock the data into the device.

---

## 5. Interrupt

Communication may be polled (the CPU periodically reads a section's status bit 7)
or interrupt-driven. Two optional interrupt paths:

- **Vectored** via the [[88-VI-RTC]] board — eight priority levels, for when
  several devices of different priority must be serviced.
- **Single-level, on the 88-4PIO itself.** Jumper any port interrupt-request
  line(s) (`JA`, `JB`, `KA`, `KB`, …) to the board's **`PINT`** pad. Any number of
  request lines may be tied to `PINT`. The CPU enables interrupts with `EI`; on an
  interrupt the request line goes low, the CPU finishes its instruction, pushes PC,
  and jumps to location **70 octal (0x38)**. The service routine begins at 70; `RET`
  returns to the interrupted program.

---

## 6. Address Selection Chart (board select)

Each 16-address board is placed on a 20-octal boundary by jumpering A4–A7 to true
or complemented lines (a bar denotes the complement).

| Base range (octal) | 7 | 6 | 5 | 4 |
|--------------------|---|---|---|---|
| 000–017 | A̅7 | A̅6 | A̅5 | A̅4 |
| 020–037 | A̅7 | A̅6 | A̅5 | A4 |
| 040–057 | A̅7 | A̅6 | A5 | A̅4 |
| 060–077 | A̅7 | A̅6 | A5 | A4 |
| 100–117 | A̅7 | A6 | A̅5 | A̅4 |
| 120–137 | A̅7 | A6 | A̅5 | A4 |
| 140–157 | A̅7 | A6 | A5 | A̅4 |
| 160–177 | A̅7 | A6 | A5 | A4 |
| 200–217 | A7 | A̅6 | A̅5 | A̅4 |
| 220–237 | A7 | A̅6 | A̅5 | A4 |
| 240–257 | A7 | A̅6 | A5 | A̅4 |
| 260–277 | A7 | A̅6 | A5 | A4 |
| 300–317 | A7 | A6 | A̅5 | A̅4 |
| 320–337 | A7 | A6 | A̅5 | A4 |
| 340–357 | A7 | A6 | A5 | A̅4 |
| 360–377 | A7 | A6 | A5 | A4 |

The pattern is a straight binary count on A4–A7: a true line where that address
bit is 1, a complemented line where it is 0.
