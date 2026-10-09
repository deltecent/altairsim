# Cromemco RDOS 3.12 for the 64FDC — Technical Bulletin

Source: [Cromemco RDOS 03.12 for the 64FDC 023-9208 19850912.pdf](#) (Cromemco *Technical
Bulletin*, "RDOS 3.12 for the 64FDC", Part No. 023-9208, dated 12.09.85), 1 p. It has a text
layer, but the layer does not carry the column headers of the table; read the table as a page
image.

This bulletin is the only Cromemco document for the switches of a 64FDC that has the **RDOS
3.12** PROM. The 64FDC manual (023-2022, see
[Cromemco 4FDC / 16FDC / 64FDC](Cromemco%204FDC%2016FDC%2064FDC%20Floppy%20Controllers.md))
describes an earlier RDOS, and its switch table is not correct for 3.12. `builtin:rdos312` is
RDOS 3.12, so this file is the authority for the switch bits that the `64fdc` board gives on
port 04 IN.

---

## 1. What RDOS 3.12 changes

- RDOS 3.12 boots Cromix-Plus or UNIX from an **STDC hard disk** partition, or from a floppy
  disk. An STDC boot also needs STDC firmware 1.23.
- RDOS 3.12 does **not** boot from a WDI hard disk. That needs RDOS 3.08.
- "Except for the new switch settings documented below and the facility to boot STDC drives (and
  the inability to boot WDI drives), RDOS 3.12 is functionally equivalent to RDOS 3.08."

## 2. The 64FDC switch settings

"Switches 2 thru 5 are used to determine default boot devices":

| Boot device | S2 | S3 | S4 | S5 | Factory setting for |
|---|:---:|:---:|:---:|:---:|---|
| STD31 | OFF | OFF | OFF | OFF | UNIX systems |
| STD63 | OFF | OFF | ON | OFF | |
| STD0 | ON | OFF | OFF | ON | Cromix-Plus systems |
| STD1 | ON | OFF | ON | ON | |
| STD2 | ON | ON | OFF | ON | |
| STD3 | ON | ON | ON | ON | |
| FLOP A | OFF | OFF | OFF | ON | Stand-alone 64FDC boards |
| FLOP B | OFF | OFF | ON | ON | |
| FLOP C | OFF | ON | OFF | ON | |
| FLOP D | OFF | ON | ON | ON | |

The bulletin does not mention switch 1. In the 64FDC manual, switch 1 ON gives a console preset
to 300 baud, and the 3.12 PROM agrees (§3).

## 3. The same table in the PROM

The switches are on port 04 IN, and a 0 bit is a switch that is ON: D4 = S5, D3 = S1, D2 = S2,
D1 = S3, D0 = S4 (64FDC manual p. 33). RDOS 3.12 (`roms/RDOS312/rdos0312.lst`) reads them in
these places, and the code agrees with each row of the table:

| Address | Code | What it does |
|---|---|---|
| `CFAA`, `C4A1` | `IN A,(4)` / `AND 8` | S1. ON (0): set the console to 300 baud. OFF: find the baud rate from RETURNs |
| `C474` | `IN A,(4)` / `CPL` / `AND 17H` | S2–S5. Runs at an automatic boot and for the `B` command with no argument |

At `C474`:

- **S5 ON, S2 OFF:** boot the floppy drive in S3 and S4 (A = both OFF).
- **S5 ON, S2 ON:** boot STDC unit 0 to 3, from S3 and S4.
- **S5 OFF, S3 OFF:** boot STDC unit 31, or unit 63 with S4 ON.
- **S5 OFF, S3 ON:** RDOS prints `Switch settings not assigned`. The bulletin has no row for it.

RDOS 3.12 has no self-test that a switch starts. The 64FDC manual gives switch 5 that function,
for the earlier RDOS.

## 4. What the simulator does

The `64fdc` board gives S1 ON, S2 OFF and S5 ON always: a console at a preset baud rate and a
floppy boot. Its `boot_drive` property sets S3 and S4, the FLOP A to FLOP D rows. The STDC is not
simulated, so the other rows are not available.
