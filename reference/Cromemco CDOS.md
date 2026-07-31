# Cromemco CDOS Operating System

Source: [Cromemco CDOS 0236 023-0036 198106.pdf](#) (*CDOS Operating System Instruction Manual*,
Part No. 023-0036, © 1978, 1981 Cromemco, Inc., June 1981 printing), 254 pp, **scanned, no text
layer, but reads cleanly as page images** (the file is RC4-encrypted against printing/editing but
not against reading).

**CDOS** is Cromemco's own operating system for its S-100 microcomputers (System Two, System
Three, Z-2H) — a **CP/M 1.3 work-alike**, licensed from Digital Research for the CP/M data
structures and user interface, upwards-compatible so that "programs written for CP/M (versions up
to and including 1.3) will run without modification under CDOS" (p.1-2). CDOS is *not* something
this simulator emulates directly; it is software the emulated Z80 boots and runs, the same
relationship the SDOS/SBC-100 pairing already documents (see
[`SD Systems SDOS.md`](SD%20Systems%20SDOS.md)) — this file is CDOS's counterpart for the
[Cromemco 4FDC/16FDC/64FDC](Cromemco%204FDC%2016FDC%2064FDC%20Floppy%20Controllers.md) family and
the **RDOS** boot PROM those cards carry. This is a distilled emulation reference: the tutorial
prose, the diskette-handling advice, and CDOSGEN's interactive Q&A are condensed to what a machine
or board emulation must reproduce for CDOS to boot and run — memory layout, disk geometry, the
boot sequence, the file/directory structure, and the system-call interface. Kit-assembly and
marketing material is omitted; Chapter 3 (CDOSGEN) and the source-listing appendices (C, D) are
summarized rather than transcribed in full since they configure/rebuild CDOS rather than describe
fixed hardware-visible behavior.

---

## 1. RDOS, CDOS, and the boot sequence

**RDOS** ("Resident Disk Operating System") is the onboard boot monitor that lives in the FDC
board's own ROM — not part of CDOS, and not on disk. CDOS itself is loaded from the disk's System
Area by RDOS's bootstrap routine (p.16); "CDOS is loaded from the System Area of the disk into
memory by a bootstrap routine" and "all of CDOS is contained in the file CDOS.COM" (pp.16, 35).

**Cold boot procedure** (§4.1.1, p.35):

1. Power on the computer, terminal, and any external disk drive.
2. Place the CDOS system diskette in disk drive A.
3. **Press RETURN up to four times** to set the console baud rate. (Not needed on a Cromemco 3102
   terminal, which sends these automatically.)
   - If **switch 3 of the disk controller board is ON**, CDOS **automatically boots** at this
     point — no further input needed.
   - If **switch 3 is OFF**, RDOS instead answers with a **`;` prompt**, to which the user must
     respond **`b` + RETURN** to boot CDOS.

This is called a **cold bootstrap**: "reading CDOS and the I/O routines from disk" (p.35). The
manual explicitly recommends inserting disks *after* power-up and removing them *before*
power-down, but says disks may stay in the drives across a **RESET** (p.35).

**RESET switch** (§4.5, p.41): pressing/turning RESET is a hardware reset that transfers control
to the power-on jump address selected on the ZPU (CPU) card. With the ZPU and disk-controller
switches at their suggested settings, a RESET hands control to RDOS and, if disk-controller switch
3 is ON, RDOS immediately re-cold-boots CDOS. **RESET during a disk write will corrupt the file
being written** — "Pressing reset while the disk is being written to will result in a file that
cannot be read" (p.13). On a non-3102 terminal, RETURN must again be pressed several times after a
RESET to reestablish the baud rate.

**Warm start**: every time a command (COM file) is about to execute, CDOS "logs off all drives by
clearing the bitmaps" — this is called a warm start, distinct from the cold bootstrap. After a warm
start, the next disk access on a drive rebuilds its bitmap (p.36). `STAT` can report whether a disk
was written to improperly (relevant to a torn-write / RESET-during-write scenario, p.36).

*(Switch 3's exact position/label, the rest of the disk-controller and ZPU switch table, and the
"`;`" RDOS-prompt wording are as read directly from the Beginner's Guide and Operation chapters,
§§1.6/4.1/4.5; Appendix B (Switch Settings) — cross-checked below — is the authoritative switch
table.)*

---

## 2. Memory layout

CDOS divides RAM into two parts (§2.1, p.15):

```
 (high) ┌───────────────────────────┐
        │ IOS  — Input/Output Sys.  │
        │ DOS  — Disk mgmt/BDOS     │  CDOS resident: ~11K-18K at the top of RAM
        │ CONPROC — command proc.   │
        ├───────────────────────────┤  "bottom of CDOS" (12-15K down from top)
        │                           │
        │        USER AREA          │  where all non-intrinsic programs run
        │                           │
  0100H ├───────────────────────────┤
        │  reserved (CDOS low mem)  │
  0000H └───────────────────────────┘
```

- **CDOS occupies `0000`–`0100H`** (low memory) **plus roughly the top 11K–18K of RAM** (p.15).
- **User Area**: `0100H` up to the bottom of CDOS — "usually about 48K" on a 64K system (p.15).
  The bottom of CDOS always lands on a `100H`-byte page boundary (p.28, CDOSGEN).
- **High memory** (CDOS resident) holds: basic I/O for console/printer/punch/reader and the disk
  I/O drivers; file management (create/open/read/write, directory, console-line editing); and the
  **intrinsic commands** (ATTR, DIR, ERA, REN, SAVE, TYPE) — intrinsics run *in* CDOS and so never
  alter the User Area (p.16, p.47).
