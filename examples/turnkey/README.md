# The MITS 8800bt -- an Altair with a Turnkey Module

The 8800b "turnkey" system had no front panel. One S-100 board -- the **Systems Turnkey
Module** -- carried the boot PROM, the terminal serial port, the sense switches, and an
Auto-Start circuit that booted the machine the moment you switched it on. These two files
are that machine, booting CP/M two ways.

```
cd examples/turnkey
altairsim floppy.toml     # -> 56K CP/M 2.2b off an 88-DCDD floppy,  A>
altairsim hdsk.toml       # -> 48K CP/M 2.2b off an 88-HDSK hard disk, A0>
```

Both are deltas on the built-in `turnkey` machine (`altairsim turnkey`), which is the
8800bt itself: the Turnkey Module, an 8080, an 88-DCDD floppy controller, the Host Bridge,
and **64K** of RAM. See `docs/boards/mits-turnkey.md` and `reference/MITS Turn Key Board.md`.

## What the Turnkey Module does that a front panel does not

- **It boots itself.** There is no front panel to toggle a bootstrap in from. `RUN 0000`
  starts the CPU at address 0, and the Auto-Start circuit **jams a `JMP` onto the bus** --
  `C3 00 hi`, where `hi` is the START ADDR switches -- so the first three fetches run the
  boot PROM. `floppy.toml` leaves the switches at FF00 (DBL, the floppy loader);
  `hdsk.toml` moves them to FC00 (HDBL, the hard-disk loader) and drops HDBL into the
  board's socket L1.
- **The boot PROM gets out of the way.** The PROM at FC00-FFFF *shadows* RAM for reads
  until the first `IN` from port FE/FF, then vanishes, so the machine has the full 64K of
  RAM. On a front-panel Altair the RAM stops at DFFF to leave room for the PROM; here it
  does not have to.
- **The console and the sense switches are on the same card.** The 6850 SIO is at 0x10
  (compatible with an 88-2SIO's Port A), and the sense switches answer port FF -- so this
  machine carries no separate `2sio` and no `fp`.

## The disk images are borrowed

`floppy.toml` boots the flagship CP/M floppy from `../cpm`, and `hdsk.toml` boots the
platter from `../hdsk`, rather than shipping third and fourth copies of two large images.
If you move this folder somewhere on its own, copy those images in beside it (or point the
`mount =` paths at wherever you put them). Both disks are mounted read/write; copy them
before testing writes in anger, or add `readonly = true` to the drive.
