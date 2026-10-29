# Jade Double D — Firmware Listings (CP/M 2.2 Release 1, and DCM 1.3)

Source: [Double-D_Firmware_1983.pdf](#)

A 151-page bundle of printouts for the Jade Double D controller, scanned by the maintainer
(2018-05-13). It holds the Jade CP/M 2.2 **Release 1** distribution notes, the assembled
listings of every Jade program on that disk (TDL Z80 assembler 2.21, by Stan Krumme), a
later **DCM 1.3** rewrite by "GRH" (Cromemco assembler, August 1982 to August 1983), and
handwritten notes by a previous owner. The OCR text layer is dirty. Every number below was
read from the page image.

The board itself is in
[Jade Double D Disk Controller Rev C Manual](Jade%20Double%20D%20Disk%20Controller%20Rev%20C%20Manual.md).
That file has the port bits, the Release 2 command block and the Release 2 boot. This file
adds what the listings show about **Release 1**, **Rev B boards**, the **disk layout** and
the **formats**. **Not emulated.**

Pages are cited as PDF pages ("p. 56"). Each listing also has its own page numbers, which
are not used here.

---

## 1. What is in the scan

| PDF pp | Content |
|---|---|
| 1–5 | Release 1 notes: the file list, the system track layout (§3), the generator utilities, and a note that the Release 1 DDBIOS is built for a **Rev B** board |
| 6 | Shugart SA800 jumpers (the same as ECN 9 in the hardware manual) |
| 7–8 | Digital Research CP/M 2.2 notes: BDOS functions 37 and 40, and field change CPM22-0001 (2/19/80, MOVCPM). Not Jade's |
| 9–17 | **Handwritten notes**: the host port (§2), the command block at Z80 1370h, and commands 0–7 with their parameters |
| 18–34 | **DDBIOS**, BIOS – CP/M 2.2, version 2.2 |
| 35–63 | **DCM** (Disk Controller Module), version 2.20, 8″ drives |
| 64–70 | **SBD** (System Bootstrap Driver), the host half of the boot PROM |
| 71–77 | **BIM** (Bootstrap Injection Module), the Double D half of the boot |
| 78–82 | **BLT** (BIOS Loader Transient), the MOVCPM cold-start loader at 0900–097Fh |
| 83–107 | **FORMAT**, version 2.2, with the two format modules (§5) and a DDT dump of them (p. 108) |
| 109–134 | **DCM 1.3** by GRH (§6) |
| 135–151 | Handwritten flowcharts of DCM routines (read, write, select, home, head load). Not transcribed |

---

## 2. Rev B and Release 1 — the host port

Release 1 is assembled for a **Rev B** board (p. 5). SBD (p. 65) and DDBIOS (p. 19) have
conditionals for both revisions, set to Rev B. Release 2's DDBOOT has the same conditionals,
set to Rev C.

| Item | Rev B | Rev C |
|---|---|---|
| Halt bit in the status port (`DS.HLT`) | **02h (D1)** | 01h (D0) |
| Window switch mask (`DS.ASW`) | **0Ch (D2–D3)** | 0Eh (D1–D3) |
| Window base (`D.BASE`) | **E400h** (E000h if the MA10 jumper is fitted) | E000h |

The control port values are the same on both: `DC.MB0`/`DC.MRQ` 01h, `DC.MB1` 03h,
`DC.MRT` 00h, `DC.EXC` 02h, `DC.BGN` 80h (p. 65). The port is 43h (`D.PORT`).

**The handwritten notes (p. 15) describe the Rev B status port:** D0 "window not present",
D1 "busy" (the controller is running a program), D2 and D3 the window address switches. This
agrees with the Rev B conditionals above. On Rev B the window can only be at E400, EC00,
F400 or FC00h without the MA10 jumper, because A10 is fixed at 1. The notes also give the
control port values above, with 80h as "resets Z80 when bank request asserted".

---

## 3. The system tracks (pp. 3–5)

The distribution disk is an 8″ disk. Track 0 is single density, 26 sectors of 128 bytes,
numbered in order. Track 1 is double density, 48 sectors of 128 bytes, physically staggered.
**All data tracks on the distribution disk are single density**, so that a single-density
CP/M machine can read it. "B" in the table is the CP/M bias for the system size.

**Track 0 (SD, 26 × 128):**

| Sector | Module | Runs at | Note |
|---|---|---|---|
| 1 | IDT | — | The ID sector (§4) |
| 2 | BLT | 1380h on the Double D | Read into the DCM sector buffer |
| 3 | — | — | Not used |
| 4–11 | BIOS | 4A00h + B | 1 K |
| 12 | RSV | — | Reserved |
| 13–20 | DCM | 1000h on the Double D | 1 K |
| 21–26 | RSV | — | Reserved |

**Track 1 (DD, 48 × 128):** sector 1 spare, sectors 2–17 CCP (3400h + B), sectors 18–45
BDOS (3C00h + B), sectors 46–48 spare.

**The generator utilities (p. 5):**

| Utility | Module | Track | Sectors | Memory image |
|---|---|---|---|---|
| SYSGEN.COM | BLT | 0 | 2 | 0900–097Fh |
| | CCP | 1 | 2–17 | 0980–117Fh |
| | BDOS | 1 | 18–45 | 1180–1F7Fh |
| BIOSGEN.COM | BIOS | 0 | 4–11 | 1000–13FFh |
| DCMGEN | DCM | 0 | 13–20 | 1000–13FFh |

A handwritten note on p. 3 shows a different track 0 layout for a later "SYS" disk, with a
DIR area and the DCM and BIOS moved. It is not explained, and nothing here depends on it.

---

## 4. The ID sector and drive log-on

DCM reads track 0, sector 1 when it logs on a drive. If the sector does not hold the label,
DCM uses the standard 8″ IBM 3740 format (p. 35). Where the ID sector lands in the DCM
sector buffer (DCM 2.20, p. 56):

| Offset in the buffer (1380h) | Size | Content |
|---|---|---|
| 00h | 8 | Label `JADE DD ` (`ID.LBL`, compared with `JADEID`) |
| 20h | 16 | Sectors per track (`ID.SPT`) |
| 30h | 1 | Sector stagger (`ID.STG`) |
| 31h | 1 | Disk flags (`ID.FLG`). 00h = 3740 |

Release 2's DDBIOS copies the DPB from 13A0h and the flags from 13B1h (window offsets 03A0h
and 03B1h), so the layout did not change.

