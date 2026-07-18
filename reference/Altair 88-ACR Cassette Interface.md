# Altair 88-ACR Audio Cassette Interface

Source: [Altair 88-ACR Cassette Interface.pdf](#)

MITS, Inc. 1975 (Second Printing, February 1977). This is a distilled
emulation reference for the 88-ACR. Kit-assembly steps, parts lists, PCB
component layouts, alignment/trim-pot procedures, and marketing/order/warranty
text from the original manual are intentionally omitted; only the information
needed to emulate the card in software is kept.

---

## 1. What the card is

The 88-ACR is two mated PC boards acting as a single card:

1. **88-SIO B board** — the standard MITS Serial TTL-level I/O board. It is the
   part the ALTAIR/S-100 bus actually talks to. It carries the UART (a
   COM2502 / 2502-type, referred to as **IC M**) plus the address-decode,
   status, interrupt, and baud-rate-divider logic.
2. **Modem (Modulator/Demodulator) board** — converts the SIO board's TTL
   serial signals to/from audio FSK tones for a cassette recorder.

**Key consequence for emulation:** from the CPU's point of view the 88-ACR *is*
an 88-SIO B. Its register model, status bits, and framing are the 88-SIO B's.
The modem board only shapes the audio; it does not change what the guest program
reads or writes. Emulating the guest-visible behavior means emulating a UART
serial port whose serial line is the tape.

The 88-SIO B is a **TTL-level** variant. (The SIO A is RS-232, the SIO C is
20 mA TTY; all three share the identical register model — only the line drivers
differ.)

---

## 2. I/O port assignments

The SIO board occupies **two consecutive device addresses**, selectable by
jumper anywhere in the range 0–376 octal (even base). Address bit **A0**
selects between them:

| A0 | Channel | Purpose | Even/Odd |
|----|---------|---------|----------|
| 0  | **Control / Status channel** | `IN` = read status word; `OUT` = set interrupt enables | always even |
| 1  | **Data channel** | `IN` = read received byte (RDA); `OUT` = transmit byte | always odd |

**Standard 88-ACR wiring (as shipped / as MITS software expects):**

| Function | Port (octal) |
|----------|--------------|
| Control / Status | **006** |
| Data             | **007** |

MITS cassette software (4K/8K/Extended BASIC, the Read/Write test programs)
assumes control at 006 and data at 007. If you re-address the board, both move
together (control = even, data = odd).

Note: some MITS text also refers to these as "I/O port 6" and "I/O port 7".

---

## 3. Status word (read from the Control channel, port 006)

`IN` on the control channel returns the UART status. **Flag polarity is
active-LOW for the two "ready" bits** — a 0 means ready/available, a 1 means
not-ready. This is the single most important quirk to get right.

| Bit | Logic LOW (0) means | Logic HIGH (1) means |
|-----|---------------------|----------------------|
| D7  | **Output device Ready** (transmitter buffer empty). Also generates a hardware interrupt if output interrupts are enabled. | Not Ready (transmitter busy) |
| D6  | not used | not used |
| D5  | not used | not used |
| D4  | (—) | **Data Overflow** — a new word was received before the previous word was read into the accumulator |
| D3  | (—) | **Framing Error** — received data word had no valid stop bit |
| D2  | (—) | **Parity Error** — received parity disagrees with the selected parity |
| D1  | not used | not used |
| D0  | **Input device Ready** — a received byte is available for the computer to input | Not Ready (no byte available) |

Idioms that follow from the polarity (emulator must satisfy these loops):

- **Wait for transmit ready:** `IN 006 / RLC / JC back` — RLC rotates D7 into
  carry; loop while carry set (D7=1, busy); fall through when D7=0 (ready).
- **Wait for receive ready:** `IN 006 / RRC / JC back` — RRC rotates D0 into
  carry; loop while carry set (D0=1, empty); fall through when D0=0 (byte
  available).

Reading the data channel (port 007) clears the input-ready flip-flop and the
UART's Data-Available (RDAV); writing the data channel clears the output-ready
(busy) flip-flop as the byte is loaded/transmitted.

---

## 4. Interrupt control (write to the Control channel, port 006)

`OUT` on the control channel latches interrupt enables from accumulator bits
**D0 and D1** (all other bits ignored):

| D1 | D0 | Output interrupt | Input interrupt |
|----|----|------------------|-----------------|
| 0  | 0  | disabled | disabled |
| 0  | 1  | enabled  | disabled |
| 1  | 0  | disabled | enabled  |
| 1  | 1  | enabled  | enabled  |

Example: to enable input and disable output interrupts, load A = xxxxxx01b
(D1=0, D0=1) and `OUT` to the control channel.

Interrupts feed the 88-VI vectored-interrupt card (priority levels 0–7, 7
highest) or a single level jumpered straight to the processor `INT` line (jump
to octal 70 on interrupt; ISR lives in 70–77 octal).

**MITS cassette software does NOT use interrupts.** For 88-ACR emulation aimed
at running MITS software, the interrupt path can be left unimplemented (or
default: both disabled after power-on clear / POC).

---

## 5. Serial framing (UART programming)

The UART framing is set by hardwire jumpers, not by software — it is fixed at
assembly time. The COM2502-type UART (IC M) options:

| UART pin | Name | Function |
|----------|------|----------|
| 35 | NPB  | High = no parity bit transmitted |
| 36 | NSB  | Low = 1 stop bit; High = 2 stop bits |
| 37 | NDB2 | Data-bits select (with NDB1) |
| 38 | NDB1 | Data-bits select (with NDB2) |
| 39 | POE  | With NPB low: Low = odd parity, High = even parity |

Data-bits selection:

| NDB2 | NDB1 | Bits/char |
|------|------|-----------|
| 0 | 0 | 5 |
| 0 | 1 | 6 |
| 1 | 0 | 7 |
| 1 | 1 | 8 |

Parity selection:

| POE | NPB | Parity |
|-----|-----|--------|
| 0 | 0 | odd |
| 1 | 0 | even |
| X | 1 | none |

**Standard 88-ACR framing: 8 data bits, 1 stop bit, no parity** (NDB1=NDB2=+V,
NSB=GND, NPB=+V, POE=+V). So a tape byte is a clean 8-bit value with no parity.

---

## 6. Baud rate / byte timing

The SIO board derives the UART clock (16× baud) from the **2 MHz** system clock
(the SΦ line) via a 12-bit presettable counter (ICs P, Q, R) and a one-shot
(IC O). Max UART input frequency 400 kHz → **max 25,000 baud** on the bare SIO
board.

**The 88-ACR is wired for 300 baud (its practical maximum on tape).** Selectable
rates and their 12-bit preset-count patterns (pads 11..0, +V = 1, GND = 0):

| Baud | 11 10 9 8 | 7 6 5 4 | 3 2 1 0 |
|------|-----------|---------|---------|
| 110   | 1 0 1 1 | 1 0 0 1 | 0 1 0 0 |
| 150   | 1 1 0 0 | 1 1 0 0 | 0 0 1 1 |
| **300** | 1 1 1 0 | 0 1 1 0 | 0 0 1 1 |
| 600   | 1 1 1 1 | 0 0 1 1 | 0 1 0 0 |
| 1200  | 1 1 1 1 | 1 0 0 1 | 1 1 0 0 |
| 2400  | 1 1 1 1 | 1 1 0 1 | 0 0 0 0 |
| 4800  | 1 1 1 1 | 1 1 1 0 | 1 0 1 0 |
| 9600  | 1 1 1 1 | 1 1 1 1 | 1 0 0 0 |
| 19200 | 1 1 1 1 | 1 1 1 1 | 1 1 1 0 |

Preset-count formula for a rate not in the chart:
`Preset Count Frequency = 4100 − (Period of Output Frequency [µs] / 0.5µs)`.

**Timing at 300 baud (the number to emulate):**

- One bit = **3.33 ms** (≈3.3 ms per bit).
- UART clock = 300 × 16 = **4800 Hz**, i.e. a **208 µs** repetition period
  (UART clock pin 40 of IC M: ~2.5 µs low-going pulse).
- A byte frame (start + 8 data + 1 stop = 10 bits) ≈ 33.3 ms → **~30 bytes/s**.
- The UART tolerates only about **±5%** total speed error before it flags a
  framing error; the demodulator can accommodate roughly ±100 Hz of tape-speed
  drift at 2125 Hz.

---

## 7. Modem board: FSK tone format (the audio on the tape)

The modem board is an FSK (frequency-shift-keyed) modem. This matters if you are
synthesizing or decoding real cassette audio (WAV); it is invisible to the guest
CPU.

**Modulator (record):** serial data on the SIO's transmit line (`STSO`, called
`XS` on the modem) keys the tones. The 2 MHz clock is divided by 104 for a
logic-1 input and by 135 for a logic-0 input (counters J & K), then divided by 8
(IC E):