- **External functions** (utilities, user COM files) load into and run in the User Area, starting
  at `0100H`, with the rest of the command line passed to them as control information and
  execution started at `0100H` (p.16, p.40).

**Low-memory (page-zero) map** (§2.1, p.17 — CP/M-compatible):

| Range | Use |
|---|---|
| `00H`–`02H` | System **warm start** vector |
| `03H` | I/O byte |
| `05H`–`07H` | **System call vector** for user requests (`CALL 5` entry — the CDOS/CP/M BDOS call) |
| `08H` | Running-OS discriminator: **`FFH`** = under CDOS, **`C3H`** = under the Cromix operating system |
| `30H`–`32H` | Breakpoints for DEBUG |
| `38H`–`3AH` | Jump to **Invalid jump** message |
| `40H`–`5BH` | Reserved for system |
| `5CH`–`7BH` | Standard user **File Control Block (FCB)** |
| `80H`–`FFH` | Standard user **I/O buffer** (disk DMA buffer, and the command-line buffer) |

CDOS supports **up to 64 KB**; CDOSGEN accepts a memory-size spec as a hex address `3FFF`–`FFFF`
or a decimal `16`–`64` (KB), naming the *highest address available to CDOS* (p.27-28) — e.g. `7FFF`
or `32` for a 32K system, `BFFF`/`48` for 48K, `FFFF`/`64` for 64K.

---

## 3. Disk organization

Every CDOS disk has two areas (§2.2, p.17):

- **System Area** — outer tracks; holds the CDOS boot image; accessible only via the `WRTSYS`
  utility or by writing a boot file with `CDOSGEN`; **not listed by `DIR`**.
- **File Area** — everything else: the directory and user files; starts at the beginning of the
  track following the System Area. CDOS walks a disk by **alternating sides/surfaces as it
  increases cylinder number**, so "next track" can mean the other surface of the *same* cylinder
  (p.19).

### 3.1 Disk geometry (§2.2.1, p.18)

| Disk | Cylinders | Surfaces | Sectors/track | Sector size |
|---|---|---|---|---|
| 8″ SD | 77 (0–4CH) | 2 | 26 (1–1AH) | 128 bytes |
| 8″ DD | 77 (0–4CH) | 2 | 16 (1–10H) | 512 bytes |
| 5″ SD | 40 (0–27H) | 2 | 18 (1–12H) | 128 bytes |
| 5″ DD | 40 (0–27H) | 2 | 10 (1–0AH) | 512 bytes |
| Hard | 350 (0–15DH) | 3 | 20 (1–14H) | 512 bytes |

**Track 0, cylinder 0, side 0 of every floppy is always initialized single-density,
128-byte-sector** by `INIT`, regardless of the disk's overall density — this lets 16FDC/4FDC RDOS
boot it (p.18). Hard disks reserve **four additional cylinders as spare/alternate tracks** for
tracks that develop hard errors (p.18).

### 3.2 Disk-type specifier (self-describing media, §2.2.2, p.18)

CDOS identifies a disk's type from a **disk type specifier** stored at **bytes 121–128 of the
first sector** — sector 1, cylinder 0, side 0 on floppies; sector 0, cylinder 0, surface 0 on hard
disks. Four two-byte ASCII fields:

| Bytes | Values | Meaning |
|---|---|---|
| 121–122 | `LG` / `SM` / `HD` | large (8″) floppy / small (5¼″) floppy / hard disk |
| 123–124 | `SS` / `DS` / `11` | single-sided / double-sided floppy / 11-Mbyte hard disk |
| 125–126 | `SD` / `DD` | single density / double density |
| 127–128 | (reserved) | — |

The System Area size is **always at least 6.5K**, spanning 1–3 tracks; on double-density floppies
part of the system area can be on the single-density cylinder-0/side-0 track and part on a
double-density track (cylinder 0, track 1). Per-type System Area / File Area start points
(p.19):

| Disk type | System Area (cyl,side) | File Area starts at |
|---|---|---|
| LG SS SD | c0,s0; c1,s0 | c2,s0 |
| LG SS DD | c0,s0; c1,s0 | c2,s0 |
| LG DD SD | c0,s0; c0,s1 | c1,s0 |
| LG DD DD | c0,s0; c0,s1 | c1,s0 |
| SM SS SD | c0,s0; c1,s0; c2,s0 | c3,s0 |
| SM SS DD | c0,s0; c1,s0 | c2,s0 |
| SM DD SD | c0,s0; c0,s1; c1,s0 | c1,s0 |
| SM DD DD | c0,s0; c0,s1 | c1,s0 |
| HD 11 | c0,s0 | c0,s1 |

("SS"/"DS" here read as "SS"/"DD" combinations per the source table's own column headers — see
p.19 for the primary source; DD = double-sided in this specific table, not to be confused with
double-*density* used elsewhere.)

Approximate File Area capacity by disk type (§2.2, p.17):

| Disk | System Area (tracks) | File Area (approx.) |
|---|---|---|
| 5″ SS SD | 3 | 81K |
| 5″ DS SD | 3 | 171K |
| 5″ SS DD | 2 | 188K |
| 5″ DS DD | 2 | 386K |
| 8″ SS SD | 2 | 241K |
| 8″ DS SD | 2 | 490K |
| 8″ SS DD | 2 | 596K |
| 8″ DS DD | 2 | 1,208K |
| Hard-11 | 1 | 10,490K |

### 3.3 Write-protect (§2.2.3, p.20)

