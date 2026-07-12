# Tarbell single-density floppy disk controller

Board **#1011**, Tarbell Electronics, first shipped **2 July 1977**. One card, two things: a
**32-byte boot PROM** that shadows low memory, and an **FD1771B-01** floppy controller. The PROM half
was built first because it is the card that settles what PHANTOM\* means for the whole bus.

**Sourced from `reference/Tarbell_Floppy_Disk_Interface_Manual.pdf`** (78 pp, real text layer), which
also reprints the FD1771 data sheet as its §7-2. Page citations below are **PDF pages**.

> **The CHIP is not this card.** The FD1771 lives in `src/chips/wd17xx.{h,cpp}` with
> `tests/test_wd17xx.cpp`, modelled from its own data sheet. This card does not model a controller
> chip — **it wires one up**: it hands `Wd1771` a `FloppyDrive` and points the chip's pins at whichever
> drive its select latch has chosen. The FD1771 has **no drive-select and no side-select pin** — both
> are the card's, which is why the chip has an `attach()` and not an index. Read `wd17xx.h` first.
>
> ⚠️ And do **not** build from `reference/Western Digital WD177X-00 - Datasheet.pdf`. That is a
> *different chip* (WD1770/72/73): wrong step rates, wrong Type-II status. See `docs/sources.md`.

## ⚠️ This card has no PHANTOM\*, and we model it with PHANTOM\* anyway

**The word "phantom" does not appear in the manual. Not once in 78 pages.** The card is from **July
1977** — *pre-IEEE-696* — and pin 67 did not yet exist as a standard. What it actually does (**p.11,
§2-5**) is assert **STATUS DISABLE\*** (pin 18) and drive the S-100 **status lines itself**, through
nine 74LS367 buffers, substituting a *write* status so the memory boards latch the byte while the
PROM answers the read:

> *"Their purpose is to suspend the usual CPU control of the data bus and gate the proper control
> signals onto the S-100 status lines to put the bootstrap in operation."* — p.11
>
> *"The special hardware tricks played on the board make this possible by controlling the bus to write
> into main memory while reading from the PROM bootstrap."* — p.44

**We model it as PHANTOM\* + `honors_phantom = "read"`, and that is a deliberate abstraction, not a
claim about the hardware.** It is recorded here rather than quietly adopted, because an unlabelled
abstraction is exactly the §0.1 failure mode — a plausible story standing in for a sourced fact.

Why the abstraction is the right call (Patrick, 2026-07-12):

- **Nothing observable differs.** Reads at `0000-001F` come from the PROM, writes fall through to
  RAM, the shadow drops on the first read with A5 high. No program can tell, and TARPROM — the only
  program that depends on any of it — certainly cannot.
- **We drive no other status lines.** Modelling STATUS DISABLE\* honestly would mean first building
  S-100 status lines *so that we had something to disable*. That is machinery invented to be faithful
  to a mechanism nobody can observe.
- **PHANTOM\* is not dead weight borrowed for this card.** It is a real signal that later cards
  genuinely assert, so the bus concept earns its keep regardless.

**The one real divergence, and it is a config trap, not a software one:** the real card takes over
the whole bus, so it shadows **every** memory board unconditionally, jumper or no jumper. Ours only
shadows boards with `honors_phantom = "read"`. A machine built with a memory card that ignores
phantom would boot on iron and hang here. Two things blunt it: the memory board **defaults to
`read`**, and the **bus prints a warning** when a board asserts PHANTOM\* while another board decoding
the same address does not honour it. So the divergence is loud, and it errs toward being *stricter*
than the hardware, which is the safe direction.

Rejected alternatives, so nobody re-opens them: **bus arbitration** (having the bus prefer the
Tarbell) breaks the rule that *the bus picks no winner* — two boards on one address is contention and
we refuse to arbitrate it (§4.6). **Modelling STATUS DISABLE\* properly** is honest and about the same
size, and it needs no jumper because the real memory boards had no say — but it requires knowing the
gating, and that is on the schematic (p.78), not in anybody's reasoning. If we ever want it, read the
schematic; do not derive it.

## Why this card matters out of all proportion to its size

Because of thirty-two bytes. The Tarbell's boot PROM is the reason `phantom = read` exists on the `memory` board, and the reason `Board::snoop()` exists on the bus. Both were designed for it before it was written, and it is the only card so far that needs either.

## What it does — sourced (Patrick, 2026-07-11)

