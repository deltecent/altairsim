# Disks

A disk on an Altair is three things:

- a **controller** board on the bus
- some **drives** on that controller
- a **disk image** in one of those drives

The controller is a board, and it goes in the machine file. The drives are units of the
controller. The disk image goes in a drive, from the machine file or with `MOUNT` at the
monitor. "Put a disk in the machine file", below, shows the two forms together.

This chapter is about the image, the drive and `MOUNT`. `MOUNT` works in the same way for every
controller. The examples use the `dcdd`, the MITS 8″ hard-sector controller, because the CP/M
machine in the package boots from it. The `mds` is its 5¼″ relative, and it works in the same
way. The Boards chapter describes every controller.

## The controllers in this chapter

| Board | What it is | Drives | Ports |
|---|---|---|---|
| `dcdd` | **MITS 88-DCDD**: the 8″ hard-sector floppy controller. CP/M booted from it | 4 by default, up to 16 | 08, 09, 0A |
| `mds` | **88-MDS**: the 5¼″ minidisk. Smaller and slower | 4 | 08, 09, 0A |

**The two boards use the same ports**, so one machine cannot have both. The drives are units of
the board: `drive0`, `drive1` and so on.

## Put a disk in: `MOUNT`

```
MOUNT <id>[:<unit>] <file> [WP] [CREATE]
```

```
altairsim> MOUNT dsk0:drive1 games.dsk
```

This puts the image `games.dsk` in drive 1 of the controller `dsk0`. A relative path starts from
the machine's folder. The machines chapter gives the rule.

**The file must already exist.** A name that does not exist is a typing mistake, not a new disk.
The program tells you so, and does not make a disk that you did not want:

```
altairsim> MOUNT dsk0:drive1 my-scratch.dsk
dsk0: 'my-scratch.dsk': no such file
dsk0: to make a blank one, add CREATE: MOUNT dsk0:drive1 my-scratch.dsk CREATE
```

To make a new disk, see "Make a scratch disk" below.

To take the disk out, type `UNMOUNT`. The guest then sees an empty drive:

```
altairsim> UNMOUNT dsk0:drive1
dsk0:drive1: unmounted (the drive is now empty)
```

Board names are not case-sensitive. When the machine has only one controller, you can leave out
the number: `dsk` finds `dsk0`. You must always give the drive, because a controller has more
than one.

## Put a disk in the machine file

`MOUNT` at the monitor is for the disk that you use *now*. A disk that belongs to a machine goes
in the machine file. The examples in the package do this. This is a complete machine file:

```toml
[machine]
name    = "mine"
base    = "default"
startup = ["RUN FF00"]

[[board]]
id = "dsk0"                    # no type: the controller is already in the machine

  [[board.drive]]
  unit  = 0
  mount = "cpm22b23-56k.dsk"   # relative to this file

  [[board.drive]]
  unit     = 1
  mount    = "games.dsk"
  readonly = true
```

**Each `[[board.drive]]` is one `MOUNT` command.** The second one is
`MOUNT dsk0:drive1 games.dsk WP`:

| At the monitor | In the machine file |
|---|---|
| `dsk0` | `id = "dsk0"` on the `[[board]]` |
| `drive1` | `unit = 1` in a `[[board.drive]]` |
| `games.dsk` | `mount = "games.dsk"` |
| `WP` | `readonly = true` |
| `CREATE` | `create = true` |

All the drives of one controller go below one `[[board]]`. Start a new `[[board.drive]]` for
each drive.

**Do not write a `type` on that `[[board]]`.** The `default` machine already has the controller
`dsk0`. With a `type`, the file replaces the controller, and the settings that the base made on
it are lost. The configuring chapter gives the rule.

A drive can also have a `media` key, which `MOUNT` does not have. See "How the program finds the
format", below.

The file and the monitor do the same thing, at different times. The file puts the disk in the
drive when the machine starts. `MOUNT` changes it later.

The name has no folder, so the program looks for the file beside the machine file. For this
reason, you can copy the whole folder to another place, and it still boots.

`writeprotect = true` is the same as `readonly = true`. `CONFIG SAVE` saves `readonly`.

## Keep your disk safe

