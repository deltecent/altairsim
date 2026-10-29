# Jade Double D double-density floppy controller

**Status:** built (`jadedd`). The board runs its own Z80 against its own RAM, local ports and
FD1791, and the host talks to it through one port and a 1 K memory window, as on the real board.
`tests/test_jadedd.cpp` runs small Z80 programs on the board that read, write and step the way
Jade's DCM2 does. Nothing in the package boots a Jade disk yet: there is no DDBOOT PROM and no
machine file for this board.

## The real hardware

The Jade Computer Products Double D (IOD-1200, Rev C, 1980) is an **intelligent** S-100 floppy
controller. It carries a Z80A, 2 K of static RAM (four 2114s) and a WD FD1791-01. The host never
touches the FD1791. The host takes the board's RAM onto the S-100 bus through a 1 K window,
writes a command block (or a whole program) into it, gives the RAM back and interrupts the Z80.
The Z80 does the disk work and executes `HLT` when it is done. The host sees the halt in its
status port, or as a vectored interrupt if the board is strapped for one.

What the board Z80 runs is whatever the host loaded. Jade's DCM2 ("Disk Controller Module")
comes off the disk: DDBOOT, the host boot PROM, loads it from track 0 into the board.

The straps:

| Strap | Sets | Property |
|---|---|---|
| P0, P1 | the host port, 40h–43h. The Jade software uses 43h | `port` |
| M10–M12 | the window, a 1 K block at E000h–FC00h | `window` |
| PHAN\* (ECN 6) | the board pulls PHANTOM\* on a window read | `phantom` |
| INTERRUPT BLOCK | VI0\*–VI7\*, or nothing | `interrupt` |
| CLOCK SELECT | 8″ (Z80 4 MHz, FD1791 2 MHz) or 5″ (2 MHz, 1 MHz) | `size` |
| S1: U0, U1, TST\* | user switches and the test switch | none (see *Quirks*) |
| address mode (M–S / M–E) | a 16-bit or a 24-bit bus | none (16-bit only) |

## Sources

| Source | Path | Authority |
|---|---|---|
| Jade *Double D Hardware Manual*, IOD-1200M Rev C, 1980, with ECNs 1–10 | `reference/Jade Double D Disk Controller Rev C Manual.md` | The host port, the window, the Z80's local ports, the A3–A7 strobes, the wait, NMI\*, the timer and the step one-shot. |
| Jade CP/M 2.2 Release 2 source: `DCM2.ASM`, `DDBOOT.asm`, `DDBIOS.ASM` | the maintainer's `deltecent/jadedd-cpm22` repository | What the board's program does with each bit. **U0 = the FD1791 bus polarity** comes from DCM2 and DDBOOT, not the manual. The DCM2 command block at window + 0370h. |
| Jade firmware printouts, 1983 | `reference/Double-D_Firmware_1983.md` | The system track layout, the Release 1 boot chain, Rev B. |
| Fairchild DM74LS123 data sheet, April 2000 | `docs/sources.md` | The timer period: tW = K × RX × CX with K ≈ 0.37, for CX ≫ 1000 pF (p. 2, note 3). |
| The disk images in `deltecent/jadedd-cpm22` | (not in the tree) | The two disk layouts, from their sizes and the directory offset. |

The manual and the shipped software disagree in one place. The manual calls status bit 6 `ILP*`
(illegal pack). ECN 5B and DCM2 make it TWO SIDED\*. The ECN and the software won.

## Register reference

### The host side

| Port | OUT (write) | IN (read) |
|---|---|---|
| `port` (43h) | D0 `SLVRQ`: 1 = the host has the RAM. D1 `ZINT`: the Z80's interrupt, **and** the bank bit IA10 while the host has the RAM. D7: reset the Z80, which starts at 0000h. One write sets all three. | D0 `ZHLT*`: 0 = the Z80 has halted. D1–D3: switches M10–M12 (open = 1). D4–D7 are not driven and read 1. |

The window decodes only while `SLVRQ` is 1. `ZINT` picks which 1 K half of the 2 K RAM is in it.
The common commands are `03h` (take the RAM, bank 1), `01h` (take it, bank 0), `02h` (give it
back and interrupt the Z80), `80h` (give it back and reset the Z80) and `00h` (give it back).

### The board side (what the board Z80 sees)

The RAM decodes on A0–A10, so it repeats 32 times in the Z80's 64 K. The ports decode on A0–A2.

| Port | OUT | IN |
|---|---|---|
| 00 | Board Level Command, a 74LS273: D0–D1 drive (`DSA`, `DSB`), D2 `DSE` (select enable, and the NMI\* gate), D3 `SERO`, D4 `DDEN`, D5 `SID1` (side, and the step direction), D6–D7 precompensation | Board Level Status: D0 `U0`, D1 `U1`, D2 `TST*`, D3 `ZINT`, D4 `SERI`, D5 `TOFF`, D6 TWO SIDED\*, D7 `CHNG*` |
| 04–07 | the FD1791 command, track, sector and data registers, **inverted** | the FD1791 status, track, sector and data registers, **inverted** |