1. It carries a **32-byte boot PROM**.
2. **POC\* asserts PHANTOM\*.** Power on the machine and the PROM is shadowing.
3. While PHANTOM\* is asserted, **the memory boards in the system must allow writes to their RAM, but not reads.**
4. **As soon as the Tarbell sees a memory read with A5 set**, it knows the PROM is no longer needed and **disables PHANTOM\***.

## The PROM itself — `roms/TARBELL-SD/TARPROM.ASM` (Patrick, 2026-07-12)

We have the source. It is **exactly 32 bytes**, `0000-001F`, ending on the `HLT` — it fits the PROM with nothing to spare. And it does not merely illustrate the three claims below, it **proves** them:

```asm
BOOT:   IN   WAIT       ; 0000  wait for home
        XRA  A
        MOV  L,A        ; HL = 0000  <-- the sector loads OVER THE PROM
        MOV  H,A
        INR  A
        OUT  SECT       ; sector = 1
        MVI  A,0CH
        OUT  DCOM       ; read sector
RLOOP:  IN   WAIT       ; 000C  <-- the loop FETCHES ITSELF from the PROM...
        ORA  A
        JP   RDONE
        IN   DDATA
        MOV  M,A        ; ...while WRITING through 0020, 0021, ... 007F
        INX  H
        JMP  RLOOP
RDONE:  IN   DSTAT      ; 0019
        ORA  A
        JZ   07DH       ; 001C  <-- 0x7D = 0111_1101. A5 IS SET.
        HLT             ; 001F
```

**It writes through itself.** `HL` starts at `0000` and `MOV M,A` walks it upward, so the sector lands in the RAM the PROM is shadowing. Reads come from the PROM; writes must reach RAM. That is `honors_phantom = "read"`, and it is why the strap lives on the **memory** board.

**Only a READ releases PHANTOM\*, never a write** — and this one is load-bearing in a way that is easy to miss. The load loop writes to `0020` and beyond *while still fetching itself* from `000C-0018`. If a **write** with A5 high dropped the shadow, the PROM would vanish in the middle of the load, the next fetch at `RLOOP` would come from RAM — which by then holds sector bytes, not the loader — and **the bootstrap would eat itself.**

**The release is combinational.** The last instruction is `JZ 07DH`, and `0x7D` is `0111_1101`: **A5 is set.** The jump into the sector it just loaded *is itself* the first read with A5 high. If the flip-flop only took effect on the following cycle, that fetch would still be shadowed and would come back from the PROM. The release is armed by the address of the code it is jumping to — which is a lovely piece of engineering, and it means the release **must** land on the cycle that triggers it.

All three are asserted in `tests/test_phantom.cpp`, with these exact addresses.

## What that implies, and why the design bends to it

### The bootstrap writes *through* itself

Point 3 is the whole reason `phantom = read` exists (`memory.md`). A 32-byte bootstrap cannot do anything useful except **load a real boot sector from disk into RAM** — and the RAM it is loading into is the RAM its own PROM is shadowing. So writes *must* reach the RAM while reads are still coming back from the PROM.

The mechanism is not a special case anywhere, but it does **not** live where an earlier draft of this file claimed.

**The Tarbell holds PHANTOM\* asserted continuously — like an interrupt.** From RESET until A5 releases it, on reads *and* on writes. It does **not** gate the pin with the read strobe, and it has no opinion at all about what a write should do.

**The read/write distinction lives on the MEMORY board:** `honors_phantom = "read"` means *stop answering reads, keep answering writes*. That is the jumper that lets the bootstrap's sector land in the RAM the PROM is shadowing.

> **Corrected 2026-07-12 by Patrick, from the schematic.** This file previously said in bold that the honoring board "must never grow" a read mode, and that the Tarbell ANDed PHANTOM\* with MEMR. Both were **wrong**, and both were *reasoned* rather than *sourced* — §0.1 exactly. Worse, the Tarbell's own documentation was quoted two files away and says the opposite in plain words: *"the memory boards installed in the system must allow writes to their RAM, but not reads."* **The memory boards.** The sentence was right there.

### The release is combinational, not clocked

Point 4 has a sharp edge. The bootstrap's **first fetch outside the PROM** is itself a read with A5 set — so if PHANTOM\* dropped on the *next* cycle, that fetch would happen while memory was still shadowed, read `0xFF` from the floating bus, and no Tarbell would ever have booted. **The release must take effect on the very cycle that triggers it.**

Hence the split in `Board`:

- `assertsPhantom()` is **combinational and pure** — it tests A5 off the address bus, so the triggering cycle is already un-shadowed.
- `snoop()` is the **flip-flop** — called once per completed cycle, it latches the release so it *stays* released.

