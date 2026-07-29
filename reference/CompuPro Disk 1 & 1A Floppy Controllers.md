# CompuPro Disk 1 / Disk 1A Floppy Disk Controllers

Source: [CompuPro Disk 1 Manual.pdf](#) (© CompuPro/Godbout 1981, board 171F,
Fourth Printing March 1983),
[CompuPro Disk 1A Manual.pdf](#) (© CompuPro 1984, board 203 Rev D, EPROM 1.4,
Doc #12036, Second Edition November 1984)

Two generations of one CompuPro (Godbout) S-100 floppy-disk controller, each a single board
built around an **NEC µPD765A / Intel 8272** third-generation FDC, a **24-bit arbitrated DMA**
engine (a temporary bus master under IEEE-696, with 24 counter bits so a transfer crosses
64 K boundaries), a digital data separator, and an onboard **BOOT EPROM** that appears at the
CPU reset address via `PHANTOM*`. Both run 8″ **and** 5.25″ drives, single **and** double
density (FM/MFM), IBM 3740 / System 34 formats. Both occupy **no memory space** — only a block
of four consecutive I/O ports (base a multiple of four, standard `C0H`) — and both do all data
transfer by DMA.

The **Disk 1** (1981) carries a **software bit-banged serial "startup" port** so a bare system
can print a sign-on and be patched before its real console board is configured; it ships **8**
boot routines (two CPU families: Z80/8085 and 8086/87). The **Disk 1A** (1984) **drops the
serial port entirely** — its console is always a separate board (System Support 1, or an
Interfacer) selected by the boot ROM — and instead puts a **Drive Select Register** and a
dedicated **Motor Control Register** (with a ~15 s auto-timeout) in the port block. It carries
**16** boot routines across **four** CPU families (adding 68000 and 32016), does software-
switched 8″/5.25″ data-rate selection, and can run 8″ and 5.25″ drives at the same time.

This is a distilled emulation reference. Kit assembly, schematics, parts lists, and the analog
data-separator / write-precompensation timing are omitted except where they set a
software-visible value. **The FDC chip internals are not re-derived here** — command encoding,
result phases, status-register semantics, the specify/seek/read/write/format command set are
the standard µPD765A/8272 model (the Disk 1 manual reprints Intel's *Single/Double Density
Floppy Disk Controller* data sheet, © Intel 1980, as pages 31–46; there is no separate
`8272.md` in this tree yet). What this file adds is the **board** wrapper: the port map, the
board registers, the DMA/boot/`PHANTOM*` mechanism, the switch/jumper configuration, and — the
point of grouping them — exactly what differs between Disk 1 and Disk 1A.

---

## 1. What differs between Disk 1 and Disk 1A

| Feature | Disk 1 (1981, bd 171F) | Disk 1A (1984, bd 203 Rev D) |
|---|---|---|
| FDC chip | NEC µPD765A / Intel 8272 | NEC µPD765A / Intel 8272 (same) |
| DMA | 24-bit, arbitrated, crosses 64 K | 24-bit, arbitrated, crosses 64 K (same) |
| **Port 0 WRITE** | *(unused)* | **Drive Select Register** (unit / data-rate / F2S / alt head-load) |
| **Port 3** | **Serial startup port** (bit-bang RS-232, D7) + boot-disable (D0) | **Motor Control Register** (5 motor bits + auto-timeout) + boot-disable (D0) |
| Port 2 IN (STATUS) bits | **D7 only** (FDC INT asserted) | **D0** drive ready, **D1** index pulse, **D2** sense-switch S3-1, **D7** FDC INT |
| Onboard console | Yes — software serial port (initial startup only) | **None** — console is a separate board chosen by boot ROM |
| 8″ / 5.25″ select | Board built for one; 5.25″ is a factory/solder mod | **Software** — Drive-Select-Reg bit 5, both at once supported |
| Motor control | One MOTOR-ON latch (the serial-out bit, gated by switch) | 4× 8″ + 1× 5.25″ motor bits; **~15 s auto-off** timer, reset on any access |
| Boot routines | **8** (2 sets of 4), **256 B** each | **16** (4 families × 4), **512 B** each (2764); 27128 → 32×512 or 64×256 |
| CPU families | Z80/8085, 8086/87 | Z80/8085/8088, 8086/286, 68000, 32016 |
| Boot EPROM page | **256** bytes at reset address | **512** bytes at reset address (256 with 27128) |
| Boot routine select | S2 pos 1–2 (binary) + **J17** low/high half | S1 pos 2–5 (binary, 4 bits) |
| Base-address switch | **S2** pos 3–8 → A7…A2 | **S3** pos 2–7 → A2…A7 |
| DMA priority switch | **S1** pos 5–8 | **S2** pos 4–7 |
| Wait-state enable | **S1** pos 1 | **S2** pos 8 |
| Boot enable | **S1** pos 4 (`OFF`=enable) | **S3** pos 8 (`ON`=enable) |
| Standard base | `C0H` (8″), `CCH` (5.25″) | `C0H` |

The FDC, the DMA engine, the boot-EPROM/`PHANTOM*` mechanism, the wait-state generator, the
DMA-priority arbiter, and the data separator are functionally the **same design** on both
boards. Disk 1A is the refinement: the fragile software serial console is gone, replaced by two
proper board registers (drive-select and motor-control), and the boot ROM grew to cover 16-bit
and 32-bit CPU families.

---

## 2. I/O port map

Four consecutive ports, base a multiple of four. Standard base `C0H` (both boards); Disk 1
also documents `CCH` as the recommended 5.25″ base. Ports below are **relative** (0–3); actual
ports are base+0…base+3.

| Rel. port | READ | WRITE — Disk 1 | WRITE — Disk 1A |
|:---:|---|---|---|
| 0 | FDC Main Status Register | *(unused)* | **Drive Select Register** |
| 1 | FDC Data Register | FDC Data Register | FDC Data Register |
| 2 | **Status Register** (board) | DMA Address Register | DMA Address Register |
| 3 | **Serial in** (Disk 1) / *(unused, Disk 1A)* | **Serial out** + boot-disable | **Motor Control Register** + boot-disable |

At the standard `C0H` base: `C0`=FDC status/drive-select, `C1`=FDC data, `C2`=board
status/DMA-address, `C3`=serial (Disk 1) / motor control (Disk 1A).

- **FDC Main Status Register** (port 0 IN) — the µPD765A/8272 main status register; poll for RQM/DIO/BUSY as usual for that chip.
- **FDC Data Register** (port 1 IN/OUT) — the µPD765A/8272 data register; all command bytes, parameters and result bytes pass through it.
- **DMA Address Register** (port 2 OUT) — a **push-down stack of three one-byte registers**. Write the 24-bit transfer address **most-significant byte first** (three writes: A23–16, A15–8, A7–0). The board's DMA counters load from this and increment per byte, independent of 64 K boundaries.

### 2.1 Board Status Register (port 2 IN)

| Bit | Disk 1 | Disk 1A |
|:---:|---|---|
| 0 | *(not significant)* | **Drive Ready** (READY = 1) |
| 1 | *(not significant)* | **Drive Index Pulse** (pulse present = 1) |
| 2 | *(not significant)* | **Sense switch S3-1** (ON = 0, OFF = 1) |
| 3–6 | *(not significant)* | not used |
| 7 | **FDC INT output asserted** (INT active = 1) | **FDC INT output asserted** (INT active = 1) |

Bit 7 is how a **polled** driver detects FDC command completion / attention without using the
S-100 vectored-interrupt lines. On Disk 1A the extra low bits let the boot ROM poll drive
readiness and read the console-select sense switch (S3-1) directly.

### 2.2 Disk 1 — Serial startup port (port 3)

A **software bit-banged** RS-232 port, not a UART. There is no baud-rate register and no
framing hardware; the boot ROM times the bit cell in a software loop (it measures terminal
speed when the operator types an upper-case **"U"** at sign-on, then prints the banner).

- **READ**, bit **D7** = current state of the serial **input** line.
- **WRITE**, bit **D7** = new state of the serial **output** line (latched until changed).
- **WRITE, D0 = 0** = **disable the BOOT EPROM** (a system reset re-enables it).

Line polarity (note the sense — this is the RS-232 line, driven to ±12 V through op-amp U6/U24B):

| D7 | Condition | Meaning |
|:---:|---|---|
| 1 | **SPACING** (+12 V) | binary 0 (a start bit is a space) |
| 0 | **MARKING** (−12 V) | binary 1 |

On **RESET** the output latch is cleared to the **MARKING** state. Electrically the port is
Interfacer-cable-compatible on connector **J9**: ground on pin 7, transmit on pin 3, receive on
pin 2, **no RS-232 handshake lines**. It is "for initial system startup only" — the manual says
do not use it as the console longer than needed to patch the BIOS. Suggested maximum baud by
CPU clock (software-timed, so clock-dependent): **2 MHz → 600, 4 MHz → 1200, 6 MHz → 2400**.

If the serial port is not wired for RS-232, **switch S1 position 3** repurposes the output latch
as a **MOTOR-ON** bit for 5.25″ minifloppies (see §5).

### 2.3 Disk 1A — Drive Select Register (port 0 WRITE) and Motor Control Register (port 3 WRITE)

Disk 1A replaces Disk 1's serial port with two board registers.

**Drive Select Register** (port 0 OUT):

| Bit | Function |
|:---:|---|
| 0 | Alternate Unit Select bit 0 |
| 1 | Alternate Unit Select bit 1 |
| 2 | Alternate Select (Normal = 0, use the alternate unit/head-load lines = 1) |
| 3 | **Force Two Sided** (Normal = 0, Force = 1) — makes the FDC access side 1 of a minifloppy that has no two-sided signal |
| 4 | Alternate Head Load (Load = 1) |
| 5 | **5.25″ / 8″ Data Rate Select** (8″ = 0, 5.25″ = 1) — switches the FDC clock 8 MHz ↔ 4 MHz |
| 6–7 | not used |

Bit 5 halves the FDC clock for 5.25″ media. After changing it you must let the FDC settle,
then re-issue a **SPECIFY** command with values recomputed for the new (4 MHz) clock. Bits 0–2
provide an "alternate" unit-select/head-load path (in the normal state the FDC's own
unit/head-load lines are used).

**Motor Control Register** (port 3 OUT):

| Bit | Function |
|:---:|---|
| 0 | **BOOT EPROM Disable** (Disable = 0; a system reset re-enables) |
| 1–3 | not used |
| 4 | 8″ Floppy 0 motor (On = 1, Off = 0) |
| 5 | 8″ Floppy 1 motor (On = 1, Off = 0) |
| 6 | 8″ Floppy 2 motor (On = 1, Off = 0) |
| 7 | 8″ Floppy 3 motor **and** the 5.25″ minifloppy motor line (On = 1, Off = 0) |

Bit 7 controls **both** 8″ drive #3 and **all** minifloppies together (minifloppies have only
one motor line) — they cannot be switched individually. The register has an **automatic
~15-second timeout**: all motors turn off ~15 s after the last controller access, and **any**
access to the board restarts the timer. Jumper **J11** installed = timeout active; J11 removed =
motors always on. (On Disk 1 the equivalent boot-disable write is `D0 = 0` to the **serial**
port; on Disk 1A it is `D0 = 0` to this Motor Control Register — same bit, same port 3.)

