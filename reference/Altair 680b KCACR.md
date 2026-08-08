# Altair 680b KCACR (Kansas City Audio Cassette Record interface)

Source: [680-KCACR Manual.pdf](#) (MITS *Altair 680b KCACR Documentation*, © MITS Inc. 1977,
reprinted June 1977)

The KCACR is the audio-cassette interface board for the **Altair 680b**, MITS's *second*
machine — a **Motorola 6800** computer, not an 8080/S-100 system. That single fact drives
everything below: the 6800 has **no separate I/O address space**, so the KCACR is **memory
mapped**. Its two registers live in the CPU's ordinary memory map and are reached with plain
`LDA`/`STA`, not `IN`/`OUT`. It replaces the 680's earlier on-board cassette circuit with a
UART-based interface that records and plays back in the **Kansas City Standard** (300 baud,
2400/1200 Hz FSK) and adds **software motor control** and **interrupt-driven** transfer — the
two things the 8800-family [88-ACR](Altair%2088-ACR%20Cassette%20Interface.md) lacks and that
the [88-UIO](88-UIO.md) later grafted onto the 8800 side. See [[altairsim-wav-cassette]],
[[altairsim-acr-acceptance-test]], [[altairsim-88uio-board]].

This is a distilled emulation reference. Kit assembly, resistor/capacitor/IC installation, the
troubleshooting waveform tables, power-supply checks, cable fabrication and the analog
modulator/filter/clock-divider circuitry are omitted except where they set a software-visible
value. What is kept: the register map, the status/control bit layout, motor control, the
interrupt model, the UART, the Kansas City modulation format, and the loader/punch PROM.

## 1. Register map (memory-mapped)

The board occupies **two consecutive bytes** of 6800 memory. Read and write of the *same*
address are *different* registers (the classic UART shape):

| Address (hex) | decimal | octal | Read (`LDA`) | Write (`STA`) |
|---|---|---|---|---|
| **`F010`** | 61456 | 360,020 | **Status** | **Control** |
| **`F011`** | 61457 | 360,021 | **Read Data** | **Write Data** |

The 680b monitor PROM and the standard KCACR jumpering place the board here; the board's own
address-decode logic (Theory §3-11/§3-12) selects on these addresses.

## 2. Status and control bits — ⚠ inverted-true convention

Both the status byte and the control byte use MITS's **active-low ("negative-true")**
convention, stated in the manual as:

> **True = Logic 0, False = Logic 1**

So a bit that is *asserted* reads/writes as **0**, and *not-asserted* is **1**. An emulator that
treats these as ordinary active-high flags gets every polarity backwards.

**STATUS — read from `F010`:**

| Bit | Function (asserted = 0) |
|---|---|
| D0 | **Read Data Available** (a received byte is waiting in `F011`) |
| D1–D6 | not used |
| D7 | **Transmit Buffer Empty** (ready to accept the next byte into `F011`) |

**CONTROL — write to `F010`:**

| Bit | Function (asserted = 0) |
|---|---|
| D0 | **Read Interrupt Enable** |
| D1 | **Write Interrupt Enable** |
| D2–D5 | not used |
| D6 | **Remote Motor Off** |
| D7 | **Remote Motor On** |

## 3. Motor control

The board can start/stop a recorder's motor through its `REMOTE` jack via an external relay
(SPST-NO, 5 V / 100 Ω min coil across connector pin 6 = +5 V and pin 9 = `MRD`, the relay-coil
driver). Because motor-on and motor-off are the two control bits D7/D6 (asserted = 0):

| Action | Machine language (`STA F010`) | BASIC |
|---|---|---|
| Motor **ON** | store **`7F`** (D7 = 0) | `POKE 61456,127` |
| Motor **OFF** | store **`BF`** (D6 = 0) | `POKE 61456,191` |

After starting the motor, wait for the mechanism to stabilize before transferring (the manual
uses a delay loop; recording starts after ≥5–30 s of leader).

## 4. Interrupts

The KCACR pulls the 6800 **`IRQ`** line LOW when an *enabled* status condition becomes true —
Read Data Available (if Read Interrupt Enable is set) or Transmit Buffer Empty (if Write
Interrupt Enable is set). The bit patterns written to `F010` (Table 2-A):

| Write to `F010` | BASIC `POKE 61456,` | Effect |
|---|---|---|
| `FE` | 254 | Read Interrupt Enable |
| `FD` | 253 | Write Interrupt Enable |
| `BF` | 191 | Motor Off **and reset interrupts** |

