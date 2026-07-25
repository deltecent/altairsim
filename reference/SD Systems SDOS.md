# SD Systems SDOS (Disk Operating System)

Source: [SDOS.pdf](#) (SD Systems SDOS User's Guide), DDB200.ASM

**SDOS** is SD Systems' disk operating system for the [SBC-100 / SBC-200](SD%20Systems%20SBC-100%20%26%20SBC-200.md)
running a [VersaFloppy](SD%20Systems%20VersaFloppy.md) controller. Architecturally it is a
**CP/M work-alike**: the same low-memory layout (warm-boot at `0000H`, a `CALL 5` system-call
vector, an FCB at `005CH`, a 128-byte DMA buffer at `0080H`) and a near-identical BDOS function
numbering, with SD-specific extensions. COSMOS is the related SD Systems OS the same monitor and
BIOS can boot. This is a distilled emulation reference: the User's Guide's command tutorials and
programmer worked-examples are condensed to the memory map, the boot flow, the system-call
interface, and the disk layout — what a machine or board emulation must reproduce for SDOS
software to run. It is **not** something the simulator emulates directly; SDOS executes on the
emulated Z80. Kit/marketing material is omitted.

The disk geometry and the BIOS jump table live with the controller — see
[`SD Systems VersaFloppy.md`](SD%20Systems%20VersaFloppy.md) §6/§8 (sourced from the same
DDB200.ASM "DDBIOS" driver). The boot and disk commands live with the monitor — see
[`SD Systems Monitor.md`](SD%20Systems%20Monitor.md) §3.4/§4.

---

## 1. Memory layout

Memory is divided into an **SDOS section** (the top ~7 KB of RAM plus the reserved base page)
and a **User section** (from `0100H` up to the bottom of SDOS):

```
  (high)  ┌───────────────────────────┐
          │  IOS  – Input/Output Sys   │  top ~7K = SDOS resident
          │  DOS  – Disk Operating Sys │  (BDOS-equivalent)
          │  CONPROC – Console Proc    │  (CCP-equivalent; may be
          │            (high memory)   │   overlaid by a user program)
          ├───────────────────────────┤
          │  User & Utility programs   │  loaded from disk as needed
   0100H  ├───────────────────────────┤
          │  base page (see §2)        │
   0000H  └───────────────────────────┘
```

- **IOS** – basic I/O for console, printer, punch, reader and disk drives (the BIOS layer;
  DDBIOS supplies the disk half at `F000H`).
- **DOS** – file management: create/open/read/write/close, directory, console-line editing.
- **CONPROC** – the console processor (command interpreter): parses a line, loads the program,
  passes parameters. Internal commands live in high memory; Utility commands and user programs
  load from disk into the User Area and may overlay CONPROC.

The resident's bottom can be lowered with system call **151 (`97H`) Set Bottom of SDOS in RAM**
(new high-byte in E, always a 256-byte boundary), which re-points the `0005H` jump and installs
a second jump block at the new bottom.

---

## 2. Base page (`0000H`–`00FFH`) — reserved, CP/M-compatible

| Range | Use |
|-------|-----|
| `0000H` | System **warm restart** on user-program exit (`JP` warm boot) |
| `0005H`–`0007H` | **System-call entry** — `JP DOS`. A `CALL 5` enters the OS; the address at 6/7 is the top of the User Area. |
| `0008H`–`003FH` | Reserved for interrupt vectors; **`0038H`–`003AH` = illegal-address trap** |
| `0040H`–`005BH` | Reserved for the system (the DDBIOS disk parameters — transfer address, unit, sector, track, record count, error mask/status — live at `0040H`–`004FH` and are shared with the monitor's `R`/`W` commands) |
| `005CH`–`007FH` | Standard user **File Control Block (FCB)** |
| `0080H`–`00FFH` | Standard user **I/O buffer** (128-byte DMA buffer; the boot sector is read here) |

---

## 3. System-call interface

A program makes a system call by **loading the function number into register C**, loading any
entry parameters (usually `DE` for an address or `E` for a byte), and executing `CALL 5`. Results
return in `A` (and `BC`/`DE` for some calls). The numbering matches CP/M's BDOS:

| # | Function | Entry | Return |
|---|----------|-------|--------|
| 0 | Program abort | — | — |
| 1 | Get console (echo) | — | A = char (parity stripped) |
| 2 | Put console | E = char | — |
| 3 | Get reader | — | A = char |
| 4 | Put punch | E = char | — |
| 5 | Put list | E = char | — |
| 6 | Get I/O byte | — | A = I/O byte |
| 7 | Set I/O byte | E = I/O byte | — |
| 9 | Print buffered line | DE = buffer | — |
| 10 (`0AH`) | Input buffered line | DE = buffer | — |
| 11 (`0BH`) | Test console ready | — | A = `FFH` ready / `0` not |
| 13 (`0DH`) | Reset SDOS, select drive A | — | — |
| 14 (`0EH`) | Select current disk | E = drive # | — |
| 15 (`0FH`) | Open file | DE = FCB | A = dir block / `FFH` not found |
| 16 (`10H`) | Close file | DE = FCB | A = dir block / `FFH` not found |
| 17 (`11H`) | Search directory for name | DE = FCB | A = dir block / `FFH` |
| 18 (`12H`) | Find next directory entry | DE = FCB | A = dir block / `FFH` |
| 19 (`13H`) | Delete file | DE = FCB | A = # entries deleted |
| 20 (`14H`) | Read next record | DE = FCB | A = 0 ok / 1 EOF / 2 unwritten |
| 21 (`15H`) | Write next record | DE = FCB | A = 0 ok / 1 err / 2 no space / `FFH` no dir |
| 22 (`16H`) | Create file | DE = FCB | A = dir block / `FFH` no dir space |
| 23 (`17H`) | Rename file | DE = FCB | A = # entries renamed |
| 24 (`18H`) | Get disk log-in vector | — | A = logged-in disks |
| 25 (`19H`) | Current disk | — | A = drive # |
| 26 (`1AH`) | Set disk (DMA) buffer | DE = buffer | — |
| 27 (`1BH`) | Disk cluster allocation map | — | BC = bitmap, DE = # clusters, A = sectors/cluster |
| 128 (`80H`) | Read console (no echo) | — | A = char |
| 129 (`81H`) | Get user register | — | BC = pointer to user register block |
| 151 (`97H`) | Set bottom of SDOS in RAM | E = new bottom hi-byte | — |

(Non-disk devices are named per the standard SDOS device table; all files may be accessed
sequentially or randomly.)

---

## 4. Disk layout and formats

An SDOS disk has a **System Section** (the SDOS resident image + the bootstrap loader + the disk
file **Directory**) and a **User File Section**. Disks self-describe their side count (1 or 2)
and density. Sectors are **128 bytes**, IBM 3740 soft-sectored; the supported geometries (from
the DDBIOS density tables) are:

| Format | Sectors/track | Tracks/side | Bytes/sector | Density |
|--------|:---:|:---:|:---:|:---:|
| 8″ single density | 26 | 77 | 128 | FM |
| 8″ double density | 50 | 77 | 128 | MFM |
| 8″ double density, 256 B | 26 | 77 | 256 | MFM |
| 5¼″ single density | 18 | 35 | 128 | FM |
| 5¼″ double density | 29 | 35 | 128 | MFM |

File names: an eight-character name plus a three-character type; standard types include `.COM`
(executable command), `.CMD` (batch command file — an "executive" file auto-run when named
without a type), plus source/object types. A password may be attached to the boot program.

---

## 5. Boot flow

Cold boot is only from **drive A**. From the monitor, `C 0` (or a bare boot) transfers to the
disk BIOS at **`F000H`**; the BIOS reads **track 0, sector 1 to `0080H`**, probes the disk to
identify its format (trying 8″ DD-256, 8″ SD, 5″ SD, 5″ DD in turn — adjusting the VersaFloppy
control-register size/density bits between tries), then loads the SDOS resident into the top of
RAM and jumps to it. On completion the OS prints its drive prompt (`[A]`). A `.CMD` file named
on the boot disk auto-runs after cold boot. The DDBIOS reset/warm-boot vectors are `BIOS`/`WBOOTE
F003H`, with the monitor re-entry at `MONITR E003H`.

---

## 6. Emulation checklist (summary of load-bearing facts)

- **CP/M-compatible OS.** Warm boot `JP` at `0000H`; system calls via **`CALL 5`** with the
  function number in **C**; FCB at `005CH`; 128-byte DMA buffer at `0080H`; illegal-address trap
  at `0038H`.
- **BDOS-numbered function set** (0–27 as CP/M, plus SD extensions 128/129/151). Console input
  strips the parity bit; `11` returns `FFH`/`0` for ready/not-ready.
- **Resident lives in the top ~7 KB**; User Area is `0100H`..bottom-of-SDOS; CONPROC (the
  command interpreter) is overlayable. `151` can lower the resident's bottom on a 256-byte
  boundary.
- **128-byte sectors, IBM 3740**; geometries per the DDBIOS tables (8″ 26/50/26×77, 5″ 18/29×35).
- **Cold boot from drive A only:** BIOS at `F000H` reads T0/S1 → `0080H`, auto-identifies format,
  loads the resident. Disk BIOS = DDBIOS (see the VersaFloppy reference); monitor re-entry at
  `E003H`, warm boot at `F003H`.
