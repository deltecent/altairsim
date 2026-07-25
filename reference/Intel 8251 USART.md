# Intel 8251 / 8251A USART

Source: [8251.pdf](https://deramp.com/downloads/intel/8251.pdf)

The **Intel 8251** (and its pin/function-compatible successor the **8251A**) is a Universal
Synchronous/Asynchronous Receiver/Transmitter — a single-chip programmable serial interface
for the 8080/8085/Z80 bus. It appears on many S-100 boards; in this simulator it is the
console USART of the [SD Systems SBC-100/200](SD%20Systems%20SBC-100%20%26%20SBC-200.md),
at ports 7CH (data) / 7DH (status+control). This is a distilled emulation reference: the
DC/AC electricals, package drawings and application-note prose from the datasheet are
omitted; only the programmer-visible register model, the state machine and the framing
behaviour needed to emulate the chip are kept.

The chip is fully double-buffered (separate transmit and receive holding + shift registers)
and can be driven **polled** (read the status register) or **interrupt-driven** (the TxRDY
and RxRDY pins). It supports 5–8-bit characters, 1/1½/2 stop bits, optional even/odd parity,
and a baud-rate factor of ×1, ×16 or ×64 in async mode.

---

## 1. Pins (programmer-relevant)

| Pin | Function |
|-----|----------|
| **D7–D0** | 8-bit bidirectional data bus to the CPU |
| **C/D̄** | Control/Data select. **1 = control/status, 0 = data.** On the SBC this is address bit A0 (7DH = control, 7CH = data) |
| **R̄D** | Read strobe (CPU reads data or status) |
| **W̄R** | Write strobe (CPU writes data or control/command) |
| **C̄S** | Chip select (low = selected) |
| **RESET** | A **high** forces the chip to an idle state; it stays idle until a new **mode** word is written. (This is the hardware reset pin — distinct from the software "internal reset" command bit.) |
| **CLK** | Chip clock for internal timing (not the baud clock) |
| **TxD / RxD** | Serial transmit / receive data |
| **TxC̄ / RxC̄** | Transmit / receive baud clocks (×1/×16/×64 of the baud rate in async) |
| **TxRDY** | Transmitter ready — the holding register can take another byte from the CPU |
| **TxEMPTY** | Transmitter empty — holding **and** shift registers are both drained |
| **RxRDY** | Receiver ready — an assembled character is waiting for the CPU |
| **SYNDET/BD** | Sync detect (sync mode) / break detect |
| **D̄SR** | Data-Set-Ready **input**, general purpose. Its level is readable in status bit D7. *(The SBC-200 straps RxD to this pin for the monitor's auto-baud — see the board reference.)* |
| **D̄TR** | Data-Terminal-Ready **output**, set low by command bit D1 |
| **C̄TS** | Clear-to-Send **input**; a low, with the TxEN command bit set, enables transmission |
| **R̄TS** | Request-to-Send **output**, set low by command bit D5 |

**CPU access decode** (C/D̄·R̄D·W̄R, C̄S low):

| C/D̄ | R̄D | W̄R | Operation |
|:---:|:--:|:--:|-----------|
| 0 | 0 | 1 | 8251 → data bus (**read receive data**) |
| 0 | 1 | 0 | data bus → 8251 (**write transmit data**) |
| 1 | 0 | 1 | **status** → data bus (**read status**) |
| 1 | 1 | 0 | data bus → **control** (**write mode or command**) |

---

## 2. Programming sequence and the write-target state machine

The single control address (C/D̄=1, write) is used for **both** the mode word and command
words, disambiguated by an internal one-bit state, **not** by the address:

1. After a **RESET** (hardware pin, or the software internal-reset command), the chip expects
   a **MODE** word. The next control write is taken as the mode instruction.
2. In sync mode only, one or two **SYNC-CHARACTER** writes follow the mode word.
3. **Every** control write after that is a **COMMAND** word.
4. A command word with the **Internal-Reset bit (D6)** set returns the chip to step 1 (it
   again expects a mode word). This is how a driver reprograms the frame on the fly.

A driver must therefore always: (reset) → write mode → [sync chars] → write command, before
transferring data. (The SD MS-monitor does exactly this: `OUT 7D,4E` mode, `OUT 7D,37`
command; later `OUT 7D,40` internal-reset, `OUT 7D,<mode>`, `OUT 7D,37` command again.)

---

## 3. MODE instruction — asynchronous

Bit layout `[D7 D6 | D5 D4 | D3 D2 | D1 D0]` = `[S2 S1 | EP PEN | L2 L1 | B2 B1]`:

| Field | Bits | Values |
|-------|------|--------|
| **Baud-rate factor** | D1 D0 | `00` = sync mode; `01` = ×1; `10` = ×16; `11` = ×64 |
| **Character length** | D3 D2 | `00` = 5, `01` = 6, `10` = 7, `11` = 8 data bits |
| **Parity enable** | D4 | `1` = enable, `0` = disable |
| **Even parity** | D5 | `1` = even, `0` = odd (only if D4=1) |
| **Stop bits** | D7 D6 | `00` = invalid, `01` = 1, `10` = 1½, `11` = 2 |

Baud-factor `00` selects **sync** mode and reinterprets the word (below). Examples used by
the SD monitor: `4EH` = `01001110` = 8 data, no parity, 1 stop, ×16; `4FH` = `01001111` =
same but ×64 (for 150/300 baud on the SBC-200).

**MODE — synchronous** (baud factor `00`): D3 D2 = character length as above; D4 parity
enable; D5 even/odd; **D6 = single(1)/double(0) sync character**; **D7 = external(1)/internal(0)
sync detect**. Sync mode is not exercised by the SD monitor; model the async path fully and
the sync path minimally.

---

## 4. COMMAND instruction

Bit layout `[D7 D6 | D5 D4 | D3 D2 | D1 D0]` = `[EH IR RTS ER SBRK RxE DTR TxEN]`:

| Bit | Name | Effect |
|:---:|------|--------|
| D0 | **TxEN** | Transmit enable (`1` = enable) |
| D1 | **DTR** | `1` forces the D̄TR output low (asserted) |
| D2 | **RxE** | Receive enable (`1` = enable) |
| D3 | **SBRK** | Send break — `1` holds TxD low (spacing) |
| D4 | **ER** | Error reset — `1` clears the PE, OE and FE status flags |
| D5 | **RTS** | `1` forces the R̄TS output low (asserted) |
| D6 | **IR** | Internal reset — `1` returns the chip to expecting a mode word |
| D7 | **EH** | Enter hunt mode — `1` searches for sync characters (sync mode only) |

Example: `37H` = `00110111` = TxEN + DTR + RxE + ErrorReset + RTS.

---

## 5. STATUS register (C/D̄=1 read)

Bit layout `[D7 D6 | D5 D4 | D3 D2 | D1 D0]` = `[DSR SYNDET FE OE PE TxEMPTY RxRDY TxRDY]`.
All flags are **active-high**. D0/D1/D2/D7 mirror the like-named output/input pins so the
chip works polled or interrupt-driven.

| Bit | Flag | Set / clear |
|:---:|------|-------------|
| D0 | **TxRDY** | Set when the transmit holding register is free (can take a byte). Reset automatically when the CPU writes data. *(On the 8251A the status TxRDY also requires TxEN and C̄TS; the status bit here means "buffer empty" — see the note.)* |
| D1 | **RxRDY** | Set when a complete character has been assembled and is waiting. Reset automatically when the CPU reads the data register |
| D2 | **TxEMPTY** | Set when both holding and shift registers are empty (nothing left to send). Reset when the CPU loads a character |
| D3 | **PE** (parity error) | Set on a parity mismatch; reset by the ER command bit. Does **not** inhibit operation |
| D4 | **OE** (overrun error) | Set when the CPU did not read a character before the next one was assembled; the **previous** character is lost. Reset by ER; does not inhibit operation |
| D5 | **FE** (framing error, async) | Set when a valid stop bit was not detected at the end of a character; reset by ER; does not inhibit operation |
| D6 | **SYNDET/BD** | Sync detect (sync mode) / break detect; reset by a status read (sync) |
| D7 | **DSR** | Reflects the D̄SR **input pin** — set when the pin is low (asserted). General-purpose input, tested by a status read |

**TxRDY vs TxEMPTY, and C̄TS gating.** The transmitter will not begin sending until the TxEN
command bit is set **and** the C̄TS input is low; TxD idles marking (high) after reset. TxRDY
signals room in the holding register (double-buffered, so it can rise while a character is
still shifting out); TxEMPTY signals the shift register has also drained — used to know when
to turn the line around in half-duplex.

---

## 6. Asynchronous framing

- **Transmit.** For each byte the CPU writes, the chip frames it as: start bit (space/low) →
  data bits **LSB first** → optional parity bit → stop bit(s) (mark/high), shifted out on the
  falling edge of TxC̄ at 1/16 or 1/64 of TxC̄ (async factor). With no data, TxD stays marking
  unless a break is commanded.
- **Receive.** RxD idles marking. A falling edge starts a candidate start bit, revalidated at
  its nominal centre; the bit counter then samples each data bit and the stop bit at their
  centres. Parity and framing are checked; a bad stop bit sets FE. The assembled character
  raises RxRDY. If the previous character was not read, OE is set and it is lost.

---

## 7. Emulation checklist

- **One control address (C/D̄=1) is mode, command and status** — a write-target state machine,
  not three addresses. First control write after reset/internal-reset = **mode**; thereafter
  = **command**; command D6 (internal reset) rewinds to expecting mode.
- **MODE async** `[S2 S1|EP PEN|L2 L1|B2 B1]`; **COMMAND** `[EH IR RTS ER SBRK RxE DTR TxEN]`;
  **STATUS** `[DSR SYNDET FE OE PE TxEMPTY RxRDY TxRDY]`, all active-high.
- **TxRDY = D0, RxRDY = D1, DSR = D7.** Data LSB-first. TxEN + C̄TS gate transmission.
- **PE/OE/FE** report line noise; with a byte-clean transport there is none, so an emulation
  models them as always-0 (the same stance as the 6850/COM2502 here) until a real serial-port
  endpoint can report a genuine error.
- **DSR (D7) is the D̄SR input pin.** A board that straps another signal to that pin (the
  SBC-200 straps RxD) makes D7 reflect it — the mechanism behind the SBC-200 auto-baud.
