# SD Systems VersaFloppy I & VersaFloppy II

Source: [VersaFloppy-I Manual.pdf](#), [VersaFloppy-II Manual.pdf](#)

SD Sales / SD Systems (S.D. Computer Products), Dallas/Garland TX. Two generations of one
S-100 floppy-disk controller board. **VersaFloppy I** (FD1771B-1, *single density* FM only)
and **VersaFloppy II** (FD1791B-1, *single **and** double density* FM/MFM). The two share the
same port layout, the same board-level control/status register at the same address, and the
same driver software family (DDBIOS / the SD & MS monitors) — they differ only in the FDC chip
and a few bits of the control register. This is a distilled emulation reference: kit-assembly
steps, parts lists, PCB layouts, trim/alignment procedures, marketing and prices from the
original manuals are intentionally omitted; only what is needed to emulate the board in
software is kept.

The controller chips themselves are documented separately — see
[`Western Digital FD1771 - Datasheet.md`](Western%20Digital%20FD1771%20-%20Datasheet.md) for
the VersaFloppy I's FD1771, and
[`Western Digital WD177X-00 - Datasheet.md`](Western%20Digital%20WD177X-00%20-%20Datasheet.md)
for the 179x-family behavior the VersaFloppy II's FD1791 follows. This reference documents only
what the *board* adds around the chip. The floppy geometry and command sequences here are
cross-checked against **DDB200.ASM** (the SDOS/COSMOS "DDBIOS" driver by Rex Brown, v3.3
6/24/82) and **VF2ROM.Z80** (the VersaFloppy II boot-ROM disassembly), which are the
authoritative artifacts for the numbers the manuals leave to software.

---

## 1. What the boards are

| | VersaFloppy I | VersaFloppy II |
|---|---|---|
| FDC chip | **Western Digital FD1771B-1** | **Western Digital FD1791B-1** |
| Recording | **single density** (FM, IBM 3740) | **single *and* double density** (FM/MFM) |
| Drive sizes | 8″ (full) and 5¼″ (mini) | 8″ (full) and 5¼″ (mini), mixable |
| Sector size | 128 bytes | 128 bytes (also 256-byte DD formats) |
| Board crystal | 4 MHz | 16 MHz (data separator + FDC) |
| Data transfer | CPU **wait-state** sync (PRDY) on the data port | same |
| Boot PROM on board | **none** — bootstrap BIOS lives on a separate PROM/memory board at `F000H` | none |

Both are S-100 slave I/O boards, not bus masters. The FDC's DRQ line drives a wait-state
generator rather than an interrupt, so a sector transfer is a tight `IN`/`OUT` loop on the data
port with no software DRQ polling (§4). Interrupts (INTRQ) are supported but the standard SD
control software does not use them — it polls the FDC status register.

---

## 2. I/O port map

The board decodes only the **low eight address bits (A0–A7)** and answers at one of two
jumper-selected 8-port windows. The manuals give addresses in **hex only** (no octal).

| Window | Ports | Jumper | Note |
|--------|-------|--------|------|
| **X = 6** (standard) | **60H–67H** | VF-I E33–E34 | Used by all the standard SD control software |
| X = E | E0H–E7H | VF-I E32–E33 | Alternate |

Within the standard window the functional ports are **63H–67H**. The board names them X3–X7:

| Port (X=6) | Label | `OUT` (write) | `IN` (read) |
|-----------|-------|---------------|-------------|
| **60H** | — | **controller reset strobe** *(used by DDBIOS/VF-II; not documented in the VF-I manual — see note)* | — |
| 61H, 62H | — | *unused / not decoded* | *unused* |
| **63H** | X3 | **board control register** (drive/side/density/wait — §3) | **board status register** (readback + INTRQ — §3) |
| **64H** | X4 | FD177x **Command** register | FD177x **Status** register |
| **65H** | X5 | FD177x **Track** register | FD177x Track register |
| **66H** | X6 | FD177x **Sector** register | FD177x Sector register |
| **67H** | X7 | FD177x **Data** register (wait-state synced) | FD177x Data register (wait-state synced) |

**⚠ Port 60H.** DDB200.ASM defines `RSET EQU X+0` (= 60H) as the "controller reset addr" and
the VersaFloppy II board responds to it. The **VersaFloppy I manual documents only 63H–67H**
and describes no reset register — a write to 60H on a VF-I is best emulated as a no-op (the
decoder only gates 63–67). On the VF-II, treat an access to 60H as a controller reset. 61H/62H
are unused on both.

The `IN`/`OUT` data buffers on the board are **inverting** (they compensate for the FD179x's
negative-true data bus); this is internal and invisible to the guest — the register values the
CPU reads/writes are already true-sense.

---

## 3. Board control/status register (port 63H)

This is the board's own 8-bit latch, separate from the FDC. **Its bit layout differs between
VF-I and VF-II** — the most important software-visible difference between the two boards.

### 3.1 VersaFloppy I — `OUT 63H` (control)

| Bit | Function |
|-----|----------|
| D0–D3 | **Drive Select 1/2/3/4** (one line per drive) |
| D4 | Side Select (double-sided drives) |
| D5 | Restore drive (optional) |
| D6 | **Wait-State circuit enable** — set before a sector read/write |
| D7 | Interrupt-Control circuit enable |

### 3.2 VersaFloppy II — `OUT 63H` (control)

| Bit | Function |
|-----|----------|
| D0–D3 | **Drive Select 1/2/3/4** |
| D4 | Side Select (double-sided drives) |
| D5 | **5″ / 8″ drive select** |
| D6 | **Double / Single density** |
| D7 | **Wait-State enable** |

DDB200.ASM's boot probe confirms the VF-II layout: it `RES 6` to clear the density bit, `SET 5`
to clear the 8″-select bit, `SET 6` to set double density (bits are active per the manual's
sense).

### 3.3 `IN 63H` (status)

- **VersaFloppy II:** reads back the current state of all eight `OUT 63H` control bits.
- **VersaFloppy I:** D0–D4 read back the control latch; **D5** = state of the "double-sided"
  configuration jumper; **D6** = seek-complete / double-sided drive signal; **D7** = **INTRQ
  from the FDC** (high when the FD1771 has raised INTRQ / finished a command). DRQ is *not*
  visible here — it is consumed by the wait-state hardware (§4).

---

## 4. FDC registers and the wait-state data transfer

Ports 64H–67H are the FD177x's four registers in the chip's standard A1/A0 order:
Command/Status, Track, Sector, Data. Their bit-level behavior (the six type-dependent status
registers, the stepping-rate/head-load flags, the master-reset-on-`MR` behavior) is the WD
chip's — see the FD1771 / WD177X datasheet references. The board adds one thing:

**Data transfer is CPU-wait-state synchronized, not DRQ-polled.** The FDC's DRQ drives a
wait-state generator whose output is **PRDY (S-100 pin 72)**. Wait states are inserted **only
on accesses to the data port 67H**, and only while the wait-state generator is enabled
(control bit — VF-I D6 / VF-II D7). So the transfer loop is simply:

```
    ; sector read (buffer at HL, count in B)
loop:  IN   A,(67H)   ; CPU stalls here until the FDC's DRQ is ready
       LD   (HL),A
       INC  HL
       DJNZ loop
```

with **no software polling of DRQ**. Reads/writes of 63H–66H never stall. The End-of-Command
routine **disables the wait-state generator before reading the FDC status at 64H**, so the
final status read must not wait. To emulate: honor DRQ-driven waits on 67H accesses only, and
gate them on the control-register wait-enable bit.

---

## 5. Command codes and error status

### 5.1 Disk-controller command codes

The stepping-rate bits differ between mini (5¼″) and full (8″) drives, and between the FD1771
and FD1791, so the exact command bytes differ per board. From the manuals' Table 8-1 and the
drivers:

| Operation | VF-I FD1771 mini | VF-I FD1771 full | VF-II FD1791 mini | VF-II FD1791 full |
|-----------|:---:|:---:|:---:|:---:|
| Restore to track 0 | 03 | 0A | 0B *(DDBIOS: 08)* | 09 *(DDBIOS: 08)* |
| Track seek, no verify | 13 | 1A | 11 *(DDBIOS: 18)* | 19 *(DDBIOS: 18)* |
| Track seek, verify | 17 | 1E | 1F *(DDBIOS: 1C)* | — *(DDBIOS: 1C)* |
| Format track | F4 | F4 | F4 | F4 |
| Read sector | 88 | 88 | 88 | 80 |
| Read sector, load head | 8C | 8C | — | — |
| Write sector | A8 | A8 | A8 | A0 |
| Write sector, load head | AC | AC | — | — |
| Read address | C4 | C4 | C4 | C0 |

*(DDB200.ASM's per-density format tables use restore/seek/verify = **08/18/1C** for 8″ and
**0B/1B/1F** for 5″, and its equates `RDCMD=88H WRCMD=0A8H WRTCMD=0F4H RDACMD=0C0H`. These are
the bytes actually issued at runtime; the manual's table is the documented set.)*

### 5.2 Error-status byte

Both manuals and DDB200.ASM agree on the driver's decoded error byte (`ERSTAT`, RAM 47H):

| Bit | Meaning |
|-----|---------|
| D0 | Write / read of a **deleted** data-address-mark sector |
| D1 | **DRQ** (indicates excessive noise on the S-100 bus) |
| D2 | Data Lost |
| D3 | CRC Error |
| D4 | Sector Not Found |
| D5 | Track Seek Error |
| D6 | Write-Protected diskette |
| D7 | Drive Not Ready |

Two whole-byte sentinel values: **`FEH` = controller hang-up**, **`0FH` = invalid-track error.**

---

## 6. Drive and media geometry

128-byte sectors, IBM 3740 soft-sectored. The exact geometry per format comes from DDB200.ASM's
density tables (the definitive source):

| Format | Sectors/track | Tracks/side | Bytes/sector | Density | Restore/Seek/Verify |
|--------|:---:|:---:|:---:|:---:|:---:|
| 8″ single density | 26 | 77 | 128 | FM | 08 / 18 / 1C |
| 8″ double density | 50 | 77 | 128 | MFM | 08 / 18 / 1C |
| 8″ double density, 256 B | 26 | 77 | 256 | MFM | 08 / 18 / 1C |
| 5¼″ single density | 18 | 35 | 128 | FM | 0B / 1B / 1F |
| 5¼″ double density | 29 | 35 | 128 | MFM | 0B / 1B / 1F |

Single vs double sided is selected by control-register bit D4; double-sided formats use both
sides. Approximate data rate (VF-I): **32 µs/byte** at 8″, **64 µs/byte** at 5¼″ (double those
rates for VF-II double density). Supported drives include Shugart SA400/450 (mini) and
SA800/850 (8″), MFE 700/750, Persci 70/277, GSI GS-105, CDC 9404/9406.

The SD monitor / DDBIOS **boot probe** reads track 0 sector 1 to `0080H` and identifies the
format by trying, in order, 8″ DD-256, 8″ SD, 5″ SD, 5″ DD, adjusting the control-register
size/density bits between tries.

---

## 7. Interrupts

The board can run with or without interrupts; the standard SD software polls. When enabled
(VF-I control bit D7):

- On end-of-command the FDC raises **INTRQ**; on a VF-I it is readable at `IN 63H` bit D7.
- Jumpers select the delivery mode (VF-I §5-4): 8080 on-board restart code (E37–E38), 8080 with
  an external priority-interrupt board via **VI2 / S-100 pin 6** (E13–E14), Z80 mode-2 on-board
  vector (E37–E38), or Z80 mode-2 off-board CTC / SBC-100 (E13–E14). The board participates in
  the S-100 priority chain via **IEI (pin 14) / IEO (pin 64)** and must be highest priority
  during a sector transfer. The literal restart/vector byte is set in the on-board vector code
  and is not stated as a value.

---

## 8. Bootstrap and driver software

Neither VersaFloppy board carries a boot PROM. The bootstrap **BIOS** (called **DDBIOS** —
"Double Density Basic I/O System") lives in a 2708/2758-class PROM on a separate PROM/memory
board (or the SBC-100/200's onboard PROM), conventionally at **`F000H`**, and CP/M / SDOS boots
by executing it. See [`SD Systems SDOS.md`](SD%20Systems%20SDOS.md) for the operating system
and [`SD Systems Monitor.md`](SD%20Systems%20Monitor.md) for the monitor's `C` (boot), `R`
(read), `W` (write) and `Z` (format) disk commands, which drive this controller directly. The
DDBIOS jump table (from DDB200.ASM) publishes `HME F018H`, `LDE F02DH`, `SVE F030H`,
`FMATE F033H`, and the density tables at `STDSDT F03FH`.

---

## 9. Emulation checklist (summary of load-bearing facts)

- **Two boards, one port layout.** 8-port block at **60–67H** (standard, X=6) or E0–E7H (X=E),
  decoded on A0–A7 only. Functional ports **63–67H**; 61/62 unused. **60H** = controller reset
  (VF-II/DDBIOS; a no-op on VF-I).
- **Port 63H is the board's own latch** — control on write, status on read. **Bit layout
  differs:** VF-I D5=restore, D6=wait-enable, D7=int-enable; VF-II D5=5″/8″, D6=density,
  D7=wait-enable. Both: D0–D3 one-hot drive select, D4 side. VF-I `IN 63H` D7 = INTRQ.
- **Ports 64–67H are the FD177x** (VF-I = FD1771, single density; VF-II = FD1791, single+double).
  Standard WD register order Command/Status, Track, Sector, Data.
- **Data transfer stalls the CPU** via PRDY on **67H accesses only**, gated by the wait-enable
  control bit — no DRQ polling. 63–66H never wait; disable waits before the final status read.
- **Geometry (from DDBIOS):** 8″ SD 26×77, 8″ DD 50×77, 8″ DD-256 26×77×256, 5″ SD 18×35, 5″ DD
  29×35; all 128 B/sector unless noted. Restore/seek/verify 08/18/1C (8″) or 0B/1B/1F (5″).
- **Error byte:** D1 DRQ, D2 data-lost, D3 CRC, D4 not-found, D5 seek-error, D6 write-protect,
  D7 not-ready, D0 deleted-mark; `FEH` hang, `0FH` invalid track.
- **No onboard boot PROM** — DDBIOS BIOS is external at `F000H`; polled, no interrupts in the
  standard driver.
