# Altair 8800b Turnkey Board (Systems Turnkey Module)

Source: [TurnKey Board.pdf](#)

This reference covers the MITS/Pertec **Systems Turnkey Module** (PCBA P/N 200372-01,
schematic P/N 200374), the single S-100 card that replaces the classic Altair front
panel logic in the 8800b Turnkey and MITS 300-series systems. It bundles six
subsystems onto one board:

- Sense switches
- PROM memory (with phantom/overlap logic)
- Serial I/O (SIO) channel — a 6850 ACIA
- Front panel logic
- Auto-Start circuit
- Miscellaneous signals (MWRT, PROT/UNPROT, AUX CLR)

Power: derived on-board from the Motherboard's +8V and ±18V lines, regulated to +5V
and ±9V.

---

## 1. Sense Switches

Eight DIP switches on the board form **one byte** readable by an I/O `IN` instruction.

- Switch toward the silkscreened arrow = **1** on the bidirectional data bus; opposite
  direction = **0**.
- Physical grouping: **SW6** carries bits 7 6 5 4, **SW7** carries bits 3 2 1 0.
- Decoding: IC K detects the reserved sense I/O port address. On a match its output goes
  active LOW, enabling the tri-state buffers that drive the switch bits onto the
  Data-In bus, and enabling the bus interface.
- Actual port and interpretation are defined by system software (the sense byte is used
  as data or to select program options).

> Emulation note: the MITS/Altair sense-switch input is the high address byte at port
> `FF` in this simulator's front-panel model — see the project's front-panel memory. The
> PDF does not hard-code the port ("reserved I/O port address … refer to the software
> manual").

---

## 2. PROM Memory

- **1024 bytes** of PROM on the board; up to **four PROM chips** may be installed.
- Starting (base) address selected by **6 switches** (SW2 = bits 11,10,15,14; SW3 = bits
  13,12,11,10 per the silkscreen labels shown) representing the **6 most-significant
  address bits (bit 15 = MSB)**. Switch toward arrow = 1, opposite = 0.
- Base must be an integral multiple of 1024, so the low-order 10 bits are always zero.
- The incoming address's top 6 bits are compared with the switch settings; on a match
  the remaining 10 bits select the byte within the 1K block.

### Phantom / RAM-overlap logic
- The **phantom circuit** lets PROM addresses overlap RAM addresses. A memory read to an
  address occupied by both PROM and RAM returns **PROM** data; thus all 64K can still be
  populated with RAM.
- Compare gate is NAND **IC D**. If the incoming address is in the selected 1K block and
  no I/O cycle is in progress, IC D goes active LOW and enables the PROM address decoder
  **IC Za**, which selects one of the PROM ICs.
- IC D's output is NOR'd with the I/O address detector output; that NOR causes the CPU to
  execute a **Wait state** and enables the data-bus interface ICs **P** and **R**.
- IC D LOW also feeds ICs **S1** and **R** to hold **MEMR LOW** on the bus, preventing
  data from being read from system RAM. The selected PROM drives the Data-In bus.

### PROM disable on I/O-from-PROM
- An **I/O cycle to port `377` octal (0xFF)** (address lines A8–A15 HIGH) drives IC B
  pin 8 LOW, clocking IC T; T pin 8 LOW permanently disables IC D and the PROM-select
  logic. **PROMs stay disabled until a system reset clears IC T.** This is how the boot
  PROM hands off to RAM after loading.

---

## 3. Serial I/O (SIO) Channel — 6850 ACIA

Heart of the SIO is a **Motorola 6850 ACIA** (IC Q / "ICQ"), clocked by the on-board
bit-rate generator.

### Port addressing
- The SIO occupies **two consecutive I/O ports**.
- The **high-order 7 bits** of the I/O address are compared with the **SIO address switch**
  settings (SW4 = bits 7,6,5,4; SW5 = bits 3,2,1; SIO ADDRESS group). Switch 7 = MSB.
  Toward arrow = 1.
- **Bit 0** selects the register:
  - bit0 = 0 → **Status / Control** port
  - bit0 = 1 → **Data** (transmit/receive) port

### 3.1 Status Register (read from the Status/Control port)

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | RDRF — Receive Data Register Full | 1 = received data transferred to RDR. Cleared by reading the Data register or master reset. Also reads empty if DCD is LOW. |
| 1 | TDRE — Transmit Data Register Empty | 1 = transmit register emptied, new data may be written. 0 = register still full / transmission of new char not begun since last write. |
| 2 | DCD — Data Carrier Detect | Set to 1 when the modem DCD input goes LOW (carrier lost). Generates an IRQ if Receive Interrupt Enable set. After DCD returns HIGH it stays 1 until Status then Data are read, or master reset. If DCD stays LOW after both reads/reset, the bit is the inverse of the DCD input. |
| 3 | CTS — Clear-to-Send | Inverse of the CTS modem input; 0 here = Clear-to-Send present. When CTS input is LOW, TDRE is inhibited and this bit is set to 1. **Master reset does not affect it.** |
| 4 | FE — Framing Error | 1 = received char improperly framed (missing first stop bit); present throughout the char's availability. |
| 5 | OVRN — Receiver Overrun | 1 = one or more chars lost (RDR not read before next char). Set only when the valid char prior to overrun is read; RDRF stays set until OVRN reset. Reset by reading Data register or master reset. |
| 6 | PE — Parity Error | 1 = ones count disagrees with selected parity. Present as long as the char is in RDR. Inhibited if no parity selected. |
| 7 | IRQ — Interrupt Request | Reflects the IRQ signal state; set (1) whenever IRQ line is LOW. |

### 3.2 Control Register (written to the Status/Control port)

**Bits 0–1: Counter Divide Select** (also master reset). After power-on / power failure
these bits **must first be set HIGH (11) to master-reset** the SIO (clears Status except
external CTS/DCD conditions, initializes Tx/Rx; does not touch other control bits).

| CR1 | CR0 | Ratio |
|-----|-----|-------|
| 0 | 0 | ÷1 (synchronized / external clock) |
| 0 | 1 | ÷16 (normal) |
| 1 | 0 | ÷64 (slow) |
| 1 | 1 | master reset |

**Bits 2,3,4: Word Select** (word length, parity, stop bits — effective immediately, not
buffered):

| CR4 | CR3 | CR2 | Format |
|-----|-----|-----|--------|
| 0 | 0 | 0 | 7 bits, even parity, 2 stop |
| 0 | 0 | 1 | 7 bits, odd parity, 2 stop |
| 0 | 1 | 0 | 7 bits, even parity, 1 stop |
| 0 | 1 | 1 | 7 bits, odd parity, 1 stop |
| 1 | 0 | 0 | 8 bits, 2 stop |
| 1 | 0 | 1 | 8 bits, 1 stop |
| 1 | 1 | 0 | 8 bits, even parity, 1 stop |
| 1 | 1 | 1 | 8 bits, odd parity, 1 stop |

**Bits 5,6: Transmitter Control** (RTS output, Tx interrupt enable, break):

| CR6 | CR5 | Function |
|-----|-----|----------|
| 0 | 0 | RTS = HIGH, Tx interrupt disabled |
| 0 | 1 | RTS = HIGH, Tx interrupt enabled |
| 1 | 0 | RTS = LOW, Tx interrupt disabled |
| 1 | 1 | RTS = HIGH, transmit break (space), Tx disabled |

**Bit 7: Receive Interrupt Enable** — 1 enables RDRF, Overrun, and DCD interrupt requests.

### 3.3 Interrupt Handling (jumpers)
- SIO IRQ can be jumpered to a bus interrupt line. **No jumper** = polled operation
  (software reads Status bits).
- Non-Vector-Interrupt systems: jumper **KA → PINT**. When SIO interrupts, PINT is pulled
  LOW until the condition clears; with CPU Interrupt Enable set this forces an **RST 7**.
  All MITS 300 I/O boards use open-collector interrupt drivers, so multiple boards may
  share PINT.
- Vector-Interrupt-board systems: jumper **KA → one of VI0…VI7** (8 priority levels; VI0
  highest). Boards driven by open-collector drivers may share a level.
- The `IRQA` (KA) pad is in the lower-left corner. Jumper pads: a row labeled `PINT` with
  numbered pads 0,1,2,3 (left) and 7,6,5,4 (right) around a center `IRQ` pad.
- Silkscreen caveat: the board's `SB` / `ON SW` / `SA` labels near the interrupt/start
  area may be printed incorrectly — the manual's labels are authoritative.

### 3.4 Bit Rate Selection
- Bit-rate clock generated by **IC G** from a **2.4576 MHz crystal**; rate chosen by
  jumpers **S0,S1,S2,S3** combined with Control bits CR0/CR1 (the counter divide).
- If CR0=CR1=0 (÷1) and no external rate selected → SIO is **transmit-only**.
- If a rate **above 300 bps** is selected, remove capacitor **C1**.
- External-clock + ÷1 counter: data must be synchronized to the clock; Tx line changes
  within 1 µs of the LOW→HIGH clock edge (~500 ns jitter); Rx sampled within 1 µs after
  HIGH→LOW edge.

**Data Transmission Rates (bps)** — X = jumper installed:

| S3 | S2 | S1 | S0 | ÷16 (CR1=0,CR0=1) normal | ÷64 (CR1=1,CR0=0) slow | ÷1 (CR1=0,CR0=0) sync/external |
|----|----|----|----|----|----|----|
| – | – | – | – | 110 | 27.5 | 1760 |
| – | – | – | X | 150 | 37.5 | 2400 |
| – | – | X | – | 300 | 75 | 4800 |
| – | – | X | X | 2400 | 600 | 38400 |
| – | X | – | – | 1200 | 300 | 19200 |
| – | X | – | X | 1800 | 450 | 28800 |
| – | X | X | – | 4800 | 1200 | 76800 |
| – | X | X | X | 9600 | 2400 | 153600 |
| X | – | – | – | 2400 | 600 | 38400 |
| X | – | – | X | 600 | 150 | 9600 |
| X | – | X | – | 200 | 50 | 3200 |
| X | – | X | X | 134.5 | 33.375 | 2152 |
| X | X | – | – | 75 | 18.75 | 1200 |
| X | X | – | X | 50 | 12.5 | 800 |
| X | X | X | – | External ÷16 rate (36,000 max) | External ÷64 rate (9,000 max) | External rate (400,000 max) |
| X | X | X | X | External ÷16 rate (36,000 max) | External ÷64 rate (9,000 max) | External rate (400,000 max) |

---

## 4. Front Panel Logic

The front-panel indicators and switches connect to the Turnkey Board via a **10-pin
ribbon cable** at connector **J1** (pins 9 & 10 not used).

- Bus signals **PHLTA, PINTE, PINT** are buffered to drive indicators **HALT, INTE, INT**
  respectively.
- The **I/O** indicator = logical OR of **INP** and **OUT** signals.
- The **POWER** indicator monitors the on-board +5V regulator output.
- **PRDY** (bus) is **grounded when RUN/STOP switch is in STOP**.
- **PRESET** is **momentarily grounded by the START switch**, which triggers the
  Auto-Start sequence. START also derives PRESET; POC (power-on clear) does likewise.
- Front-panel LED buffering uses 7402 gates (INP/OUT → I/O LED via IC U) and 220Ω series
  resistors on each LED. INTE/HALT/INT driven through buffer gates to `BINTE`/`BHLTA`/
  `BINT`. RUN/STOP is an FP micro-switch; START is a momentary. `R13` = 1K pull to +5V.

---

## 5. Auto-Start Circuit

Provides an in-PROM auto-start: on power-up or START-switch release, the CPU is forced to
**JMP to the Auto-Start address** and begin executing.

- Address set by **Auto-Start switches SW8 (bits 15,14,13,12) and SW9 (bits 11,10,9,8)**
  — the **8 high-order bits** of the address; **bit 8 = LSB**. Must be a multiple of 256
  (low 8 bits = 0). Set like the PROM address switches (toward arrow = 1).
- **SW9 note:** "as shown" for **MITS 300/25**; SW9 **to the left for MITS 300/55**.
- The switches supply the variable (high) byte of a synthesized **JMP** instruction.

### Mechanism
The JMP is generated by a multiplexer (ICs **M** and **N**) controlled by flip-flops
**T, Sa, Sb**. PRESET (from POC or the START switch) clears the flip-flops; successive
**PDBIN** pulses advance them, and the multiplexer selects each byte in turn. During the
three JMP bytes, the LOW Q output of flip-flop **Ta** (pin 12) feeds ICs S1/R1 to hold
**MEMR low** and keep memory data off the bus; once the JMP completes, Q goes HIGH and
normal memory fetches resume.

**Auto-Start control state sequence** (T Sa Sb):

| Signal In | State (T Sa Sb) | Function |
|-----------|-----------------|----------|
| PRESET | 000 | Mux outputs & bus interface enabled; **303 octal** (JMP opcode, 0xC3) put on bus |
| PDBIN | 010 | 00 (low address byte = 0x00) put on bus |
| PDBIN | 001 | Byte in Auto-Start address switches put on bus |
| PDBIN | 110 | First byte of PROM program |
| PDBIN | 111 | Next byte of PROM program (loops) |

So the synthesized instruction is `JMP <SW8:SW9>00h` (opcode C3, low byte 00, high byte
from switches).

---

## 6. Miscellaneous Signals (jumpers & cabling)

The SIO can drive **20 mA current loop (TTY)**, **RS-232**, or **TTL** peripherals,
selected by jumpers and internal cable choice. The SIO cable runs from the board's I/O
connector **J2** (10-pin) to a rear-panel industry-standard **25-pin** connector.

- **MWRT**: generated only if the **M1→M2** jumper is installed. Used by memory boards
  for write; installed on all MITS 300 systems. **Install M1–M2 for a Turnkey computer;
  remove it for a Front-Panel computer.**
- **PROT / UNPROT** (memory protect): as shipped, the board grounds PROT and pulses
  UNPROT with clock phase 2 to unprotect all memory as accessed. Disable by removing
  jumpers **O2→UM** and **GND→PM**.
- **AUX CLR**: normally pulled HIGH by an on-board resistor; optional jumpers let the
  signal be used.

### 6.1 Internal cable connections (J2 Molex → 25-pin)

| Molex pin | Function | TTY cable (F) → 25-pin | RS232 cable (M) → 25-pin | TTL cable (F) → 25-pin |
|-----------|----------|------------------------|--------------------------|------------------------|
| 1 | TTL RTS | 6 | — | 4 |
| 2 | TTY XMIT | 3 | — | — |
| 3 | TTY REC (BIAS) | 4 | — | — |
| 4 | All REC | 5 | 2 | 2 |
| 5 | DCD | — | 8 | 8 |
| 6 | CTS | — | 5 | 5 |
| 7 | XTERNAL CLOCK | — | 15 | 15 |
| 8 | GND | 2 | 7 | 7 |
| 9 | RS232 RTS | — | 4 | — |
| 10 | RS232 + TTL XMIT | — | 3 | 3 |

### 6.2 Signal-type jumper settings

| Signal type | Jumpers (From → To) | Notes |
|-------------|---------------------|-------|
| TTY compatible | X1→X2, K4→P5, K3→P3, K2→P2 | |
| RS232 compatible | X3→X4, K3→P3, K2→P2 | K3→P3 only if DCD unused; K2→P2 only if CTS unused |
| TTL compatible (3.2 mA max load, 16 mA min drive) | X2→X3, K4→P4, K3→P3, K2→P2, K1→P1 | K1→P1 not needed if external clock unused |

Note: TTL inputs are two unit loads ("TTL compatible").

---

## 7. Boot Loader PROM (MITS 300/25 — floppy)

- The **Floppy Disk Boot Loader PROM** loads system software (e.g. BASIC) from floppy
  into main memory. Installed in socket **H1** on the Turnkey Board.
- **Auto-Start switches must be set to `FF00` (hex)** for floppy boot-loader operation, or
  the computer won't operate on power-up.
- On power-up or START, it loads system software from floppy to RAM, then displays a
  **MEMORY SIZE** prompt on the terminal.

---

## 8. Boot Loader / Turnkey Monitor (MITS 300/55 — hard disk)

Two **inseparable** PROMs that rely on each other, installed together:
- **Boot Loader** PROM in socket **L1**
- **Turnkey Monitor** PROM in socket **K1**

Each has a distinct entry point on a **256-byte boundary** (directly addressable by the
Auto-Start circuitry). **Auto-Start switches must be set to `FD00` (hex)** for Turnkey
Monitor operation. On power-up a prompt `.` is displayed.

- Pressing the computer **START** switch at any time returns control to the Monitor
  (prints `.`).
- The Monitor establishes a **stack with top address `C000` hex** on entry; it uses only
  4 nested subroutine levels (8 bytes) — **RAM must exist at that stack location**.
- The Monitor **destroys all registers and the stack pointer** on entry — the user program
  must save/restore them.
- Input routines accept hex digits `0–9`, `A–F`, and **space**. They expect 4 or 8 digits
  depending on the routine; a **space terminates a number** early and delimits inputs.

### 8.1 Commands

**M — Memory Examine and Change** — format `Mxxxx`:
1. Opens the specified location, waits for **4 valid hex digits** (space not valid here to
   open).
2. On receipt, deposits into the open location. Bad deposit (nonexistent memory, ROM, or
   protected RAM) prints `?` and returns to the monitor. Otherwise deposit is made,
   verified, current location closed, and **next location opened**.
3. Continues until a non-valid char (anything but 0–9/A–F) is entered — flagged `?`,
   returns to monitor (the normal exit).
- A **space** instead of a hex char closes the current location **without change** and
  opens the next; two full valid hex digits are needed to deposit.

**L — Hard Disk Boot Loader** — transfers control to the Hard Disk Boot Loader PROM.
Messages:
1. `--WAITING` — loader running, waiting for drive to come on-line.
2. `--LOADING` — loader operating on the disk, bringing in the system image.
3. `RST CNTR` / `LOAD ERROR (dd)` — error; `dd` is the hex error byte from controller
   firmware; control returns to the Turnkey Monitor.

**J — Jump to User Program** — format `Jxxxx`: loads the PC with the address (0–4 hex
digits) and starts execution there.

### 8.2 Hard Disk load error byte (dd)

| Bit | Hex value | Error | Meaning |
|-----|-----------|-------|---------|
| 0 | 01 | DRIVE NOT READY | Drive went off-line after LOADING issued (LOADING only issues when drive ready). |
| 1 | 02 | ILLEGAL SECTOR | Sector read outside 0–23; indicates severe memory/hardware problem. |
| 2 | 04 | CRC ERROR IN SECTOR READ | CRC error reading the 256 data bytes of a sector. |
| 3 | 08 | CRC ERROR IN HEADER READ | CRC error reading a header (after Read Sector or Seek Cylinder). |
| 4 | 10 | HEADER HAS WRONG SECTOR | Read Sector header ≠ desired sector (position error). |
| 5 | 20 | HEADER HAS WRONG CYLINDER | Seek Cylinder header ≠ desired cylinder (position error). |
| 6 | 40 | HEADER HAS WRONG HEAD # | Seek/Read header ≠ desired head 0/1 (wrong platter side). |
| 7 | 80 | FIRMWARE TIMEOUT | Controller firmware didn't respond within ~1.6 s. |

The byte is a bit mask (multiple errors can be set simultaneously).

---

## 9. Board Layout Reference (silkscreen)

Component/switch designators (from the layout drawing):

- **J1** — Front Panel connector (10-pin ribbon; pins 9,10 unused).
- **J2** — SIO connector (10-pin).
- **SW6, SW7** — Sense switches (SW6=7654, SW7=3210).
- **SW1, SW2, SW3** — RAM ADD / PROM ADD switches (SW2/SW3 = PROM address per §2).
- **SW4, SW5** — SIO ADDRESS switches (SW4=7654, SW5=321).
- **SW8, SW9** — START ADD (Auto-Start address) switches.
- **BAUD** jumper posts **S0,S1,S2,S3** + **C1** + **GND**.
- **PINT** interrupt jumper block with pads 0–7 and center IRQ.
- Miscellaneous jumper posts: **M1, PM, UM, M2, GND, O2**; **SW-SA** (normally installed),
  **S-B** (normally installed); **M1–M2** (install for turnkey, remove for front panel).
- SIGNAL TYPE jumper fields near J2: posts **K/P (1 2 3 4)** and **X (1 2 3 4)**.

---

## 10. Service Bulletins / Modifications

### Service Bulletin #007 (10/9/79) — I/O-from-PROM disable bug
Applies to **all Systems Turnkey Module PCBAs P/N 200372-01**. The new module disables the
PROMs if an **I/O instruction is executed from a PROM location**, so it won't work with
certain programmed PROMs. Fix (schematic 200374 revision):
1. **Cut etch at IC "E" pin 6 on the back side** of the board.
2. Install 30 AWG jumpers:
   - IC "C" pin 4 → IC "B" pin 12
   - IC "C" pin 5 → IC "W" pin 11
   - IC "C" pin 6 → IC "E" pin 5

Schematic changes: 200374 Sht 1 Zone 7C — reroute so `PDBIN 2-D8` drives IC C pins 5,6 →
pin 4 (adds gate "Abb"); IC E pin 6→5 rerouted. 200374 Sht 2 Zone D8 — ADD gate "W"
(pins 12,13 → 11) driven by `PDBIN 1-C8` with a pull resistor.

### 88-SYS CLG modification (full 64K RAM)
8800b-T systems with the Turnkey Module installed **cannot use a full 64K of RAM** (4×
16MCD or 4× 16MCS boards) because the module's 1K PROM/RAM overlap. Modifying the module
to **disable the 1K RAM and let PROM and RAM address the same locations** yields the
**88-SYS CLG**, allowing full 64K.

Component side — cut lands:
1. Cut land to IC "D" pin 11.
2. Cut land between IC "D" pins 11 & 12.
3. Cut land to IC "D" pin 12.

Back side — cut lands:
1. Cut land to IC "B" pin 11.
2. Cut land to IC "Q" pin 7.
3. Cut land to IC "M1" pin 13.

Back side — add jumpers:
4. Collector of "Q2" → bus pin 47.
5. IC "M1" pin 13 → bus pin 47.
6. IC "D" pin 11 → land from IC "E" pin 5.
7. IC "D" pin 12 → IC "T" pin 8.
8. IC "B" pin 11 → ground.
9. IC "T" pin 7 → IC "U" pin 14 (+5V).
10. IC "T" pin 10 → IC "T" pin 11.
11. IC "T" pin 2 → IC "T" pin 6.
12. IC "T" pin 5 → IC "W" pin 4.
13. IC "W" pin 2 → IC "M1" pin 15.
14. IC "Q" pin 7 → "IRQ" land.

---

## Quick-reference summary for emulation

- 6850 ACIA at a 2-port base (Status/Control at even, Data at odd); status/control bit
  semantics per §3.1–3.2. Master reset = write control 0x03 (bits1:0 = 11) first.
- PROM: 1K block at a 1K-aligned base; reads win over RAM (phantom); an **I/O to port
  0xFF disables PROM until reset** (post-Service-Bulletin-007 boards restrict this to true
  I/O-from-PROM only).
- Auto-Start = hardware-synthesized `JMP <switch_hi>00h` on reset/START. Standard boot
  addresses: **FF00** (300/25 floppy loader), **FD00** (300/55 hard-disk Turnkey Monitor).
- Sense switches = one input byte (SW6 high nibble, SW7 low nibble).
- Front panel: STOP grounds PRDY; START momentarily grounds PRESET (fires Auto-Start);
  HALT/INTE/INT/I/O/POWER lamps are buffered bus signals.
- Turnkey Monitor: stack top C000, commands M/L/J, hard-disk error bitmask per §8.2.
