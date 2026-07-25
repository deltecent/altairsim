# SD Systems SBC-100 & SBC-200 Single-Board Computers

Source: [SBC-100 Manual.pdf](#), [SDS_SBC-200.pdf](#)

SD Sales / SD Systems, Dallas TX. Two revisions of one S-100 **Z80 single-board computer** —
CPU, serial port, parallel port, counter/timer, RAM and boot-PROM sockets on a single S-100
board that acts as the system's bus master. The **SBC-100** (Rev A, 1980) runs at 2.4576 MHz;
the **SBC-200** (Rev C, 1981) is the same architecture clocked at 4 MHz with a couple of added
features. They share their entire I/O port map, register layout and memory-mapping scheme, so
they are documented together with the differences called out in §8. This is a distilled
emulation reference: kit-assembly steps, parts lists, PCB layouts, alignment procedures,
marketing and prices from the original manuals are intentionally omitted; only what is needed
to emulate the board in software is kept.

The boot PROM that ships in these boards is the SD/MS monitor — see
[`SD Systems Monitor.md`](SD%20Systems%20Monitor.md). Together with a
[`VersaFloppy`](SD%20Systems%20VersaFloppy.md) controller they run
[`SDOS`](SD%20Systems%20SDOS.md) or CP/M.

---

## 1. What the board is

- **CPU: Zilog/Mostek Z80** (MK3880 on the SBC-100, MK3880-4 / Z80A on the SBC-200). Both are
  strictly Z80 — neither manual offers an 8080/8085 option.
- **Serial: one Intel 8251 USART** (8251 on SBC-100, 8251A on SBC-200), asynchronous or
  synchronous.
- **Counter/timer: one Mostek MK3882 Z80-CTC**, four 16-bit channels. Channel 0 is normally the
  16× (or 64×) baud-rate clock for the USART; the four channels can also serve as Z80 mode-2
  vectored-interrupt inputs.
- **RAM: 1 KB static scratchpad** (2114/4114), strappable to any address.
- **PROM: four sockets** (ROM 0–3), each holding a 1K/2K/4K/8K device, strappable anywhere in
  the map. The board's PROM normally holds the monitor.
- **Parallel: one 8-bit input port and one 8-bit output port**, each with two handshake lines.
- **Interrupts: four maskable vectored inputs (via the CTC) plus NMI.**
- **S-100 bus master.** The board *is* the CPU board; it releases the bus on DMA (address,
  data-out and status buffers tri-state while `BUSAK=1`). Onboard memory takes priority over
  off-board memory at the same address.

Connectors: **J1** = S-100 bus, **J2** = serial I/O (26-pin), **J3** = parallel I/O (26-pin).

---

## 2. I/O port map (identical on both boards)

Addresses are **hex only** (the manuals use no octal). Decoded on the low address bits.

| Port | Device / function |
|------|-------------------|
| **78H** | CTC channel 0 (baud-rate generator) |
| **79H** | CTC channel 1 |
| **7AH** | CTC channel 2 |
| **7BH** | CTC channel 3 |
| **7CH** | **8251 USART — data** (`IN` = RX data, `OUT` = TX data) |
| **7DH** | **8251 USART — status (`IN`) / control (`OUT`)** |
| **7EH** | Parallel port **data** latch — `OUT` writes the output latch, `IN` reads the input latch |
| **7FH** | Parallel port **handshake/control** register (§4); a **read of 7FH also releases the auto-start jam** (§5) |

This is the console the monitor and every SD OS uses: **console data = 7CH, console status =
7DH** (the "MS" monitor build's `CDATA`/`CSTAT` equates).

---

## 3. Serial section (8251 USART, ports 7CH/7DH)

Standard Intel 8251 register model. Status byte at `IN 7DH`, bits (both flags **active-high**):

| Bit | 8251 status | Meaning |
|-----|-------------|---------|
| D0 | **TxRDY** | transmitter ready — `OUT 7CH` will be accepted |
| D1 | **RxRDY** | a received byte is available at `IN 7CH` |
| D2 | TxEMPTY | transmitter shift register empty |
| D3 | PE | parity error |
| D4 | OE | overrun error |
| D5 | FE | framing error |
| D6 | SYNDET | sync detect |
| D7 | DSR | data-set-ready input |

The monitor's poll idioms (the emulation test vectors):

```
    IN   A,(7DH) / AND 1  / JP Z,txwait   ; wait for TxRDY (bit 0)
    IN   A,(7DH) / AND 2  / JP Z,rxwait   ; wait for RxRDY (bit 1)
    IN   A,(7CH) / AND 7FH                ; read byte, strip parity bit
```

**Baud generation.** The USART runs in 16× (or 64×) mode off CTC channel 0 at port 78H. The
monitor programs the 8251 mode/command, then loads the CTC time-constant:

```
    LD A,4EH / OUT (7DH),A     ; 8251 mode  (4FH on SBC-200 for 150/300 baud → ÷64)
    LD A,37H / OUT (7DH),A     ; 8251 command (RxE, TxEN, DTR, RTS)
    LD A,<ctc> / OUT (78H),A   ; CTC ch0 control (05H on SBC-100, 45H on SBC-200)
    LD A,<const> / OUT (78H),A ; CTC time constant (baud, table below)
```

**SBC-100** baud table (φ = 2.4576 MHz, USART ×16 throughout):

| Baud | 110 | 300 | 600 | 1200 | 2400 | 4800 | 9600 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| CTC const (hex) | 57 | 20 | 10 | 08 | 04 | 02 | 01 |

**SBC-200** baud table (φ = 4.00 MHz; USART ÷64 for 150/300, ÷16 for 600–9600):

| Baud | 150 | 300 | 600 | 1200 | 2400 | 4800 | 9600 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| CTC const (hex) | D0 | 68 | D0 | 68 | 34 | 1A | 0D |

The monitor **auto-detects baud** by timing the start bit of the first character the user types
(a CR), then loading the matching constant — which is why the serial jumpers loop RXD back to
DSR in "terminal" mode.

---

## 4. Parallel section (ports 7EH/7FH)

- **Output:** `OUT 7EH` latches 8 bits to J3 (tri-state, optionally gated by the `ORPLY` input).
  `OSTB` (output strobe) is a one-bit latch at **7FH bit 0**, jumperable as a positive or
  negative pulse and optionally auto-reset by `ORPLY`. `ORPLY` (device-ready) is read at
  **7FH bit 0**.
- **Input:** an external `ISTRB` positive edge sets a flip-flop; **`IN 7FH` bit 1 = 0 means a
  byte is available** (active-low "data ready"). The flop clears when the byte is read from
  `IN 7EH`; its complement drives `IRPLY` back to the sender.

J3 pinout: pin 1 GND; odd pins 3–17 = PDO0..PDO7; 19 = ORPLY; 21 = OSTB; 23 = +5 V; even pins
4–18 = PDI0..PDI7; 20 = IRPLY; 22 = ISTRB.

**⚠ SBC-200 only:** `IN 7FH`/`OUT 7FH` **bit 1 also switches the on-board memory in and out.**
`OUT 7FH` with `A=2` switches the onboard 1 KB RAM/PROM *out* of the map; `A=0` switches it back
*in*. The board always has onboard memory enabled after reset; when switched in, writes to the
1 KB RAM also write through to any off-board RAM at that address. Bit 0 remains the parallel
output handshake. The SBC-100 does not have this mechanism.

---

## 5. Memory mapping, boot PROM window and auto-start

**Memory-mapping headers** (identical scheme on both):

- **X1** — ROM chip-size selection (1K/2K/4K/8K per socket) and the memory-bank select
  (A10–A15) that places the board's whole window in an 8K/16K/32K/64K bank.
- **X2** — ROM *type*: routes A10/A11/A12 to the socket's address pins for the fitted device
  (2758/2716/2732 EPROMs; 2308/2316/2332 mask ROMs; Mostek 34000/32000/36000; 93451 PROM).
  Boards ship etch-jumpered for **2716** EPROMs in the top bank.
- **X3** — per-socket placement (ROM 0/1/2 low or high position, ROM 3, RAM). Only sockets
  jumpered on X3 occupy the map, so the board can claim just its 1 KB alongside a 64 KB
  EXPANDORAM.

**Auto-start (boot-from-high-PROM).** On reset the board forces the Z80 to begin execution at a
**4K boundary** (the auto-start address) instead of `0000H`, so a boot PROM living high in
memory runs first. The boundary is set by jumpers **X16/X17/X18**. The first two instructions
of the boot code release the override:

```
X000  JP  X003        ; C3 03 X0
X003  IN  A,(7FH)      ; DB 7F  — reading port 7FH clears the auto-start jam
```

After that, normal memory decoding resumes. (The override is not needed only when X = 0, i.e.
resetting straight to `0000H`.)

- The **SD/MS monitor PROM resides at `E000H`**; a floppy boot is done by setting the auto-start
  to **`F000H`**, where the disk BIOS (DDBIOS) PROM lives. The single jumper **X18-2** selects
  E000 vs F000 as the etch default.
- There is **no separate "phantom" jumper** documented: the auto-start override plus onboard
  memory priority plus the port-7FH clear together implement the high-PROM boot. (A `PHANTOM`
  net exists on the SBC-200 schematic but is not user-documented.)

---

## 6. Interrupts

Four maskable vectored inputs through the CTC (channels at 78H–7BH) plus NMI. Header jumpers map
the CTC channels to the S-100 vectored-interrupt lines: Ch0←VI1, Ch1←VI2/SYNDET,
Ch2←VI3/serial-RxRDY, Ch3←VI4/serial-TxRDY. The board sits in the Z80 mode-2 daisy chain (IEI/
IEO). Reset always begins in the auto-start/monitor path (§5), not an interrupt.

---

## 7. Reset behavior summary

On power-on / reset: onboard memory enabled, Z80 begins at the X16/X17/X18 auto-start 4K
boundary (E000H for the monitor, F000H for a floppy boot), runs `JP`+`IN 7FH` to drop the
override, then the monitor initializes the 8251 (mode 4E/4F, command 37) and CTC ch0, auto-
detects baud from the first typed CR, and prints its `.` prompt (see the Monitor reference).

---

## 8. SBC-100 vs SBC-200 — differences

| Aspect | SBC-100 | SBC-200 |
|--------|---------|---------|
| CPU | Z80 (MK3880) | Z80A (MK3880-4) |
| Clock (φ) | **2.4576 MHz** (4.9152 MHz xtal ÷2) | **4.00 MHz** (16 MHz osc ÷) |
| Clock jumper | X8 (÷2 standard / no-÷2) | X8 (4 MHz standard / 2 MHz) |
| USART | 8251 | 8251A |
| CTC | MK3882 | MK3882-A-4 |
| Baud range | 110–9600, all USART ×16 | **150–9600**, USART ÷64 (150/300) or ÷16 |
| 8251 mode byte | `4EH` always | `4EH`, but **`4FH`** for 150/300 baud |
| CTC ch0 control byte | `05H` | `45H` |
| Serial interface | RS-232 **+ 20 mA current loop**; headers X4/X6/X9/X10/X11; fixed cable | RS-232 only; single header **X20** + X11 terminal/printer + X21 baud routing |
| On-board memory switch-out | — | **`OUT 7FH` bit 1** disables onboard memory (§4) |
| DMA driver-disable jumper | — | **X22** (J1-19 pDBDIS vs BUSAK disables the bus drivers) |
| Default etch | Rev B, 2716 top bank | Rev A, 2716 top bank |

**Identical on both:** the whole port map (USART 7C/7D, parallel 7E/7F, CTC 78–7B); 8251 status
polarity (TxRDY=D0, RxRDY=D1, active-high; parallel "data ready" = 7FH D1 active-low); 1 KB RAM;
four ROM sockets; the X1/X2/X3 memory-mapping headers; auto-start via X16/X17/X18 with the port-
7FH release; monitor at E000H / disk BIOS at F000H; J3 parallel pinout; CTC-as-interrupt mapping
to VI1–VI4.

---

## 9. Emulation checklist (summary of load-bearing facts)

- **Z80 CPU, S-100 bus master.** SBC-100 = 2.4576 MHz, SBC-200 = 4.00 MHz.
- **Console = 8251 at 7CH (data) / 7DH (status/control).** TxRDY = status D0, RxRDY = D1, both
  active-high; strip parity with `AND 7FH` after reading data.
- **CTC (MK3882) at 78H–7BH;** channel 0 is the baud generator (control byte 05H SBC-100 / 45H
  SBC-200, then the time constant). Baud is auto-detected from the first typed CR.
- **Parallel at 7EH (data) / 7FH (handshake).** Input "byte ready" = `IN 7FH` D1 **= 0**
  (active-low). **SBC-200: `OUT 7FH` bit 1 switches onboard memory out(1)/in(0).**
- **Reading port 7FH releases the reset auto-start jam.** Reset begins at a 4K boundary
  (X16/X17/X18) — monitor PROM at **E000H**, floppy-boot BIOS at **F000H** — not at 0000H.
- **1 KB RAM, four PROM sockets** (1K/2K/4K/8K each), all strappable; onboard memory has bus
  priority.
- **Interrupts:** four maskable vectored via the CTC (→ VI1–VI4) + NMI; Z80 mode-2 daisy chain.
