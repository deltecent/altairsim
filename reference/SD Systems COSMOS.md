# SD Systems COSMOS Operating System

Source: [SDS COSMOS OS User Manual 198106.pdf](#) (*SD Systems Communication Oriented Multi-User
Operating System (COSMOS) — Instructional Publication*, SD #7140075 Rev. B, Version 2.17, June
1981), 94 pp, **scanned, no text layer — read as page images.** Provided by Patrick, 2026-08-18.

**COSMOS** is SD Systems' own multi-user operating system for its S-100 microcomputers (the
SD-200 / SBC-200 family). It time-slices **up to eight users** across independent jobs on an equal
priority basis, and is **CP/M 2.x-compatible**: "Programs written to run under SD-OS or CP/M* will
also run under COSMOS" (§1.1), and system call 141 (Get Version) returns `A = OSTYPE` with the CP/M
2.X compatibility flag (Table 5-6). COSMOS is *not* something this simulator emulates directly — it
is software the emulated Z-80 boots and runs, the same relationship the SDOS / SBC-100 pairing
already documents (see [`SD Systems SDOS.md`](SD%20Systems%20SDOS.md)) and CDOS documents for
Cromemco (see [`Cromemco CDOS.md`](Cromemco%20CDOS.md)).

This is a distilled emulation reference. It exists for **one payload above all**: COSMOS is the
period software that actually *drives the ExpandoRAM II bank switching* — the memory model in §2–§3
is the concrete guest-side story behind [`SD Systems ExpandoRAM II.md`](SD%20Systems%20ExpandoRAM%20II.md)
and the `bankmem` audit ([`docs/devguide/banked-ram.md`](#)). The tutorial prose, the ~45 console
and utility commands (Section IV), and CMD-file batch mechanics are condensed to what a machine or
board emulation must reproduce for COSMOS to boot and run. Kit-assembly, cabling and marketing
material are omitted.

⚠ **CP/M 2.x, not CP/M 3.** COSMOS is CP/M-*2.x*-compatible and banks memory through **its own**
port-FF page mechanism. A generic CP/M 3 (CP/M Plus) diskette will **not** exercise the ExpandoRAM
II — CP/M 3's own banking is driven by BIOS `SELMEM`/bank routines that would have to be written
for this board. Only a COSMOS (or SD-OS multi-user) image with the SD BIOS drives the port-FF
paging described here.

---

## 1. System shape (§1.1, §2.2)

- Multi-user, up to **8** users, time-sliced equal-priority. A job blocked on I/O or a semaphored
  section yields until the wait clears.
- Each user has a separate file directory; files are **local** (owner-only) or **global** (any
  user) — the global attribute is set by `ATRIB` and refused if the name collides in another
  user's directory (§2.3).
- Runs on the SD Systems microcomputer: **SBC-200** CPU (Z-80), **VersaFloppy II** floppy
  controller, **ExpandoRAM II** memory, **VDB-8024** video, and — for **more than two users** — an
  **MPC-4** four-channel serial board (§6.2.2). Two users need no MPC-4: the SBC-200 RS-232 (J10)
  drives the second terminal and the parallel port (J20) drives the printer.

---

## 2. Memory model — page-mode banking (the payload) (§2.2, Fig 2-1; §6.2.1, Figs 6-1/6-2)

COSMOS is a **page-mode** OS. A "page" is one **ExpandoRAM II 32K or 48K partition**, selected by
writing its page number to **port FF** (the ExpandoRAM II page-select latch — see
[`SD Systems ExpandoRAM II.md`](SD%20Systems%20ExpandoRAM%20II.md) §1). Verbatim (§2.2): *"COSMOS
is a page-mode multi-user operating system. Page 0 is 64K with the top 16K containing the operating
system. Each user is allocated a 32K or 48K user memory page."*

**Page 0 is special.** It holds **resident COSMOS** in the top region and the user-0 /
console-processor overlay below it. From Fig 2-1 and §2.2.1:

| Region (page 0, 48K-user layout) | Address | Contents |
|---|---|---|
| top 16K | `C000–FFFF` | **RESIDENT COSMOS** (DOS, disk allocation maps, non-disk + disk drivers) |
| just below | ~`BB00–BFFF` | User 0 variables & I/O buffer (top **1280 / `500H`** bytes) |
| lower ~47K | `0000–BAFF` | User 0 **or console-processor** memory overlay (31K in 32K mode, 47K in 48K mode) |
| page zero | `0000–00FF` | CP/M-style low memory (Table 2-1) |

In the 32K-user layout the OS region is the top 32K instead of 16K. **Only page 0 carries a copy of
resident COSMOS**; the code is re-entrant, so all active users execute the *same physical* resident
code during system calls (§2.2.1). The **console processor is not always resident** — it is
reloaded from the boot disk whenever a user program does `JP 0` / `LD C,0` / `CALL 5` (§2.2).

The top of available memory is communicated to software via the DOS entry at `0005`, exactly as in
any CP/M system (§6.2.1). About **47K of a 48K page** is left for the application (§2.2.3).

### Table 2-1 — user-page low memory (CP/M-standard)

| Hex | Description |
|---|---|
| `0000` | warm-restart entry on user-program exit |
| `0005` | DOS entry for system calls (`CALL 5`) |
| `0006–0007` | lowest memory address used by COSMOS |
| `0008–003F` | reserved for RST interrupt vectors |
| `0038` | illegal-address trap (RST 7) |
| `0040–005B` | system variables |
| `005C–007F` | standard FCB |
| `0080–00FF` | standard I/O buffer |

### The two supported hardware memory systems (§6.2.1)

Each ExpandoRAM II board is a full 64K carrying **two stacked partitions**; port FF pages among
them across several boards:

| Config | Partition | Boards | Total | Layout (Fig) |
|---|---|---|---|---|
| 32K users | 32K | **3** | 192K | Board 0 = *COSMOS 32K (top) + USER 0 (bottom)*; Board 1 = USER 2 / USER 1; Board 2 = USER 4 / USER 3 (Fig 6-1) |
| 48K users | 48K | **4** | 256K | Board 0 = *COSMOS 16K + USER 0 48K*; Boards 1–3 = a 16K piece + USER 1/2/3 48K; the three 16K pieces form USER 4 (Fig 6-2) |

Both support up to **five users**. No specific board sequence is required in the card cage (§6.2.1
NOTE). This whole-partition, common-OS-region layout is **not** a whole-64K-plane swap — which is
why the current emulation is only an approximation (§3).

---

## 3. ExpandoRAM II configuration under COSMOS — the knobs (§6.2.3.3, Table 6-1)

Every ExpandoRAM II board in a COSMOS system needs:

1. **Jumper E9→E10** (phantom-disable, all boards).
2. Manufacturing **level #9**, or the modification in **Technical Bulletin #105**.
3. The correct **82S130 bipolar PROM in U8** for the partition size:
   - **EX-32** (SD# **7010392**) — 32K user partition.
   - **EX-48** (SD# **7010393**) — 48K user partition.
4. **DIP switch S3** per **Table 6-1** — this is the **board-select** (boards 0–3), octal, ON = 0 /
   OFF = 1 (see [`SD Systems ExpandoRAM II.md`](SD%20Systems%20ExpandoRAM%20II.md) §3).

**Table 6-1 — ExpandoRAM II S3 board-select, boards 0–3, per partition size.** The scan renders S3
as darkened-switch bitmaps rather than legible octal digits (8-position switch, "darkened = ON");
the exact per-board bit pattern must be read from a clean scan or the schematic. What is certain:
each of the 3 (32K) or 4 (48K) boards carries a **distinct board-select value**, and the PROM
variant is the same across all boards in the system.

> ⚠ **Emulation gap.** `bankmem card=expandoram2` models a **binary page-select over 64K planes** —
> a documented approximation. It does **not** yet express any of what COSMOS actually needs:
> **board-select (0–7)**, **partition size (EX-32 / EX-48)**, the **32K+32K / 48K+16K partition
> geometry** with a fixed common OS region, or multiple boards coordinating on port FF. And the
> exact port-FF page → (board, window, RAS) map lives in the 82S130 PROM (Fig 2-4 of the ExpandoRAM
> II manual, not transcribable from the scan). So COSMOS cannot be booted or verified against this
> card today; a faithful model needs a **PROM dump** and a **COSMOS/SD-OS multi-user image**. See
> [`docs/boards/bankmem.md`](#) and [`docs/devguide/banked-ram.md`](#).

Companion jumpers on the other SD boards for a COSMOS install (§6.2.3):

- **SBC-200**: remove pin 13 of U8; move jumper X3-15→X3-16 to X13-2→X13-3; add X14-3→X14-4; DDBIOS
  PROM must be V2.3+; HDBIOS PROM in U37 for fixed/removable hard disk.
- **VersaFloppy II**: add E8→E9 and E11→E12.
- **VDB-8024**: add E14→E17.
- **MPC-4** (>2 users): four RS-232 channels, software-programmable baud 50–19,200 (§6.2.2.2,
  §6.3.2.7).

---

## 4. Boot and configuration (§6.3)

- **Distribution diskette**: double-density 8", **256-byte sectors**; the COSMOS system image is on
  **track 0, non-interleaved** (§2.1). Boot with the disk in drive A and typing **`C`** + RETURN;
  COSMOS signs on and prompts `[A:0]` (§6.3.1).
- **Configure**: run the batch `MCOSMOS.CMD` (`@ MCOSMOS`, or `@ MCOSMOS HD` for hard-disk boot).
  It asks the questions in Appendix B — system disk (A–J), sign-on message (≤20 chars), **user
  partition size (32/48)** (must match §6.2.1 hardware), default printer (0=parallel/1=serial), SBC
  serial baud, "IS MPC-4 IN SYSTEM (Y/N)", MPC-4 per-channel baud — and writes a system image
  **`COSMOS.SYS`** (§6.3.2). `WRTCOS d:=COSMOS.SYS` writes it to the disk's system tracks.
- **Boot a configured system**: `.C <n>` where `<n>` is a disk number from **Table 6-2** (`0`=A:
  … `4`=E: cartridge … `5–9`=F:–J: fixed platters). Omitted `<n>` defaults to 0 (§6.3.2.10).

---

## 5. System-call interface (§5, Table 5-6)

Entry via `CALL 5` with the function number in **C** (like CP/M). Calls **0–36** are the standard
CP/M-2.x BDOS set (numbered identically: 2 = put console, 6 = direct console I/O, 9 = print
buffered line, 15/16 open/close, 20/21 read/write next record, 22 create, 33/34 read/write random,
etc.). Call **11 (`0FH`) = LIFT HEAD / SPECIFY VERSION** returns `HL=02FH` (CP/M 2.X compatibility).
Drive numbers ride in **E** (Table 5-4). I/O device mnemonics are in **Table 2-2**
(`CON:`,`RDR:`,`PUN:`,`LST:`,`A:`–`D:` floppy, `E:` cartridge, `F:`–`J:` platters).

**COSMOS / SD extensions** (numbers ≥ 128; the multi-user set is ≥ 192):

| # (hex) | Function | Entry → Return |
|---|---|---|
| 128 (80H) | Read console, no echo | → `A=char` |
| 129 (81H) | Get user register pointer | → `BC`=user regs, `HL`=active user table, `A`=active user #, `E`=dir type |
| 130 (82H) | Set user Ctrl-C (abort) exit | `DE`=handler (0 reset, −1 disable) |
| 131/132 (83/84H) | Read / Write logical block | `A/D/E`=block, `B`=drive (top bit −1 = interleaved) → `A`=status |
| 141 (8DH) | **Get version of OS** | → `B/C`=version, **`A`=OSTYPE**, `E`=system disk # |
| 142 (8EH) | Set special CRT function | `D`=col/func, `E`=row/0 |
| 143/144 (8F/90H) | Set / Read date (packed BCD) | DD/MM/YY |
| 145/146 (91/92H) | Set / Read time (packed BCD) | SS/MM/HH |
| 147 (93H) | Set program return code | `E`=code |
| 148 (94H) | Set file attributes | `DE`=FCB, `B`=attrs |
| 149 (95H) | Read disk label | → `DE`=FCB |
| **192 (C0H)** | **START USER** | — |
| **193 (C1H)** | **STOP USER** | — |
| **194 (C2H)** | **MOUNT DISK** | `E`=disk# → `A`=0 ok / −1 already / −2 bad disk / −3 bad cluster |
| **195 (C3H)** | **DISMOUNT DISK** | `E`=disk# → `A`=0 ok / ≠0 can't (=user logged on) |
| **196 (C4H)** | **GET USER STATUS** | `E`=disk# → `A`=bitmap of users logged on (bit n = user n) |
| **197 (C5H)** | **DISPATCH MESSAGE** | `A`=dest user 0–7 (15 = all), `DE`=msg (first byte = length) |
| **198 (C6H)** | **RECORD LOCKING** | `DE`=lock parms → `A`=status |
| **199 (C7H)** | **ATTACH** | `E`=disk# → `A`=status |
| **200 (C8H)** | **IDENTIFY** | `B`=disk# → `A`=subdir # |

---

## 6. Disk organization (§2.1, Appendix A)

- Media areas: **COSMOS system image** (track 0, non-interleaved), **file directory**, **BAD.MAP**
  (pre-allocated bad clusters), **user file area** (the remainder).
- Hard-disk platters: up to **9 subdirectories** per platter (`MAP.DIR` bitmap per platter); each
  user independently selects the active subdirectory per platter (§2.1).
- File names: 8.3, any printable ASCII except `$ * ? = / . , :` and space; folded to upper case.
  Reserved extensions include `.COM .SYS .CMD .OBJ .HEX .PRN .BAS .BAK .$$$` (§2.3.1).

### Appendix A — SD Systems disk configurations

| Cfg | Sec/trk | Tracks | Max files | Max space | Sector size |
|:--:|:--:|:--:|:--:|:--:|:--:|
| 0 | 26 | 77 | 64 | 243K | 128 |
| 1 | 26 | 77×2 | 128 | 494K | 128 |
| 2 | 18 | 35 | 64 | 72K | 128 |
| 3 | 18 | 35×2 | 64 | 150K | 128 |
| 4 | 50 | 77 | 128 | 470K | 128 |
| 5 | 50 | 77×2 | 252 | 952K | 128 |
| 6 | 29 | 35 | 64 | 119K | 128 |
| 7 | 29 | 35×2 | 128 | 724K | 128 |
| **C** | 26 | 77×1 | 128 | 494K | **256** |
| **D** | 26 | 77×2 | 252 | 988K | **256** |

The COSMOS distribution diskette is a **config-C/D** 256-byte-sector double-density 8" disk.

---

## 7. Provenance / errata

- Version **2.17**, SD **#7140075 Rev. B**, June 1981. 94 pp; no text layer (read as page images).
- The ExpandoRAM II page-map figure this manual's memory model depends on is the one bound out of
  order in the *board* manual (its "Figure 4-2" is actually "Figure 2-4"); see
  [`SD Systems ExpandoRAM II.md`](SD%20Systems%20ExpandoRAM%20II.md) §1. This OS manual gives the
  guest-visible layout (§2, §6.2.1) but not the per-cell PROM truth table.
