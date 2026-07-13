# Sources — the manuals and data sheets the hardware was built from

`reference/` holds the period documentation this simulator's boards were modeled from.
**It is deliberately not in git** (see `.gitignore`): 83 MB of scanned manuals, none of them
ours to redistribute, and none of them needed to *build* — only to *understand*. The code
cites them by filename; this file says what each one is, so a fresh checkout knows what it
is missing and why.

Nothing here is a build dependency. If `reference/` is empty, everything still compiles and
every test still passes.

## The rule

**Period manuals and first-hand artifacts only. Never read another emulator's source to
learn how hardware works** — that explicitly includes SIMH / AltairZ80. Second-hand facts
inherit second-hand mistakes, and the value of this project rests on the hardware model
being *right*, not on matching someone else's. If a spec is missing, ask Patrick; he sources
the manual. Do not guess, do not reconstruct from memory.

Authorized web sources: **deramp.com** and **altairclone.com** (Mike Douglas). These carry
period artifacts and test programs written for real silicon — not somebody's emulator.

## The manifest

| File | What it is | Authoritative for |
|---|---|---|
| `Western Digital FD1771 - Datasheet.pdf` | WD FD1771 FDC data sheet, 20 pp, **real text layer** | `src/chips/wd17xx.{h,cpp}`. The Tarbell's actual chip. Also reprinted as §7-2 of the Tarbell manual, so there is a free cross-check. |
| `Tarbell_Floppy_Disk_Interface_Manual.pdf` | Tarbell SD FDC manual, 78 pp, **real text layer** | `docs/boards/tarbell-sd.md`. Settles the port base (DIP switch, upper 5 bits), the wait port, and the control port's 3-to-8 decoder + latch. |
| `Altair Floppy (88-DCDD) Manual.pdf` | MITS 88-DCDD manual (52 MB — the big one) | `docs/boards/mits-dcdd.md`, as a cross-check. The `BOOT.ASM` equates in `disks/` are the better spec. |
| `Altair 88-ACR Cassette Interface.pdf` | MITS 88-ACR manual, **has a text layer** | `docs/boards/mits-88acr.md`. An 88-SIO "B" board plus an analog modem board. **There is no motor control** — the operator pressed PLAY. |
| `88-SIO Rev 0 & 1.pdf` | MITS Serial I/O Board Documentation, © 1975 | `docs/boards/mits-88sio.md`. Status word, interrupt-enable bits, address decode, UART pads. |
| `88-VI-RTC.pdf` | MITS 88-VI Vector Interrupt / 88-RTC Real Time Clock, © 1976. **No text layer** — read as page images. Fetched 2026-07-13 from deramp.com (`.../00-MITS/10-MITS S100 Boards/88-VI-RTC-Vectored Ints and RTC modules/88_virtc.pdf`). | `docs/boards/mits-88virtc.md`, `src/boards/mits-88virtc.{h,cpp}`. The control register at **376Q (0xFE, write-only)**, the priority order (**VI0 highest, VI7 lowest**), level *n* → `RST n`, and the RTC's rates and its "RI" jumper. **⚠ IT CONTRADICTS ITSELF** on the encoding of bits 0–2 (prose says the level; its own table says the ones-complement) and on the sense of bit 3. **The table won, and the tiebreaker was the artifact:** the PS2 monitor's own ISR (`tapes/MitsPS2/PS2-MON.TAP`, disassembled at 08DB) writes exactly what the table predicts. Do not re-derive this from the prose. |
| `com2502.pdf` | SMC COM2502/COM2017 UART data sheet. **No text layer** — read as page images. | `src/chips/uart1602.{h,cpp}`. The 88-SIO's UART; the 88-ACR manual calls the same part an AY-5-1013/TR1602. |
| `Altair 2SIO User's Manual.pdf` | MITS 88-2SIO manual | `docs/boards/mits-2sio.md`. |
| `6850.pdf` | Motorola MC6850 ACIA data sheet, pp. 4-527…4-535. **No text layer** — read as page images. | `src/chips/mc6850.{h,cpp}`. |
| `Western Digital WD177X-00 - Datasheet.pdf` | WD1770/72/73 data sheet | **Nothing yet.** See the trap below. Kept for a future double-density controller. |
| `Altair 8800 Theory of Operation.pdf` | MITS Altair 8800 Theory of Operation, **searchable — real text layer** | `docs/boards/mits-frontpanel.md`. The CPU board's gating logic (`SSW DSB`, and the sense switches at "device address 377o"), the D/C board's EXAMINE/DEPOSIT/SINGLE-STEP sequences, and the bus pin definitions. |
| `Altair 8800 front panel schematic.pdf` | Schematic **880-106**, "Computer Front Panel Control" | `docs/boards/mits-frontpanel.md`, and **authoritative for the port FF decode**: the `sINP` + A8–A15 8-input NAND, and the three banks of 7405 buffers. **No text layer** — read it as a page image. |
| `Altair 8800 Operators Manual.pdf` | MITS Altair 8800 Operator's Manual, **searchable** | The switch and LED inventory. Note it never uses the phrase "sense switch" — the Theory of Operation and the schematic are where that lives. |
| `88-HDSK.pdf` | MITS 88-HDSK hard disk manual | Nothing yet — the board is M7. |
| `TurnKey Board.pdf` | MITS Turnkey Module manual | Nothing yet. But see `docs/boards/mits-frontpanel.md`: the Turnkey board switches its PROM out on an `IN` from port `0xFF`, which it **snoops** rather than answers — so it will not contend with the panel for the port. |

