# Altair 88-MDS Minidisk System — Documentation (Preliminary)

Source: [88-MDS Minidisk Manual.pdf](#)

MITS, July 1977 (Preliminary). This is a developer-facing distillation of the
88-MDS Minidisk manual, focused on everything needed to emulate the controller
and drive in software. Marketing copy, warranty, order forms, and pure PCB
assembly instructions are omitted. Circuit-level (gate/flip-flop) theory is
summarized only where it affects observable timing or behavior.

---

## 1. System Overview

The Altair Minidisk System (88-MDS) is mass storage for the Altair 8800. It
consists of:

- **Two controller boards** that plug into the Altair (S-100) bus:
  - **Minidisk Board #1** — Address selection, Sector circuitry, Read circuitry, Status output.
  - **Minidisk Board #2** — Disk Enable circuit, Write circuitry, Disk drive control circuitry.
- **One or more Minidisk Drives (88-MDDR)** — up to **4 drives** per controller.
  The drive cabinet contains the power supply, line buffers/receivers, and the
  drive-address selection circuitry. The actual mechanism is a **Shugart SA-400**.

All Control, Status, and Data I/O for the whole system is handled through **three
dedicated I/O ports**. A hardware **6.4-second inactivity timer** in the
controller turns the drive motor off if the disk is not accessed (to save motor
life).

The controller I/O address is **the same as the 88-DCDD floppy controller**, so
the Minidisk and the 8-inch floppy system cannot be used in the same machine
simultaneously.

### Key facts for emulation

| Item | Value |
|------|-------|
| I/O ports (octal) | 010, 011, 012 |
| I/O ports (hex) | 0x08, 0x09, 0x0A |
| Capacity per diskette (formatted) | 71,680 data bytes |
| Bytes per track | 2,048 data bytes |
| Data bytes per sector | 128 |
| Total bytes written per sector (incl. sync) | up to 137 (138th byte = 0) |
| Number of tracks | 35 (track 0 = outermost) |
| Sectors per track | 16 (hard-sectored: 16 sector holes + 1 index hole) |
| Rotational speed | 300 RPM (200 ms/revolution) |
| Time per sector | 12.5 ms |
| Track-to-track step time | 50 ms |
| Data transfer rate | 125,000 bits/s = 1 byte every 64 µs |
| Encoding | MFM, MSB (D7) first |
| Track density | 48 tracks/inch |
| Motor-startup delay (enable→read/write) | ≥ 1 s (min) |
| Average access time (incl. motor startup) | 1.85 s |
| Worst-case access time | 2.9 s |
| Worst-case latency | 200 ms |
| Inactivity motor-off timer | 6.4 s |
| Write protect | Notch-based; standard on all drives |
| Interrupts | Optional, at beginning of each sector (not used by Minidisk BASIC) |

Note on units: the manual gives the transfer rate two ways that must be
reconciled — the drive spec says **1 byte every 64 µs** and the ENWD/NRDA status
bits are asserted every **64 µs**. One passage in 4-5 mentions "every 32
microseconds" for write data; treat **64 µs/byte** as authoritative (consistent
with 125 kbit/s and the ENWD/NRDA timing throughout).

---

## 2. I/O Port Map

The controller occupies three consecutive octal ports. Each has distinct read
and write meanings:

| Port (octal) | Direction | Function |
|--------------|-----------|----------|
| 010 | OUT (from CPU) | **Disk Control Latch (DCL)** — select/enable one of four drives, or turn system off |
| 010 | IN (to CPU) | **Status** of drive and controller |
| 011 | OUT (from CPU) | **Control Disk (CD)** — drive functions: STEP IN/OUT, WRITE ENABLE, TIMER RESET, interrupt enable/disable |
| 011 | IN (to CPU) | **Sector position** of the diskette |
| 012 | OUT (from CPU) | **Write Data** |
| 012 | IN (to CPU) | **Read Data** |

The three OUT strobes are named DCL (port 010), CD/CONTROL DISK STROBE
(port 011), and WDS/WRITE DATA STROBE (port 012). Each I/O instruction produces a
500 ns low-going enable pulse in the controller.

