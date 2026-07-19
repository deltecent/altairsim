# Sources — the manuals and data sheets the hardware was built from

`reference/` holds the period documentation this simulator's boards were modeled from.
The **scanned manuals and data sheets themselves are deliberately not in git** (see
`.gitignore`): 83 MB, none of them ours to redistribute, and none needed to *build* — only
to *understand*. What **is** tracked is a **distilled, text-only `.md` for each one** — register
maps, port addresses, bit tables, geometry and timing — written from the scan and citing it by
filename. `reference/README.md` indexes them; this file is the manifest that says what each
original source is and where it came from, so a fresh checkout knows what the `.md` was made
from and what scan it is missing.

Nothing here is a build dependency. If `reference/` held only its `.md` files, everything still
compiles and every test still passes. The manifest below names each source scan by its `.pdf`
filename; the tracked distillation is the same name with a `.md` extension (indexed in
`reference/README.md`).

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
| `Hexfrmt.pdf` | *Intel Hexadecimal Object File Format Specification*, Rev A, 6-Jan-1988, 10 pp, **real text layer**. Added by Patrick 2026-07-17. | `src/core/hex.{h,cpp}` and `reference/hexfrmt.md`. The record layout, all six record types, and the checksum rule (a record's bytes sum to zero, RECLEN through CHKSUM). **Authoritative for the modulo-64K wrap** that `LOAD ... AT` does: §4 defines a byte's address as `[DRLO + DRI] MOD 64K` and says wraparound "from 0FFFFH to 00000H results in wrapping around from the end to the beginning" -- so relocating a file whose first record is F000 to AT 0 puts its E000 record back up at F000. Note it contains **no worked example** of a file: the example in `reference/hexfrmt.md` and in the LOAD reference is our own `SAVE` output, checksum-verified. |
| `Zilog Z80.pdf` | Zilog Z80 CPU User Manual **UM0080** (UM008011-0816), 332 pp, **real text layer**. Added by Patrick 2026-07-15. | `src/cpu/cpuZ80.{h,cpp}` and `src/isa/isaZ80.cpp`. The register/alternate/special set, the flag layout, and the CB/ED/DD/FD/DDCB/FDCB opcode maps with byte lengths and addressing. **⚠ Two gaps an emulator author must fill from elsewhere:** (1) the manual labels flag bits **5 and 3 "not used"** and never describes the real-silicon **F5/F3 (YF/XF)** copy behavior — for that the authority is the artifact, **ZEXALL** (`tests/cpu/z80/`), which the core passes. (2) The compact op-code tables give opcodes and byte counts but **not** T-states or flag-effect columns; those are the canonical Zilog per-instruction values (the 240-page per-instruction section was not transcribed line by line). |
| `88-HDSK.pdf` | MITS 88-HDSK hard disk manual | Nothing yet — the board is M7. |
| `88-C700.pdf` | MITS "Altair C700 Printer User's Guide", first printing March 1977, 32 pp. **Has a text layer.** Fetched 2026-07-17 from deramp.com (`.../00-MITS/10-MITS S100 Boards/88-C700 Centronics Printer Controller/88-C700.pdf`). | `src/boards/mits-88c700.{h,cpp}`, `docs/boards/mits-88c700.md`, `reference/88-C700 Centronics Printer Controller.md`. The card is modeled **polled** (the single-level interrupt is not). The two consecutive ports (even Control/Status, odd Data; A0 split; MITS default **base 002**), the status/control bit tables, the data-write strobe (~1.5 µs, "widen to ≈2 µs for Anadex") that clears the ACK latch and interrupt, the single-level interrupt switched **per-character or per-CR/LF (SW2 #4)** — the board does **not** vector like the 88-VI — and the C700's printer-side ASCII control codes (LF/CR/DC1/DC3/DEL/SO). |
| `FDC+ Manual.pdf` | Altair **FDC+** User's Manual, 23 pp, **real text layer**. v2.0, 26-Jan-2026. Fetched 2026-07-13 from deramp.com (`.../altair/hardware/fdc+/FDC+ Manual.pdf`). | `docs/boards/mits-dcdd.md`. The FDC+ is a modern *"100% compatible drop-in replacement"* for the 88-DCDD, so it is not a source for the original card's registers — but it **is** the authority on the **8 MB medium**, which is the one thing the period manuals cannot describe. §3.7.4: drive type **7**, and *"the 8Mb drive looks like an Altair 8 inch drive with 2048 tracks instead of 77."* It also documents the arrangement period software expects — **8 MB on drives A/B, ordinary 77-track floppies on C/D**, mixed on one controller — which is the real justification for the per-drive `Spindle`. |
| `TurnKey Board.pdf` | MITS Turnkey Module manual | Nothing yet. But see `docs/boards/mits-frontpanel.md`: the Turnkey board switches its PROM out on an `IN` from port `0xFF`, which it **snoops** rather than answers — so it will not contend with the panel for the port. |
| `88-MDS Minidisk Manual.pdf` | MITS 88-MDS Minidisk System manual, 110 pp. **No text layer** — read as page images. Fetched 2026-07-13 from deramp.com (`.../altair/hardware/minidisk/`). | `docs/boards/mits-88mds.md`, `src/boards/mits-88mds.{h,cpp}`. **Everything about the card**: the port map (p29), the status bits (pp. 30–31, and *"When a bit is True, it is a logic Ø"*), the control bits (pp. 31–32), the sector register (Table 3-B, p34), 300 RPM / 125 kbit/s / 64 µs a byte / 12.5 ms a sector (p4), the **6.4 s Disk Disable Timer** (p68 — a **4020** clocked by the sector pulse, so 12.5 ms × 512 = 6.4 s *exactly*), and the **1 second motor-on delay** (p58, p69). **⚠ IT CONTRADICTS ITSELF THREE TIMES** — see below. **And do not miss the unnumbered handwritten one-shot sheet bound in after p79** ("MINI DISK CONTROLLER BOARD TIMING", signed "Shep"): it is the complete 74123 timing table, with min/max, and it is not in the table of contents. |
| `88-MDS Minidisk Schematics.pdf` | MITS 88-MDS schematics, 6 sheets. **No text layer, and the scan is too coarse to read most R/C values** — say so rather than guess. | `docs/boards/mits-88mds.md`, for **structure, not numbers**: the port decode is hard-wired (`74L30`/`74L10` gates annotated *"= 1 WHEN ADDRESS = 010₈ / 011₈ / 012₈"*, i.e. 08/09/0A, with **no address jumper anywhere on the controller**); the byte clock is divided down from the **S-100 2 MHz bus clock** (pin 49 → ÷2 → ÷8 → 125 kHz), not a crystal; the status buffers are **non-inverting 8T97** (the *signals* are active-low instead — same byte on the bus, different mechanism from the 88-DCDD's 7405s); and the motor-off timer is a **4020B ripple counter (IC B2)**, not a one-shot. |
| `Minidisk Info from MITS.pdf` | MITS technical-information sheet, 1 p, **scanned**. | The system description: two controller boards, 71,680 formatted bytes, the **MDBL** and **DRWT** PROMs. **Wrong on the timer** — it says five seconds where the manual's engineering sections and two pieces of period software all say **6.4**. Marketing, not engineering. |
| `Microsoft M80 Assembler.pdf` | Microsoft MACRO-80 Assembler manual, CP/M version (Heath/Zenith 595-2666-02, © Microsoft 1979), 46 pp, **real text layer**. Fetched 2026-07-17 from deramp.com (`.../altair/software/manuals/Microsoft M80 Assembler.pdf`). | `src/core/symbols.{h,cpp}`, `docs/manual/debugging.md` (Symbols). The **`.PRN` listing format** the debugger loads: the per-line relocation markers and the symbol-table section that flags every symbol's type. Also the default extensions (`.MAC`/`.REL`/`.PRN`/`.CRF`) and the **decimal** default radix. **M80 writes `.PRN`, not `.SYM`.** |
| `Microsoft L80 Linker.pdf` | Microsoft LINK-80 Loader manual, CP/M version (Heath/Zenith, © Microsoft 1979), 11 pp, **real text layer**. Fetched 2026-07-17 from deramp.com (`.../altair/software/manuals/Microsoft L80 Linker.pdf`). | `src/core/symbols.{h,cpp}`, `docs/manual/debugging.md` (Symbols). The **`.SYM` file** the debugger loads: **L80 `/Y` writes `filename.SYM`** (with `/N`, on `/E`) — names+addresses of **globals only**, for DRI's SID/ZSID. Corrects the earlier belief that the Microsoft path emits no `.SYM`; M80 does not, but L80 `/Y` does. |
| `VDM-1 Rev E Newer Manual.pdf` | Processor Technology *VDM-1 Video Display Module User's Manual*, Manual Part No. 208003, Sixth Printing April 1978 (© 1976), 111 pp. **No text layer — read as page images** (71 MB high-res scan). Fetched 2026-07-18 from deramp.com (`.../00-Processor Technology/10-PTC S100 Boards/VDM-1/VDM-1 Rev E Newer Manual.pdf`). Note the scan is a **composite of revisions** — individual pages carry Rev A/B/C footers. | `reference/Processor Technology VDM-1.md`, and (planned) `src/boards/proctech-vdm1.{h,cpp}` / `docs/boards/proctech-vdm1.md`. Authoritative for: the **1 KB memory-mapped screen RAM** (16×64; page = jumper on ADR10–15; default **`0xCC00`**, §3.1.8), the **single I/O port** (6 jumper bits = ADR7–2, **ADR1–0 forced 0**; `port = (base>>8)&0xFC` → default `0xCC`) with `OUT` = scroll-row latch + a **0.25–0.5 s one-shot** and `IN` = status (**D0** one-shot timer, **D1** SCAN ADVANCE), the byte split (**D0–D6** → MCM6574 glyph, **D7** = cursor/blink, not in ROM; §3.1.4), CR→EOL / VT→EOScreen control blanking, and the **SW1–SW6** options (Table 3-1: video polarity / cursor / control-char blanking). Derive D0/D1 status from the `Clock`, not a poll counter. |
| `Cromemco_Dazzler_Instruction_Manual.pdf` | Cromemco *Dazzler Instruction Manual*, Part No. 023-0003, November 1978 (US Patent 4,121,283), 23 pp. **No text layer — read as page images.** Fetched 2026-07-18 from deramp.com (`.../00-Cromemco/10-Cromemco S100 Boards/Dazzler/Manual/Dazzler Rev D/Cromemco_Dazzler_Instruction_Manual.pdf`). | `reference/Cromemco Dazzler.md`, and (planned) `src/boards/cromemco-dazzler.{h,cpp}`. Authoritative for: the **DMA bus-master** framebuffer scan (~1 MB/s, ~15% slowdown — rides `requestsBus()`/`busMaster()`), `OUT 0x0E` (**D7** on/off, **D6–D0** = A15–A9 → **512-byte-aligned base** `(v&0x7F)<<9`), `OUT 0x0F` (**D6** X4, **D5** 512 B/2 KB, **D4** color/mono, **D3–D0** X4 global color/intensity), `IN 0x0E` (**D7** odd/even line, **D6** end-of-frame low ~4 ms), the **512 B (1 quadrant) / 2 KB (4 quadrants, 2×2 tile)** memory map, and the byte→pixel encoding (**normal**: two 4-bit RGBI elements/byte; **X4**: eight on/off bits as two 2×2 blocks). The p6 front-panel test program is a ready-made demo. Derive the status bits from the `Clock`. |
| `730000_Sol_Systems_Manual_Jan1978.pdf` + `roms/SOLOS/SOLOS13.ASM` | Processor Technology *Sol Systems Manual*, Jan 1978, and the **SOLOS 1.3 source** (© 1977 Processor Technology Corp., the "SOLOS 4-1" EPROM revision) it drives. Manual fetched from deramp.com (*Sol-20 hardware*); corroborated by the deramp.com *Sol-20 Restoration* notes (`.../processor_technology/sol-20/hardware/Sol-20 Restoration.pdf`). | `reference/Sol-20.md`, `src/boards/proctech-sol.{h,cpp}`, `docs/boards/proctech-sol.md`, `machines/sol20.toml`, `roms/SOLOS/`. Authoritative for the Sol-PC's **`F8h`–`FFh`** onboard I/O: serial status/data `F8`/`F9` (**active high**, D6 RX-rdy / D7 TX-empty), the **shared status register `FAh`** that multiplexes keyboard (D0), parallel (D1/D2) and tape (D3/D4/D6/D7) with **mixed polarity** (kbd/parallel active **low** — the drivers `CMA`-invert — tape active **high**) plus the `OUT FAh` tape motor (D7/D6) and 300-baud select (D5), tape data `FB`, keyboard data `FC`, parallel data `FD`, the **VDM display parameter port `FEh`** (low-4-bit scroll, fixed on the Sol vs the stand-alone VDM-1's page-tied port), sense `FF`, and the `C000`–`CFFF` ROM/scratch/video memory map. **Polarity was settled from the SOLOS driver code, not the `EQU` names.** The `HELIOS`-conditional disk ports `F0`–`F7` are **not** part of a stand-alone Sol-20. |
| `SOLOS.pdf` | Processor Technology / Software Technology Corp. *SOLOS/CUTER User's Manual*, document **M0100**, © 1977, 31 pp, **real text layer**. Fetched 2026-07-18 from deramp.com (`.../processor_technology/sol-20/software/manuals/SOLOS.pdf`). The *software* companion to the *Sol Systems Manual* — the monitor that lives in `builtin:solos`. | `reference/SOLOS.md`, `roms/SOLOS/`, `machines/sol20.toml`. The SOLOS/CUTER **command set** (5 console incl. `ENTR` = the `.ENT`-file loader, 4 tape, 10 `SET`), the input/output **pseudo-ports** (0 keyboard/VDM … 3 user), and the **machine-language interface**: entry register conventions (`SP` primed into the SOLOS stack for a `RET` to command mode, `HL` at the jump-table base), the **`C000` jump table** (`START` `00`/`7F` SOLOS-vs-CUTER discriminator, `INIT`/`RETRN`/`FOPEN`/`FCLOS`/`RDBYT`/`WRBYT`/`RDBLK`/`WRBLK`/`SOUT`/`AOUT`/`SINP`/`AINP` — the last two returning **status in the zero flag, char in `A`**), the **VDM display driver** control codes (`01`/`0B`/`0D`/`13`/`17`/`1A`) and `1B` cursor/speed escape sequences, and the cassette **file-header** layout (name/type/size/addr/xeq, TYPE bit 7 = non-executable). **`.ENT` files are console text loaded via `ENTR`, not a CUTS tape image.** |
| `CPM 2.2 Manual.pdf` | Digital Research *CP/M Operating System Manual*, the reformatted 2.2 compilation (© 1982), 317 pp, **real text layer**. Fetched 2026-07-17 from deramp.com (`.../altair/software/manuals/CPM 2.2 Manual.pdf`). Six sections: CCP, ED, ASM, DDT, BDOS interface, BIOS. | `src/core/symbols.{h,cpp}` and `docs/manual/debugging.md` (the **`ASM` `.PRN`/`.HEX`** the loader reads — 16-column geometry, `=` EQU marker, **decimal** default radix), plus the OS altairsim boots on the 88-DCDD/88-MDS: the **page-zero contract** (0000 warm-boot / 0005 BDOS / 005C FCB / 0080 DMA / 0100 TPA) and the **BDOS** call convention. **`DDT`** is the period analogue of altairsim's own debugger (`X`/`T`/`G`/`L`/`D`/`A`/`S`, the `CfZfMfEfIf A=…` register line, RST-7 breakpoints). Distilled to the toolchain-relevant parts; ED and the BDOS/BIOS internals are not modeled. |

## `disks/` — the CP/M images, and the listings that came with them

`disks/` follows the same rule as `reference/`, and `.gitignore` enforces it as an **allowlist**:
the disk images and the vendor ReadMes are **not in git** (20 MB), while the `.ASM` listings the
boards were *built from* **are** — they are first-hand period artifacts, they are cited by path in
`docs/boards/*.md`, and a citation to a file that is not in the tree is worthless.

Every directory carries a `README.md` with the exact download URL and how to run it, so a fresh
clone knows what it is missing and why. All of it is Mike Douglas's work, from **deramp.com**
(verified 2026-07-13):

| Directory | Source | Runs as |
|---|---|---|
| `mits-88dcdd/cpm22/buffered/` | `…/8_inch_floppy/CPM/CPM 2.2/CPM 2.2B/` | `examples/cpm/cpm22-buffered.toml` → `56K CP/M 2.2b v2.3`. **The machine file and the 56K image moved to `examples/cpm/` on 2026-07-19** — they ship; what stays here is source. **`BOOT.ASM` + `BIOS.ASM` here are the 88-DCDD's authoritative source.** |
| `mits-88dcdd/cpm22/8mb/` | `…/8_inch_floppy/CPM/CPM 2.2/FDC+ 8Mb CPM 2.2/` | `cpm22-8mb.toml` → `A0>`. Authoritative for the **8 MB medium** — which is the *same* hard-sector format, just 2048 tracks (see `docs/boards/mits-dcdd.md`). |
| `mits-88dcdd/cpm22/burcon/` | `…/8_inch_floppy/CPM/CPM 2.2/Burcon CPM/` | `cpm22-burcon.toml` → `56K CP/M Version 2.2mits`, © 1980 Burcon. |
| `mits-88dcdd/cpm22/lifeboat/` | `…/8_inch_floppy/CPM/CPM 2.2/Lifeboat CPM/` | `cpm22-lifeboat.toml` → Lifeboat `CONFIG` v4.8. A **48K** build, so it re-fits the memory card. |
| `mits-88dcdd/cpm22/pcgetput/` | `…/8_inch_floppy/CPM/CPM 2.2/PCGET and PCPUT/` | No machine file — CP/M-side XMODEM tools. Already on both `.dsk` images above. |
| `mits-88mds/cpm22/` | `…/minidisk/CPM 2.2/` | `cpm22-mini.toml` → `56K CP/M 2.2b v2.3 / For Altair Mini Disk / A>`. **A DIFFERENT CONTROLLER** — the 88-MDS, not the 88-DCDD. **`BOOT.ASM` + `BIOS.ASM` here are authoritative** for the minidisk's geometry (`MINIDSK`: 35 tracks, 16 sectors, `DATATRK` 4) and for the card's own account of itself. Ships as **two disks** because a minidisk holds a fourteenth of an 8″ floppy. |
| `tarbell-sd/cpm22/buffered/` | `…/tarbell_floppy_controllers/single_density_controller/CPM 2.2B (track buffered)/` | **Nothing.** The Tarbell is not built. `BIOS (1).ASM` is kept only because `docs/boards/tarbell-sd.md` cites it for the grounded-E30 finding. |

### The four images that ARE in git

An image that has to be downloaded before a test can run is an image that makes the test **skip** —
and three acceptance tests were skipping on every fresh clone and in all of CI. 480 KB buys them
back, so `.gitignore` names these, one line each (2026-07-18):

| Tracked file | Upstream SHA-256 | Why this one |
|---|---|---|
| `examples/cpm/cpm22b23-56k.dsk` | `3147946a…57a2cc` | The bootable 8″ system disk, and the only CP/M a fresh clone gets. `acceptance-examples`, `acceptance-dcdd-readonly`, `acceptance-ddt`, `acceptance-hostbridge`. |
| `mits-88mds/cpm22/CPM56K-1.DSK` | `41c87b01…32d3d` | The minidisk system disk (A:). `acceptance-minidisk`. |
| `mits-88mds/cpm22/CPM56K-2.DSK` | `0f6480b1…586db` | Its tools disk (B:) — `cpm22-mini.toml` mounts both, so one without the other does not boot. |
| `examples/diskbasic/Disk BASIC 4.1.dsk` | `e1fb7255…36b69` | Altair BASIC Rev 4.1 [Disk Extended Version], MITS 1977. `acceptance-diskbasic`. Added 2026-07-19. |

**The Disk BASIC image's upstream is NOT RECORDED**, and the hash above is of the file as it sits
in the tree, not of a download anyone has re-fetched. It arrived by hand rather than through
`fetch-disk-images.sh`, so unlike every other row here nobody can currently check it against the
place it came from. `disks/mits-88dcdd/diskbasic/Basic Versions.pdf` — MITS's own table of what
each BASIC release does — is kept beside the sources as the documentation for it, and is what the
C/O/Q answer set comes from. Fill the origin in when it is known.

**`cpm22b23-56k.dsk` is not byte-identical to the download**, and that is deliberate: it ships with
our `R.COM`, `W.COM` and `HDIR.COM` installed (26K free → 18K), so the host-bridge utilities exist
somewhere other than as `.HEX` in `cpm/hostbridge/`. The SHA above is the **pristine upstream** file;
the recipe that turns it into the tracked one is `tools/install-hostbridge-utils.sh`, which is what
makes the blob auditable rather than mysterious. Everything else on the disk is untouched.

Everything not tracked is one command away — `tools/fetch-disk-images.sh` downloads the 8.6 MB FDC+
image and the 24K floppy and checks both against pinned SHA-256s. It **never writes to what it
fetched**, so those hashes keep matching; anything needing a prepared disk copies to scratch first.
Since 2026-07-19 **no test needs either**: the 8 MB image is wanted only by `hostbridge.cmake`'s
`MODE=build`, which is run by hand when a `.ASM` changes.

CP/M 2.2 itself is redistributable: Bryan Sparks, president of DRDOS, Inc. and successor in interest
to Digital Research, granted "a right to use, distribute, modify, enhance, and otherwise make
available in a nonexclusive manner CP/M and its derivatives" (July 2022, superseding the 2001 Lineo
grant that had been tied to one website) — <http://www.cpm.z80.de/license.html>.

`tapes/` is **fully tracked** — a `.tap` is small, and the BASIC and `PS2-MON.TAP` images are what
the acceptance tests boot. The rule was never "media is untracked"; it is "*huge* media that is not
ours is untracked."

## `examples/` — TREK80, and a tape we made ourselves

`examples/sol/` ships a Processor Technology **Sol-20** running **TREK80** (© 1977 Processor
Technology Corp.). Everything there came from the deramp.com MFE archive, fetched 2026-07-19:

<https://deramp.com/downloads/mfe_archive/010-S100%20Computers%20and%20Boards/00-Processor%20Technology/40-PTC%20Software/63-TREK80/>

| File | Provenance |
|---|---|
| `TREK80.ENT` | `…/63-TREK80/Hex Dump/TREK80.ENT` — a SOLOS `ENTER` script: 7,840 bytes loading at `0000`, entry `AF C3 5C 1D`. **This is the authoritative image**, and it is the only tracked *source* in that directory. |
| `Trek80 Manual.pdf` | `…/63-TREK80/` — Processor Technology's own manual for the game. |
| `TRK80.TAP`, `TRK80.WAV` | **Derived**, by `examples/sol/make-trek80-tape.sh`. Not downloaded. |

**Why the tape is derived rather than downloaded, which is the part worth recording.** The archived
recording (`…/sol-20/software/tape_wav_files/TRK80.WAV` on deramp.com) decodes to 7,932 bytes with
**27 framing errors**, and those errors desynchronize the byte stream: **6,778 of 7,840 payload
bytes are wrong**, and the entry point reads `00 00 00 00` instead of `AF C3 5C 1D`. It cannot be
loaded. (It is still historically important — it is byte-identical to the recording
`reference/Sol-20.md` measured the CUTS parameters from.) The MFE archive's MP3 is a different dub
of a different build (8,285 bytes, 31 errors) and is no better.

A SOLOS cassette also cannot be hand-assembled: it carries a header checksum and one after every
256-byte block, and a tape whose checksums are wrong is **invisible** — `CA` lists nothing, `GE`
never finds the file, and nothing prints an error. So the tape is written **by SOLOS itself**:

1. convert `TREK80.ENT` to Intel HEX;
2. `LOAD` it into a running Sol-20, mount a blank `.TAP` in record mode;
3. `SA TRK80 0000 1E9F 0000` — the fourth argument is the execute address, which is what makes
   `XE TRK80` work rather than only `GE`;
4. `UNMOUNT` to commit. Result: 7,939 bytes.
5. `altair_tapetool encode TRK80.TAP TRK80.WAV cuts1200 22050 3 2` → a 3.4 MB WAV with **0** framing
   errors that decodes back byte-identical.

The cross-check that the result is the real artifact and not merely a plausible one: the header
checksum SOLOS computes is `D9`, the same byte in the same position as on the genuine archived tape,
whose `SIZE` field (`1EA0`) also agrees with the `.ENT`. `acceptance-trek80` reads the tape back
through SOLOS, for both media, which is the only honest test of a derivation like this.

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

**A MANUAL CAN BE A FOSSIL OF THE MANUAL IT WAS EDITED FROM.** The 88-MDS manual was clearly written
by editing the 88-DCDD's, and three of its numbers did not get changed:

- **p45: *"a new byte of Write Data is requested every 32 microseconds."*** That is the **8″ card's**
  rate. Everywhere else — p4, p30, p31, p33, p54, p60, and Figures 4-7 and 4-10 — says **64 µs**, and
  125,000 bit/s ÷ 8 = 15,625 byte/s = 64 µs closes it arithmetically.
- **pp. 27–28: the sample driver sends `MVI A,8 ; UNLOAD HEAD / OUT 9`** — on the bit **p32 of the same
  manual calls "Not used"**, for a head **p31 of the same manual says is "always loaded when the Drive
  is enabled."** There is no head solenoid on a minidisk. (p26 does it again: *"ENABLE WRITE WITHOUT
  SPECIAL CURRENT"*, which is the 88-DCDD's D6, also "Not used" here.)
- **The motor timer is 6.4 s, 6 s, and 5 s**, depending on which page you open — p31/p32/p68 vs. p3/p73
  vs. the MITS marketing sheet. **6.4 wins because it is DERIVED**: p68 says the timer is a **4020**
  ripple counter *"clocked every 12.5ms by the START OF SECTOR CLR pulse"*, and 12.5 ms × 512 = 6.4 s
  exactly. A number you can compute beats two round numbers you cannot.

**Prefer the number you can derive, then the schematic, then the tables, then the prose — and the
sample code last of all.** The sample code is the *only* source here that is flatly, checkably wrong,
and it is wrong in the most seductive way: it is real software that really ran.

**AND THIS SIMULATOR MADE THE SAME MISTAKE, FOR THE SAME REASON.** The minidisk lived inside the
88-DCDD as one format row plus `if (d->fmt.sectors != 16)`, and nothing failed — because the two
controllers are register-compatible *by design*, so wrong-card code runs. That compatibility is real
(DBL, the 8″ boot PROM, boots a minidisk perfectly well — `docs/roms.md`), which is exactly what makes
it dangerous: **the thing that works is not evidence that the model is right.**

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

- `reference/README.md` — the index of the distilled, tracked `.md` references, one per
  source in the manifest above.
- `docs/roms.md` — provenance of the ROM images that *are* committed (they are small, and
  the build embeds them).
- `docs/boards/*.md` — the transcribed spec for each board. These are the working documents;
  the PDFs are the cross-check, not the dependency.