- **8″ diskettes**: write-protected by a notch on the lower-right of the jacket; covering it with a
  silver sticker or masking tape *enables* writing (the opposite convention from the 5¼″ format).
- **5¼″ diskettes**: write-protected by *placing* a silver sticker over the notch; removing the
  sticker enables writing.
- Files may additionally be software write/read/erase-protected with `ATTR` (§6.1.1) — "a software
  write-protect only" (p.20).

### 3.4 Device names (§2.3.1, p.23)

| Device | Name | Numbers |
|---|---|---|
| Console | `CON:` | 0–7 |
| Card reader | `RDR:` | 0–3 |
| Paper-tape punch | `PUN:` | 0,1 |
| Line printer | `PRT:` | 0–3 |
| Dummy device (bit bucket/EOF) | `DUM:` | — |

Only console, printer, and disk are active as shipped; the paper-tape reader/punch share the
console's port assignments unless the I/O drivers are edited (p.22). Up to **4 floppy + 7 hard
drives (8 total, A–H)** may be attached to one Cromemco floppy controller + WDI hard-disk
controller, drive A always a floppy (p.22, p.27).

### 3.5 Filenames (§2.3.2.1, p.23-24)

`[X:]filename[.ext]` — drive letter A–H, filename up to 8 printable-ASCII characters (excluding
`$ * ? = / . , :` and space), optional 1–3 character extension. Lower-case is accepted but folded
to upper case by every system function (p.24). Standard extensions (p.25): `BAK` editor backup,
`BAS`/`LIS`/`SAV` BASIC source/listed/saved, `CMD` batch command file, `COB` COBOL source, `COM`
executable command, `FOR` FORTRAN source, `HEX` Intel-hex object (8080), `PRN` listing, `REL`
relocatable object, `SYS` system image, `TXT` text-formatter input, `Z80` assembler source. `*`
and `?` (and, in `XFER`/`STAT` only, `[...]` bracket sets) are ambiguous-reference wildcards
(§2.3.2.2, p.25-26).

---

## 4. CDOSGEN (system generator)

`CDOSGEN` builds a CDOS image tailored to the target's memory size and disk-drive configuration
(Chapter 3, p.27). Interactively prompts for: memory size (§3.2.1); per-drive type
`S`=5¼″/`L`=8″/`H`=hard/`N`=none/`E`=end, plus fast/slow seek and single/double sided and
single/dual density for floppies (§3.2.2, pp.27-29) — drive A is always floppy, B–D floppy or
hard, E–H hard only; console **function-key decoding** (Standard/None/User-defined/File-defined,
§3.2.3) — 20 programmable function keys, each a command string terminated by ESC (or CNTRL-Z),
supported on Cromemco 3101/3102 terminals; the output **command filename** (default `CDOS.COM` on
the current drive — **only a file literally named `CDOS.COM` will auto-boot from RDOS**, though a
differently-named `.COM` can still be booted "from another" already-running CDOS, §3.2.5, p.33);
and whether to **write the boot file** to the target disk's System Area (§3.2.6, p.33) — writing
it there does *not* show in `DIR` and is independent of whether the File Area is full. CDOSGEN also
reports the resulting starting/ending addresses of CDOS and its I/O drivers, and the boot loader's
size (§3.2.4, p.32-33).

---

## 5. CDOS Operation

### 5.1 Warm start and drive selection (§4.1.2, p.36)

`X:` + RETURN changes the current drive to `X`. Referencing a filename with no drive specifier
searches the current drive, then — for **COM files only** — falls back to the **master drive**
(default **A**) if not found there (p.24-25). Every program invocation first performs a warm start
(bitmaps cleared/all drives logged off); the next access to a drive rebuilds its bitmap.

### 5.2 Control characters (§4.2, pp.36-38)

Console (buffered-input mode): `^E` CR+LF without terminating; Backspace/Underscore/RUBOUT/DELETE
delete-last-char (no echo); RETURN/`^M` terminate the line; `^R` retype the line; `PAUSE`/`^S`
(3102 only for the PAUSE key) pause device I/O, any key resumes; `^U` delete current line
(hardcopy terminals); `CE`/`^V` (3102 only) erase current line; `^X` delete-with-echo (hardcopy,
emits `\\\\\\` framing). Printer: `^L` formfeed; `^N` (3703 printer only) double-width for that
line; `PRINT`/`^P` (3102 only) toggle echo console output to the printer too (auto-selects the
3703 driver); `^T` turn off printer output (program-issued only, no console effect); `^W` send
output to printer as well as console (program-issued only).

### 5.3 Automatic startup — `STARTUP.CMD` (§4.3, pp.38-40)

If a file named exactly **`STARTUP.CMD`** exists on drive A, CDOS's `@` (Batch) mechanism runs it
automatically right after boot or CDOS re-entry — the classic "power on and it just runs the
application" path. While `STARTUP.CMD` is running, **RETURN's line-terminate function is
disabled**, deliberately, so a novice can't interrupt it; normal RETURN behavior resumes once the
file finishes. Requires `@.COM` present on disk A.

### 5.4 Command structure (§4.4, p.40)

`[drive:]command[/options][args]`. Unrecognized command name → CDOS searches for `<name>.COM` on
the current disk, then the master disk; if found it's loaded at `0100H` and started there with the
rest of the line passed as its command tail; not found → error message. All letters are folded to
upper case on entry. Options are `/`-prefixed; commas/blanks/`=` delimit filenames; assignment
commands are generally `dest-file-ref=source-file-ref`.

---

## 6. CDOS I/O drivers (Chapter 5, pp.43-46)

