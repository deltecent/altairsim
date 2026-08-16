# S100Computers V2 Z80 CPU Board — onboard monitor EEPROM & Power-On-Jump

Distilled hardware reference for the part of the **S100Computers (John Monahan) V2 Z80 CPU
board** that `altairsim` models as the `v2z80` board: its **onboard paged monitor EEPROM** and
its **Power-On-Jump (POJ)** circuit. The Z80 processor itself is a separate card in this
simulator (`mits-z80cpu`); this board contributes only the boot ROM and the reset vectoring
that live on the same physical S-100 card.

## Provenance & licensing

| Source | What it gives |
|---|---|
| **V2 Z80 CPU Board** project page — s100computers.com, `My System Pages/Z80 Board/Z80 CPU Board.htm`, fetched 2026-08-16 (HTML, not a scan). | The POJ NOP-injection circuit (U15 comparator / U18 bus driver / U29 / P3 boot-address jumper); the 28C64 EEPROM at `F000`–`FFFF`; the `D2H`/`D3H` memory-manager port pair (jumper `P2 5-6`); **`OUT D3H` bit 1 = EEPROM A12** (page select) and **bit 0 = ROM inactivate**; jumper `P39 7-8` enabling software A12 control. |
| **MASTER Z80 Monitor V6.6** source — `Master0.z80` / `Master1.z80` (+ `MASTER0.HEX` / `MASTER1.HEX`, assembled, `ORG 0F000H`), supplied by Patrick 2026-08-16. | The monitor's own header comments corroborate the paging (both 4K halves appear at `F000`–`FFFF`; low half = EEPROM chip offset `0000`–`0FFF`, high half = `1000`–`1FFF`). `ACTIVATE_HIGH_PAGE:` = `LD A,06H / OUT (D3H),A`; `ACTIVATE_LOW_PAGE:` = `LD A,04H / OUT (D3H),A` (bit 1 is the page bit; bit 2 = memory-manager overlap on the companion MMU board). The `I` command (`SDBOOTCPM`→`SDCPM`, V6.52+) boots CP/M 3 from the Dual SD card. |

No explicit licence is stated (hobbyist/educational project files, DR-derived CP/M). Recorded
here and shipped; never gated on redistribution — see the project's licensing rule. The two
page images are embedded as `builtin:master0` / `builtin:master1`; their CRC32/provenance are in
`docs/roms.md`. See also [[dual-sd-card]] (the boot target this board loads) and the MITS
Turnkey board, whose Auto-Start jam is the same *class* of reset-vectoring circuit.

## 1. The onboard monitor EEPROM

A single **28C64** (8K × 8) EEPROM (or a 27C64 UV-ROM) sits at CPU addresses **`F000`–`FFFF`**,
a **4K** window. The chip holds **two 4K "pages"** and only one is visible at a time:

| Page | EEPROM chip offset | CPU window | Image | Contents (V6.6) |
|---|---|---|---|---|
| **Low** (normal) | `0000`–`0FFF` | `F000`–`FFFF` | `builtin:master0` | The everyday monitor menu; almost all commands. |
| **High** | `1000`–`1FFF` | `F000`–`FFFF` | `builtin:master1` | XModem file download, SD/CF-card CP/M boot (`I` command), clock/date; ~half free. |

The A12 address line of the EEPROM selects which 4K half maps into the window; on the V2 board
that line is driven by a port bit (below) rather than a jumper. Because the two halves overlap
at the same CPU addresses, the monitor keeps its page-switch stubs (`ACTIVATE_HIGH_PAGE` /
`ACTIVATE_LOW_PAGE`) at identical offsets in both halves so a switch is seamless to the running
code.

## 2. Port D3H — page select and ROM enable

Ports **`D2H`** and **`D3H`** (default; relocatable via jumper `P2 5-6`) are the board's
memory-manager / ROM-control registers. `D2H` maps a 16K window at `0000`–`3FFF` and `D3H`
maps one at `4000`–`7FFF` for the companion 20-bit memory manager — **`altairsim` does not
model the memory manager** (the Dual SD target is *non-banked* CP/M 3 in a flat 64K). Only two
bits of `D3H` concern the onboard EEPROM, and those are what `v2z80` implements:

