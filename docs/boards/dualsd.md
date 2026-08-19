# Dual SD — S100Computers microSD card controller

**Status:** done (2026-08-16). Boots **CP/M 3** (non-banked) to `A>` off a microSD card image,
through the V2 Z80 CPU board's MASTER monitor. `altairsim dualsd`; see `examples/dualsd/`.

## The real hardware

The **S100Computers "Dual SD Card" board** (John Monahan, with ESP32 firmware by Wayne Warthen
& Michael Petry, 2025) — a *modern* reproduction-era S-100 board that presents **two microSD
sockets** to the bus as raw 512-byte-sector block devices, so an 8080/Z80 machine can run CP/M
off flash instead of 8″ floppies. An onboard **ESP32-S3** does all the SD work; the S-100 CPU
never sees the card directly. It talks to the ESP32 through **two I/O ports and a byte-at-a-time
handshake** — architecturally the same shape as the 88-HDSK Datakeeper and the iCOM FD3712: a
programmed-I/O command engine with a full-sector buffer, not a WD177x register file.

- **No boot PROM on the board.** CP/M is loaded by the *CPU board's* ROM monitor. The pairing
  we ship is the **V2 Z80 CPU board** (`v2z80rom`), whose MASTER V6.6 EEPROM monitor added an `I`
  command (V6.52, 12/2025) that boots CP/M 3 straight off this board — no CF/IDE board needed.
- Runs **CP/M 3**, banked or non-banked. Each SD card is one CP/M drive, and the BIOS assigns
  the drive letter from the **socket** (socket 1 → A:, socket 2 → B:), not from anything on the
  card.

## Sources

| Source | Path | Authority |
|---|---|---|
| ESP32 firmware `S100_ESP32_Firmware_v1.5.ino` | `reference/dual-sd-card.md` §2–§3 | **Definitive** for the port protocol, the 8 commands, the DI7/write-busy handshake, `SET_TRK_SEC` byte order, and FORMAT. It is the code the board runs. |
| `SD_CARD.Z80` / `SD_IO.Z80` test driver | `reference/dual-sd-card.md` | Second witness for the protocol (`SEND_CMD`/`SEND_DATA`/`GET_DATA`). |
| MASTER Z80 monitor V6.6 (`Master0.z80`/`Master1.z80`) | `reference/dual-sd-card.md` §5 | The boot path: the `I` command (`SDCPM`) loads 12 sectors from LBA 1 into 100H and jumps. |
| CP/M 3 HDISK BIOS page | `reference/dual-sd-card.md` §4 | The hard-disk DPB (512 B/sector, 61 sec/track, 2K blocks, 1024 dir entries). |
| The shipping SD `CPM3.SYS` BIOS3 binary | `reference/dual-sd-card.md` §5.1 | The cold-boot **card-detect presence check** — no source published, disassembled in-sim. |

**Where the firmware and an older note disagreed, the firmware won.** The board page's early
"SD boot not wired into MASTER" remark is obsolete (MASTER V6.52+ boots SD directly); and the
erased-sector fill is `0xFF`, not `0xE5` — `0xE5` is written *only* by the FORMAT command.

## Register reference

Two consecutive ports, DIP-relocatable to any 8-/16-bit base (default **80H**):

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `80` (STATUS) | flush any pending read byte (driver housekeeping) | **status byte**: bit 7 = data ready (DI7), bit 2 = socket-2 card present, bit 1 = socket-1 card present, bit 0 = write-buffer busy; **0xFF = no board** |
| `81` (DATA) | the next byte *into* the ESP32 — the `33H` lead, the command code, or an argument/write byte | the next byte the ESP32 is returning |

**One input path.** The `33H` lead byte, the command code, *and* every argument/data byte all
go OUT the **DATA port** (81H). There is no separate command port; STATUS (80H) is read-only.

Commands (each preceded by a `33H` lead on the DATA port; every in-range command answers with a
trailing STATUS byte):

| Cmd | Code | Host sends after | Board returns |
|---|---|---|---|
| INIT drive 1 / 2 | `80`/`81` | — | STATUS |
| SELECT drive 1 / 2 | `82`/`83` | — | STATUS |
| SET_TRK_SEC | `84` | Track byte, then Sector byte | STATUS |
| READ_SECTOR | `85` | — | 512 data bytes, then STATUS |
| WRITE_SECTOR | `86` | 512 data bytes | STATUS |
| FORMAT_SECTOR | `87` | sector count (16-bit, LSB, MSB) | STATUS |
| RESET_ESP32 | `88` | — | (reboots; nothing) |
| report/utility set | `90`–`97` | varies | FWVER / SETLBA / TYPE / CAP / CID / CSD / DISP / ECHO |

STATUS values: `STAT_OK = 0x00`, `STAT_ERR = 0x1A`. Addressing: **LBA = Track·256 + Sector**
(byte offset `LBA·512`); `SETLBA` (91H) sets a full 32-bit LBA, most-significant byte first.

See `reference/dual-sd-card.md` for the complete command tables and firmware fragments.

## How it is simulated

`src/boards/dualsd.{h,cpp}`, modeled on the iCOM/88-HDSK command engines:

- Decodes `IoRead`/`IoWrite` for `port_` and `port_+1` (default `80`/`81`); `decodes()` is exact.
- **No `tick()` work and nothing on the `EventQueue`** — the engine is synchronous. Each written
  byte is consumed at once, so the write-busy bit (status bit 0) reads back 0; DI7 (bit 7) tracks
  the reply FIFO, dropping for one poll after each byte the way the firmware's byte-at-a-time
  handshake does.