## Traps, paid for once

**The WD177X sheet is the WRONG CHIP for the Tarbell.** It is the WD1770/1772/1773 —
28-pin, single-5V, MFM, motor control. The Tarbell is an **FD1771**. The step-rate table
differs (the BIOS's `STPRATE equ 2` is 10 ms on a 1771 *only*; the 177x reads that code as
20 ms) and the Type-II status bits differ (the 1771 carries a two-bit record type where the
177x lineage puts write-fault). Building from the sheet that was to hand would have produced
something plausible, clean, and **wrong** — a controller that seeks at the wrong speed and
mis-reports deleted records, while looking entirely finished. `tests/test_wd17xx.cpp` keeps
a tripwire on the step-rate table so nobody "fixes" it back. See `src/chips/wd17xx.h`.

**The Operator's Manual never says "sense switch".** Not once — grep it. It documents the DATA
switches (7–0) and the ADDRESS switches (15–0) and stops there, because to an *operator* there is
no such thing as a sense switch: it is the same row of toggles, and what it means depends on what
the program does with it. The fact that `IN 0FFH` returns `SA8`–`SA15` is in the **Theory of
Operation** and in **schematic 880-106**, and nowhere else. A plausible story about which eight
switches feed the port would have been very easy to write and would have had a 50% chance of being
backwards. See `docs/boards/mits-frontpanel.md`, which shows *why* it is the top eight (that buffer
bank already existed, to serve EXAMINE).

**Check for a text layer before spending an hour on a scan:** `pdftotext file.pdf - | wc -c`.
Several of these are pure page images and yield nothing. When that happens, the Read tool
renders PDF pages directly — that is how the WD177x was identified as the wrong part.

**Read the TABLES as images, not the OCR.** The FD1771 sheet's own prose contradicts its
Table 6 on a bit number (it calls Record Not Found "status bit 3" where the table says S4).
The table is right. Bit numbers from OCR'd prose are not evidence.

**A scan may be incomplete and not say so.** `88-SIO Rev 0 & 1.pdf` is titled "Rev 0 & 1"
and contains no Rev 1 section at all, and its address-selection chart is missing two of its
five pages. Check before trusting a gap.

## Related

- `docs/roms.md` — provenance of the ROM images that *are* committed (they are small, and
  the build embeds them).
- `docs/boards/*.md` — the transcribed spec for each board. These are the working documents;
  the PDFs are the cross-check, not the dependency.