---

## 5. Disk formats (FORMAT, pp. 105–108)

FORMAT copies a **format module** into Double D bank 1 at offset 0300h (Z80 1700h) and sends
command 3. A module is **Z80 code**, not a table. It begins with `FORMAT!`, a density letter
(`S` or `D`), `LD HL,<sector table>` and `LD E,<sectors>`, and DCM enters it at 1708h. Each
`BLOCK n,v` in the source assembles to a loop: `IN A,(80h)` (wait for DRQ), `LD A,v`,
`XOR C` (the FDC bus polarity, see the hardware-manual file), `OUT (07h),A`, `DJNZ`. The
track number byte is read from the FDC track register. The sector numbers come from the table.

**FT3740 — single density, 26 × 128, sectors 1–26 in order:**

| Field | Bytes |
|---|---|
| Track start | 40 × FFh, 6 × 00h, index mark (FCh), 26 × FFh |
| Each sector | 6 × 00h, ID mark (FEh), track, side 0, sector, size 0, CRC (F7h), 11 × FFh, 6 × 00h, data mark (FBh), 128 × E5h, CRC, 27 × FFh |
| Track end | FFh until the FDC ends the write |

**FTJ48D — double density, 48 × 128:**

| Field | Bytes |
|---|---|
| Track start | 80 × 4Eh |
| Each sector | 12 × 00h, 3 × F5h, ID mark (FEh), track, side 0, sector, size 0, CRC, 22 × 4Eh, 12 × 00h, 3 × F5h, data mark (FBh), 128 × E5h, CRC, 22 × 4Eh |
| Track end | FFh until the FDC ends the write |

**The FTJ48D sector order** (a 4-way stagger): 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45,
2, 6, 10, … 46, 3, 7, 11, … 47, 4, 8, 12, … 48.

A handwritten note beside FTJ48D (p. 106) adds an index field (12 × 00h, 3 × F6h, index mark,
50 × 4Eh) after the 80 × 4Eh. The printed module has no index mark in double density.

---

## 6. Release 1 vs Release 2A vs DCM 1.3

