# EDOS — Motorola EXORdisk EXORciser Floppy Disk Operating System (M6800)

Source: [`EDOS-I Manual.pdf`](#) — Motorola Semiconductor Products Inc., *M68FD3601–M68FD3604 EXORdisk MOTOROLA FLOPPY DISK SYSTEM USER'S GUIDE* (undated cover; the resident-driver listing is stamped **VERSION 2.0, 4/9/75**). Scanned image PDF, no text layer; distilled by reading the page images. Provenance: the deramp.com iCOM floppy software collection (`.../altair/software/icom_floppy/FDOS/`).

## Provenance / naming caveat (read first)

**This manual is NOT the iCOM 8080/Altair "EDOS-I" the filename implies.** It is Motorola's **EDOS** — the **EXORciser floppy Disk Operating System** for the Motorola **M6800** running under **EXbug** on an **EXORciser**, driving Motorola's **EXORdisk** 8-inch floppy subsystem. The disk drive hardware is nonetheless an **iCOM** product: the bundled diagnostic listing is headed *"ICOM, INC. FD360-X-68 DIAGNOSTIC"*, i.e. an iCOM FD360-family drive rebadged with a Motorola 6800 (`-68`) interface. So this document links the iCOM floppy hardware line to a 6800 host, but the operating system, CPU, monitor (EXbug), and tools are all Motorola, not the 8080-based iCOM FDOS-II/FDOS-III family. The internal module name in the source code is in fact **FDOS** ("FDOS RESIDENT MODULE"); the user-facing name throughout the manual is **EDOS**. Treat "EDOS" and "FDOS" as the same system here. For an 8080/Altair emulator this manual is useful mainly for the shared iCOM FD360 disk **format** (77 tracks / 26 sectors / 128 bytes) and the logical→physical sector-skew idea, not for the CPU-side software.

The iCOM FD3712/FD3812 floppy hardware proper is documented separately in `reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`.

## 1. What EDOS is and how it is structured

EDOS is delivered as one bootable **system diskette** plus a **PROM-resident driver** on the EXORdisk interface board. It consists of four programs:

| Program | Role |
|---|---|
| EDOS Resident Driver | PROM-resident. Disk I/O + program loading for EDOS; also callable by user programs for disk read/write and program overlay/chaining (Section 4). Internally named FDOS, v2.0. |
| EDOS Executive | Loaded into RAM by EXbug `E800;G`. Runs all EDOS operator directives and file management. Prompts with **`!`** on the console when awaiting a directive. |
| EDOS Editor | Functionally equal to the Motorola EXORciser Resident Editor, but its input is a disk source file and its output is written to a disk source file. |
| EDOS Assembler | Functionally equal to the Motorola EXORciser Resident Assembler, but source comes from a disk file and hex object goes to a disk file. |

Disk space is split into a **system area** (holds the Executive, Editor, Assembler) and a **user file area**. The delivered diskette ships the three system programs' hex object also as user files: **File 1 = Executive hex, File 2 = Editor hex, File 3 = Assembler hex** (back these up early with the `D` directive — media is volatile).

## 2. Start-up, entering / leaving EDOS

Follow the normal EXbug start-up. Two directive sets then coexist: EXbug's and EDOS's.

1. Power on EXORciser, console, and **EXORdisk** (power the drive on *before* EXbug; no diskette in a drive while powering the EXORdisk on/off).
2. Start EXbug.
3. Insert an initialized **system diskette** into **drive unit 0** (the "system drive") and close the door.
4. Ready.

| Action | Command |
|---|---|
| Enter EDOS from EXbug MAID | `E800;G` |
| Enter EDOS when Exec is already in RAM (e.g. loaded from tape) | `20;G` |
| Return to EXbug from EDOS | `M` |
| EDOS ready prompt | `!` printed on console |

## 3. File organization

The user file area on every diskette is **7 fixed-length files**, numbered 1–7, each holding either program source or hex object. A file is referenced by **drive-unit digit + file digit**:

| File spec | Meaning |
|---|---|
| `1`–`7` (or `01`–`07`) | Files 1–7 on drive unit 0 |
| `11`–`17` | Files 1–7 on drive unit 1 |
| `21`–`27` | Files 1–7 on drive unit 2 |
| `31`–`37` | Files 1–7 on drive unit 3 |

Files are contiguous. Each user file is a fixed **9 tracks** (≈230+ sectors; a margin note in the scan reads "9 tracks / 234 sectors") and begins at:

| File | Start track |
|---|---|
| 1 | 14 |
| 2 | 23 |
| 3 | 32 |
| 4 | 41 |
| 5 | 50 |
| 6 | 59 |
| 7 | 68 |

Tracks below 14 (0–13) are the system area. A file may be given a 1–10 character alphanumeric name (`N` directive); names live in a directory listed by `L`.

## 4. EDOS directives (command set)

The Executive awaits a directive at the `!` prompt. Below, `n`/`m` are file numbers, `u` a drive unit (0–3), `)` = carriage return. Summary from Table 3-1 plus the per-command detail:

| Directive | Syntax | Effect |
|---|---|---|
| Assemble | `An,m,p` | Assemble source file `n`; hex object → file `m`; `p` selects output: `p=3` listing only (list device), `p=4` hex object only (→ file `m`), `p=2` both. All three params required; `n≠m`; Assembler `OPT` directives must permit the requested list/object. Console prints the pass in progress (0–4; 0 = complete). |
| Copy | `C` | Copy the whole diskette in drive 0 onto the diskette in drive 1 (~7 min). Data may be any format, EDOS or not. |
| Dump | `DCn` or `DTn` | Dump user file `n` to the punch device. `DC` → TI cassette, `DT` → TTY (paper-tape) terminal. `DT` punches leader/trailer. File unaffected. |
| Edit | `En,m` | Edit source file `n`, updated source → file `m`. `n=0` assumes an empty input file (entering a new program from the keyboard). `n≠m`. On entry prints `EDOS EDITOR` then `@`. Editor ops identical to the EXORciser Resident Editor. Terminate the session with `@E$$`, which closes/updates the output file and returns to the Executive. |
| Home | `Hu` | Return the head on drive unit `u` to track 0 ("home"). |
| Initialize | `Iu` | Clear the user file area and delete the user file names on drive `u` (default 0). Does **not** touch the system area. |
| List Directory | `Lu` | List, on the list device, every user file's number, name, and size (in sectors) on drive `u` (default 0). |
| Monitor return | `M` | Return control to EXbug. |
| Name File | `Nn,xxxxxxxxxx` | Assign a 1–10 char alphanumeric name to file `n`. Replaces any prior name; does not affect contents. |
| Print | `Pn` | Print user file `n` to the list device. File unaffected. |
| Run | `Rn` | Load and run hex file `n`. Functionally identical to EXbug `LOAD C`. After loading the user program, control returns to EXbug. |
| Store | `SCn`, `SPn`, `STn` | Load user file `n` from tape: `SC` = TI cassette, `SP` = EXORtape, `ST` = TTY terminal. Replaces the file's previous contents. |
| Transfer (Append) | `Tn,m` | Append contents of file `n` onto the end of file `m`. File `n` unchanged. |
| Update System | `XCn`, `XPn`, `XTn` | Replace an EDOS system module from tape (`XC`/`XP`/`XT` = same devices as Store): `n=0` Executive, `n=1` Editor, `n=2` Assembler. Used to install new system versions and to generate the system area of a new diskette. |

## 5. Generating a system diskette (summary)

- **Multi-drive, existing system diskette:** system diskette in drive 0, blank in drive 1, `C`, then `I1` to initialize the copy's user area.
- **Single-drive, existing system diskette:** start EDOS (`E800;G`), insert blank in drive 0, then `XC0`/`XP0`/`XT0` (Exec from tape), `XC1`/… (Editor), `XC2`/… (Assembler), then `I` to init the user area.
- **No system diskette:** start EXbug, load the Executive from tape, `20;G`, then proceed as the single-drive case.

## 6. Error messages (Executive)

| Code | Meaning |
|---|---|
| `E1` | Disk read error — CRC error after 5 read tries. Copy the diskette to recover all but the bad data. |
| `E2` | Output file overflow — output exceeded the 9-track maximum file size. |
| `E3` | Requested drive not ready, or diskette not loaded. |

## 7. Memory map, entry points and vectors (from the driver listing)

The resident driver (`ORG $E800`) is a fixed **jump-vector table** at the front — these are the stable public entry points a user program calls. `EXEC`/`UPDATE`/`EDIT` run in low RAM (`$0020`, `$0023`); `ASMB` runs at `$0400`.

| Addr | Name | Function |
|---|---|---|
| `E800` | FDOS | Cold start: load Executive from disk, then `JMP $0020` (start Exec). |
| `E806` | INTIO | Initialize I/O / reset drive electronics (→ RESET at `E859`). |
| `E809` | XRI | **Disk read vector** → `RI` (`E91B`). |
| `E80C` | XWRT | **Disk write vector** → `WRT` (`E98E`). |
| `E80F` | UPDT | Update entry (→ PATCH `EA79` / `UPDATE $0023`). |
| `E815` | PROG | Load a user program then jump to EXbug. |
| `E81B` | ASSEM | Load Assembler, restore input-file pointers, start (`JMP $0400`). |
| `E824` | EDITR | Load Editor, restore input-file pointers, start (`JMP $0020`). |

Key absolute symbols the driver uses: `XBUG=$F564` (EXbug entry), `CO=$F018` (console-out), `XSTACK=$FF8A`, `TEMP=$FF90`–`$FF95` (scratch/checksum). Sector size is 128 bytes throughout.

### 7.1 Driver zero-page RAM interface (locations `$06`–`$0F`)

A user program calls `RI`/`WRT` (via `XRI`/`XWRT`) after "opening" a file by filling these bytes. Only **one input and one output file** may be open at a time.

| Loc | Description |
|---|---|
| `06` | Input file size (sectors) |
| `07` | Input file beginning **track** address |
| `08` | Input file beginning **unit/sector** address |
| `09` | Controller read-buffer counter (set 0 to open) |
| `0A` | Output file size (sectors) |
| `0B` | Output file beginning **track** address |
| `0C` | Output file beginning **unit/sector** address |
| `0D` | Controller write-buffer counter (set 0 to open) |
| `0E`,`0F` | Temporary |

**Read (`RI`):** each call returns the next data byte in A. Carry set = end of file (returned once input file size reaches 0). Input file size = (sectors to read **+1**) before EOF is flagged, or set to `$FF` for user-managed EOF. Track address = `00`–`4C`. Unit/sector address = drive unit in bits 6&7, **sector−1** (`00`–`19`) in low bits; set loc `09`=0. Each read auto-increments the disk address and decrements the file size; any sector carrying a **Deleted-Data (DD) mark is ignored**.

**Write (`WRT`):** each call writes the byte in A. Output file size = sectors allowed before the driver aborts with `E3` and returns to EXbug (keep between `01` and `$FF`). Track `00`–`4C`; unit in bits 6&7; sector `01`–`1A`; loc `0D`=0. Data buffers to 128 bytes then is written and verified; after 5 failed write attempts the driver writes a **DD mark** to that sector and advances. Because a partial buffer may remain, flush by writing pad bytes (`$00`) until the buffer fills — sample `FILL` routine in the scan.

### 7.2 Logical↔physical sector skew

The driver interleaves sectors to hide rotational latency: it translates a requested **logical** sector into a **physical** sector via table **`TBL`** (`EA5E`). E.g. logical sector 2 → physical `$0A`; logical `$14` → physical `$10`. Transparent under the driver; `TBL` may be overwritten (even to a 1:1 map). The full 26-entry skew table is in the listing (`FCB $1,$A,$13,$2,$B,$14,$3,$C,$15,$4,$D,$16,$5,$E,$17,$6,$F,$18,$7,$10,$19,$8,$11,$1A,$9,$12`).

## 8. EXORdisk hardware interface (Section 4-2 — controller register model)

Signals are MC6820/6821 PIA-compatible, **negative-true** (logic 1 = 0–0.4 V). The controller occupies `EC00`–`EC07`; paper-tape reader at `EC04`/`EC05`.

| PIA reg | Symbol | Use |
|---|---|---|
| `EC00` | DKDID | Data-in / status (read) |
| `EC01` | DKDIC | Data-in control |
| `EC02` | DKCOD | Command-out data |
| `EC03` | DKCOC | Command-out control |
| `EC06` | DKDOD | Data-out data (track / unit-sector / write data) |
| `EC07` | DKDOC | Data-out control |
| `EC04`,`EC05` | PTDTA, PTCTL | Paper-tape reader data / control |

**Input status byte (`EC00`, bits 0–7)** — valid when "Read Data Byte" is false; otherwise these 8 lines carry read data (bit 0 = LSB):

| Bit | Meaning |
|---|---|
| 7 | Read DD Mark — DD mark seen on last read (data still read). Reset via "clear error flags". Also flagged as IRQA1 bit 7 = device BUSY. |
| 5 | Drive Fail — selected drive not up to speed, door open, or no diskette. |
| 4 | Drive Write Protected. |
| 3 | CRC Error on last read. Reset via "clear error flags". |
| 1–2 | Unit # (last selected unit, `00`–`11`). |
| 0,6 | unused / '-'. |

**Output data (`EC06`):** if track address → bits 0–6 = track (bit 7 unused); if unit/sector → bits 6–7 = unit, bits 0–4 = sector.

**Output command (`EC02`, bits 0–7):** bit 7 = Clear Drive Elect; bit 6 = Read Data Byte; bits 4–5 = Data-Line Definition; bits 1–3 = Drive-Control Definition. CB2 strobes command acceptance; a control op raises "unit busy" on CB2's leading edge and drops IRQ1 low on completion.

Drive-control codes (3 bits):

| Code | Operation |
|---|---|
| 001 | Read a 128-char sector into read buffer |
| 010 | Write 128-char sector from write buffer (data recycled into buffer) |
| 011 | Read sector for CRC verify (read buffer unaffected) |
| 100 | Seek to specified unit/track |
| 101 | Clear error flags and abort current operation |
| 110 | Return selected unit to track 0 |
| 111 | Write Deleted-Data address mark on next "write sector" |

Data-line-definition codes (2 bits): `01` = track address, `10` = unit/sector address, `11` = write data (loaded on accept-control-strobe leading edge). **Read Data Byte**: while true, output lines carry data; a CB2 pulse while it is true shifts the next byte from the read buffer.

## 9. Media / format

| Parameter | Value |
|---|---|
| Media | IBM diskette or equivalent, 48 tracks/inch |
| Tracks per diskette | 77 (`00`–`4C`) |
| Sectors per track | 26 (`01`–`1A`) |
| Bytes per sector | 128 |
| Bytes per diskette | 256,256 |
| Bits per diskette | 2,050,048 |

## 10. Bundled diagnostic (iCOM FD360-X-68)

Appendix material includes the *"ICOM, INC. FD360-X-68 DIAGNOSTIC"* (`DIAG68`): load the tape to RAM, run at `$100`, insert a scratch diskette, type a command (CR starts it). Buffer at `$1000`–`$107F`. Commands: `A` clear drive electronics; `BU,T` seek; `DU,S` read sector to buffer; `FU,S` write buffer to sector; `GU,S` continuous read/write test; `HU` track-0↔76 loop; `I` unit-select test; `JU`/`KU` seek test once/continuous; `LU` seek+read continuous; `MU` DD-mark test; `N` return to EXbug; `OXX` fill buffer with hex `XX`; `P` print buffer. Errors: `XX` unit not ready; `01`/`02` CRC on 5 read/write attempts; `03` R/W data error; `04` unit-select error; `05` seek error; `06` DD-mark error; `07` DD-mark error on R/W test.

## Not distilled here

The scan also contains: full **EDOS Resident Driver assembly listing** (Appendix A, `FDOS RES` pages 001–013 — reproduced above only as entry points, RAM map, and skew table, not opcode-for-opcode), the complete `DIAG68` diagnostic listing (only its command/error summary is captured), and **Appendix B EXORdisk schematics** (68 Interface PCB: address buffers, data-bus buffers, address decode, PIA command-out/status-in and data-out/reader sheets, PROM decode — hand-drawn, several legible only in outline). Front-matter warranty/trademark boilerplate and unpacking/installation mechanicals are omitted.