Both halves are needed. Without the latch, a data read back down at `0x0004` (below A5) would re-shadow the PROM over the sector that was just loaded there.

### A5 is a wire, not a threshold

**`c.addr & 0x0020`. Not `c.addr >= 0x20`.** They differ: `0x0040` is above the PROM but has bit 5 *clear*, so it does **not** release PHANTOM\*. The card decodes one address line, and "the PROM is no longer needed" is the Tarbell's *reasoning*, not its *circuit*. Tidying this into a range compare would be a plausible, clean, wrong simulation of a wire — `tests/test_phantom.cpp` fails if anyone does.

### The PROM stops driving when it stops shadowing

The same signal that gates PHANTOM\* gates the PROM's own output drivers, so once released the PROM no longer decodes. It has to be that way: if it kept answering `0x0000`–`0x001F` after the memory board came back, the two would both drive and that is **real bus contention**, which we report and do not arbitrate (§4.6).

## The three open questions are CLOSED (2026-07-12, from the manual)

| Question | Answer |
|---|---|
| Does RESET\* re-arm the shadow, or only POC\*? | **RESET\* does — and so does EXTCLR\*.** All three (POC\*, RESET\*, EXTCLR\* — bus pins 99, 75, 54) feed the same gate cluster into the boot flip-flop U34. **p.3:** *"a 32-byte ROM bootstrap program, which is automatically enabled when the computer RESET button is pushed."* **p.11:** *"Bootstrap is initiated by NOR gate U28 receiving Power On Clear (**also generated by RESET**)."* The suspicion that the two were tied was right. |
| Where does the PROM decode? | **`0000`–`001F`. Sourced, byte-for-byte.** The checkout procedure (**p.21**) prints the whole PROM for you to compare against the front panel: `0000: DB FC AF 6F 67 3C D3 FA 3E 8C D3 F8 DB FC B7 F2` / `0010: 19 00 DB FB 77 23 C3 0C 00 DB F8 B7 CA 7D 00 76`. That is `TARPROM.ASM` exactly, `JZ 07DH` and all. |
| A5 as a wire, not a threshold | **Sourced.** **p.7:** the bootstrap can be disabled by hand *"by putting front panel data switch 5 to the up position and doing an examine."* Switch 5 puts **A5 high** and the shadow dies. `tests/test_phantom.cpp` is now backed by the manual, not just by inference from the PROM listing. |

---

# The FDC half

## Ports — the base is a DIP switch, and the card claims eight

A 6-bit comparator (U25) matches the **upper five address bits (A3–A7)** against DIP switch S1
(**p.8**), so the base is any multiple of 8. **Switch off = logic 1**, so **all off = `0xF8`** (**p.19**),
which is what everything in the tree assumes. The low three bits give eight ports, of which **five do
anything**:

| | OUT | IN |
|---|---|---|
| **base+0** (`F8`) | Command → 1771 | Status ← 1771 |
| **base+1** (`F9`) | Track → 1771 | Track ← 1771 |
| **base+2** (`FA`) | Sector → 1771 | Sector ← 1771 |
| **base+3** (`FB`) | Data → 1771 | Data ← 1771 |
| **base+4** (`FC`) | **Control** — a function decoder, see below | **Wait** — see below |
| base+5..7 | *nothing* | *nothing* |

The card only supplies `CS*`; **the FD1771 decodes A0/A1 itself** (**p.9**), which is why base+0..3 map
straight onto the chip's own register file. `base+1 = track` is sourced three ways (the port table on
p.9, the checkout table on p.19, and Tarbell's own driver equates on p.46) — it is no longer the
inference it was, and the boot PROM never touches it.

## `IN base+4` — the wait port

**Only bit 7 is driven.** (**p.44**)

> *"If the most significant bit is 0, the interface is indicating that it was the INTRQ that caused
> the end of the wait. If 1, it was the DRQ, indicating some data is ready to process."*

- **bit 7 = 1 → DRQ**: another byte. Go read `base+3`.
- **bit 7 = 0 → INTRQ**: the command is done. Go read status at `base+0`.

Both the PROM and Tarbell's drivers test it with `ORA A` and the **sign flag**, nothing else.

> **Bits 6..0 are NOT DRIVEN — they float.** Exactly one 74LS367 sits on this path and its output goes
> to DI7 alone; the FD1771's data buffers are not enabled (CS\* is inactive because A2=1). The manual
> never states a value and no Tarbell code ever reads them. **Do not model them as zero** — that is a
> value nobody promised, and returning it would let software depend on it.

