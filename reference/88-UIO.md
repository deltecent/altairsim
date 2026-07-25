# MITS 88-UIO Universal Input/Output

Source: [88-UIO.pdf](#)

MITS, Inc. — "Using the 88-UIO Board" plus the four-sheet Attaché schematic set
(drawing 250272A). This is a distilled emulation reference for the 88-UIO. Kit-assembly
steps, parts lists, PCB layouts, and alignment/trim-pot procedures from the original
document are intentionally omitted; only the information needed to emulate the card in
software is kept.

---

## 1. What the card is

The 88-UIO puts **two independent, separately-addressed functional sections on one S-100
board**:

1. **A serial port** built on a Motorola **6850 ACIA** (schematic IC **K**) — the same chip
   and the same guest-visible register model as one channel of an **88-2SIO**. It carries a
   7-position baud-rate jumper and a TTY/RS-232 line-driver option. This is the "main I/O
   port" — the console serial line.
2. **An audio-cassette (ACR) section** built on an **AY-5-1013A UART** (schematic IC **L**,
   a 1602/COM2502-family part with **inverted** ready flags) driving an FSK cassette modem.
   It is functionally an **88-ACR** — 300 baud, ports at 006/007 octal — with two things the
   plain 88-ACR does not have: **motor control** and a **switch-selectable modulation
   standard** (MITS *or* Kansas City).

**Key consequence for emulation.** The two sections share only the address-decode PROM and
the power supply; to the guest they are two ordinary cards that happen to live on one board.
The serial section is a 6850 (see [`6850.md`](6850.md) and
[`Altair 2SIO User's Manual.md`](Altair%202SIO%20User%27s%20Manual.md)); the cassette
section is an inverted-status UART with an FSK modem (see
[`Altair 88-ACR Cassette Interface.md`](Altair%2088-ACR%20Cassette%20Interface.md), whose
register model, status polarity, framing, and tape format the ACR section shares). This
reference documents only what the 88-UIO adds or changes.

---

## 2. The four-position configuration switch

A single 4-position DIP selects the board's options:

| Switch | OFF | ON |
|--------|-----|-----|
| **SW-1** — modulation | **MITS standard**: 2400 Hz = 1, **1850 Hz** = 0 | **Kansas City standard**: 2400 Hz = 1, **1200 Hz** = 0 |
| **SW-2** — serial address | **020/021 octal** (0x10/0x11) — *main I/O port* | **030/031 octal** (0x18/0x19) — *auxiliary port*, or when another board is the console |
| **SW-3** — ACR address | **006/007 octal** (0x06/0x07) — *normal* | **016/017 octal** (0x0E/0x0F) — *auxiliary ACR, not normally used* |
| **SW-4** — unused | — | — |

The defaults (all OFF) reproduce the standard Altair layout: the serial port answers at
0x10/0x11 exactly where 88-2SIO Port A does, and the cassette answers at 0x06/0x07 exactly
where an 88-ACR does. MITS cassette software (4K/8K/Extended BASIC) therefore boots on the
88-UIO unchanged.

---

## 3. Serial section (6850 ACIA, IC K)

The serial section **is** a 6850 and behaves exactly as one 88-2SIO channel does; its two
consecutive ports split on address bit A0:

| A0 | Port (octal, SW-2 OFF) | `IN` | `OUT` |
|----|------------------------|------|-------|
| 0  | **020** (0x10) | 6850 **status** register | 6850 **control** register |
| 1  | **021** (0x11) | receive data register (RDR) | transmit data register (TDR) |

With SW-2 ON the pair moves to **030/031** (0x18/0x19). The register bit layouts,
divide-select, word-select, transmitter-control, and master-reset behavior are the standard
MC6850's — see [`6850.md`](6850.md). Unlike the cassette section, the 6850's status flags are
**active-high** (RDRF = 1 means a byte is available; TDRE = 1 means the transmitter is ready).

**Baud rate — SK1, a 7-position jumper** (one position connected):

| Position | Baud | Jumper |
|----------|------|--------|
| 1 | 110   | 1–14 |
| 2 | 300   | 2–13 |
| 3 | 1200  | 3–12 |
| 4 | 2400  | 4–11 |
| 5 | 4800  | 5–10 |
| 6 | 9600  | 6–9  |
| 7 | 19200 | 7–8  |

**Line interface — SK2:** a 20-pin socket takes an 18-pin plug oriented one of two ways for
**TTY (20 mA current loop)** or **RS-232**. Serial connector P1 carries CTS/Data-In, Gnd,
Data-In/Gnd, Data-Out, RTS/+Data-In, DTR, and DCO, wired for MITS's DB-25 pinout.

---

## 4. Cassette (ACR) section (AY-5-1013A UART, IC L)

Two consecutive ports split on A0, at **006/007 octal** normally (SW-3 OFF) or 016/017
(SW-3 ON):

| A0 | Port (octal, SW-3 OFF) | `IN` | `OUT` |
|----|------------------------|------|-------|
| 0  | **006** (0x06) | **status** word (below) | **motor control** (below) |
| 1  | **007** (0x07) | received data byte | transmit data byte |

### 4.1 Status word — `IN` from port 006

Like the 88-SIO/88-ACR, the two "ready" flags are **active-LOW** — a 0 means ready:

| Bit | Logic LOW (0) means | Logic HIGH (1) means |
|-----|---------------------|----------------------|
| D7  | **transmit buffer empty** — the UART can take another byte to send | transmitter busy |
| D0  | **received data byte ready** — a byte is available to `IN` | no byte available |

(Bits D1–D6 follow the 1602-family UART's overflow/framing/parity error flags, as on the
88-ACR — see [`Altair 88-ACR Cassette Interface.md`](Altair%2088-ACR%20Cassette%20Interface.md)
§3.) The polling idioms are the ACR's:

- **Wait for transmit ready:** `IN 006 / RLC / JC back` — loop while D7 = 1 (busy).
- **Wait for receive ready:** `IN 006 / RRC / JC back` — loop while D0 = 1 (empty).

### 4.2 Motor control — `OUT` to port 006

This is what the 88-UIO adds over a plain 88-ACR. `OUT` to the cassette status/motor port
drives a relay wired to the recorder's "Remote" jack (P2 pins 6–7):

| Written value | Bits | Effect |
|---------------|------|--------|
| **`OUT 6,127`** | D7 **low** (0–6 high) | **motor ON** |
| **`OUT 6,191`** | D6 **low** (0–5, 7 high) | **motor OFF** |

The relay **contacts are normally closed after power-up** (i.e. the motor line is enabled
by default). This is a distinct, built-in scheme — *not* the plain 88-ACR's optional
control-bit-D0 add-on. Framing is **8 data bits, no parity** (a tape byte is a clean 8-bit
value), and the section is wired for **300 baud** — the ACR's practical maximum. Byte timing
is the ACR's: 3.33 ms/bit, ≈30 bytes/s.

### 4.3 ACR connector (P2)

| Pin | Signal |
|-----|--------|
| 1 | Record Out → recorder "Mic"/"Aux" |
| 2 | Play In ← recorder "Spkr"/"Ear"/"Line Out" |
| 4 | Ground (shield) |
| 6, 7 | Motor-control relay contacts → recorder "Remote" |

---

## 5. Modulation standards (SW-1) and the Kansas City principle

The 88-UIO's modem can lay down and recover **either** of two FSK standards, chosen by SW-1.
Both key logic 1 (mark) to **2400 Hz**; they differ in the space tone:

| SW-1 | Standard | Mark (1) | Space (0) |
|------|----------|----------|-----------|
| OFF | **MITS** | 2400 Hz | **1850 Hz** |
| ON  | **Kansas City** | 2400 Hz | **1200 Hz** |

This is a **hardware switch, not a guess**: the card records and reads exactly the one
standard SW-1 selects, and audio in the other standard is not something a single setting can
also read. (The plain 88-ACR is MITS-only and its PLL physically cannot capture a 1200 Hz
Kansas City space tone — see the ACR reference §7/§11 (MEASURED). The 88-UIO's SW-1 = ON
position is the hardware that *does* read Kansas City.)

**Kansas City Standard principle** (BYTE, February 1976 — reprinted in the manual):

- A **mark** (logic 1) is **eight cycles of 2400 Hz**; a **space** (logic 0) is **four
  cycles of 1200 Hz** — both tones occupy the same bit time (a whole number of cycles, the
  tones in a 2:1 ratio). Nominal bit rate 300 baud; the technique tolerates tape-speed
  variation because the bit clock is an integer multiple of the tone.
- A character is **a space start bit, eight data bits (LSB first), then two or more mark
  stop bits**; unused data bits (where fewer than 8 are sent) are mark. The inter-character
  interval is an unspecified run of mark. Data blocks are preceded by ≥5 s of mark, and the
  first block begins no sooner than 30 s from the start of the clear leader.

In the simulator these two standards are the existing `fsk300_1850` (MITS) and `kcs300`
(Kansas City) tape formats; SW-1 selects which single format the card's modem presents (see
`src/host/tapemodem.h`).

