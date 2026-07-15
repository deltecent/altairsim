# Altair 88-2SIO Dual Serial Interface — User's Manual

Source: [Altair 2SIO User's Manual.pdf](#)

The MITS **88-2SIO** ("88-2") is an S-100 dual serial I/O board built around the
**Motorola 6850 ACIA** (Asynchronous Communications Interface Adapter). Each
board holds **up to two ACIAs ("ports")**. Almost every option is
software-selectable; the only hardware-selected options are the **address** and
the **baud-rate** (baud rate can also be altered under software control via the
clock-divide bits). Portions of the status-register description are Copyright
1975 Motorola, Inc., Semiconductor Products Division.

---

## 1. I/O Port Assignments

Each ACIA occupies **two consecutive I/O addresses**: a control/status register
and a data register. A fully-populated board (two ports) occupies **four
consecutive addresses**, aligned to a multiple of four.

- Address line **A0** selects register within a port: **0 = control/status,
  1 = data**.
- Address line **A1** selects the port: **0 = 1st port (Port 0), 1 = 2nd port
  (Port 1)**.
- Address lines **A2–A7** are the board's base-address select (hardware
  jumpers), giving 64 possible base addresses in increments of four.

### Register / Port selection (A0, A1)

| A0 | A1 | Port # | OUTPUT (write) function | INPUT (read) function |
|----|----|--------|-------------------------|-----------------------|
| 0  | 0  | 0 (1st) | Control Register  | Status Register  |
| 1  | 0  | 0 (1st) | Transmit Data Reg | Receive Data Reg |
| 0  | 1  | 1 (2nd) | Control Register  | Status Register  |
| 1  | 1  | 1 (2nd) | Transmit Data Reg | Receive Data Reg |

The same address is a **control register on OUT** and a **status register on
IN**; likewise the odd address is **transmit data on OUT** and **receive data on
IN**. Read and write of a given address hit two physically different registers.

### Example: board strapped at base address 68 (octal 104 decimal 68)

| Address | Function |
|---------|----------|
| 68 | 1st port, control/status |
| 69 | 1st port, data in/out |
| 70 | 2nd port, control/status |
| 71 | 2nd port, data in/out |

So `OUT 70` (octal 106) writes the accumulator into the **control register of the
2nd port** of a board strapped at base 68.

### MITS software conventions / boot notes

- **MITS software assumes the 2-SIO is addressed at location 20 (octal)** when
  used to interface the loading device (tape reader, etc.). This is the standard
  console/loader address.
- A bootstrap loader labeled **"2SIO (for versions 3.2 and later)"** initializes
  the 2SIO port at its octal address **005**. The octal codes **021** or **025**
  correspond to that address; **both set the clock divide to ÷16**, and
  **interrupts should NOT be enabled** for the boot loader.
- Address **255** (octal 377) is the **front-panel sense switch** and must not be
  used for the board.
- When loading MITS software, **start the bootstrap loader BEFORE starting the
  loading device** (paper-tape reader, etc.). With the 2-SIO boot loader, first
  push STOP/RUN to RUN, then start the reader.

### Base-address hardware strapping (A2–A7)

Base address is set by jumpering pads **F2–F7** to **A2–A7** (true) or **Ā2–Ā7**
(inverted). A bar (Ā) means jumper to the *inverted* address line; a plain name
means jumper to the *true* line. The complete octal chart from the manual (a bar
is shown here as a leading `/`):

