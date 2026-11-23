# MITS 88-HDSK — the Altair "Datakeeper" hard disk

**Status:** done (2026-07-23). Boots CP/M 2.2 (`HDCPM22v16-48K.DSK`) to `A>` through the
HDBL PROM at `FC00`.

## The real hardware

The **MITS 88-HDSK "Datakeeper"**, 1977: a **Pertec D3422** hard-disk subsystem — one fixed
platter (~5 MB) and one top-load removable cartridge (~5 MB) — behind an *outboard controller*
with its own 5-slot bus, power supply, and an **8X300 bipolar microprocessor** running TTL-ROM
firmware. The controller talks to the Altair over an **88-4PIO** parallel card and to the drive
over a 24-in/24-out cable. It is a "controller does the work" card: it moves whole **256-byte
sectors** for you, into and out of **four internal page buffers**, over a command/handshake
protocol — the Altair never shifts a bit off the medium itself.

The full distilled hardware reference is `reference/88-HDSK.md` (geometry, the A0–A7 port map,
the seven-command protocol, the error byte, the IV-byte registers, the on-disk sector format).

### It is not a HardSectorFdc

The 88-DCDD and 88-MDS are `HardSectorFdc`: raw cards where software steps the head, watches the
sector counter turn, and shifts 137-byte slots through a data port in real time. The Datakeeper
is the opposite, so `HdskBoard` inherits `Board` directly (like the Tarbell would, or the
`hostbridge`). What it *reuses* is only the image layer (`DiskImage`/`MediaFile`) and the
`[[board.drive]]` machinery — there is no register model shared with the floppy cards.

## The programming model

Eight consecutive ports (default base **`A0h`** = 240 octal), all status in **bit 7**:

| Port | Name | Dir | Meaning |
|---|---|---|---|
| A0 | CREADY | in | controller ready for a new command |
| A1 | CSTAT | in | error/status flags — **reading it resets CREADY** |
| A2 | ACSTA | in | command acknowledged |
| A3 | ACMD | out | command **HIGH** byte — writing it **initiates** the command; `in` clears the ack |
| A4 | CDSTA | in | a read byte is ready at CDATA |
| A5 | CDATA | in | read-buffer / read-status data |
| A6 | ADSTA | in | the write port will take a byte |
| A7 | ADATA | out | command **LOW** byte / write-buffer data |

The **Dir** column is the *functional* direction the driver uses each port — it is not what
the address decoder gates. The host interface is an 88-4PIO (Motorola 6820 PIA), which
answers both a read and a write at every one of its eight register addresses; init even
writes control/DDR to the "in" status ports and the protocol reads the "out" command ports
to clear handshakes. So `SHOW BUS IO` correctly shows **IN OUT** on all eight — this is
faithful, not a decode that is too wide.

A command is a 16-bit word: low byte to A7, then high byte to A3 (which starts it). Opcode is
bits 15:12. The seven commands: **Seek** (0), **Write Sector** (2), **Read Sector** (3),
**Write Buffer** (4), **Read Buffer** (5), **Read Status** (6), **Set Byte** (8). Read/Write
Sector move a disk sector to/from one of the four buffers; Read/Write Buffer stream a buffer
to/from the Altair through A5/A7. The boot is Seek → Read Sector into a buffer → Read Buffer out
to memory, repeated per page.

### The head/side bit layout — HDBL wins over the manual

For Read/Write Sector the **low byte is `platter(7:6) | side(5) | sector(4:0)`**, and the high
byte carries `unit(11:10) | buffer(9:8)`. This is the layout HDBL and the deramp CP/M BIOS
actually put on the wire (`roms/HDBL/HDBL.ASM`, `CSIDE=20h`, `CFPLTR=C0h`) — and it is what the
bootable image is built for. It **contradicts** the prose in `reference/88-HDSK.md` §7, which
places the head number in bits 7:5. The prose is inconsistent with §5's bit diagram and with the
errata's corrected assembly routines; we implement the HDBL layout, and the acceptance boot is
the proof. (Diagnostic software that used the §7 "head×32" form would need a strap; nothing we
run does, so there is no strap.)

### Timing — synchronous, on purpose

No real latency is modelled. A command executes the moment its high byte is written and asserts
CREADY at once. That is period-faithful: the errata (`reference/88-HDSK.md` §4) says the firmware
sets the ready bits ~1–4.5 µs after the strobe, *"fast enough that a driver need not spin-wait"* —
so every guest poll loop terminates in a single pass, and the byte streams are paced by the
data-ready bits (A4/A6) instead. There is nothing on the `Clock`, so a snapshot has no deadline
to re-arm.

## Geometry and the image

The image is **one Pertec platter**: 406 cylinders × 2 sides × 24 sectors × 256 bytes =
**4,988,928 bytes**, linear `offset = (cyl·48 + side·24 + sector)·256`. That is exactly a
`DiskImage` built with:

```cpp
img->init(406, 2, /*interleaved=*/true);              // slot index = cyl*2 + side
img->initFormat(0, 405, 0, 1, Density::SD, 24, 256, /*startSector=*/0);
```

