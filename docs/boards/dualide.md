# Dual IDE (CF) — S100Computers 8255 IDE/CompactFlash controller

**Status:** done (2026-08). Boots **CP/M 3** (non-banked) to `A>` off a CompactFlash card image,
through the V2 Z80 CPU board's MASTER monitor. `altairsim dualide`; see `examples/dualide/`. Pair
with `dualsd` for the full A:/B:(CF) + C:/D:(SD) system (`examples/dualidesd/`).

## The real hardware

The **S100Computers "IDE-AB CF+ESP32" board** (John Monahan) is a *combination* card: an
**8255-based IDE/CompactFlash controller** presenting CP/M drives **A:/B:**, plus the
byte-identical **Dual SD ESP32 engine** presenting **C:/D:**, on one physical board. This board
models the **IDE/CF half**; the SD half is the [[dualsd]] board, and the two compose into the
whole card. Both halves address the *same* `.img`/`.geo` card medium — the combination CP/M 3
BIOS (`HIDE3.ASM`) builds an identical DPB, LBA and byte order for each, so a card is portable
between them.

- **No boot PROM on the board.** CP/M is loaded by the *CPU board's* ROM monitor. The pairing we
  ship is the **V2 Z80 CPU board** (`v2z80rom`), whose MASTER V6.6 EEPROM monitor has a **`P`
  command** that boots CP/M 3 from the CF card (the sibling `I` command boots from SD).
- The CPU drives a **8255 PPI** (five ports at 30–34) that fans out to a minimal **ATA/IDE
  register file**. Whole 512-byte LBA sectors move as 256 16-bit words. Programmed I/O only — no
  DMA, no interrupts, no memory window.

## Sources

| Source | Path | Authority |
|---|---|---|
| CP/M 3 BIOS `HIDE3.ASM` (+ `HBOOT3.ASM`, `HDRVTBL3.ASM`) | `reference/dual-ide-card.md` | **Definitive** for the 8255 port map, the IDE control-line encoding, the ATA register file, the command set, the status-polling sequences, the 16-bit transfer, the LBA math, and the DPB. It is the shipping combination BIOS source. |
| MASTER Z80 monitor V6.6 (`roms/MASTER0`,`roms/MASTER1`) | `reference/dual-ide-card.md` §5 | The boot path: the `P` command (`HBOOTCPM`) loads 12 sectors from LBA 1 into 100H and jumps. |
| CPM Card Images Store (`.imgc`) | `reference/dual-ide-card.md` §7 | The ready-made boot images (Image-1 CF-only, Image-14 CF+SD). |

## Register reference

Five consecutive 8255 ports, strap-relocatable to any base (default **30H**):

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `30` (port A) | IDE data **low** byte | register read-back (data low) |
| `31` (port B) | IDE data **high** byte | register read-back (data high) |
| `32` (port C) | the IDE control lines (A0/A1/A2/CS0/CS1/WR/RD/RST) | last value written |
| `33` (config) | 8255 mode: `92` = C out/A,B in, `80` = all out | last value written |
| `34` (drive)  | bit 0 = drive select (`0`→A:, `1`→B:) | current select |

Control-line bits on port C: `A0=01 A1=02 A2=04 CS0=08 CS1=10 WR=20 RD=40 RST=80`. With CS0
asserted the register address is `portC & 0x0F`: `REGdata=08 REGerr=09 REGseccnt=0A REGsector=0B
REGcylLSB=0C REGcylMSB=0D REGshd=0E REGcmd/REGstatus=0F`.

Access is by strobe: **write** = put the byte on port A, address port C, pulse WR high then low;
**read** = address port C, pulse RD high, read port A. A 512-byte sector transfers through REGdata
as 256 16-bit words (low = port A, high = port B). Commands (to REGcommand): `READ=20 WRITE=30
RECAL=10 INIT=91 SPINDOWN=E0 SPINUP=E1 IDENTIFY=EC`. `LBA = REGsector | REGcylLSB<<8 |
REGcylMSB<<16 | (REGshd & 0x0F)<<24`; byte offset `LBA·512`.

See `reference/dual-ide-card.md` for the complete tables and BIOS fragments.

## How it is simulated

`src/boards/dualide.{h,cpp}`, sharing the mount/snapshot scaffolding with `dualsd`:

- Decodes `IoRead`/`IoWrite` for `port_`..`port_+4` (default `30`..`34`); `decodes()` is exact.
- **No `tick()` work and nothing on the `EventQueue`** — the engine is synchronous, edge-triggered
  on writes to port C. The WR rising edge writes a register (or shifts one data word into the
  buffer); the RD rising edge presents one. On the 256th write word the sector commits to the
  medium; a READ command loads the whole sector up front.