---

## 6. Machine-language I/O (the ACR test/utility programs)

The manual ships the standard ACR reference programs, all using ports **006** (status) /
**007** (data) and test byte **125 octal** (0x55) — useful as emulation test vectors:

- **Output Test Program** (origin 200 octal): `IN 006 / RLC / JC` (wait D7), `MVI A,125 /
  OUT 007`, loop — records 125 octal continuously.
- **Input Test Program** (origin 000): polls D0 via `RRC`, reads `IN 007`, `XRI 125` to
  verify.
- **Write Program** (38 bytes, origin 017,000): `LXI H,start / LXI B,end`, write test byte
  000, then loop `IN 006 / RLC / JC` (wait D7), `MOV A,M / OUT 007`, `INX H`, compare HL:BC,
  self-loop at 017,375 when done.
- **Read Program** (48 bytes, origin 017,000): hunt for the test byte (`IN 006 / RRC / JC`
  for D0, `IN 007`, `CPI 000`), then store to memory until HL = BC, ending in a self-loop.

These are the 88-ACR's programs verbatim, confirming the cassette section is guest-compatible
with the standalone card.

---

## 7. Emulation checklist (summary of load-bearing facts)

- **Two independent sections on one board.** Serial = 6850 (active-high status); cassette =
  1602-family UART (active-**low** ready flags). They share nothing the guest can observe.
- **Serial ports:** 020/021 octal (0x10/0x11) normally, 030/031 (0x18/0x19) with SW-2 ON.
  7-position baud jumper 110…19200. TTY or RS-232 line drivers (SK2).
- **Cassette ports:** 006/007 octal (0x06/0x07) normally, 016/017 (0x0E/0x0F) with SW-3 ON.
  300 baud, 8-bit data. Status D7 low = TX empty, D0 low = RX ready.
- **Motor control (new vs 88-ACR):** `OUT 6,127` (D7 low) = motor ON; `OUT 6,191` (D6 low) =
  motor OFF; relay contacts normally closed at power-up.
- **Modulation (new vs 88-ACR):** SW-1 selects one standard — MITS 2400/1850 (OFF) or Kansas
  City 2400/1200 (ON). Mark is always 2400 Hz; idle line = mark.
- **Defaults reproduce the standard Altair layout** (serial at 0x10 = 2SIO Port A, cassette
  at 0x06 = 88-ACR), so MITS BASIC boots on the 88-UIO with no changes.
