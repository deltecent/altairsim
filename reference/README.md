# `reference/` — distilled hardware references

These `.md` files are **ours**: text-only emulation references, each written *from* a period
scan (a vendor data sheet or a MITS manual) and each citing that scan by filename in a `Source:`
line at its top. They carry what a developer reopens the scan for — register maps, port
addresses, status/control bit tables, disk geometry and timing, boot sequences, and the quirks
that bite an implementer.

The **scans themselves are not redistributed** (they are large and not ours; see
`.gitignore`). `docs/sources.md` is the manifest — what each original source is, where it came
from, and the traps paid for once. Read it before trusting any single number here; several of
these sources contradict themselves, and `sources.md` records which reading won and why.

The `Source:` links point at `#` — a placeholder to be filled in with a real URL later.

## CPU, front panel, and system

| Reference | What it covers |
|---|---|
| [Altair 8800 Operators Manual](Altair%208800%20Operators%20Manual.md) | Front-panel switches and LEDs, the 8080 register/flag layout, the full 78-instruction set with binary+octal opcodes, deposit/examine/run/step procedures, toggle-in sample programs. |
| [Zilog Z80 CPU](Zilog%20Z80.md) | Zilog Z80 CPU User Manual (UM0080): the full register/alternate/special-register set, the S Z F5 H F3 P/V N C flag layout, all instruction groups with hex opcodes/T-states/flags including the CB/ED/DD/FD/DDCB/FDCB prefix maps, interrupt modes 0/1/2 with NMI and IFF1/IFF2, and reset behavior. |
| [Altair 8800 Theory of Operation](Altair%208800%20Theory%20of%20Operation.md) | CPU-board gating and 2 MHz clock phases, the 8080 status byte and its data-bus bits, machine-cycle/interrupt/hold/reset timing, and the complete 100-pin S-100 bus pinout. |
| [Altair 8800 front panel schematic](Altair%208800%20front%20panel%20schematic.md) | Schematic 880-106: the `IN 0FFH` sense-switch decode (sINP + A8–A15 NAND), the A0–A15/D0–D7 LED buffers, and the RUN/STOP/STEP/EXAMINE/DEPOSIT one-shot sequencing. |
| [TurnKey Board](TurnKey%20Board.md) | Turnkey Module: 1K boot PROM with the port-0xFF disable it snoops, the on-board 6850 SIO, the Auto-Start JMP synthesis (FF00 floppy / FD00 hard disk), and the Turnkey Monitor commands. |
| [Processor Technology Sol-20](Sol-20.md) | The Sol-PC integrated machine: the `F8h`–`FFh` onboard I/O map (serial `F8`/`F9`, the shared mixed-polarity status register `FA` + tape motor/baud write, tape `FB`, keyboard `FC`, parallel `FD`, VDM display `FE`, sense `FF`), every status bit and its active-high/low polarity from the SOLOS drivers, the **keyboard's key codes** (Table 7-4 — the eight bit-7 special keys `MODE SELECT`/`CLEAR`/`LOAD`/`HOME CURSOR`/arrows, and the five keys that send no code at all), and the `C000`–`CFFF` ROM/scratch/video memory map. |

## Serial and UART

