# MITS 88-MDS — the Altair Minidisk System

**Status:** done (2026-07-13). Boots CP/M 2.2b off `CPM56K-1.DSK` through the MDBL PROM.

## The real hardware

The **Altair Minidisk System**, 1977: a 5.25″ hard-sector floppy subsystem, sold as the **88-MDS**
(and as the `680b-MDS` for the 680b, which is a different machine and not modelled here). The drive
holds **71,680 formatted data bytes** — a fourteenth of an 8″ Altair floppy — and MITS's pitch for it
was price, not capacity.

**It is two S-100 boards, not one.** Manual p4: *"Number of slots required in 8800 bus - 2."*
Controller Board #1 carries the port decode, the sector counter, the read circuit and the status
output; Controller Board #2 carries the control latch, the step logic, the motor/disk-enable timing
and the write circuits. A third board — a buffer/power-supply card, **not** on the S-100 bus — lives
in the drive cabinet and drives the 24-conductor cable. The only DIP switch in the whole system is
**SW-1 on the buffer board, and it selects the DRIVE ADDRESS**, not the port.

### It is not an 88-DCDD, and it is not an 88-DCDD with a smaller disk

This is the fact the whole card turns on, and this simulator got it wrong for months (see
*How we got here*, below). The 88-MDS is a **separate controller** that MITS deliberately gave the
88-DCDD's *programming model* — same three ports, same status bits, same inverted sense — so that
driver code would port. The **hardware underneath is different**:

| | 88-DCDD (8″) | 88-MDS (5.25″) |
|---|---|---|
| Rotation | 360 RPM | **300 RPM**, 200 ms/rev (p4) |
| Sector time | ~5.2 ms | **12.5 ms** (p4) |
| Data rate | 250,000 bit/s → 32 µs/byte | **125,000 bit/s → 64 µs/byte** (p4, p30, p31) |
| Sectors/track | 32 | **16** (p4, p33) |
| Tracks | 77 | **35** (p4) |
| Head load | a solenoid, commanded by bit 2 | **there is no head-load bit.** The head is loaded whenever the drive is enabled (p29, p31) |
| Head unload | bit 3 | **does not exist.** p32: *"D3 = Not used."* |
| Head current | bit 6 | **does not exist.** p32: *"D6 = Not used."* |
| Bit 2 (04h) | `cHDLOAD` | **`TIMER RESET`** — and nothing else (p32, p68) |
| Motor | always turning | **turns off after 6.4 s** of no access; **1 second** to spin back up (p31, p34, p58, p68) |
| Drives | up to 16, select bits 3..0 | **4**, select bits 1..0 (p29, p30) |
| Status inversion | 7405 open-collector inverters | **8T97 non-inverting buffers** — the *signals* are active-low instead. Same byte on the bus. |
| Boot PROM | DBL | **MDBL** (p5) — *but see below: DBL boots this CP/M too, and that is the thesis, not an accident* |

## Sources

