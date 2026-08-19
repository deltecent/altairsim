# Digicomp Research Pascal-100

Source: [Pascal 100.pdf](#) (Digicomp Research Corporation, *Pascal-100 User's Manual*,
Second edition, March 1981; appendices B/F/G/H © 1979 The MICROENGINE Company / Western
Digital, appendix C © 1977 Mostek — all reprinted vendor material)

The **Pascal-100** is a **dual-processor 16-bit S-100 CPU boardset** from Digicomp Research
(Ithaca, NY). It is **two S-100 cards mated by a cable**, occupying two adjacent slots. It
carries **two real microprocessors**:

- the **Western Digital Pascal Microengine** (the WD9000 chip set) — a stack machine that
  **directly executes UCSD Pascal version III.0 P-code** in hardware (no software
  interpreter), word-addressed, 16-bit data path, with fast IEEE 32-bit floating point and
  Pascal concurrency primitives (SIGNAL/WAIT on SEMAPHOREs); and
- an ordinary **Zilog Z80**, which handles I/O, interrupts, memory-map loading, and runs
  stock 8080/Z80/CP/M software unchanged.

**Only one processor runs at a time.** The other sits in an S-100 HOLD state; software
switches between them by writing one byte to a **switch-selectable output port** (the System
Control Port). On reset the **Z80 has control**, so the board comes up as a plain Z80 S-100
CPU and boots CP/M; a software bootloading sequence then wakes the Microengine and starts the
UCSD Pascal system.

This file is a distilled **emulation reference** written from a scanned copy of the manual
(OCR text layer, ABBYY FineReader — a few OCR slips in prose, all tables/addresses verified).
It keeps only the software-visible model an emulator needs. **Pascal-100 is not emulated in
`altairsim`** — this is reference material for possible future S-100 CPU-board support.
Omitted: the circuit-level Theory of Operations (state sequencer, buffers, power supplies),
the schematics/parts list, the UCSD Pascal language and system (editor/filer/compiler), and
the reprinted **Microengine data sheet** (appendix B), **Z80 data sheet** (appendix C — see
[[Zilog Z80]] instead), and **P-machine** description (appendix F).

> **⚠ Byte-vs-word addressing.** The Microengine is **word-addressed**: it addresses 64K
> *words* = 128K *bytes*. Its physical S-100 **byte** address is its **word address × 2**
> (line A0 = 0). So Microengine word `FC68` = byte `1F8D0`, word `FC40` = byte `1F880`. The
> Z80 is byte-addressed as usual. Both share the same physical memory and the same Memory
> Map. Throughout this file, "word address" is the Microengine's view and "byte address" the
> Z80/bus view.

---

## 1. The two I/O ports — System Control Port + Map Output Port (the payload)

Pascal-100 exposes **exactly two I/O ports** to software: a pair of consecutive addresses, the
first (even) is the **System Control Port**, the second (odd) is the **Map Output Port**.

- Their base is set by **DIP switch U225** on the Z80 board: the 7 switches select the top 7
  address bits (the switch nearest the edge connector = MSB; **off/open = 1**, **on/closed =
  0**). The low bit is forced, so the System Control Port is always **even**.
- **Factory default: System Control Port = `D0`, Map Output Port = `D1`** (the recommended
  value; much of the software assumes it).

Either processor can write the System Control Port. The Map Output Port is written **only by
the Z80** (it uses the Z80 `OUT (C),A` indirect form — see §3.4).

### 1.1 System Control Port byte values (Table II-1)

An 8-bit **write-only** output port. Each function has a distinct byte:

| Function | Enable | Disable |
|---|---|---|
| Transfer control to **Microengine** | `80` | — |
| Transfer control to **Z80** | `00` | — |
| Memory Map (dynamic address translation) for **Microengine** addresses | `C0` | `40` |
| Memory Map for **Z80** addresses | `C1` | `41` |
| NMI on **memory-protection violation** | `C2` | `42` |
| **Auto-transfer** control Microengine→Z80 on interrupt | `C3` | `43` |
| **Interrupt the Microengine** (Z80 only) | `C4` then `44` | — |
| Select **lower half** of Memory Map for loading | `45` | — |
| Select **upper half** of Memory Map for loading | `C5` | — |

