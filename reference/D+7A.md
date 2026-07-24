# Cromemco D+7A I/O Board

Source: [D+7A Rev C Manual.pdf](#)

Cromemco, Inc., "D+7A I/O" (the D+7AI/O module), Rev B/C/D manuals + Rev B schematic,
fetched from deramp.com (`.../00-Cromemco/10-Cromemco S100 Boards/D+7A/`). An S-100
analog **and** parallel I/O card: **seven 8-bit A/D input channels, seven 8-bit D/A
output channels, and one 8-bit parallel port** (a byte in, a byte out), in a single
block of eight consecutive I/O ports. Its most famous use is the input+sound end of a
Cromemco Dazzler game console: one D+7A reads one or two [JS-1 joysticks](JS-1.md) and
drives their speakers. Cromemco's own list of applications: "joystick interfaces,
oscilloscope graphics, music and voice synthesis, and process control."

This file captures what is needed to *emulate* the board: the eight-port block, the
two's-complement analog scale, the parallel port, and the conversion timing. Assembly,
calibration (the four trim pots), the op-amp/sample-and-hold analog chain, and the
schematic are omitted.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| Ports | **8 consecutive**, jumper-selected (5 jumpers = A7..A3). Recommended base **030 octal = 0x18** → 0x18–0x1F |
| BASE+0 (0x18) | **Parallel port** — `IN` reads the 8 parallel input lines, `OUT` latches the 8 parallel output lines. Independent. |
| BASE+1..BASE+7 (0x19–0x1F) | **Seven analog channels.** `IN <ch>` = the A/D of that channel's analog input; `OUT <ch>` = that channel's D/A output. Read and write are **independent** (in one direction it is an A/D input pin, in the other a D/A output pin). |
| Analog format | **8-bit two's-complement**, 20 mV per LSB, range **−2.56 V (0x80) … 0 V (0x00) … +2.54 V (0x7F)** |
| A/D time | 5 µs conversion; the board holds the S-100 **READY** line low **5.5 µs = 11 wait states** on every analog `IN` (and the same 5.5 µs on every analog `OUT`, for the D/A sample-and-hold to settle) |
| Parallel port | plain 8-bit latches; a `STB` handshake exists on the connector but there is no status register — the port is read/written directly |
| With a Dazzler | REV B / REV B-1 Dazzlers need pin 10 of Dazzler IC 29 (a 7400) lifted to stop analog cycles flashing the picture; **REV C Dazzlers need no change** (emulation: nothing to model) |

Two 88-CPU-style details: the port jumpers select `A7..A3`, so the block is always
8-aligned; `A2..A0` pick the channel within it. The two voltage regulators (7805 /
7905) and the ±12 V the JS-1 pots run on are power, not software-visible.

---

## 2. The port block

Five jumper wires above IC 30 select `A7 A6 A5 A4 A3`; `A2 A1 A0` then choose one of
the eight ports inside the block. The manual's recommended straps give **030–037
octal**:

```
  BASE+0   030o  0x18   parallel:  IN = parallel input byte / OUT = parallel output byte
  BASE+1   031o  0x19   analog 1:  IN = A/D of input 1      / OUT = D/A output 1
  BASE+2   032o  0x1A   analog 2
  BASE+3   033o  0x1B   analog 3
  BASE+4   034o  0x1C   analog 4
  BASE+5   035o  0x1D   analog 5
  BASE+6   036o  0x1E   analog 6
  BASE+7   037o  0x1F   analog 7
```

`OUT 030` (the digital port) is called out in the manual as a convenient scratch
output — the calibration program uses it because "output port 030 is available on
D+7A."

## 3. The two's-complement analog scale

Both A/D and D/A use 8-bit two's-complement, LSB = 20 mV, so the byte and the voltage
line up exactly:

| Byte | Voltage |
|------|---------|
| `0111_1111` = 0x7F | +2.54 V |
| `0000_0001` = 0x01 | +0.02 V |
| `0000_0000` = 0x00 | 0 V |
| `1111_1111` = 0xFF | −0.02 V |
| `1000_0000` = 0x80 | −2.56 V |

**Emulation:** store the raw byte; the volts are documentation only. A joystick pot
centered → 0x00, full one way → 0x80, full the other → 0x7F. Nothing in the simulator
needs volts — an A/D read hands back a byte and a D/A write takes one.

## 4. Conversion timing (the wait states)

From the Rev C addendum: "Conversion time for the D+7A A-to-D converter is 5
microseconds. … the READY line is held down for 5.5 microseconds, i.e. 11 wait states,
whenever data is input from one of the seven analog input ports. The ready line is also
held down for 5.5 microseconds when data is output to one of the seven analog output
ports to assure adequate time for settling of the analog sample and hold amplifier."

**Emulation stance:** the wait states are **not modeled** in v1 — for the same reason
the Dazzler's DMA slowdown is not: `read()`/`write()` are pure over state and the bus
does not charge per-cycle wait states to the `Clock`. The wait states only become
audible when reproducing JS-1 **sound pitch** precisely (they lengthen the CPU's
sample-output loop), and sound is a separate follow-up (see
`docs/boards/cromemco-d7a.md`). If bit-exact sound timing is ever wanted, the hook is
charging 11 T-states when a D+7A analog cycle is decoded.

## 5. Connector (informative)

The top edge connector brings out the 7 analog inputs, 7 analog outputs, the parallel
input byte, the parallel output byte, `INPUT STB` / `OUTPUT STB`, and power
(±12 V regulated, ±17 V unregulated, +5 V). The [JS-1](JS-1.md) 12-conductor cable
lands here; `reference/JS-1.md` gives the recommended per-console port assignment.

## 6. Test program (calibration — a ready-made exerciser)

The Rev C manual's A/D calibration loop reads analog channel 7 and echoes it to the
digital output port — a one-loop exerciser of an analog `IN` and a parallel `OUT`:

```
  DB 37    IN  37H    ; read analog channel 7 (A/D)
  D3 30    OUT 30H    ; echo to the parallel output port
  C3 00 00 JP  0000H  ; loop
```

The D/A calibration loop writes a constant to analog channel 7 (`3E 7F / D3 37 / C3
00 00`) and measures +2.54 V on the pin — i.e. `OUT 037,7F` → the most-positive D/A
output. Both are good headless assertions: feed a stub A/D value and read the parallel
port; write a D/A byte and read it back through the test accessor.

**A period joystick diagnostic confirms the game port map.** `ADCTEST` (`adctest.hex`,
disassembled 2026-07-23) turns on a Dazzler, then polls `IN 18H` (buttons), `IN 19H`/`1AH`
(console 1 X/Y) and `IN 1BH`/`1CH` (console 2 X/Y) — the exact ports and split
[`reference/JS-1.md`](JS-1.md) describes.
