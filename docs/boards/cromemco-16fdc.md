# Cromemco 16FDC

**Status:** done (Phase 1 — a polled boot: CDOS 2.58 cold-boots off the RDOS 2.52 PROM and reads
the disk; single and double density, up to four drives; mixed-density 8″ and 5¼″ CDOS geometries
and formattable blanks)

## The real hardware

The **16FDC** (1979–1981) is the double-density member of Cromemco's S-100 floppy-disk controller
family. Each board in the family is one card carrying three things at once:

- a **Western Digital FD177x** floppy controller — the 16FDC's is an **FD1793** (single *and*
  double density, FM/MFM);
- an onboard **TMS 5501** — a UART + five interval timers + interrupt controller, wired here as a
  single console channel;
- an onboard **RDOS** boot PROM — the 16FDC's is **4K** of RDOS 2.52 at `C000`.

The family: the **4FDC** (1977) is FD1771-only, single density; the **16FDC** and **64FDC** (1983)
both carry the FD1793. The 64FDC differs only in details Phase 1 does not model, plus an **8K** ROM
— see [`cromemco-64fdc.md`](cromemco-64fdc.md).

## Sources

| Source | Path | Authority |
|---|---|---|
| Cromemco 4FDC / 16FDC / 64FDC manuals | `reference/Cromemco 4FDC 16FDC 64FDC Floppy Controllers.md` | The hard-decoded port map, the port-34 and port-04 bit layouts, the MAXI×DDEN data-rate table, the boot/bank-select behavior. |
| RDOS 2.52 boot PROM | `builtin:rdos252` | **What the software actually does** — the seek-complete poll, the fixed-baud strap read, the boot-drive select, the per-byte `IN 34 / INI` transfer idiom. |
| Cromemco CDOS | `reference/Cromemco CDOS.md` | The mixed-density disk layout (SD track 0 side 0, DD elsewhere) and CDOS.COM's home-on-selection behavior. |
| WD FD1793 / FD179x data sheets | `reference/Western Digital FD1771 - Datasheet.md`, `src/chips/wd17xx.h` | The FD1793 register file and command set (the `Wd1791` part). |
| TMS 5501 data sheet | `reference/Cromemco TU-ART.md`, `src/chips/tms5501.h` | The UART + timers + interrupt-address register (here one channel, not the TU-ART's twin). |

## Register reference

Three I/O windows, all **hard-decoded** (unlike the VersaFloppy or TU-ART, every FDC board fixes
these addresses), plus the RDOS PROM's memory reads.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| 00 | TMS 5501 baud | TMS 5501 status |
| 01 | TMS 5501 transmit | TMS 5501 receive |
| 02 | TMS 5501 command | `FF` (not assigned) |
| 03 | TMS 5501 interrupt mask | TMS 5501 interrupt-address (RST of the top source) |
| 04 | disk aux — side-select + PerSci control | disk aux — seek status + sense switches |
| 05–09 | TMS 5501 interval timers 1–5 | — |
| 30 | FD1793 command | FD1793 status |
| 31 | FD1793 track | FD1793 track |
| 32 | FD1793 sector | FD1793 sector |
| 33 | FD1793 data (**never** wait-synced) | FD1793 data (**never** wait-synced) |
| 34 | disk control (below) | disk flags (below) |
| 40 | bank the RDOS ROM out until RESET | — |

**Port 34 OUT** — `D7` AUTO WAIT, `D6` DOUBLE DENSITY, `D5` MOTOR ON, `D4` MAXI (8″/5¼″),
`D3–D0` one-hot drive select DS4–DS1.
**Port 34 IN** — `D7` DRQ, `D6` ¬BOOT (low when strapped to boot), `D5` SELECT REQUEST, `D4`
¬INHIBIT INIT (high), `D3` MOTOR ON (echoes the OUT latch), `D2`/`D1` motor/auto-wait timeouts
(never fire on an emulated drive), `D0` EOJ.

**Port 04 OUT** (active-low) — `D1` ¬SIDE SELECT (0 = side 1), `D3` ¬RESTORE (homes the selected
drive to track 0), plus PerSci mechanical bits (¬EJECT, ¬FAST SEEK) that have no emulated effect.
**Port 04 IN** reads `0x07`: `D6` SEEK IN PROGRESS = 0 (instant-seek), `D3` = 0 (RDOS's fixed
console baud — skip the terminal auto-baud dance), `D2–D0` sense switches → boot drive 0.

## How it is simulated

`CromemcoFdcBoard` (`src/boards/cromemco-fdc.{h,cpp}`) is the shared base; `Fdc16Board`
(`src/boards/cromemco-16fdc.h`) is a thin leaf that answers only the board name and which RDOS ROM
it carries. It combines two idioms already in the tree:

- **The Tarbell half** — a `Wd1791` (FD1793) + a drive-select latch + a boot PROM the card owns,
  reusing `chips/wd17xx.h` and `boards/floppy-drive.h` unchanged, with up to four
  `DiskImageDrive` adapters.
- **The SBC half** — one UART (`Tms5501`) embedded directly as a member, the card owning
  `refresh()`/`nextEdge()`/`wake_` and the endpoint resolver.

- **Decode:** IoRead/IoWrite of 00–09, 30–34, and 40 (16/64 only); the RDOS PROM's MemReads in its
  C000 window while armed.
- **Media:** soft-sector, so `sectorSize`/`startSector` come from the format. The board (not
  `DiskImage`) probes an image's byte count into a geometry, expressed as a per-track/per-side
  `FmtRange` list (see below).
- **The RDOS PROM shadows RAM** over its C000 window on reads (PHANTOM*, read-only), so a 64K RAM
  card underneath is inhibited there and the two do not contend. **Writes fall through** to the
  RAM, so CDOS relocates itself into the RAM under the ROM and the image survives the `OUT 40H`
  bank-out.
- **Interrupts:** Phase 1 raises none — `assertsInt()` is false. The 5501's timers and its polled
  interrupt-address register *are* modeled (RDOS 3.12's disk-read timeout guard arms Timer 1 and
  polls `IN 03`), but interrupt **delivery** to the backplane is deferred.
- **DMA:** none — an S-100 slave.
- **Properties:** `bootstrap` (the BOOT/MON strap), `drives` (1–4). One serial unit `tty` plus
  `drive0..3`; `[[board.drive]]` for `unit`/`mount`/`readonly`.

### Media geometries

The board recognizes, by exact byte count: the **8″ mixed-density CDOS** image (SD 26×128 track 0
side 0, DD 16×512 side 1 and everywhere else, 77 tracks double-sided, cylinder-major/head-minor),
a **plain 8″ SD** disk (77×26×128), a **5¼″ DSDD CDOS** image (40 cyl, SD boot track + DD 10×512,
17- or 18-sector boot both handled), and a **plain 5¼″ SSSD** disk (40×18×128). Anything smaller
mounts as a **formattable blank** — every access RNFs until the guest's DFORMAT streams a track
(Write Track → `setTrackFormat`, density from the port-34 DD bit), which is what makes
`MOUNT … CREATE` work.

The **data rate is MAXI × DDEN**, not DDEN alone: only 8″ double density is 500 kbit/s; 8″ SD,
5¼″ SD and 5¼″ DD are all 250 kbit/s.

### Reset

- `Reset::PowerOn`: both chips powered to a known-good idle; the RDOS ROM re-read from the host;
  the ROM re-armed (mapped).
- `Reset::Bus`: RESET* reaches the FD1793's MR (an auto-Restore that homes the head) and the
  5501's RESET* (clears the receiver, sets TBE), so the head homes and the console comes up with no
  software help — which is what RDOS's cold start relies on. The ROM is re-armed.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **The RDOS ROM shadows RAM read-only; writes fall through** | If writes are shadowed too, CDOS's self-relocation into the RAM under the ROM is lost the moment `OUT 40H` banks the ROM out. If reads aren't shadowed, a 64K RAM card contends with the ROM at C000. |
| **Data rate is MAXI × DDEN** — only 8″ DD is 500 kbit/s | A naïve "D6 → 250/500" mis-clocks every 5¼″ double-density disk (the likely real-world break behind double-density read failures). |
| **Cylinder-major, head-minor image order** (`interleaved`) | Mixed-density needs cyl 0's DD side 1 adjacent to its SD side 0; with the flag wrong the reader looks for side 1 after all of side 0, lands on the wrong cylinder, and the cold loader reads garbage where CDOS.COM should be. |
| **AUTO WAIT covers the port-33 read, not just the port-34 poll** | RDOS reads a byte with `IN 34 / INI` per byte; toggling the wait-sync off between them makes the `INI` see it clear, mis-fire Lost Data, and return the wrong byte — an FD1793 Err-B 06. |
| **Port 04 D3 ¬RESTORE homes the head on disk selection** | CDOS.COM homes on selection through this line and issues no WD Restore of its own; without it, the first directory read finds the head where the cold loader left it (track 2) and faults Record Not Found. |
| **Mixed-density track 0 is SD 128-byte** | Cromemco's INIT always lays cyl 0 side 0 single-density so a controller can read the boot sector with no density known yet; a uniform-density probe misreads the boot sector. |

## Limitations and deliberate departures

- **Phase 1 is a polled boot.** No interrupt is delivered to the backplane (`assertsInt()` is
  false), so the disk-side interrupt routing (RST 7 / DRQ / RTC through the 5501) and the 5501's
  interrupt *delivery* are a later effort — said out loud, not overlooked. A polled RDOS/CDOS
  console boot needs no CPU interrupt.
- **PerSci mechanical bits are no-ops** — an emulated drive has no eject or fast-seek mechanism to
  drive; only the side-select and ¬RESTORE lines of port 04 move emulated state.
- **The 4FDC leaf is not built.** The base is written for it (single density, a 1K no-disable ROM,
  a different port-34/04 layout — the `buildFdc`/`hasBankSelect`/`describeGeometry` hooks are
  virtual), but only the 16FDC and 64FDC leaves ship.
- **Write Track parses the streamed track**, reusing the VersaFloppy/floppy stack — it reconstructs
  each track's geometry and stores sector payloads in the raw `.DSK`; it is not a flux-level model
  (`docs/boards/sd-versafloppy.md` has the detail).

## Verification

- **`acceptance-cdos`** (`tests/acceptance/cdos.exp`) boots the shipped `examples/cdos/cdos.toml`:
  CDOS 2.58 cold-boots off the 16FDC's RDOS 2.52 PROM with no carriage return (the fixed-baud
  strap), then `DIR` reads the whole directory off the mixed-density 8″ DSDD image — proving the
  read path, the interleave order, and the density split.
- The board's disk read/write, the wait-synced transfer, the drive-select latch and the geometry
  probes are exercised through the real ports.

## References

- `reference/Cromemco 4FDC 16FDC 64FDC Floppy Controllers.md`, `reference/Cromemco CDOS.md`.
- [`cromemco-64fdc.md`](cromemco-64fdc.md) — the 8K-ROM successor on the same base.
- `docs/boards/sd-versafloppy.md`, `docs/boards/tarbelldd.md` — the other WD-chip soft-sector
  controllers that share `Wd17xx` and `DiskImageDrive`.