---

## 3. Port 010 OUT — Disk Enable / Disk Control Latch (DCL)

Selects and enables a drive, or shuts the system down. Writing the head is
loaded automatically when the drive is enabled.

| Bit | Function |
|-----|----------|
| D0 | Drive select LSB (see table below). Head loads automatically when drive enabled. |
| D1 | Drive select MSB |
| D2–D6 | Not used |
| D7 | **0 = enable the selected drive; 1 = turn the Minidisk system OFF** |

**Drive selection** (D0/D1):

| Drive Address | D0 | D1 |
|---------------|----|----|
| 0 | 0 | 0 |
| 1 | 1 | 0 |
| 2 | 0 | 1 |
| 3 | 1 | 1 |

Writing **377 (octal) = 0xFF** to port 010 turns the system off (D7=1). To
enable drive N, output N with D7=0 (e.g. drive 0 = 000, drive 1 = 001).

### Rules / quirks

- When **changing** from one drive address to another, first turn the system OFF
  (output 377 to port 010), then enable the new drive. This resets the controller.
- **Never** issue a Head Step command right before changing drive address or
  turning off a drive. If you must step, first check the MH status bit for logic 0
  (head-movement allowed) before changing/disabling.
- If the selected drive is not connected or is powered off, the controller
  automatically turns off (DISK POWERED line stays inactive).

---

## 4. Port 010 IN — Status

Read when a drive is enabled. **Active-true bits read as logic 0; false reads as
logic 1.** Also: **all status bits read as logic 1 when there is no diskette in
the drive.** Bit names carry an overbar in the manual (active-low) — reproduced
here as the "true = 0" convention.

| Bit | Signal | Meaning (True = logic 0) |
|-----|--------|--------------------------|
| D0 | ENWD (Enter New Write Data) | True → controller wants the next write-data byte on port 012. Occurs every 64 µs in write mode; reset when the write byte is output. |
| D1 | MH (Move Head) | True → head stepping is allowed. Goes false (1) for 50 ms after a step command; also false during write mode. |
| D2 | HS (Head Status) | True → head loaded and motor speed stable. Goes true 1 s after Disk Enable; false for 50 ms after a command. When false, sector info cannot be read. Not normally polled (R/W code tests the sector channel instead). Head is always loaded while drive enabled. |
| D3 | — | Not used, always 0 when drive enabled. |
| D4 | — | Not used, always 0 when drive enabled. |
| D5 | INTE | Reflects CPU Interrupt-Enable state. True (0) when CPU interrupts are enabled. |
| D6 | TRACK 0 | True → R/W head is at the outermost track (track 0). Use to zero the software track counter. |
| D7 | NRDA (New Read Data Available) | True → read circuit has a byte ready on port 012. Occurs every 64 µs while reading; reset each time a read byte is input from port 012. |

Because true = 0, typical polling code masks the bit and tests for zero. For
example, ENWD "requesting data" corresponds to the masked bit being 0, and the
test-programs treat a masked-nonzero result as "ENWD false (=1), keep waiting".

---

## 5. Port 011 OUT — Control Disk (CD)

Controls drive functions when both drive and controller are enabled. **A logic 1
represents a True/asserted signal.** Multiple bits may be combined per the rules
below.

| Bit | Function |
|-----|----------|
| D0 | **STEP IN** — step R/W head one position toward a higher-numbered track (toward center). Resets the 6.4 s disable timer. Causes MH and sector info to go false for 50 ms. Software keeps the track position. |
| D1 | **STEP OUT** — step R/W head one position toward a lower-numbered track (toward edge/track 0). Resets the 6.4 s timer. Causes MH and sector info false for 50 ms. |
| D2 | **TIMER RESET** — resets the 6.4 s disable timer. Issue before every read/write to keep the system enabled. |
| D3 | Not used. |
| D4 | **INTERRUPT ENABLE** — enable sector interrupts (at beginning of each sector). Stays enabled until Interrupt Disable or controller turned off. |
| D5 | **INTERRUPT DISABLE** — disable the controller's interrupt circuit. |
| D6 | Not used. |
| D7 | **WRITE ENABLE** — turn on controller + drive write circuits. Auto-reset at end of each sector. |