**Copy the folder before you experiment.** The program mounts a disk **read/write**, and every
write goes to the file on your computer at once. There is no undo. `SNAPSHOT` does not help,
because it saves the state of the machine, not the files on your computer. One wrong `ERA` can
destroy the example disk. The folder has everything that the machine needs, and the copy boots
from any place.

Read/write is the default because CP/M cannot save your work on a disk that it cannot write.

### Go back to the `A>` prompt before you stop

> **Do not `UNMOUNT`, copy or quit right after a file operation.**
> **Go back to the `A>` prompt first.**

The CP/M in this package has a **track-buffering BIOS**. When CP/M writes a sector, the BIOS
copies it into a buffer in memory that holds one whole track. Nothing goes to the disk yet. The
BIOS writes the buffer to the disk when it next reads the console. At the `A>` prompt, CP/M
reads the console, so the disk on your computer is then correct.

The BIOS works this way for speed. A track write needs one seek and one turn of the disk, not
thirty-two. When the machine waits for a person to type, it has time to spare. The simulator
does not cause this. The BIOS of a real machine did the same.

**Not every CP/M does this.** The author of each BIOS decided it. A *write-through* BIOS puts
every sector on the disk before it returns. You cannot tell from the `A>` prompt which kind you
have. On a write-through BIOS, the rule costs you nothing. On a buffered BIOS, it decides
whether your file is on the disk.

For example, `Ctrl-E` at the `A>` prompt is safe. `Ctrl-E` right after `ED` says that it wrote
your file is **not** safe. On the CP/M in this package, the file is then not on the disk.

### Write protection: `WP`

`WP` **write-protects the disk**, as the write-protect notch of a real diskette did. The guest
can read the disk. The controller refuses every write, and no write reaches the file on your
computer.

```
altairsim> MOUNT dsk0:drive2 golden-master.dsk WP
dsk0:drive2: mounted golden-master.dsk (write-protected)
```

In a machine file:

```toml
[[board]]
id = "dsk0"

  [[board.drive]]
  unit     = 2
  mount    = "golden-master.dsk"
  readonly = true
```

`RO` is also accepted, and it means the same. `RO` is the word for a ROM socket. For a disk, the
correct word is `WP`. `SHOW MOUNTS` marks every write-protected disk:

```
altairsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  dsk0:drive2  disk  golden-master.dsk  (write-protected)
```

**Use `WP` only for a disk that the guest reads.** The guest is not told that the disk is
protected, and it cannot be told:

- **The controller has no bit for it.** The status byte of the 88-DCDD has no bit that means
  *the notch is covered*. A program cannot tell a protected disk from an ordinary one.
- **CP/M has no error for it.** A CP/M 2.2 BIOS write returns `0` for success or `1` for an
  error. For this reason, a write to a `WP` disk shows as `Bdos Err On A: Bad Sector`.

The message `Bdos Err On A: R/O` comes from a different thing. It is CP/M's own read-only flag,
which `STAT A:=R/O` sets and a warm boot clears. The flag is in CP/M's memory, not on the disk.
A `WP` disk never causes it.

**Look for this line in `SHOW MOUNTS`.** If your computer does not let the program write the
file, because of its permissions, the disk still mounts, write-protected. The program tells you
when it mounts the disk, and `SHOW MOUNTS` says that you did not ask for it:

```
  dsk0:drive2  disk  master.dsk  (write-protected -- THE HOST WON'T LET US WRITE IT; you did not ask for this)
```

CP/M will fail every write to that disk, and you think that it is writable.

## Make a scratch disk

`CREATE` gives you a blank medium, and a format program in the guest makes it a disk.

1. At the monitor, make the file and put it in a drive:

   ```
   altairsim> MOUNT dsk0:drive1 my-scratch.dsk CREATE
   dsk0:drive1: created my-scratch.dsk (empty)
   dsk0:drive1: mounted my-scratch.dsk
   ```

   The file has zero bytes. It is an unformatted disk, and CP/M cannot read it yet.

2. Boot CP/M, and format the disk with **`AFORMAT`**, the Altair Disk Format Utility by Martin
   Eberhard. It is on the CP/M disk in this package:

   ```
   A>AFORMAT B:

   Put disk in B
   Ready (Y/N)?Y
   Formatting an 8" Disk............................................................................
   Format another (Y/N)?N
   Put CP/M disk in A:
   Ready (Y/N)?Y
   A>
   ```

   Each dot is a track. `AFORMAT` formats 77 tracks, so the file is now a 337,568-byte 8″
   floppy.