---

## 3. Boot EPROM and PHANTOM*

On power-up the boot EPROM appears at the host CPU's **reset address**, aligned to the nearest
page (**256 B** on Disk 1, **512 B** on Disk 1A). A Z80/8085 resets to `0000H`; an 8086/8088
resets to `FFFF0H`, so the EPROM appears from `FFE00H`/`FFF00H`. The board asserts `PHANTOM*`
over its window while booting, so **at least one memory page at the reset address must respond
to `PHANTOM*` by disabling itself** — Disk 1 needs ≥256 B, Disk 1A needs ≥512 B of such RAM, or
a bus-drive conflict (and possible damage) results. The boot code disables the EPROM by writing
`D0 = 0` to port 3 (serial port on Disk 1, motor register on Disk 1A); a hardware reset
re-enables it.

The boot ROM loads track-0 sectors into memory and passes a value (the console-routine selector,
tied to sense switch S3-1) to the loaded OS so it picks the right console I/O.

### 3.1 Disk 1 boot routines (8, selected by S2 pos 1–2 + J17)

Two sets of four 256-byte routines. **S2 positions 1–2** pick one of four in binary; **J17**
(A/B) selects the low or high half of the EPROM.

| J17 | S2-1 | S2-2 | EPROM start | Routine # |
|:---:|:---:|:---:|:---:|:---:|
| B | ON | ON | 000H | 0 |
| B | ON | OFF | 100H | 1 |
| B | OFF | ON | 200H | 2 |
| B | OFF | OFF | 300H | 3 |
| A | ON | ON | 400H | 4 |
| A | ON | OFF | 500H | 5 |
| A | OFF | ON | 600H | 6 |
| A | OFF | OFF | 700H | 7 |