### Head-stepping quirk

If **both D0 and D1 are 1** during a port-011 output, the direction resolves to
**STEP OUT** (due to the clearing action on the internal step-direction
flip-flop). Do not set both.

### WRITE ENABLE sequence (D7 = 1)

1. Drive selected, head positioned at the desired track.
2. Search for the desired sector (see §6).
3. When the desired sector is found, issue WRITE ENABLE.
4. Zeros are **automatically** written for the first **1 ms** of the sector
   (the preamble). Write data requests are inhibited during this time.
5. 1 ms after the start of the sector, **ENWD goes true**, requesting the first
   write-data byte.
6. The **MSB (D7) of the first byte written must be logic 1** (the *sync bit*).
   The MSB is written first. This first byte is the **sync byte**, used for read
   synchronization.
7. ENWD goes true every 64 µs, reset after each byte is output to port 012.
8. Maximum bytes written = **137** including the sync byte.
9. The last (138th) byte written must be **all zeros (000)**. This pattern is
   written to the end of the sector; ENWD may be ignored from then on.
10. At the end of the sector the write circuit automatically turns off.

Write timing detail: WRITE ENABLE flip-flop stays set for 12.5 ms (one full
sector). Write data is MFM, MSB first.

---

## 6. Port 011 IN — Sector Position

Reads the diskette's current rotational (sector) position for the selected
drive. The controller has a counter cleared by the Index hole and counting
sectors 0–15. The **Index hole lies halfway between sector holes 15 and 0**.

Read/write must begin at the start of a sector to avoid data loss. To guarantee
this, **D0 (Sector True) is asserted only during the first 30 µs of a sector**.

| Bit | Signal | Meaning |
|-----|--------|---------|
| D0 | ST (Sector True) | **True = 0**, asserted only during the **first 30 µs** of a sector. This is the only window in which the sector position may be validly checked before read/write. |
| D1 | Sector count bit 0 (LSB) | Actual sector number, bit 0 |
| D2 | Sector count bit 1 | Actual sector number, bit 1 |
| D3 | Sector count bit 2 | Actual sector number, bit 2 |
| D4 | Sector count bit 3 (MSB) | Actual sector number, bit 3 |
| D5 | Always 0 | |
| D6 | Not used = 1 | |
| D7 | Not used = 1 | |

### Sector Position Bits (Table 3-B) — sector count encoding (D1–D4)

| Sector | D4 | D3 | D2 | D1 |
|--------|----|----|----|----|
| 0  | 0 | 0 | 0 | 0 |
| 1  | 0 | 0 | 0 | 1 |
| 2  | 0 | 0 | 1 | 0 |
| 3  | 0 | 0 | 1 | 1 |
| 4  | 0 | 1 | 0 | 0 |
| ... | | | | |
| 13 | 1 | 1 | 0 | 1 |
| 14 | 1 | 1 | 1 | 0 |
| 15 | 1 | 1 | 1 | 1 |

The manual lists the LSB→MSB rows as: D1 = `0 1 0 1 0 ... 1 0 1`, D2 = `0 0 1 1 0
... 0 1 1`, D3 = `0 0 0 0 1 ... 1 1 1`, D4 = `0 0 0 0 0 ... 1 1 1` for sectors
0,1,2,3,4,...,13,14,15. So the 4-bit field D4..D1 is just the binary sector
number 0–15.

**Note:** The sector-position channel reads as **all 1s** (disabled) for **1 s
after the drive is enabled** and for **50 ms after a step command**.

### Read/write windowing

- When **reading**, the read circuit is disabled during the **first 500 µs** of
  the sector to ensure valid sync-byte detection.
- When **writing**, enable the write circuit as close to Sector-True detection as
  possible; the circuit auto-writes zeros for the first 1 ms of the sector.

Typical code masks D0 and compares the byte against `376` (octal) — i.e. tests
that only the Sector-True/upper bits pattern indicating "start of sector 0" is
present — then compares the sector-count nibble for the desired sector.