Pattern: bit 7 of the byte is the enable/disable sense for the paired options (`C_`=on,
`4_`=off), and the low nibble selects which option — a clean way to think of it when modeling
the port as a small state machine.

### 1.2 Reset state of the System Control Port options

On S-100 RESET, the port's options come up as:

- **Z80 has control** (Microengine held).
- **Auto-transfer on interrupt = disabled.**
- **Memory Map disabled** for both processors (addresses pass straight to the bus).
- **Lower half** of the Memory Map selected for loading.
- **Memory-protection-violation interrupt = disabled.**

The Memory Map **contents are undefined at power-up** and must be loaded before use.

---

## 2. Processors and transfer of control

### 2.1 Switching processors

Write the System Control Port: **`80` → run Microengine**, **`00` → run Z80**. When control
transfers, the active processor is put into an S-100 **HOLD** state and *all* its operation
suspends; the inactive processor leaves HOLD and **resumes exactly where it left off**.

Two exceptions to "resume where it left off":

- If the system was **reset** since that processor last ran, it begins its **initialization
  sequence** instead.
- If that processor has a **pending interrupt**, it starts an interrupt-acknowledge sequence.

Control is also handed to the **Z80 automatically** in two cases: **on reset**, and (optional)
**when an interrupt arrives while the Microengine is running** (§5.2).

### 2.2 Microengine instruction set (summary)

Stack-oriented, executes UCSD P-code III.0 directly: block-structured segmented variable
addressing, procedure/function calls, INTEGER/REAL(float)/CHAR/BOOLEAN, Pascal SET, ARRAY
subscript / RECORD subfield, relative + CASE branching, multitasking SIGNAL/WAIT on
SEMAPHOREs, and debug ops BPT (breakpoint) / RBP (return from breakpoint). Details are in the
reprinted WD Microengine data sheet (appendix B) and P-machine appendix F — not transcribed.

### 2.3 Clock

One clock generator drives both processors. Frequency is set by a **plug-in DIP header in
socket U102** on the Microengine board (pre-wired headers for **2 MHz and 2.5 MHz**; the board
is rated **2–3 MHz**). A processor-speed-independent **2 MHz reference** is available at
**bus pin 49**.

---

## 3. Memory Map (dynamic address translation)

An on-board, software-loaded **address-translation table** that turns a processor's *logical*
high address bits into a wider *physical* address on the bus. Two versions exist:

### 3.1 Standard Map (base configuration)

- **16 entries, 4 bits each**, mapping **8K-byte pages** onto bus lines **A13–A16** → **128K
  bytes** total physical range.
- **Microengine** selects the entry with its top 4 logical bits **A13–A16**.
- **Z80** selects with its top 3 bits **A13–A15**, with **A16 forced 0** — so the Z80 sees
  only the **first 8** of the 16 entries.
