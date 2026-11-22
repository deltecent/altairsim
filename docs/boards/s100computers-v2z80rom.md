# S100Computers V2 Z80 CPU board — the paged monitor EEPROM

**Status:** done — for the one part of the board it models: the onboard 8K monitor EEPROM, its
two 4K pages in the `F000`–`FFFF` window, and the `D3H` latch that switches pages and turns the
EEPROM off. Board type `v2z80rom`. It carries the MASTER V6.6 monitor in the Dual SD and
IDE-AB machine files in the *User Manual*.

**Not modelled, on purpose:** the rest of the physical board. The Z80 is a separate board here,
the Power-On-Jump is replaced by `RUN F000`, and the memory manager behind `D2H`/`D3H` is left
out. See *Limitations*.

## The real hardware

The **V2 Z80 CPU board** is a modern S-100 board by **John Monahan** (S100Computers). One board
carries a Z80, a **28C64 8K monitor EEPROM** (or a 27C64 UV EPROM), a **Power-On-Jump (POJ)**
circuit that gets the Z80 from `0000` to the monitor at reset, and the `D2H`/`D3H` port pair of
a 20-bit memory manager. It has no RAM of its own.

The EEPROM answers in a **4K window at `F000`–`FFFF`**. The chip holds two 4K **pages** and
only one shows at a time. The chip's A12 line picks the page, and on the V2 board a port bit
drives A12 (jumper `P39 7-8` gives software that control):

| Page | Chip offset | Image | What the monitor keeps there (V6.6) |
|---|---|---|---|
| **Low** | `0000`–`0FFF` | `builtin:master0` | the everyday monitor menu and almost every command |
| **High** | `1000`–`1FFF` | `builtin:master1` | XModem download, the Dual SD CP/M 3 boot (`I`), the clock; about half free |

Both halves run at the same CPU addresses, so the monitor keeps its page-switch stubs
(`ACTIVATE_HIGH_PAGE`, `ACTIVATE_LOW_PAGE`) at the same offset in each. A switch lands on the
matching instruction in the other page.

## Sources

| Source | Path | Authority |
|---|---|---|
| S100Computers *V2 Z80 CPU Board* project page (HTML, fetched 2026-08-16) | `reference/v2-z80-cpu-board.md` | **Authoritative** for the 28C64 at `F000`, the `D2H`/`D3H` ports and `P2 5-6`, `OUT D3H` bit 1 = A12 and bit 0 = ROM off, `P39 7-8`, and the POJ circuit |
| MASTER Z80 Monitor V6.6 source, `Master0.z80` / `Master1.z80` and the assembled `MASTER0.HEX` / `MASTER1.HEX` | `roms/MASTER0/`, `roms/MASTER1/`; provenance and CRCs in `docs/roms.md` | **Corroborates** the paging: both halves `ORG 0F000H`; `ACTIVATE_HIGH_PAGE` writes `06H` and `ACTIVATE_LOW_PAGE` writes `04H` to `D3H` — only bit 1 differs |

The two agree. Neither states a licence; the reference records that, and `docs/roms.md`
records where the two images came from.

## Register reference

One write-only port and a 4K read-only window.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `port` (default `D3`) | **bit 1** = EEPROM A12: `0` low page, `1` high page. **bit 0** = `1` turns the EEPROM off (RAM shows through `F000`–`FFFF`); `0` turns it on. **bit 2** = the memory manager's overlap bit — ignored. Other bits ignored | not answered — the latch is write-only |

| Memory | Write | Read |
|---|---|---|
| `F000`–`FFFF`, EEPROM on | **not answered** — falls through to the RAM underneath | the selected page's byte, with **PHANTOM*** asserted so the RAM board stays off the bus |
| `F000`–`FFFF`, EEPROM off | not answered | not answered — the RAM underneath answers |

At power-on and at every reset the latch is `00`: EEPROM on, low page.

## How it is simulated

- **Bus cycles:** an `IoWrite` to `port`, and a `MemRead` in `F000`–`FFFF` while the EEPROM
  is on. Nothing else — never a `MemWrite`, never an `IoRead`.
- **RAM under ROM.** While on, `assertsPhantom()` is true for a read in the window, so a 64K
  RAM board steps aside and the EEPROM's byte wins. Writes are never decoded and reach the RAM.
  This is the Turnkey boot PROM's pattern (`docs/boards/mits-turnkey.md`), and the reason
  `DESIGN.md` §4.2 puts the PHANTOM* gate on the memory boards.
- **Turning it off** (`OUT D3H` with bit 0 set) takes the board out of the window entirely and
  calls `decodeChanged()`. That is how CP/M 3 gets the top 4K of RAM once it has booted.
