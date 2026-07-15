# Western Digital FD1771-01 Floppy Disk Formatter/Controller

Source: [Western Digital FD1771 - Datasheet.pdf](#)

MOS/LSI single-chip floppy disk controller/formatter. Compatible with the IBM
3740 (soft-sector) data-entry format. 8-bit bidirectional bus for data, status,
and command words; supports DMA or programmed I/O. Internal timing derived from
a free-running 2.0 MHz clock (1.0 MHz for mini-floppy). This document captures
everything needed to emulate the chip in software; marketing copy, package
mechanicals, and ordering information are omitted.

---

## 1. Register Model

The processor sees five 8-bit registers. Which one is accessed is selected by
address lines A1, A0 together with the read/write strobe. The register read
(via RE) differs from the register written (via WE) at the same address:

| A1 | A0 | Read (RE low) | Write (WE low) |
|----|----|---------------|----------------|
| 0  | 0  | Status Register (STR) | Command Register (CR) |
| 0  | 1  | Track Register (TR)   | Track Register (TR)  |
| 1  | 0  | Sector Register (SR)  | Sector Register (SR) |
| 1  | 1  | Data Register (DR)    | Data Register (DR)   |

- **Command Register (CR)** — write-only from the DAL side; holds the command
  being executed. Do not load while Busy (Status bit 0 = 1) *except* Force
  Interrupt, which overrides a running command (and itself causes an interrupt).
  Cannot be read back.
- **Status Register (STR)** — read-only from the DAL side; cannot be loaded.
  Bit meanings depend on the last command type (see Section 6).
- **Track Register (TR)** — holds current head track position. Auto-incremented
  when the head steps toward track 76, decremented when stepping toward track 0.
  Compared against the ID-field track number on Read/Write/Verify. Do not load
  while busy.
- **Sector Register (SR)** — holds the desired sector number; compared against
  the ID-field sector number on Read/Write. Do not load while busy.
- **Data Register (DR)** — holding register. On disk read, the assembled byte is
  transferred here from the Data Shift Register (DSR). On disk write, data goes
  from here into the DSR. During a Seek, the DR holds the desired (destination)
  track number. Do not load while busy.

Supporting internal blocks (not directly addressable):
- **Data Shift Register (DSR)** — assembles serial read data / serializes write
  data.
- **ALU** — serial comparator/incrementer/decrementer for register compare &
  modify.
- **AM Detector** — detects ID, Data, and Index address marks.
- **CRC Logic** — 16-bit CRC using polynomial **G(x) = x^16 + x^12 + x^5 + 1**.
  CRC covers everything from the address mark through the CRC bytes. The CRC
  register is **preset to all ones** before data is shifted through.

### Reset behavior

A logic low on **MASTER RESET (MR)** resets the device and loads **0x03** into
the Command Register. The Not Ready status bit (bit 7) is reset while MR is
active. When MR returns high, a **Restore command is executed** regardless of
the drive's Ready state. Phase 1 (PH1) is active low after MR.

---

## 2. Command Set

Eleven commands, grouped into four types. Load commands only when Busy (bit 0)
is off, except Force Interrupt. Completing a command sets INTRQ and resets Busy.
Bits shown in TRUE form.

### Table 2 — Command Summary

| Type | Command | b7 | b6 | b5 | b4 | b3 | b2 | b1 | b0 |
|------|---------|----|----|----|----|----|----|----|----|
| I   | Restore (Seek Track 0) | 0 | 0 | 0 | 0 | h | V | r1 | r0 |
| I   | Seek        | 0 | 0 | 0 | 1 | h | V | r1 | r0 |
| I   | Step        | 0 | 0 | 1 | u | h | V | r1 | r0 |
| I   | Step In     | 0 | 1 | 0 | u | h | V | r1 | r0 |
| I   | Step Out    | 0 | 1 | 1 | u | h | V | r1 | r0 |
| II  | Read Command (Read Sector)  | 1 | 0 | 0 | m | b | E | 0 | 0 |
| II  | Write Command (Write Sector)| 1 | 0 | 1 | m | b | E | a1 | a0 |
| III | Read Address | 1 | 1 | 0 | 0 | 0 | E | 0 | 0 |
| III | Read Track   | 1 | 1 | 1 | 0 | 0 | 1 | 0 | s̄ |
| III | Write Track  | 1 | 1 | 1 | 1 | 0 | 1 | 0 | 0 |
| IV  | Force Interrupt | 1 | 1 | 0 | 1 | i3 | i2 | i1 | i0 |

Note: the datasheet's Force Interrupt row lists the interrupt bits as
i3 i2 i1 i0 in bit positions 3..0 (see Table 5 / Type IV below). The MR-loaded
default command 0x03 is a Restore with r1r0 = 11.

### Table 3 — Type I flags

| Flag | Bit | Meaning |
|------|-----|---------|
| h  | 3 | Head Load: 1 = load head at beginning of command; 0 = do not load head at beginning |
| V  | 2 | Verify: 1 = verify on destination track; 0 = no verify |
| r1 r0 | 1–0 | Stepping motor rate — see Table 1 |
| u  | 4 | Update: 1 = update Track register per step; 0 = no update (Step / Step-In / Step-Out only) |

### Table 4 — Type II flags

| Flag | Bit(s) | Meaning |
|------|--------|---------|
| m | 4 | Multiple Record: 0 = single record; 1 = multiple records |
| b | 3 | Block length: 1 = IBM format (128–1024 bytes); 0 = non-IBM (16–4096 bytes) |
| E | 1 (see note) | Enable HLD + 10 ms delay (see Table 5 note) |
| a1 a0 | 1–0 | Data Address Mark for Write (see below) |

Type II Data Address Mark (a1 a0), used by Write Sector:

| a1 | a0 | Mark | Hex |
|----|----|------|-----|
| 0 | 0 | Data Mark (FB)          | FB |
| 0 | 1 | User-defined (FA)       | FA |
| 1 | 0 | User-defined (F9)       | F9 |
| 1 | 1 | Deleted Data Mark (F8)  | F8 |

All of the above are written with clock mark **C7**.

### Table 5 — Type III & Type IV flags

Type III:

| Flag | Bit | Meaning |
|------|-----|---------|
| s̄ | 0 | Synchronize: s̄=0 → synchronize to AM; s̄=1 → do not synchronize to AM (Read Track only) |

Type IV — Force Interrupt condition flags (bits 3–0). Any combination may be
set; the interrupt fires when the corresponding condition occurs:

| Flag | Meaning |
|------|---------|
| i0 = 1 | Not-Ready-to-Ready transition |
| i1 = 1 | Ready-to-Not-Ready transition |
| i2 = 1 | Every Index Pulse |
| i3 = 1 | Immediate interrupt (requires reset — see note) |

The **E** flag (Type II/III):
- E = 1 → Enable HLD, HLT, and the 10 ms head-settle delay.
- E = 0 → Head assumed already engaged; no 10 ms delay.

**Force Interrupt notes:** Force Interrupt (0xD_) can be loaded into the command
register at any time. If a command is executing (Busy set), it is terminated and
an interrupt is generated when the specified i0–i3 condition is met. If
i0–i3 = 0 (i.e. 0xD0), **no interrupt is generated**, but the current command is
terminated and Busy is reset. Force Interrupt is the **only** command that clears
an immediate interrupt (i3).

---

## 3. Type I Commands — Head Positioning (Restore, Seek, Step, Step-In, Step-Out)

General flow on receiving a Type I command:
1. Set Busy; reset CRC Error, Seek Error, DRQ, INTRQ.
2. If h = 1, set HLD (load head); if h = 0, reset HLD.
3. Perform the stepping (command-specific, below).
4. Each positioning step's period is set by r1r0 (Table 1). After the **last**
   step there is an additional **10 ms head-settle** delay.
5. If V = 1, perform verification (below).
6. Generate INTRQ, reset Busy.

### Table 1 — Stepping Rates

| r1 | r0 | 1771-X1, 2 MHz, TEST=1 | 1771-X1, 1 MHz, TEST=1 | 1771/-X1, 2 MHz, TEST=0 | 1771/-X1, 1 MHz, TEST=0 |
|----|----|------------------------|------------------------|-------------------------|-------------------------|
| 0 | 0 | 6 ms  | 12 ms | approx 400 µs | approx 600 µs |
| 0 | 1 | 6 ms  | 12 ms | approx 400 µs | approx 600 µs |
| 1 | 0 | 10 ms | 20 ms | approx 400 µs | approx 600 µs |
| 1 | 1 | 20 ms | 40 ms | approx 400 µs | approx 600 µs |

(TEST is a test-only pin, normally high/open. The "TEST=0" columns are test
mode. For normal emulation use the TEST=1 columns: 6/6/10/20 ms at 2 MHz,
doubled at 1 MHz.)

### Verification (V = 1)

Begins at the end of the 10 ms settle after the head is loaded. The first
encountered ID field's track number is compared against the Track Register:
- Match + valid ID CRC → verify complete, INTRQ, Busy reset.
- No match but valid CRC → Seek Error (bit 4) set, INTRQ, Busy reset.
- Match but invalid CRC → CRC Error (bit 3) set; continue to next ID field.
- No valid-CRC ID field found within **two disk revolutions** → terminate,
  INTRQ.

During verify the head is loaded and, after an internal 10 ms delay, HLT is
sampled; when HLT is true the first ID field is read.

### Restore (Seek Track 0), opcode 0x0_

Samples TR00. If TR00 is already active (head over track 0), the Track Register
is loaded with 0 and INTRQ is generated. Otherwise step pulses are issued at the
r1r0 rate until TR00 goes active; then TR ← 0 and INTRQ. If TR00 does **not** go
active after **255 step pulses**, the operation terminates, INTRQ, and Seek Error
(bit 4) is set. Executed automatically when MR goes inactive. Verify runs if V=1;
h controls head load at start.

### Seek, opcode 0x1_

Assumes TR holds the current head position and DR holds the desired track. The
FD1771 issues step pulses in the appropriate direction, updating TR by ±1 each
step, until TR = DR. Verify if V=1. h controls head load. INTRQ at completion.

### Step, opcode 0x2_ / 0x3_ (u)

Issues one step pulse in the same direction as the previous step. Delay per
r1r0. If u=1, TR is updated. Verify if V=1. h controls head load. INTRQ at
completion.

### Step-In, opcode 0x4_ / 0x5_ (u)

Issues one step pulse toward track 76. If u=1, TR is incremented by one. Delay
per r1r0, verify if V=1, h controls head load, INTRQ at completion.

### Step-Out, opcode 0x6_ / 0x7_ (u)

Issues one step pulse toward track 0. If u=1, TR is decremented by one. Delay
per r1r0, verify if V=1, h controls head load, INTRQ at completion.

### Head Load / HLT interaction

- The HLD output loads the head against the disk. It activates at the start of a
  Read/Write (E flag on) or Verify, or a Seek/Step with h=1.
- Once loaded, the head stays engaged until the third index pulse following the
  last operation that used the head. If no command is received within two disk
  revolutions, the head is auto-disengaged.
- Reading/Writing does not occur until at least 10 ms after HLD is made active.
  The delay is judged by sampling HLT after 10 ms: a high on HLT means the head
  is engaged.
- Executing two Type I commands with the E flag off gives no 10 ms delay — the
  head is assumed engaged.

---

## 4. Type II Commands — Read Sector / Write Sector

Before loading a Type II command, load the Sector Register with the desired
sector number. On receipt, Busy is set. If E=1 (normal), HLD is made active and
HLT is sampled after a 10 ms delay; if E=0, the head is assumed engaged (no
10 ms delay).

**ID-field search:** the FD1771 reads each ID field, comparing its track number
against TR. On a track match it compares the ID's sector number against SR. On a
sector match with a correct ID CRC, the data field is then read or written. A
matching ID (track, sector, CRC) must be found within **two disk revolutions**,
otherwise **Record Not Found** (Status bit 4) is set and the command terminates
with an interrupt.

**Sector length (b flag + sector-length field of the ID):**

For **b = 1** (IBM):

| Sector Length Field (hex) | Bytes in Sector |
|----|------|
| 00 | 128  |
| 01 | 256  |
| 02 | 512  |
| 03 | 1024 |

(IBM 3740 compatibility: number of bytes = 128 × 2^n, n = 0..3.)

For **b = 0** (non-IBM): number of bytes = length-field × 16, i.e. 16×N where
N = 1..256:

| Sector Length Field (hex) | Bytes in Sector |
|----|------|
| 01 | 16   |
| 02 | 32   |
| 03 | 48   |
| 04 | 64   |
| …  | …    |
| FF | 4080 |
| 00 | 4096 (all zeroes ⇒ 256 groups) |

**Multiple-record (m flag):**
- m = 0 → single sector read/written; INTRQ at completion.
- m = 1 → multiple records; the Sector Register is internally incremented (+1)
  so verification occurs on the next record. Continues until SR exceeds the
  number of sectors on the track, or until a Force Interrupt is loaded (which
  terminates and interrupts).

### Read Command (Read Sector)

Head loaded, Busy set. When an ID field with matching track/sector/CRC is found,
the data field is presented to the computer. The **Data Address Mark of the data
field must be found within 28 bytes** of the correct ID field (IBM) / within
16-28 bytes; if not, Record Not Found is set and the operation terminates. As
each data byte is assembled in the DSR it is transferred to the DR and DRQ is
generated. If the previous DR contents were not read before the next byte
arrives, that byte is lost and **Lost Data** (bit 2) is set (transfer
continues). At end of the data field, if there is a CRC error, **CRC Error**
(bit 3) is set and the command terminates (even for a multi-record command).

At end of read, the **type of Data Address Mark** encountered is recorded in
Status bits 5 and 6:

| Status Bit 5 | Status Bit 6 | Data AM (hex) |
|----|----|----|
| 0 | 0 | FB |
| 0 | 1 | FA |
| 1 | 0 | F9 |
| 1 | 1 | F8 |

### Write Command (Write Sector)

Head loaded (HLD active), Busy set. WPRT is sampled on receipt; if Write Protect
is active (low) the command is immediately terminated, INTRQ, and Write Protect
status bit set. When a matching ID field is found, the FD1771 counts off **11
bytes** from the ID CRC field; the Write Gate (WG) output is asserted **only if
the DRQ (first data byte) has been serviced** — i.e. the DR must be loaded
before WG activates. If the DR was not serviced, the command terminates and Lost
Data is set. Once WG is active, **six bytes of zeros** are written, then the Data
Address Mark determined by the a1a0 field (see Table 4). The data field is then
written, generating DRQs. If a DRQ is not serviced in time, Lost Data is set and
a byte of zeros is written for the missing byte (command not terminated). After
the last data byte, the two-byte CRC is computed and written, followed by one
byte gap of logic ones; WG is then deactivated.

---

## 5. Type III Commands — Read Address / Read Track / Write Track

### Read Address, opcode 0xC_

Head loaded, Busy set. The next ID field is read; its six data bytes are
transferred to the DR one at a time, each generating a DRQ. The six bytes:

| 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| Track Address | Side Number | Sector Address | Sector Length | CRC 1 | CRC 2 |

The CRC bytes are transferred to the computer, but the FD1771 also checks them
and sets **CRC Error** if invalid. The **Sector Address of the ID field is
written into the Sector Register**. INTRQ and Busy reset at end.

### Read Track, opcode 0xE_ (s̄ in bit 0)

Head loaded, Busy set. Reading begins at the leading edge of the first index
mark and continues until the next index pulse. Every assembled byte is
transferred to the DR with a DRQ. **No CRC checking is performed.** Gaps are
included in the stream. If bit 0 (s̄) = 0, byte accumulation is synchronized to
each address mark encountered. INTRQ at completion.

### Write Track (Format), opcode 0xF4

Head loaded, Busy set. Sampled on receipt: **DINT** — if DINT is grounded
(=0), the command will not execute; instead the Write Protect status bit is set
and INTRQ. WPRT is also honored. Writing begins at the leading edge of the first
index pulse and continues until the next index pulse (then INTRQ). DRQ is
asserted immediately on receiving the command, but writing does not start until
the first byte is loaded into the DR. If the DR has not been loaded by the time
the index pulse arrives, the operation terminates (device Not Busy), Lost Data
is set, and INTRQ. If a byte is missing when needed, a byte of zeros is
substituted.

**Control bytes for formatting.** Normally the byte in the DR is written with a
clock mark of 0xFF. If the FD1771 detects a data value in the range **F7–FE** in
the DR, it is instead interpreted as an address mark / CRC-generation control:

| Data Pattern (hex) | Interpretation | Clock Mark (hex) |
|----|----|----|
| F7 | Write CRC Character (generates **two** CRC bytes) | FF |
| F8 | Data Address Mark | C7 |
| F9 | Data Address Mark | C7 |
| FA | Data Address Mark | C7 |
| FB | Data Address Mark | C7 |
| FC | Index Address Mark | D7 |
| FD | Spare | — |
| FE | ID Address Mark | C7 |

The CRC generator is initialized when any data byte from F8 to FE is about to be
transferred from the DR to the DSR. One F7 pattern generates two CRC characters.
Consequently, patterns F7–FE **must not appear** in gaps, data fields, or ID
fields as literal data, and CRCs must be generated with an F7.

---

## 6. Status Register

Format: bits 7..0 = S7..S0. On receipt of any command except Force Interrupt,
Busy is set and the remaining bits are updated/cleared for the new command.
Force Interrupt during a running command resets Busy; Force Interrupt with no
running command resets Busy and updates the rest, reflecting Type I status. Bit
meanings depend on the last command type.

### Table 6 — Status Register Summary (per command)

| Bit | All Type I | Read Address | Read Sector | Read Track | Write Sector | Write Track |
|-----|-----------|--------------|-------------|-----------|--------------|-------------|
| S7 | Not Ready | Not Ready | Not Ready | Not Ready | Not Ready | Not Ready |
| S6 | Write Protect | 0 | Record Type | 0 | Write Protect | Write Protect |
| S5 | Head Engaged | 0 | Record Type | 0 | Write Fault | Write Fault |
| S4 | Seek Error | ID Not Found | Record Not Found | 0 | Record Not Found | 0 |
| S3 | CRC Error | CRC Error | CRC Error | 0 | CRC Error | 0 |
| S2 | Track 00 | Lost Data | Lost Data | Lost Data | Lost Data | Lost Data |
| S1 | Index | DRQ | DRQ | DRQ | DRQ | DRQ |
| S0 | Busy | Busy | Busy | Busy | Busy | Busy |

### Status bits — Type I commands

| Bit | Name | Meaning |
|-----|------|---------|
| S7 | Not Ready | Set = drive not ready. Inverted copy of READY input, logically OR'd with MR. |
| S6 | Protected | Set = Write Protect active. Inverted copy of WPRT input. |
| S5 | Head Loaded | Set = head loaded and engaged. Logical AND of HLD and HLT. |
| S4 | Seek Error | Set = desired track not verified. Reset to 0 when updated. |
| S3 | CRC Error | Set = one or more CRC errors on an unsuccessful track-verify. Reset when updated. |
| S2 | Track 00 | Set = head positioned at Track 0. Inverted copy of TR00 input. |
| S1 | Index | Set = index mark detected from drive. Inverted copy of IP input. |
| S0 | Busy | Set = command in progress. |

### Status bits — Type II and III commands

| Bit | Name | Meaning |
|-----|------|---------|
| S7 | Not Ready | Set = drive not ready. Inverted READY OR'd with MR. **Type II/III do not execute unless the drive is ready.** |
| S6 | Record Type / Write Protect | Read Sector: MSB of the record-type code from the data-field AM. Read Track: not used. Any Write: Write Protect. Reset when updated. |
| S5 | Record Type / Write Fault | Read Sector: LSB of the record-type code from the data-field AM. Read Track: not used. Any Write: Write Fault. Reset when updated. |
| S4 | Record Not Found | Set = desired track & sector not found. Reset when updated. |
| S3 | CRC Error | If S4 set, error was in an ID field; otherwise indicates a data-field error. Reset when updated. |
| S2 | Lost Data | Set = computer did not respond to DRQ within one byte time. Reset when updated. |
| S1 | Data Request | Copy of the DRQ output. Set = DR full (read) or DR empty (write). Reset when updated. |
| S0 | Busy | Set = command under execution. |

---

## 7. DRQ / INTRQ Behavior

**DRQ (Data Request)** — open-drain output (10K pull-up to +5). Also appears as
Status bit 1 during Read/Write.
- Read: set high when an assembled input byte is transferred to the DR. Cleared
  when the processor reads the DR. If a new byte arrives before the previous DR
  is read, Lost Data (bit 2) is set and the read continues to end of sector.
- Write: set high when the DR transfers its contents to the DSR (i.e. DR needs a
  new byte). Cleared when the processor loads the DR. If new data is not loaded
  by the time the next byte is required, a byte of zeros is written and Lost Data
  is set.

**INTRQ (Interrupt Request)** — open-drain output (10K pull-up to +5). Set on
completion or termination of any operation. Reset when a new command is loaded
into the command register **or** when the Status Register is read by the
processor. Also generated when a Force Interrupt condition is met. INTRQ remains
active until reset by one of those two actions.

---

## 8. Disk Read / Write Timing and Data Path

- External clock: 2.0 MHz ± 1% (1.0 MHz for mini-floppy). Internally divided by 4
  to form the 500 kHz clock-rate reference; on read this divider is synchronized
  to FDDATA transitions and further divided by 2 to separate clock and data bits,
  phased by detecting the address mark.
- Write Data (WD) output carries both clock and data bits, 500 ns pulse width.
  Interlaced with each data bit is a 0.5 µs clock pulse.
- WG (Write Gate) asserted when writing; as a precaution the first data byte must
  be loaded in response to the first DRQ before WG can activate.
- WF (Write Fault) input: when WG=1 and WF goes low, the current write is
  terminated and Write Fault (bit 5) is set. WF must be inactive (high) when WG is
  inactive.
- Normal IBM 3740 sector length = 128 bytes. Binary multiples (256/512/1024) via
  b=1. Variable length via b=0 (16×N, N=1..256; all-zero length ⇒ 256 groups).
- Read Data separation: internal (XTDS=1) or external (XTDS=0, via FDCLOCK/FDDATA).

### Read Operation timing (XTDS = 0), 2 MHz

| Symbol | Characteristic | Min | Max | Units |
|--------|----------------|-----|-----|-------|
| TSET | Setup ADDR and CS to RE | 100 | | ns |
| THLD | Hold ADDR and CS from RE | 10 | | ns |
| TRE | RE Pulse Width | 500 | | ns |
| TDRR | DRQ Reset from RE | | 500 | ns |
| TIRR | INTRQ Reset from RE | | 3000 | ns |
| TDACC | Data Access from RE | | 450 | ns |
| TDOH | Data Hold from RE | 50 | 150 | ns |

### Write Operation timing, 2 MHz

| Symbol | Characteristic | Min | Max | Units |
|--------|----------------|-----|-----|-------|
| TSET | Setup ADDR and CS to WE | 100 | | ns |
| THLD | Hold ADDR and CS from WE | 10 | | ns |
| TRE | WE Pulse Width | 350 | | ns |
| TDRR | DRQ Reset from WE | | 500 | ns |
| TIRR | INTRQ Reset from WE | | 3000 | ns |
| TDACC | Data Access from WE | 250 | | ns |
| TDOH | Data Hold from WE | 150 | | ns |

### Write Data timing

| Symbol | Characteristic | Min | Typ | Max | Units |
|--------|----------------|-----|-----|-----|-------|
| TWGD | Write Gate to Data | | 1200 | | ns |
| TPWW | Pulse Width Write Data | 500 | | 600 | ns |
| TCDW | Clock to Data | | 2000 | | ns |
| TCW | Clock Cycle Write | | 4000 | | ns |
| TWGH | Write Gate Hold to Data | 0 | | 100 | ns |

### Miscellaneous timing (2 MHz; doubled at 1 MHz)

| Symbol | Characteristic | Min | Typ | Units |
|--------|----------------|-----|-----|-------|
| TSTP | Step Pulse Output width | 3800 (2400 typ) | | ns |
| TDIR | Direction Setup to Step | 24 | | µs |
| TMR | Master Reset Pulse Width | 10 | | µs |
| TIP | Index Pulse Width | 10 | | µs |
| TWF | Write Fault Pulse Width | 10 | | µs |

(Step pulse ~4 µs wide; direction output valid ≥24 µs before the step pulse.)

---

## 9. Track Format (IBM 3740, 128 bytes/sector)

Overall record layout on a track:

```
GAP | ID_AM | TRACK# | ZERO | SECTOR# | SECTOR_LEN | CRC1 CRC2 | GAP | DATA_AM | DATA_FIELD | CRC1 CRC2
     \---------------------- ID FIELD ----------------------/        \-------- DATA FIELD --------/
```

- **ID Address Mark** = data 0xFE, clock 0xC7.
- **Data Address Mark** = data 0xF8/F9/FA/FB, clock 0xC7.
- **Index Address Mark** = data 0xFC, clock 0xD7.

**Gap requirements (formatting):** only two GAP-size rules must be met:
1. GAP 2 (between the ID field and data field) must be **17 bytes**, of which the
   **last 6 must be zero**.
2. Every address mark must be preceded by at least one byte of zeros.

Recommended: every GAP at least 17 bytes with 6 bytes of zeros. The FD1771 does
**not** require the Index Address Mark (data FC / clock D7) — it need not be
present.

### Write Track byte sequence — IBM 3740, 128 bytes/sector

Issue Write Track, then feed the DR these bytes (one DRQ per byte):

| Count | Byte value |
|-------|------------|
| 40 | 00 or FF |
| 6  | 00 |
| 1  | FC (Index Mark) |
| 26 | 00 or FF | ← bracketed field, written 26 times (one per sector) |
| 6  | 00 |
| 1  | FE (ID Address Mark) |
| 1  | Track Number (0 through 4C) |
| 1  | 00 |
| 1  | Sector Number (1 through 1A) |
| 1  | 00 |
| 1  | F7 (writes two CRC bytes) |
| 11 | 00 or FF |
| 6  | 00 |
| 1  | FB (Data Address Mark) |
| 128 | Data (IBM uses E5) |
| 1  | F7 (writes two CRC bytes) |
| 27 | 00 or FF |
| 247** | 00 or FF |

The bracketed 26-byte…-through-27-byte record block is written 26 times (26
sectors). `**` = continue writing until the FD1771 interrupts out (~247 bytes).

### Non-IBM formats

Same idea, but sector length is derived from the ID's sector-length byte via the
b=0 algorithm (16–4096 bytes, 16-byte increments). F7–FE must not appear in the
sector-length byte. Only the two GAP rules above apply.

---

## 10. Emulation Notes / Quirks

- **MR ⇒ Restore.** Bringing MR high loads 0x03 into CR and runs a Restore
  regardless of Ready. Model this on reset.
- **Restore step limit:** 255 step pulses before Seek Error; a real restore also
  works when TR00 is already asserted (immediate TR←0).
- **Two-revolution timeout** governs ID search on Type II and Type I verify;
  Read Sector additionally needs the Data AM within ~28 bytes of the ID.
- **Write Sector 11-byte count / 6 zeros / Data AM:** WG only turns on if the
  first DRQ was serviced. Six zero bytes precede the Data AM. Missing data →
  zero byte + Lost Data (not terminated).
- **Read Sector records the Data AM type** in status bits 5/6 (FB/FA/F9/F8). A
  Deleted Data Mark (F8) shows as bits 6=1,5=1.
- **Read Track does no CRC checking** and includes gaps; with s̄=0 it
  synchronizes to address marks.
- **Read Address writes the ID's sector byte into the Sector Register** as a side
  effect.
- **Write Track control bytes F7–FE** are magic (AMs/CRC); F7 emits two CRC
  bytes. Data fields must never contain literal F7–FE.
- **Status is command-type-dependent** — you must remember which command class
  last ran to present bits 3–6 correctly.
- **INTRQ clears on Status read or new command load**; DRQ clears on DR
  read/write.
- **Force Interrupt 0xD0** (no condition bits) terminates and clears Busy but
  raises no interrupt; only Force Interrupt clears an immediate (i3) interrupt.
- **CRC:** polynomial x^16+x^12+x^5+1, preset to ones, covers AM through CRC.
- **Timing scales with clock:** all "doubled at 1 MHz" values apply to the
  mini-floppy (1 MHz) case; stepping rates likewise double.