| Source | Path | Authority |
|---|---|---|
| **88-MDS Minidisk Manual**, 110 pp | `reference/88-MDS Minidisk Manual.pdf` | **Everything.** Port map (p29), status bits (pp. 30–31), control bits (pp. 31–32), sector register (pp. 33–34, Table 3-B), geometry and timing (p4), the Disk Disable Timer (p68), the 1-second motor delay (p58, p69). **No text layer — read it as page images.** |
| The manual's **handwritten one-shot sheet** | same PDF, after p79 (unnumbered, signed "Shep") | **The timing table.** Every 74123's nominal pulse width with min/max — `SECTOR COUNT 30 µs (20/40)`, `READ CLEAR 500 µs`, `WRITE CLEAR 1.0 ms`, `STEP .3 ms`, `HEAD SETTLE 50 ms`, `DRIVE MOTOR ON DELAY 1 SEC (0.9/1.5)`. This is the single most valuable page in the manual and it is not in the table of contents. |
| **88-MDS Schematics**, 6 sheets | `reference/88-MDS Minidisk Schematics.pdf` | The structure, not the values — **the scan is too coarse to read most R/C values.** What it *does* settle: the port decode is hard-wired (`74L30`/`74L10` gates annotated *"= 1 WHEN ADDRESS = 010₈ / 011₈ / 012₈"*), the byte clock is divided down from the S-100 2 MHz clock (pin 49 → ÷2 → ÷8 → 125 kHz), the status buffers are non-inverting **8T97**, and the Disk Disable Timer is a **4020B ripple counter (IC B2)**, not a one-shot. |
| **MDBL PROM**, disassembled | `roms/MDBL/MDBL.ASM` (M. Eberhard, 2014) | The register map as *software* sees it, and the tiebreaker: `MDSTAT equ 08H ;Status input (active low)`, `TMRSET equ 04H ;Reset 6.4 sec disable timer`, `SECVAL equ 01h ;Sector Valid (1st 30 uS of sector pulse)`. |
| **CP/M 2.2b BIOS/BOOT** (Mike Douglas) | `disks/mits-88mds/cpm22/{BIOS,BOOT}.ASM` | The `MINIDSK` equates — `NUMTRK 35`, `NUMSEC 16`, `DATATRK 4`, `SECMASK 0Fh`, `BIOSLEN 1000h` — and the period software's own account of the card: *"5.25″ drives (which don't support 'ready' the same as 8″ drives)"*. |
| MITS technical sheet | `reference/Minidisk Info from MITS.pdf` | Marketing. Useful for the system description; **wrong on the timer** (see below). |

### Where the sources disagree, and who won

**The motor-off timer: 6.4 s.** Three numbers are in circulation and only one is derived:

- the MITS sheet says **five seconds**;
- the manual's own introduction (p3) and its test procedure (p73) say **six seconds**;
- the manual's engineering sections (p31, p32, **p68**) say **6.4 seconds**, and p68 says *why*:
  the timer *"is clocked every 12.5ms by the START OF SECTOR CLR pulse"* and the counter is a **4020**
  (14-stage CMOS ripple counter, confirmed as IC B2 on the schematic and by p73's *"removing IC B2 on
  Minidisk Board #2 (4020)"*).

**12.5 ms × 512 = 6.4 s exactly.** That is arithmetic off a divider tap, not a stopwatch reading, so it
beats both round numbers. `MDBL.ASM` and `FORMAT8M.ASM` — two independent pieces of period software —
both say 6.4 s as well. **We use 6.4 s.**

**The byte rate: 64 µs.** p45 says *"a new byte of Write Data is requested every 32 microseconds."*
**That is an error, and a revealing one** — 32 µs is the *88-DCDD's* rate. Someone wrote the minidisk
manual by editing the floppy manual and missed a number. p4, p30, p31, p33, p54, p60 and Figures 4-7
and 4-10 all say **64 µs**, and 125,000 bit/s ÷ 8 = 15,625 byte/s = 64 µs closes it arithmetically.

**Head settle: 50 ms.** p68 says the settle one-shot is 40 ms; Figure 4-13, the handwritten sheet, p31,
p32, p34 and p75 all say 50 ms. The 40 is a typo.

## Register reference

Three ports, **hard-wired** at octal 010/011/012 = **08 / 09 / 0A** (p29; and the schematic's decode
gates are annotated with those octal addresses).

| Addr | OUT (write) | IN (read) |
|---|---|---|
| 08 | Drive enable / disable | Status (**inverted**) |
| 09 | Drive control | Sector position |
| 0A | Write data | Read data |

### OUT 08 — drive enable (p29, p30)

| Bit | Meaning |
|---|---|
| D0–D1 | **Drive address, 0–3.** Only two bits — this card daisy-chains **four** drives, not sixteen. |
| D2–D6 | Not used |
| D7 | **1 = turn the Minidisk system off.** 0 = enable the addressed drive. |

> *"The Read/Write Head is loaded when the Drive is enabled."* (p29) — there is no separate head-load
> step, and no bit that could perform one.

### IN 08 — status (pp. 30–31). **True = 0.**

> *"When a bit is True, it is a logic Ø, when False, the bit is a logic 1. Also, **all status bits are
> logic 1 when there is not a Minidiskette in the Drive**."* (p30)

| Bit | Name | Meaning |
|---|---|---|
| D0 | `ENWD` | Write circuit wants a byte. **Every 64 µs** during a write. |
| D1 | `MH` | Move Head — stepping is allowed. **False for 50 ms after a step**, and false during a write. |
| D2 | `HS` | Head Status — head loaded **and motor speed stable**. *"Goes True one second after Disk Enable."* |
| D3 | — | *"Not used, always = 0 when Drive is enabled."* |
| D4 | — | *"Not used, always = 0 when Drive is enabled."* |
| D5 | `INTE` | Reflects the **CPU's** interrupt-enable state, not the card's. |
| D6 | `TRACK 0` | Head is on the outermost track. |
| D7 | `NRDA` | A byte is ready. **Every 64 µs** while reading. |

**D3 and D4 read ZERO on an enabled card** — they are not "true = 0" bits, they are tied low. A model
that computes `~status` from a set of true-sense flags must therefore *set* both before inverting, or
they come out as ones. (The 88-DCDD has the identical trap and solves it the identical way.)

### OUT 09 — control (pp. 31–32). **True = 1** — the command byte is *not* inverted.

The bits are **independent**, not an opcode; more than one arrives at a time and software does that.

| Bit | Name | Meaning |
|---|---|---|
| D0 (01h) | `STEP IN` | Step to a higher track. **Also resets the 6.4 s timer**, and drops `MH` + the sector channel for 50 ms. |
| D1 (02h) | `STEP OUT` | Step toward track 0. Same side effects. |
| D2 (04h) | **`TIMER RESET`** | Reset the 6.4 s Disk Disable Timer. *"This command should be issued before every Read or Write operation."* |
| D3 (08h) | — | **Not used.** (On the 88-DCDD this is head-unload.) |
| D4 (10h) | `INTERRUPT ENABLE` | Interrupt at the start of every sector (12.5 ms). |
| D5 (20h) | `INTERRUPT DISABLE` | |
| D6 (40h) | — | **Not used.** (On the 88-DCDD this is head-current.) |
| D7 (80h) | `WRITE ENABLE` | Arms the write circuit. **Self-clears at the end of the sector.** |

> **Both step bits at once = STEP OUT.** p67: *"if DØØ and DØ1 are both HIGH during an output to
> Channel Ø11, the STEP OUT direction will always be selected"* — the direction flip-flop is *cleared*,
> not toggled. The 88-DCDD applies both and nets to zero. Different card, different answer.

### IN 09 — sector position (pp. 33–34, Table 3-B)

```
 D7  D6  D5   D4  D3  D2  D1   D0
  1   1   0  [ sector 0..15 ]   ST      ST = Sector True, ACTIVE LOW
```

`ST` is true for **the first 30 µs of a sector** — *"the only time the Sector position may be checked
against the desired Sector position."* The count sits in D1–D4 so that one `RAR` drops `ST` into carry
and shifts the sector down in a single instruction; the period code does exactly that.

> **NOTE, p34:** *"The Sector position channel will be disabled (all "1"s) for **1 second after the
> Drive is enabled**, and **50 ms after a step command is issued**."*

### 0A — data

The slot is **137 bytes** (p32: *"The maximum number of bytes that are written is 137, including the
sync byte"*), and the image holds all of it — this is a **hard-sector** card, like the 88-DCDD. 128 of
those bytes are payload; 35 × 16 × 128 = **71,680**, the formatted capacity on p3.

## How it is simulated

`src/boards/mits-88mds.{h,cpp}`, on top of **`src/boards/mits-hardsector.{h,cpp}`** — the register
model this card genuinely shares with the 88-DCDD (decode, the inverted status byte, the sector
register, the 137-byte read/write path, the per-drive `Spindle` + `DiskImage`, the size probe, and
`mount`/`unmount`/`[[board.drive]]`). **The base class exists because MITS built the compatibility, not
because it was convenient**: `BOOT.ASM` declares the ports and every status/command bit *outside* its
`if MINIDSK` blocks, and only the geometry inside them.

What this card overrides:

| Hook | 88-MDS |
|---|---|
| `formats()` | one row: `minidisk`, 35 × 16 × 137 = **76,720** |
| `rpm()` | **300** |
| `byteUs()` | **64** |
| `readStartUs()` | **500** (the READ CLEAR one-shot) |
| `writeStartUs()` | **1000** (zeros are written for the first 1 ms; then `ENWD`) |
| `maxDrives()` / `selectMask()` | **4** / **0x03** |
| `command()` | its own — see the table above |
| `headLoaded()` | *"the head is always loaded when the Drive is enabled"* → **the motor being up to speed** |

- **Geometry**: hard-sector, `sectorSize = 137`, `startSector = 0`, probed from the image size.
  76,720 bytes is the only size it takes. The real images are **76,800** — XMODEM padded them up to a
  128-byte boundary, exactly as it did the 8″ disks — and `sizeMatches()` (`src/host/disk.h`) tolerates
  that. Note 76,720 is **not** a multiple of 128, so unlike the 8 MB format this one really does need
  the tolerance.
- **Interrupts**: `interrupt = none|int|vi0..vi7`, decoded but not wired by default — the sector
  interrupt is optional and *"not used for Minidisk BASIC"* (p4).
- **The motor**, below. Everything else is the base class.

### The motor, which is the whole personality of this card — and is **opt-in**

```toml
[[board]]
type  = "mds"
motor = "real"     # 1 s to spin up, and it stops after 6.4 s.  Default: "free"
```

**`motor = "free"` is the default: the motor is always at speed and never stops.** That is the same call
the Clock already made — `clock_hz = 0` free-runs and 2 MHz is what you *ask* for — and for the same
reason. The card models both timers exactly; it just does not make you live through them unless you say
so. (Patrick, 2026-07-13: the machine runs at full speed unless it is told otherwise.)

Under `motor = "real"`, three timers, all from the manual, all modelled on the `Clock` as *readings
taken off time* rather than counters something advances (the `Spindle` discipline, DESIGN.md §7.5.1):

1. **Spin-up: 1 second.** Enabling a drive that was off arms a 1-second delay. Until it expires, `HS` is
   false and **the sector channel reads 0xFF** (p34). The `DRIVE MOTOR ON DELAY` one-shot is fired by the
   Disk Enable flip-flop *toggling* (p58), so re-selecting an already-spinning drive does **not** re-arm
   it — otherwise every `OUT 8` in a BIOS loop would cost a second, and the period BIOS writes the select
   port on every single access.
2. **Motor-off: 6.4 seconds.** Reset by `TIMER RESET`, by either step bit, and by enabling the drive.
   When it expires the system turns off; the next select spins it up again, at the cost of another
   second. **This is why the period BIOS issues `cRESTMR` before every single access.**

CP/M boots and runs under **both** settings — `motor = "real"` was verified end to end, including sitting
at the prompt long enough for the 6.4 s timer to stop the drive and for the next command to spin it back up.

**Not** under the switch: **step settle, 50 ms.** After a step, `MH` reads false and the sector channel
reads 0xFF. That is step logic, not the motor — a status bit that is briefly false rather than a wall the
machine runs into — and MDBL polls for it by name before it steps again.

**An empty drive reads 0xFF** — *"all status bits are logic 1 when there is not a Minidiskette in the
Drive"* (p30), and p30 NOTE 3: *"If the Drive selected is not connected or its power is off, the
Controller will automatically turn off."*

### Reset

- `Reset::PowerOn` (POC*) and `Reset::Bus` (RESET*): deselect, stop the motor, flush any pending write,
  invalidate the buffered sector. **Images stay mounted and the head does not move** — a real drive does
  not re-home because the CPU was reset, and a warm reset that seeked every drive to track 0 would be
  inventing a convenience the hardware never had.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **Status reads inverted** (and D3/D4 are tied low, not "true = 0") | Nothing works, instantly — or, worse, D3/D4 float high and the BIOS reads a card that is never enabled. |
| **There is no head-load and no head-unload.** `OUT 9, 08h` is a **no-op**. | You model a solenoid this card does not have. See below — the manual's own sample code makes this mistake. |
| **Bit 2 is TIMER RESET, not head load** | The motor stops after 6.4 s mid-transfer and the disk "fails" for reasons nothing explains. |
| **The disk turns on its own** — the sector under the head is read off the Clock, never advanced by a port read | The platter spins at the speed of whatever loop is polling it, and a recorded session stops replaying identically. |
| **Sectors are numbered from 0** | Silent disk corruption (DESIGN.md §7.3). The Tarbell numbers from 1. |
| **1 s spin-up / 50 ms step settle gate the sector channel to 0xFF** | Software that could never have run on real hardware boots anyway — a too-forgiving card, which is the failure mode you never notice. |
| **Both step bits set ⇒ STEP OUT** | The head walks the wrong way, but only for software that sets both, so it survives every test you thought to write. |
| **Write enable self-clears at the end of the sector** | A write that overruns keeps writing into the next sector. |
| **A short write is padded with the LAST BYTE OUT, not zero** | System sectors are 133 bytes and never reach 137. Inherited from the base class, and the same on both cards. |

> ### The manual's own sample code has the bug this card exists to fix
>
> pp. 27 and 28 of the 88-MDS manual print a driver containing:
>
> ```
>         MVI     A,8             ; UNLOAD HEAD
>         OUT     9               ; SEND COMMAND
> ```
>
> — on the bit that **p32 of the same manual calls "Not used"**, for a head that **p31 of the same manual
> says is "always loaded when the Drive is enabled."** Someone at MITS wrote the minidisk driver by
> editing the 8″ floppy driver, and left the head-unload in. (p26 does it again: `MVI A,128 ; ENABLE
> WRITE WITHOUT SPECIAL CURRENT` — "special current" is the 88-DCDD's D6, which p32 also calls "Not used.")
>
> **This simulator made the identical mistake, in the identical direction, for the identical reason** —
> see below. The card is the authority; the driver that happens to run on it is not.

## Limitations and deliberate departures

- **`port` is settable, and on the real card it is not.** The schematic decodes 010/011/012 octal in
  copper. We expose `port` because the base class does and because a second controller in one backplane
  is a useful thing to be able to build; no period software would ever move it. (The manual, p51,
  mentions a jumper on A8–A10, which do not participate in an 8-bit I/O decode.)
- **The byte clock is modelled in microseconds, as an RC network would be — but on this card it is
  divided down from the S-100 2 MHz clock** (pin 49 → ÷2 → ÷8 → 125 kHz). On real hardware, a machine
  with a faster crystal would transfer faster. Ours does not: 64 µs is 64 µs at any clock. This matters
  to nobody at 2 MHz and would matter to anyone modelling a 4 MHz Altair honestly. The one-shots (30 µs,
  500 µs, 1 ms, 50 ms, 1 s) *are* RC and correctly do not scale.
- **The 6.4 s timer is modelled in wall time, not counted sector pulses.** They are the same thing at
  300 RPM (512 × 12.5 ms), and would diverge only if the spindle could run at another speed. It cannot.
- **`MOVE OK` is modelled only for the 50 ms step-settle window.** Head-load settling has no meaning
  here (there is no solenoid), and the drive's mechanical seek time beyond 50 ms is not modelled.
- **No MFM channel.** The card serves whole 137-byte slots out of the image; the sync bit, the bit
  encoding, the read data window and the read clock one-shots (6.1 µs / 2.0 µs on the handwritten sheet)
  are a data-separator that no software can observe through these three ports.
- **`DISK POWERED` is modelled as "is there an image"**, which is not the same wire. A real drive that
  was cabled but powered off would auto-disable the controller; we have no way to express "cabled but
  off", and no software cares.
- **The DRWT diagnostic PROM** (p5) is not in the tree. It is not needed to boot, and no image of it has
  surfaced.

## Verification

- **`tests/test_mds.cpp`** — the 76,720 probe and the XMODEM pad; the 35-track step clamp; a 16-sector
  spindle at 300 RPM; the 64 µs byte clock; `OUT 9, 08h` is a no-op; both step bits ⇒ step **out**;
  `TIMER RESET` pushes the motor-off deadline out; the motor stops after 6.4 s of silence and the sector
  channel goes to 0xFF; the 1 s spin-up and the 50 ms step-settle gate; an empty drive reads 0xFF.
- **`media = "minidisk"` on a `dcdd` is now an error** — pinned by a test, because that is the bug this
  card was built to remove and it must not come back by accident.
- **The acceptance test is the one that counts**: `tests/acceptance/minidisk.exp` boots
  `machines/minidisk.toml` to the CP/M prompt and reads both drives. The BIOS's banner is
  `db 'For Altair Mini Disk'` — that string lives in the `if MINIDSK` half of `BIOS.ASM`, so it is the
  *disk* telling us which controller it thinks it is talking to, and reaching it is simultaneous proof
  of the geometry, the byte rate, the rotation and the inert bit 3.
  - It must be an **expect** test, not a piped one: CP/M's `DIR` polls the console between directory
    entries and **aborts the listing if a key is waiting**, so a buffered keystroke file has its second
    command silently truncate its first command's output. You get one filename, and it looks exactly
    like a card that only read one sector.
  - It is **skipped, loudly, if the `.DSK` is not downloaded** — the images are not in git. A test that
    passes because its input is missing is worse than no test.
  - The `WILL_FAIL` control is `minidisk-dcdd.toml`: the same disk in an 88-DCDD. See above for the
    control I tried *first*, and what it taught me.

## How we got here — the bug this card is

Before this board existed, the minidisk was **one row in the 88-DCDD's format table and one `if`**:

```cpp
{"minidisk", 35, 16, 4, 76720},          // in dcddFormats()

if (v & 0x08) {                          // cHDUNLD
    if (d->fmt.sectors != 16) { ... }    // <-- the 8" card asking "am I a minidisk?"
}
```

That `if` is the whole error in one line: **the controller was inferring which controller it was from
the shape of the disk in the drive.** And because every timing constant on the card was the 8″ card's,
a minidisk mounted in it turned at **360 RPM instead of 300** and clocked bytes at **32 µs instead of
64**.

It never failed, because nothing in the tree ever mounted a minidisk image. That is the point:
**a wrong machine that boots is the expensive kind of wrong.**

### …and the compatibility that hid it is real, which the negative control had to teach me

The acceptance test needed a control — something that *must not* boot, or the test proves nothing. My
first attempt swapped the **PROM**: put DBL, the 8″ bootstrap, at `FF00` on a minidisk machine, on the
theory that it could not possibly read the medium.

**It booted.** CP/M came up and listed both drives.

Mike Douglas's own `BOOT.ASM` — the loader on the very disk being booted — says why, in its header:

> *"This code is loaded from sectors 0 and 2 into RAM by the disk boot loader PROM **(DBL)**."*

DBL interleaves 2:1, so it reads sector 0 and then sector 2, which is exactly where that loader lives;
and DBL's `cHDLOAD` (bit 2) arrives at an 88-MDS as `TIMER RESET`, harmlessly. The PROMs differ where
it does not matter here — MDBL relocates itself to `4C00` instead of `2C00`, because Minidisk BASIC
loads low and would land on top of it.

**A bootstrap written for one controller runs on the other, because MITS built this card to present
the other card's programming model.** That is not a footnote; it is the same fact that let a 5.25″
minidisk sit inside an 8″ controller for months without a single symptom. The compatibility is real,
it is deliberate, and it is exactly why the *hardware underneath* has to be modelled separately even
though the *registers* are identical.

So the control swaps the **card**, not the PROM (`tests/acceptance/minidisk-dcdd.toml`): the same disk
in an 88-DCDD, which now refuses at `MOUNT` — *"76800 bytes matches no dcdd format"* — instead of
guessing. That is the variable that actually matters, and it is the bug this card removed.

## References

- `reference/88-MDS Minidisk Manual.pdf` — and read the **unnumbered handwritten one-shot sheet** after p79.
- `reference/88-MDS Minidisk Schematics.pdf`
- `roms/MDBL/MDBL.ASM` — M. Eberhard's disassembly of the boot PROM. `docs/roms.md` for provenance.
- `disks/mits-88mds/cpm22/` — Mike Douglas's CP/M 2.2b for the minidisk, and where to download it.
- `docs/boards/mits-dcdd.md` — the card this one is *not*.
