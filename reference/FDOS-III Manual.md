# iCOM FDOS-III Floppy Disk Operating System

Source: [FDOS-III Manual.pdf](#) — "FDOS-III Operator's Manual", iCOM Microperipherals /
Pertec Computer Corporation, Microsystems Division, September 1977 (drawing 505-345-9084).
Scanned image PDF (91 pages, no text layer), read visually. Provenance: deramp.com software
archive, `.../altair/software/icom_floppy/FDOS/`. Not redistributed here — this is a
text-only distillation.

FDOS-III is the most advanced of iCOM's disk operating systems for its FD3712/FD3812 8-inch
floppy hardware (the drive/controller electronics are documented separately in
`reference/iCOM FD3712 & FD3812 Floppy Disk Systems.md`). It succeeds EDOS and FDOS-II. On
top of the earlier byte-oriented disk I/O it adds a full RAM-resident **Executive** (command
interpreter, file manager) plus a suite of disk-resident tools: **Text Editor**, **Relocating
Assembler**, **Linker**, **Library Manager**, Copy, Memory-to-Disk, and System I/O
Generation. It keeps the same on-disk format lineage (26 hard/soft sectors × 128 bytes,
track-0 directory) but organizes the Executive on tracks 1-3 of a bootable **system
diskette**. FDOS-III is portable across many 8080 hosts (MDS, iCOM SBC-80/10 & /20, Altair /
IMSAI 2SIO, Altair S-100, Poly-88, and the Processor Technology Sol-20) via a per-host PROM
resident and a stored I/O-vector table.

Distilled from the Operator's Manual proper (Sections I-V) plus Appendix A (command glossary)
and Appendix B (Minimonitor). Appendices C (Diagnostic listing) and D (Resident assembly
listing) are full source/object listings; their load-bearing facts (entry addresses, RAM
map, sector-skew table, I/O-vector table) are captured in §9-§10 below.

---

## 1. Loading / booting

Follow the host's normal start-up (its resident debug monitor), insert a **system diskette**
in **drive 0**, let it spin up, then GOTO the PROM resident starting address. The resident
bootstraps the Executive into RAM and prints the prompt `!` when ready to accept a command.

| System | iCOM dash no. | PROM resident start (hex) | 2SIO ports | Minimonitor | Notes |
|---|---|---|---|---|---|
| MDS | -53 | E800 | — | No | |
| SBC-80/10 | -56 | E800 | — | No | |
| SBC-80/20 | -56 | E800 | — | No | |
| Altair | -57 | C000 | 10H, 11H | Yes | |
| Altair S-100 bus | -58 | C000 | — | Yes | Must initialize I/O vectors, see §2 |
| Poly | -59 | C000 | — | Yes | With Poly 4.0 monitor |
| Sol | -60 | B800 | — | Yes | With SOLOS monitor |

On the -57, -59 and -60, I/O vector tables are loaded (after the Executive) into the interface
board's on-board RAM to configure I/O for the host. On the **-58 only**, the vector table
must be initialized by hand (§2).

## 2. I/O-vector initialization for -58 (S-100) systems

Assumed hardware I/O: port 0 = control, port 1 = data, input-ready bit = 0 (0 = ready),
output-ready bit = 1 (0 = ready).

Procedure: execute at **C3E7H** to load console vectors and enter the Minimonitor; then enter
`GC000(CR)`. When the CPU cycles at C2XX, halt, and enter console-in (CI) and console-out
(CO) vectors to point at the user I/O subroutines, per this table:

| RAM loc | Contents | Meaning |
|---|---|---|
| 0055H | 0C3H (`JMP`) | CO entry stub |
| 0056-0057H | CI address (lo, hi) | console-in → one char to A, carry reset |
| 0058H | 0C3H (`JMP`) | CO entry stub |
| 0059-005AH | CO address (lo, hi) | console-out from C-register |
| C400H | 0C3H (`JMP`) | CI entry stub |
| C401-C402H | CI address (lo, hi) | console-in → A, carry reset |
| C403H | 0C3H (`JMP`) | CO entry stub |
| C404-C405H | CO address (lo, hi) | console-out from C-register |
| 0176-0177H | CO address (lo, hi) | CO vector restoration |

Then `GOTO 0180H` to restart FDOS-III (prompt `!`), and run a `SYSGN` (§5) to persist the
vectors on the system diskette.

## 3. Command line

At the `!` prompt, a command is a directive optionally followed by operands. The directive is
separated from the first operand by a comma; operands are comma-separated. The line must end
with a carriage return (CR); nothing is interpreted until CR.

- Example: `ASMB,AL,BOB,3(CR)`
- Commands and text may be any mix of upper/lower case.
- Numeric data is **decimal** unless immediately followed by an upper-case `H` (hex).
  `256` and `100H` are the same value.
- **RUBOUT** deletes the last character (echoed back as verification).
- **CTL-R (12H)** re-displays (reiterates) the line so far; entry resumes.
- **ESC (1BH)** aborts the line; prompt reappears.
- Control chars 00H-1FH other than CR (0DH), CTL-R (12H) and ESC (1BH) are invalid and raise
  `FORMAT ERROR`.

Operator interruption (§ Operation 3-4):
- **CTL-C (03H)** — first entry temporarily *pauses* a running function; a **second CTL-C**
  resumes. Entering any other data during the pause gives unpredictable results / possible
  data loss.
- Entering **CTL-C** as a terminator to a *lengthy* function terminates it and returns to
  command input. (The manual's wording distinguishes pause/resume from terminate by context.)

## 4. Command reference

All commands issued at `!`. Operand `unitnumber` (drive) is 0-3; if omitted, drive 0. A
command to a not-ready drive prints `DISK NOT READ`, rings the console bell, and retries once
per second up to ten times before returning to command mode.

| Command | Format | Effect |
|---|---|---|
| **ALLOC** | `ALLOC,filesize,filename` | Create a directory entry `filename` (attr 00) and reserve `filesize` sectors (decimal, min 1). |
| **ASMB** | `ASMB,sourcefile,objectfile,passoption` | Assemble source → object file; listing to list device or disk. All three operands required; use a dummy name (X/Y/Z) to suppress an object/listing file (no dir entry created for a dummy). |
| **BATCH** | `BATCH` | Execute the directives in file `BATCH` on the drive-0 system diskette, as if typed. Batch messages: `"...text..."` (CR) — bell rings, text shown, waits for any console char. Terminate with **CTL-B (02H)** (finishes current directive). |
| **CHGAT** | `CHGAT,filename,newattributes` | Change a file's attributes (00 = user, 01 = permanent/non-deletable). See §7. |
| **COPY** | `COPY` (or `COPY:u`) | One-for-one image copy of drive-0 diskette onto drive-1 diskette (source/dest need not be FDOS format). Bad sector after 5 read tries → prompt to continue/abort. COPY is a separate program and must reside on the specified drive. |
| **DELPK** | `DELPK:u,file1,file2,...,fileN` | Delete the named non-permanent files on drive u, then **pack** (repack user + directory area to reclaim space). Names in any order. |
| **DELET** | `DELET:u,file1,...,fileN` | Delete named non-permanent files from the directory on drive u (space not reclaimed until a DELPK/PACK; deleted files still show in listing but are inaccessible; can be re-activated with CHGAT to attr 0/1 before packing). |
| **DUMP** | `DUMP,filename,B` | Dump file to the punch device with leader/trailer. Without `B`: ASCII, first CTL-Z (1AH) = EOF. |
| **EDIT** | `EDIT,inputfile,newoutputfile` | Edit `inputfile` with the Text Editor into `newoutputfile`. (Editor A = read disk→buffer, P = buffer→disk, E = end/return to FDOS.) `EDIT,,BOB3` creates BOB3 fed only from console via the editor insert function. |
| **EXIT** | `EXIT` | Return control to the host's debug/monitor program. |
| **HOME** | `HOME,unitnumber` | Position the head on the drive to track 0. |
| **INIT** | `INIT,unitnumber` | Initialize the file-directory area on the drive (unit 1-3, or **99 = drive 0**). Clears all files and sets system I/O data to FF/FFFF. Prepares a non-FDOS-III/FDOS-II diskette; intended for **user** diskettes, generally not system diskettes. |
| **LIST** | `LIST,unitnumber,listdevice,MODE` | List a diskette's directory (filename, attributes, start track/sector, size in sectors) plus free-sector count and volume name. listdevice: `C`=console, `L`=line printer (default console). Lists 11 entries at a time; subcommand `N` = next 11, `P` = previous 11; ends on CR or end of directory. 4th field `X` (any non-space) starts the paged mode. |
| **LOAD** | `LOAD,newfilename,B` | Create the file and transfer the reader device into it. Without `B`: terminated by first CTL-Z (1AH). With `B` (binary): terminated when the reader driver returns carry set (EOF). |
| **MERGE** | `MERGE,newfile,file1,...,fileN` | Concatenate hex-ASCII files into a new file, in order; sources unaffected. Assumes CTL-Z = EOF (stops reading that file at CTL-Z). |
| **MERGB** | `MERGB,newfile,file1,...,fileN` | Same as MERGE but binary: ignores CTL-Z, transfers entire contents including trailing nulls of the last sector of each file. |
| **PACK** | `PACK:unitnumber` | Remove deleted filenames from the directory and pack remaining files to free space. (Do not interrupt with CTL-C — results indeterminate.) |
| **PAUSE** | `PAUSE` | Halt (mainly in a batch file) until any console char; bell rings once. |
| **PRINT** | `PRINT,filename,linesperframe,beginninglinenumber,listdevice` | Print file to list device. Defaults: lines/frame 9999+, beginning line 0, listdevice = line printer (`C` = console). Frame keys N/P/F/B as in VIEW. |
| **RENAM** | `RENAM,oldfilename,newfilename` | Rename a file (directory entry only). |
| **RUN** | `RUN,objectfilename,offsetbias` | Load a hex object file into RAM at (file-specified address + offset bias) and execute. Bias decimal or hex, default 0. After load: return to host monitor if the object has no auto-start address, else jump to the object's auto-start address. |
| **Rungo** (implied) | `Hexobjectfile,inputfile,outputfile,N` | Any *unrecognized* command is treated as a hex-ASCII program filename — see §6. |
| **SYSGN** | `SYSGN` | Alter/persist initialization data on the drive-0 system diskette (I/O vectors, volume name). See §5. |
| **VIEW** | `VIEW,filename,linesperframe,firstline,listdevice` | Display file one frame at a time on console. Defaults: 20 lines/frame, first line 0, console. Interactive keys: `nN` next frame (advance n lines), `nP` previous, `F` first frame (firstline), `B` beginning frame (line 0), `CR` return to Executive. n = 1..65535. |
| **XGEN** | `XGEN,filename` | Generate the *system region* (tracks 1-3) of a system diskette in drive 0 from a copy of the FDOS-III Executive (from the reader if filename omitted, e.g. `XGEN,EXEC:1`). Sets I/O data to FF/FFFF — reset it with SYSGN afterward. Used to make new system diskettes / bring up a blank system. |

## 5. SYSGN — system I/O generation

`SYSGN(CR)` → console shows `ICOM SYSTEM I/O GENERATION(N/R/F)`; enter **N** (new data), **R**
(revise existing), or **F** (return to FDOS-III). Each title is presented in turn after CR;
type new data + CR to change, bare CR to keep, **ESC** to terminate. If the systems area
holds FFH/FFFFH the specialization is *not* activated. Data is written to the systems area of
the drive-0 diskette. Fields:

| Title | Format | Example |
|---|---|---|
| VOLUME NAME | up to 21 ASCII chars | `ALTAIR2SIO` |
| CONSOLE INPUT VECTOR | hex address | 0011 |
| CONSOLE OUTPUT VECTOR | hex address | 0010 |
| READER VECTOR | hex address | 0011 |
| PRINTER VECTOR | hex address | 0010 |
| PUNCH VECTOR | hex address | 0010 |
| MONITOR RE-ENTRY VECTOR | hex address | C3E4 |
| I/O INITIALIZATION VECTOR | hex address | 0000 |
| HIGH MEMORY ADDR | hex address | 7FFF |
| CONSOLE STATUS PORT | hex value | 10 |
| CONSOLE DATA PORT | hex value | 11 |
| INPUT DATA AVAIL MASK | hex value | 01 |
| INPUT DATA AVAIL STATE (HI=01) | hex value | 01 |
| LINE PRINTER WIDTH | hex value | 4F |
| OBJECT CODE LOAD ADDR | hex address | 0000 |
| NO. OBJECT CODE BYTES | hex value | 1B |
| BYTE NO. 00 / 01 / … | object code | up to 256 bytes may be entered |

(Each object byte must be altered or defaulted with a CR so the count is correct.)

## 6. Rungo (implied command)

Format: `Hexobjectfilename,inputfilename,outputfilename,N(CR)`

Any illegal/unrecognized command is taken as the name of a hex-ASCII program file; if not
found → `No Such File`. Effect: load the object file into RAM, **open** `inputfilename` and
`outputfilename`, convert `N` (decimal 0-255) to hex and store it in location **PASS**, then
transfer control to memory location **ASMB** (see §9). Any/all of the last three fields may be
omitted, but commas must be preserved for skipped-then-supplied fields. Defaults: input
parameters indeterminate; output file track=76, sector=1, size=1; N=0.

A Rungo'd program should:
- `CALL RESTR` to "rewind" the input file (RESTR is in the resident, §9).
- On finishing output, `JMP UPDAT` (resident) to close the output file, write its directory
  entry, and reload the Executive.
- To update the directory after writing, the program must `JMP UPDAT` in the FDOS PROM driver.

Examples: `MAIN(CR)` loads MAIN and jumps to ASMB. `ICE80,LOADF,SAVEF(CR)` opens input LOADF,
creates/opens output SAVEF, loads ICE80, jumps to ASMB. `TRY,,,7(CR)` sets PASS=7 then loads
TRY and jumps to ASMB.

## 7. Disk & file format

**Media/geometry.** 8-inch diskettes, tracks 0-76 (00-4C hex). 26 sectors/track, numbered
1-26 (01-1A hex), 128 bytes each. Sectors are addressed with a **logical→physical skew** to
avoid rotational latency (see §10). Attribute-controlled deleted-data (**DD**) marks flag bad
sectors.

**Regions.**
- *System diskette* — 4 regions: file directory, system I/O data, system (Executive), user
  file area. **Track 0** = directory + system I/O data; **tracks 1-3** = system Executive;
  remainder = user file area.
- *User diskette* — 2 regions: **track 0** = directory; remainder = user file area.

**Track 0 sector map:**

| Sector | Contents |
|---|---|
| 1 | System I/O data |
| 2 | User object code |
| 3 | User object code |
| 4 | File Control Blocks (FCB) 1-11 |
| 5 | FCBs 12-22 |
| … | … |
| 26 | FCBs 243-253 |

So the directory occupies **sectors 4-26 of track 0** (sectors 1-3 reserved for system-gen /
object data); 11 FCBs per sector × 23 sectors = up to **253 files** per diskette. (§ Operation
notes the 254-entry limit as the `NO ROOM` directory ceiling.) The system I/O data lives on
**track 0, sectors 1-3** of the drive-0 diskette and specializes FDOS-III to the host;
`INIT`/`XGEN` clear it to FF/FFFF.

**File Control Block (11 bytes):**

| Byte | Field |
|---|---|
| 1-5 | Name, space-padded (20H) |
| 6 | Attributes |
| 7 | Starting track address |
| 8 | Starting sector address |
| 9-10 | Length in sectors, MSB first |
| 11 | (reserved for Batch Mode control counter) |

**Filenames.** 1-5 ASCII characters, must be unique per diskette (duplicate → `DUPL NAME`).
Examples: `JACK`, `JOE3`, `X`, `#SAM`, `BLOB5`.

**Drive specifier.** `filename:u` selects drive u (0-3); omitted ⇒ drive 0 (the system /
default directory device). Examples: `JOE3:1`, `#SAM:3`, `X:0`, `JACK:2`.

**Attributes.** `00` = user file, no restrictions. `01` = permanent file, cannot be deleted.

**Allocation.** Files are contiguous in the user region; each new file follows the previous.
Deleting frees a file's space to the succeeding file, and the directory entry's slot is taken
by the next entry ("disk packing"). File length: 1 sector minimum up to **1,975 sectors
(252,800 bytes)**.

## 8. Resident module — byte-oriented disk I/O (RI / WRT)

The Resident lives in PROM (usually on the interface board) and provides disk read (**RI**) and
write (**WRT**) byte routines plus the bootstrap. Open a file by loading pointer locations,
then call RI/WRT exactly like the host monitor's console-in/out routines. Only **one input and
one output file** may be open at once (data is routed through the controller's single input
and single output hardware buffers).

RAM locations used (symbolic; see §9 for addresses on the Altair/IMSAI/Poly build):

| Location | Purpose |
|---|---|
| ISIZE | Input file size in sectors (2 bytes) |
| ITRK | Input file beginning track |
| ISCTR | Input file beginning unit & sector (bits 6-7 = drive 0-3, bits 0-5 = sector) |
| ICNTR | Read buffer counter |
| OSIZE | Output file size in sectors (2 bytes) |
| OTRK | Output file beginning track |
| OSCTR | Output file beginning unit & sector |
| OCNTR | Write buffer counter |

**RI (open + read input):** set ISIZE = (sectors to read + 1); a "unique EOF" run uses a huge
size (e.g. FFFF). ITRK = start track (00-4C). ISCTR = drive in bits 6-7, sector (00-19H) in
bits 0-5. ICNTR = 00. Each RI call returns the next data byte in A (the same register the
monitor's console-in uses). At EOF (ISIZE reached 0) carry is returned **1**, else **0**. As
each 128-byte sector is read, RI increments ITRK/ISCTR and decrements ISIZE; any DD-marked
sector is skipped but still counted in the input size.

**WRT (open + write output):** OSIZE = sectors allowed before WRT terminates with error 3
(FFFF for no monitoring). OTRK = start track. OSCTR = drive (bits 6-7) + start sector (01-1A).
OCNTR = 00. Each WRT call outputs one byte; after 128 bytes it increments OTRK/OSCTR and
decrements OSIZE. WRT verifies each written sector; if it can't write after **5 tries** it
writes a **DD mark** to that sector, advances to the next contiguous address, and retries
(OSIZE decremented for each DD sector). At end, pad with a fill character (e.g. 00) until
OCNTR = 0 so the last partial buffer flushes to media (fill loop: while OCNTR≠0, WRT 00).

## 9. Executive & disk-handler entry points (system-call interface)

**Executive routines** are reachable while the Executive is in RAM via vectors at **START +
offset**. START per host: MCS/MDS = 20H, Altair/IMSAI = 40H, Poly-88 = 2040H, SBC-80/10 =
4040H, SBC-80/20 = 4040H, Sol = 40H.

| Routine | Vector | Function |
|---|---|---|
| UPDAT | START+3 | Close output file; write attribute, start address, size to directory. Requires the output file to have been opened by the implied RUNGO or by STFL2. A-reg: 0 = success, ≠0 = fail. |
| OPENR | START+6 | Open existing file for input (read by RI/RIX). FILENAME = 5-char ASCII at location FIELD (START+7BH); drive as hex in location DRIVE (START+80H). A-reg: 0 = success. |
| OPENW | START+9 | Open existing file for output (written by WRT). Params as OPENR. |
| STFL2 | START+12 | (open new file for output — the routine RUNGO uses to set up its output file / save input pointers; documented as OPENX in the resident, see below). |
| RDSCT | START+15 | Read one sector. |
| WTSCT | START+18 | Write one sector. |

**OPENX** (open new output file at first unused disk address, via WRT): besides opening the
output file, it temporarily saves the currently-open input file's pointers, so a program object
can be loaded / an input file re-passed. Restore with **CALL RESTR** in the resident. On
failure, control goes to the FDOS-III command input after an error message.

**RDSCT / WTSCT** (single-sector, register-passed):
- B = track number (hex)
- A = sector (bits 0-5) + drive (bits 6-7)
- H,L = buffer address
- Return: A = 0 success, ≠0 fail. Drive-not-ready or CRC error returns control to the FDOS-III
  command processor after an error message.

**Disk-handler library (file DKHN, relocatable binary).** Linkable into a user program (link
in relocatable format). Because I/O is routed through the single hardware buffers, to handle
more than one input/output file the program must establish its own 128-byte RAM buffers and
save areas per open file (save ITRK, ISCTR, ISIZE, ICNTR, OCNTR, OTRK, OSCTR, OSIZE), saving
pointers after each open/access. Library entry points mirror the resident: UPDAT (close output
+ directory entry), OPENR/OPENW (existing file for read/write; FILENAME → location FIELD),
OPENX (new output file → location FILED), RDSCT/WTSCT (params as above). The handler uses two
labels: **ERFLG** (1-byte error code, **must be initialized to 0** before any handler call)
and **ERRTN** (2-byte address of an error routine jumped to on error). ERFLG values: 1 = media
error, 2 = drive not ready, 3 = duplicate filename, 4 = insufficient disk space.

**Resident RAM map & entry vectors** (from Appendix D, "RESIDENT 8080 ALTAIR/IMSAI/POLY 88 FDOS
III VERSION 1.0"; PROM ORG = C000H, scratch RAM SCTCH = C400H, BASE = C430H, STACK = C47FH):

| Symbol | Addr | Meaning |
|---|---|---|
| PROM entry `JMP FDOS` | C000 | boot: entry when `Q`/GC000 typed — load FDOS, branch to start |
| CI / CO | C003 / C006 | keyboard-in / console-out vectors |
| RDRIN / LO / PO | C009 / C00C / C00F | reader-in / list-out / punch-out vectors |
| MNTR | C012 | system monitor vector |
| RSTV / RSTRV | C01E / C030 | RESET / RESTR (restore input-file pointers) |
| RIV / WRTV | C033 / C036 | RI / WRT byte routines |
| PASSV | C039 | IPASS — assembler interpass function |
| UPDAT | C045 | close output file + update directory |
| RI | C109 | disk read | 
| WRT | C194 | disk write |
| PASS | C430 | Rungo N parameter |
| OFILE / OUNIT / IUNIT | C431 / C432 / C433 | output file / output unit / input unit |
| ISIZE / ITRK / ISCTR / ICNTR | C434 / C436 / C437 / C438 | input file pointers |
| OSIZE / OTRK / OSCTR / OCNTR | C439 / C43B / C43C / C43D | output file pointers |
| ASMB | C418 | Rungo jump target (VCTRS+24) |
| START | C41B | Executive start (VCTRS+27) |
| UPDTX | C41E | Executive UPDAT (VCTRS+30) |

Power-up entry = **C3E7H** (`JMP INIT`); monitor re-entry = **C3E4H** (`JMP MNTRX`). The stored
**I/O vector table** (VECTR at C3EA on this build) is: console-in, console-out, paper-tape
reader, line printer, punch, monitor, disk-read (RI), disk-write (WRT), assembler/edit vector
(40H), executive vector (40H), UPDAT vector (43H).

## 10. Sector skew (logical → physical)

RI/WRT translate a requested *logical* sector into a *physical* sector to avoid whole-revolution
delays (skew of 3). From the resident's TBL (logical sector 1..26 → physical, hex):

| Logical | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical | 01 | 0A | 13 | 02 | 0B | 14 | 03 | 0C | 15 | 04 | 0D | 16 | 05 |

| Logical | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Physical | 0E | 17 | 06 | 0F | 18 | 07 | 10 | 19 | 08 | 11 | 1A | 09 | 12 |

(e.g. logical 2 → physical 0AH.)

## 11. Error messages

Executive command errors (printed at console):

| Message | Meaning |
|---|---|
| `FORMAT ERROR` | Command line format incorrect; execution could not proceed. |
| `NO SUCH FILE` | Filename not in the specified diskette's directory. |
| `DUPL NAME` | Attempt to add a filename already in the directory. |
| `NO ROOM` | More disk space requested than available, or directory entries would exceed 254. |
| `MEDIA ERROR` | Copy the media to recover all but the inaccessible regions. |
| `DRIVE NOT READY` | Drive not engaged or diskette unformatted; message repeats ~once/second up to ~10 s. |
| `DISK NOT READ` | (Command-mode not-ready) bell rings, retries once/second up to ten times. |

Resident-module errors — a single digit or `?`, then return to the host debug/monitor:

| Code | Meaning |
|---|---|
| `?` | Checksum error loading an object file from disk. |
| `1` | Unable to ready from diskette. |
| `2` | Attempt to write more information to a file than space available. |
| `3` | Referenced disk drive unit not ready. |

Disk-handler library (DKHN) `ERFLG` codes: 1 = media error, 2 = drive not ready, 3 = duplicate
filename, 4 = insufficient disk space (§9).

Diagnostic (Appendix C) error codes: 01 CRC on read (5×), 02 CRC on write (5×), 03 read/write
data error, 04 unit-select error, 05 seek error, 06 DD-mark error, 07 DD-mark error on
read/write.

## 12. System diskette contents

Programs supplied on the FDOS-III system diskette (format column as printed):

| Title | Definition | Format |
|---|---|---|
| ASMB | Relocating Assembler | Hex-ASCII object |
| COPY | Copy program | Hex-ASCII object |
| EDIT | Editor | Hex-ASCII object |
| EXEC | Backup copy of FDOS Executive | Hex-ASCII object |
| DIAGO / DIAGS | Disk diagnostic object / source | Hex-ASCII object / source |
| DKHN | Disk handler routine | Relocatable binary object |
| LIB | Library handler | Hex-ASCII object |
| LINK | Linker | Hex-ASCII object |
| MTDK / MTDKS | Memory-to-disk object / source | Hex-ASCII object / source |
| RDBFL | Binary object file reader | Hex-ASCII object |
| SYSGN | System generation program | Hex-ASCII object |
| TESTS | Disk handler test program | Source |
| TEST1 | Disk handler test file | Text |
| CMNDF | Link command file for the disk-handler test program | — |

(The Editor, Linker, Library Manager, System Generator, Copy, Memory-to-Disk and Relocating
Assembler are stored *within the user file area* of the system diskette, as on the supplied
disk.)

## 13. Minimonitor (Appendix B; Altair S-100 hosts only)

Called when **C3E4** is executed; prompt `>`. Three commands:

| Command | Format | Effect |
|---|---|---|
| Goto | `GXXXX(CR)` | Execute at XXXX. |
| Memory display/alter | `MXXXX(CR)` | Display/alter RAM at XXXX; type two hex chars to change a byte (next byte shown); SPACE = view next location; CR = terminate. |
| Memory test | `TXXXX,YYYY(CR)` | Test RAM from XXXX to YYYY; failure prints `XXXX = YY ZZ` (address, data written, data read). |

---

## Not distilled here

- **Appendix C — Diagnostic Listing** (`DIAGS`): the full 8080 source of the PROM-resident disk
  diagnostic (drive/track/sector test commands A,B,D,F,G,H,I,J,K,L,M,N; error codes captured in
  §11). Source listing only.
- **Appendix D — Resident Listing**: the complete FDOS-III resident assembly/object listing
  ("FDOS MDS-800 Macro Assembler Ver 1.0", ~15 listing pages + symbol table). Load-bearing
  addresses, the RAM map, the sector-skew table and the I/O-vector table are captured in
  §9-§10; the remaining object bytes and routine bodies are not transcribed.
- A few reverse/blank scan pages (e.g. show-through of ii, 3-3/3-4, 4-9, 5-15/5-16, A-3/A-4,
  C-1) are illegible mirror images of the facing page and add no content.