- Effect: 2× address space for the Z80 (its main purpose is to let the Z80 reach the
  Microengine's entire 128K range), no expansion for the Microengine.

### 3.2 Extended Map (option)

- **64 entries, 9 bits each**, mapping **2K-byte pages**.
- Without memory protection: 9 bits → bus lines **A11–A19** → **1 MB**.
- With memory protection (§4): high bit becomes a **protect-enable** bit, 8 bits → **A11–A18**
  → **512 KB**.
- **Microengine** selects the entry with top 6 bits **A11–A16**; **Z80** with top 5 bits
  **A11–A15**, A16=0 → Z80 sees the **first 32** entries (first half).

### 3.3 Enabling / disabling

Independent per processor, via the System Control Port: **Microengine `C0`/`40`**, **Z80
`C1`/`41`**. The change takes effect on the **next memory operation** (the next instruction
fetch), so the code that toggles the Map should live in memory that is unaffected by the Map —
otherwise the processor "jumps" into a different part of physical memory. Same caution applies
to changing the *other* processor's Map-enable state, since it bites at the next transfer of
control. Reset disables the Map for both.

### 3.4 Loading the Map (Z80 only)

Written through the **Map Output Port** (the odd address following the System Control Port —
`D1` by default). The Map is **write-only** (no read-back; software must track contents).

Only the Z80 can load it, using **`OUT (C),A`**: A on the data lines, C on A0–A7 (the port
address), B on A8–A15. Pascal-100 puts the port address on *both* the low and high S-100
address lines, so B's value never reaches the bus — instead the **high bits of B** are decoded
internally to pick which Map entry to load.

- The **top bit of the entry address** comes from the System Control Port half-select:
  **`45` = lower half**, **`C5` = upper half** (only half the Map is loadable at a time). The
  remaining entry-address bits come from the **high bits of register B**.
- **⚠ Contents are INVERTED before storage** (a side effect of the fast RAM chip). *You must
  write the one's-complement of the value you want.*
- **Standard Map:** entry = A register bits d1–d4 (i.e. the middle 4 bits), inverted → new
  A13–A16 contents.
- **Extended Map:** A bits 0–7 inverted → A11–A18; the 9th (A19, or protect bit) comes from
  the **low bit of register B** inverted. With protection installed, that bit **=1 enables
  protection** for the page (low bit of B = 0).

Loading while the Map is enabled is legal but changing the entry that maps the currently
running code causes a jump into other physical memory (do not).

### 3.5 Uses

Besides extending address space: (a) the Microengine's reset init vector lives at word `FC68`
(byte `1F8D0`), **above the Z80's range** — the Map lets the Z80 write there (§6.2 boot maps
it down to `38D0`); (b) relocate a fixed-physical-address device (e.g. a memory-mapped video
board) out of the middle of the logical space, or unmap a bootstrap ROM once it's no longer
needed.

---

## 4. Memory protection (Extended Map only)

Selected by **jumper J201**. The high bit of each 2K Extended-Map entry becomes a
**write-protect enable** for that 2K block (address range then 512 KB, not 1 MB). A `1`
high-bit protects the block. If enabled, a write to a protected block optionally raises an
**NMI** on the bus — armed via System Control Port **`C2`** (disable `42`).

---

## 5. Interrupts

Pascal-100 watches two S-100 lines: **pINT (pin 73)** and **NMI (pin 12)**. **All interrupts
are received and processed by the Z80.**

### 5.1 Z80 interrupts

- **pINT → Z80 IRQ**: masked/enabled by the Z80's own interrupt instructions; any of the Z80's
  three interrupt modes works. Compatible with standard S-100 Z80/8080 priority-interrupt
  boards.
- **NMI → Z80 NMI**: causes the Z80's automatic **CALL to `0066h`**.

**⚠ Pin 12** is used here for **NMI**. Boards that use pin 12 as an RDY line must be jumpered
otherwise.

### 5.2 Automatic transfer of control on interrupt

If the Microengine is running when an interrupt arrives, control is **automatically passed to
the Z80** so it can service it. Armed by System Control Port **`C3`** (disable `43`).

- With auto-transfer **disabled**, a **pINT** is not serviced until control next reaches the
  Z80 on its own.
- **NMI always forces a transfer**, regardless of this setting.
- **⚠** Whenever the Z80's interrupts are disabled *and* the Microengine is used, auto-transfer
  **must be disabled** too — otherwise an interrupt the Z80 would ignore still causes a
  spurious transfer out of the Microengine.

### 5.3 Interrupting the Microengine (from the Z80)

The Microengine ignores the bus interrupt lines (unless specially jumpered). The Z80 raises the
Microengine's **internal** interrupt line by writing the System Control Port **`C4` then `44`**
(no interrupt-acknowledge bus cycle occurs). This should be followed by a transfer of control
to the Microengine so it can respond. When it responds:

1. It first **writes spurious interrupt-controller data to byte `1F880`/`1F881`** (word
   `FC40`) — the controller isn't present on Pascal-100, but **those two bytes must not hold
   anything valuable** (save/restore them).
2. It **reads a 16-bit vector from byte `1F8C0`/`1F8C1`** (word **`FC60`**) — usually set by a
   Z80 interrupt routine — which points at a pointer to a **SEMAPHORE** (typically set up in
   Pascal via `ATTACH`).
3. It **SIGNALs** that semaphore, waking any PROCESS that issued a WAIT on it.

> **⚠ Manual erratum.** The text gives the vector's word address as "FC80", but byte `1F8C0` ÷
> 2 = word **`FC60`** (and `1F880`÷2 = `FC40` checks out, as does the reset vector `1F8D0`÷2 =
> `FC68`). Treat the byte addresses `1F8C0/1F8C1` as authoritative; word `FC60` is the correct
> conversion.

