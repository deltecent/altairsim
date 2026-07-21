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

## Keyboard — the codes each key sends

Source: Sol Systems Manual **Table 7-4** (Sol Keyboard Assignments) and §7.7 (Individual Key
Descriptions), pages VII-17…VII-25. Every code below is corroborated by the SOLOS 1.3 equates
(`roms/SOLOS/SOLOS13.ASM` lines 1944-1957), which agree with the table exactly.

The keyboard is **parallel ASCII**, not a scanning matrix the guest decodes: `IN KDATA` (`FCh`)
returns one finished byte, ready is `FAh` bit 0 (`KDR`, active low), the read clears the strobe,
and the hardware has N-key rollover. Ordinary keys send what you would expect — unshifted lower
case, shifted upper case, `CTRL`+letter = `01`–`1A`, `ESCAPE` = `1B`, `TAB` = `09`,
`RETURN` = `0D`, `LINE FEED` = `0A`, `SPACE` = `20`. The arithmetic pad duplicates the
typewriter keys and is unaffected by SHIFT; its ÷ key sends `/`.

### Special keys — the eight with no ASCII equivalent

All eight set **bit 7**, and all send the same code unshifted, shifted and with CTRL (one value
across all three columns of Table 7-4):

| Key | Code | Function |
|---|---|---|
| `MODE SELECT` | `80` | Enter the SOLOS command mode. |
| `←` | `81` | Cursor left one. |
| `CLEAR` | `8B` | Erase the screen, cursor home. |
| `LOAD` | `8C` | Nothing in SOLOS — see below. |
| `HOME CURSOR` | `8E` | Cursor to the first position, top left. |
| `→` | `93` | Cursor right one. |
| `↑` | `97` | Cursor up one. |
| `↓` | `9A` | Cursor down one. |

**A special key's code is `80h` + the VDM driver control code for the same action.** That is not
a coincidence and it is the whole trick of the design: SOLOS's display driver table is written
literally as `DB CLEAR-80H`, `DB UP-80H`, `DB HOME-80H` … (`SOLOS13.ASM`, `TBL:`), and `VDMOT`
does `ANI 7FH` on its way in. So `CLEAR` (`8B`) and a received Ctrl-K (`0B`) reach the same
handler, `HOME CURSOR` (`8E`) and Ctrl-N (`0E`), `←` (`81`) and Ctrl-A (`01`), and so on. See the
VDM driver control-code table in `SOLOS.md`.

### Keys that send no code at all

These are wired to hardware, not to the keyboard's data byte, so nothing appears at `FCh` and a
guest cannot see them:

| Key | What it does | Indicator |
|---|---|---|
| `BREAK` | Forces the serial (SDI) output to a space level while held. | — |
| `LOCAL` | Loops the SDI output back to its input and disables transmission. | yes |
| `REPEAT` | Held with another key, repeats that key at ≈15/second. | — |
| `SHIFT` / `SHIFT LOCK` | Ordinary shift; SHIFT LOCK is a locking key. | SHIFT LOCK |
| `UPPER CASE` | Locking upper-case mode. | yes |

`UPPER CASE` + `REPEAT` pressed together is the **keyboard restart** — the same effect as power-on
or the RST switch, and the documented way out of a program that ignores `MODE SELECT` or has hung.

### Quirks

- **`MODE SELECT` and Ctrl-@ are the same key to SOLOS.** Its command-mode input paths mask with
  `ANI 7FH` and then test for zero (`GCLIN`, `HBOUT`, `STAT` in `SOLOS13.ASM`), so `80h` and
  `00h` both mean "mode". Terminal mode (`TERM1`) is the exception: it compares against `80h`
  before masking, so there `00h` goes out the serial line like any other character.
- **`LOAD` (`8C`) is dead in both personality modules.** §7.7.13: the key exists and generates
  the code, but neither CONSOL nor SOLOS acts on it; it is there for a program to claim.
- **The character generator matters for what you *see*, not what is sent.** Table 7-4 gives
  separate display columns for the 6574 and 6575 ROMs. The code on the port is the same either
  way.

## CUTS audio format (MEASURED, not from a manual)

**Provenance: this section is not sourced from a Processor Technology document.** None of the
manuals we hold state the CUTS cycle counts — this page previously said only "Kansas-City-standard
FSK audio at 300 or 1200 baud". The numbers below were **measured** from a genuine Sol-20 cassette
recording -- Philip Lord's `TRK80.WAV` (digitized by him, hosted on deramp.com) -- and are
recorded here because the repo's rule is that a
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

