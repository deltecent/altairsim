# Processor Technology Sol-20 (Sol-PC) — I/O reference

Source: `730000_Sol_Systems_Manual_Jan1978.pdf` (Sol Systems Manual, Jan 1978) and the SOLOS 1.3
source (`roms/SOLOS/SOLOS13.ASM`, © 1977 Processor Technology Corp.), corroborated by the
deramp.com *Sol-20 Restoration* notes. See `docs/sources.md`. [Source: #]

The Sol-20 is an **integrated machine**: the Sol-PC motherboard carries an 8080, a VDM-1-style
video section, a parallel keyboard, an RS-232 serial port, a parallel port, and a CUTS cassette
interface, plus the SOLOS personality-module ROM — all on one board, with S-100 expansion slots.
altairsim models it as three cards on the bus (`fp` sense switches, `vdm1` video, and the
composite `sol` I/O board), which decode disjoint ports and so never contend.

## I/O port map — `F8h`–`FFh`

The Sol-PC's onboard I/O lives at the top of the I/O space. (These are physical hardware ports.
SOLOS's `SET I=`/`SET O=` "pseudo-ports" 0–3 are logical device numbers — 0 keyboard/VDM,
1 serial, 2 parallel, 3 user — not these.)

| Port | Dir | Name | Device |
|---|---|---|---|
| `F8h` | in | `SERST` | Serial status |
| `F9h` | in/out | `SDATA` | Serial data |
| `FAh` | in | `STAPT`/`TAPPT` | General + tape status (shared register) |
| `FAh` | out | `TAPPT` | Tape motor + cassette baud select |
| `FBh` | in/out | `TDATA` | Tape (CUTS) data |
| `FCh` | in | `KDATA` | Keyboard data (read-only) |
| `FDh` | in/out | `PDATA` | Parallel (printer) data |
| `FEh` | out | `DSTAT` | VDM display parameter (scroll) |
| `FFh` | in | `SENSE` | Front-panel sense switches |

The single physical status register at **`FAh`** multiplexes the readiness of the keyboard, the
parallel port, and the tape UART. That is why the keyboard/parallel/tape functions cannot be
separate cards on a single-driver bus — they are one register — and are modeled as one `sol`
board.

## `F8h` — serial status (read); `F9h` — serial data

Serial flags are **active high** (ready = 1):

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 0 | `01` | `SCD` | Carrier detect |
| 1 | `02` | `SDSR` | Data set ready |
| 2 | `04` | `SPE` | Parity error |
| 3 | `08` | `SFE` | Framing error |
| 4 | `10` | `SOE` | Overrun error |
| 5 | `20` | `SCTS` | Clear to send |
| 6 | `40` | `SDR` | **Serial data ready** (RX has a byte) |
| 7 | `80` | `STBE` | **Serial transmitter buffer empty** (OK to send) |

Driver: `SSTAT` loops on `IN SERST / ANI SDR / RZ` then `IN SDATA`; `SDROT` waits `IN SERST / RAL`
until `STBE` (bit 7) then `OUT SDATA`. The word length is jumper-selectable at 8/7/6 data bits
(Sol-PC DIP; SOLOS does not reprogram it).

## `FAh` — general + tape status (read)

**Mixed polarity** — this is the Sol's characteristic quirk. Keyboard and parallel flags are
active **low** (the drivers `CMA`-invert before testing); tape flags are active **high**:

| Bit | Mask | Name | Meaning | Active |
|---|---|---|---|---|
| 0 | `01` | `KDR` | Keyboard data ready | **low** (0 = a key is waiting) |
| 1 | `02` | `PDR` | Parallel data ready (input) | **low** |
| 2 | `04` | `PXDR` | Parallel device ready (output) | **low** (0 = can accept a byte) |
| 3 | `08` | `TFE` | Tape framing error | high |
| 4 | `10` | `TOE` | Tape overrun error | high |
| 6 | `40` | `TDR` | Tape data ready | high |
| 7 | `80` | `TTBE` | Tape transmitter buffer empty | high |

Drivers: `KSTAT` = `IN STAPT / CMA / ANI KDR / RZ` (so KDR reads 0 when a key is present);
`PASTAT` = `IN STAPT / CMA / ANI PDR / RZ`; `PROUT` loops `IN STAPT / ANI PXDR / JNZ PROUT`
(waits while set); tape `STAT` = `IN TAPPT / ANI TDR / RNZ` and `WRWAT` = `IN TAPPT / ANI TTBE /
JZ WRWAT`.

## `FAh` — tape/baud control (write)

`OUT FAh` controls the two cassette motors and the cassette baud rate:

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 5 | `20` | — | 1 = 300 baud (slow); 0 = 1200 baud (fast) |
| 6 | `40` | `TAPE2` | 1 = turn cassette unit 2 motor on |
| 7 | `80` | `TAPE1` | 1 = turn cassette unit 1 motor on |