3. Type `DIR B:`. CP/M answers `No file`, and `B:` is ready for `PIP` and `SAVE`.

In a machine file, `create = true` does step 1:

```toml
[[board]]
id = "dsk0"

  [[board.drive]]
  unit   = 1
  mount  = "my-scratch.dsk"
  create = true
```

The program makes the file only when the file is not there. For this reason, the same machine
file loads the first time and every time after it, and the disk keeps its contents.

**`CREATE` works only on a controller that can grow its disk.** The 88-HDSK "Datakeeper" hard
disk has a fixed size, so it cannot start from an empty file. It refuses `CREATE`, and removes
the empty file:

```
h0: 88-HDSK has a fixed geometry and cannot make a blank platter -- supply an existing 4988928-byte image (406 cyl x 2 sides x 24 sectors x 256)
```

For that disk, you supply a full-size image.

## How the program finds the format

You do not tell the program what kind of disk you mounted. It **looks at the number of bytes in
the file**:

| Format | Tracks | Sectors | Bytes/sector | File size |
|---|---|---|---|---|
| `8in` | 77 | 32 | 137 | **337,568** |
| `minidisk` | 35 | 16 | 137 | **76,720** |
| `fdc8mb` | 2048 | 32 | 137 | **8,978,432** |

A file of 337,568 bytes is an 8″ floppy, and it cannot be anything else. The `8in` and `fdc8mb`
formats belong to the `dcdd`, and `minidisk` belongs to the `mds`. The program chooses only from
the formats of the board.

**A file can be a little bigger than its format.** XMODEM adds bytes to fill a 128-byte block,
and most images in circulation were sent by XMODEM at some time. The CP/M disk in this package
is 337,664 bytes, and the minidisk images that you find are 76,800 bytes. The program allows for
this.

**A file whose size matches no format mounts UNFORMATTED, at the largest format of the board**:
`fdc8mb` on a `dcdd`, and `minidisk` on an `mds`. A blank file from `CREATE` is one of these.
The file grows as the guest writes it.

**`media` sets the format yourself.** Use it on the drive in a machine file when the size cannot
decide. Examples are an image that is cut short, a format that you made up, or a blank disk for
a format program that formats every track that the drive can reach:

```toml
  [[board.drive]]
  unit   = 1
  mount  = "scratch.dsk"
  create = true
  media  = "8in"        # a blank 77-track floppy, not a blank 8 MB disk
```

`media` is a key in the machine file only. You cannot set it with `MOUNT`. **For every other
disk, leave `media` out.** A wrong `media` gives disk errors in the guest, and the
troubleshooting chapter tells you why.

The package has an 8″ floppy image, the CP/M disk. You supply any `minidisk` or `fdc8mb` image.

## How an 8 MB disk works

`fdc8mb` is a disk with 2048 tracks on a controller that MITS designed for 77 tracks. It works
with **the stock board and the stock PROM**. The controller steps the head and moves bytes, and
it never asks how big the disk is. The BIOS knows where track 1500 is.

For this reason, **the format belongs to each drive, not to the controller**. One board can have
drives of different sizes. The period 8 MB CP/M BIOSes expect an 8 MB disk on A: and B:, and
77-track floppies on C: and D:, so that you can `PIP` between them.

**You supply both images.** The lines below show the form of the commands:

```
altairsim> MOUNT dsk0:drive0 big.dsk
altairsim> MOUNT dsk0:drive2 floppy.dsk
altairsim> SHOW dsk0
```

In a machine file:

```toml
[[board]]
id = "dsk0"

  [[board.drive]]
  unit  = 0
  mount = "big.dsk"

  [[board.drive]]
  unit  = 2
  mount = "floppy.dsk"
```

## ImageDisk files: `.imd`

Much archived 8″ and 5¼″ software is in the **ImageDisk** format. A controller reads raw
sectors, so the program does not mount an `.imd` directly. When you `MOUNT` a file whose name
ends in `.imd`, the program converts it to a raw image beside it, with the name `.dsk`, and
mounts the `.dsk`:

```
altairsim> MOUNT dsk0:drive0 cpm.imd
dsk0: converted cpm.imd -> cpm.dsk
dsk0:   IMD: CP/M 2.2 system disk
dsk0:   337568 bytes
dsk0:drive0: mounted cpm.dsk
```

Read the comment line of the `.imd` in the output, and check that it is the disk that you
expected.

**The program never writes over a `.dsk` that is already there.** If `cpm.dsk` exists, the
program tells you to remove it or to mount it directly. For this reason, a second mount cannot
lose the changes that you made. After the conversion, the `.dsk` is the disk.

## A disk on a TNFS server

A disk image can be on a **TNFS server**, the network file system of the FujiNet project:

```
altairsim> MOUNT dsk0:drive0 tnfs://fileserver/cpm/games.dsk
altairsim> MOUNT dsk0:drive1 tnfs://fileserver:16384/scratch.dsk WP
```

The form is `tnfs://<host>[:<port>]/<path>`. The default port is **16384**.

In a machine file:

```toml
[[board]]
id = "dsk0"

  [[board.drive]]
  unit  = 0
  mount = "tnfs://fileserver/cpm/games.dsk"

  [[board.drive]]
  unit     = 1
  mount    = "tnfs://fileserver:16384/scratch.dsk"
  readonly = true
```

A `tnfs://` name does not start from the machine's folder, because it names its server.

The program gets the whole image **one time, when you mount it**. After that, the disk works
like a local disk. The program writes your changes back to the server when the guest flushes,
and when you `UNMOUNT` the disk. `WP` works as on a local disk. If the server does not let the
program write the file, the disk mounts write-protected, as a local file does.

**If the server stops while you use the disk, the program tells you.** It prints a line that
says that it keeps the changes in memory only. The guest continues to run, and the program
continues to try. When the server comes back, the program says so, and saves the changes. If you
quit before that, the changes are lost.

TNFS carries only **single-file images**: a floppy, a minidisk, an 8 MB `fdc8mb` disk and a
cassette tape. The card images of the CompactFlash and SD boards have a second file (`.img` with
`.geo`), and you cannot mount them over TNFS.

## Simulate a bad sector: `FAULT`

`FAULT` makes a sector of a mounted disk bad. Use it to test the retry code and the error
messages of a guest. The disk image does not change. The faults are in the drive, beside the
image.

These controllers take faults: `tarbell`, `tarbelldd`, `16fdc`, `64fdc`, `versafloppy`,
`jadedd`, `disk1`, `disk1a`, `dcdd` and `mds`. Another controller refuses the command:

```
altairsim> FAULT fd0:drive0 ADD 2/0/1 data
fd0:drive0: the mdsa board does not take faults on its disk
```

### Give a fault

A fault is a sector address, a kind and an optional count:

```
<track>/<head>/<sector> <kind> [count=N]
```

- The track, the head and the sector are decimal numbers. Use `*` for all tracks, all heads or
  all sectors.
- The sector is the number that the guest asks for. On most soft-sector controllers, the first
  sector of a track is 1. On the `dcdd` and the `mds`, the sector is the number that the guest
  reads from the sector position port: 0 to 31 on an 8-inch disk, 0 to 15 on a minidisk. The
  head is always 0.
- With no count, the fault is **hard**. The sector fails each time.
- With `count=N`, the sector fails N times. After that, the sector is good. A test with a count
  shows that a retry gets past the error.

| Kind | What the controller sees |
|---|---|
| `data` | The data field has a CRC error. The guest still gets the data. |
| `id` | The ID field has a CRC error. The controller does not find the sector. |
| `missing` | The sector has no ID field. The controller does not find the sector. |
| `write` | The drive does not write the sector. The controller reports a write fault. |

The controller reports each fault with the status bits of its chip. The `disk1` and `disk1a` have
a µPD765. The other controllers have a WD17xx.

| Kind | WD17xx status | µPD765 result |
|---|---|---|
| `data` | CRC Error (bit 3) | ST0 AT, ST1 DE, ST2 DD |
| `id` | Record Not Found and CRC Error (bits 4 and 3) | ST0 AT, ST1 DE |
| `missing` | Record Not Found (bit 4) | ST0 AT, ST1 ND |
| `write` | Write Fault (bit 5) | ST0 AT and EC |