- **The two pages** are one 8K array. `power()` fills it with `FF` (an unprogrammed EEPROM) and
  loads `builtin:master0` into the low 4K and `builtin:master1` into the high 4K, through the
  same Intel HEX reader a memory board's ROM uses. A missing image is reported and leaves its
  page `FF`.
- **`peek()`** reads the selected page with no side effects, so `DUMP`, `DISASM` and `TRACE`
  see what the CPU would.
- **Starting the monitor** is the operator's keystroke: the machine file says
  `startup = ["RUN F000"]` (`DESIGN.md` §10.0), the way the `amon` machine starts its `F000`
  monitor.
- **No interrupts, no bus mastering, no media, no `Clock` use.**
- **Properties:** `port` (hex, default `D3`), which moves the latch. The window is fixed at
  `F000`.

### Reset

- `Reset::PowerOn` (POC*): reload both pages from the built-in images, then as `Reset::Bus`.
- `Reset::Bus` (RESET*): latch to `00` — EEPROM on, low page. If the guest had turned the
  EEPROM off, the window comes back, so a reset always lands in the monitor's low page.

SNAPSHOT carries the two latch bits (page, on/off). The EEPROM bytes and `port` are config and
are rebuilt on power.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **bit 1 is the page, bit 0 is on/off** — `06H` and `04H` differ only in bit 1 | Bit 2 is set in both values. Taking it as the page pins one page, the monitor's page switch does nothing, and the `I` command in the high page cannot run, so CP/M 3 does not boot |
| Reads in the window assert **PHANTOM***; writes do not | Without PHANTOM* the RAM board and the EEPROM both answer the read. If writes were claimed too, a program that copies itself under the ROM loses its bytes |
| Bit 0 = 1 takes the board **off the bus** in the window, not just the chip | CP/M 3 uses the top 4K as RAM. With the EEPROM still answering reads, what CP/M wrote there reads back as monitor code |
| Reset turns the EEPROM back **on** and selects the **low** page | After a reset from CP/M, `RUN F000` would run RAM (or the high page) instead of the monitor |
| The latch is **write-only** | A guest that reads `D3H` gets whatever else is on the bus, as on the real board |
| An unprogrammed byte reads **`FF`**, as on an erased EEPROM | A zero fill would make a page image that loaded short look like a run of `NOP`s instead of an empty chip |

## Limitations and deliberate departures

- **Only the EEPROM is modelled.** That is why the id is `v2z80rom`, not `v2z80`. A machine pairs
  it with a Z80 CPU board (`z80`) and a RAM board. The real V2 board needs a RAM board too;
  only its Z80 is on the same board.
- **No Power-On-Jump.** The real board forces `NOP`s onto the data bus from `0000` until the
  address reaches `F000`, then lets the EEPROM answer. Here the machine file's `RUN F000` does
  the same job at power-on: the Z80 fetches its first real opcode at `F000` either way. The
  difference shows after a RESET* on a running machine. The real board slides to the monitor
  again; here the Z80 starts at `0000` and runs whatever RAM holds there, and `RUN F000`
  starts the monitor.
- **No memory manager.** `D2H` is not decoded, and bit 2 of `D3H` is ignored. The Dual SD boot
  target is non-banked CP/M 3 in a flat 64K, which never uses them. Banked CP/M 3 or MP/M on
  this board would need the manager.
- **The images are fixed.** The EEPROM is loaded from `builtin:master0` and `builtin:master1`
  on power. A guest write in the window goes to the RAM underneath, never to the chip, and
  there is no property to load other images.

## Verification

- `test_v2z80rom` (unit, `./build/altair_tests v2z80rom`): the low page shows in `F000`–`FFFF`
  at reset; `OUT D3H` bit 1 switches pages — proven with `F001`, which is `84` in `master0` and
  `1C` in `master1`, so the test sees the other page, not just a flag; bit 0 empties the window;
  reads shadow RAM while writes reach it, and turning the EEPROM off shows the RAM; each page's
  unprogrammed tail reads `FF`; reset returns to the low page with the EEPROM on; `port` moves
  the latch.
- `acceptance-dualsd`, `acceptance-dualidesd`: each starts the MASTER
  monitor from this board with `RUN F000`, types its boot command (`I` for the Dual SD, which
  is in the **high** page), boots CP/M 3 and reads a directory. That exercises the EEPROM
  window, PHANTOM* and turning the EEPROM off on a whole machine through the real CLI, and
  `acceptance-dualsd` exercises the page switch too.

## References

- `reference/v2-z80-cpu-board.md` — the distilled project page and monitor source.
- `docs/boards/dualsd.md`, `docs/boards/dualide.md` — the boot targets the monitor loads from.
- `docs/boards/s100computers-propio.md` — the console it is paired with.
- `docs/boards/mits-turnkey.md` — the same RAM-under-ROM pattern on a period board.