**It genuinely stalls the CPU, and we do not.** The real card drags XRDY low and holds the processor
until DRQ or INTRQ (**p.12**; pad E48→E46 on an Altair, **p.25**). The manual's own checkout says so
out loud: *"Front panel lights 'WO' and 'WAIT' should be on"* (**p.20**) — that is a machine sitting in
a wait state on the PROM's first instruction. **We buffer whole sectors, so DRQ is always immediately
ready and the port never stalls.** Same bytes, same order; only the elapsed T-states differ, and no
software can see it. Recorded under Limitations.

## `OUT base+4` — the control port, and it is NOT a bitmap

This is the thing no code in the tree could have told us, and the reason a *reasoned* control port
would have been wrong. It is a **3-to-8 decoder (U56, 74LS138) plus a 4-bit latch (U40, 74LS175)**
(**p.10**). The low three data bits select a **function**; only **three of the eight exist**:

| D2 D1 D0 | Function |
|---|---|
| `000` | Pulse **RST\*** — via pad E-32 |
| `001` | Pulse **SO\*** — an *extra* step pulse, for drives that step faster than the 1771 can drive them |
| `010` | **Strobe D4–D7 into the latch U40** |
| `011`–`111` | **Nothing.** Y0–Y4 are physically unconnected. |

> **⚠️ `RST*` IS A DRIVE LINE, NOT THE FD1771's RESET.** This is the biggest trap on the card, and the
> plan had it wrong. `RST*` (pad E26) goes **to the disk drive** — in every one of the manual's six
> per-drive tables it is wired to the drive's **write-fault reset** (e.g. **p.26**, CDC BR803A: *"E26
> RST\* J1-42 Write fault reset drive 0"*). It never touches the controller chip. And it only pulses
> at all if the builder installed the **E32→E34** strap. Function `000` does **not** reset the 1771.

**Drive select is BINARY, in D5:D4.** Tarbell's own driver settles it (**p.46**):

```asm
SELECT: MOV  A,C     ; disk number
        ANI  3       ; 0..3
        RAL          ; into bits 4 & 5
        RAL
        RAL
        RAL
        ORI  2       ; function 010 -- strobe the latch
        OUT  DEXT    ; = base+4
```

So: `OUT base+4` with **D2:D0 = 010** and **D5:D4 = the drive number, 0–3**. The latch's low two bits
feed a 1-of-4 decoder (via strap E52→E41) and come out **one-hot** as drive-select lines. Binary in,
one-hot out — and **E52 is therefore not a spare**, which the plan also had wrong. Up to **four
drives** (**p.3**).

> **Why `BIOS (1).ASM` never writes `0xFC`** — which had looked like a hole in the source. Pad **E30 is
> tied to ground** (**p.24**) and may be strapped to E29 to *always select drive 0*. A single-drive
> Tarbell has no reason to touch the control port at all. **The BIOS is not incomplete — the board is
> strapped.** Default our straps so the shipped PROM and BIOS work unchanged: latch = 0 selects drive 0.

**There is no factory default strapping.** It is a kit, shipped with no jumpers, and §4-2 gives six
different per-drive tables. The only strap the manual *requires* is E48→E46 on an Altair.

## Status is TRUE-SENSE — the exact opposite of the DCDD

The FD1771's DAL is an **inverted** bus (data sheet, p.75), and the card's buffers (74LS368, **p.15**)
are **inverting** in both directions. **The two cancel, and the guest sees the chip's registers in true
sense** (**p.11**). No inversion belongs in the chip *or* the card.

> The **88-DCDD's status reads INVERTED**, and both cards can sit in the same machine with both
> conventions live. Say it loudly in both docs, because it is the kind of thing that gets "fixed".

## Geometry

From the **IBM 3740** format table the manual reprints at §7-2-13 (**p.63**), which is the format the
card is built for:

| | |
|---|---|
| Tracks | **77** (0–76; *"Track Number (0 thru 4C)"*) |
| Sectors | **26** (*"Sector Number (1 thru 1A)"*, *"write bracketed field 26 times"*) |
| Sector size | **128 bytes**, payload only |
| **First sector** | **1** — the DCDD's is **0** |
| Image | 77 × 26 × 128 = **256,256 bytes** |
| Density | **Single.** *"not designed to work with double-density or mini-floppys"* (**p.3**) |
| Sides | **One.** See below. |

**Sectors are numbered from 1**, and the boot PROM proves it: `XRA A / INR A / OUT SECT` loads sector
**1**. This is DESIGN.md §7.3's `startSector`, *"exactly the off-by-one that silently corrupts a
disk"* — so it is a parameter, in the board, where it is visible.