An `IN` from port 00 with A3–A7 set is a status read **and** a strobe. The bits add up, so one
`IN` can do several of these:

| Address bit | Port | Strobe |
|---|---|---|
| A3 | 08h | STEP\* to the selected drive, inward when `SID1` is 1 (only while `DSE` is 1) |
| A4 | 10h | clear the timer |
| A5 | 20h | clear `ZINT` |
| A6 | 40h | start the timer, or extend it if it runs |
| A7 | 80h | wait until the FD1791 raises DRQ or INTRQ |

The status byte of a strobe read is the one from before the strobes act (see *Limitations*).

## How it is simulated

- **A private machine.** `JadeDdBoard` holds a `CpuZ80`, a private `Bus` and a private `Clock`
  at the board's crystal (4 MHz or 2 MHz). The private bus holds two private boards, the RAM and
  the local I/O. The FD1791 is the shared `Wd1791` chip, timed on the private clock, with its
  density check on.
- **Lazy catch-up.** The board Z80 does not run on every host step. A host cycle that the board
  decodes (the port or the window) first runs the board Z80 up to the host's present, converted
  to board T-states: board T = host T × board hz / host `hz()`. The remainder carries from one
  call to the next. A `SET clock_hz` between two calls splits the interval at
  `Clock::rateChangedAt()`. Nothing the host can see changes between two of its cycles, so no
  event is lost.
- **Halted costs nothing.** A halted board Z80, or one stopped because the host has the RAM, does
  not step. The private clock jumps to the next FD1791 edge or to the target.
- **The VI deadline.** Only while `interrupt` is strapped and the board Z80 runs, a host deadline
  catches up each millisecond of host time, so the halt reaches the VI line without the host
  asking. A board that is idle, or not strapped, queues nothing on the host clock, so a host
  `HLT` still ends the run (`Clock::queued()`).
- **NMI\*** is FD1791 INTRQ AND `DSE`. **INT\*** is `ZINT`. DCM2 runs in IM 1 with its NMI handler
  at 0066h, which ends a sector transfer loop with `EX (SP),IY` and `RETN`.
- **The wait strobe** is `holdReady(fdc.holdUntilReady(...))` on the private bus. The board Z80 is
  charged the wait and skips to the next DRQ or INTRQ.
- **Media.** Four `DiskImageDrive`s, soft-sectored, 128-byte sectors starting at 1, one side.
  The board knows the layout from the image size:

  | Image size | Layout |
  |---|---|
  | 489,728 | a double-density data disk: track 0 SD 26 × 128, tracks 1–76 DD 50 × 128 |
  | 259,328 | a single-density data disk: track 0 SD 26, track 1 DD 50 (the system track), tracks 2–76 SD 26 |
  | 256,256 | an IBM 3740 disk, 77 tracks SD 26 × 128 |
  | smaller | blank, until the guest formats it |
  | anything else | refused, and the old disk stays in |

  Track 1 is double density on both Jade layouts. It holds the CCP and BDOS. The images use a
  linear sector order: the directory is at byte 9728, the start of track 2, as the DPB's
  `OFF = 2` says.
- **No interrupt to the host but VI.** The board has no pINT strap, so `interrupt` takes `none`
  and `vi0`–`vi7`.
- **`SHOW`** prints the board Z80's PC, whether it has halted, and who holds the RAM.

### Reset

- `Reset::PowerOn`: the RAM is cleared, the board Z80 is reset to 0000h, the FD1791 powers up,
  every head goes to track 0 and the timer is cleared. After that, the bus reset below acts.
- `Reset::Bus` (BR\*): both 74LS74s of Fig 2-4 are cleared, so `SLVRQ` and `ZINT` are both 1.
  The host has the RAM, with bank 1 in the window. The command latch is cleared, which deselects
  every drive. The manual does not show BR\* reaching the Z80's RESET\*, and the Z80 is stopped
  on BUSRQ\* until the host gives the RAM back, so a bus reset leaves the Z80 and the timer
  alone. DDBOOT resets the Z80 with `OUT 80h`.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **The FD1791 bus is inverted, and the board does not undo it.** Every FD1791 register byte passes through complemented, both ways. U0 reads closed (0), which tells DCM2 and DDBOOT to XOR each FD1791 byte with FFh. | With a true bus, or U0 open, the FD1791 gets the complement of each DCM2 command, and no command works. |