| Octal range | A7 | A6 | A5 | A4 | A3 | A2 |
|-------------|----|----|----|----|----|----|
| 000–003 | /A7 | /A6 | /A5 | /A4 | /A3 | /A2 |
| 004–007 | /A7 | /A6 | /A5 | /A4 | /A3 |  A2 |
| 010–013 | /A7 | /A6 | /A5 | /A4 |  A3 | /A2 |
| 014–017 | /A7 | /A6 | /A5 | /A4 |  A3 |  A2 |
| 020–023 | /A7 | /A6 | /A5 |  A4 | /A3 | /A2 |
| 024–027 | /A7 | /A6 | /A5 |  A4 | /A3 |  A2 |
| 030–033 | /A7 | /A6 | /A5 |  A4 |  A3 | /A2 |
| 034–037 | /A7 | /A6 | /A5 |  A4 |  A3 |  A2 |
| 040–043 | /A7 | /A6 |  A5 | /A4 | /A3 | /A2 |
| 044–047 | /A7 | /A6 |  A5 | /A4 | /A3 |  A2 |
| 050–053 | /A7 | /A6 |  A5 | /A4 |  A3 | /A2 |
| 054–057 | /A7 | /A6 |  A5 | /A4 |  A3 |  A2 |
| 060–063 | /A7 | /A6 |  A5 |  A4 | /A3 | /A2 |
| 064–067 | /A7 | /A6 |  A5 |  A4 | /A3 |  A2 |
| 070–073 | /A7 | /A6 |  A5 |  A4 |  A3 | /A2 |
| 074–077 | /A7 | /A6 |  A5 |  A4 |  A3 |  A2 |
| 100–103 | /A7 |  A6 | /A5 | /A4 | /A3 | /A2 |
| 104–107 | /A7 |  A6 | /A5 | /A4 | /A3 |  A2 |
| 110–113 | /A7 |  A6 | /A5 | /A4 |  A3 | /A2 |
| 114–117 | /A7 |  A6 | /A5 | /A4 |  A3 |  A2 |
| 120–123 | /A7 |  A6 | /A5 |  A4 | /A3 | /A2 |
| 124–127 | /A7 |  A6 | /A5 |  A4 | /A3 |  A2 |
| 130–133 | /A7 |  A6 | /A5 |  A4 |  A3 | /A2 |
| 134–137 | /A7 |  A6 | /A5 |  A4 |  A3 |  A2 |
| 140–143 | /A7 |  A6 |  A5 | /A4 | /A3 | /A2 |
| 144–147 | /A7 |  A6 |  A5 | /A4 | /A3 |  A2 |
| 150–153 | /A7 |  A6 |  A5 | /A4 |  A3 | /A2 |
| 154–157 | /A7 |  A6 |  A5 | /A4 |  A3 |  A2 |
| 160–163 | /A7 |  A6 |  A5 |  A4 | /A3 | /A2 |
| 164–167 | /A7 |  A6 |  A5 |  A4 | /A3 |  A2 |
| 170–173 | /A7 |  A6 |  A5 |  A4 |  A3 | /A2 |
| 174–177 | /A7 |  A6 |  A5 |  A4 |  A3 |  A2 |
| 200–203 |  A7 | /A6 | /A5 | /A4 | /A3 | /A2 |
| 204–207 |  A7 | /A6 | /A5 | /A4 | /A3 |  A2 |
| 210–213 |  A7 | /A6 | /A5 | /A4 |  A3 | /A2 |
| 214–217 |  A7 | /A6 | /A5 | /A4 |  A3 |  A2 |
| 220–223 |  A7 | /A6 | /A5 |  A4 | /A3 | /A2 |
| 224–227 |  A7 | /A6 | /A5 |  A4 | /A3 |  A2 |
| 230–233 |  A7 | /A6 | /A5 |  A4 |  A3 | /A2 |
| 234–237 |  A7 | /A6 | /A5 |  A4 |  A3 |  A2 |
| 240–243 |  A7 | /A6 |  A5 | /A4 | /A3 | /A2 |
| 244–247 |  A7 | /A6 |  A5 | /A4 | /A3 |  A2 |
| 250–253 |  A7 | /A6 |  A5 | /A4 |  A3 | /A2 |
| 254–257 |  A7 | /A6 |  A5 | /A4 |  A3 |  A2 |
| 260–263 |  A7 | /A6 |  A5 |  A4 | /A3 | /A2 |
| 264–267 |  A7 | /A6 |  A5 |  A4 | /A3 |  A2 |
| 270–273 |  A7 | /A6 |  A5 |  A4 |  A3 | /A2 |
| 274–277 |  A7 | /A6 |  A5 |  A4 |  A3 |  A2 |
| 300–303 |  A7 |  A6 | /A5 | /A4 | /A3 | /A2 |
| 304–307 |  A7 |  A6 | /A5 | /A4 | /A3 |  A2 |
| 310–313 |  A7 |  A6 | /A5 | /A4 |  A3 | /A2 |
| 314–317 |  A7 |  A6 | /A5 | /A4 |  A3 |  A2 |
| 320–323 |  A7 |  A6 | /A5 |  A4 | /A3 | /A2 |
| 324–327 |  A7 |  A6 | /A5 |  A4 | /A3 |  A2 |
| 330–333 |  A7 |  A6 | /A5 |  A4 |  A3 | /A2 |
| 334–337 |  A7 |  A6 | /A5 |  A4 |  A3 |  A2 |
| 340–343 |  A7 |  A6 |  A5 | /A4 | /A3 | /A2 |
| 344–347 |  A7 |  A6 |  A5 | /A4 | /A3 |  A2 |
| 350–353 |  A7 |  A6 |  A5 | /A4 |  A3 | /A2 |
| 354–357 |  A7 |  A6 |  A5 | /A4 |  A3 |  A2 |
| 360–363 |  A7 |  A6 |  A5 |  A4 | /A3 | /A2 |
| 364–367 |  A7 |  A6 |  A5 |  A4 | /A3 |  A2 |
| 370–373 |  A7 |  A6 |  A5 |  A4 |  A3 | /A2 |
| 374–377 |  A7 |  A6 |  A5 |  A4 |  A3 |  A2 |