**Soft-sectored: the image holds the payload ONLY.** No sector headers. The FD1771 consumes the ID
field itself; `IN base+3` hands you the 128 data bytes between the data address mark and the CRC.
(The DCDD's image holds the whole 137-byte slot, headers and all. Same file extension, incompatible
contents — DESIGN.md §7.3.)

> **Single-sided, and the manual does NOT settle it.** p.3 says the card *"will work with multiple and
> double-sided drives"*, but that is a sentence about drive compatibility: **the card provides no
> side-select signal anywhere** — not in the jumper list, not in any of the six per-drive tables, and
> the FD1771 has no side-select pin. Model it single-sided. Two sides is past what this document
> authorizes.

Ignore the *"243 kilobytes"* on p.3: that is CP/M's **data** capacity after two reserved system tracks
(75 × 26 × 128), not the image size. **The image is 256,256 bytes.**

## Clock, and the step rates that prove the chip

- **250,000 bits/sec** — *"the interface runs at the standard speed"* (**p.3**).
- A **4 MHz crystal** (Y1) is **divided by two** to give the FD1771 its **2 MHz** CLK (**p.12**, **p.20**;
  the ÷2 is a 74LS175 toggle on the schematic, and the data sheet demands *"2 MHZ ± 1%"*). Pin 25
  (XTDS\*) is grounded, disabling the chip's internal data separator — the card has its own.
- **Step rates at 2 MHz: 6 / 6 / 10 / 20 ms** (Table 1, **p.71**). *The 00 and 01 codes are both 6 ms;
  that is not a typo.* The BIOS's `STPRATE equ 2` is commented `;10ms step rate`, and rate 2 is 10 ms
  **on an FD1771 only** — a WD177x reads it as 20. This table is the tripwire in `test_wd17xx.cpp`.

## RESET homes the head

S-100 **RESET\*** reaches the FD1771's **MR** pin (pin 19), via the same gate cluster as the boot
flip-flop. The data sheet: *"When MR is brought to a logic high a **Restore Command is executed**,
regardless of the state of the Ready signal."* So a front-panel reset drives the head back to track 0
with no software involved — and the manual prints the test for it (**p.39**):

> *"Move the head out about half way by turning the shaft of the stepper motor... **Press system
> reset. The head should move to track 0 (outside) and stop.**"*

Nobody would guess this. `Wd1771::masterReset()` already implements it.

## Index pulses, and what "rotation" means on a soft-sectored disk

The FD1771's **IP** pin is not decorative: **Type I status bit S1 *is* that pin**, a Force Interrupt on
I2 fires on its **edge**, and the chip **counts index pulses to time out a sector search**. It comes
from `Spindle` (DESIGN.md §7.5.1) — `(now % tPerRev) < width`.

> **The two floppy cards mean different things by "spinning".** The **DCDD is hard-sectored**: 32
> physical sector holes plus one index hole, and the holes *are* the sector counter. The **Tarbell is
> soft-sectored**: **one index hole and no sector holes at all** — its 26 sectors are ID address marks
> *recorded on the medium*. So `Spindle::sectorAt()` here means *"which ID field is under the head"*,
> which is exactly the question `Read Address` (0xC4) has to answer, and which the buffered CP/M BIOS
> uses to begin a track read where the head already is. **A static answer spins that BIOS forever.**

## Straps we model, and the ones we do not

The card is a kit with no factory jumpers, so "default" means *what makes the shipped PROM and BIOS
work*: **E48→E46** (XRDY on an Altair — the manual's one hard requirement), **E31→E29** (the input mux
follows the select latch), **E52→E41** (the second select bit reaches the 1-of-4 decoder), **E32→E34**
(software write-fault reset), **E43→E44** (the drive has a READY line).

## Limitations

- **The wait port never stalls the CPU.** Real: XRDY drags the processor to a halt until DRQ/INTRQ.
  Ours: sectors are buffered, so DRQ is always ready. Same bytes, same order, fewer T-states.
- **`IN base+4` bits 6..0 float** and we return them undefined, because that is what the bus does.
- **Function `001` (the fast-step back door) moves the head behind the chip's back.** The manual is
  explicit that *"the program must keep track of the number of pulses"* — the FD1771's track register
  goes stale, by design. Modelled; used by the PerSci strapping and nothing else we run.
- **No side select**, because the card has none.
- The on-board input multiplexer is **2-way only**. With 3–4 drives the real machine straps E29→E30
  and relies on the drives to gate their own status onto the cable. We simply report the selected
  drive's status, which is what a machine with modern drives does.
