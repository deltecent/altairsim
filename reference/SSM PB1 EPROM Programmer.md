# SSM PB1 EPROM Programmer

Source: [pb1.pdf](#) (SSM Microcomputer Products, 2116 Walsh Avenue, Santa Clara CA;
"PB1 2708/2716 Programmer & 4K/8K EPROM Board"; software © Solid State Music 1978).
Read as page images — the scan has no text layer.

The **SSM PB1** is an S-100 board that does two unrelated jobs on one card:

1. an **EPROM programmer** — two sockets that program a **2708** (U22) or a **5-volt 2716**
   (U23), with the programming voltage generated on-board (no external supply); and
2. an **on-board read-only EPROM area** — four sockets (U11–U14) holding **4K of 2708** or
   **8K of 2716**, mappable to any 4K/8K boundary above `8000H`.

SSM is the former **Solid State Music** ("we used to be Solid State Music; we still make the
blue boards"). The programmer half is software-driven: an `OUT` to one control port arms the
board and picks 2708-vs-2716 timing, then the CPU **writes bytes to the socket's memory
addresses** and the board stretches each write into a programming pulse by holding the S-100
`READY` line. Because a write becomes a multi-millisecond wait, **the CPU must have wait
states enabled** or the pulse is too short to program a cell.

This is a distilled emulation reference: the port/socket address decode, the control-port bit
meaning, the programming handshake and pulse timing, the on-board-PROM address map, and the
wait/ready wiring. It is emulated as the **`pb1`** board (`src/boards/ssm-pb1.*`,
`docs/boards/ssm-pb1.md`, example `examples/pb1/`); the programming-pulse timing, the
S-100 READY/wait handshake and the SW1/TL497 voltage rail are not modeled (a bus write just
lands — see the board doc's *Limitations*). The manual's four driver routines (sections
4.2–4.5) are transcribed verbatim into `examples/pb1/*.{ASM,HEX}` and run against the board.

---

## At a glance

| | |
|---|---|
| Bus | S-100, 8080/Z80 host |
| Programmer sockets | **U22 = 2708**, **U23 = 2716 (5 V)** |
| Programming socket window | a **4K memory block** on any 4K boundary (`0000`–`F000`), set by **SW2-1..4** = A15–A12 |
| Control (arm/type) port | one **I/O port** at any `x0H` address (`00`,`10`,…,`F0`), set by **SW2-5..8** = A7–A4 |
| On-board PROM area | **U11–U14**, 4K (2708) or 8K (2716), any boundary **above `8000H`**, set by **SW3** |
| Wait states | jumper-selectable **0–4**, read cycles only |
| READY line | S-100 **PRDY (pin 3)** via jumper U-V, or **XRDY (pin 72)** via U-W (factory) |
| Prog. voltage | **+26.5 V** from a TL497 DC-DC converter (U32), gated by SW1 + LED (D2) |
| Power | +8 V @ 500 mA, +16 V @ 25 mA, −16 V @ 5 mA (all "less EPROM") |

---

## Addressing

The board has three independent address decodes (Theory §6.2):

- **a)** the 4K memory window for the two **programming sockets** (U22/U23);
- **b)** the 4K/8K memory window for the **on-board PROM** area (U11–U14);
- **c)** one **I/O port** that arms programming and selects 2708-vs-2716.

### Programming-socket window — SW2 positions 1–4

The board reserves a **4K block** of memory for the programming sockets, placeable on any 4K
boundary. Positions 1–4 of DIP switch **SW2** (chip U25) decode A15–A12. `OFF` = switch open,
`ON` = switch closed; a closed switch selects a `1` in that address bit.

| Start (hex) | Dec | SW2-1 (A15) | SW2-2 (A14) | SW2-3 (A13) | SW2-4 (A12) |
|---|---|---|---|---|---|
| 0000 | 0     | OFF | OFF | OFF | OFF |
| 1000 | 4096  | OFF | OFF | OFF | ON  |
| 2000 | 8192  | OFF | OFF | ON  | OFF |
| 3000 | 12288 | OFF | OFF | ON  | ON  |
| 4000 | 16384 | OFF | ON  | OFF | OFF |
| 5000 | 20480 | OFF | ON  | OFF | ON  |
| 6000 | 24576 | OFF | ON  | ON  | OFF |
| 7000 | 28672 | OFF | ON  | ON  | ON  |
| 8000 | 32768 | ON  | OFF | OFF | OFF |
| 9000 | 36864 | ON  | OFF | OFF | ON  |
| A000 | 40960 | ON  | OFF | ON  | OFF |
| B000 | 45056 | ON  | OFF | ON  | ON  |
| C000 | 49152 | ON  | ON  | OFF | OFF |
| D000 | 53248 | ON  | ON  | OFF | ON  |
| E000 | 57344 | ON  | ON  | ON  | OFF |
| F000 | 61440 | ON  | ON  | ON  | ON  |