`RTOF1: OUT TAPPT` (motors off); `OUT TAPPT ;GET TAPE MOVING`.

## `FBh` — tape data · `FCh` — keyboard data · `FDh` — parallel data

- **Tape** (`TDATA`, `FBh`): the CUTS cassette UART's data register. Kansas-City-standard FSK
  audio at 300 or 1200 baud; `IN TDATA` also clears the UART's error flags. For the audio
  itself, see "CUTS audio format" below.

## CUTS audio format (MEASURED, not from a manual)

**Provenance: this section is not sourced from a Processor Technology document.** None of the
manuals we hold state the CUTS cycle counts — this page previously said only "Kansas-City-standard
FSK audio at 300 or 1200 baud". The numbers below were **measured** from a genuine Sol-20 cassette
recording, deramp.com's `TRK80.WAV`, and are recorded here because the repo's rule is that a
hardware number has a source. Treat a period manual as authoritative over this table if one turns
up.

| | 300 baud (`SE TA 1`) | 1200 baud (`SE TA 0`, the default) |
|---|---|---|
| Logic 1 (mark) | 8 cycles of 2400 Hz | **2 cycles of 2400 Hz** |
| Logic 0 (space) | 4 cycles of 1200 Hz | **1 cycle of 1200 Hz** |
| Bit cell | 3.33 ms | **833 µs** |

Both symbols occupy the same time, which is the whole point of the 2:1 tone choice — a receiver
counts cycles instead of trusting a clock. The idle line is mark, so an unrecorded stretch is a
steady 2400 Hz tone.

**Framing is 8N2** — one start bit, eight data bits LSB first, **two** stop bits. Measured as 11.0
bit times between consecutive start bits over 7,325 of ~7,900 frames.

**How the measurement was checked.** `TRK80.WAV` (44.1 kHz mono) demodulates under the above
parameters to 7,932 bytes with 27 framing errors, and the result carries a well-formed SOLOS file
header at offset 52: name `TRK80`, then a `SIZE` field of `0x1EA0` (7,840) that matches the
decoded payload. A wrong tone pair or a wrong bit rate does not produce a valid header and a
self-consistent length — that agreement is what makes this a measurement rather than a guess.
`altair_tapetool info` reproduces it (`tools/tapetool.cpp`).
- **Keyboard** (`KDATA`, `FCh`): read-only parallel ASCII from the keyboard. Ready is `FAh`
  bit 0 (`KDR`, active low); reading `KDATA` clears the strobe.
- **Parallel** (`PDATA`, `FDh`): the general parallel port, used for a printer. Input ready
  `FAh` bit 1 (`PDR`); output ready `FAh` bit 2 (`PXDR`), both active low.

## `FEh` — VDM display parameter (write)

`OUT DSTAT` (`FEh`) writes the VDM "beginning of text" line: the low 4 bits select which of the
16 character rows is displayed at the top, giving hardware scroll without moving screen RAM.
SOLOS keeps the value in its RAM variable `BOT` and re-writes it on erase/scroll. (On a
stand-alone VDM-1 card this register is at the card's own jumpered port; on the Sol-PC it is
fixed at `FEh`, independent of the video memory page.)

## `FFh` — sense switches (read)

`IN SENSE` reads the 8-position DIP sense switches (the `fp` board). SOLOS 1.3 defines the port
but does not read it for console selection — the default console is always keyboard-in / VDM-out
(pseudo-port 0).

## Memory map

| Range | Size | Contents |
|---|---|---|
| `0000`–`BFFF` | 48 K | RAM |
| `C000`–`C7FF` | 2 K | SOLOS ROM (`builtin:solos`) |
| `C800`–`CBFF` | 1 K | SOLOS scratchpad RAM + stack (`SYSTP = CBFFh`) |
| `CC00`–`CFFF` | 1 K | VDM-1 screen RAM (16×64) |

On reset the Sol-PC vectors the CPU to the personality module at `C000h` (SOLOS's auto-startup:
`C000` = `NOP`, `C001` = `JMP` restart); altairsim's `machines/sol20.toml` does this with
`startup = ["RUN C000"]`.

## Quirks that bite an implementer

- **`FAh` is one register for three devices, with mixed polarity.** Keyboard/parallel bits are
  active low; tape bits are active high. Do not take the `EQU` mask names ("...READY") as
  active-high — check the driver's `CMA`.
- **`FAh` is read *and* written to different ends** — read = status, write = motor/baud.
- **The VDM control port is `FEh` on the Sol**, not the stand-alone VDM-1's page-tied port.
- **Helios disk** (`F0h`–`F7h`) exists only in the `HELIOS`-conditional SOLOS build and is not
  part of the stand-alone Sol-20; ignore it for a plain machine.
