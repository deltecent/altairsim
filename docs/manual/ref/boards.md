<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# Boards and their parameters

Every key below is a key you may write in a machine file, and the *same* key you
may `SET` at the monitor prompt. That is not a coincidence and it is not a
convention: a board's properties **are** its TOML schema, so there is nothing here
that could disagree with the program.

Numbers follow the one rule: **on the wire → hex, never on the wire → decimal.**
A port is hex; a baud rate and a drive count are decimal. The defaults below are
printed in each property's own base.

The catalogue is **grouped by function** — CPU, memory, disk, serial, and so on —
and within a group the boards are in **alphabetical order**.

**CPU**

| Type | What it is |
|---|---|
| [`8080`](#8080) | MITS 88-CPU: 8080A CPU board |
| [`8085`](#8085) | Generic 8085 CPU board |
| [`z80`](#z80) | Generic Z80 CPU board |

**Memory**

| Type | What it is |
|---|---|
| [`bankmem`](#bankmem) | Bank-switched RAM (Vector, Cromemco, North Star, ExpandoRAM) |
| [`memory`](#memory) | RAM/ROM board: plain, unbanked memory regions |
| [`v2z80rom`](#v2z80rom) | S100Computers V2 Z80: paged monitor EEPROM |

**Disk**

| Type | What it is |
|---|---|
| [`16fdc`](#16fdc) | Cromemco 16FDC: floppy controller + console UART, RDOS 2.52 |
| [`64fdc`](#64fdc) | Cromemco 64FDC: floppy controller + console UART, RDOS 3.12 |
| [`dcdd`](#dcdd) | MITS 88-DCDD: 8" hard-sector floppy controller |
| [`dualide`](#dualide) | S100Computers IDE-AB: two CompactFlash sockets for CP/M 3 |
| [`dualsd`](#dualsd) | S100Computers Dual SD: two microSD sockets for CP/M 3 |
| [`fdcplus`](#fdcplus) | FarmTek FDC+: serial drive (drive types 6, 7) |
| [`hdsk`](#hdsk) | MITS 88-HDSK Datakeeper: Pertec hard disk controller |
| [`icom`](#icom) | iCOM FD3712/FD3812: 8" floppy controller with boot PROM |
| [`mds`](#mds) | MITS 88-MDS: 5.25" minidisk controller |
| [`mdsa`](#mdsa) | North Star MDS-A: single-density 5.25" floppy controller |
| [`mdsad`](#mdsad) | North Star MDS-A-D: double-density 5.25" floppy controller |
| [`tarbell`](#tarbell) | Tarbell #1011: single-density floppy controller |
| [`tarbelldd`](#tarbelldd) | Tarbell #2022: double-density floppy controller |
| [`versafloppy`](#versafloppy) | SD Systems VersaFloppy I/II: WD177x floppy controller |

**Serial**

| Type | What it is |
|---|---|
| [`2sio`](#2sio) | MITS 88-2SIO: two 6850 serial ports |
| [`gsio`](#gsio) | Generic SIO: two strap-configurable serial channels |
| [`io4`](#io4) | SSM IO-4: two serial + two parallel ports |
| [`pmmi`](#pmmi) | PMMI MM-103: Bell 103 modem |
| [`propio`](#propio) | S100Computers Console IO: Propeller console serial port |
| [`sbc`](#sbc) | SD Systems SBC-100/200: Z80 single-board computer |
| [`sio`](#sio) | MITS 88-SIO: one serial port (COM2502 UART) |
| [`turnkey`](#turnkey) | MITS 8800b Turnkey Module: boot PROM, serial, auto-start |

**Tape**

| Type | What it is |
|---|---|
| [`acr`](#acr) | MITS 88-ACR: audio cassette interface |
| [`uio`](#uio) | MITS 88-UIO: serial port + cassette interface |

**Parallel and printer**

| Type | What it is |
|---|---|
| [`4pio`](#4pio) | MITS 88-4PIO: up to four 6820 parallel ports |
| [`c700`](#c700) | MITS 88-C700: Centronics line-printer controller |
| [`d7a`](#d7a) | Cromemco D+7A: analog + parallel I/O, joysticks |
| [`lpc`](#lpc) | MITS 88-LPC: 88-LP line-printer controller |
| [`music6`](#music6) | Newtech Model 6 Music Board: 6-bit D/A and speaker |
| [`pio`](#pio) | MITS 88-PIO: 8-bit parallel port |

**Video**

| Type | What it is |
|---|---|
| [`cadzilla`](#cadzilla) | CADzilla: HD63484 ACRTC graphics board with a Bt453 RAMDAC |
| [`dazzler`](#dazzler) | Cromemco Dazzler: color graphics |
| [`vdb8024`](#vdb8024) | SD Systems VDB-8024: 80x24 video terminal board |
| [`vdm1`](#vdm1) | Processor Technology VDM-1: 16x64 memory-mapped video |

**Systems**

| Type | What it is |
|---|---|
| [`sol`](#sol) | Processor Technology Sol-PC I/O: serial, keyboard, tape |

**PROM programmer**

| Type | What it is |
|---|---|
| [`pb1`](#pb1) | SSM PB1: 2708/2716 EPROM programmer |

**Other**

| Type | What it is |
|---|---|
| [`fp`](#fp) | Altair front panel: the SENSE switches at IN 0FFH |
| [`hostbridge`](#hostbridge) | Host Bridge: file transfer to the host (not a period board) |
| [`rtc100`](#rtc100) | SciTronics RTC-100: battery-backed clock/calendar |
| [`ss1`](#ss1) | CompuPro System Support 1: interrupts, timer, clock, serial |
| [`virtc`](#virtc) | MITS 88-VI/RTC: vectored interrupts + real-time clock |


## CPU

### `8080`

MITS 88-CPU: an 8080A CPU board. Decodes nothing -- it drives the bus

**Units:** `8080` (cpu)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the board. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: T-states per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


### `8085`

Generic 8085 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with an 8085 core (RIM/SIM + TRAP/RST 5.5/6.5/7.5)

**Units:** `8085` (cpu)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the board. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: T-states per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


### `z80`

Generic Z80 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core

**Units:** `z80` (cpu)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the board. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: T-states per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


## Memory

### `bankmem`

S-100 bank-switched RAM. One card, four decoders (card=vector|cromemco64kz|northstar|expandoram2): a write-only select port swaps which RAM plane(s) drive the bus. Each card owns its own decode -- one-hot select (Vector 40), 8-bit bank mask (Cromemco 40), on/off+one-hot toggle (North Star C0), or PROM page-select (ExpandoRAM II FF, approximated)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `card` | enum | `vector` | `vector` \| `cromemco64kz` \| `northstar` \| `expandoram2` | which banked card this is -- each owns its own decode: vector \| cromemco64kz \| northstar \| expandoram2 |
| `port` | int | `0x40` | `0x0` .. `0xFF` | the write-only bank-select port. Card default (vector/cromemco 40, northstar C0, expandoram2 FF); overridable, as the real boards relocate it |
| `banks` | int | `8` | `1` .. `10` | how many switchable banks/planes/pages this subsystem carries (one per board). Card-capped: vector/cromemco 8, northstar 6, expandoram2 10. The common region (partition) is not a bank. Setting `ram` derives this |
| `partition` | enum | `none` | `none` \| `ex48` \| `ex32` | ExpandoRAM II common-memory partition (the EX-48/EX-32 PROM): none \| ex48 \| ex32. ex48 = 48K banked (0000-BFFF) + 16K common (C000-FFFF); ex32 = 32K banked + 32K common (8000-FFFF). The common region is identical in every bank -- what a banked CP/M's resident OS and bank-switch routine live in. `none` (default) is a whole 64K plane |
| `ram` | int | `512` | `1` .. `640` | total board RAM in KB. Sets the bank count from the current partition: with ex48, 256 -> 5 banks; with none (whole plane), 256 -> 4 banks of 64K. The real ExpandoRAM II is 64K (16K chips) or 256K (64K chips) |
| `honors_phantom` | enum | `none` | `none` \| `read` \| `all` | A JUMPER. Another board pulls PHANTOM* (a boot PROM shadowing the RAM this card sits under) -- do I switch off? none \| read \| all. `read` keeps answering writes so a cold-boot loader falls through to this RAM while the PROM shadows reads (an ExpandoRAM II under the SBC-200 boot PROM). Default none -- a plain banked-RAM machine has no PROM overlay |
| `active` | string | — | — | the live bank(s) right now -- the guest sets this by writing the select port. Read-only here **(read-only — not a key you may set)** |
| `fill` | enum | `random` | `zero` \| `random` | RAM contents at power-on: zero \| random (real RAM is not zeroed) |
| `seed` | int | `1` | any | seed for fill=random -- the same seed fills RAM the same way at every POWER, so a run is repeatable |


### `memory`

RAM/ROM board: a list of regions and PHANTOM* -- plain, unbanked memory (bank switching is its own board, `bankmem`)

#### `[[board.region]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `type` | enum | `ram` \| `rom` | RAM, or ROM (which needs a `mount`, unless you want an empty socket) |
| `at` | int | `0x0` .. `0xFFFF` | Where it starts. An address: 0000, F800 |
| `size` | int | `1` .. `65536` | How much. Decimal, and it takes a suffix: 48K, 1024, 2M |
| `mount` | string | text | The ROM image. A file (relative to THIS FILE), or builtin:<name> |
| `relocate` | bool | `on` \| `off` | Move a HEX/S-record image to `at` instead of its own record address |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `honors_phantom` | enum | `all` | `none` \| `read` \| `all` | A JUMPER. Another board pulls PHANTOM* -- do I switch off? none \| read \| all |
| `phantom` | enum | `all` | `none` \| `read` \| `all` | What I ASSERT over my rom regions: none \| read \| all |
| `fill` | enum | `random` | `zero` \| `random` | RAM contents at power-on: zero \| random (real RAM is not zeroed) |
| `seed` | int | `1` | any | Seed for fill=random. The same seed fills RAM the same way at every POWER, so a run is repeatable; change it for a different junk pattern |
| `pages` | string | — | — | the composite page map -- which pages this board answers for. Derived from the regions you declared **(read-only — not a key you may set)** |


### `v2z80rom`

S100Computers V2 Z80 CPU board -- its onboard paged monitor EEPROM (the Z80 itself is board 'z80cpu'). An 8K 28C64 at F000-FFFF holding two 4K pages, builtin:master0 (low) / master1 (high), selected by OUT D3H bit1 (bit0=1 inactivates the EEPROM so RAM shows through). Shadows RAM in its window while enabled. Cold-start the MASTER monitor with startup=["RUN F000"]; the 'I' command boots CP/M 3 off a dualsd card

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xD3` | `0x0` .. `0xFF` | Page/ROM-control latch (D3H): bit1 = EEPROM A12 page select, bit0 = ROM inactivate |


## Disk

### `16fdc`

Cromemco 16FDC: WD FD1793 soft-sector floppy (single + double density), up to 4 drives. Disk registers at 30-34, a TMS 5501 console UART at 00-09 (unit 'tty'), and a 4K RDOS 2.52 boot PROM at C000 (OUT 40H banks it out, RESET restores it). Boots CDOS

**Units:** `tty` (serial, CONNECT), `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive senses it, so the guest is never told *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `bootstrap` | bool | `true` | `on` \| `off` | The BOOT/MON strap. On (default): the RDOS ROM is mapped at C000 and ¬BOOT reads low, so RDOS boots the disk. Off: the ROM still answers but ¬BOOT reads high (the monitor prompt instead of an auto-boot) |
| `boot_drive` | enum | `A` | `A` \| `B` \| `C` \| `D` | The boot-drive switches (16FDC switches 7 and 8, 64FDC switches 3 and 4). RDOS reads them for an automatic boot, and for its B command with no drive letter |
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (A-D, one-hot select DS4-DS1) |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a disk access completes at once. real: with Auto Wait armed, IN 34 holds READY until DRQ or the end of the command, so seek, head settle and byte times take emulated time, as on the board |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `0` .. `76800` | Line rate. The guest sets it by writing the baud register; this seeds it and shows the effective rate (0 = the register selected no rate) |
| `rate` | string | `full` | text | Console speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `64fdc`

Cromemco 64FDC: the 16FDC's 1983 successor -- same FD1793 + TMS 5501, carrying an 8K RDOS 3.12 boot PROM at C000-DFFF (OUT 40H banks it out, RESET restores it). Boots CDOS

**Units:** `tty` (serial, CONNECT), `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive senses it, so the guest is never told *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `bootstrap` | bool | `true` | `on` \| `off` | The BOOT/MON strap. On (default): the RDOS ROM is mapped at C000 and ¬BOOT reads low, so RDOS boots the disk. Off: the ROM still answers but ¬BOOT reads high (the monitor prompt instead of an auto-boot) |
| `boot_drive` | enum | `A` | `A` \| `B` \| `C` \| `D` | The boot-drive switches (16FDC switches 7 and 8, 64FDC switches 3 and 4). RDOS reads them for an automatic boot, and for its B command with no drive letter |
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (A-D, one-hot select DS4-DS1) |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a disk access completes at once. real: with Auto Wait armed, IN 34 holds READY until DRQ or the end of the command, so seek, head settle and byte times take emulated time, as on the board |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `0` .. `76800` | Line rate. The guest sets it by writing the baud register; this seeds it and shows the effective rate (0 = the register selected no rate) |
| `rate` | string | `full` | text | Console speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `dcdd`

MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive on the daisy chain |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The DRIVE senses it, so the guest is never told: it writes, the head is inhibited, the bytes go nowhere *(also `writeprotect`)* |
| `media` | enum | `8in` \| `fdc8mb` | Force the format instead of probing the image's size |
| `create` | bool | `on` \| `off` | Make the disk file (empty) if it is not there, then mount it -- a fresh disk to FORMAT. Pair with `media` to pick its geometry |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x8` | `0x0` .. `0xFD` | Base address. The board decodes three ports: BASE+0 .. BASE+2 |
| `drives` | int | `4` | `1` .. `16` | Drives on the daisy chain |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |


### `dualide`

S100Computers IDE-AB (CF): the IDE/CompactFlash half of the IDE+ESP32 combination board -- two CF sockets (drives 0/1 = A:/B:) for CP/M 3. Five 8255 ports at BASE+0..4 (default 30): A/B data, C control lines, mode config, drive select. Programmed-I/O ATA register engine (LBA read/write, 512-byte sectors). No boot PROM -- the CPU board's monitor boots CP/M from the CF. Mounts the SAME card image as dualsd (a .img with a .geo geometry sidecar); pair with dualsd for the full A:/B:+C:/D: system

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `1` | Which CF socket (0 = drive A:, 1 = drive B:) |
| `mount` | string | text | The card image (a .img with a sibling .geo geometry) to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the card *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x30` | `0x0` .. `0xFB` | Base address. The board decodes five 8255 ports: BASE..BASE+4 (A,B,C,cfg,drive) |


### `dualsd`

S100Computers Dual SD: two microSD sockets (drives 0/1) presented as raw 512-byte-sector CF/SD cards, for CP/M 3. Two ports at BASE+0..1 (default 80): status/command + data. Programmed-I/O command/handshake engine (33H-lead + 8 commands). No boot PROM -- the CPU board's monitor loads CP/M from track 0. Mount a card image (a .img with a .geo geometry sidecar)

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `1` | Which SD socket (0 = drive 1 / C:, 1 = drive 2 / D:) |
| `mount` | string | text | The card image (a .img with a sibling .geo geometry) to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the card *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x80` | `0x0` .. `0xFE` | Base address. The board decodes two ports: BASE (status/command) and BASE+1 (data) |


### `fdcplus`

FarmTek FDC+ in serial-drive mode: an 88-DCDD/88-MDS-compatible controller with no drive at all -- a drive server on unit 'line' holds the images and the card fetches a whole track at a time. drivetype=7 (8", incl. the 8 MB drive) or 6 (Minidisk), read at power-on. Four ports at BASE+0..3 (08 or 80)

**Units:** `line` (serial, CONNECT), `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive: 0-3. Drive type 5 only |
| `mount` | string | text | The 1.5 MB disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive tells the card, and the card tells the 8080: status bit 4 *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x8` | `0x0` .. `0xFC` | Base address: 08, or 80 with the address jumpers moved. Four ports, BASE+0 .. BASE+3 |
| `drivetype` | int | `7` | `5` .. `7` | Drive Type switches (S3), read at power-on: 5 = 1.5 MB floppy, 6 = serial drive as a Minidisk, 7 = serial drive as an 8" drive |
| `baud` | int | `403200` | `9600` .. `460800` | Serial drive baud rate: 9600, 19200, 38400, 57600, 76800, 230400, 403200 (preferred) or 460800 |
| `connect` | string | `null` | text | The drive server on the other end of the line (CONNECT sets this) |


### `hdsk`

MITS 88-HDSK Datakeeper: Pertec hard disk, 256-byte sectors from a linear .DSK. Eight ports at BASE+0..7 (default A0). Command/handshake protocol, four page buffers

**Units:** `drive0` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | any | Which logical drive (slot = unit*2 + platter) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xA0` | `0x0` .. `0xF8` | Base address. The board decodes eight ports: BASE+0 .. BASE+7 |
| `drives` | int | `1` | `1` .. `8` | Logical drives (one platter each): slot = unit*2 + platter |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |


### `icom`

iCOM FD3712/FD3812 8" floppy: a programmed-I/O command/handshake controller on the S-100 Interface board. Two ports at BASE+0..1 (default C0) plus a boot PROM and 6810 scratch RAM in high memory (rom=builtin:icom-fd3712-cpm | icom-fd3712-fdos | icom-fd3812-cpm). Boots CP/M 2.2 (single and double density) and FDOS. Up to 4 drives

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `1` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xC0` | `0x0` .. `0xFE` | Base address. The board decodes two ports: BASE (command/status) and BASE+1 (data) |
| `rom` | string | `builtin:icom-fd3712-cpm` | text | The boot PROM this interface board carries: builtin:icom-fd3712-cpm (CP/M 2.2, single density, F000), builtin:icom-fd3712-fdos (FDOS, C000), or builtin:icom-fd3812-cpm (CP/M 2.2, double density, F000). The window base and the 6810 scratch RAM follow the image |
| `drives` | int | `2` | `1` .. `4` | Drives on the controller (unit 0..3, selected by cDRVSEC bits 7:6) |


### `mds`

MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive on the daisy chain |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The DRIVE senses it, so the guest is never told: it writes, the head is inhibited, the bytes go nowhere *(also `writeprotect`)* |
| `media` | enum | `minidisk` | Force the format instead of probing the image's size |
| `create` | bool | `on` \| `off` | Make the disk file (empty) if it is not there, then mount it -- a fresh disk to FORMAT. Pair with `media` to pick its geometry |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x8` | `0x0` .. `0xFD` | Base address. The board decodes three ports: BASE+0 .. BASE+2 |
| `drives` | int | `4` | `1` .. `4` | Drives on the daisy chain |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |
| `motor` | enum | `free` | `free` \| `real` | free: always at speed (default). real: 1 s spin-up, and it stops after 6.4 s |


### `mdsa`

North Star MDS-A: single-density 5.25" hard-sector floppy, up to 3 drives. No ports: a 1 K memory block at BASE (default E800) where a memory READ is the command. Boot PROM on the board -- RUN E900

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `2` | Which drive. Unit 0 is the drive that North Star calls drive 1 |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the diskette. The guest sees it in the WP status bit *(also `writeprotect`)* |
| `media` | enum | `sd` | Force the format instead of probing the image's size |
| `create` | bool | `on` \| `off` | Make the disk file (empty) if it is not there, then mount it -- a blank diskette for the guest to format |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xE800` | `0x0` .. `0xFC00` | Origin of the 1 K block. The built-in PROM is the standard part and runs only at E800 |
| `drives` | int | `3` | `1` .. `3` | Drives on the cable |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the sector-pulse interrupt is jumpered (lower left of the board) *(interrupt strap)* |
| `motor` | enum | `free` | `free` \| `real` | free: the motors run until they are stopped (default). real: they stop by themselves after the board's count of revolutions |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a read-data or write-data access completes at once. real: it holds READY until the byte is under the head, so a sector transfer takes emulated time, as on the board |


### `mdsad`

North Star MDS-A-D: double-density 5.25" hard-sector floppy (single, double and two-sided), up to 4 drives. No ports: a 1 K memory block at BASE (default E800) where a memory READ is the command. Boot PROM on the board -- RUN E800

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive. Unit 0 is the drive that North Star calls drive 1 |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the diskette. The guest sees it in the WP status bit *(also `writeprotect`)* |
| `media` | enum | `sd` \| `dd` \| `quad` | Force the format instead of probing the image's size |
| `create` | bool | `on` \| `off` | Make the disk file (empty) if it is not there, then mount it -- a blank diskette for the guest to format |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xE800` | `0x0` .. `0xFC00` | Origin of the 1 K block. The built-in PROM is the standard part and runs only at E800 |
| `drives` | int | `4` | `1` .. `4` | Drives on the cable |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the sector-pulse interrupt is jumpered (lower left of the board) *(interrupt strap)* |
| `motor` | enum | `free` | `free` \| `real` | free: the motors run until they are stopped (default). real: they stop by themselves after the board's count of revolutions |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a read-data or write-data access completes at once. real: it holds READY until the byte is under the head, so a sector transfer takes emulated time, as on the board |


### `tarbell`

Tarbell #1011: single-density WD FD1771 floppy, up to 4 drives. Eight ports at BASE+0..7 (default F8). Carries a 32-byte boot PROM that shadows 0000 over PHANTOM* -- boots CP/M automatically at reset (bootstrap=on)

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive senses it, so the guest is never told *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `bootstrap` | bool | `true` | `on` \| `off` | The boot-PROM enable DIP. On (default): the 32-byte PROM shadows 0000 over PHANTOM* at reset and boots the disk. Off: a plain disk controller, no PROM |
| `port` | int | `0xF8` | `0x0` .. `0xF8` | Base address. The board decodes eight ports: BASE+0 .. BASE+7 (default F8) |
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (binary select 0-3) |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a disk access completes at once. real: the wait port holds READY until the next byte or the end of the command, so seek, head settle and byte times take emulated time, as on the board |


### `tarbelldd`

Tarbell #2022: double-density WD FD1791 floppy (mixed-density media, SD track 0), up to 4 drives. The single-density card's twin with a bitmap OUT-FC latch and a port-FD DMA/ext-addr register. Same 32-byte boot PROM

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive senses it, so the guest is never told *(also `writeprotect`)* |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `bootstrap` | bool | `true` | `on` \| `off` | The boot-PROM enable DIP. On (default): the 32-byte PROM shadows 0000 over PHANTOM* at reset and boots the disk. Off: a plain disk controller, no PROM |
| `port` | int | `0xF8` | `0x0` .. `0xF8` | Base address. The board decodes eight ports: BASE+0 .. BASE+7 (default F8) |
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (binary select 0-3) |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a disk access completes at once. real: the wait port holds READY until the next byte or the end of the command, so seek, head settle and byte times take emulated time, as on the board |
| `dmaport` | int | `0xE0` | `0x0` .. `0xF0` | Base of the on-card 8257 DMA controller's 16-port register block (default E0). The DMA-mode CBIOS programs ADR/WCT here; the SD2DD strap is E0 |


### `versafloppy`

SD Systems VersaFloppy I/II: WD FD177x soft-sector floppy, up to 4 drives. Eight ports at BASE+0..7 (default 60). variant=vfi (FD1771, single density) | vfii (FD1791, single+double). Boots SDOS with the SBC-200 + DDBIOS

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive senses it, so the guest is never told *(also `writeprotect`)* |
| `media` | enum | `8sd` \| `8dd` \| `8dd256` \| `8sd-ds` \| `8dd-ds` \| `8dd256-ds` \| `5sd` \| `5sd-ds` \| `5dd` \| `5dd-ds` | Force the format instead of probing the image's size |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `variant` | enum | `vfii` | `vfi` \| `vfii` | Which board: vfi (FD1771, single density) or vfii (FD1791, single and double density). vfii is the default -- it boots SDOS's DD-256 disks |
| `port` | int | `0x60` | `0x0` .. `0xF8` | Base address. The board decodes eight ports: BASE+0 .. BASE+7 (60H) |
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (one-hot select D0-D3) |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |
| `timing` | enum | `full` | `full` \| `real` | Disk timing. full: a disk access completes at once. real: the data port holds READY until the next byte or the end of the command (while the wait-state circuit is enabled), so seek, head settle and byte times take emulated time, as on the board |


## Serial

### `2sio`

MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3

**Units:** `a` (serial, CONNECT), `b` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x10` | `0x0` .. `0xFC` | Base address. The board decodes four ports: BASE+0 .. BASE+3 |

#### Unit `a` — `[board.unit.a]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |

#### Unit `b` — `[board.unit.b]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `gsio`

Generic SIO: a strap-configurable serial board with TWO independent channels, units 'a' and 'b' (configure each under [board.unit.a] / [board.unit.b]). Basic transmit/receive only -- no programmable word length, parity, stop bits or framing/overrun status; a specific card that needs those is a separate emulated board. Per channel you strap: a status/control port (status_port -- read synthesizes DAV/TBMT at bit positions you pick, write is discarded) and a data port (data_port); one inverter_gate knob inverts both status bits together. Built-in profiles preset the straps: profile=sior1 (MITS SIO Rev 1, the default) | sior0 (MITS SIO Rev 0) | tuart (Cromemco TU-ART) | imsai-sio2 | compupro-if2 (CompuPro Interfacer II) | compupro-ss1 (CompuPro System Support 1). Channel a defaults to ports 0/1, b to 2/3. Polled, no interrupts. CONNECT each channel to a file, socket, serial port, in:/out:

**Units:** `a` (serial, CONNECT), `b` (serial, CONNECT)

#### Board properties

*No settable properties.*

#### Unit `a` — `[board.unit.a]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `profile` | enum | `sior1` | `custom` \| `sior1` \| `sior0` \| `tuart` \| `imsai-sio2` \| `compupro-if2` \| `compupro-ss1` | Built-in card to preset the straps from: custom, or a named board. Selecting one sets status_port/data_port/bits/inverter_gate (still overridable) |
| `status_port` | int | `0x0` | `0x0` .. `0xFF` | Status(read)/control(write) port. Control writes are discarded |
| `data_port` | int | `0x1` | `0x0` .. `0xFF` | Data port: receive(read)/transmit(write) |
| `dav` | int | `0` | `0` .. `7` | Status bit (0-7) carrying DAV, data available (a byte to receive) |
| `tbmt` | int | `7` | `0` .. `7` | Status bit (0-7) carrying TBMT, transmit buffer empty (ready to send) |
| `inverter_gate` | bool | `true` | `on` \| `off` | Route both status bits through the inverter gate -- asserted DAV/TBMT read 0 (active low) |
| `baud` | int | `9600` | any | Line rate programmed onto a CONNECTed real serial port (8N1). Inert on a socket/file; does not pace the emulated line |
| `connect` | string | `null` | text | The endpoint on the serial line (CONNECT sets this): a file, socket, serial port, in:/out: file, null, loopback |

#### Unit `b` — `[board.unit.b]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `profile` | enum | `sior1` | `custom` \| `sior1` \| `sior0` \| `tuart` \| `imsai-sio2` \| `compupro-if2` \| `compupro-ss1` | Built-in card to preset the straps from: custom, or a named board. Selecting one sets status_port/data_port/bits/inverter_gate (still overridable) |
| `status_port` | int | `0x2` | `0x0` .. `0xFF` | Status(read)/control(write) port. Control writes are discarded |
| `data_port` | int | `0x3` | `0x0` .. `0xFF` | Data port: receive(read)/transmit(write) |
| `dav` | int | `0` | `0` .. `7` | Status bit (0-7) carrying DAV, data available (a byte to receive) |
| `tbmt` | int | `7` | `0` .. `7` | Status bit (0-7) carrying TBMT, transmit buffer empty (ready to send) |
| `inverter_gate` | bool | `true` | `on` \| `off` | Route both status bits through the inverter gate -- asserted DAV/TBMT read 0 (active low) |
| `baud` | int | `9600` | any | Line rate programmed onto a CONNECTed real serial port (8N1). Inert on a socket/file; does not pace the emulated line |
| `connect` | string | `null` | text | The endpoint on the serial line (CONNECT sets this): a file, socket, serial port, in:/out: file, null, loopback |


### `io4`

SSM IO-4 (2P+2S): the real Solid State Music board -- two full-duplex serial channels AND a four-port parallel section. Serial units 'a'/'b' are real 1602-family UARTs with programmable word length (data_bits 5-8), parity and stop bits (unlike the generic gsio), plus the full status-word strap-up (stat_* map, invert_status, port_reversal) and named profiles (default altair-rev1). A 4-port block set by switch S3 (default 0-3): Serial A status/data at BASE+0/+1, Serial B at BASE+2/+3. Parallel units 'pa'/'pb' are 8212 latched ports (input latch + service request, output latch) on their own 2-port block set by switch S4 (par_port, default 4-5): Parallel A at PAR+0, B at PAR+1; a byte the far end sends is strobed in, a write latches out, and the §3.2.2 status/data console flag is strappable (dav_bit/dav_source/dav_active_low). If the two blocks overlap, neither section answers the shared ports. Each serial channel's receive (DAV) and transmit (TBMT) and each parallel input (service request) can raise an interrupt, strapped on header W4 to a VI line, pin 73 (int), or none (rx_int/tx_int per serial channel, int per parallel port) -- there is no software enable, the strap is the enable, and a parallel input interrupt rises even when the port is not addressed. Configure each unit under [board.unit.a] / [board.unit.pa] etc.; CONNECT each to a file, socket, serial port, in:/out:

**Units:** `a` (serial, CONNECT), `b` (serial, CONNECT), `pa` (serial, CONNECT), `pb` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x0` | `0x0` .. `0xFC` | Serial base address (switch S3) -- a 4-PORT BLOCK, so a multiple of 4. Serial A at BASE+0/+1, Serial B at BASE+2/+3 |
| `par_port` | int | `0x4` | `0x0` .. `0xFE` | Parallel base address (switch S4) -- a 2-PORT BLOCK, so a multiple of 2. Parallel A (J6-in/J5-out) at BASE, Parallel B (J4-in/J3-out) at BASE+1. If it overlaps the serial block, neither section answers the shared ports |

#### Unit `a` — `[board.unit.a]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `profile` | enum | `altair-rev1` | `custom` \| `altair-rev1` \| `altair-rev0` \| `i8251` \| `proctech` \| `imsai` | Preset the status straps from a host personality: custom, altair-rev1 (the default and the SSM 8080 monitor console), altair-rev0, i8251, proctech, imsai. Sets stat_*, invert_status and port_reversal (overridable) |
| `stat_dav` | enum | `0` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying DAV, data available (0-7 \| none) |
| `stat_tbmt` | enum | `7` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying TBMT, transmit buffer empty (0-7 \| none) |
| `stat_teoc` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying TEOC, transmitter end of character (0-7 \| none) |
| `stat_ror` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying ROR, receiver over-run (0-7 \| none; always inactive) |
| `stat_rpe` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying RPE, receiver parity error (0-7 \| none; always inactive) |
| `stat_rfe` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying RFE, receiver framing error (0-7 \| none; always inactive) |
| `invert_status` | bool | `true` | `on` \| `off` | Invert every status bit -- a 74368 buffer (negative sense): an asserted signal reads 0, an inactive one reads 1. Off = 74367 (positive sense) |
| `port_reversal` | bool | `false` | `on` \| `off` | Reverse the channel's two ports (S1/S2-PR): off = status first, data last (MITS, Proc Tech); on = data first, status last (IMSAI) |
| `rx_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the receive interrupt (a new character, DAV) is strapped on header W4: none \| int \| vi0..vi7. Canonical: vi1 (Serial A) / vi0 (Serial B) *(interrupt strap)* |
| `tx_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the transmit interrupt (buffer empty, TBMT) is strapped on header W4: none \| int \| vi0..vi7. It is a LEVEL -- held while the transmitter is idle. Canonical: vi2 (Serial A) / vi3 (Serial B) *(interrupt strap)* |
| `baud` | int | `9600` | `50` .. `25000` | Line rate (header W3). RX and TX share one rate here -- a real host serial port cannot be split. Canonical IO-4 rates: 55-9600 |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character (S1/S2 NDB1+NDB2) |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits (S1/S2 NSB): 1 or 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | Parity (S1/S2 NPB/POE): none \| odd \| even |
| `connect` | string | `null` | text | The endpoint on this channel's line (CONNECT sets this): a file, socket, serial port, in:/out: file, null, loopback |

#### Unit `b` — `[board.unit.b]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `profile` | enum | `altair-rev1` | `custom` \| `altair-rev1` \| `altair-rev0` \| `i8251` \| `proctech` \| `imsai` | Preset the status straps from a host personality: custom, altair-rev1 (the default and the SSM 8080 monitor console), altair-rev0, i8251, proctech, imsai. Sets stat_*, invert_status and port_reversal (overridable) |
| `stat_dav` | enum | `0` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying DAV, data available (0-7 \| none) |
| `stat_tbmt` | enum | `7` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying TBMT, transmit buffer empty (0-7 \| none) |
| `stat_teoc` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying TEOC, transmitter end of character (0-7 \| none) |
| `stat_ror` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying ROR, receiver over-run (0-7 \| none; always inactive) |
| `stat_rpe` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying RPE, receiver parity error (0-7 \| none; always inactive) |
| `stat_rfe` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit carrying RFE, receiver framing error (0-7 \| none; always inactive) |
| `invert_status` | bool | `true` | `on` \| `off` | Invert every status bit -- a 74368 buffer (negative sense): an asserted signal reads 0, an inactive one reads 1. Off = 74367 (positive sense) |
| `port_reversal` | bool | `false` | `on` \| `off` | Reverse the channel's two ports (S1/S2-PR): off = status first, data last (MITS, Proc Tech); on = data first, status last (IMSAI) |
| `rx_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the receive interrupt (a new character, DAV) is strapped on header W4: none \| int \| vi0..vi7. Canonical: vi1 (Serial A) / vi0 (Serial B) *(interrupt strap)* |
| `tx_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the transmit interrupt (buffer empty, TBMT) is strapped on header W4: none \| int \| vi0..vi7. It is a LEVEL -- held while the transmitter is idle. Canonical: vi2 (Serial A) / vi3 (Serial B) *(interrupt strap)* |
| `baud` | int | `9600` | `50` .. `25000` | Line rate (header W3). RX and TX share one rate here -- a real host serial port cannot be split. Canonical IO-4 rates: 55-9600 |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character (S1/S2 NDB1+NDB2) |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits (S1/S2 NSB): 1 or 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | Parity (S1/S2 NPB/POE): none \| odd \| even |
| `connect` | string | `null` | text | The endpoint on this channel's line (CONNECT sets this): a file, socket, serial port, in:/out: file, null, loopback |

#### Unit `pa` — `[board.unit.pa]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on this parallel port's line (CONNECT sets this): a file, socket, printer, in:/out: file, null. A WRITE latches out; a byte the far end sends is strobed into the input latch |
| `dav_bit` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit a data-available flag is jumpered onto when this port is read (§3.2.2 status/data console): 0-7, or none |
| `dav_source` | enum | `self` | `self` \| `sibling` | Whose service request the dav_bit shows: self (this port's) or sibling (the other parallel port's -- the status/data console reads the data port's flag at the status port) |
| `dav_active_low` | bool | `false` | `on` \| `off` | Present the data-available flag active-low -- the dav_bit reads 0 when a byte is waiting (§3.2.2 "D0 going low"). Off = active-high |
| `int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this parallel input's interrupt (a byte latched in, service request) is strapped on header W4: none \| int \| vi0..vi7. Canonical: vi6 (Parallel A) / vi5 (Parallel B) *(interrupt strap)* |

#### Unit `pb` — `[board.unit.pb]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on this parallel port's line (CONNECT sets this): a file, socket, printer, in:/out: file, null. A WRITE latches out; a byte the far end sends is strobed into the input latch |
| `dav_bit` | enum | `none` | `none` \| `0` \| `1` \| `2` \| `3` \| `4` \| `5` \| `6` \| `7` | Data-bus bit a data-available flag is jumpered onto when this port is read (§3.2.2 status/data console): 0-7, or none |
| `dav_source` | enum | `self` | `self` \| `sibling` | Whose service request the dav_bit shows: self (this port's) or sibling (the other parallel port's -- the status/data console reads the data port's flag at the status port) |
| `dav_active_low` | bool | `false` | `on` \| `off` | Present the data-available flag active-low -- the dav_bit reads 0 when a byte is waiting (§3.2.2 "D0 going low"). Off = active-high |
| `int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this parallel input's interrupt (a byte latched in, service request) is strapped on header W4: none \| int \| vi0..vi7. Canonical: vi6 (Parallel A) / vi5 (Parallel B) *(interrupt strap)* |


### `pmmi`

PMMI MM-103: Bell 103 modem on an S-100 card, unit 'line'. Four ports at BASE+0..3 (default C0), read/write different registers. dial=host:port places a call over TCP when the guest goes off-hook with DTR; answer=port rings the guest on an inbound call. CONNECT the line to a file, socket or real serial port instead. Pulse digits are not decoded; no interrupts

**Units:** `line` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xC0` | `0x0` .. `0xFC` | Base address -- the 6-position DIP. Four ports at BASE..BASE+3; MUST be on a 4-port boundary. Default C0 (North Star alternative E0) |
| `connect` | string | `null` | text | The endpoint on the phone line (CONNECT sets this). in:/out: a file, ... |
| `dial` | string |  | text | Originate target host:port (empty = cannot dial). SH off-hook + DTR dials it; the guest's pulse digits are not decoded |
| `answer` | string |  | text | Auto-answer TCP port (empty/0 = will not answer). DTR arms the listener; an inbound call RINGS until the guest answers with RI |
| `rtsdtr` | bool | `false` | `on` \| `off` | Mirror DTR onto RTS on a CONNECTed serial port (for cables that need RTS asserted to pass data). Default off |
| `telnet` | bool | `true` | `on` \| `off` | Speak the Telnet protocol on a dial=/answer= line, for a human telnet client (no double echo, character-at-a-time). Default on; telnet=off for a raw pipe to another simulator |
| `frame` | string | — | — | Live UART frame (read-only), e.g. 8N1. Set by OUT BA+0 bits 2-6 **(read-only — not a key you may set)** |
| `baud` | int | — | — | Live line rate (read-only), 250000/(16*N) from the OUT BA+2 divisor **(read-only — not a key you may set)** |
| `uart` | string | — | — | Live UART status (read-only). CAPITALS = asserted: TBMT DAV **(read-only — not a key you may set)** |
| `lines` | string | — | — | Live modem lines (read-only). CAPITALS = asserted: SH RI DTR ST DT RING CTS AP (DT/RING/CTS/AP from the phone line + handshake state machine) **(read-only — not a key you may set)** |


### `propio`

S100Computers Console IO Board (Parallax-Propeller console), unit 'serial'. A strap-configurable serial subtype preset to the board's documented convention: status/data at 00/01, RX-ready = status bit 1, TX-ready = status bit 2, both active high. Every strap (status_port/data_port/dav/tbmt/inverter_gate) is still overridable -- the real board is jumpered. Polled, no interrupts. CONNECT it to a file, socket, serial port, in:/out:

**Units:** `serial` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `profile` | enum | `custom` | `custom` \| `sior1` \| `sior0` \| `tuart` \| `imsai-sio2` \| `compupro-if2` \| `compupro-ss1` | Built-in card to preset the straps from: custom, or a named board. Selecting one sets status_port/data_port/bits/inverter_gate (still overridable) |
| `status_port` | int | `0x0` | `0x0` .. `0xFF` | Status(read)/control(write) port. Control writes are discarded |
| `data_port` | int | `0x1` | `0x0` .. `0xFF` | Data port: receive(read)/transmit(write) |
| `dav` | int | `1` | `0` .. `7` | Status bit (0-7) carrying DAV, data available (a byte to receive) |
| `tbmt` | int | `2` | `0` .. `7` | Status bit (0-7) carrying TBMT, transmit buffer empty (ready to send) |
| `inverter_gate` | bool | `false` | `on` \| `off` | Route both status bits through the inverter gate -- asserted DAV/TBMT read 0 (active low) |
| `baud` | int | `9600` | any | Line rate programmed onto a CONNECTed real serial port (8N1). Inert on a socket/file; does not pace the emulated line |
| `connect` | string | `null` | text | The endpoint on the serial line (CONNECT sets this): a file, socket, serial port, in:/out: file, null, loopback |


### `sbc`

SD Systems SBC-100/200: Z80 single-board computer. One 8-port block (78-7F): Intel 8251 console (unit 'tty', data 7C / status 7D, RxD->/DSR auto-baud for MSMONR21), Z80-CTC (78-7B) whose ch1 raises a mode-2 keyboard interrupt (vector 0x82) off the 8251 RxRDY, and a parallel port (7E/7F) whose OUT 7F bit 1 switches the onboard PROM out. Optional onboard boot PROM via [[board.socket]] (at+mount). variant=sbc100|sbc200

**Units:** `tty` (serial, CONNECT)

#### `[[board.socket]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `at` | int | any | Where the socket sits: a socket address of the bank (etch: E000 = monitor, F000 = disk BIOS) |
| `mount` | string | text | What is in the socket: builtin:<name> or a HEX/BIN path. Relative to THIS FILE. |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `variant` | enum | `sbc200` | `sbc100` \| `sbc200` | Which board: sbc100 (2.4576 MHz) or sbc200 (4 MHz). The console, CTC and PROM behave alike here; the CPU crystal is set on the z80 card |
| `rxd2dsr` | bool | `true` | `on` \| `off` | RxD strapped to /DSR (the SBC auto-baud jumper). Off = a plain 8251 /DSR |
| `port` | int | `0x7C` | `0x0` .. `0xFE` | Base I/O address (a card jumper). Data at BASE, status/command at BASE+1. The etch default is 7C |
| `rom_size` | enum | `2K` | `1K` \| `2K` \| `4K` \| `8K` | Size of each PROM socket (the X1 jumpers): 1K, 2K, 4K or 8K. The etch is 2K |
| `bank` | int | `3` | `0` .. `7` | Which bank the onboard memory is in (the X1 jumpers). A bank is eight sockets' worth: 0-7 for 1K, 0-3 for 2K, 0-1 for 4K, 0 for 8K. The etch is 3 (C000-FFFF) |
| `ram` | bool | `true` | `on` \| `off` | The onboard 1K static RAM is jumpered in (X3-15 to X3-16). It is the last socket's worth of the bank: F800-FFFF on the etch |
| `start` | int | `0x0` | `0x0` .. `0xF000` | Auto-start address (the X16/X17/X18 jumpers): after a reset the Z80 reads the PROM here. A multiple of 1000; 0000 = no auto-start |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. On the SBC the CTC generates it; here it paces the receive line and sizes the auto-baud bit. No free-running setting (min 50) |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this port's IRQ is jumpered: none \| int \| vi0..vi7 (decoded; not yet honored) *(interrupt strap)* |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `sio`

MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits

**Units:** `tty` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x0` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control at BASE, data at BASE+1 |
| `rev` | enum | `1` | `0` \| `1` | Board revision. 1 = the factory errata mod: ready is bit 7 (out) and bit 0 (in), both inverted. 0 = as shipped, which also reports them true-sense on bits 5 and 1 |
| `baud` | int | `9600` | `50` .. `25000` | Line rate. A JUMPER on the real card -- software cannot change it |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character. The NDB1/NDB2 pads |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits. The NSB pad: GND = 1, +V = 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | The NPB/POE pads: none \| odd \| even |
| `in_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the IN pad is soldered (RX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `out_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the OUT pad is soldered (TX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `turnkey`

MITS 8800b Turnkey Module: phantom boot PROM (FC00-FFFF), integrated 6850 SIO (unit 'tty', default 0x10), sense switches at FF, and the Auto-Start JMP jam. Sockets via [[board.socket]]

**Units:** `tty` (serial, CONNECT)

#### `[[board.socket]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `at` | int | any | Where the socket sits in the window (FC00/FD00/FE00/FF00) |
| `mount` | string | text | What is in the socket: builtin:<name> or a HEX/BIN path. Relative to THIS FILE. |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `prom` | int | `0xFC00` | `0x0` .. `0xFC00` | PROM ADDR switches: base of the 1K boot-PROM window (FC00-FFFF normal) |
| `start` | int | `0xFC00` | `0x0` .. `0xFF00` | START ADDR switches: Auto-Start jams JMP here at reset (a multiple of 256) |
| `sense` | int | `0x0` | `0x0` .. `0xFF` | Sense switches (SW6/SW7), read at port FF |
| `sio_base` | int | `0x10` | `0x0` .. `0xFE` | Base address of the integrated 6850 SIO (0x10 = 2SIO Port A) |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


## Tape

### `acr`

MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the WIND/REWIND/EXTRACT verbs and a tape counter

**Units:** `tape` (tape, MOUNT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x6` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control at BASE, data at BASE+1 |
| `rev` | enum | `1` | `0` \| `1` | Board revision. 1 = the factory errata mod: ready is bit 7 (out) and bit 0 (in), both inverted. 0 = as shipped, which also reports them true-sense on bits 5 and 1 |
| `baud` | int | `300` | `50` .. `25000` | Line rate. A JUMPER on the real card -- software cannot change it |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character. The NDB1/NDB2 pads |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits. The NSB pad: GND = 1, +V = 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | The NPB/POE pads: none \| odd \| even |
| `in_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the IN pad is soldered (RX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `out_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the OUT pad is soldered (TX): none \| int \| vi0..vi7 *(interrupt strap)* |

#### Unit `tape` — `[board.unit.tape]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `mode` | enum | `play` | `play` \| `record` | Which way the bytes go: play loads from the file, record saves to it |
| `format` | enum | `auto` | `auto` \| `raw` \| `fsk300` | How to read the mounted file: auto \| raw \| fsk300 |
| `leader` | int | `15` | `0` .. `120` | Seconds of idle tone before recorded data, when writing audio |
| `trailer` | int | `5` | `0` .. `120` | Seconds of idle tone after recorded data, when writing audio |
| `waveform` | enum | `square` | `square` \| `sine` | Carrier shape when writing audio: square (like real hardware) \| sine |
| `level` | int | `36` | `1` .. `100` | Recording level as a percent of full scale, when writing audio |
| `rate` | enum | `full` | `full` \| `real` | Playback speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `detected` | string | — | — | What the mounted tape turned out to be (empty if nothing is mounted) **(read-only — not a key you may set)** |
| `position` | string | — | — | Where the tape head is now: mm:ss / total (percent) -- read-only **(read-only — not a key you may set)** |
| `counter` | enum | `on` | `on` \| `off` | Live tape counter on the console during a load: on \| off |
| `stop` | string | `off` | off \| end \| mm:ss | Auto-stop playback at this time: off \| end \| <mm:ss> |


### `uio`

MITS 88-UIO: serial + cassette on one board. A 6850 (unit 'serial', default 0x10) and an 88-ACR cassette section (unit 'tape', default 0x06) with motor control and a SW-1 MITS/Kansas-City modulation switch. Defaults reproduce the standard 0x10 + 0x06 layout

**Units:** `tape` (tape, MOUNT), `serial` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x6` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control at BASE, data at BASE+1 |
| `rev` | enum | `1` | `0` \| `1` | Board revision. 1 = the factory errata mod: ready is bit 7 (out) and bit 0 (in), both inverted. 0 = as shipped, which also reports them true-sense on bits 5 and 1 |
| `baud` | int | `300` | `50` .. `25000` | Line rate. A JUMPER on the real card -- software cannot change it |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character. The NDB1/NDB2 pads |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits. The NSB pad: GND = 1, +V = 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | The NPB/POE pads: none \| odd \| even |
| `in_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the IN pad is soldered (RX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `out_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the OUT pad is soldered (TX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `serial_port` | int | `0x10` | `0x0` .. `0xFE` | Serial (6850) base -- SW-2. 0x10 = 2SIO Port A (default); SW-2 ON = 0x18 |
| `standard` | enum | `mits` | `mits` \| `kansas` | SW-1 tape modulation: mits (2400/1850) \| kansas (Kansas City 2400/1200) |
| `motor` | enum | — | — | Tape-recorder motor relay (guest-driven: OUT 6,127 = on, OUT 6,191 = off) **(read-only — not a key you may set)** |

#### Unit `tape` — `[board.unit.tape]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `mode` | enum | `play` | `play` \| `record` | Which way the bytes go: play loads from the file, record saves to it |
| `format` | enum | `auto` | `auto` \| `raw` \| `fsk300` | How to read the mounted file: auto \| raw \| fsk300 |
| `leader` | int | `15` | `0` .. `120` | Seconds of idle tone before recorded data, when writing audio |
| `trailer` | int | `5` | `0` .. `120` | Seconds of idle tone after recorded data, when writing audio |
| `waveform` | enum | `square` | `square` \| `sine` | Carrier shape when writing audio: square (like real hardware) \| sine |
| `level` | int | `36` | `1` .. `100` | Recording level as a percent of full scale, when writing audio |
| `rate` | enum | `full` | `full` \| `real` | Playback speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `detected` | string | — | — | What the mounted tape turned out to be (empty if nothing is mounted) **(read-only — not a key you may set)** |
| `position` | string | — | — | Where the tape head is now: mm:ss / total (percent) -- read-only **(read-only — not a key you may set)** |
| `counter` | enum | `on` | `on` \| `off` | Live tape counter on the console during a load: on \| off |
| `stop` | string | `off` | off \| end \| mm:ss | Auto-stop playback at this time: off \| end \| <mm:ss> |

#### Unit `serial` — `[board.unit.serial]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


## Parallel and printer

### `4pio`

MITS 88-4PIO: up to four 6820 PIAs, sections ja/jb.. per port. 16 ports from BASE (default 20). Software-set direction; CONNECT each section

**Units:** `ja` (serial, CONNECT), `jb` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x20` | `0x0` .. `0xF0` | Base address -- must be on a 16-address boundary. 16 ports from here |
| `ports` | int | `1` | `1` .. `4` | How many 6820 PIAs are populated (1..4 -- J, K, L, M) |

#### Unit `ja` — `[board.unit.ja]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this section (CONNECT sets this) |

#### Unit `jb` — `[board.unit.jb]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this section (CONNECT sets this) |


### `c700`

MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file, a socket, or a real printer queue

**Units:** `prn` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x2` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1 |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |
| `interrupt` | enum | `int` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the printer's interrupt is soldered: none \| int (pin 73) \| vi0..vi7 *(interrupt strap)* |
| `interrupt_after` | enum | `char` | `char` \| `crlf` | SW2 #4: raise the interrupt after every 'char' or only after a 'crlf' |


### `d7a`

Cromemco D+7A: analog + parallel I/O. Eight ports from BASE (default 18): one parallel port + seven two's-complement A/D-in/D/A-out channels. Reads 1-2 JS-1 joysticks from the host

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x18` | `0x0` .. `0xF8` | Base of the 8-port block (A7..A3 jumpers): parallel at BASE, analog at BASE+1..7. A multiple of 8; default 18 |
| `joystick1` | string | `auto` | text | Which host controller drives JS-1 console 1: 'none', 'auto' (the matching gamepad -- console 1->pad 0, console 2->pad 1 -- or the keyboard), 'keyboard', or a device index like 0 |
| `joystick2` | string | `auto` | text | Which host controller drives JS-1 console 2: 'none', 'auto' (the matching gamepad -- console 1->pad 0, console 2->pad 1 -- or the keyboard), 'keyboard', or a device index like 0 |
| `speaker1` | string | `1` | text | The analog output that JS-1 console 1's speaker is on: 'none', or a channel 1 to 7 (channel n is port BASE+n). Default 1 |
| `speaker2` | string | `3` | text | The analog output that JS-1 console 2's speaker is on: 'none', or a channel 1 to 7 (channel n is port BASE+n). Default 3 |


### `lpc`

MITS 88-LPC: 88-LP line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Line-buffered: 6-bit codes + PRINT/LINE FEED/CLEAR. CONNECT it to a file, a socket, or a real printer queue

**Units:** `prn` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x2` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1 |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `music6`

Newtech Model 6 Music Board: one write-only port into a 6-bit D/A, an amplifier and a speaker. Answers at four addresses from BASE (default 24); DO7..DO2 are latched. A program makes the sound by writing the port in a timed loop. Needs a crystal (clock_hz) to play

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x24` | `0x0` .. `0xF4` | The output port (jumpers J1..J8 set A7..A4): 04, 14, 24 ... F4. The board answers at this address and the next three. Default 24 |


### `pio`

MITS 88-PIO: 8-bit parallel port, units 'out'/'in'. Two ports at BASE+0..1 (default 04). CONNECT a printer, a keyboard, a socket

**Units:** `out` (serial, CONNECT), `in` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x4` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1 |

#### Unit `out` — `[board.unit.out]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |

#### Unit `in` — `[board.unit.in]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |


## Video

### `cadzilla`

CADzilla: an HD63484 ACRTC graphics board with a Bt453 RAMDAC and 2 MB of fixed frame memory, on a fixed VESA monitor (mode: 640x480, 800x600, 1024x768 (default)). One 8-port I/O block at BASE (default 70): ACRTC RS=0 at +0, MODE register at +1 (write-only: HSPOL/VSPOL/AMODE/OLEN), ACRTC RS=1 at +2, Bt453 at +4..+7. Draws by command through the ACRTC FIFO; wired for 8 bpp, GAI +8, single or interleaved access set by MODE AMODE. Interrupts (SW1-8) optional (interrupt=none|int|vi0..vi7). Needs a Display

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x70` | `0x0` .. `0xF8` | I/O base -- one 8-port block: BASE the ACRTC address/status, BASE+1 the MODE register, BASE+2 the ACRTC data/FIFO port, BASE+4..+7 the Bt453. A multiple of 8; default 70 |
| `mode` | enum | `1024x768` | `640x480` \| `800x600` \| `1024x768` | The monitor: a fixed-frequency VESA raster the ACRTC's picture is placed in by its HDS/VDS. 640x480, 800x600 or 1024x768 (default) |
| `draw_rate` | enum | `full` | `full` \| `real` | Drawing speed. full: each ACRTC command completes at once. real: each command takes its HD63484 data-sheet time, and the write FIFO fills as on the real board |
| `width` | string | `auto` | text | Video window width in pixels: 'auto' (default) opens about half the screen wide, or a number like 1024. The height follows the board's own aspect, and the picture is a whole multiple of its pixels so it stays crisp |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | SW1-8: the S-100 line for the ACRTC's IRQ*. none (default) disconnects it. int (pin 73) or vi0..vi7 is asserted while an enabled status flag is set *(interrupt strap)* |
| `video` | string | — | — | LIVE: whether the ACRTC is displaying -- OMR STR and DCR SE1 both set. Read-only **(read-only — not a key you may set)** |
| `picture` | string | — | — | LIVE: the size in pixels of the picture that the ACRTC is set to show, and the position of its top-left corner in the monitor's frame. Read-only **(read-only — not a key you may set)** |
| `wiring` | string | — | — | LIVE: 'ok' when the ACRTC settings agree with the board's wiring (CCR GBM 8 bpp, OMR GAI +8 words, OMR ACM the same as MODE AMODE). If not, it names the setting that is wrong, and the picture is scrambled. Read-only **(read-only — not a key you may set)** |
| `hspol` | string | — | — | LIVE: MODE register bit 0, the horizontal sync polarity. The simulator shows the value but does not use it. Read-only **(read-only — not a key you may set)** |
| `vspol` | string | — | — | LIVE: MODE register bit 1, the vertical sync polarity. The simulator shows the value but does not use it. Read-only **(read-only — not a key you may set)** |
| `amode` | string | — | — | LIVE: MODE register bit 2, the access mode of the board's fetch logic: single or interleaved. The ACRTC's OMR ACM bit must agree (see wiring). Read-only **(read-only — not a key you may set)** |
| `olen` | bool | — | — | LIVE: MODE register bit 3, overlay enable. The board does not use this bit. Read-only **(read-only — not a key you may set)** |
| `status` | int | — | — | LIVE: the ACRTC status register -- CER ARD CED LPD RFF RFR WFR WFE. Read-only **(read-only — not a key you may set)** |
| `irq` | bool | — | — | LIVE: whether IRQ* is asserted right now -- an enabled status flag pending AND the interrupt strap not 'none'. Read-only **(read-only — not a key you may set)** |


### `dazzler`

Cromemco Dazzler: color graphics from a framebuffer in main RAM. Two ports at BASE+0..1 (default 0E): control/status and format. 32x32 to 128x128, 16 colors/greys. Needs a Display

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xE` | `0x0` .. `0xFE` | I/O base port -- control/status (BASE) and format (BASE+1). Even; default 0E |
| `width` | string | `auto` | text | Video window width in pixels: 'auto' (default) opens about half the screen wide, or a number like 1024. The height follows the board's own aspect, and the picture is a whole multiple of its pixels so it stays crisp |
| `video` | string | — | — | LIVE: whether the Dazzler is displaying -- OUT BASE D7 (on/off). Read-only **(read-only — not a key you may set)** |
| `resolution` | string | — | — | LIVE: picture size in elements, decoded from the format byte (D6 X4, D5 size): 32x32, 64x64 or 128x128. Read-only **(read-only — not a key you may set)** |
| `color` | string | — | — | LIVE: color vs black-and-white -- format D4. Read-only **(read-only — not a key you may set)** |
| `size` | string | — | — | LIVE: framebuffer footprint -- format D5: 512 bytes (one quadrant) or 2 KB (four quadrants). Read-only **(read-only — not a key you may set)** |
| `base` | int | — | — | LIVE: framebuffer start address in RAM -- OUT BASE D6-D0 << 9. Read-only **(read-only — not a key you may set)** |


### `vdb8024`

SD Systems VDB-8024: an 80x24 video terminal on one board -- the video console for an SBC-100/200 (the alternative to the 8251). Two I/O ports at BASE+0..1 (default 00): status/keyboard/display. Unit 'keyboard' (CONNECT). Optional keyboard-strobe interrupt strap (interrupt=vi0..vi7) for the SBC-200's CTC to vector -- what the SD video CBIOS needs; polled by default. Boots sdmonv21. Needs a Display

**Units:** `keyboard` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x0` | `0x0` .. `0xFE` | Low I/O port: status (IN base+0) / keyboard (IN base+1) / display (OUT base+1). The real card is fixed at 00 |
| `cursor` | enum | `blink` | `off` \| `blink` \| `steady` | Cursor at the current cell: off, blink, or steady (the board default is a blinking cursor) |
| `video` | enum | `normal` | `normal` \| `reverse` | Screen video polarity: normal (light on dark) or reverse |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Keyboard-strobe interrupt strap: none = polled (default), or the S-100 VI line the keyboard raises while a byte waits (the SBC-200 CTC vectors it -- video CBIOS straps vi2) *(interrupt strap)* |
| `width` | string | `auto` | text | Video window width in pixels: 'auto' (default) opens about half the screen wide, or a number like 1024. The height follows the board's own aspect, and the picture is a whole multiple of its pixels so it stays crisp |

#### Unit `keyboard` — `[board.unit.keyboard]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of the keyboard line (CONNECT sets this) |


### `vdm1`

Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). MCM6576 character ROM; control codes 00-1F show their glyphs unless `blanking` says not. Needs a Display

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xCC00` | `0x0` .. `0xFC00` | Screen-RAM base address -- 1 KB-aligned (16x64 = 1024 bytes) |
| `port` | int | `0xCC` | `0x0` .. `0xFC` | I/O port -- scroll (OUT) / status (IN). Low two bits are zero |
| `video` | enum | `normal` | `normal` \| `reverse` | Video polarity (SW1/SW2): normal (light on dark) or reverse |
| `cursor` | enum | `blink` | `off` \| `blink` \| `steady` | Cursor for a byte with bit 7 set (SW3/SW4): off, blink, or steady |
| `blanking` | enum | `none` | `none` \| `crvt` \| `control` \| `all` | Control-code and text blanking (SW5/SW6): none = every code shows its glyph; crvt = also a CR blanks the rest of its line and a VT the rest of the screen; control = crvt, and codes 00-1F are blank; all = only cursor blocks show |
| `fill` | enum | `random` | `zero` \| `random` | Screen RAM at power-on: zero \| random (real RAM is not zeroed; 0x00 shows as a box) |
| `seed` | int | `1` | any | Seed for fill=random. The same seed fills the screen the same way at every POWER, so a run is repeatable; change it for a different pattern |
| `width` | string | `auto` | text | Video window width in pixels: 'auto' (default) opens about half the screen wide, or a number like 1024. The height follows the board's own aspect, and the picture is a whole multiple of its pixels so it stays crisp |


## Systems

### `sol`

Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one board. Seven ports F8..FE. Units serial/printer/keyboard (CONNECT) and tape1/tape2 (MOUNT). Brings the WIND/REWIND/EXTRACT verbs and a tape counter

**Units:** `serial` (serial, CONNECT), `printer` (serial, CONNECT), `keyboard` (serial, CONNECT), `tape1` (tape, MOUNT), `tape2` (tape, MOUNT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xF8` | `0x0` .. `0xF8` | Base I/O port (decodes BASE+0..6). Fixed at F8 on a real Sol-PC |

#### Unit `serial` — `[board.unit.serial]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |
| `baud` | int | `9600` | `1` .. `1000000` | Serial line speed (the strap on the Sol-PC's serial UART) |
| `data_bits` | int | `8` | `5` .. `8` | Serial word length: 8, 7, or 6 (the Sol-PC DIP) |

#### Unit `printer` — `[board.unit.printer]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |

#### Unit `keyboard` — `[board.unit.keyboard]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |

#### Unit `tape1` — `[board.unit.tape1]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `mode` | enum | `play` | `play` \| `record` | Which way the bytes go: play loads from the file, record saves to it |
| `format` | enum | `auto` | `auto` \| `raw` \| `cuts1200` \| `kcs300` | How to read the mounted file: auto \| raw \| cuts1200 \| kcs300 |
| `leader` | int | `3` | `0` .. `120` | Seconds of idle tone before recorded data, when writing audio |
| `trailer` | int | `2` | `0` .. `120` | Seconds of idle tone after recorded data, when writing audio |
| `waveform` | enum | `square` | `square` \| `sine` | Carrier shape when writing audio: square (like real hardware) \| sine |
| `level` | int | `36` | `1` .. `100` | Recording level as a percent of full scale, when writing audio |
| `rc` | int | `4000` | `1000` .. `20000` | Edge-rounding low-pass corner in Hz, when writing CUTS audio |
| `rate` | enum | `full` | `full` \| `real` | Playback speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `detected` | string | — | — | What the cassette in this deck turned out to be (empty if none) **(read-only — not a key you may set)** |
| `position` | string | — | — | Where this deck's head is now: mm:ss / total (percent) -- read-only **(read-only — not a key you may set)** |
| `counter` | enum | `on` | `on` \| `off` | Live tape counter on the console during a load: on \| off |
| `stop` | string | `off` | text | Auto-stop playback at this time: off \| end \| <mm:ss> |

#### Unit `tape2` — `[board.unit.tape2]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `mode` | enum | `play` | `play` \| `record` | Which way the bytes go: play loads from the file, record saves to it |
| `format` | enum | `auto` | `auto` \| `raw` \| `cuts1200` \| `kcs300` | How to read the mounted file: auto \| raw \| cuts1200 \| kcs300 |
| `leader` | int | `3` | `0` .. `120` | Seconds of idle tone before recorded data, when writing audio |
| `trailer` | int | `2` | `0` .. `120` | Seconds of idle tone after recorded data, when writing audio |
| `waveform` | enum | `square` | `square` \| `sine` | Carrier shape when writing audio: square (like real hardware) \| sine |
| `level` | int | `36` | `1` .. `100` | Recording level as a percent of full scale, when writing audio |
| `rc` | int | `4000` | `1000` .. `20000` | Edge-rounding low-pass corner in Hz, when writing CUTS audio |
| `rate` | enum | `full` | `full` \| `real` | Playback speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `detected` | string | — | — | What the cassette in this deck turned out to be (empty if none) **(read-only — not a key you may set)** |
| `position` | string | — | — | Where this deck's head is now: mm:ss / total (percent) -- read-only **(read-only — not a key you may set)** |
| `counter` | enum | `on` | `on` \| `off` | Live tape counter on the console during a load: on \| off |
| `stop` | string | `off` | text | Auto-stop playback at this time: off \| end \| <mm:ss> |


## PROM programmer

### `pb1`

SSM PB1: 2708/2716 EPROM programmer + on-board EPROM board. A 4K programming-socket window (default D000, sockets U22=2708/U23=2716) and one control port (default 10): OUT arms the board and picks the chip (D0=2708, D1=2716), then a window write burns a byte and a window read disarms it. Save the burn to a host hex file with `SAVE file window`. Optional read-only on-board EPROM area via [[board.prom]] (at + mount). The board ships no firmware -- run any 2708/2716 burner, such as SSM's own from the PB1 manual

**Units:** `u22` (rom, MOUNT), `u23` (rom, MOUNT)

#### `[[board.socket]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `chip` | enum | `2708` \| `2716` | Which programming socket: 2708 (U22, 1K) or 2716 (U23, 2K) |
| `mount` | string | text | A source or erased chip image to put in the socket. Relative to THIS FILE. |

#### `[[board.prom]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `at` | int | `0x8000` .. `0xFFFF` | Where the on-board EPROM sits. An address at or above 8000. |
| `mount` | string | text | The firmware image: a file (relative to THIS FILE), or builtin:<name>. |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x10` | `0x0` .. `0xF0` | Control port: an OUT here arms the board and picks 2708 (D0) or 2716 (D1). Only A4-A7 decode, so it must be an x0H address (00, 10, .. F0) |
| `window` | int | `0xD000` | `0x0` .. `0xF000` | The 4K programming-socket window, on a 4K boundary (0000, 1000, .. F000). The guest writes bytes here to burn, and reads here to disarm |


## Other

### `fp`

Altair front panel: the SENSE switches a guest reads at IN 0FFH -- a configured byte (SET fp0 sense= or TOML), not toggled here. No OUT

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `sense` | int | `0x0` | `0x0` .. `0xFF` | The SENSE switches, SA8..SA15 -- what IN 0FFH reads |


### `hostbridge`

Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN BOARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xB0` | `0x0` .. `0xFE` | Base port. Two ports: BASE+0 command/status, BASE+1 data |
| `hostdir` | string |  | text | The sandbox root. Guest names resolve here and CANNOT escape it. Empty = the shell's working directory |
| `hostdir_root` | string | — | — | LIVE: the sandbox root as RESOLVED -- the actual directory the guest is fenced into. Read-only; `hostdir` is what was written. **(read-only — not a key you may set)** |
| `readonly` | bool | `false` | `on` \| `off` | Refuse OPEN_WRITE and DELETE -- the guest may read the host, not change it |


### `rtc100`

SciTronics RTC-100: an S-100 battery-backed real-time clock/calendar (OKI MSM5832) behind a 6821 PIA. Four consecutive ports from a base that must be a multiple of 4 (port A data/direction at base+0 -- digit address in the low nibble, digit data in the HIGH nibble on a read; port A control at base+1 -- CA2 is the clock's Hold, low = stopped; port B at base+2 -- bit 0 is the Write strobe; port B control at base+3 -- CB2 is the Read line). Keeps time from the host, battery-backed across RESET, and settable by the guest. Optional once-a-second interrupt on pin 73, vectored by the card itself with RST 0-7 (the `restart` switch)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xF0` | `0x0` .. `0xFC` | Base address -- MUST BE A MULTIPLE OF 4. Four consecutive ports |
| `interrupt` | enum | `none` | `none` \| `int` | The once-a-second interrupt: none \| int (pin 73, vectored by `restart`) |
| `restart` | int | `7` | `0` .. `7` | INT switch: which RST the card jams on acknowledge, 0-7 (vector 8n). 0 and 7 are legal but commonly taken by other devices |
| `time` | string | — | — | LIVE: the date/time the MSM5832 is showing, and its offset from host time **(read-only — not a key you may set)** |


### `ss1`

CompuPro System Support 1: multifunction S-100 board. Dual 8259A interrupt controllers in a master/slave cascade (master/slave at base+0..+3; master watches VI0-6 and drives pin 73, slave takes the timer OUTs and the UART's Rx/TxRDY), an 8253 interval timer (three counters + control at base+4..+7, 2 MHz clock), the OKI MSM5832 battery-backed real-time clock/calendar (command/data at base+10/+11) and a 2651 UART serial channel (base+12..+15); base default 50H. The 9511/9512 math socket is unpopulated

**Units:** `serial` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0x50` | `0x0` .. `0xF0` | Base port of the 16-port I/O block. CompuPro standard is 50H |
| `clock` | string | — | — | LIVE: the date/time the MSM5832 is showing, and its offset from host time **(read-only — not a key you may set)** |
| `timer` | string | — | — | LIVE: the three 8253 counters -- mode, current count and OUT level **(read-only — not a key you may set)** |
| `pic` | string | — | — | LIVE: the master/slave 8259A -- IRR/ISR/IMR and the master's INT pin **(read-only — not a key you may set)** |

#### Unit `serial` — `[board.unit.serial]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `19200` | Line rate. The 2651 generates it from Mode Register 2, so the guest's MR2 write overwrites this; it seeds the line before then (min 50) |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this port's IRQ is jumpered: none \| int \| vi0..vi7. The chip raises it on RxRDY; the standard board routes it through the 8259A instead *(interrupt strap)* |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `virtc`

MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xFE` | `0x0` .. `0xFF` | Control port. 0xFE (376 octal) on the real card -- write only |
| `rtc_source` | enum | `line` | `line` \| `clock` | RTC clock source jumper: the 60 Hz line, or 10 kHz off the 2 MHz clock |
| `rtc_divide` | enum | `1` | `1` \| `10` \| `100` \| `1000` | RTC divider jumper: source frequency / 1, 10, 100 or 1000 |
| `rtc_interrupt` | enum | `none` | `none` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the RTC's interrupt ("RI") is jumpered: none \| vi0..vi7. Leave it `none` to run the PS2 package *(interrupt strap)* |
| `vi_enabled` | bool | — | — | LIVE: is the 88-VI structure enabled? (control bit 7; POC clears it) **(read-only — not a key you may set)** |
| `level_live` | bool | — | — | LIVE: is the current-level comparison in circuit? (control bit 3) **(read-only — not a key you may set)** |
| `rtc_pending` | bool | — | — | LIVE: has the RTC's interrupt flip-flop set? (cleared by control bit 4) **(read-only — not a key you may set)** |
| `current_level` | int | — | — | LIVE: the current interrupt level (control bits 0-2, ones-complement on the wire). Nothing at this level or below may interrupt while level_live **(read-only — not a key you may set)** |