---

## 6. Reset, jump-on-reset, and the boot sequence

### 6.1 Z80 jump-on-reset (Power-On-Jump)

Like the CompuPro and MITS Turnkey S-100 CPUs, Pascal-100 can force the Z80 to begin execution
at a PROM anywhere on a **4K boundary (`0000`–`F000`)** after reset, so a monitor/boot PROM
need not live at 0.

- Enabled by **jumper J202** (factory **enabled**). If disabled, the Z80 starts at `0000`.
- Address set by **DIP switch U242** (4 switches = A12–A15; furthest from edge = MSB;
  **off/open = 1**, **on/closed = 0**; rest of address = 0). Factory default **`0000`**.
- Mechanism: on the first three post-reset fetches the board **synthesizes a `C3 00 X0` Z80
  `JP` instruction** on the data bus (`C3`, then `00` low byte, then the switch-set high byte),
  then re-enables normal memory. (Shift register U244 clocked by pSYNC.)

### 6.2 Microengine reset

On reset the Microengine fetches **initialization information from byte `1F8D0`** (word
`FC68`). This is above the Z80's 64K range, so unless memory ignores the extended address bits
the **Map must be used** for the Z80 to write there — which is exactly what the soft boot does
(mapping `1F8D0` → `38D0`).

> **Power-sequencing note (hardware, but observable):** if +12 V comes up before −3.9 V, the
> Microengine mis-initializes — instead of executing code it writes to byte `1F808/9` and then
> **hangs reading those locations forever**. (The board delays +12 V to avoid this; noted here
> only because the failure signature is a tight read loop at `1F808`.)

### 6.3 Pascal bootloading sequence (UCSD Standard Format disk)

Tracks/sectors below are for "UCSD Standard Format"; other disk formats differ.

1. **Hard boot** (system-dependent PROM). Reads **SBQB** (track 0, sectors 2–5, 4 sectors) and
   the **Z80 BIOS** (track 0, sectors 6–21, 16 sectors) — 20 sectors — into memory starting at
   **`280h`**, does system init (serial ports, interrupt controllers), and jumps to `280h`. On
   Tarbell and similar, this happens in two stages (a tiny PROM reads track 0 sector 1, which
   then loads the rest).
2. **Soft boot** (`280h`, system-independent). Uses the resident BIOS to read the **Pascal
   boot** (track 0 sectors 22–25 + Pascal blocks 0–1 = track 1 odd sectors; 12 sectors) into
   **`5000`–`55FF`**. Initializes the **ZMCOM** communication area (§7) from a parameter block
   at the front of SBQB (usable memory size, System Control Port address, BIOS-extension /
   user-Z80-function addresses — all editable with the **QGEN** utility). **Maps the
   Microengine init vector `1F8D0` → `38D0`**, points the init vector at the first three words
   of the Pascal boot, and fires up the Microengine via the **QB** interface routine.
3. **Pascal boot** (runs on the Microengine). Searches the boot volume's Pascal directory:
   loads **`SYSTEM.Z80BIOS`** (optional BIOS replace/extend, for BIOSes bigger than the 16
   track-0 sectors) and **`SYSTEM.Z80CODE`** (optional user Z80 functions / interrupt
   routines) to SBQB-specified load points, then loads **`SYSTEM.PASCAL`** (segment dictionary
   + OS segments 0 and 3) and transfers to segment 3 — the UCSD Pascal OS is now running.

---

## 7. Interprocessor communication — ZMCOM

When UCSD Pascal is running, the Z80 and Microengine communicate through a shared **ZMCOM**
record at **byte address `80h`** (Microengine word address 64) plus two routines: **Z80FUNC**
(Microengine side, marshals a call to the Z80) and **QB / "quarterback"** (Z80 side). The Z80
does hardware I/O, interrupt handling, Map changes, and any user Z80 functions on the
Microengine's behalf.