Routine intent (routines 4–7 are the CP/M-80 / CP/M-86 set; 0–3 duplicate them for 8086/87,
except #1 which is unused by 8086/87):

- **#4** — Interfacer 1/2 serial console at `00`/`01`, LPT list at `02`/`03`.
- **#5** — Disk 1's **own onboard serial port** as console and list (**CP/M-80 only**; CP/M-86 has no on-board-serial routine).
- **#6** — System Support 1 serial channel at `50H`, 9600 baud, as console and list.
- **#7** — Interfacer 3/4 at `10H`, 9600 baud, user 7 = console, user 6 = list.
- **#0/#2/#3** — identical to #4/#6/#7 but for CPU 8086/87.

Some Disk 1 boards ship set for routines 4–7 rather than 0–3; in that case leave J17 as
shipped and treat them as 0–3.

### 3.2 Disk 1A boot routines (16, selected by S1 pos 2–5)

Four routines per CPU family, 512 bytes each; **S1 positions 2–5** form the 4-bit selector
(the EPROM's address lines). Standard config is a 2764 with 16×512 B routines (a 27128 holds
32×512 B or 64×256 B).

| Routine # | S1 2–5 (binary) | EPROM start | CPU family |
|:---:|---|:---:|---|
| 0–3 | On On On On … On On Off Off | 0000–0600H | 8085/8088/Z80 |
| 4–7 | On Off On On … | 0800–0E00H | 8086/286 |
| 8–11 | Off On On On … | 1000–1600H | 68000 |
| 12–15 | Off Off On On … | 1800–1E00H | 32016 |

Within each family the four routines differ only in drive-search behavior:

- **First (0, 4, 8, 12)** — look for an 8″ drive as drive 0; if not ready, boot from the DISK 3 (hard disk controller).
- **Second (1, 5, 9, 13)** — always boot from DISK 3, never look for floppies.
- **Third (2, 6, 10, 14)** — try 8″ drive 0; else a 5.25″ as physical drive 2; else loop (8″, then 5.25″).
- **Fourth (3, 7, 11, 15)** — try a 5.25″ drive 0; else DISK 3; then loop.

Every routine ends by passing (2 + the sense-switch **S3-1** value) so the OS selects the
console: **S3-1 ON** → System Support 1 console, **S3-1 OFF** → Interfacer 3/4 user 7.

---

## 4. Switch and jumper configuration

Base address is set by a DIP switch decoding the upper address bits, **`ON` = 0, `OFF` = 1**
(so all `ON` = base 00, and the C0H base has the two high positions `OFF`). Note the switch
identities differ between the boards:

| Function | Disk 1 | Disk 1A |
|---|---|---|
| Base address (A7…A2) | **S2** pos 3→A7, 4→A6, 5→A5, 6→A4, 7→A3, 8→A2 | **S3** pos 2→A2, 3→A3, 4→A4, 5→A5, 6→A6, 7→A7 |
| Boot routine select | S2 pos 1–2 + J17 (§3.1) | S1 pos 2–5 (§3.2) |
| Wait-state enable | **S1 pos 1** ON = enable | **S2 pos 8** ON = enable |
| Boot EPROM enable | **S1 pos 4** OFF = enable | **S3 pos 8** ON = enable |
| DMA priority (4 bits) | **S1 pos 5–8** | **S2 pos 4–7** |
| Motor / serial mode | **S1 pos 3** (serial vs MOTOR-ON latch) | (n/a — dedicated motor register) |
| Console select (to OS) | (via boot routine) | **S3 pos 1** ON = System Support 1, OFF = Interfacer |

**DMA priority** is a 4-bit binary value, `ON` = 0 / `OFF` = the position's weight. Highest
weight is the lowest switch number in the group (Disk 1 S1-5 = 8, S1-6 = 4, S1-7 = 2, S1-8 = 1;
Disk 1A S2-4 = 8, S2-5 = 4, S2-6 = 2, S2-7 = 1). All four `OFF` → priority 15 (highest); all
`ON` → priority 0. Up to 16 bus masters can coexist under IEEE-696 arbitration, one per
priority level. CompuPro software conventionally runs the Disk boards at priority 15.

**Wait states** cover the slow EPROM/FDC access on fast CPUs. When enabled, the EPROM read gets
**5** wait states and each I/O or DMA cycle gets **2/3/4**, chosen by a jumper (Disk 1: **J16**
A = 2, B = 3, removed = 4; Disk 1A: **J7** B = 2, A = 3, removed = 4). CompuPro recommends 4
wait states with 5.25″ drives.

**Vectored interrupts** (both boards): the FDC's INT output can drive one of the S-100 vectored
lines **VI0*–VI7*** via a shunt/#30-wire jumper (Disk 1: **J0–J7** → VI0–VI7, **J8** → `INT*`;
Disk 1A: **J10** positions 0–7 → VI0–VI7). Use the highest-priority line so it can't be masked
off. CompuPro software uses **VI4*** for the floppy interrupt. Both boards can instead run
**polled** off Status-Register bit 7 (§2.1).

