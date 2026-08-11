# iCOM FDOS-II Floppy Disk Operating System

Source: [FDOS-II Manual.pdf](#) — *Operator's Manual, iCOM Microperipherals, FDOS-II
for SBC/8800/ALTAIR/IMSAI/POLY88*, Rev. October 15 1976, © September 1976 (iCOM
Microperipherals, 6741 Variel Avenue, Canoga Park, CA 91303). Scanned image PDF (77 pages,
no text layer), provenance deramp.com (`.../altair/software/icom_floppy/FDOS/`). All text
below was distilled visually from the scan.

FDOS-II is iCOM's second-generation floppy disk operating system for 8080 microcomputers,
supplied with the iCOM FD3712/FD3812 (FD360) single-density floppy hardware, which is
documented separately in `reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`. It sits
between the earlier EDOS and the later FDOS-III in the iCOM ecosystem. FDOS-II bundles four
software modules — a PROM-resident disk driver/bootstrap, a RAM-loaded command Executive, a
Text Editor and a Macro Assembler — and runs on the SBC-80/10, MITS Altair 8800, IMSAI 8080
and PolyMorphic Poly-88. The same disk format and Executive underlie iCOM's Text Editor,
Macro/Relocating Assemblers and DEBBI BASIC, which are loaded and run through FDOS-II.

The interface board carries the resident PROM and a block of on-board scratch RAM holding the
I/O vectors and the driver's file pointers. Two builds exist with different load addresses:
an **Altair/IMSAI/Poly-88/8800** build (PROM at C000H, scratch RAM at C400H) and an
**MDS/SBC-80** build (PROM at E800H, scratch RAM at 3CE0H). Addresses in this reference use
the Altair/IMSAI build unless noted; the MDS equivalents are given where they differ.

---

## 1. Modules and memory map

| Module | Where it lives | Role |
|---|---|---|
| Resident Module | PROM on interface board (C000H; MDS E800H) | Disk read/write handler, bootstrap loader, RI/WRT byte routines. Available to user programs. |
| Executive | RAM, loaded from disk by the bootstrap | Command-line interpreter, file management, prints the `!` prompt. |
| Text Editor | Disk file `EDIT`, loaded to RAM on the EDIT/BUILD command | Edits a disk input file into a disk output file; reloads Executive on exit. |
| Assembler | Disk file `ASMB`, loaded to RAM on the ASMB command | Assembles a disk source file to object/listing; reloads Executive on exit. |

### 1.1 Interface-board resident entry points (Altair/IMSAI build, from Appendix B)

| Address | Symbol | Purpose |
|---|---|---|
| C000H | FDOS | Bootstrap entry (`GC000` from mini-monitor boots FDOS-II) |
| C003H | CI  | Console (keyboard) input vector → `JMP VCTRS+3` |
| C006H | CO  | Console output vector |
| C009H | RDRIN | Reader input vector |
| C00CH | LO  | List output vector |
| C00FH | PO  | Punch output vector |
| C012H | MNTR | System monitor (mini-monitor) vector |
| C033H | RIV | Disk read routine (`JMP RI`) — user-callable byte read |
| C036H | WRTV | Disk write routine (`JMP WRT`) — user-callable byte write |
| C07AH | RESTR | "Rewind" input file (restore IFILE pointers) — `CALL RESTR` |
| C045H | UPDAT | Update output-file directory entry, reload Executive |
| C3E4H | — | I/O-vector table / mini-monitor **re-entry** address (`JMP MNTRX`) |
| C3E7H | — | Mini-monitor **power-up/start** address (`JMP INIT`) |
| C3ECH | VECTR | Base of the I/O vector word table (see below) |

The PROM's `VECTR` table (C3ECH+) holds the working device vectors as 16-bit words: console
in, console out, reader (RDIX), list (LOX), punch (POX), monitor (MNTRX), disk read (RI),
disk write (WRT), assemble/edit (4000H), executive (4000H), UPDAT (4300H).

Console/disk port equates (Altair build): console control port 0, console data port 1,
console RxRDY = bit0, console TxRDY = 20H; disk `DATAO`=C1H, `DATAI`=C0H, `CNTRL`=C0H.
(MDS build: disk `DATAO`/`DATAI`=07H, `CNTRL`=06H; console ports 0/1.)

### 1.2 Scratch-RAM layout (Altair build C430H base; MDS 3CE0H base)

The interface board's on-board RAM holds a JMP vector table (C400H–C420H) initialized at boot
and the driver's file-pointer block (BASE = C430H). Section 4.1's RI/WRT variables live here:

| Symbol | Addr (Altair) | Meaning |
|---|---|---|
| PASS  | C430H | RUNGO / assembler interpass byte |
| OFILE | C431H | output-file flag |
| OUNIT | C432H | output unit |
| IUNIT | C433H | input unit |
| ISIZE | C434H | input file size in sectors (2 bytes) |
| ITRK  | C436H | input file beginning track |
| ISCTR | C437H | input file beginning unit&sector |
| ICNTR | C438H | controller read-buffer counter |
| OSIZE | C439H | output file size in sectors (2 bytes) |
| OTRK  | C43BH | output file beginning track |
| OSCTR | C43CH | output file beginning unit&sector |
| OCNTR | C43DH | controller write-buffer counter |
| TITRK | C42FH | saved input track (for RESTR) |
| TISZE | C43EH | saved input size (for RESTR) |

### 1.3 User-supplied device-vector JMP table (section 1.3.3)

To run FDOS-II in a non-default configuration the user writes a JMP table into the interface
board's RAM (C400H onward) pointing at their own I/O routines, before booting. Each entry is
a 3-byte `JMP` (`C3H`, lo, hi):

| RAM addr | Routine | Description |
|---|---|---|
| C400H | CI  | Return one char from console keyboard in A; carry reset. |
| C403H | CO  | Output the char in C to the console. |
| C406H | RI  | Return one byte from reader in A; carry reset if a byte returned, set if none. |
| C409H | LO  | Output the char in C to the list device. |
| C40CH | PO  | Output the byte in C to the punch device. |
| C40FH | EXIT | Entry to user's monitor. Used by the EXIT command and on fatal errors. |
| C412H | RI  | Disk read vector (`C3 09 C1` → C109H). |
| C415H | WRT | Disk write vector (`C3 94 C1` → C194H). |
| C418H | —   | Assemble/edit vector (`JMP 4000H`). |
| C41BH | —   | Exec vector (`JMP 4000H`). |
| C41EH | —   | Update vector (`JMP 4300H`). |

Power-up initialization: execute at C3E7H (`GC3E7`); subsequent re-entries at C3E4H. Assumes
ports 0 and 1 for the console device. A permanent auto-initializing copy of FDOS-II can be
built by assembling a JMP-table stub (`ORG C400H`) with the user routine addresses, deleting
the EXEC end-file, MERGing, and running `XGEN` onto a fresh diskette (§1.3.4–1.3.9). The
`RUNGO` execution address can be redirected by adding `ORG C418H / JMP <userstart>` to a
program's source (§1.3.10).

---

## 2. Mini-monitor (interface-board PROM monitor)

Prompt character `>`. Used to boot FDOS and to test RAM. Commands:

| Command | Syntax | Effect |
|---|---|---|
| GOTO | `G XXXX <cr>` | Execute at hex address XXXX. `GC000` boots the FDOS loader. |
| MEMORY DISPLAY/ALTER | `M XXXX <cr>` | Display RAM location XXXX. Typing 2 hex digits writes that byte and advances; **space** = leave unaltered and advance; **carriage return** = terminate. |
| TEST MEMORY | `T XXXX,YYYY <cr>` | Continuous RAM test from XXXX to YYYY. Failure prints `XXXX = YY ZZ` (address, data written, data read). Abort with CTL-C or front panel. |

Recommended to TEST all RAM before loading FDOS — many FDOS-II failures trace to bad memory.

**Boot sequence (§1.5):** power up and execute C3E7H (or user monitor) → change vectors if
needed → insert system diskette in drive 0 → type `GC000 <cr>` (or execute C000H from the
front panel). FDOS-II prints its prompt `!` when ready for a command.

---

## 3. Disk and file format

### 3.1 Diskette layout

- **System Diskette** — three regions: file directory (track 0), system area (tracks 1–3, the
  Executive), and user file area (rest). The supplied preloaded diskette holds `EDIT`, `ASMB`,
  `EXEC` in the user file area.
- **User Diskette** — two regions: file directory (track 0) and user file area (rest). No
  system area, so maximum user storage. `INIT` produces a User Diskette.
- Tracks 0–76 (`00–4C` hex); 26 sectors/track (`01–1A` hex); 128 bytes/sector.
- Drive units 0–3. Unit 0 is always the **System Device** / **Bootstrap Device** and the
  **default directory device** when a suffix is omitted.

### 3.2 Track 0 directory

| Track 0 sector | Contents |
|---|---|
| 1–3 | Reserved for future FDOS-II use |
| 4 | FCB entries 1–11 |
| 5 | FCB entries 12–22 |
| … | … (11 FCBs per sector) |
| 26 | FCB entries 243–253 |

Up to **253 files** per diskette. Each FCB is 11 bytes:

| FCB byte | Meaning |
|---|---|
| 1–5 | File name, ASCII, padded with spaces (20H) |
| 6 | File attributes |
| 7 | Starting track address |
| 8 | Starting sector address (with unit) |
| 9–10 | Length in sectors, most-significant byte first |
| 11 | Reserved for future FCB expansion |

**File attributes:**

| Value | Meaning |
|---|---|
| 00 | User file, no restrictions |
| 01 | Permanent file, cannot be deleted |
| 80 | Deleted file (managed by FDOS-II; not user-settable) |
| FF | End of directory (managed by FDOS-II; not user-settable) |

File length: 1 sector minimum to 1,975 sectors (252,800 bytes) maximum.

### 3.3 File names and device suffixes

- Name = ASCII string, 1–5 significant characters (extra characters accepted but ignored).
  Valid examples: `JACK`, `JOE3`, `X`, `#SAM`, `BLOB5`.
- Optional device suffix = `:unit` (unit 0–3), e.g. `JOE3:1`, `#SAM:3`, `X:0`, `JACK:2`.
  Omitted suffix ⇒ drive 0 (the System Diskette).
- Files occupy contiguous disk space; each new file follows the previous one. Deleting a file
  frees its directory entry and space; `DELET` repacks ("packs down") the user area to close
  the gap.

---

## 4. Resident RI/WRT disk I/O for user programs (section 4.1)

The resident module exposes byte-oriented disk read (`RI`) and write (`WRT`) routines so a user
program can treat the floppy as mass storage outside the FDOS-II environment. A file is
"opened" by setting up pointers; then RI/WRT behave like the monitor's console in/out. Only
**one input and one output file** may be open at a time. Sector size is 128 bytes.

**Opening for input (RI):** store into `ISIZE`, `ITRK`, `ISCTR`, `ICNTR`.
- `ITRK` = starting track (00–4C).
- `ISCTR` = drive unit (00–11 = 0–3 decimal... see note) in bits 6–7, and starting sector−1
  (00–19 hex) in the low bits.
- `ICNTR` = 00.
- `ISIZE` = (number of sectors to read) + 1 before RI returns end-of-file (carry set). Set to
  a large value (FFFF) to do your own EOF handling.

Each `CALL RI` returns the next byte in A. When ISIZE reaches 0, carry is returned set (EOF),
else reset (`0`). RI increments the disk address (ITRK/ISCTR) and decrements ISIZE per sector.
A sector containing a DD (deleted-data) mark is ignored for its data but still counted in size.
To "rewind," `CALL RESTR` (restores the input pointers from TITRK/TISZE).

**Opening for output (WRT):** store into `OSIZE`, `OTRK`, `OSCTR`, `OCNTR`.
- `OTRK` = starting track (00–4C); `OSCTR` = unit in bits 6–7 + starting sector (01–1A);
  `OCNTR` = 00; `OSIZE` = sectors allowed to be written before WRT aborts with error message
  `3` (or FFFF to self-monitor).

Each `CALL WRT` outputs one byte (char in C). Every 128 bytes WRT writes a sector, increments
OTRK/OSCTR, decrements OSIZE. WRT verifies each sector; after 5 failed attempts it writes a DD
mark to that sector and advances to the next. To flush a partial final sector, keep calling
`WRT 00` until OCNTR = 0 (pad routine). After output, `JMP UPDAT` reloads the Executive and
updates the output file's directory entry.

### 4.2 Logical/physical sector translation (interleave)

RI/WRT translate a requested *logical* sector to a *physical* sector via table `TBL`, to avoid
rotational latency (3:1 interleave). Logical sector n → physical sector as follows (hex):

| Logical | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical | 01 | 0A | 13 | 02 | 0B | 14 | 03 | 0C | 15 | 04 | 0D | 16 | 05 |

| Logical | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical | 0E | 17 | 06 | 0F | 18 | 07 | 10 | 19 | 08 | 11 | 1A | 09 | 12 |

The table may be overwritten (e.g. to a 1:1 map) if the program bypasses this translation.

---

## 5. Operation and command line

FDOS-II awaits a directive at the `!` prompt. Return to the mini-monitor with `EXIT` (or on a
fatal error); re-enter FDOS from the monitor at will. A command line is a directive followed
by comma-separated operands, terminated by carriage return, e.g. `!ASMB,AL,BOB,3<cr>`.

- **RUBOUT** deletes and echoes the last character typed.
- **BREAK** discards the whole line; FDOS-II responds `!`.

### 5.1 Error messages (section 3.3)

Executive messages (typed as text):

| Message | Meaning |
|---|---|
| FORMAT ERROR | Command-line format incorrect; not executed. |
| NO SUCH FILE | Named source file not in the specified diskette's directory. |
| DUPL NAME | Attempt to create a directory entry duplicating an existing name. |
| NO ROOM | Requested more disk space than remains, or a 254th directory entry was attempted. |
| DISK NOT READY | Drive not ready: no diskette, door open, or not up to speed. |
| MEDIA ERROR | Unable to write the media; copy it to recover all but the bad regions. |

Resident-module errors (single digit or `?`, then return to monitor):

| Code | Meaning |
|---|---|
| ? | Checksum error while loading an object file from disk. |
| 1 | Unable to read the diskette media. |
| 2 | Attempt to write more information to a file than space was allocated. |
| 3 | Referenced drive became not-ready (cf. DISK NOT READY). |

---

## 6. FDOS-II directives (section 5)

Syntax note: `<cr>` terminates; `:u` and unit fields are 0–3 (0 assumed if omitted). Numeric
operands (sizes, biases, attributes) are hexadecimal.

| Command | Format | Effect |
|---|---|---|
| ASMB | `ASMB,source,object,pass` | Assemble `source` to `object` and/or listing. All 3 operands required (dummy name if no output). Pass option: **2**=listing to list device only; **3**=object to object file only; **4**=both listing and object; **5**=listing to the disk object file. Example `ASMB,JOES,JOEO,3`. |
| BUILD | `BUILD,newfile` | Create a new source file from the console keyboard via the editor (like EDIT but no pre-existing input). Use editor `I` to insert, `E` to end. |
| CHGAT | `CHGAT,file,newattrib` | Change a file's attributes (see §3.2). `CHGAT,MAIN,1` = permanent; `CHGAT,MAIN,0` or `CHGAT,MAIN` = no restrictions. |
| COPY | `COPY` | One-for-one image copy of drive 0 → drive 1 (format-independent). After 5 failed reads of a sector, the last data (good or bad) is written. |
| CREAT | `CREAT,newfile,size` | Create a directory entry with the given name and size in sectors (hex, min 1), allocating the space. `CREAT,JACK,1F` = 31 sectors, attrib 00. |
| DELET | `DELET:u,file1,file2,…` | Delete the named non-permanent files from unit u and repack the diskette. Names in any order. Unit omitted ⇒ 0. |
| DUMP | `DUMP,file` | Dump a file to the punch device (with blank leader/trailer where applicable). |
| EDIT | `EDIT,infile,newoutfile` | Edit `infile` into new `outfile` using the Text Editor. Editor `A` reads disk→buffer, `P` writes buffer→disk, `E` ends (updates directory, returns to FDOS). |
| EXIT | `EXIT` | Return control to the microcomputer's debug/monitor program. |
| HOME | `HOME,u` | Position drive u's head to track 0. Unit omitted ⇒ 0. |
| INIT | `INIT,u` | Initialize the file directory on unit u (u = 1, 2, 3, or **FF** for unit 0). Clears all files, permanent or not. Required to prepare any non-FDOS-II diskette; result is a User Diskette. Use with caution on a System Diskette. |
| LIST | `LIST,u` | Print the directory of unit u: file names, attributes, starting track/sector, and size in sectors. Unit omitted ⇒ 0. |
| LOAD | `LOAD,newfile` | Create the file and transfer the reader input device's contents into it. |
| MERGE | `MERGE,newfile,file1,file2,…` | Create a new file that is the concatenation of the listed files (source files unaffected). `MERGE,MAINC,MAIN` copies MAIN → MAINC. |
| PRINT | `PRINT,file` | Print a file to the list device. |
| RENAM | `RENAM,oldfile,newfile` | Replace a file's name in its directory entry (name field only). |
| RUN | `RUN,objectfile,offsetbias` | Load an object file into RAM for execution at (file address + offset bias, hex; 0 if omitted). Returns to monitor if the object has no auto-start address, else jumps to it. |
| RUNGO | `RUNGO,hexobject,infile,outfile,n` *(FDOS-II/MDS only)* | Load hex object into RAM, open `infile`/`outfile`, put `n` (00–FF) in location PASS, then transfer control to location ASMB. Trailing fields may be omitted (keep the commas). Defaults: infile indeterminate, outfile track=76 sector=1 size=1, n=0. Rewind input via `CALL RESTR`; after output `JMP UPDAT` to update the directory. |
| VIEW | `VIEW,file,linesperframe,firstline` | Display a file one frame at a time (default 14 lines, first line 1; numbers hex). Keys: **N** next frame, **P** previous, **F** first frame (firstline), **B** beginning (line 1), **CR** return to Executive. |
| XGEN | `XGEN` | Generate the system region of a System Diskette in drive 0 from a copy of the Executive in the reader. Used to make new System Diskettes (then LOAD `EDIT`/`ASMB`). |

---

## 7. Appendix A — FD360 diagnostic

Appendix A is the source header of the iCOM FD360 diagnostic (loaded into RAM and started at
4000H for SBC-80/10, 100H for Altair/IMSAI, 2000H for Poly-88; buffer 1000H–107FH). Single-key
tests on a scratch diskette:

| Key | Test |
|---|---|
| A | Clear drive electronics |
| BU,T | Seek to track |
| DU,S | Read to buffer from present track |
| FU,S | Write from buffer to present track |
| GU,S | Read/write (buffer) continuous on present track |
| HU | Track 0 → track 76 loop |
| I | Unit select test |
| JU | Seek test once (2 min) |
| KU | Seek test continuous |
| LU | Seek test, read only |
| MU | DD-mark test once |
| N | Return to monitor |

(U = unit 0/1/2/3, T = track, S = sector.) Diagnostic error codes: 01 CRC on read (5×),
02 CRC on write (5×), 03 read/write data error, 04 unit-select error, 05 seek error, 06 DD-mark
error, 07 DD-mark error on read/write. Continuous tests abort with CTL-C.

---

## Not distilled here

The scan additionally contains the full 8080 assembly-language source listings of the
Resident Module for both builds (Appendix B: MDS-800 version ORG E800H, and the
SBC/8800/Altair/IMSAI/Poly-88 version ORG C000H, each ~16 assembler pages with symbol table)
and the FD360 diagnostic body — reproduced verbatim from the object code. Key addresses,
equates, the sector-translation table and the interpass/PASS logic have been extracted above;
the routine-by-routine instruction listings, unpacking/installation prose and the cover/legal
pages are not reproduced. Several pages of the 77-page scan are blank.
