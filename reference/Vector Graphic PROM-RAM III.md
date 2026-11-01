# Vector Graphic PROM/RAM III — 12K of PROM, 1K of RAM and a jump on reset

Source: [PROM-RAM-III.pdf](#)

Vector Graphic *PROM/RAM III Board*, Revision 1, and *PROM Programming Program*, Revision 1, Users
Manual Revision A, July 16, 1979, © 1979, 42 PDF pages. This is the board that the older Vector
Graphic machines use for the Monitor. It also programs a 2708 or 2704 in one socket. This file omits
the listing of the program (2-18 to 2-24) and the repair agreement.

**Not emulated.** `machines/vector.toml` has a stand-in memory board for the Monitor.

---

## 1. Quick reference for emulation

| Item | Value |
|---|---|
| Blocks | Two independent 8K blocks, A and B, on 8K boundaries |
| Block A | Up to 8K of PROM, sockets 0–7. **Disabled as shipped** |
| Block B | 4K of PROM, sockets 8–11, then 4K of scratchpad that holds the 1K of RAM. **At C000 as shipped** |
| Standard map | PROM C000–CFFF, RAM DC00–DFFF, D000–DBFF left for other boards |
| Monitor | C000 |
| RAM | 2114, 300 ns |
| PROM | 2708 (1K) or 2704 (½K, each followed by a ½K gap) |
| Reset | `POC` (or `PRESET` by jumper) jumps to the first address of block B, with phantom on line 67 |
| Wait state | Optional, one for each access (area K). Off as shipped |

---

## 2. Address jumpers (2-2 to 2-7, Tables 1 to 3)

Both blocks together cover 16K. A block that is enabled owns all of its 8K, even with empty sockets.
The exception is the 3K that areas H and I give back to other boards.

**Areas E and F.** Area E sets block B with pads BA1, BA2 and BA3. Area F sets block A with pads BB1,
BB2 and BB3. Tying any of the three pads to `DISABLE` turns the block off.

| Start | BA1/BB1 | BA2/BB2 | BA3/BB3 |
|---|---|---|---|
| 0000 | /A13 | /A14 | /A15 |
| 2000 | A13 | /A14 | /A15 |
| 4000 | /A13 | A14 | /A15 |
| 6000 | A13 | A14 | /A15 |
| 8000 | /A13 | /A14 | A15 |
| A000 | A13 | /A14 | A15 |
| C000 | /A13 | A14 | A15 |
| E000 | A13 | A14 | A15 |

**Area G, block swap.** Shipped with 20–21 and 25–26. Tying 20–25 and 21–26 swaps areas E and F, and
the reset jump then goes to block A.

**Area J, PROM at top or bottom.** Shipped with 6–7, PROM in the low 4K of block B. Cut 6–7 and tie
6–8 to put the PROM in the high 4K.

**Areas I and H, the 1K of RAM in the other 4K.**

| RAM at (from the start of the 4K) | Area I | Area H, the 3K given back |
|---|---|---|
| 0000 | 18 to RA0 | 15–16, 13–14, 11–12 |
| 0400 | 18 to RA4 | 15–17, 13–14, 11–12 |
| 0800 | 18 to RA8 | 15–17, 13–16, 11–12 |
| 0C00 (shipped) | 18 to RAC | 11–14, 13–16, 15–17 |

**Sockets of block A.** Socket *n* is at A + *n* × 400H.

**The standard map (Figure 2).** From the start of block B: PROM, video 1000–17FF, disk 1800–1BFF,
RAM 1C00–1FFF. Figure 3 is the inverse: disk 0000–03FF, RAM 0400–07FF, video 0800–0FFF, PROM
1000–1FFF. The video and disk ranges are other boards, in the 3K given back. At the standard C000,
that puts video at D000 and the disk at D800.

---

## 3. Reset (2-8 to 2-10, 3-3)

| Area | Shipped | Effect |
|---|---|---|
| D | 27–28, `POC` | Cut it and tie 28–29 for `PRESET` |
| C | 1–2 | Generate phantom on line 67 at reset. Cut to stop it |
| A | 3–4 | Answer 0000 at reset. Cut 3–4 and tie 4–5 to stop it |

An RS flip-flop in U20 holds phantom and enables the decoder, so the block answers address 0000. The
processor runs the first instruction of the block. If that instruction is a jump to the next
instruction in the block, the flip-flop resets and phantom ends. The first instruction must
therefore be that jump.

In standard Vector Graphic computers, the RESET switch and power-on both pulse `POC` low through the
Z-80 board. Those computers put that jump first (2-8).

---

## 4. MWRITE and wait state (2-10, 2-11, 3-3)

- **Area B, 9 to 10:** generate `MWRITE` on the board and drive the bus. Not needed in Vector Graphic
  machines shipped after April 9, 1979, because their Z-80 board generates it.
- **Area K, 22 to 23:** tie `PRDY` to `WAIT` for one wait state each time the board is addressed.

---

## 5. Programming a PROM (2-13 to 2-16, 3-4)

- **Socket 11 only.** It is at CC00–CFFF on the standard map.
- **Any memory write to socket 11 is a program request** while the slide switch at the upper right
  is on. Software needs no port.
- **Timing:** 10 µs delay, then a 400 µs pulse of 26 V on pin 18. `XRDY` holds the processor in a
  wait for the pulse and ½ µs more.
- **Each byte takes 256 writes,** by passes over the whole range. The range is at least 16 bytes, on
  16-byte boundaries.
- **A dynamic RAM board needs a delay.** After each byte the program must run at least 128
  instructions, so that the processor refreshes memory.

---

## 6. The PROM program (2-13 to 2-24)

The program is called `PROM`. It runs at 2B00, takes less than 1K, and is on MDOS system disks from
version 8.4. It asks "Starting from:", "terminating at:" and "Source address:". Addresses are
checked to CC00–CFF0 and CC0F–CFFF, ending in 0 and F. It uses the Monitor for console I/O through
the Monitor's jumps: `CALL C003` for input and `CALL C008` for output. `ESC` returns to the Monitor,
`J` from there returns to MDOS, and `Q CC00 CFFF` at the Monitor computes a checksum.

---

## Quirks worth carrying

- **⚠ The errata page is missing.** The contents list *Schematic Errata* at 4-2. The scan goes from
  the layout at 4-1 to the schematic, with no errata page.
- **⚠ Duplicate pages.** Pages 2-14 and 2-15 are in the scan twice.
- **⚠ Section number.** The contents give 3.4 twice. The Power Supplies section is 3.5 in the text.
- **⚠ A wait-state fault that the manual admits (3-3).** With area K jumpered, the wait state also
  comes on the 3K that other boards use, such as video and disk.
- **⚠ Keep the program switch off.** A write to socket 11 programs it while the switch is on (2-16,
  3-4). The switch stays off outside a programming run.
- **⚠ Block letters.** Block B pads are named BA and block A pads are named BB. The manual says the
  pads were named before the block swap of area G was reversed (2-6).
