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
software actually touches: the 8251 console, the one CTC interrupt SD CP/M uses, the onboard
PROM sockets and 1K RAM with their decode jumpers, the memory switch-out, and the auto-start
circuit.

**The defining strap** is the 8251's **RxD wired to /DSR**, so **MSMONR21** can auto-detect the
console baud by timing an incoming start bit in status bit 7. It is the etch default (`rxd2dsr`).

## Sources

| Source | Path | Authority |
|---|---|---|
| SD Systems SBC-100 & SBC-200 manuals | `reference/SD Systems SBC-100 & SBC-200.md` | Port map, 8251 orientation, CTC wiring, the RxD→/DSR strap, the memory decode jumpers (Tables 2-3 to 2-5), the memory switch-out, the auto-start jumpers (Table 2-6). |
| MSMONR21 monitor PROM | `builtin:msmonr21` | The auto-baud that reads status bit 7; the `C`/`R`/`W`/`Z` boot commands; the `JP E00F` / `IN A,(7F)` that releases the auto-start circuit. |
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
| 7F | parallel handshake; **bit 1 switches the onboard memory out** | `FF`; **the read releases the auto-start circuit** |

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

- **Decode:** IoRead/IoWrite of the 8-port block (78–7F, no wrap); the MemReads of the onboard
  PROMs and RAM while they are switched in; and the `IntAck` cycle **only** when the keyboard interrupt is pending
  (like the 88-VI), so an unclaimed acknowledge still floats `0xFF`.
- **The Z80-CTC, as much as is observable.** At flat-out speed the only thing a guest can read
  back is the one interrupt SD CP/M uses, so the model carries exactly the vector register
  (channel-0 write with D0=0) and channel 1's interrupt-enable (a control word with D7). Time
  constants and the baud divider are absorbed. It can graduate to a real `src/chips/z80ctc.*` if a
  timer ever becomes observable.
- **The onboard memory shadows the RAM board** while switched in: the board answers the read
  and asserts PHANTOM\*, and a write goes to the RAM board under it (so that board is
  `honors_phantom = read`). The same mechanism as the Turnkey boot PROM. `OUT 7F` bit 1 = 1
  drops it out; bit 1 = 0 (and any reset) restores it.
- **`snoop()`** is where the board latches: the read of port 7F that releases auto-start, and a
  write in the onboard RAM's slot. `peek()` gives DISASM and DUMP the byte a read would get.
- **Interrupts:** `assertsInt()` pulls pin 73 when the CTC has armed channel 1 and a byte is
  waiting. The card `watchesVi()` so the bus re-derives `/INT` when the VDB moves VI2.
- **`watchesVi`:** true, for the off-card VDB-8024 keyboard trigger on VI2.
- **DMA:** none here — the real card's Z80 bus-mastering is the CPU card's concern, not this
  board's.
- **Properties:** `variant` (`sbc100`/`sbc200`), `rxd2dsr` (the auto-baud strap), `port` (base,
  etch `7C`), `rom_size`, `bank`, `ram`, `start` (below). One serial unit `tty`.
  `[[board.socket]]` (`at` + `mount`) for the onboard PROMs.

### The memory decode jumpers

The X1 and X3 headers set where the four PROM sockets and the 1K RAM are decoded (manual
Tables 2-3, 2-4 and 2-5). The board has them as two properties:

| Property | Jumpers | Values | Default (the etch) |
|---|---|---|---|
| `rom_size` | X1 | `1K`, `2K`, `4K`, `8K` | `2K` |
| `bank` | X1 | 0–7 for 1K, 0–3 for 2K, 0–1 for 4K, 0 for 8K | `3` (`C000`–`FFFF`) |

A bank is eight slots of `rom_size`. Slot address = `bank × 8 × rom_size + slot × rom_size`.
Slots 0–2 are ROM 0, 1 and 2 in their low place, slot 3 is ROM 3, slots 4–6 are ROM 0, 1 and 2
in their high place, and slot 7 is the RAM. With the defaults the slots are `C000`, `C800`,
`D000`, `D800`, `E000`, `E800`, `F000`, and the RAM at `F800`.

`[[board.socket]]` (`at` + `mount`) puts a `builtin:` ROM or a host HEX/BIN file in a socket.
`at` must be the address of slot 0–6. The board refuses an address that is not a slot, the RAM's
slot, a second place for a ROM that has one (ROM 0 at `C000` and at `E000` is one socket), and
a `bank` that `rom_size` does not have. A ROM longer than `rom_size` is cut at the end of its
slot, and the log has a line for it. Only filled sockets are in the memory map: an empty slot
belongs to the RAM board.

### The onboard 1K RAM

`ram` is the X3-15 to X3-16 jumper. It is `true` by default. The RAM takes all of slot 7, so with
a slot larger than 1K the same 1K repeats (2K: `F800` and `FC00` are one byte).