| Reference | What it covers |
|---|---|
| [88-SIO Rev 0 & 1](88-SIO%20Rev%200%20%26%201.md) | MITS Serial I/O board: the two-port model, both original and errata status-word bit layouts (with the active-low ready polarity), interrupt-enable bits, the 12-bit baud preset, and the address jumpers. |
| [Altair 2SIO User's Manual](Altair%202SIO%20User%27s%20Manual.md) | 88-2SIO dual-6850 board: per-port register map, control/status bits and their polarity quirks, the ÷16/÷64 baud jumpers, and the three interrupt modes. |
| [Altair 88-ACR Cassette Interface](Altair%2088-ACR%20Cassette%20Interface.md) | 88-ACR cassette: ports 006/007, status-word polarity, 8N1 framing, the 300-baud FSK tone format, the tape leader format, and the load procedure (no motor control). |
| [6850](6850.md) | Motorola MC6850 ACIA: register map, control/status bit layouts, divide/word-select/transmitter-control tables, master reset, and chip-select logic. |
| [com2502](com2502.md) | SMC COM2502/COM2017 UART (the 88-SIO's UART): the control-word framing fields, the SWE#-gated status flags, and double-buffered TX/RX holding-register behavior. |

## Printer

| Reference | What it covers |
|---|---|
| [88-C700 Centronics Printer Controller](88-C700%20Centronics%20Printer%20Controller.md) | MITS 88-C700 S-100 interface for the Altair C700 line printer: the two consecutive ports (even Control/Status, odd Data) with the A0 split and the MITS default base 002, the status byte (ACKNOWLEDGE/BUSY/PAPER EMPTY/SELECT/FAULT/INTERRUPT ENABLE/REQUEST), the control byte (D0 PRIME, D1 INTERRUPT CONTROL), the data-write strobe that clears the ACK latch and interrupt, the single-level interrupt switched per-character or per-CR/LF (SW2 #4), and the C700's own ASCII control codes (LF/CR/DC1/DC3/DEL/SO). |

## Video / display

| Reference | What it covers |
|---|---|
| [Processor Technology VDM-1](Processor%20Technology%20VDM-1.md) | VDM-1 Video Display Module: the 1 KB memory-mapped screen RAM (16×64, default `0xCC00`), the single I/O port (default `0xCC`) with `OUT` scroll-latch + 0.25–0.5 s one-shot and `IN` status (D0 timer, D1 SCAN ADVANCE), the character/cursor byte split (D0–D6 → MCM6574 glyph, D7 → cursor), the CR→EOL / VT→EOScreen control blanking, and the SW1–SW6 video/cursor/blanking options. |
| [Cromemco Dazzler](Cromemco%20Dazzler.md) | Dazzler color graphics: the DMA bus-master framebuffer scan, `OUT 0x0E` (D7 on/off, D6–D0 = 512-byte-aligned base), `OUT 0x0F` (resolution X4/size/color bits), `IN 0x0E` (odd-even line, end-of-frame), the 512-byte/2 KB quadrant memory map, and the byte→pixel/color encoding for all four modes (normal 4-bit RGBI two-per-byte; X4 eight on/off bits). |

## Floppy and disk

| Reference | What it covers |
|---|---|
| [Altair Floppy (88-DCDD) Manual](Altair%20Floppy%20%2888-DCDD%29%20Manual.md) | 88-DCDD 8″ floppy: ports 010/011/012, drive-select/status/control register bits (active-low convention), the 32-sector hard-sector geometry, and the head/step/read/write sequences. |
| [88-MDS Minidisk Manual](88-MDS%20Minidisk%20Manual.md) | 88-MDS minidisk: ports 08/09/0A with full bit tables, 35-track/16-sector geometry, 300 RPM / 64 µs-byte / 12.5 ms-sector timing, and the motor/disable timers. |
| [88-MDS Minidisk Schematics](88-MDS%20Minidisk%20Schematics.md) | 88-MDS 6-sheet schematics: the hard-wired port decode (no address jumper), the S-100-2 MHz-derived byte clock, the non-inverting 8T97 status buffers, and the 4020 motor-off counter. |
| [Minidisk Info from MITS](Minidisk%20Info%20from%20MITS.md) | One-page MITS info sheet: capacity/transfer/access figures, the MDBL and DRWT PROMs, software hard-sectoring. Flags its own wrong 5-second motor timer. |
| [FDC+ Manual](FDC%2B%20Manual.md) | The modern FDC+ drop-in: the port map, the nine latched drive types and their geometries (including the 8 MB medium the period manuals cannot describe), and sector-start interrupt routing. |
| [88-HDSK](88-HDSK.md) | 88-HDSK "Datakeeper" hard disk: the Pertec D3422 geometry, the 4PIO port map, all seven commands with bit layouts, the errata-corrected error/status byte, and the IV-byte register map. |
| [Tarbell Floppy Disk Interface Manual](Tarbell_Floppy_Disk_Interface_Manual.md) | Tarbell SD interface (FD1771): the F8–FC port map with DRQ/INTRQ polarity, the DIP config, the FD1771 command/status layouts, the 32-byte bootstrap, and IBM 3740 geometry. |
| [Western Digital FD1771 - Datasheet](Western%20Digital%20FD1771%20-%20Datasheet.md) | WD FD1771 FDC: the five-register model, all 11 commands (Types I–IV) with opcode/flag bits, per-command-type status bits, and the Write-Track control bytes. |
| [Western Digital WD177X-00 - Datasheet](Western%20Digital%20WD177X-00%20-%20Datasheet.md) | WD1770/72/73 FDC: register set, Type I–IV commands with hex opcodes, per-type status bits, FM/MFM track formats. **Not the Tarbell's chip** — see the trap in `docs/sources.md`. |

## File formats

| Reference | What it covers |
|---|---|
| [Intel Hexadecimal Object File Format](hexfrmt.md) | The Intel HEX specification (Rev A, 1988): the general record layout, all six record types, and the checksum rule (a record sums to zero). Plus what an 8-bit machine actually needs — types 00 and 01 — the modulo-64K address wrap that `LOAD ... AT` relies on, and the traps: RECLEN counts data bytes not characters, a file is sparse and its gaps must not be filled, and its records need not ascend. |

## Software toolchain

Not hardware — Microsoft's CP/M assembler and linker, kept for the **symbol files the
debugger loads**. The toolchain is `M80 → .REL → L80 → .COM/.HEX (+ optional .SYM)`; see the
Symbols section of `docs/manual/debugging.md` and `src/core/symbols.{h,cpp}`.

| Reference | What it covers |
|---|---|
| [Microsoft M80 Assembler](Microsoft%20M80%20Assembler.md) | MACRO-80: command string and default extensions (`.MAC`/`.REL`/`.PRN`/`.CRF`), the `.PRN` listing format the symbol loader reads — the per-line relocation markers (`'`/`"`/`!`) and the symbol-table section that flags every symbol's type (`U`/`C`/`*`/`'`/`"`/`!` and Public `I`) — plus the symbol rules (6 significant chars, `##`/`::`), the **decimal** default radix, and the switch/pseudo-op set. **M80 emits `.PRN`, not `.SYM`.** |
| [Microsoft L80 Linker](Microsoft%20L80%20Linker.md) | LINK-80: the command string and `.REL` default, the switches that decide the output — `/N` (name, `.COM`), `/E`/`/G` (exit/go), `/X` (`.HEX`), and **`/Y` (write `filename.SYM`)** — the 100H startup-`JMP` trap, and the key fact that a Microsoft `.SYM` holds **globals only**, which is why it is a flat name=value list with no EQU/label split. |
| [SOLOS / CUTER](SOLOS.md) | The Sol-20's personality-module monitor (and its non-Sol VDM sibling CUTER): the five console commands (incl. **`ENTR`, which is the `.ENT` file loader** — `.ENT` software is console text, not tape), the four tape commands and ten `SET` commands, the input/output **pseudo-ports** (0 keyboard/VDM … 3 user), the machine-language interface — register conventions on entry (`SP` in the SOLOS stack, `HL` at the jump-table base), the **`C000` jump table** (`START`/`INIT`/`RETRN`/`FOPEN`…`SINP`/`AINP` with the zero-flag status convention), the **VDM display driver** control codes + `1B` escape sequences (cursor addressing, speed), and the cassette **file-header** layout with its auto-execute type bit. The software companion to `Sol-20.md`. |
| [CP/M 2.2 Manual](CPM%202.2%20Manual.md) | The DRI compilation altairsim runs — distilled to the parts we touch: the **page-zero contract** (0000 warm-boot, 0005 BDOS, 005C FCB, 0080 DMA, 0100 TPA), the **CCP** built-ins + line-editing control chars, **`ASM`** (directives, the **decimal** default radix, 16-significant-char identifiers, and the 16-column `.PRN` geometry with its `=` EQU marker that the symbol loader reads), **`DDT`** (the period debugger our own mirrors — `X`/`T`/`G`/`L`/`D`/`A`/`S`, the `CfZfMfEfIf A=…` register line, and RST-7 breakpoints), and the **BDOS** call convention + function numbers. |
