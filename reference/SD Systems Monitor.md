# SD Systems SD / MS Monitor (v2.10)

Source: [SDS_Monitor.pdf](#), SDMONV21.Z80, MSMONR21.Z80

The **SD Monitor** is the boot/debug PROM monitor for the SD Systems
[SBC-100 / SBC-200](SD%20Systems%20SBC-100%20%26%20SBC-200.md). It is a **Z80** monitor
(full Z80 register control, including IX/IY, the alternate set, and the I / interrupt-flag
registers) that lives in the onboard PROM at **`E000H`** and boots
[SDOS / COSMOS](SD%20Systems%20SDOS.md) or CP/M from a
[VersaFloppy](SD%20Systems%20VersaFloppy.md) controller. This is a distilled emulation
reference: the manual's tutorial prose and worked examples are condensed to the command set,
the hardware the monitor touches, and its RAM layout — the facts needed to reproduce or drive
it in software.

The command set below is from the manual (SD #7140011, Rev B, March/April 1981). The two ROM
sources — **SDMONV21.Z80** ("SD monitor Version 2.10") and **MSMONR21.Z80** ("MS monitor
Version 2.10") — are the assembled artifacts; they are the authoritative source for the RAM map
and the console-port constants, and their register-save area matches the manual's Table 4-1
exactly.

---

## 1. Console, prompt and version

- **Prompt: a period `.`** printed on reset and before every command. Typing `.` in place of a
  command (or during one) aborts back to the prompt.
- **No sign-on banner or version string is printed** — reset produces only the `.` prompt.
- **Console port** (from the ROM sources — the manual does not print the address):
  - **MSMONR21 ("MS" build): data = `7CH`, status = `7DH`** — the 8251 USART on the SBC-100/200
    exactly as documented in the board reference. This build does the **auto-baud** measurement:
    it waits on 8251 status bit 7, times the start bit of the first character, and loads the CTC
    time constant to match (the manual's "reply with a CR and the baud rate is matched").
  - **SDMONV21 ("SD" build): data = `01H`, status = `00H`**, with a baud latch written at port
    `78H`. This is a fixed-console variant for a serial port at 00/01 rather than the 8251 at
    7C/7D; the command set is otherwise identical.
- Both are **Version 2.10**. Operands are **hex only** (0–9, A–F, uppercase); only the last four
  digits of a number are used, leading zeros assumed; a bad character prints `?` and aborts.

---

## 2. RAM register-save map (`FFE6H`–`FFFFH`)

The monitor keeps the guest's Z80 register image in high RAM; it is loaded into the CPU on
`G`/`S` and saved on a breakpoint or single-step. This map (manual Table 4-1) is confirmed by
the ROM equates (`MNFLG0 EQU 0FFFAH`, `TEMPB0 EQU 0FFFCH`, `ADRBUF EQU 0FFFEH`, …):

| Address | Contents | | Address | Contents |
|---------|----------|-|---------|----------|
| FFE6/E7 | SP (lo/hi) | | FFF4–F9 | L H E D C B |
| FFE8/E9 | IY | | FFFA | IF (interrupt flag; `04` = enabled) |
| FFEA/EB | IX | | FFFB | I |
| FFEC–F3 | alt set L′H′E′D′C′B′F′A′ | | FFFC | F |
| | | | FFFD | A |
| | | | FFFE/FF | PC (lo/hi) |

The monitor's own stack and scratch sit just below this (`STKTOP EQU 0FFC0H`, parameter and
breakpoint-save cells from `STKTOP+1` up). Base-page RAM cells `0040H–004FH` mirror the DDBIOS
disk parameters (transfer address, unit, sector, track, record count, error mask/status) that
the `R`/`W`/`Z` commands share with the floppy driver.

---

## 3. Command set

Every command is one letter plus optional hex operands, terminated by `CR`; `.` aborts.

### 3.1 Memory

| Cmd | Syntax | Action |
|-----|--------|--------|
| **D** | `D aaaa bbbb` | Display memory `aaaa`..`bbbb`, 16 bytes/line, hex + ASCII (`.` for non-print). `bbbb` optional → 256 bytes. Space = next 256; `.` = exit. |
| **E** | `E aaaa` | Examine/substitute at `aaaa`: type a byte + `CR` to change and advance; `^` re-examines / with no data steps back; `CR` alone advances unchanged; `.` after data exits without altering. |
| **F** | `F aaaa bbbb cc` | Fill `aaaa`..`bbbb` with byte `cc`. |
| **M** | `M aaaa bbbb cccc` | Move block `aaaa`..`bbbb` to `cccc` (`bbbb` > `aaaa`). |
| **L** | `L aaaa bbbb cc0..ccn` | Locate a string (up to 6 bytes) in `aaaa`..`bbbb`; prints each match address; `.` ends. |
| **T** | `T aaaa bbbb` | RAM test `aaaa`..`bbbb`; reports addr/written/read on failure, prints `P` per pass; `.` ends. |
| **V** | `V aaaa bbbb cccc` | Verify/compare `aaaa`..`bbbb` against the block at `cccc`; reports differences. |

### 3.2 I/O ports

| Cmd | Syntax | Action |
|-----|--------|--------|
| **I** | `I pp nn` | Input from port `pp`, `nn` times (`nn` omitted → 1; `nn`=0 → continuous until `.`). |
| **O** | `O pp dd nn` | Output data `dd` to port `pp`, `nn` times (same `nn` rule). |
| **P** | `P pp` | Port examine/modify at `pp` (interactive like `E`). |

### 3.3 Program control

| Cmd | Syntax | Action |
|-----|--------|--------|
| **B** | `B aaaa` | Set a breakpoint at `aaaa` (3-byte `JMP` patched over user code; auto-removes the previous one). On hit, user code is restored, registers displayed, and single-step mode is entered. `B` alone removes the breakpoint. |
| **G** | `G aaaa` | Go: load the RAM register map into the CPU and execute at `aaaa` (`G` alone resumes at the saved PC). |
| **S** | `S aaaa nn` | Single-step `nn` steps from `aaaa`, displaying registers after each. `CR` = one step; space = 11 steps with heading; `.` = exit. |
| **X** | `X a b` | Examine/set the register-display mode: `a` = headings on/off, `b` = short (PC & AF) vs long (all). The setting persists for breakpoint/step displays. |
| **H** | `H aaaa bbbb` | Hex arithmetic: prints `aaaa+bbbb` then `aaaa−bbbb`. |

### 3.4 Disk (require a VersaFloppy II + the SBC-200 DDBIOS PROM)

| Cmd | Syntax | Action |
|-----|--------|--------|
| **C** | `C aa` | **Boot SDOS/COSMOS** from drive `aa` (0–9 → A–J; default 0/A). On success the OS prints `[A]`. |
| **R** | `R aaaa fd tt ss nn` | Read `nn` × 128-byte sectors from disk (format `f`, drive `d`, track `tt`, sector `ss`) to RAM at `aaaa`. |
| **W** | `W aaaa fd tt ss nn` | Write `nn` × 128 bytes from `aaaa` to disk (same operands; drive 0–3). |
| **Z** | `Z f d` | Format a diskette (IBM 3740), format-type `f`, drive `d`; both sides if the VersaFloppy II double-sided jumper is set. |
| **Q** | `Q f d` | Read a diskette (IBM 3740). *Noted in the manual as not working with the VersaFloppy II.* |

**Disk format-type codes `f`** (for `R`/`W`/`Z`): `0`=8″SS-SD-128, `1`=8″DS-SD-128, `2`=5″SS-SD-128,
`3`=5″DS-SD-128, `4`=8″SS-DD-128, `5`=8″DS-DD-128, `6`=5″SS-DD-128, `7`=5″DS-DD-128,
`C`=8″SS-DD-256, `D`=8″DS-DD-256.

---

## 4. Boot flow

On reset the SBC board auto-starts the monitor PROM at `E000H` (see the board reference §5). The
monitor initializes the 8251 (mode `4E`/`4F`, command `37`) and CTC channel 0, auto-detects the
console baud from the first typed `CR`, then prints `.`. `C` transfers to the disk BIOS
(`MONITR EQU 0E003H` is the monitor entry the DDBIOS jumps back to); the BIOS at `F000H` reads
track 0 sector 1 to `0080H`, identifies the disk format, and loads the OS.

---

## 5. Emulation checklist (summary of load-bearing facts)

- **Z80 monitor at `E000H`; prompt `.`; no banner; hex-only operands; `.` aborts.** Version 2.10.
- **Two builds differ only in the console driver:** MSMONR21 → 8251 at **7CH/7DH** with CTC
  auto-baud; SDMONV21 → console at **01H/00H** with a baud latch at 78H. Identical command set.
- **Register image in RAM `FFE6H`–`FFFFH`** (SP, IY, IX, alt set, L H E D C B, IF, I, F, A, PC);
  `G`/`S` load it, breakpoints/steps save it. Monitor stack `STKTOP=FFC0H`; disk params mirror
  base page `0040H–004FH`.
- **Breakpoint = 3-byte `JMP` patch**, single previous breakpoint auto-removed, drops into
  single-step on hit.
- **Disk commands `C/R/W/Z/Q`** drive a VersaFloppy II via the DDBIOS PROM at `F000H`; `C aa`
  boots SDOS/COSMOS and the OS prints `[A]`.
