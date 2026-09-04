# SD Systems SBC-100 / SBC-200

**Status:** done (the SBC-200 auto-bauds and boots MSMONR21 to its `.` prompt, then loads SDOS
through a VersaFloppy; the SBC-100 is selectable via `variant`; a VDB-8024 video console drives
the same card over an S-100 VI line)

## The real hardware

The **SBC-100 / SBC-200** were SD Systems' (S.D. Sales / S.D. Computer Products, Dallas TX)
S-100 **Z80 single-board computers** — a whole machine on one card: the Z80 CPU, an **Intel
8251** USART, a **Z80-CTC** counter/timer, a parallel port, and RAM plus boot-PROM sockets. The
card is the system's **bus master**.

- **SBC-100** (Rev A, 1980) — 2.4576 MHz.
- **SBC-200** (Rev C, 1981) — the same architecture at 4 MHz, with a couple of added features.

They share their entire I/O port map, register layout and memory-mapping scheme, so they are one
board here with the CPU crystal set on the separate `z80` card. This board models the parts SD's
software actually touches: the 8251 console, the one CTC interrupt SD CP/M uses, the memory
switch-out, and the onboard boot PROM.

**The defining strap** is the 8251's **RxD wired to /DSR**, so **MSMONR21** can auto-detect the
console baud by timing an incoming start bit in status bit 7. It is the etch default (`rxd2dsr`).

## Sources

| Source | Path | Authority |
|---|---|---|
| SD Systems SBC-100 & SBC-200 manuals | `reference/SD Systems SBC-100 & SBC-200.md` | Port map, 8251 orientation, CTC wiring, the RxD→/DSR strap, the memory switch-out, the onboard window layout. |
| MSMONR21 monitor PROM | `builtin:msmonr21` | The auto-baud that reads status bit 7; the `C`/`R`/`W`/`Z` boot commands. |
| Intel 8251 USART data sheet | `src/chips/intel8251.h` | The chip: data at the low port, status/command at the high port — the reverse of the 6850. |

## Register reference

One 8-port I/O block. The 8251 **data** port is the block's fifth port (etch **7C**); the block
starts four ports below it (**78**), so CTC = 78–7B, USART = 7C–7D, parallel = 7E–7F.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| 78–7B | Z80-CTC channels 0–3 (ch0 = baud gen + interrupt vector; ch1 = keyboard interrupt) | `FF` |
| 7C | 8251 transmit data | 8251 receive data (clears RxRDY) |
| 7D | 8251 mode, then command | 8251 status |
| 7E | parallel data latch (no observable effect) | `FF` |
| 7F | parallel handshake; **bit 1 switches the onboard PROM out** | `FF` |

The **8251 orientation is data LOW, status/command HIGH** — the reverse of the 6850 ACIA
section, which is exactly why this card does not reuse the 2SIO's `Sio2Port`.

**The keyboard interrupt** is a Z80 **mode-2 vectored** interrupt (not an S-100 VI line): the
CTC's channel 1 raises `/INT` with vector `vectorBase | 2` whenever a byte is waiting — SD CP/M's
CONIO writes the CTC vector base `0x80` (channel 0, D0=0), so channel 1's vector is **0x82** and
its ISR pointer sits at `FF82`. The ISR reads the 8251 data port and `/INT` drops. The trigger is
the 8251's own RxRDY, **or** — in a video machine — the VDB-8024's keyboard strobe on S-100 **VI2**.

## How it is simulated

`SbcBoard` (`src/boards/sd-sbc.{h,cpp}`) is the structural twin of the 88-SIO: one UART
(`Intel8251`) embedded directly as a member, with the card owning `refresh()`/`nextEdge()`/`wake_`
and the endpoint resolver.

- **Decode:** IoRead/IoWrite of the 8-port block (78–7F, no wrap); the onboard PROM's MemReads
  while it is switched in; and the `IntAck` cycle **only** when the keyboard interrupt is pending
  (like the 88-VI), so an unclaimed acknowledge still floats `0xFF`.
- **The Z80-CTC, as much as is observable.** At flat-out speed the only thing a guest can read
  back is the one interrupt SD CP/M uses, so the model carries exactly the vector register
  (channel-0 write with D0=0) and channel 1's interrupt-enable (a control word with D7). Time
  constants and the baud divider are absorbed. It can graduate to a real `src/chips/z80ctc.*` if a
  timer ever becomes observable.
- **The onboard PROM shadows RAM** while switched in: reads come from the PROM, writes fall
  through to the RAM under it (so the RAM card is `honors_phantom = read`). The same mechanism as
  the Turnkey boot PROM. `OUT 7F` bit 1 = 1 drops it out; bit 1 = 0 (and any reset) restores it.
