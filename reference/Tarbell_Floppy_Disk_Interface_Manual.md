# The Tarbell Floppy Disk Interface Manual

Source: [Tarbell_Floppy_Disk_Interface_Manual.pdf](#)

This is an emulation-oriented digest of the Tarbell Floppy Disk Interface owner's
manual (Tarbell Electronics, Carson, CA; first deliveries July 2, 1977). The board
is an S-100 (IMSAI / Altair) floppy controller built around the **Western Digital
FD1771A/B-01** Floppy Disk Formatter/Controller. The full FD1771 data sheet is
reproduced in the original section 7-2 and is summarized here. Assembly steps,
parts lists, jumper-by-jumper drive wiring, marketing copy, and prices have been
condensed to only what matters for a software model of the card.

---

## 1. Overview / Key Characteristics

| Property | Value |
|---|---|
| Controller IC | Western Digital FD1771B-01 (single density / FM) |
| Transfer method | **Programmed I/O only — NOT DMA** |
| Data rate | 250,000 bits/sec (single density, 8" drives) |
| Formatted capacity | 243 Kbytes per diskette (IBM 3740 format) |
| Density support | Single density only. NOT double-density, NOT mini-floppy (as shipped) |
| Tracks | 0 (outermost) to 76 (innermost); 77 tracks |
| IBM 3740 geometry | 26 sectors/track, 128 bytes/sector |
| Drives supported | Up to 4 directly; up to 8 via spare IC slots |
| Bootstrap ROM | 32-byte 82S123 PROM, gated onto the bus by RESET, auto-switches out |
| CRC polynomial | x^16 + x^12 + x^5 + 1 (16-bit) |
| Board crystal | 4 MHz (Y1), divided to the FD1771's 2 MHz CLK |

Drives it was tested with: Shugart 800/801, Innovex 410, Innovex 220 (with a jumper
change), GSI 110, CDC 803 (BR803A), PerSci 270/277.

**Caution relevant to emulation of a full machine:** all RAM used by the disk
operating system must have **no wait states** (the transfer loop is timing-critical),
and the boot code and any driver must run in memory above 00FF hex if it is not the
sector-load target.

---

## 2. I/O Port Map

The board decodes **8 consecutive I/O ports**. The upper 5 address bits (A3..A7) are
matched by a 6-bit comparator (DM8131 U25) against DIP switch S1 positions 1-5; the
low 3 bits (A0,A1,A2) select the function. With **all address switches OFF the base
address is F8 hex** (upper five bits all high). All firmware in this manual assumes
the F8 base.

Port offsets from the base (base = F8 by default):

| Port (F8 base) | Offset (A2 A1 A0) | OUT (write) | IN (read) |
|---|---|---|---|
| **F8** | 0 0 0 | Command register → FD1771 | Status register ← FD1771 |
| **F9** | 0 0 1 | Track register → FD1771 | Track register ← FD1771 |
| **FA** | 0 1 0 | Sector register → FD1771 | Sector register ← FD1771 |
| **FB** | 0 1 1 | Data register → FD1771 | Data register ← FD1771 |
| **FC** | 1 0 0 | **Extended command** (see 2.2) | **Wait / drive-status** (see 2.1) |
| FD | 1 0 1 | not used | not used |
| FE | 1 1 0 | not used | not used |
| FF | 1 1 1 | not used | not used |

For ports F8-FB (A2=0) the board simply asserts the 1771's chip-select (CS*); A0/A1
are passed straight to the 1771, which does the register decode internally (see the
1771 register table in section 4). Reads are gated when PDBIN and SINP* are both true;
writes when PWR* and SOUT are both true.

Standard driver EQUates used throughout the manual:

```
DCOM  EQU 0F8H   ; 1771 command port (write)
DSTAT EQU 0F8H   ; 1771 status port (read)
TRACK EQU 0F9H   ; 1771 track register port
SECT  EQU 0FAH   ; 1771 sector register port
DDATA EQU 0FBH   ; 1771 data register port
WAIT  EQU 0FCH   ; disk WAIT command (read side of FC)
DEXT  EQU 0FCH   ; disk EXTended command (write side of FC)
```

### 2.1 Port FC read — the WAIT / hardware-hold port

`IN FC` ("IN WAIT") is the heart of the interface's flow control. Because the board
is programmed-I/O, it forces the **CPU into a hardware WAIT state until the FD1771
raises either DRQ or INTRQ.** When one of them goes true the byte returned on the
data bus reflects those two lines:

- **Bit 7 = INTRQ** and **Bit 6 = DRQ** (this is how the boot/driver loops decide).
- Convention in the code: after `IN WAIT` do `ORA A` to set flags. If the sign bit
  (bit 7) is set → it was **INTRQ** (end of operation / error). If positive → it was
  **DRQ** (a data byte is ready to move).

Mechanically: DRQ and INTRQ from the 1771 drive the CPU RDY/WAIT lines through gates
U30/U57 onto bus signals PRDY or XRDY. E48 must be jumpered to the correct ready pin:
**E47 (bus pin 72, PRDY) on IMSAI, E46 (bus pin 3, XRDY) on Altair.** This lets the
1771 stall the next CPU instruction until an internal process completes.

Note: the WAIT hold means an emulator servicing these ports must present DRQ/INTRQ
timing correctly or the guest loop hangs — the guest does `IN FC` expecting the
hardware to block. In a simulator you satisfy the read as soon as your model's DRQ or
INTRQ would be asserted.

### 2.2 Port FC write — the Extended Command port

`OUT FC` ("OUT DEXT") drives a 3-to-8 line decoder (74LS138 U56) using data bits
D0,D1,D2 (active-low decode; only the top 3 outputs Y5/Y6/Y7 are used) plus a latch
(74LS175 U40) loaded from data bits D4-D7. The function is selected by the **low 3
data bits**:

| D2 D1 D0 (low bits) | U56 output | Action |
|---|---|---|
| 0 0 0 | Y7 | Pulse **RST\*** (drive master-reset line, via pad E32→E34) |
| 0 0 1 | Y6 | Pulse **SO\*** (fast step-out; via E14→E13, faster than the 1771 can step directly) |
| 0 1 0 | Y5 | Strobe **data bits 4,5,6,7 into latch U40** (drive-select and control latch) |

Latch U40 bit meanings (loaded when low bits = 010):

- **Bit 4 (E31):** drive select 0/1 under software control (LSB of latch; selects drive 0 or 1 when tied to the input multiplexer control E29).
- **Bit 3 (E33):** RST* hold — can make the drive reset line stay high or low.
- **Bit 2 (E42/E52):** used for HLD3 or "full decoder" drive select, jumper-dependent.

**Drive-select example** (manual's SELECT routine, selecting 1 of up to 4 drives):

```
SELECT: MOV A,C      ; disk number in C
        ANI 3        ; keep 2 LSBs
        RAL          ; shift disk number into bits 4 & 5
        RAL
        RAL
        RAL
        ORI 2        ; low bits = 010 -> strobe latch
        OUT DEXT     ; OUT FC : loads drive select into latch U40
```

So the software-visible model of `OUT FC`:
- low bits 000 → reset the selected drive
- low bits 001 → issue a fast step-out pulse
- low bits 010 → latch bits 4-7 (drive-select, control lines)

---

## 3. DIP Switch S1 (7 positions)

| Position | Function |
|---|---|
| 1 | Address bit (with 2-5, sets the upper 5 bits A3..A7 of the base I/O address) |
| 2 | Address bit |
| 3 | Address bit |
| 4 | Address bit |
| 5 | Address bit |
| 6 | **Write inhibit / write-protect the whole board.** When ON (in protect position) the write-gate can never go active regardless of any program command. When OFF, writes are enabled. |
| 7 | **Bootstrap enable.** When ON, a RESET gates the 32-byte PROM bootstrap onto the bus. When OFF, the PROM bootstrap is disabled. |

- **All switches OFF → base address F8, write enabled, bootstrap disabled.**
- The bootstrap can also be suppressed at runtime without touching switch 7 by
  setting front-panel data switch 5 up and doing an EXAMINE.
- Address-decode sanity (from checkout, base F8): U27 pin 6 / pin 8 states —
  F8 status/command: pin6 Low / pin8 High; F9 track: Low/High; FA sector: Low/High;
  FB data: Low/High; **FC wait/control: pin6 High / pin8 Low**; FD/FE/FF: High/High.

---

## 4. FD1771 Register Set (as wired on this board)

The 1771 has five program-visible 8-bit registers selected by A1,A0 under RE (read)
or WE (write). On the Tarbell board these map to ports F8-FB:

| A1 A0 | Port | READ (RE) | WRITE (WE) |
|---|---|---|---|
| 0 0 | F8 | Status Register | Command Register |
| 0 1 | F9 | Track Register | Track Register |
| 1 0 | FA | Sector Register | Sector Register |
| 1 1 | FB | Data Register | Data Register |

Register notes for a model:

- **Data Shift Register** (internal, not addressable): assembles/serializes disk bytes.
- **Data Register (DR):** holding register for read/write bytes; also holds the target
  track for a Seek. Do not load while BUSY.
- **Track Register (TR):** current head track. Incremented on step-in (toward 76),
  decremented on step-out (toward 00). Compared against the ID field's track number
  during read/write/verify. Do not load while BUSY.
- **Sector Register (SR):** desired sector; compared against the ID field's sector
  number. Loadable/readable; do not load while BUSY.
- **Command Register (CR):** the command being executed. Loading it while BUSY is
  ignored *unless* you intend to override (which raises an interrupt). Write-only.
- **Status Register (STR):** device status; contents depend on the last command type.
  Read-only. Reading it (or loading the command register) resets INTRQ.

The FD1771 requires a free-running **2 MHz ±1%** clock on CLK (pin 24). (1 MHz for
mini-floppy — not used here.) All data-sheet timings double at 1 MHz.

---

## 5. FD1771 Command Set

Commands are written to port F8 while BUSY (status bit 0) is off. Eleven commands in
four types. **The command byte is shown in true form** (bit 7 = MSB).

| Type | Command | 7 6 5 4 3 2 1 0 |
|---|---|---|
| I | Restore (seek track 0) | 0 0 0 0 h V r1 r0 |
| I | Seek | 0 0 0 1 h V r1 r0 |
| I | Step | 0 0 1 u h V r1 r0 |
| I | Step-In | 0 1 0 u h V r1 r0 |
| I | Step-Out | 0 1 1 u h V r1 r0 |
| II | Read Command (read sector) | 1 0 0 m b E 0 0 |
| II | Write Command (write sector) | 1 0 1 m b E a1 a0 |
| III | Read Address | 1 1 0 0 0 0 0 0 |
| III | Read Track | 1 1 1 0 0 1 0 s̄ |
| III | Write Track | 1 1 1 1 0 1 0 0 |
| IV | Force Interrupt | 1 1 0 1 i3 i2 i1 i0 |

### 5.1 Command flag bits

**Type I flags:**
- **r1 r0 (bits 1,0):** stepping-motor rate (see table below).
- **h (bit 3, Head Load):** 1 = load head at start of command; 0 = do not.
- **V (bit 2, Verify):** 1 = verify destination track (read an ID field, compare track,
  check CRC) after settling; 0 = no verify.
- **u (bit 4, Update):** 1 = update Track Register on each step; 0 = no update.
  (Restore/Seek always update TR; Step/Step-In/Step-Out use u.)

**Type I stepping rates (Table 1), FD1771-01:**

| r1 r0 | CLK=2 MHz | CLK=1 MHz |
|---|---|---|
| 0 0 | 6 ms | 12 ms |
| 0 1 | 6 ms | 12 ms |
| 1 0 | 10 ms | 20 ms |
| 1 1 | 20 ms | 40 ms |

After the last step there is an additional fixed **10 ms head-settling delay**.

**Type II flags (Read/Write sector):**
- **m (bit 4, Multiple):** 0 = single record; 1 = multiple records (auto-increment SR
  until it exceeds sectors on track or a Force Interrupt).
- **b (bit 3, Block length):** 1 = IBM format (128 to 1024 bytes, per sector-length
  field); 0 = non-IBM (16 to 4096 bytes).
- **E (bit 2, Enable HLD/settle):** 1 = load head, sample HLT after 10 ms delay
  (normal case); 0 = head assumed engaged, no 10 ms delay.
- **a1 a0 (bits 1,0 — Write only):** Data Address Mark to write:

| a1 a0 | Data Mark (hex) | Clock Mark (hex) |
|---|---|---|
| 0 0 | FB (normal Data Mark) | C7 |
| 0 1 | FA (user defined) | C7 |
| 1 0 | F9 (user defined) | C7 |
| 1 1 | F8 (Deleted Data Mark) | C7 |

Sector length encoding (b=1, IBM):

| Sector-length field (hex) | Bytes/sector |
|---|---|
| 00 | 128 |
| 01 | 256 |
| 02 | 512 |
| 03 | 1024 |

For b=0 (non-IBM): bytes = (sector-length field) × 16; 01→16, 02→32, … FF→4080, 00→4096.

**Type III flags:**
- **s̄ (bit 0 of Read Track):** synchronize flag. s̄=0 → synchronize to address marks;
  s̄=1 → do not synchronize.

**Type IV — Force Interrupt (bits 3-0 = interrupt conditions):**
- i0=1: Not-Ready → Ready transition
- i1=1: Ready → Not-Ready transition
- i2=1: every Index Pulse
- i3=1: immediate interrupt
- If i3..i0 = 0: no interrupt is generated, but the current command is terminated and
  BUSY is reset.

### 5.2 Command behavior notes for a model

- **Restore:** samples TR00. If already at track 0, TR←0 and INTRQ. Else steps out at
  rate r1r0 until TR00 asserts (or terminates + sets Seek Error after 255 pulses).
  Executed automatically when MR (master reset) goes inactive-to-active.
- **Seek:** DR holds desired track; TR holds current. Steps in the correct direction
  until TR == DR; optional verify if V=1; INTRQ at completion.
- **Step / Step-In / Step-Out:** one pulse; direction as noted; TR updated if u=1;
  verify if V=1; INTRQ at completion.
- **Read/Write sector:** for Read/Write the 1771 samples READY; if not ready the
  command is not executed and INTRQ is generated. Seek/Step run regardless of READY.
  The DR must be serviced within one byte-time or the **Lost Data** bit is set (reads
  drop the byte; writes substitute a byte of zeros).
- The Data Address Mark of a Read must be found within 28 bytes of the ID field or
  **Record Not Found** is set.
- **Read Address:** reads the six ID-field bytes into DR (with a DRQ per byte):
  Track Addr, Zeros, Sector Addr, Sector Length, CRC1, CRC2. The sector-address byte
  is copied into the Sector Register. CRC is checked.
- **Force Interrupt** may be loaded at any time (the exception to "don't load CR while
  BUSY").

---

## 6. FD1771 Status Register

The bit meanings depend on the last command type (Table 6):

| Bit | All Type I | Read Address | Read (sector) | Read Track | Write (sector) | Write Track |
|---|---|---|---|---|---|---|
| S7 | Not Ready | Not Ready | Not Ready | Not Ready | Not Ready | Not Ready |
| S6 | Write Protect | 0 | Record Type | 0 | Write Protect | Write Protect |
| S5 | Head Engaged | 0 | Record Type | 0 | Write Fault | Write Fault |
| S4 | Seek Error | ID Not Found | Record Not Found | 0 | Record Not Found | 0 |
| S3 | CRC Error | CRC Error | CRC Error | 0 | CRC Error | 0 |
| S2 | Track 0 | Lost Data | Lost Data | Lost Data | Lost Data | Lost Data |
| S1 | Index | DRQ | DRQ | DRQ | DRQ | DRQ |
| S0 | Busy | Busy | Busy | Busy | Busy | Busy |

### 6.1 Status bit detail

**Type I:**
- **S7 Not Ready** — inverted READY input, OR'd with MR. Set = drive not ready.
- **S6 Protected** — inverted WPRT input. Set = write protect active.
- **S5 Head Loaded** — logical AND of HLD and HLT. Set = head loaded & engaged.
- **S4 Seek Error** — set if destination track not verified; reset when updated.
- **S3 CRC Error** — set on one or more CRC errors during verify; reset when updated.
- **S2 Track 00** — inverted TR00 input. Set = head over track 0.
- **S1 Index** — inverted IP input. Set = index mark detected.
- **S0 Busy** — set while a command is in progress.

**Type II / III:**
- **S7 Not Ready** — as above; Type II/III commands won't execute unless ready.
- **S6 Record Type / Write Protect** — On read-sector: MSB of the record-type code
  from the data-field address mark. On any Write Track: Write Protect. Reset when updated.
- **S5 Record Type / Write Fault** — On read-sector: LSB of the record-type code.
  On Write: Write Fault. Reset when updated.
- **S4 Record Not Found** — desired track+sector not found; reset when updated.
- **S3 CRC Error** — if S4 set, error in an ID field; else error in the data field.
- **S2 Lost Data** — computer did not service DRQ within one byte-time.
- **S1 Data Request** — copy of DRQ (DR full on read / empty on write).
- **S0 Busy** — set while executing.

Record-type code reported on a Read (status S5=bit5, S6=bit6) vs. the data-field
address mark encountered:

| S6 (bit 5 in read-sector text) | S5 | Data AM (hex) |
|---|---|---|
| 0 | 0 | FB |
| 0 | 1 | FA |
| 1 | 0 | F9 |
| 1 | 1 | F8 |

(The data sheet lists these as "Status Bit 5 / Status Bit 6"; the F8 mark is the
Deleted Data Mark.)

INTRQ is asserted at the completion or abnormal termination of any command, and
**remains asserted until the Status Register is read or the Command Register is loaded.**

---

## 7. The 32-byte Hardware Bootstrap (82S123 PROM)

Enabled when S1 position 5/7 is on and RESET is pushed. It reads the **first sector
of track 0** into memory starting at 0000 hex, then jumps to 0000 to execute it (that
loaded 128-byte module is a larger loader that pulls in the OS). Special bus hardware
lets the board read from the PROM while writing into RAM at 0000, so the loaded sector
does not clobber the still-running bootstrap. On any disk error it HALTs (retry via
RESET). The upper five bits of every I/O instruction are hard-coded to match the
default DIP setting (base F8); a different base requires reprogramming the PROM.

Authoritative disassembly (from section 6-1):

```
ADDR MACH    LABEL   ASM              COMMENT
0000 DB FC   BOOT:   IN  WAIT         ; wait for home (INTRQ / track 0)
0002 AF              XRA A            ; A = 0
0003 6F              MOV L,A          ; L = 0
0004 67              MOV H,A          ; H = 0  (HL = 0000, load target)
0005 3C              INR A            ; A = 1
0006 D3 FA           OUT SECT         ; sector register = 1
0008 3E 8C           MVI A,8CH        ; read command, single record, IBM, head load
000A D3 F8           OUT DCOM         ; issue read
000C DB FC   RLOOP:  IN  WAIT         ; wait for DRQ or INTRQ
000E B7              ORA A            ; set flags
000F F2 19 00        JP  RDONE        ; bit7=0 was DRQ? (JP if positive = INTRQ path)
0012 DB FB           IN  DDATA        ; read a data byte
0014 77              MOV M,A          ; store to memory
0015 23              INX H            ; bump pointer
0016 C3 0C 00        JMP RLOOP        ; loop
0019 DB F8   RDONE:  IN  DSTAT        ; read disk status
001B B7              ORA A            ; set flags
001C CA 7D 00        JZ  007DH        ; if status 0 (no error) jump to loaded code (007D)
001F 76              HLT              ; disk error -> halt
```

(Byte image, base F8, from the checkout self-check:
`0000: DB FC AF 6F 67 3C D3 FA 3E 8C D3 F8 DB FC B7 F2`
`0010: 19 00 DB FB 77 23 C3 0C 00 DB F8 B7 CA 7D 00 76`.)

Note the read command byte **8C** = `1000 1100`: type II read (bits 7-5 = 100),
m=0 single record, b=1 IBM 128-byte, E=1 head-load-with-delay, a bits 0.

The `JP RDONE` uses the fact that `IN FC` returns INTRQ in bit 7 and DRQ in bit 6:
sign-positive means the byte-ready (DRQ) path was NOT the interrupt, so it actually
falls through to read the byte; on INTRQ (bit7=1, sign negative) it does NOT jump —
i.e. the loop reads bytes on DRQ and exits to RDONE on INTRQ. (In the code as written,
`JP` = jump-if-positive is taken when bit 7 is clear; the loop structure exits when
the read operation completes and INTRQ fires.)

---

## 8. Sample Driver Routines (from section 6)

These use the EQUates in section 2. They are the canonical programming sequences an
emulator's guest software will exercise.

### 8.1 Home the head (Restore to track 0)

```
HOME:  MVI A,0D0H     ; force-interrupt: clear any pending command
       OUT DCOM
HOME1: IN  DSTAT      ; read status
       RRC            ; test Busy (LSB)
       JC  HOME1      ; wait for not busy
       MVI A,3        ; Restore command with rate bits = 11
       OUT DCOM       ; issue restore/home
       IN  WAIT       ; wait for INTRQ (end of op)
       ORA A          ; set flags
       MVI A,1        ; preset error indicator
       JM  ERROR      ; if DRQ instead of INTRQ -> error
       IN  DSTAT      ; read status
       MOV E,A
       ANI 4          ; test Track 0 (bit 2)
       JZ  HERR       ; not at track 0 -> error
       MOV A,E
       ANI 91H        ; mask non-error bits
       RET            ; return if no error
HERR:  MVI A,1        ; set hardware error
       ORA A
       RET
```

Note the **0D0 force-interrupt** to clear a pending command, and the `MVI A,3` Restore
(the low bits 11 select the 20 ms step rate; head-load/verify flags zero here).

### 8.2 Read one sector (modified boot)

Uses RAMADD (any RAM address in HL) as the target. Loads sector register, issues read
command 8C, loops on `IN WAIT`: on DRQ read DDATA→memory, on INTRQ read the status word
and stop.

```
        LXI H,RAMADD   ; target
        MVI A,01H
        OUT SECT       ; sector 1
        MVI A,8CH
        OUT DCOM       ; read, single, IBM 128, head-load
RLOOP:  IN  WAIT
        ORA A
        JP  RDONE      ; INTRQ -> done
        IN  DDATA      ; DRQ -> read byte
        MOV M,A
        INX H
        JMP RLOOP
RDONE:  IN  STAT       ; STAT = 0F8H
        ORA A
        STA $+5        ; save status
        HLT
```

On a successful read of a freshly formatted IBM diskette, the target and next 127
bytes contain the fill byte (**IBM uses E5 hex**).

### 8.3 Write one sector

```
        LXI H,RAMADD
        MVI A,01
        OUT SECT       ; sector 1
        MVI A,0ACH     ; write command (type II write, IBM 128, head-load)
        OUT DCOM
WLOOP:  IN  WAIT
        ORA A
        JP  DONE       ; INTRQ -> done
        MOV A,M
        OUT DDATA      ; DRQ -> write byte
        INX H
        JMP WLOOP
DONE:   IN  STAT
        ANI 0FDH       ; mask non-error bits (drops DRQ)
        STA $+4
        HLT
```

Write command **AC** = `1010 1100`: type II write (bits 7-5 = 101), m=0, b=1 IBM 128,
E=1 head-load, a1a0=00 → Data Mark FB.

---

## 9. Disk Format (IBM 3740, single density, 128 bytes/sector)

Track geometry the 1771 formats via a Write Track command (per track, 26 sectors):

Preamble / per-track:
| Count | Byte |
|---|---|
| 40 | 00 or FF |
| 6 | 00 |
| 1 | FC (Index Address Mark) |
| 26 | 00 or FF |

Per sector, the following bracketed field is written **26 times**:
| Count | Byte |
|---|---|
| 6 | 00 |
| 1 | FE (ID Address Mark) |
| 1 | Track Number (0 thru 4C hex = 0..76) |
| 1 | 00 |
| 1 | Sector Number (1 thru 1A hex = 1..26) |
| 1 | 00 |
| 1 | F7 (writes 2 CRC bytes) |
| 11 | 00 or FF |
| 6 | 00 |
| 1 | FB (Data Address Mark) |
| 128 | Data (IBM fill = E5 hex) |
| 1 | F7 (writes 2 CRC bytes) |
| 27 | 00 or FF |

Post-amble:
| Count | Byte |
|---|---|
| ~247 | 00 or FF (until the FD1771 interrupts out) |

Special bytes recognized in the outgoing Write-Track stream (Control Bytes for
Initialization):

| Data byte (hex) | Interpretation | Clock mark (hex) |
|---|---|---|
| F7 | Write 2 CRC chars | FF |
| F8 | Data Address Mark | C7 |
| F9 | Data Address Mark | C7 |
| FA | Data Address Mark | C7 |
| FB | Data Address Mark | C7 |
| FC | Index Address Mark | D7 |
| FD | Spare | — |
| FE | ID Address Mark | C7 |

Consequently **byte patterns F7-FE must never appear in gaps, data fields, or ID
fields** except where an address mark or CRC is intended; a single F7 pattern generates
two CRC characters. The CRC generator is initialized (preset to all ones) whenever any
byte F8-FE is about to be transferred to the shift register.

ID field on-disk layout (Read Address returns bytes 1,3,4,5,6 as Track/Sector/Length/CRC):

`ID AM | Track# | Zeros | Sector# | Sector-Length | CRC1 | CRC2`

Data field: `Data AM | (128×2^n data bytes) | CRC1 | CRC2`.

**Non-IBM format:** same structure but the sector-length byte in the ID field is
interpreted as (n×16) bytes, allowing 16-4096 byte sectors (see b=0). GAP2 (between ID
and data field) and the data-field gap must be 17 bytes with the last 6 = zero; every
address mark must be preceded by at least one zero byte. The index address mark need
not be present for the FD1771.

Track format landmarks (Figure 13): physical index; ~46 bytes to the Index Address
Mark; GAP4 (pre-index) ~320 bytes nominal; GAP1 (post-index) 32 bytes; GAP2 (ID→data)
17 bytes (11 bytes of 00/FF + 6 bytes of 00, write-gate turn-on point after the 6th);
GAP3 (data→next ID) 33 bytes (1 byte + 32 bytes, write-gate turn-off point).

---

## 10. FD1771 Pinout (reference for signal semantics)

Computer-side and disk-side pins that affect the software/behavioral model:

| Pin | Name | Notes |
|---|---|---|
| 1 | VBB | -5V |
| 2 | WE (Write Enable) | active low; gates DAL into selected register when CS low |
| 3 | CS (Chip Select) | active low |
| 4 | RE (Read Enable) | active low; places selected register on DAL when CS low |
| 5,6 | A0, A1 | register select |
| 7-14 | DAL0-DAL7 | **inverted** bidirectional data bus |
| 15 | PH1/STEP | step pulse (4 µs) in step-direction mode |
| 16 | PH2/DIRC | direction (high=step-in, low=step-out) |
| 17 | PH3 | phase 3 (three-phase motor mode) |
| 18 | 3PM | low = three-phase motor; high/open = step-direction |
| 19 | MR (Master Reset) | active low; resets device, clears command register, resets Not Ready during MR; a Restore runs when MR returns high |
| 20 | VSS | ground |
| 21 | VCC | +5V |
| 22 | TEST | tie +5V or open for normal use |
| 23 | HLT (Head Load Timing) | sampled 10 ms after HLD; high = head engaged |
| 24 | CLK | free-running 2 MHz ±1% |
| 25 | XTDS | low = external data separator (grounded on this board disables internal separator) |
| 26 | FDCLOCK | externally separated clock in (when XTDS=0) |
| 27 | FDDATA | raw read data (XTDS=1) or separated data (XTDS=0) |
| 28 | HLD (Head Load) | active high = load head |
| 29 | TG43 (Track > 43) | high when head is on tracks 44-76 (valid only during read/write) |
| 30 | WG (Write Gate) | active when writing |
| 31 | WD (Write Data) | combined clock+data, 500 ns pulses |
| 32 | READY | high = drive ready (sampled before read/write; appears inverted as status S7) |
| 33 | WF (Write Fault) | low with WG=1 terminates write, sets Write Fault status |
| 34 | TR00 (Track 00) | low = head on track 0 |
| 35 | IP (Index Pulse) | low ≥10 µs = index mark |
| 36 | WPRT (Write Protect) | low when a write command is received terminates it, sets protect status |
| 37 | DINT (Disk Initialization) | sampled on Write Track; if low, terminates & sets protect status |
| 38 | DRQ (Data Request) | open drain; DR full (read) / empty (write); 10K pull-up to +5 |
| 39 | INTRQ (Interrupt Request) | open drain; set at command completion; reset by reading status or loading command; 10K pull-up to +5 |
| 40 | VDD | +12V |

Board timing/hardware wrapped around the 1771:

- **Head-load delay:** U41/U57 sample HLD and add the drive's physical head-load delay
  before passing it to the 1771's HLT input (10 ms check after HLD active).
- **Step-pulse stretch:** the 1771 step output (pin 15, 4 µs) is stretched by one-shot
  U51 for drives needing a longer pulse. Fast-step drives (e.g. PerSci) can be pulsed
  from the extended-command port (E14) faster than the 1771 steps directly.
- **TG43:** from pin 29, optionally inverted (U35) for drives wanting opposite polarity;
  tells the drive to reduce write current on tracks > 43. Jumper E49-E50 sets active-low.
- **Write gate** can only go active when S1 position 6 (write inhibit) is OFF.
- **Clock/data separator:** U1,U2,U17,U33,U34,U35,U36 form the separator from the 4 MHz
  crystal Y1 (divided to the 1771's 2 MHz). Grounding pin 25 (XTDS) disables the 1771's
  internal separator in favor of the board's.

---

## 11. Emulation Checklist (quick summary)

1. Ports **F8-FB** are the FD1771 command/status, track, sector, data registers.
2. Port **FC read** = wait-for-DRQ/INTRQ; return **bit7=INTRQ, bit6=DRQ**. The read
   blocks until one is asserted (model it as immediate once your DRQ/INTRQ is true).
3. Port **FC write** = extended command: low bits 000=reset drive, 001=fast step-out,
   010=latch bits4-7 (drive select / control).
4. Base address default **F8**; upper 5 bits from DIP S1 1-5; pos 6 = write inhibit;
   pos 7 = bootstrap enable.
5. Model the 1771 command set (Type I-IV), the type-dependent status register, the
   DRQ/INTRQ handshake (one byte per DRQ, Lost Data if not serviced in one byte-time),
   and the IBM 3740 geometry (77 tracks 0-76, 26 sectors 1-26, 128 bytes, fill E5).
6. INTRQ clears on status read or command write. Force Interrupt (0Dx) is the only
   command loadable while BUSY.
7. Bootstrap: reads track 0 sector 1 (128 bytes) to 0000, jumps to 007D on success,
   HALTs on error. Read command used is **8C**; write is **AC**.
8. CRC polynomial x^16+x^12+x^5+1 (only matters if modeling raw track data / format).
9. 250 kbit/s single density; NOT DMA — everything is programmed I/O through the WAIT
   port.