| **Each host write is a whole command.** D0, D1 and D7 act from one `OUT`. `02h` gives the RAM back and interrupts the Z80. `80h` gives it back and resets it. | Applying them one at a time leaves a pending interrupt, or a Z80 that never starts. |
| **`ZINT` has two jobs.** It is the Z80's interrupt, and the window's bank bit while the host has the RAM. Reset sets it, so bank 1 is in the window. | The host writes the command block to the wrong half of the RAM. |
| **A read of port 00 with A3–A7 set is also a strobe.** DCM2 clears `ZINT`, starts the timer, steps and waits for DRQ this way, and never with an `OUT`. | A model that treats them as plain status reads never steps a head or clears `ZINT`, and its sector loop reads bytes before the FD1791 has them. |
| **NMI\* is gated by `DSE`.** A command left in the FD1791 with no drive selected does not interrupt the Z80. | The board Z80 gets an NMI that the real board never gave it. |
| **STEP\* moves the head only while `DSE` is 1.** The program, not the FD1791, steps the head, and `SID1` is the direction. | A step with no drive selected moves a head that the real board left alone. |
| **Track 1 is double density on every Jade disk**, including the single-density layout. The density check is on, so a read at the wrong density ends in Record Not Found. | A system track that reads at either density hides a `DDEN` bug in the program. |
| **The timer runs on the board clock**, because it times the board Z80's own disk work. 0.37 × 240 kΩ × 47 µF = 4.1736 s: 16,694,400 board T-states at 4 MHz and 8,347,200 at 2 MHz. `TOFF` reads 1 once it has run out. | A timer on the host clock depends on `clock_hz`, which the board does not see. |

## Limitations and deliberate departures

- **No boot.** The package has no DDBOOT PROM and no `jadedd` machine. A guest program must load
  the board's program into the window itself.
- **The EIA port is not connected.** A write to `SERO` does nothing, and `SERI` reads 1 (an idle,
  marking line). DCM2's list command bit-bangs a character out through `SERO`, and its list
  status reads `SERI` as ready, so list output from DCM2 is lost without an error.
- **No two-sided disks.** No two-sided layout is in the size table, so TWO SIDED\* always reads 1
  (one side). DCM2 selects side 1 only when TWO SIDED\* reads 0. A program that selects side 1
  anyway gets Record Not Found.
- **`CHNG*` always reads 1** (no change). The board has no disk-change sense, and DCM2 never reads
  the bit.
- **`U1` and `TST*` read open (1).** The test switch's drive-0 motor and head-load action is not
  modelled.
- **MOTOR ON\* is not modelled.** On a 5″ board the timer drives the drive motors. Here a mounted
  drive is always ready, whatever the timer says. DCM2 still reads `TOFF` and waits its motor
  delay.
- **Precompensation** (D6–D7 of the command latch) is stored and has no effect. The image holds
  bytes, not flux.
- **The 24-bit address mode** (IC 4B, jumper M–E) is not modelled. The window decodes on the
  16-bit address.
- **Rev B boards** are not modelled. Rev B has the halt bit on D1 of the host status, and
  Release 1 software (in the 1983 printouts) is built for it.
- **The debugger does not reach the board Z80.** `BREAK`, `STEP`, `REGS` and `DISASM` work on the
  host processor only. `SHOW` gives the board Z80's PC and its halted state.
- **A strobe read returns the status from before the strobe.** The manual does not say which
  value the cycle reads. DCM2 never tests the status of a strobe read, so it cannot tell.
- **Undriven host status bits D4–D7 read 1.** The manual's Fig 2-5 does not show them. This is
  the reading of a 74LS244 that drives only D0–D3 on a bus with pull-ups.

## Verification

`tests/test_jadedd.cpp` drives real host bus cycles and runs hand-assembled Z80 programs on the
board, loaded through the window:

- reset, the window and its bank bit, the switch bits in the host status;
- `OUT 80h` runs a program to `HLT`, `OUT 02h` interrupts it in IM 1, and `IN 20h` clears `ZINT`;
- board time against host time, at 2:1 and after `SET clock_hz`;
- a halted board queues nothing, and the VI strap catches up by itself;
- the inverted FD1791 bus, both ways;
- a sector read and a sector write as DCM2's `RD.SEC` and `WR.SEC` do them: an `IN 80h` loop
  that NMI ends through 0066h, `EX (SP),IY` and `RETN`;
- the `DSE` gate on NMI\*, STEP and its direction, `DDEN` on both layouts, mounting each size;
- the timer, TWO SIDED\* and `CHNG*`;
- a snapshot taken in the middle of a sector read, which finishes after the restore.

Each of these mutations fails at least one check: NMI\* not gated by `DSE`, reads not inverted,
writes not inverted, STEP not gated by `DSE`, `IN 10h` not clearing the timer, no wait on
`IN 80h`, the wrong reset state of `ZINT`, no split at a rate change, `IN 20h` not clearing
`ZINT`, no VI update at a halt, and a restore that drops the timer.

## References

- `reference/Jade Double D Disk Controller Rev C Manual.md`
- `reference/Double-D_Firmware_1983.md`
- `docs/sources.md` (the manual, the firmware printouts and the DM74LS123 data sheet)