- **Interrupts:** `assertsInt()` pulls pin 73 when the CTC has armed channel 1 and a byte is
  waiting. The card `watchesVi()` so the bus re-derives `/INT` when the VDB moves VI2.
- **`watchesVi`:** true, for the off-card VDB-8024 keyboard trigger on VI2.
- **DMA:** none here — the real card's Z80 bus-mastering is the CPU card's concern, not this
  board's.
- **Properties:** `variant` (`sbc100`/`sbc200`), `rxd2dsr` (the auto-baud strap), `port` (base,
  etch `7C`). One serial unit `tty`. `[[board.socket]]` (`at` + `mount`) for the onboard PROMs.

### The onboard PROM sockets

The onboard window is **E000–FFFF**. `[[board.socket]]` places a `builtin:` or host HEX/BIN ROM
in it — conventionally the monitor at E000 and the disk BIOS at F000 — over a plain 64K RAM board.
Sockets are **empty by default**, so a machine that keeps its ROMs on a `memory` card
(`machines/sbc200.toml`) is untouched; the socket overlay exists for the authentic single-board
layout. The bytes travel the same Intel HEX/BIN loader as a `memory` card's ROM region.

### Reset

- `Reset::PowerOn` (POC*, cold): the 8251 is powered to a known-good idle; the socket ROMs are
  re-read from the host; the onboard memory is switched **in**.
- `Reset::Bus` (RESET*, warm): the onboard memory is switched **in** (the card comes up with the
  PROM mapped); the receiver is re-polled and the deadline re-armed. The 8251's RESET pin is
  **not** driven from the backplane — the monitor always software-programs the chip (mode then
  command) out of reset, so nothing period-correct can tell.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **8251 orientation: data LOW, status/command HIGH** (reverse of the 6850) | Every monitor status poll reads the data port and every transmit writes to the command port — the console never works. This is why the card does not reuse `Sio2Port`. |
| **RxD strapped to /DSR** — the auto-baud line | MSMONR21 times the first character's start bit in status bit 7 to set its baud; with the strap off it never trains, and the console prints **nothing until you press Enter** (which is authentic, not a hang). |
| **Memory switch-out: `OUT 7F` bit 1 drops the PROM, any RESET restores it** | CP/M's 64K cold boot switches the PROM out so RAM shows through at the top; if the write is missed the guest can't reach the RAM under the ROM, and if RESET doesn't restore it the machine won't cold-start again. |
| **Keyboard interrupt from off-card, on VI2** | A VDB-8024 video console pulls VI2 while a key waits; if the card doesn't watch the VI wire, a key typed on the video console never raises `/INT` and the vectored console driver hangs. |

## Limitations and deliberate departures

- **The card is not a full single-board computer here** — the Z80 core, its bus mastering, and
  the CPU crystal live on the separate `z80` card. This board is the peripheral half SD's software
  touches; `variant` only records which SBC it is (the console, CTC and PROM behave alike).
- **The CTC is modeled only as far as it is observable at flat-out speed** — its vector and the
  channel-1 arm bit. Time constants and the on-card baud divider are absorbed and forgotten,
  because at flat-out speed nothing can read them back.
- **The parallel port is inert** apart from the memory switch-out. `OUT 7E` latches a byte with
  nothing wired to J3, so it has no observable effect; the CTC and parallel ports read back `0xFF`.
- **The 8251's RESET pin is not driven from the backplane**, on the same reasoning as the 88-SIO:
  the monitor software-programs the chip out of reset every time.

## Verification

- **`acceptance-sdos`** (`tests/acceptance/sdos.exp` + `sdos.toml`) boots SDOS end-to-end on an
  SBC-200 + DDBIOS + VersaFloppy: the SBC auto-bauds off the typed boot character, and the
  VersaFloppy chapter (`docs/boards/sd-versafloppy.md`) covers the disk side.
- **`machines/sbc200.toml`** cold-starts MSMONR21 to its `.` prompt on the on-card 8251;
  **`machines/sbc200v.toml`** drives the same card from a VDB-8024 video console over VI2.

## References

- `reference/SD Systems SBC-100 & SBC-200.md`, `reference/SD Systems Monitor.md`,
  `reference/SD Systems SDOS.md`.
- `docs/boards/sd-versafloppy.md` — the soft-sector floppy controller the SBC boots through.
- `docs/boards/sd-vdb8024.md` — the video console that drives this card's CTC over VI2.