Servicing sequence (from the manual's own model):

1. Software must run with interrupts masked until set up; **`CLI`** (opcode `0E`) clears the
   6800 interrupt mask to enable `IRQ`.
2. On `IRQ` the 6800 finishes the current instruction, pushes its registers, sets the mask, and
   vectors through **`FFF8`/`FFF9`** — which the **680b monitor PROM points at `0100`**. The
   interrupt handler therefore lives at `0100`.
3. **Reading the status or the data channel, or issuing Motor Off, resets the interrupt-enable
   latches** and drops the request. The handler verifies status, moves the data, then restores
   registers (re-enabling interrupts before pulling them back off the stack if more are wanted).

## 5. The UART

The serial engine is an **SMC COM2017/H** (the "H" high-clock part of the COM2502/COM2017
family) — the **same 1602-family single-chip UART** documented in
[com2502.md](com2502.md) and used by the [88-SIO](88-SIO%20Rev%200%20%26%201.md). 40-pin DIP,
fully double-buffered, framing set entirely by hard-wired/strobed control pins (no software
mode register). For the KCACR the framing is **8 data bits, no parity, 2 (or more) stop bits**,
driven by a **16× clock** (4800 Hz = 16 × 300 baud). The board-level status bits in §2 are
this chip's **TBMT** (pin 22) and **RDA** (pin 19) brought to the bus; **TDS** strobes a
transmit byte, **RDAR** resets Read-Data-Available after a read.

## 6. Kansas City Standard modulation

The recorded waveform is the **Kansas City Standard** (KCS), reprinted in the manual from
*BYTE*, February 1976:

- **300 baud**, nominal tape speed **1.875 in/s** (4.75 cm/s).
- A **mark (logical 1)** = **8 cycles of 2400 Hz**.
- A **space (logical 0)** = **4 cycles of 1200 Hz**.
- A character = one **space** start bit + **8 data bits** (LSB first) + **2 or more mark** stop
  bits; the inter-character gap is an arbitrary run of mark (2400 Hz).

This is the *same* KCS the [88-UIO](88-UIO.md) selects with SW-1 = ON (2400/1200); it differs
from the plain 88-ACR's MITS 2400/1850 tone pair. Emulating the *bytes* on tape (the `.TAP`/WAV
path) does not require reproducing the tones — see [[altairsim-wav-cassette]].

## 7. KCACR loader/punch PROM

An optional PROM in **socket V** of the 680b main board, beginning at **`FD00`**, that loads
and dumps memory over the KCACR in **Motorola (S-record) format**:

- **Punch (save):** `.J FD74`, then answer the two `?` prompts with the **start** and **end**
  addresses (4 hex digits each). Data is dumped as S-records terminated by an **`S9`** record,
  followed by **50 trailing nulls**. (`.J` is the 680b monitor's *jump* command; the leading
  `.` is the monitor prompt.)
- **Load:** `.J FD00`, then PLAY the tape. Ends at the `S9` record. On error it prints one
  character to the terminal: **`C`** = checksum or non-hex character; **`M`** = memory error
  (write to a nonexistent/nonfunctioning location).
- **⚠ Never punch or load `0000`–`00FF`** — that is the monitor's and the PROM's own stack and
  work area. Punching it is harmless, but *loading* it back corrupts the loader mid-run
  (usually a checksum error, but the result is undefined).

## 8. Emulation notes / gotchas

- **It is memory-mapped, not ported.** Model `F010`/`F011` as memory addresses the CPU reaches
  with `LDA`/`STA`; there is no 6800 `IN`/`OUT`. This is unlike every 8800/S-100 serial board
  in the tree.
- **Every bit is active-low.** Assert = 0. RDA, TBE, both interrupt enables and both motor bits
  follow the "True = Logic 0" rule. Do not reuse an active-high 6850/2SIO status model.
- **Two addresses that alias by direction.** `F010` read ≠ `F010` write; `F011` read ≠ `F011`
  write. Wire read-status/write-control to one decode and read-data/write-data to the other.
- **Interrupt latch auto-clears on access.** Reading `F010`/`F011`, or a Motor-Off write, drops
  `IRQ`. The 680b IRQ vector `FFF8/FFF9` is monitor-supplied as `0100`.
- **Same UART as the 88-SIO.** If a COM2502/1602 UART model already exists
  (`src/chips/uart1602.*`), the KCACR is that chip in 8N2 at a 4800 Hz clock, with a different
  (memory-mapped, active-low) host wrapper.
- **Don't invent tones.** A byte-level tape model needs the KCS *framing* (300 baud, 8 data
  bits, LSB first), not the 2400/1200 Hz audio, unless the WAV path is in play — see the
  hardware-fidelity rule [[altairsim-cuts-write-path]].
- **Interference note (real hardware):** with a 680b-UI/O board present, MITS says remove IC A1
  on the UI/O; and `PLAY IN` and `RECORD OUT` should not both be cabled to the recorder at once.
  Neither affects an emulator but both explain period wiring instructions.

## 9. Key facts at a glance

| | |
|---|---|
| Machine | Altair **680b** (Motorola **6800**), memory-mapped I/O |
| Status/Control register | **`F010`** (61456 / 360,020q) — read = status, write = control |
| Data register | **`F011`** (61457 / 360,021q) — read = RX, write = TX |
| Bit convention | **active-low: True = Logic 0** |
| Status D0 / D7 | Read Data Available / Transmit Buffer Empty |
| Control D0 / D1 | Read Int Enable / Write Int Enable |
| Control D6 / D7 | Remote Motor Off / Remote Motor On |
| Motor ON / OFF | store `7F` / `BF` (POKE 61456,127 / 191) |
| Int enable writes | `FE` read-int, `FD` write-int, `BF` motor-off+reset |
| IRQ vector | `FFF8/FFF9` → **`0100`** (monitor PROM) |
| UART | SMC **COM2017/H** (COM2502 family), 8N2, 16× clock (4800 Hz) |
| Modulation | **Kansas City Standard** 300 baud: mark = 8×2400 Hz, space = 4×1200 Hz |
| Loader PROM | socket V, **`FD00`**; punch `.J FD74`, load `.J FD00`; Motorola S-records |
| Loader errors | `C` checksum/non-hex, `M` memory error |