Within each 4-address group: **000 = Port 0 control, 001 = Port 0 data,
002 = Port 1 control, 003 = Port 1 data** (even = control, odd = data; first pair
= Port 0, second pair = Port 1).

---

## 2. Control Register (write-only, even address)

Each port has an 8-bit control register, written via `OUT` to the even address.
**Data bit low = 0, data bit high = 1.**

| Bit 7 | Bit 6 | Bit 5 | Bits 4 3 2 | Bits 1 0 |
|-------|-------|-------|------------|----------|
| Rx (In) Interrupt enable | Tx (Out) Interrupt / RTS control | (part of Tx/RTS field) | Transmission format (word/stop/parity) | Clock Divide and Reset |

Bits 5–6 together form the transmit-control field (RTS + Tx interrupt + break);
bit 7 is the receive-interrupt enable. See the sub-tables below.

### 2.1 Bits 1–0 — Counter Divide Select / Master Reset

| Bit 1 | Bit 0 | Function |
|-------|-------|----------|
| 0 | 0 | ÷ Clock by 1 |
| 0 | 1 | ÷ Clock by 16 |
| 1 | 0 | ÷ Clock by 64 |
| 1 | 1 | **Master Reset** |

**Normal init sequence:** first write bits 1,0 = **1,1** to master-reset the ACIA;
then write bits 1,0 = **0,1** (÷16), because the incoming clock frequency on the
board is 16× the baud rate. Any of the eight baud rates silk-screened on the
board are intended for use with **÷16**.

To reach the **five additional (higher) baud rates**, use **÷64** (bits 1,0 =
1,0). In ÷64 mode the effective rate is ¼ of the silk-screened value, because
the incoming clock is 16× the desired rate and the ACIA divides it by 64 (64÷16 =
4):

| Silk-screen value (÷16) | Effective baud (÷64) |
|-------------------------|----------------------|
| 27.5  | 110  |
| 37.5  | 150  |
| 75.0  | 300  |
| 450   | 1800 |
| 600   | 2400 |

### 2.2 Bits 4–3–2 — Word length / stop bits / parity