Disk 1A default jumpers (as shipped, C0H / priority 15 / EPROM wait states on): J1–J2 pos 8
(or "5" for 5.25″), J3–J4 pos 5 for 5.25″ / pos 8 for 8″, J5 removed, J6 A–C for all CompuPro
drives, J7 B–C for 8″ (removed for CompuPro 5.25″ alone), J8/J9 B–C, J10 pos 4, J11 installed,
J12/J13 removed.

---

## 5. Drive interface and connectors

Disk 1 brings the drive interface out on connector **J10**; Disk 1A on **CONN 2** (8″, 50-pin)
and **CONN 1** (5.25″, 34-pin). All odd pins are ground. The 8″ 50-pin pinouts are
substantially the same across the two boards. Salient signals:

- **8″ (50-pin):** LOW CURRENT (2), MOTOR OFF 1/2/3 (4/6/8), TWO SIDED (10), SIDE SELECT (14), HEAD LOAD (18), INDEX (20), READY (22), MOTOR OFF 4 (24), DRIVE SELECT 1–4 (26/28/30/32), DIRECTION SELECT (34), STEP (36), WRITE DATA (38), WRITE GATE (40), TRACK 00 (42), WRITE PROTECT (44), READ DATA (46).
- **5.25″ (34-pin):** HEAD LOAD (4), DRIVE SELECT 4 (6), INDEX (8), DRIVE SELECT 1–3 (10/12/14), MOTOR ON (16), DIRECTION SELECT (18), STEP (20), WRITE DATA (22), WRITE GATE (24), TRACK 00 (26), WRITE PROTECT (28), READ DATA (30), SIDE SELECT (32), READY (34).

