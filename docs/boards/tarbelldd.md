# Tarbell double-density floppy disk controller (#2022)

**Status:** built (`tarbelldd`). Boots CP/M 2.2 automatically off the same 32-byte boot PROM as
the single-density #1011, but from a **mixed-density** disk. `tests/test_tarbell.cpp` and
`acceptance-tarbelldd` pin it. Run it with `altairsim tarbelldd` (mount a disk) or
`examples/tarbell/tarbelldd.toml`.

## It is the #1011's twin

Read [`tarbell-sd.md`](tarbell-sd.md) first — this card is `TarbellDdBoard : TarbellBoard`, and it
inherits the whole single-density card: the 8-port block at **F8**, the FD177x register file at
F8-FB, the wait-synced data transfer, the 32-byte boot PROM that shadows 0000 over PHANTOM\*, the
automatic boot, and the drive table. The double-density card (Tarbell Electronics #2022, 1979-80)
changes exactly four things, and nothing else.

| | #1011 (`tarbell`) | #2022 (`tarbelldd`) |
|---|---|---|
| **FDC** | WD **FD1771** (single density, FM) | WD **FD1791** (single *and* double density, FM/MFM) |
| **`OUT FC`** | function decoder; drive select is the **complement** of D5:D4 (the CBIOS does `CMA`) | plain **bitmap latch**: D3 density (0=SD, 1=DD), D5:D4 binary drive, D6 side |
| **Port FD** | unused | **DMA-busy** (read, bit 7 = complete) / **extended-address latch** A16-A23 (write) |
| **Media** | uniform single density | **mixed density** — SD track 0, DD tracks 1-76 (and it reads plain SD disks, and formats a blank with `DFORMAT`) |

## The build-the-right-chip trap

`buildChip()` is virtual, and the base `TarbellBoard` constructor calls it — which means during base
construction it reaches `TarbellBoard::buildChip` (an FD1771), **not** the FD1791 override, because
C++ dispatches virtuals to the base during base construction. A `TarbellDdBoard` built that way gets
the single-density chip and **hangs mid-load on the first double-density track**. The fix is a
`TarbellDdBoard` constructor that calls `buildChip()` again once the object is fully itself. Do not
remove it, and do not move chip construction back into the base constructor alone.

## Mixed-density media

The tracked master (`examples/tarbell/TARBELLDD-CPM22-SSDD-48K.DSK`, 499,456 bytes) is **single
density on track 0** — 26 sectors of 128 bytes, the boot format the shared PROM and cold loader read
— and **double density on tracks 1-76** — 51 sectors of 128 bytes. Two `initFormat` ranges express
it (`describeGeometry()`):

```
init(77, 1, /*interleaved=*/false);
initFormat(0, 0,  0, 0, SD, 26, 128, 1);   // track 0:      26 × 128, single density
initFormat(1, 76, 0, 0, DD, 51, 128, 1);   // tracks 1-76:  51 × 128, double density
```

On a **read**, the density is decided **by the medium**, not the `OUT FC` density strap: a read
takes its byte count entirely from the track's declared format, so the boot works because track 0 is
*declared* SD and the card powers up density-clear. The strap (`dataRate_` 250k/500k) drives the
chip's `dataRateBits` for fidelity and for anything that reads it back.

### Reading SD media and formatting a blank

`describeGeometry()` is a **superset**, not a single-size gate — the #2022 reads more than its own
mixed disk:

| Image size | Mounts as |
|---|---|
| 499,456 | the mixed disk — SD track 0, DD tracks 1-76 |
| 256,256 | a plain **single-density** disk (an existing SSSD image, PD disk 2), all 77 tracks SD |
| smaller / blank | **unformatted** — formattable track-by-track (`MOUNT … CREATE`) |
| larger | error (a real DD disk is never bigger) |

On a **format**, the density is the `OUT FC` strap's, not the medium's: `Write Track` records each
track at the density the chip's `dataRateBits` implies (SD at 250k, DD at 500k). So `DFORMAT.COM` —
the public-domain Tarbell mixed-density formatter on the tracked master — turns a blank into a valid
499,456-byte mixed image: it clears the density bit for track 0 and sets it for the tracks-1-76 pass,
and the SD/DD split falls straight out. Answer **N** to *Use DMA?* (the DMA path bypasses the data
port). `tests/test_tarbell.cpp` proves the whole cycle at the board level.

## Port FD, and why it never hangs

The #2022 offered an optional DMA path. We model it as programmed I/O — the CBIOS on the tracked disk
transfers through the wait-synced data port like the #1011 — so port FD is two harmless registers:
`IN FD` returns **0x00** (bit 7 = 0, "DMA complete") so any code that polls the DMA-busy flag falls
straight through, and `OUT FD` stores the A16-A23 extended-address latch, which has no effect in a
64K machine.

## Limitations

- **DMA is not modelled**, only its status (always "complete"). No tracked software drives it.
- Everything the #1011 doc lists under *Limitations* applies here too — the wait port never stalls
  the CPU, `IN base+4` bits 6..0 float, and so on.