| `OUT D3H` bit | Meaning | Values |
|---|---|---|
| **bit 1** | EEPROM **A12** — which 4K page is visible | `0` → **low** page (`master0`); `1` → **high** page (`master1`). `OUT D3H,0` = low, `OUT D3H,2` = high. |
| **bit 0** | Onboard EEPROM **inactivate** | `0` → EEPROM answers `F000`–`FFFF` (normal); `1` → EEPROM off, RAM shows through the whole window. (`QO D3,1` in the docs.) |

`D3H` is **write-only** (a latch); the board does not answer `IN D3H`. Enabling software control
of A12 requires jumper `P39 7-8` (assumed present). The monitor's `ACTIVATE_*` stubs write
`06H`/`04H` — bit 2 (`04H`) is the memory-manager overlap bit for the MMU board and is ignored
here; only bit 1 differs between them, and it is the page bit.

**Reset default:** both window/offset latches power up `0x00`, so at reset the EEPROM is
**enabled** and the **low** page is selected.

## 3. Power-On-Jump (POJ) — the NOP-injection slide

The Z80 resets with `PC = 0000`, but the monitor lives at `F000`. The V2 board vectors execution
there by **forcing NOPs onto the data bus** until the program counter climbs to the boot address:

1. At reset a comparator (**U15**) continuously compares the upper address lines **A12–A15**
   against jumper **P3**. Default = *no jumpers* → boot address **`F000`**.
2. While A12–A15 do **not** match, U15 enables a bus driver (**U18**) that forces **`00H`
   (NOP)** onto the data bus for every fetch, overriding any RAM. The Z80 executes NOPs and
   `PC` increments one byte per fetch — a "slide" upward from `0000`.
3. When the fetch address reaches `F000` (A12–A15 = `1111`), U15 asserts `ROM_SELECT*`; U29
   disables U18 and enables the EEPROM outputs. The Z80 fetches the first real opcode at
   `F000` (`C3 84 F0` = `JP F084`) and the monitor runs.

The POJ is a **one-shot armed by any reset** (POC* or RESET*), disarmed once the slide reaches
the boot address, so ordinary RAM at `0000`–`EFFF` is reachable during normal operation. This is
the same class of circuit as the MITS Turnkey Auto-Start, which instead *jams a 3-byte `JMP`*;
the V2 board genuinely NOP-slides the full distance (≈`F000` fetches).

### How `altairsim` models it (`v2z80`)

- **Armed at reset** (`pojArmed_`). While armed, for a `MemRead` **opcode fetch** below
  `F000` the board **decodes the cycle, returns `0x00` (NOP), and asserts `PHANTOM*`** so no RAM
  board contends — exactly U18 forcing the bus.
- The first fetch at or above `F000` is answered by the EEPROM (not a NOP); `snoop()` then
  **disarms** the POJ (U18 off, `ROM_SELECT*` latched). Low memory reads normally thereafter.
- The EEPROM decodes `MemRead` in `F000`–`FFFF` while enabled (bit 0 = 0), returning the
  selected page's byte and asserting `PHANTOM*` for reads so it shadows any RAM mapped there.
  Writes are **not** decoded (it is ROM) and fall through to the RAM underneath. Setting bit 0
  = 1 removes the board from `F000`–`FFFF` entirely, so a guest (e.g. CP/M using the full 64K)
  gets RAM in the window.

## 4. Booting CP/M 3 from the Dual SD card

MASTER V6.52+ adds the **`I`** command (`SDBOOTCPM` → `SDCPM`, in the **high** page): it reads
**12 sectors from LBA 1** (the CPM3 loader, `CPMLDR`) of the selected Dual SD card into RAM at
**`0100H`**, verifies the first byte is `31H`, and `JP 0100H`. No CF/IDE drive is required and
no loader is fabricated — this is the board's own shipped boot path. See [[dual-sd-card]] for the
card-side protocol.