so the controller's `(cylinder, side, sector)` maps 1:1 onto `readSector(t, h, s)` and lands at
the right offset with no arithmetic in the board. A **logical drive is one platter**
(`reference/88-HDSK.md` §3); a physical unit carries up to two, so the mount slot is
`unit·2 + platter`. The bootable CP/M image is one platter in `drive0` (unit 0, platter 0). The
`drives` property grows the slot table up to 8 (four units × two platters).

## Boot

`builtin:hdbl` — Martin Eberhard's HDBL v2.00 at `FC00` (`roms/HDBL/`) — reads the disk's Pack
Descriptor Page (cyl 0 / side 0 / sector 0), which names the boot pages, loads them from `0000h`,
and jumps in. Sense switch **A11** picks the platter: down (the default `sense = 0x00`) is the
removable cartridge, which is the shipped image. The console is the base machine's 88-2SIO at
`10h`, which is where HDBL and the CP/M BIOS expect it. `tests/media/hdsk/hdsk.toml` is `base =
"default"` plus the controller, the PROM region, and the platter — three additions and a
`startup = ["RUN FC00"]`.

## Bad sectors: `FAULT` and the `faults` key

A drive can have bad sectors on its platter, to test the retry code of a guest (#972). The
faults are a table in each drive slot (`DiskFaults`, `src/host/diskfaults.h`), and the image
does not change. Give them with `FAULT hd0:driveN`, with the `disk_fault_*` MCP tools, or with
the `faults` key of a `[[board.drive]]` table. The *User Manual* gives the syntax, in "Simulate
a bad sector" in the Disks chapter.

The controller has its own error byte, CSTAT at port A1 (`reference/88-HDSK.md` §6). A fault
sets the bit that the controller sets for that error. The CP/M BIOS reads CSTAT and masks bit 7,
write protect, so each of these bits is an error to it.

| Kind | Read Sector | Write Sector |
|---|---|---|
| `data` | The data goes into the buffer. CSTAT bit 2 (`04`), data CRC error. | No effect |
| `id` | The buffer does not change. CSTAT bit 3 (`08`), header CRC error. | The sector is not written. CSTAT bit 3. |
| `missing` | The buffer does not change. CSTAT bit 4 (`10`), the header has the wrong sector. | The sector is not written. CSTAT bit 4. |
| `write` | No effect | The sector is not written. CSTAT is 0, so the guest is not told. |

- **The address** is `<cylinder>/<side>/<sector>`: cylinder 0 to 405, side 0 or 1 of the
  platter, and sector 0 to 23. One drive slot is one platter.
- **A sector gets one kind on an access,** in the order `missing`, `id`, then `data` or `write`.
- **A hit counts at the Read Sector or Write Sector command.** Seek uses no fault.
- **A drive that is not ready, an illegal sector and a write to a write-protected platter give
  their own error first.** They use no fault.
- **HDBL reads the Pack Descriptor Page, `0/0/0`, one time for each boot.** A bad read prints
  `LOAD ERR`, and HDBL stops in a loop at `BFD9`. A `count=2` fault there makes two boots fail.
  The third boot reaches `A0>`.
- **The CP/M BIOS reads a sector one time.** On `HDCPM22v16-48K.DSK`, 48K CP/M 2.2b v1.6, the
  directory starts at `1/0/0`. A hard `data`, `id` or `missing` fault there gives 1 hit, then
  `Bdos Err On A: Bad Sector`.
- **A `write` fault loses the write.** `SAVE 1 NEW.COM` writes the directory sectors `1/0/4`,
  `1/0/11`, `1/0/18` and `1/0/1`. With `write` faults on them, `SAVE` gives no error. After a
  warm boot, `NEW.COM` is not in the directory.

`SET hd0 DEBUG=fault` writes a line each time that a fault is used, for example
`fault drive0 1/0/0 data hit 1`. `MOUNT` and `UNMOUNT` clear the faults of the drive.
`CONFIG SAVE` writes the `faults` key. A snapshot keeps the faults with their counts.

## Sources

| Source | Where | Authority |
|---|---|---|
| 88-HDSK Datakeeper manual + errata | `reference/88-HDSK.md` (distilled from deramp.com) | the controller protocol, geometry, error byte |
| `HDBL.ASM` (Eberhard v2.00) | `roms/HDBL/` | the boot loader that drives this card |
| `BOOT.ASM` / `BIOS.ASM` (Mike Douglas) | deramp.com hard-disk CP/M | the (cyl,side,sector) wire layout and the image build |

## Tests

- `tests/test_hdsk.cpp` — the probe, the `(cyl,side,sector)→offset` mapping pinned three ways,
  the Seek→Read Sector→Read Buffer boot sequence, the Write Buffer→Write Sector→read-back round
  trip, the power-on `0xFF` status quirk, the error flags, one-pass handshake termination, and
  each fault kind on Read Sector and Write Sector, with its CSTAT bit and its count.
- `tests/acceptance/hdsk-fault.cmake` — HDBL past a `count=2` fault on `0/0/0`, a hard `data`,
  `id` and `missing` fault on the directory, and `write` faults that lose the directory writes
  of `SAVE`.
- `tests/acceptance/media.cmake` — boots `tests/media/hdsk` to `A>` and reads a directory entry
  off the platter, from the fixture's own directory and by path.
