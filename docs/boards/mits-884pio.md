# MITS 88-4PIO — 4-Port Parallel Input/Output Board

**Status:** implemented, `type = "4pio"` — **polled**, pragmatic 6820 (see *Limitations*)

## The real hardware

The MITS **88-4PIO** (© 1975; third printing March 1977) is built around the **Motorola 6820
PIA** (Peripheral Interface Adapter). Because the 6820 holds its own control and data registers,
nearly every option is *software*-selectable at run time: the direction of each of the eight data
lines (the data-direction register, DDR) and the interrupt/control structure. A board can be
populated with up to **four 6820s** — the "PORTS," ICs J, K, L, M — making it the fully
programmable cousin of the fixed-direction [88-PIO](mits-88pio.md).

Each board occupies **16 addresses** (4 per port), on a 16-address boundary strapped by jumpers
A4–A7. Within a port, address bits A3,A2 pick the 6820; A1 picks the **section** (A or B); A0
picks **control/status** vs **data-or-DDR**. Each section is an 8-bit data path with a
control/status register, a DDR, and an interrupt-request output.

## Sources

| Source | Path | Authority |
|---|---|---|
| *88-4 Parallel Input/Output Board*, MITS, © 1975 (3rd printing 1977) | `reference/MITS 88-4PIO.md` (distilled from `MITS 88-4PIO.pdf`) | **Authoritative.** The addressing (port/section/register), the control/status register bit map, the DDR direction rule, the C1/C2 tables, the power-on reset, and the polled/interrupt paths. |
| I/O Address Selection Chart | same, §6 | **Authoritative** for the 16-aligned base-address jumper encoding. |

## Register reference

Addressing: `addr = base + port*4 + section*2 + reg`.

| A3 A2 | Port (IC) | | A1 | A0 | Selects |
|---|---|---|---|---|---|
| 00 | J | | 0 | 0 | A control/status |
| 01 | K | | 0 | 1 | A data or DDR |
| 10 | L | | 1 | 0 | B control/status |
| 11 | M | | 1 | 1 | B data or DDR |

The data address reaches the **DATA** register or the **DDR** depending on **control-register
bit 2** (0 → DDR, 1 → data). Writing 0 to a DDR bit makes that line an input; 1 an output.

### The control/status register

| bit | 7 | 6 | 5 4 3 | 2 | 1 0 |
|---|---|---|---|---|---|
| function | IRQ1 flag (status) | IRQ2 flag (status) | C2 control | DDR select | C1 control |

Bits 7 and 6 are status only (unaffected by a write). **Bit 7** (the C1/IRQ1 flag) is set when
input data arrives and **cleared when the data register is read** — the standard 6820 poll.

## How it is simulated

**A pragmatic 6820 register file** (`src/boards/mits-884pio.{h,cpp}`). The board holds one
`Section` per section (control/status, DDR, data register, a one-byte input latch, and a
`ByteStream`), and exposes two connectable units per populated port.

- **Decodes** `IoRead`/`IoWrite` in a 16-address window from `base_`, but only for the populated
  ports (`ports` × 4 addresses). `port` **must be 16-aligned** (the board answers a 16-address
  block), refused with a reason otherwise. `ports` (1–4) selects how many 6820s are populated,
  which grows the unit list (`ja`,`jb`,`ka`,`kb`,…).
- **Reading the control address** returns the stored control bits with the live bit 7 (data
  available). **Reading the data address** returns the input latch (and clears bit 7) when the
  data register is selected, or the DDR when it is not.
- **Writing the control address** stores control bits 5..0 (7 and 6 are read-only flags).
  **Writing the data address** sends the byte out on the section's line (data register selected)
  or programs the DDR.
- **`pump()`** drains each section's output line and latches one input byte, setting bit 7 — so a
  polling driver (poll bit 7, read data) works.
- **Each section is a line you CONNECT** (DESIGN.md §7.7): `file:`, `console`, `socket:`,
  `loopback`, `null`. Each round-trips its endpoint through a per-unit `connect` property.
- Does **not** master the bus, and asserts no interrupt.

### Reset

- `Reset::PowerOn` (POC*, cold) and `Reset::Bus` (RESET*, warm) both clear every section's
  registers: all data lines become inputs (DDR = 0), all flags clear. **The lines stay connected.**

## Quirks reproduced

- **DDR/data share the odd address**, gated by control bit 2. Software programs direction by
  clearing bit 2, writing the DDR, then setting bit 2 to reach data. Get the gate wrong and a
  driver's DDR write lands in the data register (or vice-versa) and direction is never set.
- **Bit 7 clears on a data read**, not on anything else — the poll loop depends on it.

## Limitations and deliberate departures

**Pragmatic 6820, and polled.** Modeled: the control/status register, the DDR, the data
register, and status bit 7 driving the poll. **Stored but not simulated:** the C1/C2 control bits
(a guest may write them freely) and the **CA2/CB2 output-strobe timing** from the manual's
handshake tables — those are invisible through a byte-stream endpoint. Like the C700 the card is
**polled**: no interrupt wire is pulled (issue #26).

**DDR is not bit-masked onto the stream.** The DDR is stored and reported, but a `ByteStream`
carries all 8 bits, so a section connected to an endpoint moves whole bytes regardless of which
individual lines the DDR marks as inputs or outputs. A section used purely as an 8-bit input or
8-bit output port — the common case — behaves exactly right; a genuinely mixed-direction byte is
not split.

## Verification

`tests/test_4pio.cpp`, with `Bus::setVerify(true)` on:

1. **The card** — one populated port shows two sections (`ja`,`jb`); `ports = 2` grows it to four.
2. **Decode** — 16 addresses from a 16-aligned base, only the populated ports; an unaligned base
   is refused with a reason and the decode follows the base.
3. **The DDR** — reached through the data address when control bit 2 is 0, and *not* once bit 2
   is set.
4. **Output** — a byte written to a section's data register goes out on its line, in order.
5. **Input** — a byte sets status bit 7 on `pump()`, is read from the data register, and clears
   bit 7.
6. **Independence** — the two sections move their own bytes.
7. **Polled** — a ready section pulls no wire.
8. **`connect`/`disconnect`** — round-trips the per-section endpoint spec and names a real one.

## References

- `reference/MITS 88-4PIO.md` — the distilled *88-4 Parallel Input/Output Board* manual.
- `docs/boards/mits-88pio.md` — the fixed-direction, discrete-TTL cousin.
