# Cromemco 4FDC / 16FDC / 64FDC Floppy Disk Controllers

Source: [Cromemco 4FDC Instruction Manual.pdf](#) (© Cromemco 1977),
[Cromemco 16FDC Instruction Manual.pdf](#) (© Cromemco 1979–1981),
[Cromemco 64FDC Instruction Manual.pdf](#) (© Cromemco 1983)

Three generations of one Cromemco S-100 floppy-disk controller family, each a single S-100
board combining a Western Digital FD177x controller chip, an onboard **TMS 5501** UART +
timer + interrupt-controller chip (the same device documented in
[`Cromemco TU-ART.md`](Cromemco%20TU-ART.md), here wired as a single channel rather than the
TU-ART's twin), and an onboard boot **RDOS** PROM. The **4FDC** (1977) is FD1771-only —
single density (FM). The **16FDC** (1979–81) and **64FDC** (1983) both carry an **FD1793**
and add single **and** double density (FM/MFM), plus a phase-locked-loop data separator; the
16FDC additionally offers a real-time-clock interrupt option the 64FDC's manual does not
document. All three share the same port-block shape (00–09 serial/timers, 30–34 disk,
40 bank-select) and the same RDOS monitor family, making them one emulation family with small,
well-defined per-board deltas.

This is a distilled emulation reference: kit-assembly, schematics, parts lists, and voltage/
timing specs for the analog data separator are omitted except where they set a
software-visible register value (baud/clock tables). The WD1771/1793 chip internals
(command encoding, status-bit semantics, CRC, track format) are **not re-derived here** — see
[`Western Digital FD1771 - Datasheet.md`](Western%20Digital%20FD1771%20-%20Datasheet.md); all
three boards' disk-command/status registers follow that chip's model (FD1793 additionally
supports MFM, per its own datasheet family). What this file adds is the **board**-level
wrapper: the port map, the auxiliary drive-control registers, the wait/DRQ transfer
mechanism, the boot PROM and power-on-jump logic, and the RTC/serial extras — and, critically,
what differs across the three boards.

---

## 1. What differs between 4FDC / 16FDC / 64FDC

| Feature | 4FDC (1977) | 16FDC (1979–81) | 64FDC (1983) |
|---|---|---|---|
| FDC chip | WD **FD1771** (-1) | WD **FD1793**-1 (dash "-B02" variant referenced in theory-of-op) | WD **FD1793B-02** |
| Density | Single (FM) only | Single **and** double (FM/MFM) | Single **and** double (FM/MFM) |
| Serial chip | TMS 5501, one channel | TMS 5501, one channel | TMS 5501, one channel |
| Board crystal | not captured here (2 MHz-class, per FD1771 norms) | **8.000 MHz** onboard crystal | **8.000 MHz** onboard crystal |
| Data separator | none documented (single-density only) | Phase-Locked Loop (PLL), free-running RCLK table keyed on MAXI/DDEN | Phase-Locked Loop (PLL), identical free-running RCLK table |
| Write precompensation | not documented | not documented (not needed at FM-only / 16FDC era per manual) | **Yes** — PAL (74946) delays WD on 8″ double density inside track 43 |
| Boot/monitor ROM | **1K** (2708-class) at `C000`–`C3FFH` | **4K** at `C000`–`CFFFH` (RDOS 2.52) | **8K** at `C000`–`DFFFH` (RDOS 3.12) — 32K memory-card address footprint (decode `8000`–`FFFFH`), the ROM answering `C000`–`DFFF`. *(The manuals describe a 4K `C000`–`CFFF` window; the shipping RDOS 3.12 image and its `rdos0312.lst` listing `ORG 0C000H` run through `DFFF`, so the part grew into the second 4K — see `docs/roms.md`.)* |
| ROM disable | no port-40 bank select (`40` not assigned) | `OUT 40H` (any byte) disables ROM if jumpered for RES; RESET re-enables | `OUT 40H` (any byte) disables ROM if jumper location 2 set; RESET re-enables |
| Boot/monitor select | SW3 (`BOOT`/`MON`), readable at port 34 D6 | Switches 1–4 (RDOS defeat / disable-after-boot / boot-or-mon / inhibit-init), readable via port 04 D3–D0 (switches 5–8 only) | **Jumpers**, not switches, for RDOS-defeat/disable-after-boot/boot-mon/inhibit-init (jumper locations 1–4); switches 1–5 instead set baud rate + boot **drive** + self-test |
| Real-Time-Clock interrupt | not documented | **Yes** — 512 ms jumper option onto XI7 (mutually exclusive with the DRQ jumper) | not documented in this manual |
| Mode-2 (Z80) interrupt vectors | not documented | **Yes** — jumper forces even vectors for Z80 IM2 | not documented in this manual (assume 8080-mode-0 restart-opcode gating only) |
| Aux. disk register (port 04 OUT) | EJECT L/R, FAST SEEK, RESTORE, CONTROL OUT (PerSci 277 options) | EJECT, DRIVE SELECT OVERRIDE, FAST SEEK, RESTORE, CONTROL OUT, SIDE SELECT (PerSci 277/299B options) | DRIVE SELECT OVERRIDE, CONTROL OUT, SIDE SELECT only (no eject/fast-seek/restore — simpler, PerSci 299B) |
| Port 34 IN extra flags | DRQ, BOOT, HEADLOAD, EOJ only (no Motor/Autowait timeout bits) | (see §4; same shape as 64FDC) | DRQ, BOOT\*, SELECT REQUEST, INHIBIT INIT\*, MOTOR ON, MOTOR TIMEOUT, AUTOWAIT TIMEOUT, EOJ |
| Max drives | 4 (A–D) | 4 (A–D) | 4 (A–D) |
| Geometry (documented) | Large floppy: tracks `0`–`4C`h, sectors `1`–`1A`h. Mini floppy: tracks `0`–`27`h, sectors `1`–`12`h | not tabulated in the register chapters read; follows same FD1793 ID-field-driven sizing | same FD1793 ID-field sizing; 128×2ⁿ (IBM) or 16×N (non-IBM) per datasheet |
| Wait-state on 4 MHz ROM/disk access | not documented (era predates the 4 MHz wait-state note) | at 4 MHz, disk-port reads insert a stabilization wait (per 16FDC theory notes on the wait-state generator) | **Only** Auto-Wait-mode port-34 accesses wait; 30–33H never wait (explicit in Interface Characteristics chapter) |
| CPU requirement | none stated beyond FD1771 norms | none stated | **Minimum 4 MHz, zero memory wait states** (needed for programmed disk/serial I/O, no DMA) |

---

## 2. I/O port map (all three boards)

All three decode the same block shape; register **content** differs as noted per-board below.

| Port (hex) | IN | OUT |
|:---:|---|---|
| 00 | UART status | UART baud rate |
| 01 | UART receiver data | UART transmitter data |
| 02 | not assigned | UART command register |
| 03 | Interrupt address (encoded RST vector) | Interrupt mask |
| 04 | Parallel input / Auxiliary disk status | Parallel output / Auxiliary disk command |
| 05–09 | not connected | Timers 1–5 |
| 30 | Disk status (command-dependent) | Disk command |
| 31 | Track register | Track register |
| 32 | Sector register | Sector register |
| 33 | Data register | Data register |
| 34 | Disk flags | Disk control |
| 40 | not assigned | Bank select (ROM disable) — **16FDC/64FDC only**; not assigned on 4FDC |

(4FDC Register Description ch.3 p.11; 16FDC Register Description ch.3; 64FDC Register
Descriptions ch.3.) These addresses are **not jumper-relocatable** the way the TU-ART's or
VersaFloppy's are — all three FDC boards are hard-decoded at these fixed hex addresses.

---

## 3. Serial channel (TMS 5501, one per board)

Identical register shape and status polarity to the TU-ART's TMS 5501 channel — see
[`Cromemco TU-ART.md`](Cromemco%20TU-ART.md) §§3–6 for the status-bit meanings (**all
active-high**; TBE=D7, RDA=D6), baud-register one-hot encoding, and command-register bits
(RES/BRK/RS7/INE/HBD/TB5). The FDC boards differ from the TU-ART only in:

- **One channel, fixed base `00`.** No DIP-switch base address — port 00–09 is hard-wired,
  not jumper-selected the way the TU-ART's per-device base is.
- **RS7 / interrupt-7 routing is disk-aware.** Bit D2 of the command register (RS7) routes
  either Timer 5 *or* the disk's DRQ (4FDC/16FDC, via jumper "INTER 7"/"DRQ jumper") *or*,
  16FDC only, a 512 ms real-time-clock square wave (jumper "RTC") onto the lowest-priority
  interrupt request. **DRQ and RTC are mutually exclusive jumpers** on the 16FDC — only one
  may be inserted (16FDC Register Description p.27, "DRQ AND RTC JUMPER LOCATIONS").
- **Interrupt-address register (port 03 IN)** enumerates the same 8 sources as the TU-ART but
  substitutes "End of Job (from disk)" (`D7`) for one of the TU-ART's channel-specific sources,
  and the lowest-priority vector (`FF`) reads "Timer 5 **or** DRQ from disk **or** real time
  clock" on the 16FDC (4FDC's equivalent line reads "Timer 5 or (DRQ From Disk)" — no RTC).

Baud register (port 00 OUT), command register (port 02 OUT), and status register (port 00 IN)
bit layouts are byte-identical across all three FDC boards and to the TU-ART (D7 STOP /
9600-4800-2400-1200-300-150-110 one-hot; HBD ×8 octuple). (4FDC pp.13–14; 16FDC pp.24,26,30;
64FDC register pp.34–36 — 64FDC's chapter shows only the timers/aux-disk portion explicitly
re-derived here since its 00–03 serial registers are stated to match the family.)

---

## 4. Disk flags / disk control — port 34

**Port 34 IN (Disk Flags)** differs meaningfully between the 4FDC and the 16FDC/64FDC pair:

| Bit | 4FDC | 16FDC / 64FDC |
|:---:|---|---|
| D7 | DRQ | DRQ |
| D6 | ¬BOOT (low = SW3 set to BOOT) | ¬BOOT\* (low = jumper/switch set to BOOT) |
| D5 | HEADLOAD | SELECT REQUEST (high = 1793 requesting drive select) |
| D4 | not assigned | ¬INHIBIT INIT\* (low = jumpered) |
| D3 | not assigned | MOTOR ON |
| D2 | not assigned | MOTOR TIMEOUT (motors auto-off ~8 s after last op) |
| D1 | not assigned | AUTOWAIT TIMEOUT (~4 s abnormal-termination guard) |
| D0 | EOJ (End of Job) | EOJ |

(4FDC p.29 "34 IN Disk Flags"; 64FDC p.47 "\_4 IN DISK FLAGS" — the printed port label is
truncated by the scan but the register content and the surrounding 31–34/40 sequence confirm
it is port 34.)

**Port 34 OUT (Disk Control)** — 4FDC has **no density bit** (single-density-only chip); the
16FDC/64FDC add **Double Density** at the same position the 4FDC leaves unused:

| Bit | 4FDC | 16FDC / 64FDC |
|:---:|---|---|
| D7 | AUTO WAIT | AUTO WAIT |
| D6 | not assigned (no density control — FD1771 is FM-only) | **DOUBLE DENSITY** (high = MFM) |
| D5 | MOTOR ON | MOTOR ON |
| D4 | MAXI (8″/5″ select) | MAXI |
| D3–D0 | DS4–DS1 (drive select, one-hot) | DS4–DS1 |

**AUTO WAIT** is identical in all three: a `1` written to D7 arms Auto Wait; a subsequent
**read** of port 34 then holds the CPU in an extended wait state until one of: (1) the FDC
issues DRQ (normal use), (2) the FDC issues EOJ (normal termination), (3) a hardware RESET, or
(4) the Auto-Wait timeout (~4 s, abnormal). This lets a driver do a tight `IN 34H` / `IN 33H`
loop with no software DRQ polling at all — the CPU simply stalls until data is ready. (4FDC
p.29; 16FDC theory-of-operation "AUTO WAIT" p.65–66; 64FDC Register Descriptions p.48 and
Theory of Operation p.62.)

---

## 5. Auxiliary disk register — port 04 (drive-mechanics options)

Port 04 carries PerSci-drive-specific mechanical controls, and is the register with the
**widest per-board variation** — it shrinks from 4FDC → 16FDC → 64FDC as the mechanical
feature set each manual targets narrows:

| Bit | 4FDC (PerSci 277) | 16FDC (PerSci 277/299B) | 64FDC (PerSci 299B) |
|:---:|---|---|---|
| D7 | not assigned | (DRQ/RTC — see §3, this is IN not OUT) | (DRQ — same shape, IN) |
| D6 | ¬EJECT (activates PerSci 277 eject line) | ¬EJECT | not assigned |
| D5 | ¬DRIVE SELECT OVERRIDE (4FDC: this bit is actually EJECT RIGHT — see note) | ¬DRIVE SELECT OVERRIDE | ¬DRIVE SELECT OVERRIDE |
| D4 | ¬FAST SEEK (puts FD1771 into fast step mode for voice-coil drives) | ¬FAST SEEK | not assigned |
| D3 | ¬RESTORE (forces selected drive to Track 0) | ¬RESTORE | not assigned |
| D2 | ¬CONTROL OUT (pulls daisy-chain pin low, test only) | ¬CONTROL OUT | ¬CONTROL OUT |
| D1 | not assigned | ¬SIDE SELECT (0 = side 1, 1 = side 0) | ¬SIDE SELECT |
| D0 | not assigned | not assigned | not assigned |

**4FDC note:** the manual's actual bit table (p.17) reads D6=¬EJECT LEFT, D5=¬EJECT RIGHT,
D4=¬FAST SEEK, D3=¬RESTORE, D2=¬CONTROL OUT — i.e. the 4FDC has **two** eject lines (LEFT and
RIGHT, for a dual-PerSci-277 mechanism) and **no side-select or drive-select-override bit at
all** — those two concepts (dual-headed drives, multiplexed drive status) postdate the 4FDC.
The 16FDC then drops one eject line to add Drive-Select-Override and Side-Select; the 64FDC
drops eject/fast-seek/restore entirely (assumed handled by the 1793's own stepping and a
simpler drive) and keeps only Drive-Select-Override, Control-Out, and Side-Select. **All
listed bits are active-low** except where noted, and all "normally high" (i.e., idle/no-op)
per each manual.

**Port 04 IN (Parallel Input / Auxiliary Disk Status)** likewise varies:

| Bit | 4FDC | 16FDC | 64FDC |
|:---:|---|---|---|
| D7 | DRQ (jumper option, mirrors port 30/34 DRQ) | DRQ **or RTC** (jumper option) | *(64FDC's aux disk command is OUT-only per the manual excerpt read; no equivalent IN table captured here beyond D5/D1 above — see 64FDC p.34)* |
| D6 | SEEK IN PROGRESS (voice-coil motion) | SEEK IN PROGRESS | — |
| D5–D0 | unassigned (free for system use) | D5–D4 unassigned, **D3–D0 = sense switches 5–8** (0 = ON) | — |

---

## 6. FD177x disk registers (30–33) and command/status semantics

Ports 30 (command/status), 31 (track), 32 (sector), 33 (data) are the WD chip's own four
registers in standard order — see the FD1771 datasheet reference for the full command set
(Restore/Seek/Step/Step-In/Step-Out, Read/Write Record(s), Read Address/Track, Write Track,
Force Interrupt), per-command-type status-bit meanings, and CRC/track-format rules. All three
Cromemco boards use that model unchanged; the **command byte encodings and stepping-rate
tables differ only because the chip differs**:

- **4FDC (FD1771, single density only):** stepping rate field r1r0 → 8″: 6/6/10/20 ms, 5″:
  12/12/20/40 ms. Sector-length field is the same 128×2ⁿ (IBM) / 16×N (non-IBM) rule as the
  datasheet. Data-address-mark field a1a0 → FB/FA/F9/F8 (Data/user/user/Deleted), all written
  with clock mark C7. Write Track control bytes match the datasheet's F7–FE magic values
  exactly (no F5/F6/A1/C2 double-density codes — those don't exist on an FD1771).
- **16FDC/64FDC (FD1793, both densities):** same Type I–IV command shape, but the Write Track
  control-byte table gains **density-dependent interpretation** — in double density (DDEN=0)
  the codes F5 and F6 write the MFM sync marks A1\* and C2\*\* (missing clock transitions
  between specific bit pairs) instead of being disallowed, and CRC preset differs (F7 vs F5
  triggers depending on density). Stepping rate table is command-type-dependent: 3/6/10/15 ms
  (8″) or 6/12/20/30 ms (5″) selected by r1r0 = 0..3 (64FDC Register Descriptions p.39; 16FDC
  equivalent not independently re-derived here but stated to follow the same 1793 model).
- **Error-status byte convention** (as decoded by the 4FDC/64FDC monitor commands, e.g.
  `R-ERR nn`/`W-ERR nn`/`S-ERR nn`/`H-ERR nn`): bit7 Not Ready, bit6 Write Protect/Record
  Type, bit5 Head Engaged/Record Type/Write Fault, bit4 Seek Error/Record Not Found, bit3 CRC
  Error, bit2 Track 0/Lost Data, bit1 Index/DRQ, bit0 Busy — i.e. the monitor's printed error
  byte **is** the raw FD177x status register for the command just executed (4FDC pp.7–10;
  64FDC Register Descriptions pp.37–39, table "Last Command" × bit).

**Data-transfer synchronization differs from the VersaFloppy family.** Unlike the SD Systems
VersaFloppy (which stalls the CPU via a dedicated PRDY wait-state generator gated by a
control-register bit, with **no software DRQ polling at all**, see
[`SD Systems VersaFloppy.md`](SD%20Systems%20VersaFloppy.md) §4), the Cromemco family exposes
DRQ as a **readable bit** (port 30 or 34, D7/D1 depending on board and command) and additionally
offers the **Auto Wait** mechanism (§4) as an optional CPU-stall path on port 34 specifically —
not on the raw data port 33. A driver may choose to poll DRQ in software (the monitor's Read/
Write Disk commands do exactly this per the R-ERR/W-ERR bit tables) or use Auto Wait to let the
hardware stall the CPU instead. Port 33 (the data register itself) never wait-states on any of
the three boards; only port 34, when Auto Wait is armed, does.

---

## 7. Boot PROM / power-on-jump / bank select

| | 4FDC | 16FDC | 64FDC |
|---|---|---|---|
| ROM size/address | 1K, `C000`–`C3FFH` (2708-class) | 4K, `C000`–`CFFFH` (RDOS 2.52) | 8K, `C000`–`DFFFH` (RDOS 3.12) — memory-card address footprint is `8000`–`FFFFH`; the manuals describe a 4K `C000`–`CFFF` window, but the RDOS 3.12 image + `rdos0312.lst` (`ORG 0C000H`) run to `DFFF`, so the emulation decodes 8K (see `docs/roms.md`) |
| Boot select | SW3 = BOOT/MON, readable at port 34 D6 | switch/jumper set, readable at port 34 D6 (¬BOOT) | jumper location 3 = BOOT/MON, readable at port 34 D6 (¬BOOT) |
| ROM disable | not supported (no bank-select port) | `OUT 40H` (any byte) permanently deselects the ROM if jumpered for RES; cleared only by hardware RESET | identical mechanism, gated by jumper location 2 |
| Auto-boot on power-up | via SW3/monitor switch | via switch/jumper set (RDOS defeat / disable-after-boot / boot-or-mon / inhibit-init) | via jumper locations 1–4 (same four functions, **jumpers not switches** — 64FDC's front-panel switches are repurposed for baud + boot-drive + self-test instead, see §1) |
| Wait states on ROM access | not documented | none stated | **one wait state at 4 MHz, none at 2 MHz**; Hold Acknowledge (DMA) temporarily disables the ROM entirely |

**RDOS** ("Resident Disk Operating System" — the 4FDC/16FDC/64FDC's onboard monitor) is not a
separate BIOS-on-a-memory-board the way the VersaFloppy's DDBIOS is — it lives *in* the FDC's
own onboard ROM window and boots directly from power-on or reset if the board is jumpered/
switched to `BOOT`. RDOS commands referenced by these manuals include Read/Write/Seek Disk,
Select Disk Drive (`d;xyz` on the 16FDC, encoding seek speed via semicolon count plus optional
side/density/Cromix flags), Display/Move/Query Memory, Examine/Output port, and Initialize
Baud Rate (16FDC RDOS-II ch.2, pp.11–16).

---

## 8. Real-Time Clock and Mode-2 interrupts (16FDC only, as documented)

**Real Time Clock.** The 16FDC offers an optional **512 ms** interrupt source: a jumper
("RTC") connects a 512 ms square wave onto interrupt line XI7 (the same physical line the
disk's DRQ can otherwise be routed to via the "DRQ" jumper — **the two are mutually
exclusive**). When the RTC jumper is inserted and the UART command register's RS7 bit (D2) is
set, an interrupt fires every 512 ms regardless of disk activity; the UART's own Timer 5
interrupt must be disabled and the mask register set correctly for this to reach the CPU
(16FDC Theory of Operation p.70 "REAL TIME CLOCK"; jumper location shown Register Description
p.27). **This is not documented for the 4FDC or the 64FDC** — treat RTC as 16FDC-specific
unless later evidence surfaces (the 64FDC manual's theory-of-operation chapter, read through
its Phase-Locked-Loop section, has no equivalent "REAL TIME CLOCK" heading).

**Mode 2 (Z80) interrupt acknowledge.** Normally the 16FDC answers an S-100 interrupt
acknowledge cycle by gating a Restart opcode (`C7,CF,D7,DF,E7,EF,F7,FF`) onto the bus — 8080
mode-0 behavior, directly executable as `RST n`. A jumper can force bit 0 of every vector to
zero (`C6,CE,D6,...,F6,FE`), converting the board to **Z80 Mode 2**: the Z80 appends this byte
to its I register to form a 16-bit pointer into a vector table, rather than executing the byte
as an instruction. Once converted, the board is "no longer compatible with the mode 0 (8080
compatible) interrupt structure." The interrupt-address *register* (port 03 IN) is unaffected
by this jumper and still reads the same restart-opcode values regardless of the acknowledge-
cycle jumper setting (16FDC Theory of Operation p.70 "MODE 2 INTERRUPT ACKNOWLEDGE"). Not
documented for the 4FDC (predates general Z80-mode-2 support) or the 64FDC manual sections
read here.

---

## 9. Disk geometry

Only the 4FDC manual states an explicit geometry table directly (its Read/Write/Seek Disk
monitor commands report track/sector ranges):

| | Large (8″) floppy | Mini (5¼″) floppy |
|---|---|---|
| Tracks | `0`–`4C`h (76 decimal, i.e. 77 tracks) | `0`–`27`h (39 decimal, i.e. 40 tracks) |
| Sectors | `1`–`1A`h (26 decimal) | `1`–`12`h (18 decimal) |
| Sector size | 128 bytes (IBM 3740, `b`=1) | 128 bytes |

(4FDC pp.7,10.) The 16FDC and 64FDC manuals' register chapters describe sector size as
derived from the FD1793's own ID-field sector-length byte at read/write time — 128×2ⁿ bytes
(n=0..3) for IBM-compatible (`b`=1) formatting, or 16×N bytes for non-IBM (`b`=0) — exactly
per the WD177x datasheet's own Type-II command rule; **no fixed sectors/track or tracks/side
table for the 16FDC/64FDC's own drives was found in the register-description or interface
chapters read for this reference.** The RDOS `L` ("List all disks logged in") command reports
Large/Small, Single/Double sided, Single/Dual density, and Fast/Medium/Slow seek per logged-in
drive rather than a fixed geometry constant, implying geometry is drive-format-detected at
mount time rather than board-fixed (16FDC RDOS-II p.13 "LIST ALL DISKS LOGGED IN").

---

## 10. Emulation checklist

- **One TMS 5501 channel, fixed base 00H**, all three boards — reuse the TU-ART's status/
  baud/command register model (§3) but with **no DIP-selectable base**.
- **Disk register block is always 30–34H + bank-select 40H**, never jumper-relocatable on any
  of the three boards.
- **Pick the right FDC core by board:** 4FDC = FD1771 (single density only, no density bit in
  its control register); 16FDC/64FDC = FD1793 (single **and** double density, control-register
  D6 = Double Density on those two boards only).
- **Auto Wait (port 34 D7 OUT) is the CPU-stall path**, distinct from port-33 data-register
  DRQ polling; a byte-clean emulator should honor it as an alternate "block until DRQ/EOJ"
  mechanism, not require it.
- **Port 04's bit meaning is board-specific** — do not share one bit table across all three;
  4FDC has dual eject lines and no side-select/override; 16FDC has eject + override + side-
  select; 64FDC drops eject/fast-seek/restore and keeps only override/control-out/side-select.
- **Boot PROM size and disable path differ:** 4FDC 1K no-disable; 16FDC 4K (RDOS 2.52) and
  64FDC 8K (RDOS 3.12, `C000`–`DFFF`), both with an `OUT 40H` bank-select disable (jumper-gated,
  RESET-restored).
- **RTC and Mode-2-interrupt jumpers are 16FDC-specific** in the manuals read; do not assume
  the 64FDC has them without further evidence — its theory-of-operation chapter has no such
  section, and its front-panel switches are reassigned to baud-rate/boot-drive/self-test
  instead of the 16FDC's RDOS-defeat/boot-select functions (those move to jumpers on the
  64FDC).
- **64FDC needs a minimum 4 MHz, zero-wait-state CPU model** for its programmed (non-DMA)
  disk and serial I/O to behave as documented; it explicitly notes it inserts wait states
  **only** for Auto-Wait-armed port-34 accesses, unlike some wording in the 16FDC's
  theory-of-operation notes about 4 MHz disk-port waits generally — when in doubt, prefer the
  64FDC's explicit Interface Characteristics statement for that board, and treat the 16FDC's
  wait-state generator (theory-of-op p.65, "3. Any reference to the TMS5501") as covering the
  UART ports, not blanket disk-port waits.
- **No erratum found analogous to the TU-ART's baud-port EQU mistake** in the sections read;
  each board's own register-description chapter and its theory-of-operation chapter agree on
  every address cited above.