| Serial level | Tone |
|--------------|------|
| **logic 1** (mark) | **2400 Hz** |
| **logic 0** (space) | **1850 Hz** |

The square waves are shaped to ~100 mV P-P sawtooth for the recorder "Mic"
input. **The idle line is logic 1, so an idle recording is a steady 2400 Hz
tone at "Record Out".** Record-out level is ~150 mV P-P.

**Demodulator (play):** tape audio (35 mV RMS to 3.5 V RMS at "Play In") passes
a two-stage op-amp ~2 kHz filter (ICs A, B), then a phase-locked-loop
(XR-210 / XR210, IC C) whose internal oscillator sits halfway between 1850 and
2400 Hz. A carrier-detect circuit (Q2, Q3, Q4) enables the PLL. PLL output:
logic 1 for 2400 Hz, logic 0 for 1850 Hz, level-shifted (via a zener) to TTL and
fed back to the SIO's receive line (`SRSI`).

Only one of "Play In" or "Record Out" is connected to the recorder at a time,
never both.

Requires a **−12 V** supply (zener-regulated on the SIO board; anode of Z1) in
addition to +5 V.

---

## 8. Cassette / tape leader format (MITS software)

MITS BASIC and the test programs record a **steady leader tone** followed by a
repeated **leader byte**, then the checksum-loader data (BASIC is stored high
memory downward; data is checksummed every 256 bytes).

