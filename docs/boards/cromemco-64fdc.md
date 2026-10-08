# Cromemco 64FDC

**Status:** done (Phase 1 — the 16FDC's successor on the shared base: an FD1793, an 8K RDOS 3.12
boot PROM at C000–DFFF, `OUT 40H` bank-select)

## The real hardware

The **64FDC** (1983) is the 16FDC's successor. It is the same S-100 controller in every respect
Phase 1 models — the same **FD1793**, the same onboard **TMS 5501** console, the same `OUT 40H`
bank-select — carrying **RDOS 3.12** in an **8K** boot PROM (`C000–DFFF`), where the 16FDC's RDOS
2.52 is 4K. RDOS 3.12 grew into the second 4K, which is the one axis the board leaf overrides.

For the full register reference, the media geometries, the reset behavior and the quirks — all
shared with the 16FDC — see [`cromemco-16fdc.md`](cromemco-16fdc.md). This page records only what
differs.

## What differs from the 16FDC

| | 16FDC | 64FDC |
|---|---|---|
| RDOS boot PROM | 4K RDOS 2.52 (`C000–CFFF`) | **8K RDOS 3.12 (`C000–DFFF`)** |
| Port 04 D3 ¬RESTORE | homes the head on disk selection | **not assigned** — the drivers home with the FD1793's own Restore command |
| Port 04 IN D6 | SEEK IN PROGRESS; reads 0 (the seek is complete) | **always 1** — the board has no seek-complete input |
| Front-panel switches | RDOS-defeat functions | baud / boot-drive / self-test (not modeled) |
| RTC / Mode-2 jumpers | present | dropped (not modeled) |

**The dropped ¬RESTORE line is confirmed, not assumed.** The 64FDC manual (023-2022, March 1983)
gives D3, D4 and D6 of port 04 as "Not assigned" (p.34). Its schematic (Board Rev B, sheet 5 of 6)
shows why: port 04 OUT is the TMS 5501's parallel output, and XO3 has no connection. Only XO1
(side select), XO2 (control out) and XO5 (drive-select override) go through the 8T98 buffer to a
signal; XO4 and XO6 enter the buffer and its outputs for them go nowhere. The drive interface
(pp.53–54) has no restore, fast-seek, eject or seek-complete line.

**Port 04 IN D6 is what tells CDOS which board it is on.** The manual gives the bit as "always
1" (p.33); the schematic holds the TMS 5501's XI6 input high through a resistor. CDOS 2.58 reads
it each time it logs in an 8″ drive: it writes port 04 with D5 low (the drive-select override),
reads port 04, and tests D6. A 0 marks the drive as a voice-coil (PerSci) drive, and CDOS homes
that drive only with port 04 D3 ¬RESTORE, with no FD1793 command. A 1 makes CDOS home the drive
with the FD1793's Restore. So the bit has to read 1 on this board: with a 0, CDOS pulls a line
the 64FDC does not have, the head does not move to track 0, and the first read after the sign-on
stops with `Read error … Status=10` (Record Not Found). That was issue #707. The seek speed set
in CDOSGEN does not enter into it; it changes only the rate bits of the FD1793 command.

The `Fdc64Board` leaf (`src/boards/cromemco-64fdc.h`) overrides the board name, `romBytes()`
(8192), `romName()` (`rdos312`), `auxRestoreHomesHead()` → false for the dropped ¬RESTORE line,
and `readAux()` to set D6. Everything else is the shared `CromemcoFdcBoard` base.

## How it is simulated

Identical to the 16FDC (`docs/boards/cromemco-16fdc.md` → *How it is simulated*), with the 8K ROM
window and no port-04 ¬RESTORE. The board type is `64fdc`; `builtin:rdos312` is the boot PROM.

## Limitations and deliberate departures

- **The 64FDC-specific details Phase 1 does not model** — its front-panel baud/boot-drive/self-test
  switches, and the RTC/Mode-2 jumpers the 16FDC has and the 64FDC drops — are noted rather than
  emulated, because a polled CDOS/RDOS boot does not touch them.
- **The port-04 subset** (the 64FDC drops the 16FDC's eject/fast-seek/¬RESTORE bits) is a
  follow-up for when port 04 stops being an inert stub; today only side-select moves emulated
  state, and the head homes on the FD1793's own Restore.
- Everything in the 16FDC's *Limitations* section (Phase-1 polled boot, no interrupt delivery,
  Write Track parsing) applies unchanged, and so does its AUTO WAIT `timing` property.

## Verification

- **`acceptance-cdos-64fdc-8in`** (`tests/acceptance/cdos.exp`) boots
  `tests/media/cdos/cdos-64fdc.toml`: CDOS 2.58 cold-boots off the 64FDC's RDOS 3.12 PROM from
  the 8″ mixed-density DSDD disk, then `DIR` reads the whole directory. It fails if port 04 IN D6
  reads 0, because CDOS then homes the drive with ¬RESTORE.
- **`acceptance-cdos-64fdc`** runs the same test on `tests/media/cdos/cdos-5in-64fdc.toml`, the
  5¼″ mixed-density DSDD disk.
- The unit tests (`tests/test_cromemco-fdc.cpp`) check the board's own divergence through the
  real ports: the 8K ROM, the dropped ¬RESTORE, and D6 of port 04 IN (1 on the 64FDC, 0 on the
  16FDC, the switch bits the same on both).
- The rest of the shared base is exercised through the 16FDC by `acceptance-cdos` and
  `acceptance-cdos-5in`.

## References

- [`cromemco-16fdc.md`](cromemco-16fdc.md) — the shared register reference, geometries and quirks.
- `reference/Cromemco 4FDC 16FDC 64FDC Floppy Controllers.md`, `reference/Cromemco CDOS.md`.