**How the measurement was checked.** `TRK80.WAV` demodulates under the above parameters to
7,939 bytes with **0 framing errors**, and the result carries a well-formed SOLOS file header at
offset 52: name `TRK80`, then a `SIZE` field of `0x1EA0` (7,840) that matches the decoded
payload, header checksum `D9`. A wrong tone pair or a wrong bit rate does not produce a valid
header and a self-consistent length — that agreement is what makes this a measurement rather than
a guess. `altair_tapetool info` reproduces it (`tools/tapetool.cpp`).

**This recording used to decode badly, and the story is worth keeping.** An earlier demodulator
classified and sliced each half-cycle interval, and on this dub — recorded an octave low, in the
1-to-2-crossings-per-bit regime a real Sol tape sits in — it kept its framing (99.7%, 27 errors
in ~7,900 frames) but got the DATA wrong: **6,778 of 7,840 payload bytes bad**, the tape
unloadable. That was a genuine trap. The carrier was intact and the framing rate high, so the
parameters above looked confirmed — yet the bytes were garbage. What broke the tie was the
header: it decoded (checksum `D9`, `SIZE 0x1EA0`), which proved the timing table right and the
**decoder** wrong. The matched-filter decoder (`src/host/tapemodem.cpp`) now reads all 7,939
bytes correctly and the tape loads.

So the timing table above **stands**, and always did — it was measured from the carrier, and the
carrier was intact even when the decoder was not. A high framing rate vouches for the carrier,
not the bit values; check the header before doubting the table. And because the archived
recording now reads clean, it is the shipped example: `examples/sol/TRK80.WAV` **is** this tape,
and `acceptance-trek80-wav` reads it back through SOLOS as the decoder's regression.

### Leader, trailer, and what a whole tape looks like (MEASURED)

Five genuine Sol cassette dubs from the same archive, all 1200 baud. These are *recordings of
tape*, not modulator reconstructions, so the leader and trailer are a real transport and a real
operator's finger:

| Tape | Audio leader | Audio trailer | Byte leader | SOLOS file |
|---|---|---|---|---|
| `TRK80` | 3.05 s | 1.93 s | 51 | `TRK80`, 7,840 B |
| `ROBOT_INSTR` | 4.33 s | 1.47 s | 51 | `ROBOT`, 10,677 B |
| `SKULL` | 4.22 s | 1.73 s | 52 | `SKULL`, 16,853 B |
| `ALS8` | 3.83 s | 1.23 s | 50 | `ALS8`, 8,320 B |
| `TINY_TREK` | 3.68 s | 2.30 s | 50 | `TTREK`, 8,194 B |

So a CUTS tape is: **~4 s of idle 2400 Hz**, then **~51 bytes of `00`**, then a **`01` sync byte**,
then the 16-byte SOLOS header (`FOPEN` layout, above), then the payload, then **~2 s of idle tone**.
The byte-level leader and sync are written by SOLOS itself, so they survive in a byte tape image;
only the audio seconds are lost, and only they must be synthesized when writing audio back out.

**Every tape in this archive holds exactly ONE file**, checked by scanning the full decoded stream
for a second leader+`01`+header signature — including `ROBOT_INSTR`, whose *filename* suggests a
program plus its instructions but which decodes to a single file named `ROBOT`. Nor does any tape
contain an interior run of idle tone longer than 10 bit times (one all-ones frame).

**This is structural, not a sampling accident, and it is why no multi-file example will be found
here.** These are commercial distribution masters, and Processor Technology sold one program per
cassette. A multi-file tape is a *user* artifact — somebody saving several of their own programs
onto one C-60 — and nobody archived their own scratch tapes. So the inter-file gap the SOLOS manual
implies and that a human needs (to stop the transport before the next program runs past the head)
is **real but unmeasurable from published media**, and the simulator must pick a number rather than
recover one. Note that the gap and the leader are the same thing seen twice: a second file on a
tape is just a file, and it wants the leader every file wants.
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

## CPU clock

**Source: Sol Systems Manual, Theory of Operation §VIII** — the φ1/φ2 pulse-width table, which
gives a clock rate against the pulse widths for each 8080 part the board was jumpered for.

| Part | Clock | φ1 | φ2 |
|---|---|---|---|
| **8080A (stock)** | **2.045 MHz** | 140 ns | 280 ns |
| 8080A-2 | 2.386 MHz | — | — |
| 8080A-1 | 2.863 MHz | — | — |

The 2.045 MHz is not a round number by accident: it is the **14.31818 MHz dot clock ÷ 7**. The
Sol derives the CPU clock from the video timing chain, so the processor rate is a consequence of
NTSC colour-burst arithmetic. The faster two are jumper options for the faster-binned parts, not
separate designs.

altairsim's `examples/sol/trek80.toml` sets `clock_hz = 2045000` for this reason; `machines/sol20.toml`
leaves the clock free-running, which is the simulator's default — a period-accurate rate is opt-in.

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
