# MITS 8800b Turnkey Module (Systems Turnkey Board)

Source: [TurnKey Board.pdf](https://deramp.com/downloads/altair/hardware/turnkey_board/TurnKey%20Board.pdf)
and [8800b Turnkey Notes with Schematics.pdf](https://deramp.com/downloads/altair/hardware/turnkey_board/8800b%20Turnkey%20Notes%20with%20Schematics.pdf)

Two sources are folded together here:

- **The MITS/Pertec *Systems Turnkey Module* Theory of Operation** (drawing
  `200786A`, §4-4), plus its Boot Loader PROM (§9) and Boot Loader/Turnkey
  Monitor (§10) chapters, the *Systems Turnkey module malfunction* Service
  Bulletin **#007** (10/9/79), and the assembly/schematic drawing `248067A`.
  This is the 300-series (MITS 300/25 floppy, 300/55 hard disk) documentation.
- **Martin Eberhard's *8800b Turnkey Module Notes*** (13 Aug 2014), which gives
  the actual addresses, socket map, PROM contents, the two board revisions, and
  the real (buggy) behaviour of the Phantom circuit that the MITS manual only
  hints at. Where the two disagree, Eberhard's notes are the more reliable
  account of what the hardware *does*; the MITS manual describes what it was
  *meant* to do.

The Turnkey Module is the single S-100 board that replaces the classic Altair
front-panel logic in the 8800bt ("Turnkey", no front panel) and the MITS 300
systems. One board bundles six subsystems: **PROM**, **onboard SRAM** (older
revision only), a **6850 SIO** serial channel, **Auto-Start**, **sense
switches**, and the **front-panel / miscellaneous** glue.

- PCBA P/N **200372-01** (newer), schematic P/N **200374**.
- Power is derived on-board from the Motherboard's **+8V and ±18V** lines,
  regulated to **+5V and ±9V**.

---

## 1. Two board revisions

| | "8800b TURNKEY MODULE REV 0" (older) | `200372-01` (newer) |
|---|---|---|
| Onboard SRAM | **Yes** — eight 2102 SRAMs = 1K at `F800h`–`FBFFh` | **No** — SRAM removed |
| Phantom circuit | No | **Yes** (see §9) |
| Tell-tale | Eight 2102s in a row above the EPROM sockets | No SRAM chips |

MITS's **88-SYS-CLG** field modification tried to make an older board behave
like a newer one (disable the RAM, add Phantom). It was not entirely
successful — see §11.

> Emulation note: for a software model, target the newer `200372-01` behaviour
> (1K PROM + Phantom, no onboard RAM) unless you are specifically reproducing a
> REV 0 board. The onboard RAM at `F800h`–`FBFFh` only exists on REV 0.

---

## 2. Memory & I/O map (the emulation-relevant summary)

### PROM — four 1702A sockets, 1 KB total

Normally addressed at **`FC00h`–`FFFFh`** (PROM ADDR switches `SW-2 = xx11`,
`SW-3 = 1111`). Each 256-byte socket:

| Socket | Address | Normal contents |
|---|---|---|
| `L1` | `FC00h` | **HDBL** — hard-disk boot loader |
| `K1` | `FD00h` | **TURMON** / **UBMON** — system (Turnkey) monitor |
| `J1` | `FE00h` | **MBL** — multi-boot loader (paper tape / cassette) |
| `H1` | `FF00h` | **DBL / MDBL / CDBL** — floppy-disk boot loader |

The board inserts **one wait state per PROM access**.

### Onboard SRAM (REV 0 only) — 1 KB

Normally addressed at **`F800h`–`FBFFh`** (RAM ADDR switches `SW-1 = 1111`,
`SW-2 = 10xx`). Absent on the newer board.

### SIO — 6850 ACIA, the Terminal Port

Normally addressed at **`10h`** (SIO ADDR switches `SW-4 = 0001`,
`SW-5 = 000x`). Two consecutive ports: **even = Status/Control, odd = Data**
(address bit 0 selects: 0 → Status/Control, 1 → Data). **Compatible with Port A
of an 88-2SIO.** One wait state per SIO access.

> Emulation note: because the SIO is 2SIO-Port-A-compatible at `10h`, software
> written for a 2SIO terminal (e.g. Altair BASIC, the boot loaders) drives the
> Turnkey SIO unchanged. In this simulator the 6850 model is shared — see
> [`6850.md`](6850.md) and [`Altair 2SIO User's Manual.md`](Altair%202SIO%20User%27s%20Manual.md).

---

## 3. Switch configuration

All address-selection DIP switches read the same way: **toward the silkscreened
arrow / to the right = binary `1`**, opposite = `0`. The tables above use the
MITS `SW-n` numbering. Physical grouping on the board (assembly drawing
`248067A`):

| Switch bank | Function | Bits selected |
|---|---|---|
| `SW-1`, `SW-2` | **RAM ADDR** (REV 0) | high bits of the onboard-RAM base |
| `SW-2`, `SW-3` | **PROM ADDR** | the **6 most-significant** address bits (A15..A10); base is a multiple of 1024, low 10 bits = 0 (bit 15 = MSB) |
| `SW-4`, `SW-5` | **SIO ADDR** | high 7 bits of the SIO port address (switch 7 = MSB; bit 0 selects Data/Status) |
| `SW-6`, `SW-7` | **SENSE** | one data byte (see §4) |
| `SW-8`, `SW-9` | **START ADDR** | high 8 bits of the Auto-Start address (see §7) |

`SW-2` is shared between the RAM and PROM base decoders on REV 0.

> The assembly drawing carries the warning "**THESE LABELS ARE CORRECT (the
> board may be labeled incorrectly)**" next to the `PINT` / `SB` / `SW` / `SA`
> pads — the silkscreen on some boards is wrong. Trust the drawing, not the
> board.

---

## 4. Sense switches

Eight DIP switches — **`SW-6` = bits 7 6 5 4**, **`SW-7` = bits 3 2 1 0** — form
**one byte** on the bidirectional data bus. A switch **up / toward the arrow =
`1`** (high-order bit on the left). In an 8800bt (no front panel) these replace
the front-panel sense switches; they are normally set to specify the **load
port** (for MBL) and the **Terminal port** (for BASIC and DOS).

`IC K` decodes the reserved sense-switch I/O port; on a match its output goes
**active LOW**, enabling the tri-state buffers that drive the switch bits onto
the Data-In bus and enabling the bus interface. The port and the meaning of the
byte are defined by system software ("refer to the appropriate software
manual").

> Emulation note: in this simulator the MITS/Altair sense-switch input is the
> **high address byte at port `FF`** in the front-panel model — see
> [`Altair 8800 front panel schematic.md`](Altair%208800%20front%20panel%20schematic.md)
> and `docs/boards/mits-frontpanel.md`. On the Turnkey board, **reading the
> sense-switch port is exactly the event that disables the Phantom PROM** (§9) —
> the two behaviours are the same port. See the Phantom-PROM issues.

---

## 5. SIO — the 6850 serial channel

The SIO is a **Motorola 6850 ACIA** (`IC Q`). It appears as two of the 256 I/O
ports: the high-order seven address bits are compared with the SIO ADDR switch
settings; the least-significant address bit selects **Data (bit 0 = 1)** or
**Status/Control (bit 0 = 0)**.

### Status register (read at the Status/Control port)

| Bit | Name | Meaning |
|---|---|---|
| 0 | **RDRF** — Receive Data Register Full | Received data transferred to RDR. Cleared by reading RDR or master reset. **Also reads empty if DCD is LOW.** |
| 1 | **TDRE** — Transmit Data Register Empty | Set = TDR contents transferred, new data may be written. |
| 2 | **DCD** — Data Carrier Detect | Set when the `DCD` input goes LOW (carrier lost); can raise an interrupt if Rx interrupt enabled. Latched until Status then Data is read, or master reset. |
| 3 | **CTS** — Clear-to-Send | Inverse of the `CTS` input. When CTS is LOW, **TDRE is inhibited** and this bit is set to 1. Master reset does not affect it. |
| 4 | **FE** — Framing Error | Missing first stop bit (bad framing / break). Present throughout the character's time in the RDR. |
| 5 | **OVRN** — Receiver Overrun | A character was received but not read before the next arrived. Cleared by reading RDR or master reset. |
| 6 | **PE** — Parity Error | Received parity disagrees with the selected parity. |
| 7 | **IRQ** — Interrupt Request | Set (mirrors the state) whenever the `IRQ` line is LOW. |

### Control register (written at the Status/Control port)

**Bits 0-1 — Counter Divide Select** (also the master reset):

| CR1 | CR0 | Ratio |
|---|---|---|
| 0 | 0 | ÷1 (synchronised) |
| 0 | 1 | ÷16 (normal) |
| 1 | 0 | ÷64 (slow) |
| 1 | 1 | **master reset** |

After power-on / power-fail restart these bits **must be set HIGH (`11`) to
reset the SIO** before selecting a divide ratio. Master reset clears the status
register (except the external CTS/DCD conditions) and initialises Rx and Tx; it
does not affect the other control bits.

**Bits 2-4 — Word Select:**

| CR4 | CR3 | CR2 | Format |
|---|---|---|---|
| 0 | 0 | 0 | 7 bits, even parity, 2 stop |
| 0 | 0 | 1 | 7 bits, odd parity, 2 stop |
| 0 | 1 | 0 | 7 bits, even parity, 1 stop |
| 0 | 1 | 1 | 7 bits, odd parity, 1 stop |
| 1 | 0 | 0 | 8 bits, 2 stop |
| 1 | 0 | 1 | 8 bits, 1 stop |
| 1 | 1 | 0 | 8 bits, even parity, 1 stop |
| 1 | 1 | 1 | 8 bits, odd parity, 1 stop |

Word-length / parity / stop-bit changes are **not buffered** — they take effect
immediately.

**Bits 5-6 — Transmitter Control:**

| CR6 | CR5 | Function |
|---|---|---|
| 0 | 0 | RTS = HIGH, Tx interrupt disabled |
| 0 | 1 | RTS = HIGH, Tx interrupt enabled |
| 1 | 0 | RTS = LOW, Tx interrupt disabled |
| 1 | 1 | RTS = HIGH, transmit a break level (space), transmitting disabled |

**Bit 7 — Receive Interrupt Enable:** a `1` enables RDRF, Overrun, and DCD
interrupt requests.

### Baud-rate selection

The bit-rate clock is `IC G` driving a **2.4576 MHz crystal**. The rate is set
by jumpers **`S0`–`S3`** together with control-register bits `CR1`/`CR0` (the
÷16 / ÷64 / ÷1 divisor). If `CR0 = CR1 = 0` and no external rate is selected the
SIO is **transmit-only**. **Above 300 bps, remove capacitor `C1`.** With the ÷1
(synchronous) counter selected and an external clock, transmit data changes
within 1 µs of the LOW→HIGH clock edge (jitter ≈ 500 ns) and receive data is
sampled within 1 µs of the HIGH→LOW edge.

| `S3` | `S2` | `S1` | `S0` | ÷16 (normal) | ÷64 (slow) | ÷1 (sync) |
|---|---|---|---|---|---|---|
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
| X | X | X | – | External ÷16 (36 000 max) | External ÷64 (9000 max) | External (400 000 max) |
| X | X | X | X | External ÷16 (36 000 max) | External ÷64 (9000 max) | External (400 000 max) |

`X` = jumper installed. (Eberhard's notes label the middle column "/32"; the
MITS control-register table and the 4:1 value ratios both make it **÷64** — the
label is a slip.)

### Electrical interface (20 mA loop / RS-232 / TTL)

The SIO can be configured for a **20 mA current loop, RS-232, or TTL** via
jumpers and the internal cable. The cable runs from connector **J2** to the rear
panel's industry-standard **25-pin** data-communications connector.

| Mode | Jumpers |
|---|---|
| **RS-232** | `X3-X4`; `K3-P3` (only if DCD unused); `K2-P2` (only if CTS unused) |
| **TTY** (20 mA) | `X1-X2`, `K4-P5`, `K3-P3`, `K2-P2` |
| **TTL** (3.2 mA max load, 16 mA min drive) | `X2-X3`, `K4-P4`, `K3-P3`, `K2-P2`, `K1-P1` (omit if no external clock) |

**J2 SIO connector pinout** (pin 1 is on the *right* of the connector):

| J2 pin | Dir | Signal | DB-25 pin |
|---|---|---|---|
| 1 | Out | TTL RTS | — |
| 2 | Out | TTY TxD | — |
| 3 | In | TTY RxD (bias) | — |
| 4 | In | RxD | 2 |
| 5 | In | DCD | 8 |
| 6 | In | CTS | 5 |
| 7 | In | External clock | (15) |
| 8 | — | GND | 7 |
| 9 | Out | RS-232 RTS | 4 |
| 10 | Out | RS-232 / TTL TxD | 3 |

---

## 6. Interrupts

The SIO's `IRQ` can be jumpered to the bus, or left off for **polled** operation
(software reads the status register). `IRQ` is brought to jumper pad **`KA`** in
the lower-left corner:

- **No Vector-Interrupt board:** jumper `KA`→`PINT`. An SIO interrupt pulls
  `PINT` LOW until the condition clears; with the CPU interrupt enabled a
  **`RST 7`** is forced into the instruction stream. Other I/O boards may share
  `PINT` — all MITS 300 I/O boards use **open-collector** interrupt drivers.
- **With a Vector-Interrupt board:** jumper `KA`→one of `VI0`…`VI7` for the
  desired priority level. See [`88-VI-RTC.md`](88-VI-RTC.md).

---

## 7. Auto-Start circuit

On power-on (POC) or when the front-panel **Start** switch is released, the
Auto-Start logic forces the CPU to jump to an address set by the eight **START
ADDR** switches (`SW-8`, `SW-9`). The address must be a **multiple of 256** (low
8 bits = 0; the 8 switches are the high byte, bit 8 = LSB). Common settings:

| `SW-8` | `SW-9` | Boots |
|---|---|---|
| `1111` | `1100` | **HDBL** — Altair hard disk (`FC00h`) |
| `1111` | `1101` | **TURMON / UBMON** — system monitor (`FD00h`) |
| `1111` | `1110` | **MBL** — paper tape / cassette (`FE00h`) |
| `1111` | `1111` | **DBL / MDBL / CDBL** — floppy disk (`FF00h`) |

(The MITS 300 manual notes `S9` "as shown" for the MITS 300/25 and "to the left"
for the 300/55.)

### How it jams the JMP

The circuit **jams a 3-byte `JMP` instruction onto the bus in the first three
cycles after Reset**, forcing the S-100 `MEMR` signal **inactive (LOW)** so no
other memory board drives the bus while it feeds the three JMP bytes. `MEMR` is
overdriven by a **big transistor** (REV 0) or by **six open-collector inverters
in parallel** (newer board), overpowering the CPU board's `MEMR`.

A multiplexer (`IC M`, `IC N`) driven by flip-flops `T`, `Sa`, `Sb` sequences
the bytes; the flip-flops are cleared by `PRESET` and advanced by `PDBIN`
pulses:

| Signal in | State (T Sa Sb) | Byte placed on bus |
|---|---|---|
| `PRESET` | `000` | Mux + bus interface enabled; **`303` octal = `C3h` (JMP opcode)** |
| `PDBIN` | `010` | **`00h`** (low address byte) |
| `PDBIN` | `001` | **byte from START ADDR switches** (high address byte) |
| `PDBIN` | `110` | first byte of the PROM program |
| `PDBIN` | `111` | next byte of the PROM program … |

So the synthesised instruction is `C3 00 <switch-byte>` → `JMP (switch-byte)×256`.
During the three JMP bytes the LOW `Q` of flip-flop `Ta` (pin 12) holds `MEMR`
low; once the JMP completes, `Q` goes HIGH and normal memory fetches resume.

---

## 8. Front-panel (truncated) interface

On an 8800bt the board drives a **truncated** front panel showing **+5V, INT,
INTE, HLTA, and I/O** (SOUT or SIN) with **RUN/STOP** and **RESET** switches.
The bus signals `PHLTA`, `PINTE`, `PINT` are buffered to the `HALT`, `INTE`,
`INT` indicators; the `I/O` indicator is the **OR of `INP` and `OUT`**; the
`POWER` indicator monitors the +5V regulator. `PRDY` is grounded when RUN/STOP
is in **STOP**; `PRESET` is grounded momentarily by **START**, which initiates
Auto-Start.

**J1 front-panel connector pinout** (pin 1 is on the *right*):

| J1 pin | Dir | Signal | Function |
|---|---|---|---|
| 1 | Out | +5V | Regulated +5 V |
| 2 | Out | `/BHLTA` | Active-low HALT |
| 3 | Out | `/BINTE` | Active-low Interrupt Enable |
| 4 | Out | `BI/O` | Active-low Input-or-Output |
| 5 | Out | `/BINT` | Active-low Interrupt |
| 6 | — | GND | Ground |
| 7 | In | `/PRESET` | Active-low processor reset |
| 8 | In | `PRDY` | Active-high processor ready (LOW stops the CPU) |
| 9–10 | | — | Not used |

**Using a Turnkey Module in a machine that has a full front panel** (e.g. an
8800b): **remove the `M1-M2` jumper** (disconnect MWRT from the bus) and **move
`B-S` to `B-+5V`** (disable the Turnkey sense switches); leave J1 unconnected.

---

## 9. Phantom PROM — the behaviour that bites

The newer board (and any REV 0 reworked with 88-SYS-CLG) drops the onboard RAM
and adds a **Phantom** circuit. The intent: the PROMs are enabled at Reset so
the machine boots, but disable themselves later so **all 64 KB can be RAM**. The
PROMs are held enabled from Reset until the **first Input or Output to I/O port
`FEh` or `FFh`**, after which they phantom out (the same `MEMR`-overdrive trick
as Auto-Start keeps other boards off the bus while a PROM is being read). The
disable latches until a system reset clears it (`IC T`).

**The original timing was broken:** as built, *any* input or output instruction
disabled the PROMs. **Service Bulletin #007** (10/9/79) fixed it by inserting
`/PDBIN` into the port-`FE`/`FF` decode (an OR gate at `IC C` pins 4-6),
qualifying the I/O address with correct timing. A side effect of the fix: the
PROMs are now disabled **only on an *Input* instruction** from `FE`/`FF`, not on
an output.

> The MITS 300-series Theory of Operation describes this as an I/O cycle to port
> **`377`octal (`FFh`)** clocking `IC T` and permanently disabling `IC D` and
> the PROM-select logic until reset. Eberhard's notes generalise the trigger to
> ports **`FEh`/`FFh`** and record the pre/post-SB007 difference.

### Why the "read the sense switches to boot into 64K RAM" trick was clever…

Reading the sense-switch port is an `IN` from the SIO/sense address, but here
the sense port overlaps the Phantom trigger: Altair BASIC reads the sense
switches once during init (to find the Terminal port), which **also disables the
PROMs and unlocks the full 64 KB of RAM** — with no change to BASIC.

### …and the two ways it misbehaves

1. **Front-panel reset re-enables PROM.** BASIC reads the sense switches only
   once. If a running machine is reset from the front panel and then re-run from
   address 0 (front-panel Examine, or the monitor's `J` command), the PROMs are
   **re-enabled and the top 1 KB of RAM is again hidden** — but BASIC still
   assumes 64 KB. Unexpected behaviour follows.
2. **PROM code must never read the sense switches.** Doing so disables the PROM
   out from under the running code. This is why **DBL and the other Altair PROM
   loaders do *not* read the sense switches** (and therefore do not set the 2SIO
   stop bits from them), and why **MITS's own MBL — which reads the sense
   switches to pick the load port — will not run on a Turnkey Module.**

> Emulation note: model the Phantom as a one-shot latch — PROM visible at
> `FC00h`–`FFFFh` from reset until the first qualifying I/O access to `FE`/`FF`,
> then hidden (RAM shows through) until the next reset. Choosing the SB007
> behaviour (input-only trigger) matches real shipped boards and the way period
> software expects it. This is the "snoops port `FF` rather than answers it"
> note in `docs/sources.md` — the board *watches* the access, it does not drive
> data for it, so it never contends with the front panel for the port.

---

## 10. Boot Loader / Turnkey Monitor programs

The PROM set is a system program. On the MITS 300/55 (hard disk) the **Boot
Loader (`HDBL`) and the Turnkey Monitor (`TURMON`) are inseparable** — both
PROMs must be installed (`L1` = boot loader, `K1` = monitor) because they rely
on each other; each has an entry point on a 256-byte boundary reachable by
Auto-Start. Setting START ADDR to `FD00h` boots the monitor (prompt `.`);
`FF00h` boots the floppy loader directly (which prints `MEMORY SIZE`).

Pressing **START** at any time returns control to the monitor (reprints `.`).

**Runtime environment:** the monitor builds a stack topped at **`C000h`**, uses
only four nested subroutine levels (8 bytes) — the user must ensure RAM exists
there. The monitor **destroys all registers and SP** on entry; the caller must
save/restore them. Input routines accept `0`–`9`, `A`–`F`, and space; they
expect 4 or 8 hex digits, and a space terminates/delimits a number early.

### Commands

| Cmd | Form | Action |
|---|---|---|
| **M** | `Mxxxx` | Memory examine/change. Opens the location, waits for 4 hex digits, deposits + **verifies** and opens the next. A bad deposit (nonexistent / ROM / protected RAM) prints **`?`** and returns. A space closes the current location unchanged and opens the next. Any non-hex character prints `?` and returns to the monitor. |
| **L** | `L` | Run the **hard-disk boot loader**: prints `--WAITING` (drive coming on line), `--LOADING` (reading the system image), then on failure `RST CNTR` / `LOAD ERROR (dd)`. |
| **J** | `Jxxxx` | Load PC with the address and jump to the user program. |

### `L`-command error byte `(dd)`

The hard-disk loader recognises errors for a system image loaded from the disk +
controller firmware. Sectors are `0`–`23`; the firmware timeout is ≈ **1.6 s**.

| Bit | Value | Meaning |
|---|---|---|
| 0 | `01` | **DRIVE NOT READY** — drive went off line after `--LOADING` |
| 1 | `02` | **ILLEGAL SECTOR** — a sector outside 0–23 was requested (loader never does this → severe HW/memory fault) |
| 2 | `04` | **CRC ERROR IN SECTOR READ** — CRC failure over the 256 data bytes |
| 3 | `08` | **CRC ERROR IN HEADER READ** — CRC failure reading a sector header |
| 4 | `10` | **HEADER HAS WRONG SECTOR** — position error (Read Sector) |
| 5 | `20` | **HEADER HAS WRONG CYLINDER** — position error (Seek Cylinder) |
| 6 | `40` | **HEADER HAS WRONG HEAD #** — wrong side of platter |
| 7 | `80` | **FIRMWARE TIMEOUT** — controller did not respond in ≈ 1.6 s |

---

## 11. Miscellaneous signals & board modifications

**MWRT** is generated only if the **`M1-M2` jumper** is installed (all MITS 300
systems; install for a Turnkey computer, **remove for a front-panel computer**).

**PROT / UNPROT** (memory protect): as shipped, the board grounds `PROT` and
pulses `UNPROT` with clock **phase 2** to unprotect all memory as it is
accessed. Disable by removing the `O2`→`UM` and `GND`→`PM` jumpers.

**AUX CLR** is normally pulled HIGH by a resistor; optional jumpers let the
signal be used.

### Service Bulletin #007 (10/9/79)

Corrects the Phantom timing on all `200372-01` boards (§9). Rework: cut the etch
at `IC E` pin 6 (back side); add jumpers `IC C` pin 4→`IC B` pin 12, `IC C` pin
5→`IC W` pin 11, `IC C` pin 6→`IC E` pin 5. (Revises schematic `200374`,
sht 1 zone 7C and sht 2 zone D8.)

### 88-SYS-CLG / 88-SYS-CLG2 (REV 0 field modifications)

An 8800bt with a REV 0 (RAM-bearing) Turnkey Module **cannot use a full 64 KB of
S-100 RAM** (4× 16MCD or 16MCS boards) because the onboard 1 KB RAM contends.
**88-SYS-CLG** reworks the board to disable the onboard RAM and add a Phantom
circuit like the newer board. Both MITS's own bulletin and Eberhard document
this as an extensive cut/jumper list (≈ 3 component-side cuts + ~14 solder-side
operations).

- **88-SYS-CLG's Phantom is broken:** because its sense-switch decode does not
  incorporate `PDBIN`, it *randomly glitches* the Phantom flip-flop (`IC T`
  pin 5) on any input — so, e.g., TURMON reading the console can disable the
  PROMs. The RAM had to be disabled by the mod because the Phantom method does
  not block *writes* to the phantomed address (a write would hit both the
  onboard RAM and the S-100 board at that address).
- **Disabling Phantom on a reworked board** (re-enable onboard RAM, keep only
  the Auto-Start jam): a short jumper set undoes the broken Phantom without
  fully reversing CLG.
- **88-SYS-CLG2** is the further rework that makes the Phantom circuit work
  correctly — nearly identical to a newer board with SB #007 — allowing full
  64 KB of S-100 RAM once an *input* accesses port `FE`/`FF`. It requires all
  RAM ADDR switches set right (`1`); the eight 2102 SRAMs may then be removed to
  save power.

> These rework lists are hardware-only and are not reproduced switch-for-switch
> here — see the source PDF (which includes the full 88-SYS-CLG2 schematics) if
> you need to physically modify a board. For emulation, the only behavioural
> facts that matter are the ones in §1, §2, and §9.

---

## 12. Emulation checklist

- **Boot PROM window** `FC00h`–`FFFFh`, four 256-byte pages (`HDBL` / monitor /
  `MBL` / floppy loader), inserted **only until the Phantom disables it**.
- **Phantom latch:** enabled from reset; first qualifying I/O to `FE`/`FF` (an
  `IN`, on SB007/CLG2/newer boards) hides the PROM until the next reset. The
  sense-switch read *is* that trigger — model them as the same port event.
- **Terminal SIO** = 6850 at `10h`, 2SIO-Port-A-compatible (Data odd,
  Status/Control even).
- **Auto-Start** synthesises `C3 00 <START-ADDR-hi>` and boots the selected PROM
  page on reset.
- **No onboard RAM** on the modelled (newer) board; REV 0 adds 1 KB at
  `F800h`–`FBFFh`.
- Boot loaders deliberately **do not read the sense switches** — don't "fix"
  that in a model; it is the Phantom working as designed.