The `dcdd` and the `mds` have no error bit. The guest reads each sector as 137 raw bytes, and it
finds an error with its own checks, such as a checksum. A fault on these controllers changes the
bytes that the guest reads:

| Kind | What the guest reads on a `dcdd` or an `mds` |
|---|---|
| `data` | Byte 64 of the sector is wrong, so the checksum of the sector fails. |
| `id` | Byte 0 gives the wrong track number. |
| `missing` | Each byte of the sector is 0, as on a sector that was never written. |
| `write` | The drive does not write the sector. The old bytes stay on the disk. |

On these controllers, a fault occurs when the guest reads the bad byte. A sector that turns
under the head while the guest does not read it does not use the fault. For example, the CP/M
BIOS of the 88-DCDD reads a bad sector 12 times. Sector 0 of track 2 holds directory entries. A
hard fault on that sector gives 12 hits and then the BDOS error:

```
A>DIR
Bdos Err On A: Bad Sector
```

```
altairsim> FAULT
  UNIT         FAULT        HITS   LEFT
  dsk0:drive0  2/0/0 data     12      -
```

1. Mount the disk.
2. Add the fault. This one gives two CRC errors on track 2, head 0, sector 1:

   ```
   altairsim> FAULT fdc0:drive0 ADD 2/0/1 data count=2
   fdc0:drive0: fault 2/0/1 data count=2
   ```

3. Run the guest.
4. Type `FAULT` to see the faults of all the drives:

   ```
   altairsim> FAULT
     UNIT         FAULT                HITS   LEFT
     fdc0:drive0  2/0/1 data count=2      2      0
   ```

`HITS` is the number of times that the fault occurred. `LEFT` is the number of failures before
the sector is good. `LEFT` is `-` for a hard fault. On this disk, CP/M reads the sector at its
first `A>` prompt. The BIOS got two CRC errors, read the sector a third time, and continued.

A second `ADD` with the same address and kind replaces the fault, and sets its counts again.
`SHOW MOUNTS` shows the number of faults beside each disk:

```
altairsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  fdc0:drive0  disk  cpm22.dsk  (2 faults)
```

### Remove a fault

| Command | What it removes |
|---|---|
| `FAULT fdc0:drive0 CLEAR 2/0/1` | each fault at that address |
| `FAULT fdc0:drive0 CLEAR 2/0/1 data` | the `data` fault at that address |
| `FAULT fdc0:drive0 CLEAR ALL` | each fault of the drive |

`CLEAR` matches the address as you wrote it. `CLEAR 10/*/*` removes the fault `10/*/*`. It does
not remove a fault on one sector of track 10.

`MOUNT` and `UNMOUNT` remove the faults of the drive, because the faults are for the disk in it.

### See each fault as it occurs

Turn on the `fault` debug flag of the controller:

```
altairsim> SET fdc0 DEBUG=fault
```

The controller then writes a line each time that a fault occurs. The line starts with the
address of the instruction that the processor ran:

```
AEC8  fdc0: fault drive0 2/0/1 data hit 1, 1 left
AEC8  fdc0: fault drive0 2/0/1 data hit 2, 0 left
```

The flag writes nothing until you turn it on. `SET fdc0 NODEBUG=fault` turns it off. The flag
changes only the lines. The faults occur and count in the same way with the flag off.

### Faults in the machine file

The `faults` key of a drive gives its faults. Put a `;` between two faults:

```toml
[[board]]
type = "tarbell"
id   = "fdc0"

  [[board.drive]]
  unit   = 0
  mount  = "cpm22.dsk"
  faults = "2/0/1 data count=2; 10/*/* write"
```

The machine starts with the faults on the disk. `CONFIG SAVE` writes the `faults` key of each
drive that has faults. A snapshot keeps the faults and their counts, and `RESTORE` gives them
back.

## Look at what is in the machine

`SHOW` on the controller lists its drives and the disk in each one. `BOARDS` lists every board
and the addresses that it decodes:

```
altairsim> SHOW dsk0
altairsim> BOARDS
```

These two commands answer most questions about a machine that does not boot. The most common
answer is that the disk is in the wrong drive.