| Item | DCM 2.20 (Release 1, this scan) | DCM2 Release 2A (`jadedd-cpm22`) | DCM 1.3 (GRH, this scan) |
|---|---|---|---|
| Assembled at | 1000h | 1000h | 0000h |
| Command block | 1370h | 1370h | 0370h (the same RAM) |
| Commands | 0–3, 7. 4 = not implemented. 5, 6 unused (handwritten notes) | 0–7. 5 = list output, 6 = list status | 0–3, 7. 4 not implemented, 5–6 "spare" (a command error) |
| Timing block | **6 words** at 130Eh | **4 words** at 1010h | 6 words |
| Sector buffer | 128 bytes at 1380h | 128 bytes at 1380h | 128 bytes at 0380h, plus a **256-byte physical buffer at 0400h** |
| Other | — | List output through the EIA port | RECAL when track 0 is sensed outside track 0. **Deblocks 256-byte sectors** into two 128-byte halves |

**DCM 2.20 timing block** (p. 56, units of 0.1 ms), with the listing's own table for other
drives:

| Word | Use | SA800 (set) | FDD100-8C | SA850 |
|---|---|---|---|---|
| `TM.HLD` | Head engage | 350 | 250 | 350 |
| `TM.STP` | Step interval | 80 | 60 | 30 |
| `TM.ALS` | After last step | 80 | 80 | 120 |
| `TM.MTO` | Motor start | 1 | 1 | 1 |
| `TM.PLD` | Phase lock recovery | 1200 | 1200 | 1200 |
| `TM.SAW` | Step after writing | 10 | 10 | 10 |

Release 2A keeps the first four words (350, 80, 80, 1) and drops `TM.PLD` and `TM.SAW`.

**Boot timing:** the Release 1 BIM steps at 8 ms, waits 1 ms before it reads and allows 80
steps (p. 72). The Release 2 DDBOOT module uses 10 ms, 40 ms and 76 steps.

**Boot size:** the Release 1 SBD copies a 192-byte (C0h) injection module (p. 65). The
Release 2 module is 200 bytes (C8h).

---

## 7. The Release 1 boot chain

The boot in Release 1 is the same as in Release 2. The only changes are the sizes and timings
in §6.

1. **SBD** (host, p. 66) finds the window from the status port, takes bank 0, copies BIM to
   the window, and writes 80h to reset the Double D Z80.
2. **BIM** (on the Double D) homes drive 0 with software steps, reads DCM (track 0,
   sectors 13–20) into bank 1 and jumps to the DCM cold entry at bank 1 + 3. On an error it
   halts with an error code in the command block.
3. DCM copies itself to bank 0 and reads **BLT** (track 0, sector 2) into its sector buffer
   at 1380h.
4. **BLT** reads the BIOS (track 0, sectors 4–11) into bank 1. It puts the load address at
   1378h and the length at 137Ah (BIOS at `CPM.SZ − 600h`, 4A00h for 20 K). The Z80 halts.
5. SBD, which has waited for the halt since step 1, checks the status at 1377h, copies the
   BIOS from bank 1 to that address and jumps to the BIOS cold start.

The Release 2 DDBOOT source (`DDBOOT.asm`, `JMP DCM$BG`) confirms the hand-off from BIM to DCM
in step 2.

---

## Quirks worth carrying

- ⚠ **Release 1 is built for Rev B.** The p. 5 note says to change DDBIOS and reassemble it
  for a Rev C board. A Release 1 disk in a Rev C model reads the window switch bits as the halt
  bit.
- ⚠ **Rev B and Rev C status ports differ.** Rev B: D0 = window not present, D1 = halt (busy).
  Rev C: D0 = halt, D1–D3 = switches. A model must pick the revision.
- ⚠ **Format modules are code.** DCM jumps into the bytes that the host wrote to bank 1. A
  model of the DCM protocol cannot treat command 3 as "format track N". It must run the module
  (or recognize the two Jade modules).
- ⚠ **256-byte sectors exist only in DCM 1.3.** DCM 2.20 and Release 2A transfer 128-byte
  sectors only.
- ⚠ **The double-density format has no index mark** as printed. The handwritten note that adds
  one is marginalia, and the scan does not show which version was used.
- ⚠ **The label is `JADE DD `**, eight bytes with a trailing space. A disk without it logs on
  as IBM 3740.