CDOS ships with a driver for **Cromemco dot-matrix printers**. A **3355A** typewriter-quality
printer needs its own driver (`3355A.COM`, typed once after boot; stays resident until reboot,
p.43). Drivers live in one assembler source, `DRIVERS.Z-80` (Cromemco Z-80 Macro Assembler
diskette FDA-L/FDA-S), edited and reassembled to add/remove device support, remove code to shrink
CDOS's memory footprint, or make port-address/terminal-model changes via `EQU`s at the top of the
file (e.g. `C3102`/`C3101` terminal-model flags, `FUN.KEYS`). Rebuild recipe (p.44-45):
`XFER/V` a copy → edit with `SCREEN` → `ASMB … HEX=0` and `ASMB … HEX=100` (two separate origins
are required — `0` and `100H` — because the drivers get relocated into two different places
inside the final CDOS image) → rename each `.HEX` output → `CDOSGEN <hex0> <hex100>` to fold the
rebuilt drivers into a fresh `CDOS.COM`, then **reboot** to pick it up. CDOS's **I/O byte** (page
zero, `03H`) selects among multiple like devices at runtime (also settable live via
`STAT dev:=n`): bits 0-2 CONsole 0-7, bits 3-4 ReaDeRs 0-3, bit 5 PUNch 0-1, bits 6-7 PRinTer 0-3
(p.46) — e.g. `STAT PRT:=0` (or `PRT:=PAR:`) selects the parallel dot-matrix driver, `PRT:=1`
(`SER:`) a serial one, `PRT:=2` (`TYP:`) the 3355A, when both `C3703` and `S.PRINTER` were built in
(p.45-46).

---

## 7. CDOS Commands (Chapter 6)

### 7.1 Intrinsic commands (§6.1) — run in CDOS's own resident memory, never touch the User Area