**8″ drive requirements (both boards):** the stepper/spindle motors must be enabled **at all
times** (not tied to drive select or head load), because the 765A/8272 continually scans the
drives — tying head-load to drive-select makes the drive buzz. This means the +24 V supply
carries full load continuously and the enclosure must cool for it.

**5.25″ minifloppy programming notes (Disk 1A, §"Programming Considerations"):**
1. Minifloppies use half the 8″ data rate → set Drive-Select-Reg bit 5 (5.25″), wait for the FDC to settle, and re-SPECIFY for the 4 MHz clock.
2. Almost all minifloppies have a motor line → turn the motor on and wait for spin-up; the Motor Register times out in ~15 s (install **J11** to keep motors on, if desired).
3. If the drive drives READY, connect **J6 A-C**; if not, connect **J6 B-C** so READY is forced whenever the 5.25″ bit is set (you then can't tell if the drive is truly ready).
4. Minifloppies have no single/double-sided signal → set **Force Two Sided** (Drive-Select bit 3) for double-sided media, or the FDC won't access side 1.

**Disk 1 5.25″ conversion:** a Disk 1 built for 8″ is converted to 5.25″ by a solder mod
(alternate parts, cut three traces behind J11/J12/J13, add jumpers "5"/"C" and J14, set J15 for
the READY option). CompuPro offered the mod at the factory.

---

## 6. Density / format compatibility

Both manuals carry the same **WARNING**: not all controllers generate true IBM 3740 / System 34
format even when they claim to, so do **not** write data onto a diskette formatted by another
controller — format fresh diskettes on the Disk 1/1A and copy onto them. The technical reason
(Disk 1 Software User's Guide): most **WD 1791**-type controllers insert a byte of `00` right
after the header CRC bytes; that byte is not specified in either IBM standard and can confuse
the 8272/765 on the Disk 1. Diskettes formatted by a true-IBM controller (or by IBM) are fine.
This is a real interoperability note for anyone modeling cross-controller disk exchange between
a CompuPro machine and a WD177x-based one (Tarbell, Cromemco, VersaFloppy).

---

## 7. Emulation checklist

For a `disk1` / `disk1a` board over a shared 8272/765A FDC core:

1. **Four ports at a multiple-of-four base (default `C0H`).** Port 0 IN = FDC main status, port 1 = FDC data, port 2 IN = board status / OUT = 24-bit DMA address (three writes, MSB first). Port 3 and port-0 OUT differ per board (§2).
2. **DMA is the only data path.** The FDC's DRQ drives an onboard 24-bit counter that reads/writes host memory directly, incrementing across 64 K boundaries; there is no PIO data port. Model the transfer as DMA against the loaded 24-bit address.
3. **Board Status bit 7 = FDC INT** (both boards) is the polled completion flag. Disk 1A adds ready/index/sense-switch in bits 0–2.
4. **Disk 1 serial port (port 3, bit 7):** a bit-banged RS-232 line, MARKING/SPACING inverted onto D7 (D7=1 → space → logic 0). Rarely needed for guest software once a real console board exists; model it only if booting routine #5. Boot-disable = write D0=0.
5. **Disk 1A Drive Select Register (port 0 OUT)** and **Motor Control Register (port 3 OUT):** unit/data-rate/F2S/head-load, and 5 motor bits with a ~15 s auto-off timer (reset on any access, defeated by removing J11). Boot-disable = write D0=0 to the motor register.
6. **Boot EPROM via `PHANTOM*`** at the reset address (256 B page Disk 1, 512 B Disk 1A); needs cooperating RAM that yields to `PHANTOM*`. Disabled by the D0=0 write, re-enabled by reset. The specific boot image is the thing that actually varies; the search/console-select behavior is in §3.
7. **Config knobs that change guest-visible behavior:** base address, boot-routine number (console selection), DMA priority, wait-state count, and the VI-line the FDC interrupt drives (default VI4*). Everything else (data separator, precompensation) is timing the guest cannot read back — do not model it as hardware the guest can observe.
8. **FDC internals defer to the µPD765A/8272 data sheet** (reprinted in the Disk 1 manual). If/when an `8272.md` is added, this file points at it exactly as the Cromemco FDC family points at the FD1771 datasheet.

---

## 8. Errata / traps

- **Switch identities are not shared between the boards.** Base address is on S2 (Disk 1) vs S3 (Disk 1A); DMA priority on S1 (Disk 1) vs S2 (Disk 1A); wait/boot-enable move too (§4). Do not carry a Disk 1 switch map onto a Disk 1A.
- **Base-address switch is inverted** (`ON` = 0). The standard `C0H` base is *not* "all on".
- **Boot-enable polarity is opposite between boards:** Disk 1 **S1-4 OFF** = enabled; Disk 1A **S3-8 ON** = enabled.
- **DMA Address Register is MSB-first into a three-deep push-down stack** — three writes, high byte first, not a low/high pair.
- **Disk 1 serial-line polarity is inverted onto D7** (SPACING/logic-0 = D7 high; MARKING/logic-1 = D7 low), and cleared to MARKING on reset — easy to get backwards.
- **Motor bit 7 (Disk 1A) is shared** between 8″ drive #3 and *all* minifloppies; you cannot switch them independently.
- No internal contradictions were found in either manual.