U26 (74LS136 XOR) decodes this 4K boundary → chip-select for U22/U23. A read to any address
in this block also **resets the programming flip-flop** (see *Programming*), which is how the
routines "reset PB1" and turn the LED off.

### Control-port address — SW2 positions 5–8

Programming is armed by writing to a single **output port**, placeable at any one of 16
addresses. Positions 5–8 of **SW2** decode A7–A4.

| Port (hex) | Dec | SW2-5 (A7) | SW2-6 (A6) | SW2-7 (A5) | SW2-8 (A4) |
|---|---|---|---|---|---|
| 00 | 0   | OFF | OFF | OFF | OFF |
| 10 | 16  | OFF | OFF | OFF | ON  |
| 20 | 32  | OFF | OFF | ON  | OFF |
| 30 | 48  | OFF | OFF | ON  | ON  |
| 40 | 64  | OFF | ON  | OFF | OFF |
| 50 | 80  | OFF | ON  | OFF | ON  |
| 60 | 96  | OFF | ON  | ON  | OFF |
| 70 | 112 | OFF | ON  | ON  | ON  |
| 80 | 128 | ON  | OFF | OFF | OFF |
| 90 | 144 | ON  | OFF | OFF | ON  |
| A0 | 160 | ON  | OFF | ON  | OFF |
| B0 | 176 | ON  | OFF | ON  | ON  |
| C0 | 192 | ON  | ON  | OFF | OFF |
| D0 | 208 | ON  | ON  | OFF | ON  |
| E0 | 224 | ON  | ON  | ON  | OFF |
| F0 | 240 | ON  | ON  | ON  | ON  |

**⚠ Only A4–A7 are decoded.** U18 (7433 open-collector NOR) requires **A0–A3 = 0**, and U24
(74LS136 XOR) decodes A4–A7 against SW2-5..8. So the port answers only at an address whose
**low hex digit is 0** (`00`, `10`, `20`, …). The SSM sample software uses port **`10H`**
(`CPORT EQU 10H`).