ZMCOM (from Figure 5-2) holds: a **system-configuration section** (top-of-memory word address,
System Control Port address, config flags, load points for `SYSTEM.Z80BIOS`/`Z80CODE`, the
user-Z80-function jump-table address, end-of-Z80-low-memory address, disk-I/O buffer byte
address), the **current disk-I/O parameter-block pointer**, a **general communication section**
(requested Z80 function number, return code, and images of Z80 registers A/BC/DE/HL plus extra
`zx`/`zy`/`zintr` cells — the register-passing channel both ways), and a default **128-byte
disk-I/O buffer** followed by a **64-word Memory Map image** (`zmmi[0..63]`, one word per 2K
page — low 9 bits are the Extended-Map contents; for the Standard Map only the word for the
last 2K of each 8K page is meaningful).

Low-memory layout (Figure 5-1), byte addresses: interrupt vectors `00`–`7F`; ZMCOM `80`–`1A3`;
UCSD Pascal OS tables `1A6`–`235`; SYSCOM config table `236`–`307`; unused `308`–`344`; Z80 QB
routine from `346`; Z80 BIOS from `480`; then optional user Z80 functions, the Pascal heap /
code / stack, and (optionally) more user Z80 functions at the very top.

---

## 8. I/O operations

### 8.1 Z80 I/O

All Z80 I/O instructions work. Pascal-100 **duplicates the 8-bit port address onto the upper 8
address lines** too (A8–A15), for compatibility with older S-100 I/O boards that decode the
full 16 bits. **⚠** With the Z80 *indirect* I/O forms (`IN r,(C)` / `OUT (C),r`), register B
does **not** appear on the upper address lines (the port address is mirrored there instead).

Extended (16-bit) I/O addressing from the IEEE standard is **not** supported — the port address
goes on both halves. Slow boards can get an **automatic I/O wait state** via jumper J105.

### 8.2 Microengine I/O — memory-mapped

The Microengine has no I/O instructions. Instead the **top 512 words** of its 64K-word space are
decoded as I/O ("memory-mapped I/O", *not* the Memory Map):

- A read/write to word **`FFpp`** = an **8-bit** I/O access to **port `pp`** (address mirrored
  on both address halves, as with Z80 I/O).
- A read/write to word **`FEpp`** = a **16-bit** I/O access: **sSIXTRQ is asserted**; if the
  device returns **SIXTN**, 16 bits transfer, otherwise a single byte (otherwise identical to
  the 8-bit case).

**Pascal-level `PORTIO`** exploits signed-16-bit word arithmetic: `portaddr := portnumber −
256` maps port `pp` to word address `FFpp` (since `FFpp` as a signed integer = `pp − 256`); use
**`− 512`** for a 16-bit port (`FEpp`). It then reads/writes through that pointer. `PORTIO`
does no ready-check.

### 8.3 Direct Microengine serial I/O (NEWSETUP facility)

Digicomp's UCSD OS can drive serial units (Pascal I/O units 1,2,6,7,8) **directly from the
Microengine** instead of calling the Z80 BIOS, for speed. Each such unit is parameterized (via
the **NEWSETUP** utility, which edits `SYSTEM.MISCINFO`) with an **input control+data port
pair** and an **output control+data port pair**, plus which bit is **RDA** (received-data
available) on input and **TBE** (transmit-buffer empty) on output. **TRUE may be a 0 or a 1**,
configurable per unit, to suit different I/O boards. The driver (`DIRECTIO`) polls the control
port until the ready bit matches, then reads/writes the data port; on input it **masks to the
low 7 bits** (`value mod 128`). Default `SYSTEM.MISCINFO` has units 1,2,4,5,6 present and
direct-Microengine I/O **off** for all units.

---

## 9. S-100 bus and compatibility (IEEE-696)

Pascal-100 meets the **IEEE S-100 (696)** standard with extras for pre-standard boards.

- **16-bit data transfers:** requester asserts **sSIXTRQ (pin 58)**; a capable board answers
  **SIXTN (pin 60)**; DI+DO buses then carry 16 bits. No SIXTN → two sequential byte transfers,
  so 8- and 16-bit memory freely mix.
- **Byte sex: low-order-even, even-byte-first** (the low byte of a word has the lower, even
  address). Forced by needing the Z80 (which stores 16-bit low-byte-first) and the Microengine
  to agree, and by cheap even-address increment.
