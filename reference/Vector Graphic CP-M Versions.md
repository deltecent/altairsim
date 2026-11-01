# Vector Graphic CP/M Versions — which BIOS fits which system

Source: [Vector System Numbers.pdf](#)

Vector Graphic *CP/M Information Sheet*, Rev 10/16/81, and a page on `MOVCPM`, 3 PDF pages. The sheet
lists each CP/M BIOS version with its size, systems, drives, controller, `MOVCPM` program and
Timeshare support.

**Reference only.** This sheet says which disks fit the Dual-Mode controller, and which system
numbers run Timeshare.

---

## 1. Quick reference for emulation

- **Versions H to T all use the Dualmode controller.**
- **On a Tandon system, drive A must be a double-sided Tandon** (H, I). Drives B, C and D can be
  either type.
- **Timeshare systems are the 5005 and the 5032,** with version N for a Timeshare upgrade.

---

## 2. Versions

| Version | Size | System | Drives | Controller | MOVCPM | Timeshare |
|---|---|---|---|---|---|---|
| A | 8K | SB, VIP | Micropolis | Micropolis | MOVCPM | No |
| B | 8.5K | 2800 | Qume 8" single density | Qume | MOVCPM/B | No |
| C | 8.75K | 2800 | Qume 8" double density | Qume | MOVCPM/C | No |
| C | 10K | 3030 | Winchester hard drive, Micropolis | Megastor | MOVCPMHD | No |
| D | 10K | 2800 | Qume 8" single or double density, Micropolis | Qume | MOVCPM/D | No |
| H | 8.75K | 2600, 3005, 3105, 3100, 3032, VIP-T | Tandon double or single sided, and Micropolis. Drive A must be Tandon double sided. B, C and D can be either | Dualmode | MOVCPMH | No |
| I | 9.5K | 5005, 5032 | As H | Dualmode | MOVCPMI | Yes |
| J | 9.5K | 3005, 3105 | Seagate hard drive as A and B. C, D and E Tandon single or double sided, or Micropolis | Dualmode | MOVCPMJ | No |
| K | 10K | 5005 | As J | Dualmode | MOVCPMK | Yes |
| L | 9.5K | 3005, 3105 | Seagate hard drive as A. B, C and D Tandon single or double sided, or Micropolis | Dualmode | MOVCPML | No |
| M | 10K | 5005 | Seagate hard drive as A. B, C, D and E Tandon single or double sided, or Micropolis | Dualmode | MOVCPMM | Yes |
| N | 9.5K | SB | Micropolis only, using the Dualmode controller. For Timeshare system upgrades | Dualmode | MOVCPMN | Yes |
| P | 10K | 3032 | 8" Quantum hard disk as A, B, C and D. Tandon floppy as E or F, either or both may be single sided. MOVCPM sets the configuration | Dualmode | MOVCPMP | No |
| Q | 10.5K | 5032 | As P | Dualmode | MOVCPMQ | Yes |
| R | 8.75K | SB | Micropolis only, using the Dualmode controller. For B5 upgrades | Dualmode | MOVCPMR | No |
| S | 9.5K | SB | Seagate hard drive as A, Micropolis floppies as B and C. For B5 upgrades | Dualmode | MOVCPMS | No |
| T | 9.5K | SB | Seagate hard drive as A and B, Micropolis floppies as C and D. For B5 upgrades | Dualmode | MOVCPMT | No |

---

## 3. MOVCPM (first page)

Type `MOVCPMX`, where X is the version. The program prints:

```
CONSTRUCTING 56K CP/M VERSION X.X
WHICH DRIVES SHOULD BE SINGLE SIDED
(C,D,E)
```

The drive letters depend on the version. Type the letters of the single-sided drives, with no
spaces. Press Return if you type fewer than three letters or none. The program then prints
`READY FOR "PUTSYS"`. Run `PUTSYS` to keep the change. Without `PUTSYS`, the configuration is
temporary.

---

## Quirks worth carrying

- **⚠ OASIS and the 5032.** The label of the OASIS 5.5A boot disk in hand is `SYS5032`. This sheet
  puts the 5032 on the Dualmode controller and gives it Timeshare. That fits a multi-user OASIS, but
  the sheet does not name OASIS.
- **⚠ Two letters C.** The sheet prints version C twice, for the 2800 and for the 3030.
- **⚠ The Dualmode runs Micropolis drives too.** Versions N, R, S and T are Micropolis on Dualmode.
  A Vector Graphic disk from a Dualmode machine is not always a Tandon disk.
- **⚠ `SB` is not a floppy-only name here.** Versions S and T add a Seagate hard drive to an SB.
  altairsim's `dualmode` board has no hard disk.
