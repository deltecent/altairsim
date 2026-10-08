# Tarbell double-density floppy disk controller (#2022)

**Status:** built (`tarbelldd`). Boots CP/M 2.2 automatically off the same 32-byte boot PROM as
the single-density #1011, but from a **mixed-density** disk. It carries an **on-card Intel 8257
DMA controller** and is the **first shipping board to master the S-100 bus** (DESIGN.md §4.5) —
a CBIOS assembled `DMACNTL=TRUE` moves sectors by DMA instead of an `IN`/`OUT` byte loop.
`tests/test_tarbell.cpp`, `acceptance-tarbelldd` and `acceptance-tarbelldd-dma` pin it. Run it
with `altairsim tarbelldd` (mount a disk).

## It is the #1011's twin

Read [`tarbell-sd.md`](tarbell-sd.md) first — this card is `TarbellDdBoard : TarbellBoard`, and it
inherits the whole single-density card: the 8-port block at **F8**, the FD177x register file at
F8-FB, the wait port and its `timing` property, the 32-byte boot PROM that shadows 0000 over PHANTOM\*, the
automatic boot, and the drive table. The double-density card (Tarbell Electronics #2022, 1979-80)
changes these things, and nothing else.

| | #1011 (`tarbell`) | #2022 (`tarbelldd`) |
|---|---|---|
| **FDC** | WD **FD1771** (single density, FM) | WD **FD1791** (single *and* double density, FM/MFM) |
| **`OUT FC`** | function decoder; drive select is the **complement** of D5:D4 (the CBIOS does `CMA`) | plain **bitmap latch**: D3 density (0=SD, 1=DD), D5:D4 binary drive, D6 side |
| **Port FD** | unused | **busy** (read, bit 7 = 1 until the FD1791's INTRQ) / **extended-address latch** A16-A23 (write) |
| **DMA** | none | an **on-card Intel 8257** at a second port block (base **0xE0**), mastering the bus when the CBIOS drives it |
| **Media** | uniform single density | **mixed density** — SD track 0, DD tracks 1-76 (and it reads plain SD disks, and formats a blank with `DFORMAT`) |

## The build-the-right-chip trap

`buildChip()` is virtual, and the base `TarbellBoard` constructor calls it — which means during base
construction it reaches `TarbellBoard::buildChip` (an FD1771), **not** the FD1791 override, because
C++ dispatches virtuals to the base during base construction. A `TarbellDdBoard` built that way gets
the single-density chip and **hangs mid-load on the first double-density track**. The fix is a
`TarbellDdBoard` constructor that calls `buildChip()` again once the object is fully itself. Do not
remove it, and do not move chip construction back into the base constructor alone.

## Mixed-density media

The tracked master (`tests/media/tarbell/TARBELLDD-CPM22-SSDD-48K.DSK`, 499,456 bytes) is **single
density on track 0** — 26 sectors of 128 bytes, the boot format the shared PROM and cold loader read
— and **double density on tracks 1-76** — 51 sectors of 128 bytes. Two `initFormat` ranges express
it (`describeGeometry()`):

```
init(77, 1, /*interleaved=*/false);
initFormat(0, 0,  0, 0, SD, 26, 128, 1);   // track 0:      26 × 128, single density
initFormat(1, 76, 0, 0, DD, 51, 128, 1);   // tracks 1-76:  51 × 128, double density
```

On a **read**, the `OUT FC` density bit must match the density the track is recorded at. The bit
sets the chip's density (`doubleDensity`) and its `dataRateBits` (250k/500k), and the board turns on the chip's density check
(`Wd17xx::setDensityChecked`): an FD1791 set for one density finds no ID field recorded at the
other, and the command ends in **Record Not Found**. The boot works because track 0 is single
density and the card powers up density-clear; the CBIOS sets the bit for tracks 1-76. The byte count
of a sector still comes from the track's declared format.

**The cold loader must set the bit too.** The loader in the boot sector reads the rest of track 0,
then steps in and reads the system from track 1, which is double density. Tarbell's loader source,
`2DBOOT24.ASM` on Tarbell Public Domain Disk 2, has an option for this: with `DOUBDEN TRUE` it
writes 08 to `OUT FC` and changes its sectors-per-track count to 51 before the step, and it puts
`DD` in byte 7E of the sector to tell the CBIOS that the disk is double density. Both tracked
masters carry that loader, assembled with `MSIZE 48`, `DOUBDEN TRUE` and `DMACNTL FALSE`. A loader
assembled with `DOUBDEN FALSE` reads track 1 with the bit clear and stops with Record Not Found —
on this board as on the card.

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
track at the chip's density, which the bit sets (SD at 250k, DD at 500k). So `DFORMAT.COM` —
the public-domain Tarbell mixed-density formatter on the tracked master — turns a blank into a valid
499,456-byte mixed image: it clears the density bit for track 0 and sets it for the tracks-1-76 pass,
and the SD/DD split falls straight out. Answer **N** to *Use DMA?* (the DMA path bypasses the data
port). `tests/test_tarbell.cpp` proves the whole cycle at the board level.

## The on-card 8257 — DMA is bus mastering

The #2022 carried an **Intel 8257 DMA controller** as a chip on the card, and this board models it:
`TarbellDdBoard` *has-a* `BusMaster` (DESIGN.md §4.5) driven by an `I8257 dma_` member
(`src/chips/i8257.{h,cpp}`), making it the **first shipping board to master the S-100 bus**. A CBIOS
assembled `DMACNTL=TRUE` moves a sector like this:

1. reset the 8257's first/last byte flip-flop (`OUT 0xE8`, 0),
2. load the byte **count** low-then-high through `OUT 0xE1` — the top two bits are the mode
   (01 = write-to-memory = disk→RAM, 10 = read-from-memory = RAM→disk),
3. load the memory **address** low-then-high through `OUT 0xE0`,
4. arm channel 0 (`OUT 0xE8`, 0x41), which pulls **pHOLD**,
5. issue the FD1791 `Read`/`Write` command through the ordinary F8 registers,
6. spin polling **port FD** bit 7 for completion.

The 8257's register block sits at a second decoded window, base **0xE0** by default (the `dmaport`
board property; SD2DD straps it E0, so it can be omitted). When channel 0 is armed the board pulls
pHOLD; the run loop (`Debugger::serviceDma`) grants the bus at the next instruction boundary and the
board's `Mover` moves one byte per DRQ — one `readData`/`writeData` — advancing the 8257's address
and count until terminal count, when it drops pHOLD. The stolen T-states are charged to the clock, so
the CPU genuinely loses the time (the unit test in `tests/test_tarbell.cpp` reads the exact theft back
out of `clock.now()`).

How the bytes spread out in time is the `timing` property:

- **`timing = full`** (the default): the FD1791 is wait-synced, so DRQ is up again the moment a byte
  is taken. pHOLD stays high and the whole sector drains in **one grant**, at the instruction boundary
  right after the `Read`/`Write` command.
- **`timing = real`**: the chip delivers one byte per byte time (32 µs SD, 16 µs DD). DRQ, and so
  pHOLD, rises once per byte; each grant moves that byte and gives the bus back. That is cycle
  stealing, as on the card, and the CPU runs between bytes.

`tests/media/tarbell/TARBELLDD-CPM22-SSDD-48K-DMA.DSK` is such a disk: its CBIOS is `DMACNTL=TRUE`, so
every post-boot sector read (DIR, warm boot) flows through the 8257. Its **cold boot loader stays
PIO** — RESET reads SD track 0 the proven way, and the DMA path takes over once CP/M is up.
`acceptance-tarbelldd-dma` boots it to `A>` and reads a directory through the on-card 8257.

## Port FD

- **`IN FD`**: bit 7 is **1 while the FD1791 command runs** and **0 once INTRQ is up**. The manual
  says only *"bit 7 = 0 means the DMA transfer is complete"*; it does not say what drives the bit.
  INTRQ is our reading, and the tracked CBIOSes agree with it. The DMA build polls FD after a **Seek**,
  with no DMA armed, before it reads status — so the bit must follow the command, not the 8257. The
  PIO build's Read Address recovery drains ID bytes while bit 7 is set. The 1791 has no BUSY pin, so
  INTRQ is the one end-of-command line the card could gate here. Reading status resets INTRQ, so FD
  reads busy again after a status read, until the next command ends — as the pin would. The other
  bits read 0.
- **`OUT FD`** stores the A16-A23 extended-address latch, used by the DMA `Mover` as the high address
  bits (0, hence no effect, in a 64K machine).

Under `timing = full` every command has ended before the CBIOS reaches its poll, so the poll falls
straight through. Under `real` it waits out the seek or the transfer.

## Double density under `timing = real` needs a 4 MHz CPU

An 8-inch DD disk delivers a byte every **16 µs**: 32 T-states at 2 MHz. The tracked PIO CBIOS's
read loop takes 56 T-states a byte, so at 2 MHz under `real` the chip sets **Lost Data** and the CBIOS
prints its error — as the hardware would. Run the PIO build at `clock_hz = 4000000` under `real`. The
DMA build does not care: the 8257 takes each byte on its own DRQ.

## Limitations

- **Format-time DMA is not modelled.** The 8257 moves **sectors** (the `Read`/`Write` data path the
  CBIOS uses); `DFORMAT`'s optional DMA-during-`Write Track` is a different path we do not drive, so
  answer **N** to *Use DMA?* when formatting (see above).
- **Burst under `timing = full`.** The sector drains in one grant; `real` steals one cycle per byte.
- **Port FD's busy bit is our reading of the card**, not the manual's (see *Port FD*).
- Everything the #1011 doc lists under *Limitations* applies here too — the wait port stalls only
  under `timing = real`, `IN base+4` bits 6..0 float, and so on.