- At least **~15 seconds of steady tone** is recorded before data (to clear the
  plastic leader and tape wrinkles); at least **5 seconds of tone between
  batches** when multiple blocks are recorded.
- **Leader byte** (the value repeated before the checksum loader begins):

| Software version | Leader byte (octal) | (hex / dec) |
|------------------|---------------------|-------------|
| Version 3.1 / 4K | 175 | 0x7D / 125 |
| Version 3.2      | 256 | 0xAE / 174 |

The bootstrap leader detector (below) spins reading the data channel until it
sees the leader byte, then jumps to the bootstrap loader.

The MITS Altair BASIC cassette carries a **125-octal test recording** at the
start of the *back* side (used for alignment); the 8K BASIC boot expects the
88-ACR wired for **300 baud, addresses 6 & 7**.

**Successful-load front-panel signatures (address lights, octal):**

| Software | Address lights after "jump" |
|----------|-----------------------------|
| 4K BASIC | 007647 |
| 8K BASIC | 017647 |
| Extended BASIC (v3.2) | 037647 |

During a good load the status lights show MEMR, INP, MI, WO on, WAIT dimly lit.
A printed "C" during load = checksum error (tape speed / wow-flutter / head
alignment); a printed "M" = memory problem.

---

## 9. Loading procedure (how MITS software boots off tape)

1. Deposit the cassette bootstrap loader (4K vs 8K code differs), starting at
   examine 000,000; raise A15.
2. Connect "Play In" to the recorder output; volume/tone at max (user notes
   recommend ~1/3 volume in practice to avoid noise).
3. Start the tape at the leader ("000" on a counter); wait ~15 s.
4. Press RUN **before** the data starts.
5. Address lights "jump" (~10 s later) to the signature above; BASIC loads
   downward from high memory.

**Bootstrap Leader Detector** (helper that waits for the leader byte instead of
timing the 15 s by hand); loaded above/alongside the bootstrap loader, entry at
001,000 octal:

| Addr (octal) | Code | Mnemonic | Meaning |
|--------------|------|----------|---------|
| 001,000 | 333 | IN  | input data |
| 001,001 | 007 |     | from ACR (data port 007) |
| 001,002 | 376 | CPI | compare byte to leader |
| 001,003 | 256 |     | leader byte (175 for v3.1) |
| 001,004 | 302 | JNZ | jump if not leader |
| 001,005 | 000 |     | to START (001,000) |
| 001,006 | 001 |     | |
| 001,007 | 303 | JMP | jump to bootstrap loader if leader found |
| 001,010 | 000 |     | (bootstrap loader address) |
| 001,011 | 000 |     | |