| Bit 4 | Bit 3 | Bit 2 | # Data Bits | # Stop Bits | Parity |
|-------|-------|-------|-------------|-------------|--------|
| 0 | 0 | 0 | 7 | 2 | Even |
| 0 | 0 | 1 | 7 | 2 | Odd  |
| 0 | 1 | 0 | 7 | 1 | Even |
| 0 | 1 | 1 | 7 | 1 | Odd  |
| 1 | 0 | 0 | 8 | 2 | None |
| 1 | 0 | 1 | 8 | 1 | None |
| 1 | 1 | 0 | 8 | 1 | Even |
| 1 | 1 | 1 | 8 | 1 | Odd  |

### 2.3 Bits 7–6–5 — Interrupt / handshake control

Bits 6 and 5 control RTS, the transmit interrupt, and break. Bit 7 controls the
receive interrupt independently. (`X` = don't care.)

| Bit 7 | Bit 6 | Bit 5 | Function |
|-------|-------|-------|----------|
| X | 0 | 0 | RTS = low, transmit interrupt **disabled** |
| X | 0 | 1 | RTS = low, transmit interrupt **enabled** |
| X | 1 | 0 | RTS = high, transmit interrupt **disabled** |
| X | 1 | 1 | RTS = high, **transmits a break level** on transmit-data output, transmit interrupt disabled |
| 0 | X | X | Receive interrupt **disabled** |
| 1 | X | X | Receive interrupt **enabled** |

---

## 3. Status Register (read-only, even address)

Read via `IN` from the even address. Reflects the state of the transmit data
register, receive data register, error logic, and peripheral/modem inputs.
(This section is Motorola-copyright material.)

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | **RDRF** — Receive Data Register Full | 1 = a received character has been transferred into the RDR and is available. Cleared (→0) by **reading the Receive Data Register** or by master reset. **DCD high also forces RDRF to indicate empty.** |
| 1 | **TDRE** — Transmit Data Register Empty | 1 = transmit data register is empty; new data may be written. 0 = register full, transmission of a new character has not begun since the last write. **A high CTS input inhibits (forces low) TDRE.** |
| 2 | **DCD** — Data Carrier Detect | 1 = the DCD input from a modem went high (carrier lost). A low→high transition on DCD **generates an interrupt request when the receive-interrupt enable is set**; the DCD status bit then stays high until cleared by *first reading the status register and then reading the data register*, or by master reset. If DCD input is still high after that clear sequence, the status bit stays high and follows the input. |
| 3 | **CTS** — Clear-to-Send | Reflects the CTS input from a modem. A low CTS indicates clear-to-send. **When CTS is high, the TDRE (bit 1) status bit is inhibited and the CTS status bit will be high.** Master reset does **not** affect the CTS status bit. |
| 4 | **FE** — Framing Error | 1 = received character was improperly framed (missing 1st stop bit) — a sync error, faulty transmission, or break. The FE flag is set/reset during the receive data transfer time, so it is present the whole time the associated character is available. |
| 5 | **OVRN** — Receiver Overrun | 1 = one or more characters were received but lost (not read from the RDR before subsequent characters arrived). The overrun condition begins at the midpoint of the last bit of the second character received in succession without a read of the RDR. Overrun does not appear in status until the *valid* character prior to overrun has been read; RDRF stays set until overrun is reset. Overrun indication is reset by reading the RDR, and also by master reset. |
| 6 | **PE** — Parity Error | 1 = number of 1-bits does not match the selected odd/even parity. Present as long as the data character is in the RDR. If no parity is selected, both the transmitter parity generator output and the receiver parity check are inhibited. |
| 7 | **IRQ** — Interrupt Request | Reflects the state of the (active-low) IRQ output. Any interrupt condition with its applicable enable set is indicated here. **Whenever the IRQ output is low (asserted), the IRQ status bit is high.** Read this bit to determine interrupt/service-request status. |

### Flag polarity notes (important for emulation)

- **TDRE (bit 1)** and **RDRF (bit 0)** are the two flags software polls in the
  typical polled-console loop: TDRE=1 → OK to send; RDRF=1 → a byte is waiting.
- **CTS high inhibits TDRE** (holds it at 0). If CTS/DCD inputs are unused they
  **must be jumpered to ground** on the real board (see Notice below), i.e. the
  emulator should treat unconnected CTS/DCD as **low/asserted-clear** so TDRE and
  RDRF behave normally.
- **DCD high forces RDRF to read empty** and can generate an interrupt.
- **IRQ status bit (bit 7) is the inverse polarity** of the physical IRQ line:
  IRQ line low (asserted) ⇒ status bit 7 = 1.

---

## 4. Data Registers

- **Transmit Data Register** — write-only, odd address (`OUT`). Writing a byte
  clears TDRE (bit 1 → 0) until transmission frees the register.
- **Receive Data Register** — read-only, odd address (`IN`). Reading it clears
  RDRF (bit 0 → 0) and resets the OVRN overrun indication.

---

## 5. Baud Rate / Clock

- On-board crystal: **2.4576 MHz** (CR1); baud-rate generator uses a 4702 baud
  generator IC plus a 93L34 latch.
- **Eight baud rates** are available on the board silk screen: **110, 9600, 4800,
  1800, 150, 300, 2400, 1200**.
- Baud-rate clock input for **Port 0 (IC D)** is **CK0**; for **Port 1 (IC E)** is
  **CK1**. Either or both ports may be jumpered to any of the eight rates.
- The board delivers an incoming clock at **16× the desired baud rate**; combined
  with the ACIA's counter-divide bits this gives the effective rate (use **÷16**
  for the silk-screened rates; **÷64** for the five higher rates in the ÷64 table
  in §2.1).