---

## 7. Port 012 — Write Data (OUT) / Read Data (IN)

- **OUT (port 012):** Write a data byte to the write circuit. Output **only after
  ENWD (port 010 bit D0) has gone true**. ENWD is reset when the controller
  receives the byte. Data is written **MSB (D7) first, in MFM format**.
- **IN (port 012):** Read a data byte from the read circuit. When the drive is
  enabled, the controller is normally in read mode unless write is enabled. Read
  a byte after **NRDA (port 010 bit D7) goes true**; NRDA is reset when the byte
  is read. Data is read **MSB first, MFM**.

---

## 8. General Read/Write Programming Procedure

To begin disk operation the CPU must select and enable the drive and controller:

1. Output the drive address (000–004 octal... actually 000–003) on port 010 with
   D7=0. Head loads automatically and the motor starts.
2. Find **TRACK 0**: step the head out (port 011 D1=1) repeatedly, testing status
   port 010 bit D6 (TRACK 0) for 0.
3. Step the head in (port 011 D0=1) to the desired track. Software tracks the
   current track; only track 0 is found by hardware.
4. Locate the correct sector: input port 011 (sector position) and compare the
   sector count with the desired sector. Also check D0 (Sector True) for start of
   sector.
5. Issue TIMER RESET (port 011 D2=1) before every read or write to keep the
   system enabled.
6. Then either:
   - **Read:** input bytes from port 012 (D0–D7) after NRDA goes true, or
   - **Write:** enable write (port 011 D7=1), wait a few hundred µs, then output
     each byte to port 012 as ENWD is requested.
7. When done, clear Disk Control (output 377 to port 010, all bits = 1). This
   disables the drive and stops all disk functions. Turning the drive power off
   or disconnecting the cable also clears Disk Control.
8. When switching drives, clear Disk Control (377 to port 010) before enabling
   the new drive.

### Reference: Read/Write assembly (Program 3-II)

The manual's example subroutines. Port numbers are decimal in the mnemonics:
port 8 = octal 010 (status), port 9 = octal 011 (control/sector), port 10 =
octal 012 (data).

**Write a sector** (`DSKO`): call with A = number of data bytes to write, HL =
pointer to data buffer.
- `MVI A,136` / `SUB C` → compute number of zero bytes = 136 − (data bytes),
  then B = zeros+1 (a full sector body is 137 bytes; the code pads the remainder
  with zeros).
- `CALL SECGET` — wait for latency / correct sector.
- `MVI A,128` / `OUT 9` — enable write without special current (128 = 0x80 =
  WRITE ENABLE, D7=1 on port 011).
- First byte OR'd with `128` (0x80) to force the sync bit (D7=1) high.
- Poll `IN 8` (status), test the ENWD bit (mask `1` = D0), wait until ready,
  then `OUT 10` (port 012) each byte.
- After the last data byte, keep clocking out zeros while decrementing the zero
  counter B.
- `EI` / `MVI A,8` / `OUT 9` — re-enable interrupts and **unload head**
  (command 8 = 0x08 on port 011). Note: 0x08 = D3, which the port-011 table lists
  as "not used" — the example uses it as an unload/step-settle command in
  practice.

**Read a sector** (`DSKI`): call with HL = pointer to a 137-byte buffer.
- `CALL SECGET` — point to the right sector.
- `MVI C,137` — byte count.
- Poll `IN 8` (status), `ORA A`, `JM` — wait for NRDA (D7, sign bit) true, then
  `IN 10` (port 012), store to buffer, `INX H`, `DCR C`, loop.

**Sector-locate helper** (`SECGET`): disable interrupts, then loop:
`IN 9` (port 011 sector position), `RAR` (rotate D0/Sector-True into carry),
`JC` back if not start of sector, `ANI 15` to get the sector number, `CMP E`
against the desired sector, `JNZ` to keep searching.

---

## 9. Interrupts (Optional)

Not used by Minidisk BASIC. When enabled (port 011 D4=1), the controller
interrupts at the **beginning of every sector**, i.e. every **12.5 ms**.