Procedure: load bootstrap loader, load leader detector, examine 001,000, start
tape + press RUN; after ~25 s the jump occurs and software loads.

---

## 10. Machine-language I/O test/utility programs

These are the reference programs MITS ships for the ACR; useful as emulation
test vectors. All use ports 006 (status) / 007 (data). Test byte is **125
octal** (0x55).

**Output Test Program** (records 125 octal continuously until stopped), origin
200 octal:

| Addr | Code | Mnemonic | Note |
|------|------|----------|------|
| 200 | 333 | IN  | status |
| 201 | 006 |     | port 006 |
| 202 | 007 | RLC | rotate D7 into carry |
| 203 | 332 | JC  | loop if busy |
| 204 | 200 |     | → 200 |
| 205 | 000 |     | |
| 206 | 076 | MVI | A ← |
| 207 | 125 |     | test byte |
| 210 | 323 | OUT | data |
| 211 | 007 |     | port 007 |
| 212 | 303 | JMP | |
| 213 | 200 |     | → 200 |
| 214 | 000 |     | |

**Input Test Program** exists as the playback counterpart (origin 000; polls D0
via RRC, reads data, XORs against 125 to verify).

**Write Program** (38 bytes, origin 017,000): LXI H,start / LXI B,end / write
test byte 000 first, then loop `IN 006 / RLC / JC` (wait D7), `MOV A,M / OUT 007`,
increment HL, compare HL to BC, `JMP END (017,375)` self-loop when done.

**Read Program** (48 bytes, origin 017,000): LXI H,start / LXI B,end / hunt for
test byte 000 (`IN 006 / RRC / JC` for D0, `IN 007`, `CPI 000`), then read loop
storing to memory until HL = BC, ending in a self-loop at 017,375.

In both, the start/end addresses are placed in HL and BC; completion is visible
as the address lights stopping.

---

## 11. Tape-recorder motor (remote) control — optional

A commonly requested add-on: DC/servo recorders with a "Remote" jack can be
started/stopped from **control-channel bit D0** by wiring one of the (normally
interrupt) flip-flops (IC B) on the SIO B board to a spare 8T97 driver as a
relay driver. This reuses the interrupt path that ACR software otherwise leaves
idle.

- 8K BASIC uses it directly: **`OUT 6,1` turns the motor ON**, **`OUT 6,0`
  turns it OFF**; `CLOAD` / `CSAVE` drive it.
- Machine-language: `MVI A,001 / OUT 006` on (control channel), `MVI A,000 /
  OUT 006` off.
- The motor must be turned on **5–15 seconds before** outputting data (leader).

This is optional hardware; MITS software runs without it (motor stays on
manually). AC-motor recorders can't use it.

---

## 12. Emulation checklist (summary of load-bearing facts)

- Two ports: **control/status = 006** (even), **data = 007** (odd); A0 selects.
- **Ready flags are active-LOW:** status D7=0 ⇒ TX ready; status D0=0 ⇒ RX byte
  available. D4/D3/D2 = overflow/framing/parity, active-HIGH.
- `OUT 006` sets interrupt enables from D0/D1 only; MITS software never uses
  interrupts.
- Framing fixed at **8N1** for the ACR; a tape byte is a clean 8-bit value.
- **300 baud**: 3.33 ms/bit, ~30 bytes/s, UART clock 4800 Hz.
- FSK tones (audio layer only): **logic 1 = 2400 Hz, logic 0 = 1850 Hz**; idle
  line = logic 1 = steady 2400 Hz. Guest CPU never sees tones — only the
  demodulated serial bytes.
- Leader byte before the checksum loader: **256 octal** (v3.2) or **175 octal**
  (v3.1/4K), after ~15 s of steady tone.

## What published Altair cassette audio measures (MEASURED)

**Provenance: measured, not from a manual** — from deramp.com's published Altair cassette audio.

| Published tape | Measured tones | Reads on a real 88-ACR? |
|---|---|---|
| `8K BASIC v4 2SIO Cassette`, `PS2 v3 2SIO Cassette` | **2397 / 1852 Hz**, 300 baud | **yes** — this card's FSK |
| `4K BASIC 3.2 (mem dump, KCS)`, `4K BASIC 4.0 KCACR Standard` | **2377 / 1201 Hz**, 300 baud | **no** — Kansas City |

