# CP/M 3 on the S100Computers IDE-AB CF + Dual SD combination board

```
altairsim dualidesd.toml

->                       (the V2 Z80 board's MASTER V6.6 monitor)
P

Loading CPM from IDE/CF card... Done
CP/M V3.0 Loader
64K CP/M VERSION (Non banked)
A: & B: = IDE CF cards (61 Sec/Track)
C: & D: = SD cards (61 Sec/Track)

A>DIR C:
```

John Monahan's **S100Computers "IDE-AB CF+ESP32" board** is a combination card: an **8255-based
IDE/CompactFlash controller** presenting CP/M drives `A:`/`B:`, plus a **Dual SD ESP32 engine**
presenting `C:`/`D:`. This example runs the **whole board** — both halves, all four drives —
booting **CP/M 3** off a CF card with the S100Computers `HIDE3` combination BIOS. One **V2 Z80 CPU
board** drives the 8255 IDE register file at ports `30h`–`34h` (the `dualide` board) and the SD
command engine at ports `80h`/`81h` (the `dualsd` board). See `docs/boards/dualide.md`,
`reference/dual-ide-card.md`, and `reference/dual-sd-card.md`.

For each half **on its own**, see `examples/dualide/` (CF only) and `examples/dualsd/` (SD only).

## One image format, four drives

The IDE/CF and SD halves build an **identical** disk parameter block, LBA and byte order, so a card
image is **portable between them** — the same bytes boot as `A:` on the CF half and read back as
`C:` on the SD half. That is the point this example makes concrete:

| Drive | Board | Socket | Image | What it is |
|---|---|---|---|---|
| `A:` | `dualide` (CF) | drive 0 | `cpm3-cf.img` | the CP/M 3 boot system on CompactFlash |
| `B:` | `dualide` (CF) | drive 1 | `cf-blank.img` | a blank spare CF card (`DIR B:` → `No File`) |
| `C:` | `dualsd` (SD) | socket 0 | `cpm3-sd.img` | **a byte copy of `A:`**, on microSD |
| `D:` | `dualsd` (SD) | socket 1 | `sd-blank.img` | a blank spare microSD card (`DIR D:` → `No File`) |

`cpm3-cf.img` and `cpm3-sd.img` are **the same file** — copy a card between the CF and SD halves and
it just works, because the BIOS treats both the same way.

**The monitor is the boot command.** There is no `BOOT` verb. The **V2 Z80 CPU board** (`v2z80rom`)
carries the **MASTER V6.6** monitor in its paged EEPROM at `F000`–`FFFF`; `startup = ["RUN F000"]`
cold-starts it. At the monitor's `->` prompt, the `P` command reads the CP/M 3 cold loader (12
sectors from LBA 1 of CF drive 0) into `100h` and jumps into the CP/M 3 cold boot. The `I` command
boots the **SD** half as `A:` instead. The combination BIOS logs the SD drives **lazily**: `DIR C:`
and `DIR D:` initialize their sockets on first access, gated by the card-detect lines.

`^E` (STOP) takes the keyboard back to the simulator's monitor at any point; `RUN` resumes.

**Moving files to and from your host.** This machine fits the **host bridge** at ports `B0`/`B1`,
and the boot card carries its three utilities: `HDIR` lists your host directory, `R <file>` reads a
host file onto the CP/M drive, and `W <file>` writes a CP/M file back out. They are sandboxed to the
`hostdir` set in `dualidesd.toml` (empty = the directory you launched `altairsim` from). See
`docs/boards/hostbridge.md`. The machine also folds your terminal's Delete key to Backspace
(`[console] bsdel = "bs"`) so editing at the `->` monitor and the `A>` prompt just works.

## A card is an image plus a `.geo` sidecar

Each socket mounts a raw card image (`*.img`) that has a sibling geometry descriptor of the same
base name (`*.geo`) declaring the card's true size:

```
sector_size 512
sectors     7806960
```

The sidecar declares the full card (a ≈3.72 GB CompactFlash, a ≈394 MB microSD), but the `.img` is
only **~3 MB** — the CP/M 3 system, its directory, and its files. Every sector past the end of the
image reads back the **erased-card fill byte (`0xFF`)**, which is what an unwritten sector returns
and all CP/M ever sees out there (free space). That is why a card that is gigabytes on real hardware
ships here as a few megabytes, and why you can back up or swap a whole card as one host file.
(Mounting a `.img` with no `.geo` beside it is an error — a card image is meaningless without its
declared geometry.)

## The files

| File | What it is |
|---|---|
| `dualidesd.toml` | The machine: V2 Z80 CPU + MASTER EEPROM (`v2z80rom`), a `propio` console, the `dualide` board at `30h`–`34h` (drives A:/B:), the `dualsd` board at `80h`/`81h` (drives C:/D:), a front panel, the host bridge at `B0`/`B1`, and 64K RAM. |
| `cpm3-cf.img` + `.geo` | Drive `A:` — the boot card (CompactFlash). |
| `cf-blank.img` + `.geo` | Drive `B:` — a blank spare CompactFlash card. |
| `cpm3-sd.img` + `.geo` | Drive `C:` — a portable copy of the system (microSD). |
| `sd-blank.img` + `.geo` | Drive `D:` — a blank spare microSD card. |

**There is no undo.** The cards are mounted read/write because that is what a real board is, and
CP/M writes to them. In a clone `git checkout` puts the images back; in a package you were handed,
nothing does. Copy them first if you are about to test writes in anger, or add `readonly = true` to
a drive in `dualidesd.toml`.

This CP/M 3 system image comes from the S100Computers "CP/M Card Images" collection (Image-14,
IDE-CF + Dual SD non-banked CP/M 3; DR-supplied CP/M 3, hobbyist/educational). Only the live
filesystem is kept here; the trailing sectors of the original card carried unrelated leftover data
that CP/M never reads. The card's cosmetic cold-boot `PROFILE.SUB` (which only ran `SETDEF`) was
removed so the system boots straight to `A>`.
