# Digital Research CP/M 2.2 — Operating System Manual (CCP, ED, ASM, DDT, BDOS/BIOS)

Source: [CPM 2.2 Manual.pdf](https://deramp.com/downloads/altair/software/manuals/CPM%202.2%20Manual.pdf)
— *CP/M Operating System Manual*, Digital Research (© 1982), the reformatted 2.2 compilation.
317 pp, real text layer. Fetched 2026-07-17 from deramp.com.

Not a hardware source. It is here because altairsim **runs** CP/M 2.2 (the 88-DCDD and 88-MDS
boot it) and its toolchain feeds the debugger: **`ASM` writes the `.PRN`/`.HEX`**
`src/core/symbols.{h,cpp}` and `LOAD` consume, and **`DDT` is the period analogue of our own
debugger** — the same `X`/`T`/`G`/`L`/`D`/`A`/`S` verbs and the same register-line format. See
also [[Microsoft M80 Assembler]] / [[Microsoft L80 Linker]] for the Microsoft path, and
`docs/manual/debugging.md`. Six sections: **CCP**, **ED**, **ASM**, **DDT**, **BDOS
interface**, **BIOS/system generation**. Distilled here: the parts altairsim touches.

## The one page-zero map every CP/M machine depends on (§6.9)

The bytes below 0100H are contract, not incidental — a booted guest, `LOAD`, `SAVE`, and DDT
all assume them:

| Locations | Contents |
|---|---|
| `0000H–0002H` | `JMP` to the warm-boot entry (BIOS+3). `JMP 0000H` (or a front-panel restart) reboots. |
| `0003H` | Intel `IOBYTE` (logical↔physical device map; optional in the CBIOS). |
| `0004H` | Current default drive (0=A … 15=P). |
| `0005H–0007H` | `JMP` to the **BDOS entry**. `JMP 0005H` is *the* system-call door; `LHLD 0006H` yields the lowest CP/M address (top of the TPA). DDT rewrites this field to shrink the TPA. |
| `0038H–003AH` | **RST 7** — jumps into DDT/SID for breakpoints in debug mode. Otherwise unused by CP/M. |
| `005CH–007CH` | Default **FCB** the CCP builds for a transient. |
| `0080H–00FFH` | Default 128-byte **DMA buffer**; also holds the command tail when a transient loads. |
| `0100H` | Base of the **TPA** — every `.COM` starts here. |

## CCP — built-in commands and line editing (§1.4–1.5)

The Console Command Processor's own verbs (everything else is a transient `.COM` loaded into
the TPA). `ufn` = unambiguous file, `afn` = wildcard (`?`/`*`); names fold to upper-case.

| Command | Form | Does |
|---|---|---|
| `DIR` | `DIR afn` | List matching files (bare `DIR` = `DIR *.*`; `NO FILE` if none). |
| `ERA` | `ERA afn` | Erase. `ERA *.*` prompts `ALL FILES (Y/N)?`. |
| `REN` | `REN new=old` | Rename (`FILE EXISTS` / `NO FILE` on error). |
| `SAVE` | `SAVE n ufn` | Write `n` 256-byte pages from the TPA (100H up) to `ufn`. |
| `TYPE` | `TYPE ufn` | Print an ASCII file (expands tabs on 8-column stops). |
| `USER` | `USER n` | Switch user area 0–15. |
| `d:` | `B:` | Switch logged drive. |

Transients named in the manual: `STAT`, `ASM`, `LOAD` (Intel HEX → `.COM`), `DDT`, `PIP`,
`ED`, `SYSGEN`, `SUBMIT`, `DUMP`, `MOVCPM`.

**Line-editing control characters (Table 1-1)** — worth knowing because they are what a
console filter must pass through, not eat:

| Key | Effect |
|---|---|
| `CTRL-C` | Warm-boot when pressed at start of line. |
| `CTRL-E` | Physical end-of-line (no send until RETURN). |
| `CTRL-H` | Backspace one character. |
| `CTRL-P` | Toggle echo of console output to the list device. |
| `CTRL-R` | Retype the current line. |
| `CTRL-S` | Freeze console output; any key resumes. |
| `CTRL-U` / `CTRL-X` | Delete the whole line. |
| `CTRL-Z` | End of input (PIP/ED — and the `.SYM`/`.PRN` end-of-file pad byte, 1AH). |
| `rub/del` | Delete + echo the last character. |

## ASM — the CP/M assembler (§3)

The 8080 assembler bundled with CP/M (no macros — that is the optional `MAC`). `ASM x`
assembles `x.ASM` → **`x.HEX`** (Intel HEX) + **`x.PRN`** (listing). A `parms` suffix
`x.p1p2p3` redirects: p1 = source drive, p2 = HEX drive (`Z` skips HEX), p3 = PRN drive
(`X` = console, `Z` = skip).

**The `.PRN` geometry — the exact shape our symbol loader reads.** Machine info occupies the
**leftmost 16 columns**, source to the right; an **`=` in the EQU column** marks an equate.
From the manual's own sample (§4.4):

```
Code address   Source program
0100 0608      MVI  B,LEN     ;LENGTH OF VECTOR TO SCAN
0107 7E        LOOP:   MOV A,M ;GET VALUE
0008 =         LEN     EQU $-VECT   ;LENGTH
```

That `=` is why a `.PRN` can tell an `EQU`/constant from a real label — the split
[[altairsim-symbolic-debugging]] relies on so `0008` never prints as `LEN`. (This is the ASM
listing; M80/MAC produce the same 16-column shape, verified across assemblers.)