The 2397/1852 measurement is a direct confirmation of §7's arithmetic (2 MHz ÷ 104 ÷ 8 = 2404 Hz,
÷ 135 ÷ 8 = 1852 Hz) off real media. Note that "2SIO as cassette" names which **serial card the
loader talks to**, not a different modulation: the modem board, and therefore the audio, is the
same.

**The Kansas City files are not 88-ACR tapes.** "KCS" and "KCACR" (Kansas City ACR) name a
*different standard*, and §7 says why this card cannot read them: the demodulator is an XR-210 PLL
sitting at **2125 Hz** and accommodating roughly **±100 Hz** of drift. A Kansas City space tone is
**1200 Hz** — about 925 Hz outside the capture range. A real 88-ACR fed one of these tapes does not
read it slowly or badly; it does not read it at all.

Consequence for the simulator: a card declares the modulation **its own hardware demodulates**, and
a tape in any other modulation is REFUSED with a message naming what it actually is. Accepting a
Kansas City tape on this card would be giving the 88-ACR a capability the physical board never had
— see `src/host/tapemodem.h` and the "never invent hardware" rule in `DESIGN.md`. (The Sol-20 is a
genuinely different case: its CUTS UART really does do 300 **and** 1200 baud, selected by the guest
at `OUT FAh` D5 — see `reference/Sol-20.md`.)

## What idle tape carries, and where the leader really lives (MEASURED)

**Provenance: measured, not from a manual** — same corpus. This answers "what is on the tape when
the guest is not sending anything?", and it matters for *writing* audio as much as reading it.

**Idle is a tone, not silence.** The UART's serial output pin idles HIGH, high is mark, and the
modem board has no squelch and no carrier-off — its oscillator runs whenever the card is powered
(§7). So an 88-ACR recording nothing lays down **continuous 2400 Hz**. Blank tape and idle tape are
acoustically different things, and a decoder must treat a steady single tone as *no data*, never as
a run of one symbol.

**The published files have essentially no audio leader**, which contradicts §8's "~15 seconds of
steady tone" — because they are modulator reconstructions trimmed to the data, not dubs of a
cassette:

| Tape | Duration | Leading mark | Trailing mark |
|---|---|---|---|
| `8K BASIC v4 2SIO Cassette` | 282.6 s | 0.001 s | 0.001 s |
| `Ext BASIC 4.1 2SIO Cassette` | 583.3 s | 0.001 s | 0.001 s |
| `PS2 v3 2SIO Cassette` | 116.7 s | 0.001 s | 0.001 s |
| `TRK80.WAV` (Sol CUTS, a real dub) | 77.7 s | **3.05 s** | **1.93 s** |

TRK80 is the control: a genuine recording *does* carry seconds of leader, and you can see the
operator's finger in it. **Do not use the published `.wav` files as masters for writing a physical
cassette** — §8's 15 s exists to clear the plastic leader and let the transport settle, and these
files have none of it.

**The leader that the loader actually needs is BYTES, not audio**, and it survives in the decoded
stream — which is why a byte-level tape image loses nothing:

| Tape | Leader byte | Count | Trailer |
|---|---|---|---|
| `8K BASIC v4` | `0xC2` | 255 | 257 × `0x00` |
| `PS2 v3` | `0xAE` (§8's 256 octal) | 127 | 30 × `0x00` |

**Nothing on these tapes is encoded as duration.** Checked directly: the longest interior run of
mark on TRK80 is exactly **10 bit times** — which is just one all-ones frame, since every frame
begins with a start bit and mark can never run longer however many `0xFF`s follow. The 88-ACR tapes
have no interior run over 4 bit times. So a `.TAP` byte image is a complete representation of these
recordings.

**The caveat is multi-file tapes**, which this corpus does not contain and which are therefore
UNVERIFIED here. §8 is explicit that MITS software wants **at least 5 seconds of tone between
batches**, and a Sol tape holding several SOLOS files needs the same for a human reason: the
operator must be able to stop the transport before the next program runs past the head. That
silence is *time*, and time is the one thing a byte image cannot hold — so when the simulator
*writes* audio it must synthesize leader and inter-file gaps rather than recover them.
- Successful-load address-light signatures: 007647 (4K), 017647 (8K), 037647
  (Extended 3.2).
- Optional motor control on **control-channel bit D0** (`OUT 6,1` on / `OUT 6,0`
  off).