---

## 6. Initialization Sequence (example)

Example from the manual: a Teletype at **8 data bits, 2 stop bits, no parity**,
with **both receive and transmit interrupts enabled**, I/O address = 0 (octal
`323 000` = `OUT 0`):

| Octal code | Meaning |
|-----------|---------|
| `076` `003` `323` `000` | **Reset port** — load A=003 (master reset, bits 1,0=1,1) and `OUT 0` to control register |
| `076` `261` `323` `000` | **Configure** — load A=261 octal (see below) and `OUT 0` to control register |

`261` octal = `10110001` binary:
- bits 1,0 = **01** → ÷16
- bits 4,3,2 = **100** → 8 data bits, 2 stop bits, no parity
- bits 6,5 = **01** → RTS low, transmit interrupt enabled
- bit 7 = **1** → receive interrupt enabled

**Master reset must precede configuration** — always write the reset word
(bits 1,0 = 1,1) first, then the real control word.

---

## 7. Interrupt Behavior

Three interrupt options, board-selectable:

1. **Eight-level** interrupt via the **88-Vector Interrupt (88-VI)** board.
2. **Single-level** interrupt via the IRQ line brought out on the 88-2SIO board
   (pad **PINT**). Port 0's interrupt request is **DI**, Port 1's is **EI**;
   jumper the chosen line(s) to **PINT**. **If the single-level line is used, no
   other board may be hardwired to the processor for interrupt** (the CPU handles
   only one interrupt signal).
3. **No interrupt** at all.

Interrupt sources, per port, are enabled by the control-register bits:
- **Receive interrupt** enabled by control bit 7 = 1 (fires on RDRF, and on a
  DCD low→high transition).
- **Transmit interrupt** enabled by control bits 6,5 = 0,1 (fires on TDRE).
- The physical **IRQ output is active-low**; its state is readable as status bit 7
  (active-high there).

For the MITS boot loaders, **interrupts should not be enabled**.

---

## 8. Bus / Timing Behavior (for cycle-accurate emulation)

- The board decodes an **8-bit I/O address**, which the CPU places on **both** the
  low address lines (A0–A7) and the high address lines (A8–A15) during an I/O
  machine cycle. Status signals distinguish I/O from memory: **SINP** for `IN`,
  **SOUT** for `OUT`.
