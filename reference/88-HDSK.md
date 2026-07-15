# Altair Hard Disk (88-HDSK) — Datakeeper Controller

Source: [88-HDSK.pdf](#)

MITS 88-HDSK "Datakeeper" hard disk system, Preliminary Documentation,
October 1977. This reference distills the parts needed to emulate the
controller in software: disk geometry, the 4PIO port map, the command
protocol, the controller's internal "IV byte" registers, error/status
flags, on-disk sector format, timing, and the Datakeeper BASIC file
system. Circuit-level theory of operation, wiring lists, and
troubleshooting (PDF pp. 47–170) are omitted as not relevant to emulation.

**All numeric errata from the MITS errata sheets are folded in below and
marked "(errata)".** The corrected assembly-language driver routines in the
errata are the most authoritative description of the protocol.

---

## 1. System overview

- Controller: **Datakeeper Controller**, a self-contained box with its own
  5-slot bus, 5 V supply, and three cards. It talks to the Altair 8800 over
  **two parallel I/O ports** and to the drive over a 24-in/24-out cable.
- Processor: **8X300 (Signetics) bipolar microprocessor**, TTL ROM firmware,
  **1 K of buffer RAM = four 256-byte data buffers**.
- Drive: **Pertec D3422-E024-NWU**, one fixed platter (~5 MB) + one top-load
  5440-type removable cartridge (~5 MB). Up to **4 drives** daisy-chained.
- Serial data rate drive↔controller: **2.5 Mbit/s** (2200 BPI, 2400 RPM).
- Data rate controller↔Altair: asynchronous, handshaked, 0–2.5 MB/s.
- Software: a special version of **Altair Extended (MITS) BASIC 4.1**
  ("Datakeeper BASIC") on the cartridge, plus a bootstrap PROM.

### Controller cards
| Card | Function |
|------|----------|
| Processor Card | 8X300, TTL ROM, 1 K buffer RAM (4×256), 2 bidirectional I/O ports to the Altair (via 8T32 ports = "IV bytes"). |
| Disk Data Card | serial↔parallel converters, 9403 FIFO registers, 9401 CRC generator/checker, bit counters. No external connectors. |
| Disk Interface Card | write-data-rate clock (74S124 VCO), I/O ports, line drivers to the drive (24 in / 24 out), interfaces up to 4 drives. |

Cards are bus-oriented and may sit in any slot. Power supply has
over-current protection; a trip drops DC power (reset by power-cycling the
switch).

---

## 2. Disk geometry (Pertec D3422)

| Parameter | Value |
|-----------|-------|
| Drive type | Pertec D3422-E024-NWU |
| Fixed platter capacity | 4,988,928 data bytes |
| Removable cartridge capacity | 4,988,928 data bytes |
| Total capacity | 9,977,856 data bytes |
| Tracks per inch | 200 |
| Cylinders | 406 (addresses 0–405) |
| Disk surfaces (heads) | 4 |
| Tracks | 1624 |
| Sectors per track | 24 (0–23) |
| Data bytes per sector | 256 |
| Spindle speed | 2400 RPM = 40 rev/s |
| Recording density | 2200 BPI |
| Serial transfer rate | 2.5 Mbit/s |

**Access timing:** latency max 25.0 ms / typ 12.5 ms; seek: adjacent-track 10 ms
min, average (1/3 stroke) 40 ms, full-stroke 65 ms max; total max access to
read a sector = **92 ms** (25 latency + 65 seek + 2 read). Minimum non-buffered
read ~2 ms. Seeks may be overlapped with buffer transfers and with seeks of
other drives.

### Page/allocation model
- The addressable unit is a **page = one sector = 256 data bytes**.
- A **logical drive = one platter = 2 head surfaces**. It has
  **19,488 pages, numbered 0–19,487**.
- `4 surfaces × 24 sectors × 406 cylinders = 38,976 pages` per **unit** (both
  platters); `× 256 = 9,977,856` total usable bytes.
- Space is allocated in **groups of 8 pages** ("group" = 8 pages).

Other supported Pertec drives: D3412 (1 removable platter), D3462 (3 fixed
+ 1 removable). Head range narrows with fewer platters: 1 platter → heads
0–1, 2 platters → heads 0–3, 4 platters → heads 0–7.

---

## 3. Terminology
- **unit** = physical drive. Address 0–3, set by a front-panel thumbwheel
  switch (1–4). `unit 0 = drive switch 1`, unit 1 = switch 2, etc. Each unit
  address must be unique.
- **platter** = **logical drive** = 2 head surfaces. D3422 = up to 2 platters
  (1 removable + 1 fixed).
- **cylinder** = radial head position (0–405).
- **sector** = rotational position (0–23).
- **page** = 256-byte sector; **group** = 8 pages.

Figure 3-2 head-surface assignment for the D3422:
- Removable platter: Head Surface 0 (top), Head Surface 1 (underside).
- Fixed platter: Head Surface 2 (top), Head Surface 3 (underside).

---

## 4. Host interface: 4PIO port map

The controller connects to an Altair **88-4PIO** addressed at **240 octal =
160 decimal = 0xA0**. Ports are used as two 16-bit command/data paths plus
handshake status lines. All I/O addresses below are **decimal**.

| Dec | Octal | Hex | Dir | Name (this doc's mnemonic) | Function |
|-----|-------|-----|-----|-----|----------|
| 160 | 240 | A0 | IN  | CREADY (Port 1A ctrl) | **Controller Ready** status; bit 7 = ready. |
| 161 | 241 | A1 | IN  | CSTAT (Port 1A data)  | **Command response / error flags** (read after command). |
| 162 | 242 | A2 | IN  | (Port 1B ctrl) | **Command-acknowledge** status; bit 7 high = command received. |
| 163 | 243 | A3 | OUT | ACMD (Port 1B data)   | **Command out** — high-order byte of a command; writing it *initiates* the command. |
| 164 | 244 | A4 | IN  | CDSTA (Port 2A ctrl)  | **Read-data/status ready** status; bit 7 high = read byte ready. |
| 165 | 245 | A5 | IN  | CDADA (Port 2A data)  | **Primary data in** — Read Buffer data / Read Status data. |
| 166 | 246 | A6 | IN  | ADSTA (Port 2B ctrl)  | **Write-data ready** status (ADPA); bit 7 high = OK to send next byte. |
| 167 | 247 | A7 | OUT | ADATA (Port 2B data)  | **Data out** — low-order byte of a command / Write Buffer data / Set Byte data. |

Status bits are always **bit 7 (MSB)** of the status ports (test with
`ANI 128` / `AND 128`). Reading the paired data port clears its handshake:

- Reading **161** resets CREADY (bit 7 of 160) low, and resets the
  command-ack strobe.
- Reading **167**/writing **167** clears ADPA (bit 7 of 166).
- Reading **165** clears CDA (bit 7 of 164).
- Writing **163** strobes the "Altair command ready" line to the controller.

### 4PIO initialization (Table 3-C)
Perform before any communication:

| Port | Clear register | Set direction | Set register |
|------|----------------|---------------|--------------|
| 1A | `OUT 160,0` | `OUT 161,0`   | `OUT 160,44` |
| 1B | `OUT 162,0` | `OUT 163,255` | `OUT 162,36` (\*) |
| 2A | `OUT 164,0` | `OUT 165,0`   | `OUT 164,44` |
| 2B | `OUT 166,0` | `OUT 167,255` | `OUT 166,44` |

(\*) CB2 will hold LOW until CB1 is strobed.

### Handshake line summary (Table, p. 32)
Input lines to Altair (controller → computer status), each is bit 7 of its port:
- Port 1 CA1 → **Controller Ready** (bit 7 port 160).
- Port 1 CB1 → **Command acknowledge / command received** (bit 7 port 162).
- Port 2 CA1 → **Ready for Read Buffer** / read data byte ready (bit 7 port 164).
- Port 2 CB1 → **Ready for Write Buffer** / OK to write data byte (bit 7 port 166).

Output lines from Altair (computer → controller):
- Port 1 CB2 → **Altair Command Ready** (command sent) — set by writing 163.
- Port 2 CA2 → **Read Buffer Strobe** (data byte read) — set by reading 165.
- Port 2 CB2 → **Write Buffer Strobe** (data byte written) — set by writing 167.
- Port 1 CA2 → not used.

Timing constants the firmware guarantees (errata): the controller sets the
Altair data-port-available bit (ADPA, 166.7) about **4.5 µs** after receiving
a command, and sets Controller Ready (160.7) about **1 µs** after finishing a
command / receiving the last data byte — fast enough that a driver need not
spin-wait on them.

---

## 5. Command set

There are **seven commands**. Each command is a 16-bit word sent as the
**low byte to port 167 first, then the high byte to port 163** (writing 163
initiates it). The command *type* lives in bits 15–12 of the word.

Low byte = port 167 (bits 7–0), high byte = port 163 (bits 15–8).

| Command | Bits 15–12 | High-byte opcode base | Low byte (167) | High byte extra fields (163) |
|---------|-----------|----------------------|----------------|------------------------------|
| **Seek Cylinder**  | 0000 | `00h` (CSEEK)  | cylinder bits 7–0 | bit8=cyl bit8; bits 10–11 = unit |
| **Write Sector**   | 0010 | `20h` (CWRSEC) | head(bits7–5)·sector(bits4–0) | bits 8–9 = buffer; bits 10–11 = unit |
| **Read Sector**    | 0011 | `30h` (CRDSEC) | head(bits7–5)·sector(bits4–0) | bits 8–9 = buffer; bits 10–11 = unit |
| **Write Buffer**   | 0100 | `40h` (CWRBUF) | transfer length (byte count) | bits 8–9 = buffer |
| **Read Buffer**    | 0101 | `50h` (CRDBUF) | transfer length (byte count) | bits 8–9 = buffer |
| **Read Status**    | 0110 | `60h` (CRSTAT) | status-word (IV-byte) address | bits 10–11 = unit |
| **Set Byte**       | 1000 | `80h` (CSETIV) | IV-byte data address | — |

Field ranges: unit 0–3, head 0–7, sector 0–23, buffer 0–3, cylinder 0–405,
transfer length 0–255 (**a length of 0 means 256 bytes**).

**Head field is bits 5,6,7 of the low byte** (p. 23 text: "Bits 5, 6 and 7
indicate the head number"; the corrected assembly routines rotate head into
bits 7:5, i.e. `head × 32`). *Caution:* the errata BASIC listings compute
`H*64` for head, which is inconsistent with the bit diagram and the assembly
routines — prefer the `head × 32` (bits 5–7) placement, or see Table 3-E.

### BASIC command formulas (from the errata listings)
Let `D` = drive 1–4 (`unit = D-1`), `BF` = buffer 0–3, `H` = head, `SC` =
sector, `TK` = cylinder, `L` = length.

- **Seek:**   `OUT 167, TK AND 255` ; if `TK>255` then cyl-bit8=1;
  `OUT 163, (D-1)*4 + cylbit8 + 0`
- **Write Sector:** `OUT 167, H*32 + SC` ; `OUT 163, (D-1)*4 + BF + 32`
- **Read Sector:**  `OUT 167, H*32 + SC` ; `OUT 163, (D-1)*4 + BF + 48`
- **Write Buffer:** `OUT 167, L MOD 256` ; `OUT 163, 64 + BF` ; then write `L` data bytes to 167
- **Read Buffer:**  `OUT 167, L MOD 256` ; `OUT 163, 80 + BF` ; then read `L` data bytes from 165
- **Read Status:**  `OUT 167, ivaddr` ; `OUT 163, (D-1)*4 + 96` ; read result from 165 (and error flags from 161)
- **Set Byte:**     `OUT 167, ivaddr` ; `OUT 163, 128` ; then `OUT 167, data`

Note Read/Write Sector operate on **the cylinder the head is currently
positioned on** — issue a Seek first.

### Command protocol (canonical sequence)
The corrected assembly driver does this per command (waiting for completion
*after*, not readiness before):

1. `IN 163`/`IN 167` etc. to clear stale handshake (`IN ACMD` resets the
   command-ack; `IN ADATA` clears ADPA; `IN CDATA` clears CDA).
2. Write low byte to **167**.
3. Write high-byte opcode to **163** → initiates command.
4. Poll status per data-bearing phase:
   - **Write data** (Write Buffer / Set Byte data): wait ADPA (`IN 166` bit 7),
     then `OUT 167, byte`.
   - **Read data** (Read Buffer / Read Status): wait CDA (`IN 164` bit 7),
     then `IN 165`.
5. Wait **Controller Ready** (`IN 160` bit 7) for completion.
6. Read **error flags** from **161** (see §6). For Read/Write Sector, Seek,
   the flags reflect the operation; write-protect is not an error for reads.

Driver timeout constants used by MITS (µs = T-states/2 at 2 MHz): send/read
IV byte ~4.7 ms (`256×37/2`); Read/Write Sector ~125 ms (`5102×49/2`, 5
rotations); Seek ~130 ms (`5106×49/2`, 2× max seek). Rotation period = 25 ms.

The controller **waits for the addressed drive to be Ready** before it will
read any IV byte or complete a Read Status; a not-ready drive stalls the
command (Read Status won't complete). For bench testing with no drive, the
Ready line on P2 must be pulled low.

---

## 6. Error / status flags (port 161)

Read from **port 161** after a command completes (or via Read Status). Bit =
1 means the condition is present (errata-corrected table):

| Bit | =1 means | Occurs in |
|-----|----------|-----------|
| 0 | Drive not ready | any command except Set Byte / initialize |
| 1 | Illegal sector | Seek, Read Sector, Write Sector, Format, Read Unformatted |
| 2 | CRC error in sector data (read) | Read Sector, Read Unformatted |
| 3 | CRC error in header read | Seek, Read Sector, Write Sector, Format |
| 4 | Header has wrong sector | same as bit 3 |
| 5 | Header has wrong cylinder | same as bit 3 (may fire spuriously if command targets a drive other than the last seek; ignored by write logic) |
| 6 | Header has wrong head | same as bit 3 |
| 7 | Write protect | Write Sector (and Format, per errata) |

Notes:
- **All bits read as 1 on the first read of port 161 after the controller is
  powered on.**
- Bit 2 (data CRC) always sets on an *unformatted* read of a formatted sector.
- Write-protect: on a write to a protected sector, data is not written, the
  Write Sector command is ignored, and the flag is set. Write-protect is *not*
  treated as an error by the Read/Write Sector assembly routines (they mask
  it with `ORI 7Fh` before the zero test).

BASIC error-decode convention (Read/Write Sector):
`1=not ready, 2=illegal sector, 4=CRC error in sector data, 8=CRC error in
sector header, 16=header wrong sector, 32=header wrong cylinder, 64=header
wrong head, 128=write protected`.

---

## 7. Head / surface selection (Table 3-E)

For Read Sector / Write Sector, the head number selects a surface via low-byte
(channel 167) bits 7,6,5:

| Head | bit7 | bit6 | bit5 | Surface / cartridge |
|------|------|------|------|---------------------|
| 0 | 0 | 0 | 0 | Top, Removable |
| 1 | 0 | 0 | 1 | Bottom, Cartridge (removable underside) |
| 2 | 0 | 1 | 0 | Top, Fixed |
| 3 | 0 | 1 | 1 | Bottom, Fixed Platter |
| 4 | 1 | 0 | 0 | Top, Extended |
| 5 | 1 | 0 | 1 | Bottom, Fixed Platter 1 |
| 6 | 1 | 1 | 0 | Top, Extended |
| 7 | 1 | 1 | 1 | Bottom, Fixed Platter 2 |

For the standard 2-platter D3422, only heads 0–3 exist.

---

## 8. On-disk sector format

Each sector on the medium is laid out as (Fig., p. 42):

```
| PREAMBLE (all 0s, | SYNC | DATA (256 bytes) | CRC     | POSTAMBLE |
|  length set by    | BYTE |                  | 2 bytes |           |
|  firmware)        | (1)  |                  |         |           |
|<---------------------------- 1.04 ms -------------------------->|
```

- **Preamble**: all zero bits, length determined by firmware.
- **Sync byte**: a leading `0`s then a `1` bit marks data start (sync
  detection).
- **Data**: 256 bytes.
- **CRC**: 2 bytes (9401 generator/checker), appended when CRC-append is
  enabled.
- Whole sector spans **1.04 ms** (= 1 / (40 rev/s × 24 sectors)).

---

## 9. Controller internal registers ("IV bytes")

The controller's internal state lives in **8T32 bidirectional I/O ports**
called *IV bytes*, each with an address. The host reaches them only through
the **Set Byte** command (write) and the **Read Status** command (read). These
are diagnostic/low-level; normal disk I/O uses the seven commands above.

**Bit-order caveat (errata):** for every IV byte in Table 3-D, the 8T32
"user data" bit/pin order is **reversed** relative to how the Altair sees it
via Set Byte / Read Status:

| 8T32 user bit (pin) | Altair bit |
|---------------------|-----------|
| 0 (pin 8) | 7 |
| 1 (pin 7) | 6 |
| 2 (pin 6) | 5 |
| 3 (pin 5) | 4 |
| 4 (pin 4) | 3 |
| 5 (pin 3) | 2 |
| 6 (pin 2) | 1 |
| 7 (pin 1) | 0 |

Additionally, **IV bytes H, I, J (addresses 17, 18, 19) invert the data** —
the value written must be inverted. (E.g. to select cylinder-address 0 in IV
byte J, write 255.)

### Processor Card IV bytes
| Addr | Name | Dir | Contents |
|------|------|-----|----------|
| 1 | X | out | RAM address bits 9,8 (selects 1 of 4 buffers) + IV-byte direction controls for A8–A13. |
| 2 | A8 | out | **Error/command-response byte to computer** (P1-PA, read as IN 161). *(errata: A8 communicates error conditions to the computer; the "not used" comment in the original is wrong.)* |
| 3 | A9 | in  | Upper 8 bits of command from Altair (P1-PB, OUT 163). |
| 6 | A12 | out | Read Buffer data / status to computer (P2-PA, IN 165). |
| 7 | A13 | in  | Write data / Set Byte data / lower 8 command bits from computer (P2-PB, OUT 167). *(errata: direction is computer→controller.)* |
| 4 | A10 | out | Handshake strobes to Altair: bit0=P2-CB1 (ready for write data / low command byte, sets 166.7), bit1=P2-CA1 (ready for read/status, sets 164.7), bit6=P1-CB1 (command ack, sets 162.7), bit7=P1-CA1 (ready for new command, sets 160.7). |
| 5 | A11 | in  | Handshake strobes from computer: bit0=P2-CB2 (write to 167), bit1=P2-CA2 (read 165), bit6=P1-CB2 (write to 163, latched low; reset when CB1 strobed), bit7=P1-CA2 (read 161, not used). |

### Disk Data Card IV bytes
| Addr | Name | Dir | Contents |
|------|------|-----|----------|
| 34 | B | out | **Write data to FIFO**: bits0–3 = high nibble (IC F), bits4–7 = low nibble (IC M). |
| 33 | A | in  | **Read data from FIFO**: high nibble (IC F) + low nibble (IC M). |
| 35 | C | out | *(errata: output, not input)* Bit-counter load: load pulses (enabled one at a time) + data loaded into the bit counter = number of bits to transfer during a read/write. |
| 36 | D | in  | **Status**: bit5 = **DRDST** (HIGH = CRC error after read; *errata: the read-CRC result is only on bit 5, not bit 3*); bit6 = **DTRCMP** (HIGH = data transfer complete, end of sector data); bit7 = **TRR** (HIGH = transfer request, requests a data-byte transfer). |
| 37 | E | out | *(errata: address 37 is IV Byte E, not B)* **Control**: bit0=**CRCAPE** (1=CRC append enable); bit1=**DISTRAN STRT** (1=start read/write of data); bit2=**DISRMD** (1=read mode, 0=write mode); bit4=**CLR** (active-low, clears registers+latches at start of sector R/W — *errata: CLR is bit 4, not bit 5*); bit6=**TRAS** (transfer-ack strobe, after each FIFO byte); bit7=**SDSEL** (source data select, 0=processor data normal, 1=cache — optional/unavailable). |

### Disk Interface Card IV bytes
| Addr | Name | Dir | Contents (to/from drives) |
|------|------|-----|----------|
| 17 | H | out | Drive function control: bit0=Start/Stop all drives; bit1=Extension Select; bit2=Platter Select; bit3=Head Select; bits4–7=Drive Select 4/3/2/1. |
| 18 | I | out | bit0=Emergency Unload (all); bit1=Offset plus; bit2=Offset minus; bit3=Enable Write; bit4=Cylinder Restore (LOW + cyl strobe = slow seek to 0); bit5=Cylinder Strobe (goes LOW then HIGH; active going HIGH); bit7=Cylinder Address 8. |
| 19 | J | out | Cylinder Address bits 7–0 (8 of 9 cylinder-address lines). |
| 20 | K | in  | bit0=Malfunction; bit3=Extension Status; bit5=Dual Platter; bit7=Double Track. |
| 21 | L | in  | bit0=Ready; bit1=Index Pulse; bit2=File Protected; bit3=Illegal Address; bits4–7=Busy Seeking 4/3/2/1. |
| 22 | M | in  | bit0=Sector Pulse; bits1–7=Sector Count 6–0. |

Head/surface select line polarities (IV byte H):
- **Extension Select:** LOW = bottom two (extended) platters; HIGH = upper two.
- **Platter Select:** LOW = top (removable) platter; HIGH = bottom (fixed) platter.
- **Head Select:** LOW = top surface of selected platter; HIGH = bottom surface.

**Read Status side-effect (errata):** the Read Status command *rewrites 7
bits of IV byte H (addr 17)* — user bits 4–7 = the unit bits from the command
(command bits 3:2); user bits 1:3 are forced to `011b` (Extension Select low,
Platter and Head select high); bit 0 (Start/Stop all) is unchanged.

---

## 10. Firmware flow (8X300 ROM)

Reset/init: reset drive interface, select drive 1, restore to track 0. Then
the main loop: set Controller Ready to Altair → wait Altair Ready → transfer
command + parameters to registers → send Ack to Altair → wait Altair Ack →
dispatch through a branch table on the command type:

| Code | Command |
|------|---------|
| 1 | Seek Cylinder |
| 2 | Sector Transfer (Read/Write Sector) |
| 3 | Buffer Transfer (Read/Write Buffer) |
| 4 | Status Transfer (Read Status) |
| 5 | Set Byte |
| 6, 7 | unassigned |

- **Seek:** send address to drive, set strobe line, return to Comm.
- **Sector:** set up sector/track params; if read → set up Disk Data Card for
  read, set buffer (RAM) pointers, wait for proper signal, start transfer,
  loop (data available? → transfer to RAM) until end of transfer, stop
  transfer; write is virtually the same as read.
- **Buffer:** set RAM pointer; loop transferring bytes to/from RAM,
  handshaking each with the Altair strobe (write mode → transfer to RAM),
  increment counter until done.
- **Status:** read requested status byte, send to Altair.

---

## 11. Boot / initialization

- Bootstrap PROM label **HD-LDR, 103292**, resides at **176000 octal
  (374000)**.
- **8800b:** install HD-LDR in PROM socket E of the 88-PMC (PMC wired for
  highest address). Boot: RESET, examine 176000, set sense switches per the
  BASIC 4.1 manual (p. 101), RUN.
- **8800b Turnkey:** install HD-LDR in Turnkey Module socket L1, auto-start
  address 374000, 1 K RAM at an unused area (e.g. 48–49 K). Powers up and
  auto-starts at 176000.
- Power-on order: Altair on → Datakeeper controller to RESET → drive power ON
  (wait for SAFE light) → drive RUN/STOP (motor spins up ~1 min to READY) →
  set Datakeeper to RUN → then boot the Altair. Power-down reverses this
  (drive off before controller off) to avoid emergency unload.
- Minimum Altair config: 8800b, 48 K RAM, 88-4PIO at 240 octal + an 88-PP,
  88-PMC PROM card at highest address, 88-2SIO terminal at 020 octal.

---

## 12. Datakeeper BASIC (file system)

MITS BASIC 4.1, nearly identical to the floppy version:

- **Disk organization:** each platter is one "disk". For a 10 MB (2-platter)
  unit 0, the fixed platter is **disk 0** and the removable is **disk 1**;
  these numbers are used wherever a disk number is needed.
- **Max file size:** a file cannot span a platter → ~5 MB, or **37,500 random
  records** (128 bytes/record).
- **Max files:** directory is fixed-size → ~**500 files per platter** (vs 255
  per floppy).
- `DSKI$` and `DSKO$` primitives are **not** included.
- Backspace via **Ctrl-H** (ASCII 010 octal), non-echoing.
- `PIP` is not provided. Utilities on the system platter: **COPYFLOP**
  (copy named files from floppy tracks 6–76 to Hard Disk 0), **COPYHARD**
  (copy one platter to another, initializes cartridge), **DIRLIST** (sorted
  directory), **HELP** (`RUN "HELP",0`), **STARTREK**. Data files (not
  runnable): COPFTH, HDCPYBS, HELP.TXT. Before running utilities, limit
  MEMORY SIZE to **44000** in the init dialog.

### File-manager error codes (BASIC prints code in hex)
| Hex | Dec | Meaning |
|-----|-----|---------|
| 01 | 1 | Undefined system error |
| 05 | 5 | Invalid mode parameter |
| 07 | 7 | Unable to find buffer to allocate (system error) |
| 09 | 9 | Invalid drive number parameter |
| 0A | 10 | Attempt to write to volume mounted read only |
| 13 | 19 | Volume already mounted |
| 15 | 21 | Invalid drive number parameter in mount |
| 17 | 23 | Drive not mounted |
| 1F | 31 | File name not found |
| 21 | 33 | Too many open files — out of open entry blocks |
| 23 | 35 | Internal inconsistency, out of index entry blocks |
| 27 | 39 | Invalid file number parameter |
| 29 | 41 | File number not opened |
| 2D | 45 | CRC error on read of sector from disc |
| 2F | 47 | Attempt to open read-only file in write mode |
| 31 | 49 | Not enough space on volume |
| 33 | 51 | File name already on volume |
| 35 | 53 | Not enough space in directory for new file |
| 39 | 57 | Controller did not respond to Seek command — reset controller |
| 3B | 59 | Controller did not respond to Read Sector command — reset |
| 3D | 61 | Controller did not respond to Read Buffer command — reset (could also return ERR=1) |
| 3F | 63 | Controller did not respond to Write Sector command — reset |
| 40 | 64 | Drive is not on line |
| 43 | 67 | Controller did not respond to Status command — reset |
| 44 | 68 | Controller did not respond to Write Buffer command — reset |
| 47 | 71 | Not enough space for internal tables in set-up |
| 49 | 73 | Invalid number of buffers in set-up |
| 4B | 75 | Invalid number of drives or files parameters in set-up |
| 4D | 77 | Attempt to write to disc with write-protect switch set |
| 4F | 79 | Unable to update directory (write protect / drive on line) |
| 55 | 85 | Attempt to read past end of file |
| 57 | 87 | Internal inconsistency, attempt to allocate to allocated group |
| 59 | 89 | Attempt to access invalid logical page |
| 5B | 91 | Internal inconsistency, no free bit during allocation |
| 5D | 93 | Attempt to write to file opened read-only / kill read-only file |
| 5F | 95 | Unable to update directory entry (write protect) |
| 61 | 97 | Unable to update volume descriptor (write protect) |
| 63 | 99 | Internal inconsistency, drive number for open file invalid |
| 67 | 103 | Internal inconsistency, index pointer for open file invalid |
| 69 | 105 | Invalid seek mode parameter |
| 6B | 107 | Controller did not respond to Read Buffer in compare |
| 6D | 109 | Data written to disc did not compare on read-back |
| 6F | 111 | Error closing file during dismount, volume still mounted |
| 71 | 113 | File is open, cannot kill until closed |
| 73 | 115 | Error writing page to disc from buffer pool (write protect) |
| FF | 255 | Invalid function parameter to driver |

---

## 13. Key logic parts (for reference)
8X300 bipolar processor; **8T32** bidirectional I/O ports (the IV bytes);
**9401** CRC generator/checker; **9403** FIFO buffer memory; 74S124 dual VCO
(write-data-rate clock); 74S472 PROM (firmware); 74LS191 counters; 74S138
decoders; 74LS377 registers.
