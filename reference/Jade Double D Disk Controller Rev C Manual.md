# Jade Double D — Double-Density Disk Controller (Rev C)

Source: [Jade Double D Disk Controller Rev C Manual.pdf](#)

Jade Computer Products, *Double D, The Double Density Disk Controller, Hardware Manual*,
IOD-1200M, Revision C, © 1980. The scan is 47 PDF pages. It is a 2004 retype of the manual,
with the period figures pasted in. Ten Engineering Change Notices (ECNs, August 1980 to May
1981) come before the manual. The Double D is an **intelligent** S-100 floppy controller. It
has its own Z80A, 2 K of static RAM and a WD FD1791. The host never touches the FD1791. The
host talks to the board through one I/O port and a 1 K memory window. This file leaves out
the assembly steps and most of the parts list. **Not emulated.**

**The scan is incomplete.** The table of contents lists Appendices D (S-100 bus connections),
E (schematics) and F (feedback). The PDF ends after Appendix C, at printed p. 38. The download
is complete (it ends in `%%EOF`), so the retype itself does not include them.

**Shipped software checked against it.** The maintainer's `deltecent/jadedd-cpm22` repository
holds Jade CP/M 2.2 Release 2 as period source: `DCM2.ASM` (the on-board Z80 program, "Disk
Controller Module", Release 2A, by Stan Krumme), `DDBOOT.asm` (the host boot PROM at F000h)
and `DDBIOS.ASM` (the host CBIOS). §2 and §3 cite them where they confirm or add to the
manual. The *Double D Software Manual* (IOD-1201M, Release 2, 105 pp, no text layer) is in the
same repository and is **not** distilled here.

Page numbers below are the printed numbers. The front matter (cover, ECNs, contents) is PDF
pp. 1–9 and is cited as "ECN n". After that, printed page *n* is PDF page *n* + 9. Printed
pp. 18 and 32 are blank.

---

## 1. Quick reference for emulation

| Item | Value | Page |
|---|---|---|
| Host I/O port | one port, **40h–43h** (switches P0, P1). The Jade software uses **43h** | 2, 4 |
| Host port OUT | Disk Processor **Control** Port | 5 |
| Host port IN | Disk Processor **Status** Port | 6 |
| Memory window | one **1 K** block on a 1 K boundary, **E000h–FC00h** (switches M10–M12) | 3 |
| Window contents | one of the two 1 K halves of the board's 2 K RAM, chosen by `ZINT` | 5–6 |
| On-board processor | Z80A, **4 MHz** (8″) or **2 MHz** (5″). DCM2 runs it in **IM 1** | 6 |
| FDC | WD **FD1791-01**, **2 MHz** clock (8″) or **1 MHz** (5″), at Z80 ports 04–07 | 6–8 |
| Crystal | 8.000 MHz | 6 |
| On-board RAM | 2 K (four 2114), mirrored 32 times in the Z80's 64 K | 7 |
| Recording | FM single density, MFM double density | 2 |
| Drives | up to four, 8″ (50-pin J3) or 5″ (34-pin J2), single or double sided | 1, 8 |
| Host interrupt | optional, one of **VI0*–VI7*** by jumper, raised when the Z80 executes `HLT` | 12 |
| Serial | one EIA input and one EIA output, at the 8-pin DIP socket J4 | 2, 12 |
| Bus | IEEE 696.1/D2 signals. 16-bit address, or 24-bit with IC 4B added | 1, 3 |

The model: the host takes the Z80's bus, writes a command into the window, gives the bus
back and interrupts the Z80. The Z80 does the disk work and executes `HLT` when it is done.
The host sees the halt in the status port, or as a vectored interrupt. §2.6 gives the
sequence that the Jade software uses.

---

## 2. Host side

### 2.1 Port address (Table 2-2, p. 4)

The board decodes A0–A7. P1 and P0 set the two low bits. A closed switch is 0.

| P1 | P0 | Port |
|---|---|---|
| Closed | Closed | 40h |
| Closed | Open | 41h |
| Open | Closed | 42h |
| Open | Open | 43h |

### 2.2 Memory window (Table 2-1, p. 3)

M12, M11 and M10 set A12, A11 and A10 of the window. A13–A15 are always 1, so the window is
always in E000h–FFFFh. A closed switch reads as 0 on the matching `SA` signal.

| M12 | M11 | M10 | SA12 SA11 SA10 | Window |
|---|---|---|---|---|
| Closed | Closed | Closed | 0 0 0 | E000–E3FF |
| Closed | Closed | Open | 0 0 1 | E400–E7FF |
| Closed | Open | Closed | 0 1 0 | E800–EBFF |
| Closed | Open | Open | 0 1 1 | EC00–EFFF |
| Open | Closed | Closed | 1 0 0 | F000–F3FF |
| Open | Closed | Open | 1 0 1 | F400–F7FF |
| Open | Open | Closed | 1 1 0 | **F800**–FBFF ⚠ printed `FE00` |
| Open | Open | Open | 1 1 1 | FC00–FFFF |

- **Address mode jumper** (p. 25). `M`–`S` = 16-bit bus, IC 4B not needed. `M`–`E` = 24-bit
  bus, IC 4B (74LS30) fitted. In extended mode the window is a 1 K block in the top 8 K of the
  24-bit space. Fig 2-1 gates A16–A23 through 4B, with cuttable links on A20–A23.
- **PHANTOM\*** (p. 13, ECN 6). The board can drive PHANTOM\* when the host reads the
  window. This lets the window overlap a 64 K RAM board that cannot release a 1 K hole. The
  data-out driver turns on about 120 ns late (R13/C15), so that the other memory has time to
  turn off. The path is open as shipped. A jumper in the `PHAN*` block below IC 4D connects
  it. The overlapped memory board must respond to PHANTOM\*.
- The window answers the bus **only while the Z80 has granted its bus** (SLVACK\* low, p. 13).
- The DDBOOT PROM lives at F000h, so a Jade system sets the window in E000h–EC00h.

### 2.3 Disk Processor Control Port — host OUT (p. 5, Fig 2-4)

Strobed by sOUT, pWR\* and a port match (BPA\*).

| Bit | Name | Function |
|---|---|---|
| D0 | `SLVRQ` | 1 = request the on-board memory for the host. Drives the Z80's `BUSRQ*`. When the Z80 answers with `BUSACK*` (`SLVACK*`), the window is live on the bus. 0 = give the memory back to the Z80. |
| D1 | `ZINT` | Two jobs. **(a)** 1 = maskable interrupt (`INT*`) to the Z80. **(b)** While the host has the memory, `ZINT` is internal address bit **IA10**: it selects which 1 K half of the 2 K RAM is in the window. |
| D2–D6 | — | Not used. |
| D7 | `ZRST` | 1 = fire one-shot 1A, which pulses the Z80's `RESET*`. |

The values that the Jade software writes (`DDBOOT.asm`, `DDBIOS.ASM`):

| Value | DDBOOT name | Effect |
|---|---|---|
| 01h | `DC$MRQ`, `DC$MB0` | Request the memory, bank 0 in the window |
| 03h | `DC$MB1` | Request the memory, bank 1 in the window |
| 00h | `DC$MRT` | Release the memory |
| 02h | `DC$EXC` | Release the memory **and** interrupt the Z80 |
| 80h | `DC$BGN` | Release the memory **and** reset the Z80, which starts at 0000h |

- **Board reset** (`BR*`, from SLVCLR\*, or from POC\* with the `BRST*`–`POC*` jumper) sets the
  port to the state where the **memory is switched onto the bus** (p. 5). After reset the
  host owns the window and can load the Z80's program before the Z80 runs.
- While the Z80 has granted the bus it does not take an interrupt. This is why `ZINT` can act
  as the bank bit (p. 6).
- The Z80 clears `ZINT` itself with an `IN` from its port 20h (§3.2).

### 2.4 Disk Processor Status Port — host IN (p. 6, Fig 2-5)

Read with pDBIN, sINP and a port match. Bit positions are from Fig 2-5 (74LS244 at 4H).

| Bit | Name | Meaning |
|---|---|---|
| D0 | `ZHLT*` | Z80 `HALT*`. **0 = the on-board Z80 has halted.** |
| D1 | `SA10` | switch M10 (1 = open) |
| D2 | `SA11` | switch M11 |
| D3 | `SA12` | switch M12 |
| D4–D7 | — | Not shown in Fig 2-5. The value is not given. |

DDBOOT confirms the layout. For Rev C it masks the halt bit with 01h. It finds the window with
`IN 43h`, `ANI 0Eh`, `RLC`, `ORI 0E0h`, which gives the high byte of the window address. The
`RLC` moves D1–D3 to A10–A12, which confirms Table 2-1 and Fig 2-5.

**Rev B is different** (DDBOOT conditionals, not this manual): halt bit D1 (mask 02h), address
switch mask 0Ch, window base E400h (E000h if the MA10 jumper is fitted).

### 2.5 Vectored interrupt to the host (p. 12, Fig 2-11)

Optional. The interrupt is a 3-input NOR (3J) of `ZHLT*`, `SLVRQ` and `ZINT`, through a 7406.
It drives one of VI0\*–VI7\*, chosen by a jumper in the INTERRUPT BLOCK. The request is
active when the Z80 has halted, the host has not requested the memory, and no host interrupt
is pending. The request ends when the host sets `SLVRQ` (takes the memory) or sets `ZINT`
(interrupts the Z80). The Jade software polls D0 and does not use the interrupt.

### 2.6 Host protocol

The manual (p. 7) says the host must load the control program through the window before the
Z80 can run. It also says the host block-moves all sector data through the window. The command
layout is set by the on-board program. For Jade CP/M 2.2 Release 2 it is in the source.

**Window layout (bank 0, offsets from the window base):**

| Offset | Size | Use |
|---|---|---|
| 0000h | — | Z80 reset entry. DCM2 base page and restart vectors (`RST 7` at 0038h, NMI at 0066h) |
| 0370h | 8 | Command block: command, drive, track, sector, format flags, list character, mode, status |
| 0378h | 2 | Load address (boot) |
| 037Ah | 2 | Load length (boot) |
| 0380h | 128 | Sector buffer |
| 03A0h, 03B1h | — | Disk parameter block and density flags from the ID sector (log-on) |

Bank 1 offset 0300h is the format buffer. DCM2 is assembled at 1000h, which the 2 K mirror
makes the same RAM as 0000h.

**DCM2 commands** (command byte, low three bits): 0 log on, 1 read sector, 2 write sector,
3 format track, 4 read address, 5 list output, 6 list status, 7 background. Mode bit 7 = no
retries.

**DCM2 status byte** (offset 0377h): bit 7 drive not ready, bit 6 write protected, bit 4 record
not found, bit 3 CRC error, bit 2 lost data, bit 1 home error, bit 0 two sides (format).

**A disk command** (`DDBIOS.ASM` `DSK$EX`):

1. `OUT 01h` to get the memory (bank 0).
2. Copy the command bytes to window + 0370h. For a write, copy the sector to window + 0380h.
3. `OUT 02h` to release the memory and interrupt the Z80.
4. Read the status port until D0 = 0 (the Z80 has halted).
5. `OUT 01h` to get the memory, then read the status at window + 0377h.
6. For a read, copy the sector from window + 0380h.

DCM2 waits for a command with `EI` then `HLT`. The interrupt goes to 0038h (IM 1). The handler
does `IN 20h` to clear `ZINT` and dispatches on the command byte.

**Cold boot** (`DDBOOT.asm`):

1. `OUT 01h`. Copy the injection module (C8h = 200 bytes, `DDBOOT.PRN` F056h) to window + 0000h.
2. `OUT 80h` to release the memory and reset the Z80. The Z80 starts the module at 0000h.
3. Wait for `HLT`, then `OUT 01h`. Read the status at window + 0377h (bit 7 = drive not ready).
4. Read the load address and length at window + 0378h. `OUT 03h` and copy bank 1 to that
   address.

The module homes drive 0 with software step pulses (10 ms), reads DCM2 from track 0 (from
sector 13, 1 K) into bank 1, and jumps to DCM2's cold entry at bank 1 + 3. DCM2 copies itself
to bank 0 and loads the BIOS loader, which reads the BIOS into bank 1 and halts. The host
waits for that one halt. The system track layout and the loader chain are in
[Double-D Firmware 1983](Double-D_Firmware_1983.md).

---

## 3. On-board side (what the Z80 sees)

### 3.1 Memory (pp. 7, 13)

2 K of static RAM (four 2114-3L at 4C, 4DL, 4DR, 4E). The upper five address bits are not
decoded, so the 2 K repeats 32 times in the Z80's 64 K. The program can be assembled at any
2 K boundary. The Z80 reads and writes the RAM only while `SLVACK*` is high.

### 3.2 Internal I/O ports (Table 2-3, p. 8)

Decoded on A0–A2 only. The host cannot reach them.

| Addr | Dir | Register |
|---|---|---|
| 00 | IN | Board Level Status |
| 00 | OUT | Board Level Command |
| 04 | IN | FD1791 Status |
| 04 | OUT | FD1791 Command |
| 05 | I/O | FD1791 Track |
| 06 | I/O | FD1791 Sector |
| 07 | I/O | FD1791 Data |

The FD1791 status register also shows the drive lines WRITE PROTECT\*, READY\*, INDEX\*,
TRACK-ZERO\* and SEEK COMPLETE\* (p. 8).

**Events on an IN from port 00 (Table 2-4, p. 9).** A3–A7 have no effect on the other ports.
On an IN from port 00, each of A3–A7 that is high starts an event. A0–A2 are 0 for port 00, so
the port number carries the events. For example, `IN 08h` reads the board status and issues a
step pulse.

| Addr bit | Port | Event (manual) | DCM2 name | Section |
|---|---|---|---|---|
| A3 | 08h | Issue a step pulse | `XP.STP` | 2.15 |
| A4 | 10h | Clear the timer | `XP.MTO` motor turn off | 2.14 |
| A5 | 20h | Reset the host interrupt (`ZINT`) | `XP.IRR` | 2.5 |
| A6 | 40h | Start the timer | `XP.MTX` motor time extend | 2.14 |
| A7 | 80h | Wait-state request | `XP.DSH` data sync hold | 2.16 |

### 3.3 Board Level Status — IN 00 (Table 2-5, p. 9)

| Bit | Signal | Meaning | DCM2 name |
|---|---|---|---|
| 0 | `U0` | User switch 0. The Jade software reads it as the **FDC data-bus polarity** | `BS.US0` |
| 1 | `U1` | User switch 1 | `BS.US1` |
| 2 | `TST*` | Test switch (0 = test mode) | `BS.TST` |
| 3 | `ZINT` | Interrupt request from the host | `BS.INT` |
| 4 | `SERI` | EIA input, at TTL level | `BS.EIA` |
| 5 | `TOFF` | Timer off | `BS.MOF` motor off |
| 6 | `ILP*` | Illegal pack. **TWO SIDED\* since ECN 5B** (J3 pin 48) | `BS.TSD` two-sided drive |
| 7 | `CHNG*` | Disk has been changed | `BS.DCN` |

U0, U1 and TST\* are positions of the 8-position switch S1 (Fig 2-7). Closing TST\* selects
drive 0, turns on the motor and loads the head, for PLL adjustment (p. 26).

**U0 and the FDC (DCM2, DDBOOT).** U0 closed (0) = FD1791 or FD1795, which have an inverted
data bus. The software then XORs every FDC command, register and data byte with FFh. U0 open
(1) = FD1793 or FD1797, with a true bus. The hardware manual names only the FD1791-01.

### 3.4 Board Level Command — OUT 00 (Table 2-6, p. 10)

A 74LS273 latch (4M), cleared by `BR*`. DCM2 uses the same bit names (`BC.*`).

| Bit | Signal | Meaning |
|---|---|---|
| 0 | `DSA` | Drive select bit 2⁰ |
| 1 | `DSB` | Drive select bit 2¹ |
| 2 | `DSE` | Drive select enable. Also gates the FD1791 interrupt to the Z80 NMI |
| 3 | `SERO` | EIA output (0 = negative EIA level) |
| 4 | `DDEN` | 1 = double density (MFM) |
| 5 | `SID1` | Side select during read/write. **Step direction** during a step (0 = toward track 0 on most drives) |
| 6 | `PCA` | Precompensation select A |
| 7 | `PCB` | Precompensation select B |

**Drive selection (Table 2-7):** DSE = 0 selects no drive. DSE = 1 selects drive
`DSB·2 + DSA` (0–3). A 7445 (3M) decodes it.

**Write precompensation (Table 2-8, p. 11):**

| PCB | PCA | Precomp | DCM2 name |
|---|---|---|---|
| 0 | 0 | off | — |
| 0 | 1 | 200 ns | `BC.PCH` heavy |
| 1 | 0 | 160 ns | `BC.PCM` medium |
| 1 | 1 | 120 ns | `BC.PCL` light |

ECN 10 confirms two bit positions. Its DCM2 mask `ANI NOT (BC$DSE OR BC$SD1)` is DBh, which is
NOT 24h, so DSE = 04h and SID1 = 20h. The old code was FBh (NOT 04h, DSE only).

### 3.5 Interrupts on the Z80 (p. 7, Fig 2-10)

| Z80 pin | Source | DCM2 |
|---|---|---|
| `INT*` | `ZINT`, set by the host (control port D1) | IM 1, handler at 0038h |
| `NMI*` | FD1791 INTRQ (`DINT`), on command completion. Clamped by CR18 while **DSE = 0** | handler at 0066h, returns through IY |

### 3.6 Wait state for data transfer (p. 12, Fig 2-10)

An IN from port 00 with **A7 high** puts the Z80 in WAIT (`ZHLD*`). The wait holds until the
FD1791 raises **DRQ** (`DDRQ`) or **INTRQ** (`DINT`). DCM2's sector loops do `IN 80h` before
each data byte.

### 3.7 Timer (p. 11, Fig 2-8)

One-shot 0M (74LS123, R12 = 240 K, C16 = 47 µF). An IN from port 40h starts it. An IN from
port 10h clears it. While it runs it drives J2 pin 16 `MOTOR ON*` for 5″ drives. The Z80 reads
its state as `TOFF` (status bit 5). The program uses it to deselect an idle drive. The manual
gives no period.

### 3.8 Step control (p. 11, Fig 2-9)

The program, not the FD1791, steps the head. An IN from port 08h fires the other half of
one-shot 0M for a **3 µs** STEP\* pulse to J3 pin 36. `SID1` drives DIRC on J3 pin 34. The
program times the gap between pulses, to better than 0.1 ms. C9 and R10 set the pulse width.
DCM2 times steps in units of 0.1 ms (0.2 ms on a 5″ board, which runs the Z80 at half speed).

---

## 4. Clocks, data separator and precompensation

- **Clock (p. 6, Fig 2-6).** 8.000 MHz crystal into a 74LS93 (2M). The CLOCK SELECT jumper
  is `CLK`–`8` or `CLK`–`5`. 8″: Z80 4 MHz, FD1791 2 MHz. 5″: Z80 2 MHz, FD1791 1 MHz. The 5″
  Z80 runs slower to keep the FD1791 RE\*/WE\* timing.
- **PLL data separator (pp. 14–17).** A 74LS123 VCO (1B) and a digital phase detector (1C, 3A,
  74LS113). For 8″ MFM, data pulses are 2, 3 or 4 µs apart and the read clock changes every
  1 µs. All timings double for 5″. In FM, every other 1 µs frame is an "illegal frame"
  (`PDIF`). Trimmer R5 sets the VCO to 2.0 MHz (1.0 MHz for 5″) at 1B pin 12. Trimmer R2 sets
  the 2A one-shot to 1.0 µs (2.0 µs for 5″) (p. 26).
- **Precompensation (p. 14, Fig 2-13).** The FD1791 EARLY and LATE outputs change the period
  of one-shot 1A. PCA and PCB set the size (Table 2-8). IC 3B turns precomp off when both are 0.

---

## 5. Engineering Change Notices (front matter)

| ECN | Date | Content |
|---|---|---|
| 1 | 4 Aug 1980 | The board grounds bus pins 20, 53 and 70 (IEEE 696.1/D2). This stops IMSAI front-panel machines. The links can be cut. |
| 2 | 4 Aug 1980 | 8″ PLL values changed: R1 6.8 K (now to +5 V, not VX), R3 12 K, R4 10 K, R38 20 K, R42 470 K, R43 2.7 K, R49 a jumper. Re-tune the PLL. |
| 3 | 4 Aug 1980 | SBC-100 and SBC-200 do not drive SWO\*. Cut pin 97 and jumper 1H pin 2 to 3J pin 1. |
| 4 | 4 Aug 1980 | A fix on the Big-Z CPU board, which puts write data on the bus late. |
| 5B | 5 Aug 1980 | J3 pin 48, ILLEGAL PACK\*, becomes **TWO SIDED\*** for Jade CP/M 2.2 Release 2. Cut the link at J3 pin 48. For SA850/851, jumper lower pin 48 to upper pin 10. Drives with a data separator put SEPARATED DATA\* on pin 48, and DCM2 reads it as TWO SIDED\*. |
| 6 | 5 Aug 1980 | Add the PHAN\* jumper when the window overlaps memory (§2.2). |
| 7 | 6 Oct 1980 | Use National Semiconductor 74LS123 one-shots. The R/C values are chosen for them. |
| 8 | 6 Oct 1980 | Corrections to the 3/7/80 schematic: 1M pin 6 is `DINT*`. Part of 3L is a 74LS240, not a 74LS244. A missing 3L inverter makes `DDEN*` from `DDEN`. Four 3H (8131) pin numbers. 4A pin 13 is `BPWR`. |
| 9 | 6 Oct 1980 | Jumper settings for Shugart SA800/801 and SA850/851, and Siemens FD120-8B and FD100-8D. |
| 10 | 8 May 1981 | Qume DataTrak 8 jumpers, and a DCM2 patch: address 111Dh, FBh → DBh (§3.4). In Jade CP/M 2.2 from S/N 2-187-1410. |

---

## 6. Parts and identification

| IC | Part | Job |
|---|---|---|
| 1J | FD1791-01 | Formatter/controller |
| 2D | Z80A | On-board processor |
| 4C, 4DL, 4DR, 4E | 2114-3L | 2 K RAM |
| 3F, 3H | 8131 | Port and window address comparators |
| 4B | 74LS30 | Extended (24-bit) address decode |
| 4H | 74LS244 | Host status port |
| 2L | 74LS74 | `ZINT` and `SLVRQ` latches |
| 4L | 74LS244 | Board status port |
| 4M | 74LS273 | Board command latch |
| 3M | 7445 | Drive select decoder |
| 0L, 0M, 1A, 1B, 2A | 74LS123 | One-shots: write strobe, timer, step, precomp, VCO, pulse detector |
| U1, U2, U6 | 78M12, 7805, 79L12 | +12 V, +5 V, −12 V regulators |

Power (p. 2): +7 to +11 V at 0.90 A typical (1.50 A max). +14.5 to +21.5 V at 25 mA typical
(50 mA max). −14.5 to −21.5 V at 8 mA typical (30 mA max). The board is a four-layer PCB with a
ground plane. Board marking: IOD-1200-C.

---

## Quirks worth carrying

- ⚠ **Table 2-1, row 7, prints `FE00 – FBFF`.** The switch row (1 1 0) and the 1 K step make
  it **F800–FBFF**, and so does DDBOOT's `ANI 0Eh / RLC / ORI 0E0h`. The spec page (p. 2) has
  the same kind of slip: `EP00` and `FA00` for E800 and F800.
- ⚠ **`ZINT` has two jobs.** While the Z80 runs, it is the Z80's interrupt. While the host has
  the memory, it is IA10, the 1 K bank select of the window. DDBOOT and DDBIOS always write
  01h (bank 0) before they release the memory, so that no interrupt is left pending.
- ⚠ **Each release is also a command.** `OUT 02h` releases the memory and interrupts the Z80.
  `OUT 80h` releases the memory and resets the Z80. A model must apply D0, D1 and D7 from one
  write.
- ⚠ **Reset gives the memory to the host** (p. 5). After a bus reset the Z80 cannot run until
  the host clears `SLVRQ`.
- ⚠ **The host never reaches the FD1791.** All disk commands go through a program that the
  host loads into the board. An emulation must run a second Z80 with the loaded program
  (DCM2 comes off the disk), or model the DCM2 command block in §2.6.
- ⚠ **The Z80 reads events from the port number.** `IN` from 08h, 10h, 20h, 40h or 80h (or a
  sum of them) is a board status read **and** a strobe.
- ⚠ **U0 sets the FDC bus polarity, in software.** The manual calls U0 a user switch. DCM2 and
  DDBOOT read it to decide whether to invert every FDC byte (closed = FD1791/1795, inverted).
  A model with an FD1791 must invert, and must report U0 closed.
- ⚠ **Status bit 6 changed its meaning** (ECN 5B). The manual table says `ILP*` (illegal
  pack). Jade CP/M 2.2 Release 2 reads it as TWO SIDED\* (`BS.TSD`).
- ⚠ **Rev B boards differ** (DDBOOT): halt bit D1, not D0, and a different window base.
- ⚠ **Three readings of the PLL resistors.** The Appendix B parts list (R1 10 K, R3 18 K
  printed `1SK`, R4 15 K, R38 27 K, R42 390 K, R43 2.4 K, R49 82 K) is the set before the ECN.
  The §3.2 assembly steps print the same values but tag them `ECN#2`. ECN 2 (8/80) has the
  latest values (§5). ECN 2 wins because it is the latest dated change.
- ⚠ **NMI needs DSE.** With DSE = 0 the FD1791 completion interrupt does not reach the Z80.
- FD1791 chip detail is not repeated here. The FD1771 sheet
  ([Western Digital FD1771 - Datasheet](Western%20Digital%20FD1771%20-%20Datasheet.md)) has
  the shared register model, but MFM, some commands and some status bits differ. There is no
  FD179x data sheet in `reference/` yet.
