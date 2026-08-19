# CP/M 3 on an S100Computers IDE-AB (CF) board

```
altairsim dualide.toml

->                       (the V2 Z80 board's MASTER V6.6 monitor)
P

Loading CPM from IDE/CF card... Done
CP/M V3.0 Loader
64K CP/M VERSION (Non banked)
A: & B: = IDE CF cards

A>DIR
```

John Monahan's **S100Computers "IDE-AB CF+ESP32" board** is a combination card: an **8255-based
IDE/CompactFlash controller** presenting CP/M drives `A:`/`B:`, plus a Dual SD ESP32 engine
presenting `C:`/`D:`. This example runs the **IDE/CompactFlash half** (`dualide`) on its own,
booting **CP/M 3** off a CF card. The Z80 drives an 8255 PPI at ports `30h`–`34h` that fans out
to a minimal ATA/IDE register file; whole 512-byte LBA sectors move as 256 16-bit words. See
`docs/boards/dualide.md` and `reference/dual-ide-card.md`.

For the **full A:/B:(CF) + C:/D:(SD)** system — both boards installed, all four drives — see
`examples/dualidesd/`. The IDE/CF half and the SD half read the **same** card image format: the
combination BIOS builds an identical DPB, LBA and byte order for each, so a card is portable
between them.

**The monitor is the boot command.** There is no `BOOT` verb. The **V2 Z80 CPU board** (`v2z80rom`)
carries the **MASTER V6.6** monitor in its paged EEPROM at `F000`–`FFFF`; `startup = ["RUN F000"]`
cold-starts it. At the monitor's `->` prompt, the `P` command reads the CP/M 3 cold loader (12
sectors from LBA 1 of the CF card) into `100h` and jumps into the standard five-stage CP/M 3 cold
boot.

`^E` (ATTN) takes the keyboard back to the simulator's monitor at any point; `RUN` resumes.

**Moving files to and from your host.** This machine fits the **host bridge** at ports `B0`/`B1`,
and the boot card carries its three utilities: `HDIR` lists your host directory, `R <file>` reads
a host file onto the CP/M drive, and `W <file>` writes a CP/M file back out. They are sandboxed to
the `hostdir` set in `dualide.toml` (empty = the directory you launched `altairsim` from). See
`docs/boards/hostbridge.md`. The machine also folds your terminal's Delete key to Backspace
(`[console] bsdel = "bs"`) so editing at the `->` monitor and the `A>` prompt just works.

## A card is an image plus a `.geo` sidecar

The socket mounts a raw card image (`cpm3-cf.img`) that has a sibling geometry descriptor of the
same base name (`cpm3-cf.geo`) declaring the card's true size:

```
sector_size 512
sectors     7806960
```

The sidecar declares the full **7806960-sector** (≈3.72 GB) CompactFlash card, but `cpm3-cf.img`
is only **~3 MB** — the CP/M 3 system, its directory, and its files. Every sector past the end of
the image reads back the **erased-card fill byte (`0xFF`)**, which is what an unwritten CF sector
returns and all CP/M ever sees out there (free space). That is why a card that is gigabytes on
real hardware ships here as a few megabytes, and why you can back up or swap a whole card as one
host file. (Mounting a `.img` with no `.geo` beside it is an error — a card image is meaningless
without its declared geometry.)

## The files

| File | What it is |
|---|---|
| `dualide.toml` | The machine: V2 Z80 CPU + MASTER EEPROM (`v2z80rom`), a `propio` console, the `dualide` board at `30h`–`34h` with the boot card on drive 0, a front panel, the host bridge at `B0`/`B1`, and 64K RAM. |
| `cpm3-cf.img` + `cpm3-cf.geo` | Drive `A:` — the boot card: the CP/M 3 system image and its geometry sidecar. |

**There is no undo.** The card is mounted read/write because that is what a real board is, and
CP/M writes to it. In a clone `git checkout` puts the image back; in a package you were handed,
nothing does. Copy it first if you are about to test writes in anger, or add `readonly = true` to
the drive in `dualide.toml`.

This CP/M 3 system image comes from the S100Computers "CP/M Card Images" collection (Image-1,
IDE-CF-AB non-banked CP/M 3; DR-supplied CP/M 3, hobbyist/educational). The trailing sectors of
the original card carried an unrelated leftover pattern that CP/M never reads; only the live
filesystem is kept here.
