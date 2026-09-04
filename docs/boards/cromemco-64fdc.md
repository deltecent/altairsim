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
| Front-panel switches | RDOS-defeat functions | baud / boot-drive / self-test (not modeled) |
| RTC / Mode-2 jumpers | present | dropped (not modeled) |

The `Fdc64Board` leaf (`src/boards/cromemco-64fdc.h`) overrides exactly three things: the board
name, `romBytes()` (8192), and `romName()` (`rdos312`), plus `auxRestoreHomesHead()` → false for
the dropped ¬RESTORE line. Everything else is the shared `CromemcoFdcBoard` base.

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
  Write Track parsing) applies unchanged.

## Verification

The 64FDC rides the shared base that `acceptance-cdos` exercises through the 16FDC; its own
divergence (the 8K ROM, the dropped ¬RESTORE) is covered by the board's unit tests.

## References

- [`cromemco-16fdc.md`](cromemco-16fdc.md) — the shared register reference, geometries and quirks.
- `reference/Cromemco 4FDC 16FDC 64FDC Floppy Controllers.md`, `reference/Cromemco CDOS.md`.