- Two `[[board.drive]]` sub-units = the two SD sockets. Each holds a `MediaFile` opened via
  `openMedia(resolvePath(mount), ro, err)` — a **directory card** (`src/host/cardimg.{h,cpp}`):
  a raw `.img` plus a sibling `.geo` sidecar that declares the full card's `sector_size` and
  `sectors`. The board addresses the medium **directly by byte offset** (`readAt/writeAt(lba·512,…)`),
  never wrapping it in `DiskImage` — the card owns its own geometry.
- **Does not master the bus; no interrupts** — programmed I/O only, as the firmware is.
- `sync()` after each WRITE (per-sector durability, like the other controllers).
- `properties()`: `port` (radix 16, default `80`), `drives` (default 2). Drives are a
  `[[board.drive]]` table (`unit` / `mount` / `readonly`, alias `writeprotect`).

### Reset

- `Reset::PowerOn` (POC*, cold): reset the command engine (drive select, current sector, the
  reply FIFO and its pointers). Mounted cards stay mounted.
- `Reset::Bus` (RESET*, warm): same engine reset; cards and their contents survive.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **Status bits 1/2 are the two sockets' card-detect lines**, and the CP/M 3 BIOS gates its cold boot on them (bit 1 = socket 1, bit 2 = socket 2) | A naïve STATUS=0x00 board loads CPMLDR but the BIOS reads **both cards absent** → `NO CCP.COM FILE`. Both sockets must have a card mounted or CP/M 3 will not come up — which is why the machine needs a card in **both**, even to boot the one in socket 1 |
| **STATUS must never read 0xFF while the board is present** | The BIOS's `CP FF` guard reads 0xFF as "no Dual SD board detected" and aborts. An installed-but-empty board reads 0x00, not 0xFF |
| `SET_TRK_SEC` reads **track first, then sector**; LBA = Track·256 + Sector | Wrong sector addressed — CP/M reads/writes the wrong block and the filesystem is scrambled |
| A never-written but **in-range** sector returns the medium's erased fill (`0xFF`), STATUS OK; only a read *past the media* is an error (512 × `0x00` + STATUS=ERR) | Treating an unbacked sector as an error fails the boot; the shipped `.img` is truncated to the live filesystem and the BIOS reads past its end routinely |
| `FORMAT_SECTOR` fills each sector with **`0xE5`** and **skips sector 0** (never overwrites the boot sector) | Filling with `0xFF` (the erased byte) or clobbering sector 0 makes a "formatted" card CP/M can't read, or destroys the boot loader |
| Every in-range command answers with a trailing STATUS byte; `RESET` (88H) returns nothing | The driver's `GET_DATA` reads one byte too few or too many and desyncs the protocol |

## Limitations and deliberate departures

- **The report/utility commands (`90`–`97`) are modeled only as far as the boot needs.** FWVER,
  TYPE, CAP, CID, CSD, DISP and ECHO return plausible fixed answers; no period boot path reads a
  real card's CID/CSD, so nothing exercises them for real.
- **A bootable card cannot be blank-created.** `MOUNT … CREATE` authors a blank *data* card (an
  empty `.img` + its `.geo`); the CP/M 3 system tracks come only from a real image written by
  `WR_BOOT.COM` on hardware. This is the same limitation the hard-sector and iCOM cards carry.
- **The banked CP/M 3 build is not exercised.** We ship and test the non-banked SD image; the
  board itself is bank-agnostic (it moves sectors), but no banked machine is wired up.
- **Drive lettering is the image's, not the board's.** The SD `CPM3.SYS` is a multi-build card
  authored under AltairZ80 and may print `(CPM C:)`/`(CPM D:)` in its presence messages while its
  banner says `A: & B:`. The presence *mechanism* is invariant; the letter mapping is a property
  of the image (`reference/dual-sd-card.md` §5.1).

## Verification

- **Unit** (`tests/test_dualsd.cpp`, `Clock c;` first): drives the 80H/81H protocol over a
  `cardimg`/`MemoryMedia` fixture — INIT/SELECT, SET_TRK_SEC, WRITE-then-READ a sector, the
  FORMAT `0xE5` fill with sector 0 skipped, the DI7/write-busy handshake, the card-detect status
  bits, RESET, and the past-media read error.
- **Acceptance** (`tests/acceptance/dualsd.exp`, label `acceptance`): boots real CP/M 3 on a
  whole machine through the CLI — the MASTER monitor's `I` command loads the loader off the card,
  CP/M 3 signs on and runs its cold-boot PROFILE.SUB, and `DIR` reads the directory back (`PIP`).
  The card is mounted write-protected and shasummed before and after — the boot only reads.
- **End to end**: the shipped `examples/dualsd/` boots read/write with the host bridge at B0, so
  `R`/`W`/`HDIR` move files between the guest and the host at the `A>` prompt.

## References

- `reference/dual-sd-card.md` — the distilled firmware/BIOS reference (the authority for every
  fact above).
- `reference/v2-z80-cpu-board.md` — the V2 Z80 CPU board and its MASTER monitor EEPROM (the
  `v2z80rom` board that boots this one).
- `src/host/cardimg.{h,cpp}` — the directory-card medium (`.img` + `.geo` sidecar).
- `docs/boards/hostbridge.md` — the host bridge the example fits at B0 for file transfer.