- A read is answered by the board, with PHANTOM\*.
- A write goes to the onboard RAM and to the RAM board at that address, as the manual says. The
  board does not decode the write: it takes the byte in `snoop()`, so one board answers the
  cycle and there is no contention.
- `OUT 7F` bit 1 switches it out with the PROMs. A write while it is switched out does not
  reach it.
- A reset does not change its contents. `POWER` fills it with random bytes (a fixed seed).
- Its 1024 bytes are in a snapshot.

### Auto-start

`start` is the X16/X17/X18 jumpers (manual Table 2-6): a multiple of `1000`, `0000`–`F000`.
`0000` (the default) is no auto-start.

The Z80 always starts at `0000`. The board does not inject a jump and does not write PC. While
the circuit is armed, a memory read at address `A` reads the onboard memory at
`start | (A & 0FFF)`: every 4K page reads the `start` page. The PROM's first instruction is a
jump to an absolute address in the PROM, which puts PC there. Its next instruction is
`IN A,(7F)`, and that read releases the circuit.

- Both resets arm it when `start` is not `0000`.
- Only reads move. A write goes to its own address.
- Switching the onboard memory out (`OUT 7F` bit 1) also takes the override away.
- When no socket has a PROM at `start`, the override finds nothing to read, and the log has a
  line for it at power.
- The latch is in a snapshot.

### Reset

- `Reset::PowerOn` (POC*, cold): the 8251 is powered to a known-good idle; the socket ROMs are
  re-read from the host; the onboard RAM is filled; the onboard memory is switched **in**;
  auto-start is armed.
- `Reset::Bus` (RESET*, warm): the onboard memory is switched **in** (the card comes up with the
  PROM mapped); auto-start is armed; the onboard RAM keeps its contents; the receiver is re-polled and the deadline re-armed. The 8251's RESET pin is
  **not** driven from the backplane — the monitor always software-programs the chip (mode then
  command) out of reset, so nothing period-correct can tell.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **8251 orientation: data LOW, status/command HIGH** (reverse of the 6850) | Every monitor status poll reads the data port and every transmit writes to the command port — the console never works. This is why the card does not reuse `Sio2Port`. |
| **RxD strapped to /DSR** — the auto-baud line | MSMONR21 times the first character's start bit in status bit 7 to set its baud; with the strap off it never trains, and the console prints **nothing until you press Enter** (which is authentic, not a hang). |
| **Memory switch-out: `OUT 7F` bit 1 drops the PROM, any RESET restores it** | CP/M's 64K cold boot switches the PROM out so RAM shows through at the top; if the write is missed the guest can't reach the RAM under the ROM, and if RESET doesn't restore it the machine won't cold-start again. |
| **Auto-start: a reset reads the PROM at the `start` page until `IN 7F`** | The Z80 starts at `0000` in RAM. A machine needs `RUN E000` to start, and a `RESET` on a running machine runs whatever is in RAM at `0000`. |
| **A write to the onboard RAM also reaches the RAM board** | After CP/M switches the onboard memory out, the bytes the monitor wrote at `F800`–`FFFF` are not in the RAM that shows through. |
| **Keyboard interrupt from off-card, on VI2** | A VDB-8024 video console pulls VI2 while a key waits; if the card doesn't watch the VI wire, a key typed on the video console never raises `/INT` and the vectored console driver hangs. |

## Limitations and deliberate departures

- **The card is not a full single-board computer here** — the Z80 core, its bus mastering, and
  the CPU crystal live on the separate `z80` card. This board is the peripheral half SD's software
  touches; `variant` only records which SBC it is (the console, CTC and PROM behave alike).
- **The width of the auto-start override is not confirmed.** The manual's circuit analysis is
  blank. The four jumpers of Table 2-6 set A12–A15, so the board moves all four bits on every
  read while the circuit is armed. A real board can differ in a way no PROM here shows.
- **The manual's `JP X003` is an example.** The monitor PROMs here use `JP E00F`. The board
  needs only the jump and the read of port 7F.
- **`builtin:ddb200` at `F000` is not known to be an auto-start entry.** The manual says to set
  the auto-start to `F000` for the disk controller PROM; this PROM's first bytes are not the
  release sequence, and `start = F000` is not tested.
- **A write while auto-start is armed is not moved.** The manual says nothing about it.
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
  the `sbc200v.toml` machine file in the *User Manual* drives the same card from a VDB-8024
  video console over VI2. Both have their PROMs in sockets and `start = E000`, and start with a plain `RUN`.
- **`tests/test_sbc.cpp`** has the decode jumpers, the onboard RAM, and auto-start: the
  release, both resets, the snapshot, a PROM in a low bank, and a Z80 that runs the monitor
  PROM from `0000`.

## References

- `reference/SD Systems SBC-100 & SBC-200.md`, `reference/SD Systems Monitor.md`,
  `reference/SD Systems SDOS.md`.
- `docs/boards/sd-versafloppy.md` — the soft-sector floppy controller the SBC boots through.
- `docs/boards/sd-vdb8024.md` — the video console that drives this card's CTC over VI2.