**Assembler directives (Table 3-3):** `ORG`, `END [addr]`, `EQU`, `SET`, `IF`/`ENDIF`
(conditional; body listed-but-not-assembled when the expr is 0), `DB`, `DW`, `DS`.

**Operators (Table 3-2), highest precedence first:** `* / MOD SHL SHR` · unary `- +` · `NOT`
· `AND` · `OR XOR`. All arithmetic is 16-bit unsigned (`-1` → `0FFFFH`).

**Two traps for anyone reading a CP/M `.ASM`/`.PRN`:**
- **Default radix is DECIMAL.** `B`/`O`/`Q`/`D`/`H` suffixes select base; a bare number is
  decimal. ⚠ altairsim's own expression evaluator defaults to *hex* — a constant copied out
  of a `.ASM` is not what the monitor would read it as. A hex constant must lead with a digit
  (`0FFH`), same escape as [[altairsim-symbolic-debugging]]'s `known()` leading-zero rule.
- **Identifiers: up to 16 chars, all significant** (embedded `$` ignored, for readability).
  That is *different from M80's 6-significant-char rule* — an ASM `.PRN` will not collide two
  names the way an M80 listing can, so the collision policy is an M80/MAC concern, not an ASM one.

Reserved register values in operands: `B=0 C=1 D=2 E=3 H=4 L=5 M=6 A=7`, `SP=6`, `PSW=6`.
`$` = address of the next instruction. Opcode mnemonics evaluate to their base byte (`MOV`→40H).

## DDT — the Dynamic Debugging Tool (§4): the period debugger our own mirrors

`DDT`, `DDT x.HEX`, or `DDT x.COM` loads over the CCP, below the BDOS, and shrinks the TPA
(rewriting the `0006H` field). Prompt is `-`. `DDT x.typ` ≡ `DDT` then `Ix.typ` then `R`.

| Cmd | Form | Does |
|---|---|---|
| `A` | `As` | In-line **assemble** from address `s` (empty line ends). |
| `D` | `D` / `Ds` / `Ds,f` | **Dump** hex + ASCII, 16/line. |
| `F` | `Fs,f,c` | **Fill** `s..f` with byte `c`. |
| `G` | `G` / `Gs` / `Gs,b` / `Gs,b,c` | **Go**, up to two breakpoints. Break prints `*addr`; **both breakpoints clear on hit**. |
| `I` | `Ifile[.typ]` | Set the default **FCB** at 5CH (and the file for `R`). |
| `L` | `L` / `Ls` / `Ls,f` | **List** (disassemble), 12 lines. |
| `M` | `Ms,f,d` | **Move** block `s..f` → `d`. |
| `R` | `R` / `Rb` | **Read** the `I`-named HEX/COM (optional bias `b`). Prints `NEXT PC / nnnn pppp`. |
| `S` | `Ss` | **Set**/examine memory, byte at a time (`.` ends). |
| `T` | `T` / `Tn` | **Trace** 1..n steps, CPU line before each (~500× slower). |
| `U` | `U` / `Un` | **Untrace** — like `T` but no per-step display. |
| `X` | `X` / `Xr` | e**X**amine/alter the CPU state. |

**CPU register display (X command, Table 4-3)** — the line format DDT prints, which
altairsim's status line echoes:

```
CfZfMfEfIf A=bb B=dddd D=dddd H=dddd S=dddd P=dddd  inst
```

Flags `C Z M E I` (carry, zero, minus/sign, even-parity, interdigit/half-carry) as `0/1`;
`A` byte; `B D H S P` as 16-bit pairs; `inst` is the disassembly at P. Registers alter as
**pairs** — set `B` sets BC.

**Breakpoints and trace ride on RST 7.** `G`/`T`/`U` break by planting an **RST 7** (038H), so
**a program under test cannot use that interrupt vector**, and a hung real-time program is
recovered by a front-panel RST 7. Trace pauses across the CP/M interface (BDOS I/O runs
real-time) and always runs with interrupts enabled. Numbers are hex, 1–4 digits, truncated on
the right. The A/L assembler-disassembler overlay can be clobbered by a large program under
test — then `A`/`L` give `?` and `T`/`X` show `inst` in raw hex.

## BDOS call convention + function numbers (§5.2) — what a running guest asks the OS

Call through `0005H`: **function number in `C`**, argument (address) in **`DE`**; byte result
in `A`, word result in `HL` (with `A=L`, `B=H`). Out-of-range function → `A=0`.

```
 0 System Reset      10 Read Console Buffer   20 Read Sequential    30 Set File Attributes
 1 Console Input     11 Get Console Status     21 Write Sequential   31 Get Addr(Disk Parms)
 2 Console Output    12 Return Version         22 Make File          32 Set/Get User Code
 3 Reader Input      13 Reset Disk System      23 Rename File        33 Read Random
 4 Punch Output      14 Select Disk            24 Return Login Vec    34 Write Random
 5 List Output       15 Open File              25 Return Current Disk 35 Compute File Size
 6 Direct Console IO 16 Close File             26 Set DMA Address     36 Set Random Record
 7 Get I/O Byte      17 Search for First       27 Get Addr(Alloc)     37 Reset Drive
 8 Set I/O Byte      18 Search for Next        28 Write Protect Disk  40 Write Random w/ Zero Fill
 9 Print String      19 Delete File            29 Get R/O Vector
```

(Functions 28 and 32 are flagged non-portable.) Function 26 (**Set DMA Address**) is why the
88-DCDD/88-MDS BIOS and the guest agree on where a sector lands; the default is the 0080H
buffer above.