- Two `[[board.drive]]` sub-units = the two CF sockets. Each holds a `MediaFile` opened via
  `openMedia(resolvePath(mount), ro, err)` — the **same** `cardimg` medium `dualsd` uses (a raw
  `.img` plus a sibling `.geo`). The board addresses it **directly by byte offset**
  (`readAt/writeAt(lba·512,…)`), never wrapping it in `DiskImage`.
- **Status:** BSY always 0, RDY set with a card present, DRQ during a transfer, ERR in bit 0 —
  exactly what `IDEwaitnotbusy` `(st&0xC0)==0x40` and `IDEwaitdrq` `(st&0x88)==0x08` poll.
- **Does not master the bus; no interrupts** — programmed I/O only.
- `sync()` after each WRITE (per-sector durability).
- `properties()`: `port` (radix 16, default `30`). Drives are a `[[board.drive]]` table (`unit` /
  `mount` / `readonly`, alias `writeprotect`).

### Reset

- `Reset::PowerOn` (POC*, cold) and `Reset::Bus` (RESET*, warm): reset the engine (transfer phase,
  register file, drive select, sector buffer). Mounted cards and their contents survive.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **An empty socket floats `0xFF` on every register**, status included | A `0x00` "idle" status would look ready-with-no-drive; the BIOS's `IDEwaitnotbusy` must time out on `0xFF` so `COMMON$INIT` reports the drive "not Present" |
| **The 16-bit sector transfer is low byte (port A) then high byte (port B)** | Swapping them byte-swaps every word on the card — a scrambled, unbootable filesystem, and cards no longer interchange with the SD half |
| **LBA spans REGsector + REGcylLSB + REGcylMSB (+ REGshd nibble)** | Dropping the cyl-MSB byte caps the card at 64K sectors and misaddresses anything past it |
| **BSY always clear, RDY always set, DRQ per transfer phase** | If DRQ never sets, `IDEwaitdrq` times out and every read/write "fails"; if BSY sticks, the boot hangs in `IDEwaitnotbusy` |
| A never-written **in-range** sector returns the medium's erased fill (`0xFF`), ERR clear; only a read *past the card* is an error | Treating an unbacked sector as an error fails the boot; the shipped `.img` is truncated to the live filesystem and the BIOS reads past its end routinely |

## Limitations and deliberate departures

- **IDENTIFY (`EC`) returns 256 words of zero.** No period boot path reads the identity block; it
  is modeled only so the register exists.
- **A bootable card cannot be blank-created.** `MOUNT … CREATE format=dualide` authors a blank
  *data* card (an empty `.img` + its `.geo`); the CP/M 3 system tracks come only from a real image.
- **The banked CP/M 3 build is not exercised.** We ship and test the non-banked images.
- **Drive B: is a second CF image if supplied, else absent.** The MASTER `P` command boots drive 0
  (A:) only; drive 1 (B:) reads `0xFF` when its socket is empty.

## Verification

- **Unit** (`tests/test_dualide.cpp`, `Clock c;` first): drives the 8255/ATA strobe engine over a
  `cardimg`/`MemoryMedia` fixture — the WRITE→READ sector round trip at `LBA·512`, the status byte
  vs `IDEwaitnotbusy`/`IDEwaitdrq`, RST aborting a transfer, the three-register LBA, the two CF
  sockets and the drive-select port, an empty socket floating `0xFF`, write-protect, the port
  strap, snapshot round-trip, and — the point — a card written by `dualide` read byte-for-byte by
  `dualsd`.
- **Acceptance** (`tests/acceptance/dualide.exp`, label `acceptance`): boots real CP/M 3 on a whole
  machine through the CLI — the MASTER monitor's `P` command loads the loader off the CF card, CP/M
  3 signs on, and `DIR` reads the directory back. `examples/dualidesd` also proves `DIR C:`/`DIR D:`
  on the SD drives.

## References

- `reference/dual-ide-card.md` — the distilled BIOS reference (the authority for every fact above).
- `docs/boards/dualsd.md` — the SD half (`dualsd`) that shares the card medium and composes into
  the full board.
- `reference/v2-z80-cpu-board.md` — the V2 Z80 CPU board and its MASTER monitor (the `v2z80rom` board
  that boots this one).
- `src/host/cardimg.{h,cpp}` — the card medium (`.img` + `.geo` sidecar).