| Command | Purpose |
|---|---|
| **ATTR** (or **ATRIB**) | `ATTR file-ref [+][p...]` — set/clear file-access attributes: **E** erase-protect, **R** read-protect, **W** write-protect, **S** system file, **U** user file. No `+` *replaces* the existing attribute set; `+` *adds* to it. Bare `ATTR file-ref` (no letters) clears all user-assignable attributes. Software-only protection (§6.1.1, pp.48-50). |
| **DIR** (Directory) | `DIR [y: \| file-ref]` — lists filenames, sizes (K), and ATTR flags on a drive (default: current), or matching an ambiguous file-ref; final summary line gives files/entries/K-displayed/K-left. Alphabetized listing: use `STAT/A` (with sizes) or `STAT/N` (names only) (§6.1.2, pp.51-52). |
| **ERA** (Erase) | `ERA file-ref` — delete a file; accepts a drive specifier (§6.1.3). |
| **REN** (Rename) | `REN newname=oldname` — renames without moving data or touching contents (§6.1.4). |
| **SAVE** | `SAVE file-ref n` — snapshots *n* 256-byte pages of the User Area (starting at `0100H`) to a disk file, typically to turn a just-linked FORTRAN/COBOL/ASM program into a `.COM` file before running it. *n* = the linker's third bracketed exit value, or convert the high byte of the highest address to save to decimal (§6.1.5, p.57). |
| **TYPE** | `TYPE file-ref` — dumps an ASCII text file to the console; undefined result on non-text files. Console control chars stay active: **Ctrl-S** pauses (any key resumes), **Ctrl-P** also echoes to the printer, any other key aborts; an embedded **Ctrl-W**/**Ctrl-T** in the file itself starts/stops printer echo mid-listing (§6.1.6, p.58). |

### 7.2 Utility programs (§6.2) — separate `.COM` files on the current or master disk; invoke without the `.COM` extension

| Utility | Purpose |
|---|---|
| **@** (Batch) | Runs a sequence of commands unattended. Interactive: type `@`, wait for `!`, enter commands one per RETURN-terminated line, blank line ends and starts execution. Or from a pre-written `name.CMD` file (one command/line) via `@ name`. `STARTUP.CMD` on drive A auto-runs this way at boot (§6.2.1, §4.3). |
| **DUMP** | Displays a file's raw bytes (for non-ASCII/binary files, complementing TYPE) (§6.2.2). |
| **INIT** (Initialize) | `[x:]INIT` — no command-line args, purely interactive Q&A: drive letter (validated against CDOSGEN's own config); single/double-sided; single/double density; first/last cylinder (RETURN twice = whole disk; a single track or any range is legal); surface(s) (default = all). **Destroys existing data** — records track/sector/surface info the controller needs to address the disk. **All 8″ Cromemco-supplied diskettes ship pre-initialized double-sided** per IBM 3740 and should not be reinitialized new; blank 5¼″ disks always need it. **Switch 4 on the 16FDC/4FDC must be OFF** for INIT to run at all (the INITIALIZATION INHIBIT switch, §10 below); **double-density init is impossible on a 4FDC** (single-density-only controller). Hard-disk INIT additionally prompts for **alternate-track declaration** — usual answer is No/No; if Yes, the hard-error track is auto-numbered 1-12, input is strictly validated, and **Ctrl-C abort is disabled** during this phase because the in-progress table is RAM-only until written back — the manual explicitly warns against resetting the machine here. Factory-declared alternate tracks must never be removed (voids the drive warranty). After INIT, label the disk with `STAT/L` (§6.2.3-6.2.3.1, pp.64-66). |
| **STATus** | Bare `STAT` prints a full status block: system memory (OS version/total/OS-size/user-size), device config (CON:/PRT:/RDR:/PUN: selections), disk memory (label, date, total/used/free KB), disk config (master drive, **cluster size**, **sector size**, total/used/free **directory entries**), and drive/diskette sidedness+density. Options (`/`-prefixed, combinable, e.g. `STAT/DT`): **/A** alphabetized directory w/ sizes+attrs; **/B** 4-line brief summary; **/D** set date (`(mm/dd/yy):` prompt, stored value readable via syscall 144, resets to `00/00/00` on reboot); **/E** interactive erase (Y/N query per file, alphabetical); **/L** set/change a disk **label** (name/date/**directory-entry-count** — default 64 most floppies, **128** double-sided 8″, **512** hard disk, range 64-512 rounded down to a multiple of 4; **destroys part of the existing directory**, confirmed Y/N — a real reformat, not just metadata); **/M** select a secondary master drive to search; **/N** alphabetized names only (fastest); **/S** force the status printout alongside other options; **/T** set time-of-day (readable via syscall 146; on a 3102 also sets the terminal's own clock); **/Z** used with `/E` for a no-query batch-mode erase-all. `/B`, `/L`, `/S` also run a **directory validation pass** (cross-linked files / unallocated clusters, from mid-program disk swaps). Live device reassignment: `STAT dev:=n` (`CON:` 0-7, `RDR:` 0-3, `PUN:` 0-1, `PRT:` 0-3) writes the page-zero **I/O byte** (§6.2.4, pp.67-75; also §5, p.46). |
| **WRTSYS** | `[x:]WRTSYS[/s] {d: \| file-ref-1} = {f: \| file-ref-2}` — writes/reads the CDOS resident image in a disk's hidden **System Area** (the same area CDOSGEN's boot-file step writes; not the File Area `XFER` touches). A bare drive letter on either side means the system area of that disk; a filename must carry extension **`.SYS`** and then reads/writes a normal user file instead. `/s` = single-drive mode, prompts for a disk swap. **Does not affect the currently-resident CDOS in memory** — a reboot is required to actually switch OS. **Preserves the disk's 8-byte label** across the copy, so it works between differently-geometried disks (§6.2.5, pp.75-78). |
| **XFER** (Transfer) | Repeat mode `[x:]XFER<RETURN>` prompts `!` repeatedly (RETURN exits); one-time mode `[x:]XFER[/s1/s2...] {d: \| file-ref-1} = file-ref-2[,file-ref-3...]`. Switches: **A** ASCII transfer (strips EOF markers from all-but-last source when concatenating, prints line count); **C** compare without transfer (driven by the shorter/source file's length — a length mismatch where the shorter file's content matches in full still counts as "same"); **F** filter illegal ASCII chars; **R** transfer even a read-protected file; **S** strip rubouts/nulls; **T** expand tabs to spaces (recommended when sending text to the printer); **V** verify after transfer — on a wildcard transfer, a verification failure **aborts the remaining batch**; **Z** suppress the size-statistics printout. Destination `d:` alone preserves each source's name+extension; a device specifier (`PRT:`) is a valid destination too. **Will not transfer random-access/ISAM files** — those need a custom same-language program. **File-area only** — system-area images are `WRTSYS`'s job (§6.2.6, pp.78-81). |

### 7.3 Editors (§6.3)

Two: the **Cromemco Screen Editor** and the **Cromemco Text Editor** (§6.3.1-6.3.2, pp.81-82) —
full-screen vs. line-oriented; used e.g. by the driver-rebuild recipe above (`SCREEN file`).

---

## 8. CDOS Programmer's Guide (Chapter 7) — system-call interface

*(This section is the CP/M-workalike contract a guest program — and hence a booted OS image — uses
to talk to CDOS; it is the load-bearing part for anyone verifying that CDOS software runs correctly
against an emulated Z80 + FDC.)*

A program issues a system call by loading the function number into **C**, any parameters typically
into **DE**/**E**, and executing **`CALL 5`** (the page-zero system-call vector installed at
`0005H`). All Z-80 registers survive a call except **F** (flags) and whichever registers the call
is documented to return values in; the primed register set and `IX`/`IY` are safe for a program's
own use across a call. **Disk I/O disables interrupts**, and a system call may too — a program
using its own interrupts must save/restore registers around one (§7.6, p.91). CDOS implements
**all 27 CP/M 1.3 system calls**, numbered identically to CP/M's own BDOS, plus a long list of
**CDOS-specific extensions** starting at 128 (`80H`) (Introduction, p.2; §7.1, p.83).

### 8.1 File Control Block (FCB) — 33 bytes, in RAM (§7.3, p.87)

| Byte | Contents |
|---|---|
| 0 | disk descriptor before open (0=current, 1-8=drives A-H, disk# in bits 0-3); **attribute byte after open** (bit 7 write-protect, 6 read-protect, 5 system file, 4 user file) |
| 1-8 | filename, space-padded |
| 9-11 | filename extension, space-padded |
| 12 | file entry/extent number (initially 0; +1 per new 16K entry) |
| 13-14 | reserved |
| 15 | record count in this entry |
| 16-31 | cluster allocation map (clusters allocated to this entry) |
| 32 | next record to read/write, 0-127 |

### 8.2 Directory entry — on disk (§7.4, p.88)

Structurally similar to an FCB. Byte 0 is a "special" bit field: bit 7 erase-protected, 6
write-protected, 5 read-protected, 4 system-file attribute, 3 user-file attribute, **2 extended
file format** (hard disk only — cluster pointers in this entry point at a 2K *cluster of
directory entries*, not file data, needed once a file exceeds 16K/one extent), byte value `E5H`
= erased-file marker, byte value `81H` = **this entry is the disk label**. Bytes 1-8/9-11 are
filename/extension; byte 12 extent number; byte 14 record count in the *last* extent (hard disk
only); byte 15 record count; bytes 16-31 cluster numbers (one byte/cluster on floppies, range
0-255; two bytes/cluster on hard disks, range 0-65535 — per the disk label's own pointer-width
flag).

### 8.3 Disk label — the first directory entry on a labeled disk (§7.5, p.90)

The first directory entry is structurally distinct: label flag `81H` at byte 0; label (volume)
name at bytes 1-8; date labeled at bytes 9-11 (month/day/year-since-1900); **records per
cluster** at byte 12 (8 = 1K cluster, `10H`=16 = 2K cluster — a CDOS record is always 128
bytes); flags at byte 13 (bit 7 = 2-byte cluster pointers, bit 6 = extended file format present
— hard disk only, bit 5 = bitmap stored on disk — hard disk only); byte 14 reserved; byte 15
record count of the directory itself; bytes 16-31 cluster numbers of the directory. The extended
file format bit here tells CDOS whether it must consult individual directory entries to detect
files over 16K on this disk.

### 8.4 System calls (§7.7, pp.92-157)

Calls 0-27 are CP/M-1.3-identical. Calls 128+ (`80H`+) are CDOS-only, no CP/M equivalent — the
interesting differentiators for an emulator. The manual notes, call by call, whether each is
**implemented in the Cromix CDOS Simulator** (a CDOS-compatibility shim under Cromemco's
separate, Unix-like Cromix OS) — a "no"/"ignored" mark below is a hardware/CDOS-native concept
(disk deselection, disk-space bookkeeping, master-drive lookup) with no meaning inside that
simulator; this column is omitted below except where the manual explicitly says a call is *not*
or is *ignored* by the simulator.

| # (hex) | Function | Entry | Return |
|---:|---|---|---|
| 0 (00) | Program abort | — | — |
| 1 (01) | Read console (with echo) | — | A = char, parity bit reset |
| 2 (02) | Write console | E = char | — |
| 3 (03) | Read reader | — | A = char (parity kept) |
| 4 (04) | Write punch | E = char | — |
| 5 (05) | Write list (printer) | E = char | — |
| 6 | not in use | | |
| 7 (07) | Get I/O byte | — | A = I/O byte |
| 8 (08) | Set I/O byte | E = I/O byte | — |
| 9 (09) | Print buffered (`$`-terminated) line | DE = buffer addr | — |
| 10 (0A) | Input buffered line | DE = buffer addr (byte 0 = max len) | byte 1 = actual len |
| 11 (0B) | Test console ready | — | A = FFH ready / 0 not |
| 12 (0C) | Deselect current disk | — | — *(ignored under the Cromix simulator)* |
| 13 (0D) | Reset CDOS and select drive A | — | — |
| 14 (0E) | Select current disk | E = drive 0-7 | — |
| 15 (0F) | Open disk file | DE = FCB addr | A = directory block / FFH not found |
| 16 (10) | Close disk file | DE = FCB addr | A = dir block / FFH not found |
| 17 (11) | Search directory for filename | DE = FCB addr | A = directory block / FFH not found |
| 18 (12) | Find next directory entry | DE = FCB addr | A = directory block / FFH not found |
| 19 (13) | Delete file | DE = FCB addr | A = # entries deleted |
| 20 (14) | Read next record | DE = FCB addr | A = 0 ok / 1 EOF / 2 unwritten cluster |
| 21 (15) | Write next record | DE = FCB addr | A = 0 ok / 1 entry err / 2 disk full / FFH dir full |
| 22 (16) | Create file | DE = FCB addr | A = dir block / FFH dir full |
| 23 (17) | Rename file | DE = FCB addr | A = # entries renamed |
| 24 (18) | Get disk login vector | — | A = bitmask of logged-in disks |
| 25 (19) | Current disk | — | A = drive # |
| 26 (1A) | Set disk (DMA) buffer | DE = buffer addr | — |
| 27 (1B) | Disk cluster allocation map | — | BC = bitmap addr, DE = #clusters, HL = last CDOS addr, A = records/cluster |
| 128 (80) | Read console, no echo | — | A = char |
| 129 (81) | Get user register pointer | — | BC = ptr to user register block |
| 130 (82) | Set user ^C-abort handler | DE = handler addr (0 = reset, -1 = disable) | — |
| 131 (83) | Read logical record (no FCB) | B = drive# (0=current,1-8=A-H; bit6=1: record# in HLDE, bit6=0: record# in DE; bit7=1: interleaved read) | A = 0 ok/1 I/O err/2 illegal req/3 illegal block |
| 132 (84) | Write logical record (no FCB) | same entry shape as 131 | A = 0 ok/1 I/O err/2 illegal req/3 illegal block |
| 133 | not in use | | |
| 134 (86) | Format name to file control block | HL = string addr, DE = FCB addr | HL = terminator addr, DE = FCB addr |
| 135 (87) | Update directory entry | DE = FCB addr | — |
| 136 (88) | Link to program | DE = FCB addr | A = -1(FFH) if error; else executes loaded program at 100H |
| 137 (89) | Multiply integers | DE = factor 1, HL = factor 2 | DE = product |
| 138 (8A) | Divide integers | HL = dividend, DE = divisor | HL = quotient, DE = remainder |
| 139 (8B) | Home drive | B = drive number | — |
| 140 (8C) | Eject diskette | E = drive number | — |
| 141 (8D) | Get version of operating system | — | A = operating system, B = version#, C = release# |
| 142 (8E) | Set special CRT function | D = column addr/special function, E = row addr/0 | — |
| 143 (8F) | Set date | B = day, D = month, E = year-1900 | — |
| 144 (90) | Read date | — | A = day, B = month, C = year-1900 |
| 145 (91) | Set time of day | B = seconds, D = minutes, E = hours (24hr) | — |
| 146 (92) | Read time of day | — | A = seconds, B = minutes, C = hours (24hr) |
| 147 (93) | Set program return code | A = return code for next program | A = none |
| 148 (94) | Set file attributes | DE = FCB addr, B = new attributes | — |
| 149 (95) | Read disk label | DE = FCB addr | — |
| 150 (96) | Turn motors off | — | — |
| 151 (97) | Set bottom of CDOS in RAM | E = high byte of new bottom address | — |
| 152 (98) | Read current record | DE = FCB addr | A = 0 ok/1 EOF/2 unwritten records |
| 153 (99) | Write current record | DE = FCB addr | A = 0 ok/1 entry err/2 disk full/FFH dir full |
| 154 (9A) | Check if allocated | DE = FCB addr | A = 0 allocated / -1 not allocated |
| 155 | not in use | | |
| 156 (9C) | List directory | DE = FCB addr | — |
| 157 (9D) | Set options | D = desired option, E = mask (bit0 ^P flag, bit1 read-after-write, bit2 ESC-as-CR, bit3 no-echo-CR, bit6 no-echo) | A = old options |
| 158 (9E) | Delete extents (reduce file size) | DE = FCB addr | A = 0 not found / 1 found and erased — *(not implemented under the Cromix simulator)* |
| 159 (9F) | Get master drive | — | A = master drive#, B = last drive used by `@` batch — *(not implemented under the Cromix simulator)* |

(Summary table pp.153-157, captioned "implemented in CDOS version 02.17"; individual per-call
descriptions pp.92-152 give the prose behind ambiguous entries above, e.g. call 130's exact
semantics or call 142's CRT addressing scheme.) Function numbers **6**, **133**, and **155** are
reserved/retired gaps, not omissions.

Behavioral notes worth an emulator's attention:
- **Warm boot** (jump to `0000H`, same effect as call 0) is the normal way a `.COM` program exits
  back to the CDOS prompt.
- **Calls 9/10** (print/input buffered line) interpret `^P` (toggle console+printer tee), `^W`
  (force tee on) and `^T` (force tee off) as they stream — the same toggle the console driver
  itself honors on typed input (calls 1/2), so a program driving these calls gets identical
  editing/tee behavior to a human typing at the prompt.
- **Calls 17/18** (search directory) return matches to **both erased and live** entries — the
  caller must check the attribute byte for `E5H` itself; ASCII `?` (`3FH`) in the FCB's filename
  wildcards a single character, and `3FH` in the drive-descriptor byte means "current drive."
- **Call 21/153** (write next/current record) closing rule: a file just written to **must** be
  closed (call 16) or its last extent's cluster map is never flushed to the directory and becomes
  unreadable — the same must-close-after-write rule CP/M itself has.
- **Call 27** (disk cluster allocation map) hands back a live pointer into CDOS's own in-RAM
  bitmap, not a copy — the primitive `STAT`'s directory-validation modes (`/B`, `/L`, `/S`) use to
  detect cross-linked files after an improper disk swap mid-program.

---

## 9. Error messages (Chapter 8, pp.159-168)

### 9.1 Floppy disk access errors (§8.1)

Format: `mode Error, Drive x, Cylinder cc, Sector ss, Status=ee` — `mode` is Seek / Read / Write
/ Home (seeking track 0) / Read-after-Write (the CRC check following a read or read-after-write
verify); `x` is the drive letter A-H; `cc`/`ss` are hex; `ee` is the raw 8-bit controller status
byte, decoded bit-by-bit per mode (bit 7 not-ready in every mode; write-protect(6)/head-engaged
(5)/track-0(2)/index(1)/busy(0) for Seek/Home; record-type(6,5)/record-not-found(4)/CRC(3)/
lost-data(2)/data-request(1)/busy(0) for Read/R-A-W; write-protect(6)/write-fault(5)/
record-not-found(4)/CRC(3)/lost-data(2)/data-request(1)/busy(0) for Write). Bits marked with an
asterisk in the manual's own per-mode table are conditions *present but not the cause* (e.g.
"head engaged" during a Seek is normal, not itself an error). After **ten silent retries** the
error message appears, with four possible operator responses: **R** retry, **I** ignore (the
function is abandoned, no error code returned to the caller), **C** continue (function abandoned,
an error code *is* returned to the caller), **^C** abort to the CDOS prompt.

### 9.2 Hard disk errors (§8.2)

Format: `mode Drive d Cylinder cc Surface hh Sector ss Status ffss` — `mode` is Read/Write/
Read-after-Write/Home/Seek error; `ffss`'s first two hex digits are a **fatal** error code, the
second two a **system** error code.

Fatal codes `00`-`0D`: failed seek & read header during R/W; seek timeout; fault during seek;
failed to seek to correct track; header CRC bad; rezero timeout; fault after rezeroing; drive
not ready; write fault; verify-after-write failed; read fault; read CRC bad; sector not found on
track; surface write-protected.

System codes `00`-`06`: no acknowledge from drive; drive stuck busy (acknowledge stuck low);
rezero timeout; drive-reported fault; read CRC bad; header off the disk doesn't match expected
header; verify-after-write failed.

### 9.3 Notable system error messages (§8.3, condensed)

`Bad directory block dddH` (directory block overwritten with bad data); `Bad disk block
overwritten` (a `C` response was given to a SAVE-time disk error); `Cannot read double density
diskettes` / `Cannot read double sided diskettes` (CDOS was configured, via CDOSGEN, for a
narrower drive capability than the media inserted); `CDOS.COM not found` (no bootable CDOS.COM
on current or master drive); `Drive x write-protected` (hard disk key-locked) / `Diskette in
drive x write-protected` (missing write-enable sticker on 8″, or a present write-protect sticker
on 5¼″); `Drive not found` (drive letter outside the current CDOSGEN configuration); `Drive not
ready` (no diskette in the drive); `File already exists` (rename target collision); `File not
found` (no matching file on current+master drive); `file-ref program too big` (a `.COM` exceeds
available User Area); `Illegal system call cccH at aaaH` (an undefined function number was
loaded into C); `Invalid jump to location xxxx` (control transferred to a nonexistent address,
or to any location holding `FFH`/RST 38H); `Logical disk error` (an access targeted a sector not
on the disk — usually a corrupt directory); `Program not found` (no matching `.COM` on current
or master disk).

## 10. Switch settings (Appendix B, pp.175-176)

*(Appendix B in the manual's own numbering; renumbered here to follow directly after the error
messages chapter.)*

**16FDC** (identical for 4FDC):

| Switch | Function | Recommended initial setting |
|---|---|---|
| 1 | **RDOS DISABLE** — ON: the boot PROM cannot be accessed at all. OFF: RDOS lives at `C000H`-`C3FFH` during startup. | OFF |
| 2 | **RDOS DISABLE AFTER BOOT** — ON: RDOS is unmapped from address space right after a CDOS boot. OFF: RDOS stays resident at `C000H` after boot. | ON |
| 3 | **BOOT ENABLE** — ON: CDOS's bootstrap runs automatically from power-on or reset. OFF: RDOS comes up instead (power-on/reset) and must be told `b`+RETURN to boot CDOS. | ON |
| 4 | **INITIALIZATION INHIBIT** — ON: diskettes cannot be initialized under software control. OFF: `INIT` is permitted. | ON or OFF |

On a **64K system, switch 2 must be ON** so RDOS is unmapped after boot and does not overlap
system RAM at `C000H`-`C3FFH`. With switch 2 ON, the **only** way back into RDOS after a CDOS
boot is to reset the machine; if switch 3 is *also* ON, that reset immediately re-boots CDOS
again — RDOS becomes effectively unreachable in that combination.

**ZPU (CPU board)**: power-on jump address set to `C000H` (RDOS's location) via DIP switches
`#12`=on, `#13`=on, `#14`=off, `#15`=off (numbering/on-off sense as printed in the manual); clock
switch set to 4 MHz.

---

## 11. Version and provenance

This manual (Part No. 023-0036) is copyright 1978 and 1981, printed June 1981. Internal evidence
of versioning, cited where it appears rather than inferred: the system-call summary table (§8.4)
is captioned "implemented in CDOS version 02.17"; a sample `STAT` display (§6.2.4) shows an
**operating-system version of 02.36**; `STAT` itself is shown at version 02.16 — i.e. the OS,
`STAT`, and the individual utility programs are versioned independently of one another. **Disk
labels are called out explicitly as "a feature of Series-2 CDOS"** (§6.2.4, the `STAT/L`
description) — meaning CDOS existed in at least two generations, and label-bearing disks (§3,
§8.3) are the later, Series-2 format; nothing in the manual states what a Series-1 disk (no
label) looks like beyond "no label present," so an emulator need only treat an absent/invalid
`81H` first-directory-byte as an unlabeled disk and fall back to the built-in default name/count
`STAT` itself uses ("Harddisk"/"Userdisk", 64 or 128 or 512 directory entries per §8.3).

---

## 12. Emulation checklist (summary of load-bearing facts)

- **Boot is a two-stage handoff**: RDOS (in FDC board ROM) reads CDOS's System Area boot image and
  jumps into it; CDOS itself never lives in ROM. Disk-controller **switch 3 ON = silent
  auto-boot**; **switch 3 OFF = RDOS's `;` prompt, and the operator must type `b` + RETURN**.
  Console baud is set by **up to 4 RETURN presses** before/after either path (not needed on a
  Cromemco 3102, which auto-sends them).
  RESET re-enters RDOS and, if switch 3 is ON, silently re-boots CDOS; RESET during a disk write
  corrupts that file.
- **Page zero is the CP/M-standard layout**: `0000` warm boot, `0005` `CALL 5` BDOS-style vector,
  `0008` = `FFH` under CDOS / `C3H` under Cromix, `005C` FCB, `0080` DMA/command-tail buffer,
  `0038` illegal-jump trap.
- **CDOS resident occupies low memory (`0000`-`0100H`) plus ~11-18K at the top of RAM**; the User
  Area (`0100H` up) is where every non-intrinsic program, including the six intrinsics'
  external-utility siblings, actually executes.
- **Disks self-describe**: bytes 121-128 of the first sector name size (`LG`/`SM`/`HD`),
  sidedness (`SS`/`DS`/`11`), and density (`SD`/`DD`). Geometry is fixed per this 5-way type
  table (8″/5″ × SD/DD, plus 11 MB hard) — see §3.1. **Track 0/side 0 of every floppy is always
  single-density/128-byte-sector**, independent of the disk's declared density, purely so RDOS can
  read it to boot.
- **CDOS is CP/M-1.3-call-compatible** (`CALL 5`, all 27 CP/M functions) with Cromemco-specific
  extensions layered on top — see §8 above and the per-call table pulled from pp.83-158.
