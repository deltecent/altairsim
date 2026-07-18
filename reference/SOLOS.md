# SOLOS / CUTER — personality-module monitor reference

Source: `SOLOS.pdf` (Processor Technology / Software Technology Corp. *SOLOS/CUTER User's Manual*,
document M0100, © 1977), corroborated by the SOLOS 1.3 source (`roms/SOLOS/SOLOS13.ASM`). See
`docs/sources.md`. [Source: #]

This is the *software* companion to `Sol-20.md` (the Sol-PC hardware I/O map). SOLOS is the
2048-byte personality-module monitor that ships in the Sol-20's `builtin:solos` ROM
(`C000`–`C7FF`); **CUTER** is the same operating system repackaged for a non-Sol machine that has
a VDM-1 (Processor Technology's `builtin:cuter`, VDM console). This file distills the command set,
the console/VDM control codes, and the machine-language calling interface a guest program uses.

## SOLOS vs CUTER — one OS, two builds

The manual refers to "SOLOS" meaning the SOLOS/CUTER pair; they are source-compatible. The one
byte that distinguishes them at run time is `START` (`C000`, the cold-start `NOP`): **`00` under
SOLOS, `7F` under CUTER**. A program can read that byte through the jump-table base (in `HL`, see
below) to know which it is under. Differences that matter:

- **`TERM` (terminal mode) is SOLOS-only.**
- In COMMAND mode, SOLOS output always goes to the VDM; **CUTER** routes command output to the
  current output pseudo-port, and a system reset also resets CUTER's I/O pseudo-port selections to
  the default.

## Command mode

A system reset (power-on, or **upper-case + repeat** pressed together on the Sol) enters COMMAND
mode: SOLOS emits `CRLF` then a `>` prompt and waits for a line. A command is processed on
**Carriage Return**. Only the **first two letters** of a command are significant (shown
underscored in the reference lists). **`MODE` / Control-`@`** (`00`) aborts: while awaiting a
command it discards the current line; during most commands it turns off both tape machines and
returns to COMMAND mode.

### Console commands (5)

| Command | Syntax | Function |
|---|---|---|
| `EXEC` | `EX addr` | Begin program execution at `addr` (see register conventions below). |
| `ENTR` | `EN addr` | Enter hex data into memory starting at `addr`; a `/` terminates. |
| `DUMP` | `DU addr1 (addr2)` | Display memory from `addr1` to `addr2`. |
| `TERM` | `TE (port-in (port-out))` | Enter terminal mode — the Sol becomes a video terminal (SOLOS only). |
| `CUST` | `CU name (addr)` | Insert (or, if already present, remove) a custom command in the RAM table (up to six; `addr` defaults to the start of SOLOS). |

**`ENTR` is the `.ENT` file loader.** A `.ENT` software file (e.g. `trk80.ent`) is literally a
script of console input for this command: a first line `ENTER 0000` selects the load address,
then each line is `AAAA: bb bb bb …` where the `AAAA:` re-seats the address (the `:` is an address
terminator inside `ENTR`) and the bytes are laid down in sequence; a trailing **`/`** ends the load
and returns to COMMAND mode. Feed the file to the console (keyboard or serial), then `EX addr` to
run it. There is no tape involved — `.ENT` is console text, not a CUTS cassette image.

### Tape commands (4)

Unit is optional (`/1` default, or `/2`), separated from the name by a slash with no spaces
(`TARGT/2`). CUTS cassette at 300/1200 baud; deferred in altairsim (ports read idle).

| Command | Syntax | Function |
|---|---|---|
| `GET`  | `GE (name(/unit) (addr))` | Search forward and load the named (or next) tape file; `addr` overrides the header's load address. |
| `SAVE` | `SA name (/unit) addr1 addr2 (addr3)` | Save `addr1..addr2` to a named file; `addr3` overrides the load address written to the header. |
| `XEQ`  | `XE (name(/unit) (addr))` | `GET` then execute per the header (only if the file's TYPE says executable). |
| `CAT`  | `CA (/unit)` | Catalog: start the tape and list each file's header. Also handy to power the tape for rewind. |

A cassette error prints `ERROR (name)(type)(addr)(size)` — a bad/CRC read, a `MODE` abort during a
read, or an `XEQ` of a non-executable file.

### SET commands (10)

| Command | Syntax | Function |
|---|---|---|
| Speed of display | `SE S=data` | Screen character rate, `00` fastest … `FF` slowest. |
| Output port | `SE O=port` | Select output pseudo-port (0–3). |
| Input port | `SE I=port` | Select input pseudo-port (0–3). |
| Number of nulls | `SE N=data` | Nulls emitted after each `CRLF` (00…FF; 0 default). |
| Auto-execute | `SE XE addr` | Auto-exec address written to the next `SAVE` header. |
| Tape speed | `SE TA 0/1` | `0` = 1200 baud (fast), `1` = 300 baud (slow); normally 0. |
| Type | `SE TY data` | The header "type" byte for the next `SAVE`; **bit 7 = 1 → non-executable (data file)**, else auto-exec. Value is shown as its ASCII char by `GET`/`CAT`. |
| Custom output | `SE CO addr` | Address of a user output driver (used when `O=3`). |
| Custom input | `SE CI addr` | Address of a user input driver (used when `I=3`). |
| CRC check | `SE CR data` | `FF` = ignore all tape CRC read errors; any other value = normal checking. |

## Pseudo-ports

All non-tape I/O goes through four input and four output logical "pseudo-ports". SOLOS maps them
to hardware; CUTER maps them onto a VDM machine's ports (see `Sol-20.md` for the physical `F8`–`FF`
ports these resolve to). These logical numbers are **not** the hardware port numbers.

| Port | Input | Output |
|---|---|---|
| 0 | Keyboard | VDM display driver |
| 1 | Serial in | Serial out |
| 2 | Parallel in | Parallel out |
| 3 | User routine (`SET CIN`) | User routine (`SET COUT`) |

The default console is **input 0 / output 0** (keyboard in, VDM out).

## Machine-language interface

### Register conventions on entry to a user program

When SOLOS dispatches a program (`EXEC`, `XEQ`, or a custom command):

- **`SP`** is set into the SOLOS stack (top of SOLOS RAM, growing down — `SYSTP = CBFFh`). The
  stack is primed so the program can `RET` straight back to COMMAND mode.
- **`HL`** points to the very start of SOLOS — i.e. the base of the jump table below. A program
  uses `HL` to call SOLOS wherever it lives (CUTER can be relocated), and to read the `START` byte
  to tell SOLOS from CUTER.

### Jump table (`C000` on the Sol)

Always call SOLOS through this table, never a scattered internal address, so a program stays
compatible with CUTER. Offsets are from the jump-table base (`HL`); the Sol's SOLOS sits at
`C000`.

| Addr | Off | Label | Len | Function |
|---|---|---|---|---|
| `C000` | +0  | `START` | 1 | Cold-start reset marker byte (`00` SOLOS / `7F` CUTER). |
| `C001` | +1  | `INIT`  | 3 | `JMP` to the power-on reset. |
| `C004` | +4  | `RETRN` | 3 | Return to COMMAND mode (no system reset). |
| `C007` | +7  | `FOPEN` | 3 | Open a tape file. |
| `C00A` | +10 | `FCLOS` | 3 | Close a tape file. |
| `C00D` | +13 | `RDBYT` | 3 | Read a byte from an open tape file. |
| `C010` | +16 | `WRBYT` | 3 | Write a byte to an open tape file. |
| `C013` | +19 | `RDBLK` | 3 | Read one tape block into memory (per header). |
| `C016` | +22 | `WRBLK` | 3 | Write one tape block from memory (per header). |
| `C019` | +25 | `SOUT`  | 3 | Output the char in `B` to the current system output pseudo-port. |
| `C01C` | +28 | `AOUT`  | 3 | Output the char in `B` to the pseudo-port named in `A` (0–3). |
| `C01F` | +31 | `SINP`  | 3 | Status/char from the current system input pseudo-port → `A`. |
| `C022` | +34 | `AINP`  | 3 | Status/char from the pseudo-port named in `A` → `A`. |

**`SINP`/`AINP` are a combined status-and-get:** on return, the **zero flag set = no character
available**; zero flag reset = a character is in `A`. The idiom to wait for a key is `CALL AINP`
(or `SINP`) followed by `JZ` back to the call. `SOUT`/`AOUT` take the byte in `B`; on return from
`AOUT`, `A` and `PSW` are undefined and all other registers are preserved.

### VDM display driver (output pseudo-port 0)

Characters sent to the VDM driver are placed on screen, except these control codes and escape
sequences (the driver is why SOLOS can do cursor addressing a hardcopy terminal cannot):

| Code | Key | Function |
|---|---|---|
| `01` | Ctrl-A (SOH) | Cursor left one (wraps). |
| `0B` | Ctrl-K (VT)  | Clear screen, cursor home. |
| `0D` | Ctrl-M (CR)  | Clear to end of line, cursor to start of line. |
| `13` | Ctrl-S (DC3) | Cursor right one (wraps). |
| `17` | Ctrl-W (ETB) | Cursor up one line (wraps). |
| `1A` | Ctrl-Z (SUB) | Cursor down one line (wraps). |

Escape sequences begin with `1B` (ESC); `##` is a following data byte:

| Sequence | Function |
|---|---|
| `1B 01 ##` | Cursor to character position `##` (`00`–`3F`) of the current line. |
| `1B 02 ##` | Cursor to line `##` (`00`–`0F`, top = 0). |
| `1B 03`    | Return cursor position: `B` = char (00–3F), `C` = line (00–0F). |
| `1B 04`    | Return the screen memory address of the cursor in `BC`. |
| `1B 07 ##` | Output `##` to the screen literally (even a control code), advance cursor. |
| `1B 08 ##` | Set display speed to `##` (`00` fastest … `FF` slowest). |
| `1B 09 ##` | Same as `1B 01 ##` (cursor to char position `##`). |

### Cassette file header

`FOPEN` takes `HL` pointing at a header of this layout (also what `SAVE`/`GET` read and write):

| Field | Bytes | Meaning |
|---|---|---|
| `NAME` | 5 | ASCII file name, trailing zero-padded. |
| — | 1 | Reserved, must be 0. |
| `TYPE` | 1 | File type; **bit 7 = 1 → data file (non-executable)**. |
| `SIZE` | 2 | Length of the file in bytes. |
| `ADDR` | 2 | Load/save address. |
| `XEQ`  | 2 | Auto-execute address (ignored for data files). |
| — | 3 | Unused by SOLOS. |

Block access (`RDBLK`/`WRBLK`) moves a whole file per call; byte access (`FOPEN`/`RDBYT`/`WRBYT`/
`FCLOS`) buffers into 256-byte blocks (one cassette operation per 256 transfers) — BASIC uses this
for data files. `RDBLK`/`WRBLK` take unit+speed in `A`: **bit 5** speed (0 = 1200, 1 = 300), **bit
7** = tape 1, **bit 6** = tape 2, all other bits 0.

## Memory map (Sol-20)

See `Sol-20.md` for the full picture; the parts SOLOS defines:

| Range | Contents |
|---|---|
| `C000`–`C7FF` | SOLOS ROM (jump table at `C000`). |
| `C800`–`CBFF` | SOLOS scratchpad RAM + stack (`SYSTP = CBFFh`, grows down). |
| `CC00`–`CFFF` | VDM-1 screen RAM (16×64). |

## Quirks that bite an implementer

- **`.ENT` files load through `ENTR`, not tape.** They are console text (`ENTER addr` then
  `AAAA:` hex lines, `/` to end). A CUTS `GET` is a different path entirely.
- **The SOLOS/CUTER discriminator is the `START` byte** (`C000`): `00` vs `7F`. A relocatable
  program finds SOLOS through `HL` (set on entry), never a hardcoded `C000`.
- **`SINP`/`AINP` return status in the zero flag, char in `A`** — one call does both; loop with
  `JZ` to wait.
- **The `SAVE` "type" byte's bit 7 gates auto-execute** (`XEQ`), and the low 7 bits are shown as
  an ASCII character by `GET`/`CAT` — pick a printable value.