**⚠ The control-port address must differ from the high-order byte of the programming-socket
window** (manual's own NOTE), or the two decodes collide.

### On-board PROM area — SW3

U11–U14 hold **4K of 2708** or **8K of 2716**, mappable to any 4K (2708) or 8K (2716) boundary
**above `8000H`** (A15 must be 1 to enable the U16 dual-1-of-4 decoder). Set the type with
**SW3-1** and the boundary with SW3-2..7. Be sure the EPROM-type jumpers (below) match.

**2708 (4K), SW3-1 = OFF:**

| Addr | SW3-2 | SW3-3 | SW3-4 | SW3-5 | SW3-6 | SW3-7 |
|---|---|---|---|---|---|---|
| 8000 | OFF | ON  | ON  | OFF | OFF | OFF |
| 9000 | ON  | OFF | ON  | OFF | OFF | OFF |
| A000 | OFF | ON  | OFF | ON  | OFF | OFF |
| B000 | ON  | OFF | OFF | ON  | OFF | OFF |
| C000 | OFF | ON  | OFF | OFF | ON  | OFF |
| D000 | ON  | OFF | OFF | OFF | ON  | OFF |
| E000 | OFF | ON  | OFF | OFF | OFF | ON  |
| F000 | ON  | OFF | OFF | OFF | OFF | ON  |

(SW3-2/3 are complementary and pick A12 — the odd/even 4K within an 8K pair; SW3-4..7 are
one-hot and pick the 8K region.)

**2716 (8K), SW3-1 = ON:**

| Addr | SW3-2 | SW3-3 | SW3-4 | SW3-5 | SW3-6 | SW3-7 |
|---|---|---|---|---|---|---|
| 8000 | OFF | OFF | ON  | OFF | OFF | OFF |
| A000 | OFF | OFF | OFF | ON  | OFF | OFF |
| C000 | OFF | OFF | OFF | OFF | ON  | OFF |
| E000 | OFF | OFF | OFF | OFF | OFF | ON  |

**EPROM-type jumpers (§3.5):**

| | 2708 | 2716 |
|---|---|---|
| jumpers | A-E (A10), B-D (A11), F-H (−5 V), J-K (+12 V) | B-E (A11), C-D (A12), F-G (+5 V), J-L (A10) |

**Socket auto-disable (§3.8):** an unused PROM socket does **not** drive the data bus, so the
board is never committed to the full 4K/8K — RAM can live at an address inside the PB1 window
if no PROM occupies that socket. To use **only** the two programming sockets and disable all
four on-board sockets, set **SW3-4,5,6,7 = OFF**.

---

## Programming

### Control port — arm + select type

An `OUT` to the control port (SW2-5..8 address) does two things at once:

- **Sets the programming flip-flop** (U10 pin 12 / U21 pin 8). The board detects a valid I/O
  address (U8 pin 11) plus **`SWO` output status** (U8 pin 9); the LED (D2) lights while the
  flip-flop is set.
- **Latches D0/D1** into U19 (74LS175) to choose the pulse timing:

  | Written value | Mode | Pulse source | Pulse width |
  |---|---|---|---|
  | `01H` (D0=1) | **2708** | U27 pin 5 → Q1/Q2/Q3 level-shift to high-voltage on U22 pin 18 | **0.5–0.7 ms** (~0.6 ms) at **+26 V** |
  | `02H` (D1=1) | **2716** | U27 pin 13 → U23 pin 18 | **45–55 ms** (~50 ms) at **+5 V** |

The flip-flop is **reset** by S-100 **power-on-clear (bus pin 99)** *or* by a **memory read
cycle to the programming-socket window** (U26 → U8 pin 3 on a `SMEMR` cycle). Reset = LED off,
board back in the harmless read-only state.

### The programming pulse (write handshake)

With the flip-flop set, a **memory write (`SWO`) to any address in the socket window** drives
U9 pin 6 high, which triggers U28 (setup time). At the end of setup, U27 fires the pulse
(pin 5 for 2708, pin 13 for 2716) onto pin 18 of U22/U23. The **trailing edge of the pulse**
triggers U28 pin 12, a negative pulse that **releases the processor** to proceed to the next
byte — this is the data-hold time. Throughout, the board holds the CPU with a wait state via
`READY`, so a single `STAX D`/`MOV M` becomes a full ~0.6 ms (2708) or ~50 ms (2716) pulse.

Program-pulse waveforms the manual gives for scope verification:

```
2708 (U22 pin 18):  +26 V ___⎍___   0.5–0.7 ms high
2716 (U23 pin 18):  +5 V  ___⎍___   45–55 ms high
```

### The SSM driver routines

The manual ships four short 8080 routines, all `ORG`'d in the `0100H`–`0180H` region, all
ending with `JMP` to the SSM 8080 monitor entry at **`F021H`** (patchable: low byte at `011F`,
high byte at `0120`; or replace with `HLT` = `76` at `011E`). Register convention:

- **A** = data byte passed to the programmer
- **B** = number of repeated programming cycles (2708 = `FFH`; 2716 = `01H`)
- **C** = size; bytes = 256 × (C+1) (2708 → C=`03` = 1K; 2716 → C=`07` = 2K)
- **DE** = PROM card address (the socket window)
- **HL** = source data address to be copied

The data source's start address is patched at program locations `010CH` (low) / `010DH`
(high); it may be anywhere in memory **except** the socket window, and may even be the
on-board PROM area (so the board can copy one EPROM to another).

**2708 programmer (§4.2), `ORG 0100H`, control port `10H`, socket `D000H`, source `4000H`:**

```asm
PROG0: MVI  A,01        ; 01 = 2708 mode
       OUT  CPORT       ; preset board (arm; latch type; LED on)
       MVI  B,0FFH      ; 256 programming cycles
       MVI  C,03        ; size: 256*(3+1) = 1024 bytes
PROG1: LXI  D,PROM      ; DE -> programming socket
       LXI  H,RAM       ; HL -> source data
PROG2: MOV  A,M
       STAX D           ; write byte -> one programming pulse (CPU waits)
       INX  D
       INX  H
       MOV  A,D
       ANA  C
       ORA  E
       JNZ  PROG2       ; loop over this pass
       DCR  B
       JNZ  PROG1       ; repeat the whole PROM B times
       DCX  D
       LDAX D           ; read socket -> RESET PB1 (LED off)
       JMP  MONIT
```

The **2716 routine (§4.3)** is identical except `MVI A,02` (2716 mode), `MVI B,01` (one pass),
`MVI C,07` (2K). Because the 2708 needs many short pulses and the 2716 one long pulse per byte,
total programming time is roughly **160 s for a 2708** and **100 s for a 2716**.

Two verify helpers are also given: an **erase check** (§4.4, `ORG 140H` — reads the socket and
prints `P`/`F` depending on whether every byte is `FFH`) and a **copy verify** (§4.5, `ORG
180H` — compares source RAM against the programmed EPROM, prints `P`/`F`); both call the user
console-out routine (`CO EQU F009H` in the examples).

### Step-by-step (§4.1) and the SW1 safety interlock

`SW1` is an SPST switch that **gates the +26.5 V onto the sockets** — a manual defeat against
accidental programming. The procedure: sockets empty and `SW1` off and LED off; put the data
in memory; insert the EPROM (U22 for 2708, U23 for 2716) and verify it is erased; load/patch
the driver; **turn SW1 on**; run the routine at `0100H`; the LED lights during programming and
goes out when done; **turn SW1 off**; verify.

**⚠ Wait states are mandatory for programming.** Without them the "pulse" is a bus cycle wide.
The manual calls this out specifically for the **North Star Z80 CPU** (§3.11): enable the J2
wait-state option (jumper 1W) or "the programming time will be a couple of seconds which will
not program an EPROM."

---

## Wait states and READY

**Wait states (§3.9):** 0–4, **read cycles only** (either the programming sockets or the
on-board PROM area). Jumpers:

| Wait states | Jumpers |
|---|---|
| 0 | R-S |
| 1 | S-T, Q-P *(factory example)* |
| 2 | S-T, Q-O |
| 3 | S-T, Q-N |
| 4 | S-T, Q-M |

**READY line (§3.10):** the PB1 asserts the CPU `READY` for both programming and read wait
states. Select which S-100 ready line by jumper: **PRDY (bus pin 3)** = U-V, or **XRDY (bus
pin 72)** = U-W (the factory/circled choice).

**Wait circuitry (Theory §6.2):** on a **read**, U20 (74LS173) acts as a 4-bit shift register —
`PSYNC` resets it and Φ2 shifts a `1` through; the jumper taps the chosen stage for 0–4 waits
(the `T`–`S` jumper must be present for read waits). On **programming**, U20 is inhibited and
the wait ends at the completion of the data-hold time (rising edge on U29 pin 11). The two
D-flip-flops in U29 (74LS74) — pin 9 = programming waits, pin 5 = read waits — are preset by
`PSYNC` and combined by U21 into the wait-request, enabled/disabled by U18 pin 10.

---

## Programming voltage

A switching supply built around **U32 (TL497 DC-DC converter)** generates the programming
rail; the current is stored in **C1 (1000 µF)** charged to **+26.5 V** (trimmed by R36; verify
across C1's + lead to ground). SW1 passes it to the PROMs. From there the +26.5 V drives a
pulse-shaping stage (Q1/Q2/Q3, for 2708 high-voltage pulses) and an enable stage (Q4/Q5, for
2716). Q6/Q7/Q8 hold the `CS` pin of U22 at **+12 V during programming, +5 V when not
selected, 0 V when selected for reading**.

---

## Chip complement (Theory §6.1)

| Ref | Part | Role |
|---|---|---|
| U1–U3 | 74LS367 hex tri-state | buffer address lines onto the card, `RDY` onto the bus |
| U4–U6 | 74LS367 hex tri-state | buffer data bus + address-decode / `SWO` status |
| U7 | 74LS04 hex inverter | signal buffering, LED drive |
| U8 | 74LS10 triple 3-in NAND | enable/reset programming flip-flop; data-out enable for reads |
| U9 | 74LS11 triple 3-in AND | data-setup one-shot enable; wait enable (U20); `SMEMR` buffer |
| U10 | 74LS10 triple 3-in NAND | programming flip-flop; socket `CS`; enable PROM decoder U16 |
| U11–U14 | sockets | 4K 2708 / 8K 2716 on-board read-only memory |
| U15 | 74LS30 8-in NAND | detects an `FF` (all-ones) byte |
| U16 | 74LS139 dual 1-of-4 | chip selects for the U11–U14 PROM block |
| U17 | DIP switch (**SW3**) | on-board PROM address select |
| U18 | 7433 quad 2-in NOR (o.c.) | decode 4 LSBs of the output-port address; enable `RDY` buffer |
| U19 | 74LS175 quad latch | latch D0/D1 to select 2708 vs 2716 timing |
| U20 | 74LS173 4-bit register | read-cycle wait-state shift register |
| U21 | 74LS00 quad 2-in NAND | programming flip-flop; buffer Φ2; gate wait signals to U3 |
| U22 | socket | **2708** programming socket |
| U23 | socket | **2716** programming socket |
| U24 | 74LS136 quad 2-in XOR | decode the programming flip-flop's I/O port (A4–A7) |
| U25 | DIP switch (**SW2**) | upper 4 = socket window address; lower 4 = control-port address |
| U26 | 74LS136 quad 2-in XOR | decode the 4K block for the programming sockets |
| U27, U28 | 74LS123 dual one-shot | set-up, hold, and programming-pulse timing (2708/2716) |
| U29 | 74LS74 dual flip-flop | wait-state control for read and programming cycles |
| U30 | regulator | +5 V |
| U31 | regulator | +12 V |
| U32 | TL497 | DC-DC converter → +26.5 V programming voltage |
| U33 | regulator | −5 V |
| Q1–Q8 | transistors | Q1–Q3 2708 pulse shaping; Q4–Q5 2716 enable; Q6–Q8 U22 `CS` control |
| SW1 | SPST | manual gate for the +26.5 V programming rail (safety) |
| D2 | LED | lit while the programming flip-flop is set |