Uses:
- **Sector search** offloading — only a few µs are needed to read the sector
  count, freeing the CPU between sectors.
- **Step timing** — instead of polling MH status, enable interrupts, issue the
  step after the first interrupt, count 4 interrupts (= 50 ms), then issue the
  next step. Check TRACK 0 status before stepping.

Hardware setup:
- On Controller Board #1, the **SR1 jumper** must connect to the desired
  interrupt pad: **INT (or PINT)** pad for single-level interrupts, or the
  highest-priority level **VI0** for vectored interrupts (see 88-VI/RTC manual).
- The 8080 CPU interrupts must be enabled, then the controller's interrupts
  enabled. CPU interrupts must be re-enabled after each interrupt is serviced.

---

## 10. Drive Address Selection (Hardware, SW-1)

The drive address is set by **switch SW-1** on the drive's Buffer PC board.
Positions 1–2 select the addressing circuit; positions 3–4 select the address
shown on the front panel.

| Address | SW-1 pos 1 | pos 2 | pos 3 | pos 4 |
|---------|-----------|-------|-------|-------|
| 0 | down | down | down | down |
| 1 | up | down | up | down |
| 2 | down | up | down | up |
| 3 | up | up | up | up |

WRITE PROTECT: an unprotected minidiskette has a notch that lets a switch close,
enabling the write circuit. Covering the notch (tape) blocks the switch and
write-protects the disk. The WRITE PROTECT indicator lights only when the drive
is enabled. **Drive must be address 0 to load Minidisk BASIC.**

---

## 11. Loading Minidisk BASIC (Boot)

Requires a write-protected Minidisk BASIC diskette, a bootstrap loader, and ≥ 24K
RAM. Drive must be at address 0 with the BASIC diskette inserted.

### MDBL PROM
The **Minidisk Bootstrap Loader (MDBL)** PROM is used with the **88-PMC** PROM
memory card, placed at the **highest 256-byte block, octal 177400**. To boot:
examine 177400, set the sense switches for the desired I/O device (Table 3-A),
press RUN. The DRWT PROM (diagnostic) sits at the 3rd-highest 256-byte block.

### Boot procedure (MDBL PROM)
1. Examine address **177400** (octal).
2. Set sense switches per Table 3-A for the terminal / load device.
3. Press RUN. BASIC loads and prints the initialization dialog.

The full 384-byte MDBL PROM octal listing occupies 177400–177777 (see the PDF,
Program 3-I, for the byte-for-byte dump if a ROM image is needed).

### Sense Switch Settings (Table 3-A)

Sense switches A8–A15 set before loading. Low nibble (rightmost 4) = load I/O
board; high nibble (leftmost 4) = terminal I/O board.

| Device | Sense Setting (octal) | Terminal Switches | Load Switches | Channels (octal) |
|--------|----------------------|-------------------|---------------|------------------|
| 2SIO (2 stop bits) | 0 | none | none | 20, 21 |
| 2SIO (1 stop bit) | 1 | A12 | A8 | 20, 21 |
| SIO | 2 | A13 | A9 | 0, 1 |
| ACR | 3 | A13, A12 | A9, A8 | 6, 7 |
| 4PIO | 4 | A14 | A10 | 40, 41, 42, 43 |
| PIO | 5 | A14, A12 | A10, A8 | 4, 5 |
| HSR | 6 | A14, A13 | A10, A9 | 46, 47 |
| non-standard terminal | 14 | | | |
| no terminal | 15 | | | |

There are also cassette-tape and paper-tape boot loaders (for use with the
88-ACR / 2SIO), togglable in at address 000; the loader programs are octal dumps
in the PDF (§3-2). If another interface than the 2SIO is used, the loader
programs are on pages 96–99 of the Altair BASIC Reference Manual v4.0.

### Loading error codes
The checksum loader lights the Interrupt-Enable lamp on error; the ASCII error
letter is stored in memory location 0 and sent to all terminal channels:

