# Western Digital WD177X-00 Floppy Disk Formatter/Controller

Source: [Western Digital WD177X-00 - Datasheet.pdf](#)

This is an emulation-oriented reference for the WD177X-00 family (WD1770, WD1772,
WD1773) of single-chip Floppy Disk Formatter/Controllers. The WD177X-00 is a
28-pin NMOS device with a built-in digital data separator and write
precompensation. It is software compatible with the WD1793 (WD1773 is 100%
compatible) and functionally similar to its FD179X predecessor. All content below
is what a developer needs to implement or debug this controller in software.

---

## Family Members and Differences

| Device  | Step Rates              | Motor Control (MO) | Precomp Enable / Ready | Notes |
|---------|-------------------------|--------------------|------------------------|-------|
| WD1770  | 6 / 12 / 20 / 30 ms (FD179X rates) | Yes (pin 20 = MO output) | N/A         | Standard 179X step rates |
| WD1772  | 6 / 12 / 2 / 3 ms (faster)          | Yes (pin 20 = MO output) | N/A         | Faster step rates; adds 15 ms settle option |
| WD1773  | 6 / 12 / 20 / 30 ms (FD179X rates) | No (pin 20 = RDY/ENP input) | Yes (RDY/ENP) | 100% software compatible with WD1793 |
| WD1770-02 / WD1772-00 | — | — | — | Precomp Enable bit in Write Commands controls precomp |
| WD1772-02 | — | — | — | Same as WD1772-00 but enhanced digital data separator |

Key distinction: the WD1770/WD1772 have a Motor On (MO) output on pin 20 and
handle spindle motor start-up automatically. The WD1773 uses pin 20 as a
combined READY input / write-precompensation-enable (RDY/ENP) and has no motor
control; it assumes an always-ready or externally-managed drive and requires the
drive to be ready before Type II/III commands execute.

- **CLK** input must be a free-running 50% duty-cycle clock at 8 MHz ±0.1%,
  regardless of DDEN.
- Single (FM) and double (MFM) density are supported, selected by the DDEN pin
  (DDEN=1 → FM/single, DDEN=0 → MFM/double).
- Data rates: 125 KBits/sec single density, 250 KBits/sec double density.
- Write precompensation value: 125 nsec from nominal.
- Sector lengths supported: 128, 256, 512, or 1024 bytes.
- Sectors per track: 1 to 240. Tracks: 0 to 240.
- Drives one standard TTL load / three LS loads.

---

## Pin Description

| Pin | Mnemonic | Signal | I/O | Function |
|-----|----------|--------|-----|----------|
| 1  | CS (active low)   | Chip Select        | I   | Low selects chip, enables host communication. |
| 2  | R/W (R=high)      | Read/Write         | I   | High = place selected register data on D0-D7 (read); Low = write to selected register. |
| 3,4| A0, A1            | Address 0,1        | I   | Select register (see register-select table). |
| 5-12| DAL0-DAL7        | Data Access Lines  | I/O | 8-bit bidirectional bus; enabled by CS and R/W. Each line drives one TTL load. |
| 13 | MR (active low)   | Master Reset       | I   | Low pulse resets device and initializes Status Register (internal pull-up). |
| 14 | GND               | Ground             | —   | Ground. |
| 15 | VCC               | Power              | —   | +5 V ±5%. |
| 16 | STEP              | Step               | O   | Pulse per head step. Rate differs WD1770 vs WD1772. |
| 17 | DIRC              | Direction          | O   | High = stepping in (toward center); Low = stepping out. Valid 24 µsec before first step pulse. |
| 18 | CLK               | Clock              | I   | Free-running 50% duty clock at 8 MHz ±0.1%. |
| 19 | RD (active low)   | Read Data          | I   | Raw data line: clock + data pulses from drive. Only line needed to recover FM/MFM data. |
| 20 | RDY/ENP (WD1773)  | Ready/Enable Precomp | I | READY input during read/step; write-precomp enable during write. READY latched on WG true. |
| 20 | MO (WD1770/1772)  | Motor On           | O   | High enables spindle motor prior to read/write/step. |
| 21 | WG                | Write Gate         | O   | Made valid prior to writing on diskette. |
| 22 | WD                | Write Data         | O   | FM or MFM clock+data pulses to be written. |
| 23 | TR00 (active low) | Track 00           | I   | Low informs chip R/W head is at track zero. |
| 24 | IP (active low)   | Index Pulse        | I   | Low when physical index hole encountered. |
| 25 | WPRT (active low) | Write Protect      | I   | Sampled on Write Command; low prevents any write (internal pull-up). |
| 26 | DDEN (active low) | Double Density Enable | I | DDEN=0 → double (MFM); DDEN=1 → single (FM). Internal pull-up. |
| 27 | DRQ               | Data Request       | O   | High = Data Register full (read) or empty (write). |
| 28 | INTRQ             | Interrupt Request  | O   | High at completion of any command; reset by reading Status Register. |

---

## Register Set

The chip exposes five 8-bit registers, selected by A1/A0 combined with R/W:

| A1 | A0 | Read (R/W=1)      | Write (R/W=0)     |
|----|----|-------------------|-------------------|
| 0  | 0  | Status Register   | Command Register  |
| 0  | 1  | Track Register    | Track Register    |
| 1  | 0  | Sector Register   | Sector Register   |
| 1  | 1  | Data Register     | Data Register     |

- **Command Register (CR)** — holds the command being executed. Not loaded while
  busy, except for a Force Interrupt. Loaded from DAL, not read onto DAL.
- **Track Register (TR)** — 8-bit; holds the current track number of the R/W head.
  Incremented on step-in, decremented on step-out. Compared against the recorded
  track number in the ID field during read/write/verify. Loadable/readable via
  DAL, but **not** while busy.
- **Sector Register (SR)** — 8-bit; holds the desired sector. Compared against the
  recorded sector in the ID field during read/write. Loadable/readable via DAL,
  but **not** while busy.
- **Data Register (DR)** — 8-bit holding register between Data Shift Register and
  the host. On Seek, DR holds the desired track. Loaded from and read onto DAL.
- **Status Register (STR)** — 8-bit device status; meaning of bits depends on the
  type of the last command. Read onto DAL, not loaded from DAL.

Internal registers:
- **Data Shift Register (DSR)** — assembles serial read data / serializes write data.
- **CRC Logic** — 16-bit CCITT CRC, polynomial G(x) = x^16 + x^12 + x^5 + 1.
  Includes all bytes from the address mark through the CRC characters. Register is
  preset to all ones before data is shifted through.
- **ALU** — serial comparator/incrementer/decrementer for register modification
  and ID-field comparison.

### Register access timing quirk

After any register is written, the same register cannot be read for 16 µsec (MFM)
or 32 µsec (FM). See the internal-sync delay table under Programming Notes.

---

## Command Summary

Commands are only loaded into the Command Register when Busy (Status bit 0) is
off, except the Force Interrupt command which may be loaded at any time. There are
11 commands in four types.

| Type | Command       | b7 | b6 | b5 | b4  | b3  | b2 | b1  | b0  |
|------|---------------|----|----|----|-----|-----|----|-----|-----|
| I    | Restore       | 0  | 0  | 0  | 0   | h   | V  | r1  | r0  |
| I    | Seek          | 0  | 0  | 0  | 1   | h   | V  | r1  | r0  |
| I    | Step          | 0  | 0  | 1  | u   | h   | V  | r1  | r0  |
| I    | Step-In       | 0  | 1  | 0  | u   | h   | V  | r1  | r0  |
| I    | Step-Out      | 0  | 1  | 1  | u   | h   | V  | r1  | r0  |
| II   | Read Sector   | 1  | 0  | 0  | m   | h/S | E  | 0/C | 0   |
| II   | Write Sector  | 1  | 0  | 1  | m   | h/S | E  | P/C | a0  |
| III  | Read Address  | 1  | 1  | 0  | 0   | h/o | E  | 0   | 0   |
| III  | Read Track    | 1  | 1  | 1  | 0   | h/o | E  | 0   | 0   |
| III  | Write Track   | 1  | 1  | 1  | 1   | h/o | E  | P/0 | 0   |
| IV   | Force Interrupt | 1 | 1  | 0  | 1   | I3  | I2 | I1  | I0  |

Notes on ambiguous bit positions:
- Type II bit3 is the Motor/Spin-up flag `h` on WD1770/1772 and the Side Compare
  flag `S` on WD1773.
- Type III bit3 is `h` on WD1770/1772 and `o` on WD1773 (see H/o below); for all
  Type III commands bit 3 must be 0 on the WD1773 side-compare interpretation.
- Type II Read Sector bit1 is the WD1773 Side Compare enable `C`; on WD1770/1772
  bit1 must be 0.
- Type II Write Sector bit1 is Precomp `P` (WD1770/1772) or Side Compare `C`
  (WD1773).
- Write Track bit1 is Precomp `P` (WD1770/1772) or 0.

---

## Flag Definitions

### Type I flags

**h — Motor On Flag (bit 3) [WD1770/1772]**
- h = 0: Enable spin-up sequence
- h = 1: Disable spin-up sequence

**V — Verify Flag (bit 2) [1770/2/3]**
- V = 0: No verify
- V = 1: Verify on destination track

**r1, r0 — Stepping Rate (bits 1,0)**

| r1 | r0 | WD1770-00 / WD1773-00 | WD1772-00 |
|----|----|-----------------------|-----------|
| 0  | 0  | 6 ms                  | 6 ms      |
| 0  | 1  | 12 ms                 | 12 ms     |
| 1  | 0  | 20 ms                 | 2 ms      |
| 1  | 1  | 30 ms                 | 3 ms      |

**u — Update Flag (bit 4) [1770/2/3]**
- u = 0: No update of Track Register
- u = 1: Update Track Register

### Type II & III flags

**m — Multiple Sector Flag (bit 4) [1770/2/3]**
- m = 0: Single sector
- m = 1: Multiple sector

**H — Motor / Spin-up Flag (bit 3) [1770/2]**
- H = 0: Enable spin-up sequence
- H = 1: Disable spin-up sequence

**S — Side Compare Flag (bit 3) [1773]**
- S = 0: Compare for side 0
- S = 1: Compare for side 1
- For all Type III commands bit 3 must be 0.

**E — 30 ms Settling Delay (bit 2) [1770/2/3]**
- E = 0: No delay
- E = 1: Add 30 ms delay (WD1772 add 15 ms delay)

**C — Side Compare Flag (bit 1) [1773 only]**
- C = 0: Disable side compare
- C = 1: Enable side compare

**P — Write Precompensation (bit 1) [1770/2/3]**
- P = 0: Enable write precomp
- P = 1: Disable write precomp

**a0 — Data Address Mark (bit 0) [1770/2/3]** (Write Sector)
- a0 = 0: Write normal Data Mark
- a0 = 1: Write Deleted Data Mark

### Type IV flags (Force Interrupt, I3-I0 = interrupt condition, bits 3-0)

- I0 = Not used (WD1770/1772); Not-Ready-to-Ready transition (WD1773)
- I1 = Not used (WD1770/1772); Ready-to-Not-Ready transition (WD1773)
- I2 = Interrupt on every Index Pulse
- I3 = Immediate interrupt
- I3-I0 = 0000: Terminate with no interrupt

---

## Type I Commands (Restore, Seek, Step, Step-In, Step-Out)

Type I commands position the R/W head. A stepping pulse of 4 µsec (MFM) or
8 µsec (FM) is output to the drive per step. Every step pulse moves the head one
track in the direction set by DIRC. The chip steps in the last-used direction
unless the command changes it. DIRC is valid 24 µsec before the first stepping
pulse.

**Motor / spin-up (WD1770/1772 only):** All commands except Force Interrupt are
programmable via the h flag to delay for spindle motor start. If h is not set and
MO is low when a command is received, MO is forced high and the chip waits 6
revolutions before executing (guarantees ~1 second start-up at 300 RPM). After
finishing, if idle for 9 revolutions MO goes back low. If a command is issued
while MO is high it executes immediately, defeating the 6-rev start-up — this lets
consecutive read/write commands avoid re-waiting. The WD1770/2 assumes the spindle
is up to speed.

**Verify (V=1):** After the last directional step, an additional 30 ms head
settle occurs (also 30 ms head settle if E is set in any Type II/III command; on
WD1772 the E delay is 15 ms). Verification begins at end of the 30 ms: the track
number from the first encountered ID field is compared against the Track Register.
If the track numbers match **and** the ID Field CRC is correct, verification is
complete and INTRQ is generated with no error. If there is a match but not a valid
CRC, the CRC error status bit (Status bit 3) is set and the next encountered ID
field is read. The chip searches up to 5 revolutions for a correct track number
with correct CRC; if not found, the Seek Error status bit (Status bit 4) is set
and INTRQ is generated. If V=0, no verification is performed.

### Restore (Seek Track 0)
On receipt, the TR00 input is sampled. If TR00 is active low (head already at
track 0), the Track Register is loaded with zeroes and an interrupt is generated.
If not, stepping pulses are issued at the rate in the r1,r0 field until TR00 goes
active low; then TR is loaded with zeroes and INTRQ is generated. If TR00 does not
go active low after 255 stepping pulses, the chip terminates, interrupts, and sets
the Seek Error status bit provided the V flag is set. A verify occurs if V=1.

### Seek
Assumes TR contains the current track and the Data Register contains the desired
track. The chip steps in the appropriate direction until TR equals DR. Verify if
V=1. Note: with multiple drives, update TR for the selected drive before seeking.

### Step
Issues one stepping pulse in the same direction as the previous step command.
After a delay per r1,r0, verify if V=1. If u=1, TR is updated. INTRQ at completion.

### Step-In
Issues one stepping pulse toward track 76 (in). If u=1, TR is incremented by one.
Verify if V=1 after the r1,r0 delay. INTRQ at completion.

### Step-Out
Issues one stepping pulse toward track 0 (out). If u=1, TR is decremented by one.
Verify if V=1 after the r1,r0 delay. INTRQ at completion.

---

## Type II Commands (Read Sector, Write Sector)

Before loading a Type II command, load the Sector Register with the desired sector
number. On receipt the Busy bit is set. If E=1 the command executes after a 30 ms
(15 ms WD1772) delay. The chip compares the Track number in each ID field against
TR; on the WD1773 the side (if C=1) and sector are also compared. On a full match
(track, sector — and side/CRC) the data field is written or read. If no match
after searching, and the ID CRC is correct, the search continues; after 5 disk
revolutions with no match the Record Not Found bit (Status bit 4) is set and the
command terminates with INTRQ.

**Multiple sector (m flag):** If m=0, a single sector is read/written and INTRQ is
generated at completion. If m=1, the Sector Register is internally incremented
(ascending) and the operation continues for subsequent records until SR exceeds
the number of sectors on the track or a Force Interrupt is loaded. Example: if
told to read sector 27 when only 26 exist, SR exceeds available; the chip searches
5 revolutions, interrupts, resets Busy, and sets Record Not Found.

### Read Sector
On an ID field with correct track/sector/CRC, the data field is presented to the
host. The Data Address Mark is searched 30 bytes (single density) / 43 bytes
(double density) after the last ID CRC byte. If not found, the ID field is
re-searched; after 5 revolutions with no DAM, Record Not Found is set and the
operation terminates. As each byte is assembled in the DSR it is transferred to
DR and DRQ is generated. If the host fails to read DR before the next byte
arrives, that byte is lost and the Lost Data status bit (Status bit 2) is set. On
a data-field CRC error, the CRC Error bit is set and the command terminates (even
mid multi-record). At end of read, the type of Data Address Mark is recorded in
Status Bit 5:

| Status Bit 5 | Meaning            |
|--------------|--------------------|
| 1            | Deleted Data Mark  |
| 0            | Data Mark          |

### Write Sector
On an ID field with correct track/sector/CRC, a DRQ is generated. The chip counts
11 bytes (single density) / 22 bytes (double density) from the CRC field; WG is
made active if DRQ was serviced (DR was loaded). If DRQ was not serviced the
command terminates and Lost Data is set. With WG active, 6 bytes of zeroes (single)
or 12 bytes of zeroes (double) are written. The Data Address Mark is then written
per the a0 field:

| a0 | Data Address Mark (bit 0) |
|----|---------------------------|
| 1  | Deleted Data Mark         |
| 0  | Data Mark                 |

The chip writes the data field, generating DRQs. If a DRQ is not serviced in time
for continuous writing, the Lost Data bit is set and a byte of zeroes is written
(the command is not terminated). After the last data byte, the two-byte CRC is
computed internally and written, followed by one byte of logic ones (FF) in FM or
MFM. WG then goes inactive. INTRQ is set 24 µsec (MFM) after the last CRC byte is
written. For partial sector writes, write data and fill the balance with zeroes.

---

## Type III Commands (Read Address, Read Track, Write Track)

### Read Address
On receipt, Busy is set. The next encountered ID field is read in: the six ID
bytes are assembled and transferred to DR, generating a DRQ for each. The six
bytes of the ID field are:

| 1          | 2           | 3           | 4             | 5    | 6    |
|------------|-------------|-------------|---------------|------|------|
| Track Addr | Side Number | Sector Addr | Sector Length | CRC1 | CRC2 |

The CRC characters are transferred to the host; the chip checks CRC validity and
sets the CRC status bit if a CRC error. The Track Address of the ID field is
written into the Sector Register so the user can compare it. INTRQ and Busy reset
at completion.

### Read Track
On receipt the head is loaded and Busy is set. Reading starts at the leading edge
of the first encountered index pulse and continues until the next index pulse. All
gap, header, and data bytes are assembled and transferred to DR, generating a DRQ
per byte. Byte accumulation is synchronized to each address mark encountered.
INTRQ at completion. Diagnostic characteristics: no CRC checking is performed; gap
information is included in the data stream; the Address Mark Detector is on for the
whole command duration, so write splices or noise may look like an AM. The ID AM,
ID field, ID CRC, DAM, Data, and Data CRC bytes for each sector are correct; gap
bytes may be read incorrectly during write-splice time due to synchronization.

### Write Track (Formatting)
Position the R/W head over the desired track, then issue Write Track. On receipt
Busy is set. Writing starts at the leading edge of the first index pulse and
continues until the next index pulse. The Data Request is activated immediately on
receiving the command, but writing does not start until the first byte is loaded
into DR. If DR is not loaded within three byte times, the operation terminates,
Not Busy and Lost Data status bit set, INTRQ activated. If DR is not present when
needed, a byte of zeroes is substituted. Normally whatever pattern is in DR is
written with a normal clock pattern. However, patterns F5-FE are interpreted
specially (see Write Track byte-interpretation table). The CRC generator is
initialized whenever an F8-FE byte is transferred to DR in FM, or on receipt of an
F5 in MFM. An F7 pattern generates two CRC characters. So F8-FE (FM) and F5-FE
(MFM) must not appear in gaps, data fields, or ID fields; CRCs are generated by an
F7.

Disks are formatted in IBM 3740 or System 34 formats with sector lengths of 128,
256, 512, or 1024 bytes.

#### Write Track Data-Byte Interpretation

| Data in DR (hex) | In FM (DDEN=1)                     | In MFM (DDEN=0)                     |
|------------------|-----------------------------------|-------------------------------------|
| 00 thru F4       | Write 00 thru F4, CLK = FF        | Write 00 thru F4 in MFM             |
| F5               | Not Allowed                       | Write A1* in MFM, Present CRC       |
| F6               | Not Allowed                       | Write C2** in MFM                   |
| F7               | Generate 2 CRC bytes              | Generate 2 CRC bytes                |
| F8 thru FB       | Write F8-FB, CLK = C7, Preset CRC | Write F8-FB in MFM                  |
| FC               | Write FC with CLK = D7            | Write FC in MFM                     |
| FD               | Write FD with CLK = FF            | Write FD in MFM                     |
| FE               | Write FE, CLK = C7, Preset CRC    | Write FE in MFM                     |
| FF               | Write FF with CLK = FF            | Write FF in MFM                     |

\* Missing clock transition between bits 4 and 5.
\** Missing clock transition between bits 3 and 4.

---

## Type IV Command (Force Interrupt)

Used to terminate a multiple-sector read/write or to insure Type I status is in
the Status Register. May be loaded into the Command Register at any time. If a
command is under execution (Busy set), the command is terminated and Busy is
reset. The lower four bits determine the conditional interrupt:

- I0 = Not used (WD1770/1772); Not-Ready-to-Ready transition (WD1773)
- I1 = Not used (WD1770/1772); Ready-to-Not-Ready transition (WD1773)
- I2 = Every Index Pulse
- I3 = Immediate Interrupt

A conditional interrupt is enabled when the matching bit (I3-I0) is 1. When the
specified condition occurs, INTRQ goes high. If I3-I0 are all zero (Hex D0), no
interrupt occurs but any command under execution is immediately terminated.

**Immediate interrupt (I3=1, Hex D8):** an interrupt is generated immediately and
the current command terminated. Reading the status or writing to the Command
Register does **not** automatically clear this interrupt. Hex D0 is the only
command that clears an immediate interrupt enabled by Hex D8, or that enables a
subsequent Load Command Register or Read Status Register operation. **Follow a Hex
D8 with a D0.**

Wait 16 µsec (double density) or 32 µsec (single density) before issuing a new
command after a forced interrupt. Loading a new command sooner nullifies the
forced interrupt.

Forced interrupt stops a command at the end of an internal micro-instruction and
generates INTRQ when the specified condition is met. Forced interrupt waits until
ALU operations in progress complete (CRC calculations, compares, etc.).

**Status after Force Interrupt:** On receipt of a Force Interrupt, the Busy bit is
set and the rest of the status bits are updated/cleared for the new command. If
issued while a command is under execution, Busy is reset and the rest of the
status bits are unchanged. If issued when no command is under execution, Busy is
reset and the rest of the status bits are updated/cleared — in this case Status
reflects the Type I commands.

Common Force Interrupt opcodes: D0 = terminate, no interrupt; D8 = immediate
interrupt.

---

## DRQ / INTRQ Behavior

- **INTRQ** is set (high) at the completion of every command; reset by reading the
  Status Register or by loading the Command Register. Also generated if a Force
  Interrupt condition is met.
- **DRQ** (high) means DR is full (read) or empty (write). It also appears as
  Status bit 1 during read/write. On read, DRQ is set when an assembled byte is in
  DR, cleared when the host reads DR. On write, DRQ is set when DR is transferred
  to DSR and requires a new byte, reset when the host loads DR.
- If DR is read too late (read) or loaded too late (write), the Lost Data status
  bit (bit 2) is set. On write, a byte of zeroes is written when lost.
- Reading the DRQ bit in the Status Register does **not** reset DRQ; reading the
  Data Register auto-resets both DRQ and the DRQ status bit. A write to the Data
  Register also resets DRQ.
- INTRQ de-asserts on read of status or load of Command Register.
- Worst-case service time for DRQ: 23.5 µsec (MFM), 47.5 µsec (FM).

---

## General Read / Write Operation Notes

**Sector length field (from ID field 4th byte at format time):**

| Sector Length Field (hex) | Bytes in Sector (decimal) |
|---------------------------|---------------------------|
| 00                        | 128                       |
| 01                        | 256                       |
| 02                        | 512                       |
| 03                        | 1024                      |

**Write inhibit:** Writing is inhibited when WPRT is asserted; the Write Command
is immediately terminated, an interrupt is generated, and the Write Protect status
bit is set.

**Write precompensation:** For write operations WG enables a Write condition
consisting of active-high pulses containing both Clock and Data in FM/MFM; WD
provides the missing-clock patterns for Address Marks. The WD1773 enables precomp
when RDY/ENP is asserted and READY has been latched (WG asserted). On WD1770-02 /
WD1772-00 the Precomp Enable bit in Write Commands (bit1 P=0) enables automatic
precomp; the outgoing Write Data stream is delayed/advanced from nominal by 125
nsec per the precomp table:

| Pattern (prev/cur/next bits) | MFM   | FM  |
|------------------------------|-------|-----|
| X 1 1 0                      | Early | N/A |
| X 0 1 0                      | Late  | N/A |
| 0 0 0 1                      | Early | N/A |
| 1 0 0 0                      | Late  | N/A |

(Bit order in pattern: previous bit sent, current bit sending, next bit to be
sent.) Precompensation is typically enabled on the innermost tracks where bit
shift occurs and bit density is at maximum. READY is true for read/write
operations (all Type II and III executions).

---

## Status Register

Format (bits 7..0): S7 S6 S5 S4 S3 S2 S1 S0. Meaning depends on the command type
last executed.

### WD1770-00 / WD1772-00 Status Register Description

| Bit | Name                | Meaning |
|-----|---------------------|---------|
| S7  | MOTOR ON            | Reflects the Motor On output. |
| S6  | WRITE PROTECT       | On Read Record: not used. On Read Track: not used. On any Write: indicates Write Protect. Reset when updated. |
| S5  | RECORD TYPE / SPIN-UP | Type I: set when Motor Spin-Up completed (6 revolutions). Type 2 & 3: indicates record type — 0 = Data Mark, 1 = Deleted Data Mark. |
| S4  | RECORD NOT FOUND (RNF) | Set when the desired track, sector, or side was not found. Reset when updated. |
| S3  | CRC ERROR           | If S4 set: error found in one or more ID fields; otherwise indicates data-field error. |
| S2  | LOST DATA / BYTE    | Set when computer did not respond to DRQ in one byte time. Reset to zero when updated. On Type I commands: reflects the status of the TR00 signal. |
| S1  | DATA REQUEST / INDEX | Copy of the DRQ output: DR full on Read, DR empty on Write. Reset to zero when updated. On Type I commands: reflects the status of the IP (index) signal. |
| S0  | BUSY                | Set when command under execution; reset when no command executing. |

### WD1773-00 Status Register Summary (per command)

| Bit | All Type I    | Read Address | Read Sector | Read Track | Write Sector | Write Track |
|-----|---------------|--------------|-------------|------------|--------------|-------------|
| S7  | NOT READY     | NOT READY    | NOT READY   | NOT READY  | NOT READY    | NOT READY   |
| S6  | WRITE PROTECT | 0            | 0           | 0          | WRITE PROTECT| WRITE PROTECT|
| S5  | HEAD LOADED   | 0            | RECORD TYPE | 0          | WRITE FAULT  | WRITE FAULT |
| S4  | SEEK ERROR    | RNF          | RNF         | 0          | RNF          | 0           |
| S3  | CRC ERROR     | CRC ERROR    | CRC ERROR   | 0          | CRC ERROR    | 0           |
| S2  | TRACK 0       | LOST DATA    | LOST DATA   | LOST DATA  | LOST DATA    | LOST DATA   |
| S1  | INDEX PULSE   | DRQ          | DRQ         | DRQ        | DRQ          | DRQ         |
| S0  | BUSY          | BUSY         | BUSY        | BUSY       | BUSY         | BUSY        |

### WD1773-00 Status for Type I Commands

| Bit | Name          | Meaning |
|-----|---------------|---------|
| S7  | NOT READY     | Set = drive not ready. Inverted copy of Ready input, logically ORed with MR. |
| S6  | PROTECTED     | Set = Write Protect activated. Inverted copy of WPRT input. |
| S5  | HEAD LOADED   | Set = head loaded and engaged. Logical AND of HLD and HLT signals. |
| S4  | SEEK ERROR    | Set = desired track not verified. Reset to 0 when updated. |
| S3  | CRC ERROR     | CRC encountered in ID field. |
| S2  | TRACK 00      | Set = R/W head positioned at Track 0. Inverted copy of TR00 input. |
| S1  | INDEX         | Set = index mark detected from drive. Inverted copy of IP input. |
| S0  | BUSY          | Set = command in progress. |

### WD1773-00 Status for Type II and III Commands

| Bit | Name          | Meaning |
|-----|---------------|---------|
| S7  | NOT READY     | Set = drive not ready. Inverted copy of Ready input ORed with MR. Type II/III will not execute unless drive ready. |
| S6  | WRITE PROTECT | On Read Record: not used. On Read Track: not used. On any Write: Write Protect. Reset when updated. |
| S5  | RECORD TYPE   | On Read Record: record-type code from data-field address mark. 1 = Deleted Data Mark, 0 = Data Mark. On any Write: forced to zero. |
| S4  | RECORD NOT FOUND (RNF) | Set = desired track, sector, or side not found. Reset when updated. |
| S3  | CRC ERROR     | If S4 set: error in ID field(s); otherwise error in data field. Reset when updated. |
| S2  | LOST DATA     | Set = computer did not respond to DRQ in one byte time. Reset to zero when updated. |
| S1  | DATA REQUEST  | Copy of DRQ: DR full on Read, DR empty on Write. Reset to zero when updated. |
| S0  | BUSY          | Set = command under execution. |

The user reads Status via program control or DRQ/interrupt methods. Reading the
DRQ bit in Status does not reset DRQ; only reading DR resets DRQ and its status
bit.

---

## Disk Format

### Single Density (FM) — IBM 3740, 128 bytes/sector
Track layout (from index pulse), repeated for each sector:

| 40 bytes FF | 6 bytes 00 | ID FE | Track | Side | Sector | Length | CRC1 | CRC2 | 11 bytes FF | 6 bytes 00 | Data Addr Mark | 128 bytes user data | CRC1 | CRC2 | 10 bytes FF (Gap IV) |

ID field = ID(FE), Track, Side, Sector, Length, CRC1, CRC2.
Data field = Data Addr Mark, user data, CRC1, CRC2.

### Double Density (MFM) — IBM System 34, 256 bytes/sector
Track layout (from index pulse), repeated for each sector:

| 60 bytes 4E | 12 bytes 00 | 3 bytes A1 | ID FE | Track | Side | Sector | Length | CRC1 | CRC2 | 22 bytes 4E | 12 bytes 00 | 3 bytes A1 | ID FB | 256 bytes user data | CRC1 | CRC2 | 24 bytes 4E (Gap IV) |

### Gap lengths

|         | FM            | MFM           |
|---------|---------------|---------------|
| Gap I   | 16 bytes FF   | 32 bytes 4E   |
| Gap II  | 11 bytes FF   | 22 bytes 4E   |
|         | 6 bytes 00    | 12 bytes 00 + 3 bytes A1 |
| Gap III | 10 bytes FF   | 24 bytes 4E   |
|         | 4 bytes 00    | 8 bytes 00 + 3 bytes A1 |
| Gap IV  | 16 bytes FF   | 16 bytes 4E   |

Byte counts must be exact. Gap III byte counts are minimum except exactly 3 bytes
of A1 must be written.

### Non-standard formats
Variations are possible if: (1) sector size is 128/256/512/1024; (2) Gap 2 is
unchanged from recommended; (3) exactly 3 bytes of A1 are used in MFM. The Index
Address Mark is not required for operation. Gaps 1, 3, 4 can be as short as 2 bytes
for chip operation, but PLL lock-up, motor speed variation and write-splice
require more; use recommended lengths for reliability.

### Recommended Write Track format — 128 bytes/sector (single density)
Issue Write Track, load DR with each byte (one DRQ per byte):

| # of Bytes | Hex Value Written |
|------------|-------------------|
| 40         | FF (or 00)        |
| 6          | 00                |
| 1          | FE (ID Address Mark) |
| 1          | Track Number      |
| 1          | Side Number (00 or 01) |
| 1          | Sector Number (1 thru 10) |
| 1          | 00 (Sector Length) |
| 1          | F7 (2 CRCs written) |
| 11         | FF                |
| 6          | 00                |
| 1          | FB (Data Address Mark) |
| 128        | Data (IBM uses E5) |
| 1          | F7 (2 CRCs written) |
| 10         | FF (or 00)        |

Bracketed field (FE...F7 CRC + data) written 16 times. Continue writing FF (or 00)
until the chip interrupts out — approx 369 bytes total for the final gap.

### Recommended Write Track format — 256 bytes/sector (double density)

| # of Bytes | Hex Value Written |
|------------|-------------------|
| 60         | 4E                |
| 12         | 00                |
| 3          | F5 (Writes A1)    |
| 1          | FE (ID Address Mark) |
| 1          | Track Number (0 thru 4C) |
| 1          | Side Number (0 or 1) |
| 1          | Sector Number (1 thru 10) |
| 1          | 01 (Sector Length) |
| 1          | F7 (2 CRCs written) |
| 22         | 4E                |
| 12         | 00                |
| 3          | F5 (Writes A1)    |
| 1          | FB (Data Address Mark) |
| 256        | Data              |
| 1          | F7 (2 CRCs written) |
| 24         | 4E                |

Bracketed field written 16 times. Continue writing until the chip interrupts out —
approx 668 bytes.

---

## Timing / Electrical

### Internal-sync delay after a register access
Because of internal sync cycles, observe these delays under programmed I/O:

| Operation             | Next Operation           | FM      | MFM     |
|-----------------------|--------------------------|---------|---------|
| Write to Command Reg. | Read Busy Bit (Status 0) | 48 µsec | 24 µsec |
| Write to Command Reg. | Read Status Bits 1-7     | 64 µsec | 32 µsec |
| Write Register        | Read Same Register       | 32 µsec | 16 µsec |

### Read Enable timing (RE such that R/W=1, CS=0)

| Symbol | Characteristic          | Min | Typ | Max | Units |
|--------|-------------------------|-----|-----|-----|-------|
| tRE    | RE Pulse Width of CS    | 200 |     |     | nsec  |
| tDRR   | DRQ Reset from RE       |     | 200 | 300 | nsec  |
| tDV    | Data Valid from RE      |     | 100 | 200 | nsec  |
| tDOH   | Data Hold from RE       | 20  |     | 150 | nsec  |
|        | INTRQ Reset from RE     |     |     | 8   | µsec  |

DRQ and INTRQ reset from the rising (lagging) edge of RE; resets from the falling
(leading) edge of WE.

### Write Enable timing (WE such that R/W=0, CS=0)

| Symbol | Characteristic       | Min | Typ | Max | Units |
|--------|----------------------|-----|-----|-----|-------|
| tAS    | Setup ADDR to CS     | 50  |     |     | nsec  |
| tSET   | Setup R/W to CS      | 0   |     |     | nsec  |
| tAH    | Hold ADDR from CS    | 10  |     |     | nsec  |
| tHLD   | Hold R/W from CS     | 0   |     |     | nsec  |
| tWE    | WE Pulse Width       | 200 | 100 | 200 | nsec  |
| tDRW   | DRQ Reset from WE    |     | 100 | 200 | nsec  |
| tDS    | Data Setup to WE     | 150 |     |     | nsec  |
| tDH    | Data Hold from WE    | 0   |     |     | nsec  |
|        | INTRQ Reset from WE  |     |     | 8   | µsec  |

### Read Data timing

| Characteristic       | Min   | Max | Units | Condition |
|----------------------|-------|-----|-------|-----------|
| Raw Read Pulse Width | 0.200 | 3   | µsec  | MFM       |
| Raw Read Pulse Width | 0.400 | 3   | µsec  | FM        |
| Raw Read Cycle Time  | 3     |     | µsec  |           |

### Write Data timing

| Symbol | Characteristic          | Typ   | Units | Condition |
|--------|-------------------------|-------|-------|-----------|
|        | Write Gate to Write Data| 4     | µsec  | FM        |
|        | Write Gate to Write Data| 2     | µsec  | MFM       |
|        | Write Data Cycle Time   | 4,6,8 | µsec  |           |
|        | Write Gate off from WD  | 4     | µsec  | FM        |
|        | Write Gate off from WD  | 2     | µsec  | MFM       |
| tWP    | Write Data Pulse Width  | 820   | nsec  | Early MFM |
| tWP    | Write Data Pulse Width  | 690   | nsec  | Nominal MFM |
| tWP    | Write Data Pulse Width  | 570   | nsec  | Late MFM  |
| tWP    | Write Data Pulse Width  | 1.38  | µsec  | FM        |

Precomp: Early = 6.5 CLKs, Nominal = 5.5 CLKs, Late = 4.5 CLKs between write-data
pulses.

### Miscellaneous timing

| Symbol | Characteristic     | Min | Typ | Units | Condition |
|--------|--------------------|-----|-----|-------|-----------|
| tCD1   | Clock Duty (low)   | 50  | 67  | nsec  |           |
| tCD2   | Clock Duty (high)  | 50  | 67  | nsec  |           |
| tSTP   | Step Pulse Output  |     | 4   | µsec  | MFM       |
| tSTP   | Step Pulse Output  |     | 8   | µsec  | FM        |
| tDIR   | Dir Setup to Step  |     | 24  | µsec  | MFM       |
| tDIR   | Dir Setup to Step  |     | 48  | µsec  | FM        |
| tMR    | Master Reset Pulse Width | 50 |  | µsec  |           |
| tIP    | Index Pulse Width  | 20  |     | µsec  |           |

### DC operating characteristics (TA 0–70 °C, VCC +5 V ±.25 V)

| Symbol | Characteristic       | Min | Max  | Units | Conditions |
|--------|----------------------|-----|------|-------|------------|
| IIL    | Input Leakage        |     | 10   | µA    | VIN = VCC  |
| IOL    | Output Leakage       |     | 10   | µA    | VOUT = VCC |
| VIH    | Input High Voltage   | 2.0 |      | V     |            |
| VIL    | Input Low Voltage    |     | 0.8  | V     |            |
| VOH    | Output High Voltage  | 2.4 |      | V     | IO = -100 µA |
| VOL    | Output Low Voltage   |     | 0.40 | V     | IO = 1.6 mA |
| PD     | Power Dissipation    |     | .75  | W     |            |
| RPU    | Internal Pull-Up     | 100 | 1700 | µA    | VIN = 0 V  |
| ICC    | Supply Current       | 75 (typ) | 150 | mA |            |

---

## Programming Quirks / Emulation Notes

- **CLK is always 8 MHz** regardless of density; only DDEN switches FM/MFM.
- After writing any register, the same register cannot be reliably read for 16 µsec
  (MFM) / 32 µsec (FM); after a Command Register write, wait 24 µsec (MFM) /
  48 µsec (FM) to read the Busy bit and 32 µsec (MFM) / 64 µsec (FM) for status
  bits 1-7.
- Commands (except Force Interrupt) are ignored while Busy is set.
- **Force Interrupt Hex D8 (immediate) is NOT cleared by reading status or loading
  a command** — only Hex D0 clears it. Always follow D8 with D0. After a forced
  interrupt, wait 16 µsec (MFM) / 32 µsec (FM) before the next command or the
  forced interrupt is nullified.
- On WD1770/1772, the h flag controls whether the 6-revolution spin-up wait occurs;
  a command issued while MO is already high skips the wait. MO drops after 9 idle
  revolutions.
- On WD1773 (no motor control), the drive must be READY (pin 20) before Type II/III
  execute; NOT READY blocks them.
- The CRC (CCITT, x^16+x^12+x^5+1) is preset to all ones before the address mark
  and covers everything from the AM through the CRC bytes.
- Write Track: F5 → A1 (missing clock), F6 → C2 (missing clock), F7 → generate 2
  CRC bytes, F8-FE preset/initialize CRC (density-dependent). These bytes must not
  appear in data unless intended as marks/CRCs.
- Multi-sector operations auto-increment the Sector Register and stop on RNF or a
  Force Interrupt.
- Verify (V=1) searches 5 revolutions; Restore steps up to 255 pulses before
  Seek Error.
- Data-loss handling: on write, a lost byte is written as zeroes and the command
  continues (Lost Data set); on read, a lost byte simply sets Lost Data.