- **Extended addressing:** up to 24 address lines. Standard Map uses **1** extended line
  (128K); Extended Map uses **4** (1 MB) or **3** (512K with protection); unused extended lines
  held at 0. Jumper **J203** optionally asserts **PHANTOM (pin 67)** whenever an address **>64K**
  is presented, so old non-extended boards in the low 64K can be disabled by PHANTOM (they must
  disable *both* read and write; the >64K boards must ignore PHANTOM).
- **Status/control:** all IEEE status lines latched and valid through the cycle (false ≥¼ clock
  between cycles); **sSTACK not generated**; **sM1** is a true instruction-fetch only for the
  Z80. Status is **not** placed on the data lines during pSYNC (boards that decode status off
  the data bus need modifying). Control-signal timing ≈ 8080. **pSTVAL (pin 25)** timing is
  jumperable IEEE-vs-8080 (J104/J106/J107). **MWRITE** generation via **J204** (default on).
  **pWAIT (pin 27)** generation via **J109** (default on) for dynamic-RAM refresh in wait
  states.
- **Not generated:** 8080 INTE, Z80 MREQ/RFSH (contrary to IEEE and meaningless when the
  Microengine runs; Z80 refresh addresses are suppressed).
- **Front panels:** work only for **reset, run, and sometimes single-step** — examine/deposit
  etc. do not.
- **Memory speed:** access time = clock period − 90 ns (**410 ns at 2 MHz**); 450 ns static RAM
  works at 2 MHz. The limit is the Z80 instruction-fetch cycle.

---

## 10. Switches and jumpers (quick map)

**DIP switches (Z80 board):**

| Switch | Sets | Bits | Default |
|---|---|---|---|
| **U225** | System Control Port / Map Output Port base | top 7 addr bits (off=1) | `D0`/`D1` |
| **U242** | Z80 jump-on-reset address | A12–A15 (off=1) | `0000` |

**Jumpers (Table III-1)** — factory defaults in the last column:

| Jumper | Board | Function | Default |
|---|---|---|---|
| J101, J102, J103, J108 | Microengine | factory test (do not change) | — |
| **J104** | Microengine | pSTVAL timing: IEEE or 8080 | 8080 |
| **J105** | Microengine | extra wait state during I/O | disabled |
| **J106** | Microengine | pSTVAL inactive (IEEE) or not (8080) when pHLDA asserted | 8080 |
| **J107** | Microengine | pSTVAL disabled (IEEE) or not (8080) when CDSB asserted | 8080 |
| **J109** | Microengine | pWAIT generated during wait states | enabled |
| **J201** | Z80 | memory-protection option (512K+protect vs 1M) | disabled |
| **J202** | Z80 | jump-on-reset enable | enabled |
| **J203** | Z80 | assert PHANTOM for addresses >64K | disabled |
| **J204** | Z80 | generate MWRITE | enabled |

---

## 11. Emulation notes

- Modeling this board means modeling **two CPUs sharing memory with a HOLD handshake** plus a
  **write-only mode/state port** — closest existing analogue in the tree is the CompuPro
  8085/88 dual-processor CPU (see [[CompuPro CPU 8085-88]]) and the swap-port pattern there,
  though Pascal-100's second CPU is a **P-code engine**, not another 8080-family part.
- The **Microengine (WD9000) P-machine is the hard part**: it executes UCSD P-code III.0
  natively; a faithful emulation needs a P-machine interpreter, not just a second 8080-style
  core. Its data sheet (appendix B) and the P-machine description (appendix F) are the
  reprinted WD/MICROENGINE authorities; the Version III.0 P-machine instruction set is tabulated
  in appendix H.5.
- The Z80 side is a stock Z80 ([[Zilog Z80]]) with a **Power-On-Jump** (§6.1, `C3 00 X0`
  synthesis — same trick as [[MITS Turn Key Board]] and [[CompuPro CPU 8085-88]]) and a
  **dynamic address-translation Map** (§3).
- The three cross-checkable "magic" byte addresses to get right: Microengine **reset init
  vector `1F8D0`** (word `FC68`), **interrupt-controller scratch `1F880/1`** (word `FC40`), and
  **interrupt vector `1F8C0/1`** (word `FC60` — the manual's "FC80" is wrong, §5.3).