| Letter | Meaning |
|--------|---------|
| C | Checksum error (bad tape data — defective diskette hard error or alignment problem) |
| M | Memory error (won't store; address of bad location in locations 1 and 2) |
| O | Overlay error (attempt to load on top of the loader) |
| I | Invalid load device (bad sense-switch setting) |

---

## 12. Minidisk BASIC Operation

BASIC is **Altair BASIC REV 4.1 [DISK EXTENDED VERSION]**, resides in lower 24K.
Initialization dialog after load:

- `MEMORY SIZE?` — total memory for BASIC + programs (RETURN = use all).
- `LINE PRINTER?` — `Q` = Q70, `C` = C700, `O` = LP80 (any letter if none).
- `HIGHEST DISK NUMBER?` — number of last drive (0 if one drive, 1 if two).
- `HOW MANY FILES?` — max simultaneously open disk files.
- `HOW MANY RANDOM FILES?` — max random-access files open (≤ HOW MANY FILES).

Then prints `XXXXX BYTES FREE / ALTAIR BASIC REV. 4.1 / [DISK EXTENDED VERSION] /
COPYRIGHT 1977 BY MITS, INC. / OK`.

Disk commands: `LOAD "name",n` / `SAVE "name",n` / `RUN "name",n` /
`MOUNT n` / `UNLOAD n` (omit n = all disks) / `DSKINI n` (initialize a blank
diskette — marks all sectors empty; **destroys all files**; ~2 min/disk). The
write-protected BASIC diskette causes a DISK I/O ERROR when mounted, but reading
still works.

---

## 13. Timing Reference

### Controller Board #1 test-point pulse widths

| TP | Pulse width (nominal, range) | Name | Notes |
|----|------------------------------|------|-------|
| TP-1 | 2.0 µs (1.6–2.4) | Read Clock (mask) | Every 8 µs when enabled and not writing; separates read clock from read data |
| TP-2 | 6.1 µs (5.9–6.3) | Read Data Window | Every 8 µs; most critical time constant (nominal 6.1 µs) |
| TP-3 | 9.6 ms (8.0–11.2 raw window) | Index (Window) Pulse | Every 12.5 ms; separates index from sector pulses |
| TP-4 | 300 µs (150–600) | Sector Pulse (mask) | Every 12.5 ms; separates index from sector pulses |
| TP-5 | 500 µs (400–600) | Read Clear | Every 12.5 ms; clears read circuit at start of sector |
| TP-6 | 9.6 ms (8.0–11.2) | Index Pulse Verification | Every 200 ms; verifies index detection when drive first enabled |
| TP-7 | 30 µs (20–40) | Sector Count (True) | Every 12.5 ms; indicates beginning of sector |
| TP-8 | 1.0 ms (0.9–1.2) | Write Clear | Every 12.5 ms; keeps write registers cleared during preamble (first ~1 ms zeros) |

### Controller Board #2 test-point pulse widths

| TP | Pulse width (nominal, range) | Name |
|----|------------------------------|------|
| TP-1 | 3 µs (1.5–4.5) | Disk Enable |
| TP-2 | 3 µs (1.5–4.5) | Disk Reset |
| TP-3 | 3 µs (1.5–4.5) | Step Out (direction) |
| TP-4 | 0.3 ms (200–400 µs) | Step (pulse) |
| TP-5 | 50 ms (45–75) | Head Settle |
| TP-6 | 1 s (0.9–1.5) | Drive Motor On Delay |
| TP-7 / TP-8 | not used | |

### Raw index/sector pulse timing (from the drive)

- Sector holes generate a **4.4 ms pulse every 12.5 ms**.
- The Index hole (halfway between sectors 15 and 0) generates a **4.4 ms pulse
  every 200 ms** (once per revolution).
- Index/sector circuit is synchronized by the Altair 2 MHz clock.

### Read/write data timing

- Composite read clock + data from drive: **read clock pulse every 8 µs (±1 µs)**;
  a logic-1 read-data pulse appears **4 µs later**.
- A serial-to-parallel shift register assembles 8 read bits; **NRDA asserted every
  64 µs**.
- Write: 1 MHz write clock (2 MHz ÷ 2). Write byte shifted out serially every
  8 µs/bit → **ENWD every 64 µs** per byte.
- **Disk Disable timer**: clocked every 12.5 ms by the start-of-sector clear
  pulse; turns the system off after 6.4 s with no head movement / timer reset.

### Disk Function Control timing (Figure 4-13)

- Step pulse (TP-4 A1-5): 300 µs.
- Step-out direction (TP-3 A1-13): 3 µs.
- Head settle (TP-5 B1-5): 50 ms. Move Head status (E4-8) held for 50 ms.
- Write Enable FF (E2-9): 12.5 ms (one sector).
- Start of Sector Clear (SOS, B4-9): 500 ns ± 250 ns.
- Sector interrupt enable/disable pulses (J1-13 / J1-1): 500 ns.
- Reset interrupt latch / interrupt acknowledge (E3-11): 500 ns.

---

## 14. Preliminary Checkout Static Test (behavioral snapshot)

The Disk Enable static test (§5-1) single-steps output/input instructions and
reads back the expected front-panel lamp patterns. Useful as an emulation sanity
check for what the ports should show:

**After enabling drive 0 (MVI A,000 / OUT 010), reading Sector Count (IN 011):**

| Bit | Behavior |
|-----|----------|
| D0 | ON all the time |
| D1 | ON, flashing very fast |
| D2 | Flashing very fast |
| D3 | Flashing fast |
| D4 | Flashing slowest |
| D5 | OFF all the time |
| D6 | ON all the time |
| D7 | ON all the time |

(D0 = Sector True — mostly high except the 30 µs true window; D1–D4 = the
counting sector nibble at successively slower rates; D5 always 0; D6/D7 always 1.)

**Reading Status (IN 010) with drive enabled:**

| Bit | Behavior |
|-----|----------|
| D0 | ON (ENWD = write circuit not requesting data) |
| D1 | OFF (MH = OK to step head) |
| D2 | OFF (HS = head properly loaded) |
| D3 | OFF (no function) |
| D4 | OFF (no function) |
| D5 | ON if front-panel INTE off |
| D6 | Track 0 indication — OFF if at track 0 |
| D7 | ON, flickering (NRDA = read circuit detecting data) |

**Note:** For the disk-enable static test the manual disconnects the 6.4 s
disable timer by lifting pin 10 of IC B2 (a 4020) on Board #2; otherwise the
motor turns off after 6 s.

---

## 15. Hardware Notes (for completeness)

- **Controller:** 2 S-100 slots; 57 ICs (57 TTL logic + 1 CMOS 4020 + 2 voltage
  regulators). Power: 1.4 A @ 8 V.
- **Drive mechanism:** Shugart SA-400. Drive cabinet power: 25 W standby / 35 W
  operating; 110 V or 220 V, 50/60 Hz. Fuse: 1 A 3AG slow-blow (110 V) or
  0.5 A (220 V).
- **Reliability:** soft errors 1 per 10^8 bits read; hard errors 1 per 10^11
  bits read; MTBF 8000 hr; media life 3.0 × 10^6 passes/track.
- **Interconnect:** 26-conductor cable, 26-pin sockets both ends. Bulkhead
  connectors reverse wire order; two reversals net to straight-through (1→1).
  Pin 1 is marked; pins 25 and 26 are the two blank pins on the 26-pin socket.
- **Board part numbers:** Controller #1 PC board 100216; Mini Disk #2 PC board
  100222; DCCA adapter board 100223; Buffer PC card 100224. Blank diskette MITS
  part 102501. System manual part 101571.
- **Diagnostic PROMs:** MDBL (bootstrap) at highest 256-byte block (177400);
  DRWT (floppy + minidisk read/write test) at 3rd-highest block — both on 88-PMC.

Schematics: Figure 4-14 (Board #1, 3 sheets: Address/I-O select + sector, read +
status, interconnections), Figure 4-15 (Board #2, 2 sheets: drive control +
write, interconnections), Figure 4-16 (Buffer/Power Supply). Refer to the PDF for
gate-level detail; the emulation-relevant behavior is captured above.