- **`IN` (read):** data strobed into the accumulator with **PDBIN**. SINP also
  forces a **CPU wait state of ~500 ns** (one wait state) to allow port
  address-setup time; the wait occurs **only on input**.
- **`OUT` (write):** accumulator placed on the data bus and strobed out with
  **PWR** (active low). SOUT forces the ACIA chip-select and R/W low, then PWR
  generates the ACIA **E (Enable)** pulse.
- The ACIA **E pulse** (required to read/write any ACIA register) is generated
  each machine cycle from PWR or PDBIN.
- **POC** (power-on-clear) clears the wait-state flip-flop at power-up.
- `IN` = opcode **333 octal (0xDB)**, `OUT` = opcode **323 octal (0xD3)**; byte 2
  is the device address (0–377 octal).

---

## 9. Signal Interface Levels

Each port can be hardware-jumpered for one of three electrical interfaces:

**1. RS-232**
- Inputs via 1489 receivers: min swing ±3 V, max swing ±30 V, converted to TTL.
- Outputs via 1488 driver: ±9 V minimum swing, current-limited ~10 mA.

**2. TTL**
- Inputs: 1 unit load; logic low = −0.5 to 0.8 V, logic high = 2 to 5.5 V.
- Outputs: 10 unit loads; logic low = 0.4 V max, logic high = 2.4 V min.
- (1 unit load = 40 µA high / 1.6 mA low.)

**3. TTY (20 mA current loop)**
- Input: transmit-distributor contacts drive gate K; marking = contacts open.
- Output: transistor Q1 driven by port output; marking state = Q1 conducting.

### 10-pin Molex connector signal pin-outs (board ↔ device)

| Interface | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|-----------|---|---|---|---|---|---|---|---|---|----|
| **TTY** | — | DCD (no polarity) | RTS (+) | Ground | Transmit (+) | — | Receive (no polarity) | Clear-to-Send | (CTS no polarity) | Ground |
| **TTL** | CTS | DCD | RTS | Ground | Transmit | — | — | — | Receive | Ground |
| **RS-232** | CTS | DCD | RTS | Ground | — | — | Receive | Transmit | — | Ground |

### RS-232 to standard DB-25 device (normal interconnect)

| Signal | DB-25 Pin |
|--------|-----------|
| Receive | 2 |
| Transmit | 3 |
| Request to Send | 4 |
| Clear to Send | 5 |
| Signal Ground | 7 |
| Carrier Detect | 8 |
| Data Terminal Ready | 20 |

---

## 10. Operating Notes / Quirks

1. **Unused DCD and CTS inputs must be jumpered to ground.** In the emulator,
   model unconnected CTS/DCD as the asserted/clear state so TDRE and RDRF are not
   inhibited.
2. When using the 2-SIO to load MITS software, **start the bootstrap loader
   before starting the loading device** (paper-tape reader, etc.).
3. **Master reset (control bits 1,0 = 1,1) does NOT clear the CTS status bit**;
   CTS follows its input.
4. Reading the receive data register clears **both** RDRF and the OVRN overrun
   indication.
5. The DCD interrupt/status clear requires **read status, then read data** in that
   order (or master reset).

---

## 11. Key ICs / Hardware Reference

| Ref | Part | Role |
|-----|------|------|
| D, E | 6850 | The two ACIAs (Port 0 = D, Port 1 = E). E installed only for 2-port boards. |
| F | 4702 | Baud-rate generator (16-pin). Installed only where needed. |
| G | 93L34 | Baud-rate latch |
| CR1 | 2.4576 MHz crystal | Baud-rate clock source |
| I, J | 1489 | RS-232 receivers (J only for 2-port) |
| N | 1488 | RS-232 driver |
| A, B, C | 8T97 | Tristate bus drivers |
| Q1–Q4 | EN2907 (PNP) | TTY current-loop output transistors |
| VR1 | MC7805 | +5 V regulator |

Board also derives **+12 V / −12 V** from the S-100 ±16 V rails via R1/R2 and
12 V zeners (D1/D2) for the RS-232 drivers.
