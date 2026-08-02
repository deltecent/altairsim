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
| [`8080`](#8080) | MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus |
| [`z80`](#z80) | Generic Z80 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core |

**Memory**

| Type | What it is |
|---|---|
| [`memory`](#memory) | RAM/ROM board: a list of regions, PHANTOM*, and five banking schemes |

**Disk**

| Type | What it is |
|---|---|
| [`16fdc`](#16fdc) | Cromemco 16FDC: WD FD1793 soft-sector floppy (single + double density), up to 4 drives. Disk registers at 30-34, a TMS 5501 console UART at 00-09 (unit 'tty'), and a 4K RDOS 2.52 boot PROM at C000 (OUT 40H banks it out, RESET restores it). Boots CDOS |
| [`64fdc`](#64fdc) | Cromemco 64FDC: the 16FDC's 1983 successor -- same FD1793 + TMS 5501, carrying an 8K RDOS 3.12 boot PROM at C000-DFFF (OUT 40H banks it out, RESET restores it). Boots CDOS |
| [`dcdd`](#dcdd) | MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits |
| [`hdsk`](#hdsk) | MITS 88-HDSK Datakeeper: Pertec hard disk, 256-byte sectors from a linear .DSK. Eight ports at BASE+0..7 (default A0). Command/handshake protocol, four page buffers |
| [`mds`](#mds) | MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s |
| [`tarbell`](#tarbell) | Tarbell #1011: single-density WD FD1771 floppy, up to 4 drives. Eight ports at BASE+0..7 (default F8). Carries a 32-byte boot PROM that shadows 0000 over PHANTOM* -- boots CP/M automatically at reset (bootstrap=on) |
| [`tarbelldd`](#tarbelldd) | Tarbell #2022: double-density WD FD1791 floppy (mixed-density media, SD track 0), up to 4 drives. The single-density card's twin with a bitmap OUT-FC latch and a port-FD DMA/ext-addr register. Same 32-byte boot PROM |
| [`versafloppy`](#versafloppy) | SD Systems VersaFloppy I/II: WD FD177x soft-sector floppy, up to 4 drives. Eight ports at BASE+0..7 (default 60). variant=vfi (FD1771, single density) \| vfii (FD1791, single+double). Boots SDOS with the SBC-200 + DDBIOS |

**Serial**

| Type | What it is |
|---|---|
| [`2sio`](#2sio) | MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3 |
| [`pmmi`](#pmmi) | PMMI MM-103: Bell 103 modem on an S-100 card, unit 'line'. Four ports at BASE+0..3 (default C0), read/write different registers. Transmit/receive over a ByteStream; CONNECT it to in:/out: files. No dialer; modem status is a fixed stub |
| [`sbc`](#sbc) | SD Systems SBC-100/200: Z80 single-board computer. One 8-port block (78-7F): Intel 8251 console (unit 'tty', data 7C / status 7D, RxD->/DSR auto-baud for MSMONR21), Z80-CTC (78-7B) whose ch1 raises a mode-2 keyboard interrupt (vector 0x82) off the 8251 RxRDY, and a parallel port (7E/7F) whose OUT 7F bit 1 switches the onboard PROM out. Optional onboard boot PROM via [[board.socket]] (at+mount). variant=sbc100\|sbc200 |
| [`sio`](#sio) | MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits |
| [`turnkey`](#turnkey) | MITS 8800b Turnkey Module: phantom boot PROM (FC00-FFFF), integrated 6850 SIO (unit 'tty', default 0x10), sense switches at FF, and the Auto-Start JMP jam. Sockets via [[board.socket]] |
| [`usio`](#usio) | Universal Serial board: a UART-agnostic serial card, unit 'serial'. Two ports you strap: a status/control port (status_port -- read synthesizes RDR/TDRE at bit positions you pick, write is discarded) and a data port (data_port). Built-in profiles preset the straps: profile=tuart (Cromemco TU-ART) \| imsai-sio2 \| compupro-if2 (CompuPro Interfacer II) \| compupro-ss1 (CompuPro System Support 1). Polled, no interrupts. CONNECT it to a file, socket, serial port, in:/out: |

**Tape**

| Type | What it is |
|---|---|
| [`acr`](#acr) | MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the WIND/REWIND/EXTRACT verbs and a tape counter |
| [`uio`](#uio) | MITS 88-UIO: serial + cassette on one board. A 6850 (unit 'serial', default 0x10) and an 88-ACR cassette section (unit 'tape', default 0x06) with motor control and a SW-1 MITS/Kansas-City modulation switch. Defaults reproduce the standard 0x10 + 0x06 layout |

**Parallel and printer**

| Type | What it is |
|---|---|
| [`4pio`](#4pio) | MITS 88-4PIO: up to four 6820 PIAs, sections ja/jb.. per port. 16 ports from BASE (default 20). Software-set direction; CONNECT each section |
| [`c700`](#c700) | MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file, a socket, or a real printer queue |
| [`d7a`](#d7a) | Cromemco D+7A: analog + parallel I/O. Eight ports from BASE (default 18): one parallel port + seven two's-complement A/D-in/D/A-out channels. Reads 1-2 JS-1 joysticks from the host |
| [`lpc`](#lpc) | MITS 88-LPC: 88-LP line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Line-buffered: 6-bit codes + PRINT/LINE FEED/CLEAR. CONNECT it to a file, a socket, or a real printer queue |
| [`pio`](#pio) | MITS 88-PIO: 8-bit parallel port, units 'out'/'in'. Two ports at BASE+0..1 (default 04). CONNECT a printer, a keyboard, a socket |

**Video**

| Type | What it is |
|---|---|
| [`dazzler`](#dazzler) | Cromemco Dazzler: color graphics from a framebuffer in main RAM. Two ports at BASE+0..1 (default 0E): control/status and format. 32x32 to 128x128, 16 colors/greys. Needs a Display |
| [`vdb8024`](#vdb8024) | SD Systems VDB-8024: an 80x24 video terminal on one board -- the video console for an SBC-100/200 (the alternative to the 8251). Two I/O ports at BASE+0..1 (default 00): status/keyboard/display. Unit 'keyboard' (CONNECT). Optional keyboard-strobe interrupt strap (interrupt=vi0..vi7) for the SBC-200's CTC to vector -- what the SD video CBIOS needs; polled by default. Boots sdmonv21. Needs a Display |
| [`vdm1`](#vdm1) | Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display |

**Systems**

| Type | What it is |
|---|---|
| [`sol`](#sol) | Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one board. Seven ports F8..FE. Units serial/printer/keyboard (CONNECT) and tape1/tape2 (MOUNT). Brings the WIND/REWIND/EXTRACT verbs and a tape counter |

**Other**

| Type | What it is |
|---|---|
| [`fp`](#fp) | Altair front panel: the address switches, SA0..SA15. The top eight double as the SENSE switches, which IN 0FFH reads -- the panel answers no OUT |
| [`hostbridge`](#hostbridge) | Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN BOARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM |
| [`virtc`](#virtc) | MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE |


## CPU

### `8080`

MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus

**Units:** `8080` (cpu)

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

### `memory`

RAM/ROM board: a list of regions, PHANTOM*, and five banking schemes

#### `[[board.region]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `type` | enum | `ram` \| `rom` | RAM, or ROM (which needs a `mount`, unless you want an empty socket) |
| `at` | int | `0x0` .. `0xFFFF` | Where it starts. An address: 0000, F800 |
| `size` | int | `1` .. `65536` | How much. Decimal, and it takes a suffix: 48K, 1024, 2M |
| `mount` | string | text | The ROM image. A file (relative to THIS FILE), or builtin:<name> |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `honors_phantom` | enum | `all` | `none` \| `read` \| `all` | A JUMPER. Another board pulls PHANTOM* -- do I switch off? none \| read \| all |
| `phantom` | enum | `all` | `none` \| `read` \| `all` | What I ASSERT over my rom regions: none \| read \| all |
| `bank_type` | enum | `none` | `none` \| `eram` \| `vram` \| `cram` \| `hram` \| `b810` | none\|eram\|vram\|cram\|hram\|b810 -- five real cards, no two alike |
| `banks` | int | — | — | how many banks this board has. The board decides: it follows bank_type **(read-only — not a key you may set)** |
| `bank` | int | `0` | `0` .. `15` | The live bank. 0 .. banks-1, and `banks` follows bank_type -- a board with one bank takes only 0 |
| `fill` | enum | `random` | `zero` \| `random` | RAM contents at power-on: zero \| random (real RAM is not zeroed) |
| `seed` | int | `1` | any | Seed for fill=random. The same seed fills RAM the same way at every POWER, so a run is repeatable; change it for a different junk pattern |
| `pages` | string | — | — | the composite page map -- which pages this board answers for. Derived from the regions you declared **(read-only — not a key you may set)** |


## Disk

### `16fdc`

Cromemco 16FDC: WD FD1793 soft-sector floppy (single + double density), up to 4 drives. Disk registers at 30-34, a TMS 5501 console UART at 00-09 (unit 'tty'), and a 4K RDOS 2.52 boot PROM at C000 (OUT 40H banks it out, RESET restores it). Boots CDOS

**Units:** `tty` (serial), `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (A-D, one-hot select DS4-DS1) |

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

**Units:** `tty` (serial), `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (A-D, one-hot select DS4-DS1) |

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

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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


### `hdsk`

MITS 88-HDSK Datakeeper: Pertec hard disk, 256-byte sectors from a linear .DSK. Eight ports at BASE+0..7 (default A0). Command/handshake protocol, four page buffers

**Units:** `drive0` (disk)

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


### `mds`

MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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


### `tarbell`

Tarbell #1011: single-density WD FD1771 floppy, up to 4 drives. Eight ports at BASE+0..7 (default F8). Carries a 32-byte boot PROM that shadows 0000 over PHANTOM* -- boots CP/M automatically at reset (bootstrap=on)

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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


### `tarbelldd`

Tarbell #2022: double-density WD FD1791 floppy (mixed-density media, SD track 0), up to 4 drives. The single-density card's twin with a bitmap OUT-FC latch and a port-FD DMA/ext-addr register. Same 32-byte boot PROM

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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


### `versafloppy`

SD Systems VersaFloppy I/II: WD FD177x soft-sector floppy, up to 4 drives. Eight ports at BASE+0..7 (default 60). variant=vfi (FD1771, single density) | vfii (FD1791, single+double). Boots SDOS with the SBC-200 + DDBIOS

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

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


## Serial

### `2sio`

MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3

**Units:** `a` (serial), `b` (serial)

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


### `pmmi`

PMMI MM-103: Bell 103 modem on an S-100 card, unit 'line'. Four ports at BASE+0..3 (default C0), read/write different registers. Transmit/receive over a ByteStream; CONNECT it to in:/out: files. No dialer; modem status is a fixed stub

**Units:** `line` (serial)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xC0` | `0x0` .. `0xFC` | Base address -- the 6-position DIP. Four ports at BASE..BASE+3; MUST be on a 4-port boundary. Default C0 (North Star alternative E0) |
| `connect` | string | `null` | text | The endpoint on the phone line (CONNECT sets this). in:/out: a file, ... |
| `dial` | string |  | text | Originate target host:port (empty = cannot dial). SH off-hook + DTR dials it; the guest's pulse digits are not decoded |
| `answer` | string |  | text | Auto-answer TCP port (empty/0 = will not answer). DTR arms the listener; an inbound call RINGS until the guest answers with RI |
| `rtsdtr` | bool | `false` | `on` \| `off` | Mirror DTR onto RTS on a CONNECTed serial port (for cables that need RTS asserted to pass data). Default off |
| `frame` | string | — | — | Live UART frame (read-only), e.g. 8N1. Set by OUT BA+0 bits 2-6 **(read-only — not a key you may set)** |
| `baud` | int | — | — | Live line rate (read-only), 250000/(16*N) from the OUT BA+2 divisor **(read-only — not a key you may set)** |
| `uart` | string | — | — | Live UART status (read-only). CAPITALS = asserted: TBMT DAV **(read-only — not a key you may set)** |
| `lines` | string | — | — | Live modem lines (read-only). CAPITALS = asserted: SH RI DTR ST DT RING CTS AP (DT/RING/CTS/AP from the phone line + handshake state machine) **(read-only — not a key you may set)** |


### `sbc`

SD Systems SBC-100/200: Z80 single-board computer. One 8-port block (78-7F): Intel 8251 console (unit 'tty', data 7C / status 7D, RxD->/DSR auto-baud for MSMONR21), Z80-CTC (78-7B) whose ch1 raises a mode-2 keyboard interrupt (vector 0x82) off the 8251 RxRDY, and a parallel port (7E/7F) whose OUT 7F bit 1 switches the onboard PROM out. Optional onboard boot PROM via [[board.socket]] (at+mount). variant=sbc100|sbc200

**Units:** `tty` (serial)

#### `[[board.socket]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `at` | int | any | Where the socket sits in the onboard window (E000 = monitor, F000 = disk BIOS) |
| `mount` | string | text | What is in the socket: builtin:<name> or a HEX/BIN path. Relative to THIS FILE. |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `variant` | enum | `sbc200` | `sbc100` \| `sbc200` | Which board: sbc100 (2.4576 MHz) or sbc200 (4 MHz). The console, CTC and PROM behave alike here; the CPU crystal is set on the z80 card |
| `rxd2dsr` | bool | `true` | `on` \| `off` | RxD strapped to /DSR (the SBC auto-baud jumper). Off = a plain 8251 /DSR |
| `port` | int | `0x7C` | `0x0` .. `0xFE` | Base I/O address (a card jumper). Data at BASE, status/command at BASE+1. The etch default is 7C |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. On the SBC the CTC generates it; here it paces the receive line and sizes the auto-baud bit. No free-running setting (min 50) |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this port's IRQ is jumpered: none \| int \| vi0..vi7 (decoded; not yet honored) *(interrupt strap)* |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `sio`

MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits

**Units:** `tty` (serial)

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

**Units:** `tty` (serial)

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


### `usio`

Universal Serial board: a UART-agnostic serial card, unit 'serial'. Two ports you strap: a status/control port (status_port -- read synthesizes RDR/TDRE at bit positions you pick, write is discarded) and a data port (data_port). Built-in profiles preset the straps: profile=tuart (Cromemco TU-ART) | imsai-sio2 | compupro-if2 (CompuPro Interfacer II) | compupro-ss1 (CompuPro System Support 1). Polled, no interrupts. CONNECT it to a file, socket, serial port, in:/out:

**Units:** `serial` (serial)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `profile` | enum | `custom` | `custom` \| `tuart` \| `imsai-sio2` \| `compupro-if2` \| `compupro-ss1` | Built-in card to preset the straps from: custom, or a named board. Selecting one sets status_port/data_port/bits/polarity (still overridable) |
| `status_port` | int | `0x0` | `0x0` .. `0xFF` | Status(read)/control(write) port. Control writes are discarded |
| `data_port` | int | `0x1` | `0x0` .. `0xFF` | Data port: receive(read)/transmit(write) |
| `rdr_bit` | int | `0` | `0` .. `7` | Status bit (0-7) that signals receive data ready |
| `tdre_bit` | int | `1` | `0` .. `7` | Status bit (0-7) that signals transmit data empty |
| `rdr_active_low` | bool | `false` | `on` \| `off` | Invert the receive-data-ready bit (asserted reads 0) |
| `tdre_active_low` | bool | `false` | `on` \| `off` | Invert the transmit-data-empty bit (asserted reads 0) |
| `baud` | int | `9600` | any | Line rate programmed onto a CONNECTed real serial port (8N1). Inert on a socket/file; does not pace the emulated line |
| `connect` | string | `null` | text | The endpoint on the serial line (CONNECT sets this): a file, socket, serial port, in:/out: file, null, loopback |


## Tape

### `acr`

MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the WIND/REWIND/EXTRACT verbs and a tape counter

**Units:** `tape` (tape)

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

**Units:** `tape` (tape), `serial` (serial)

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

**Units:** `ja` (serial), `jb` (serial)

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

**Units:** `prn` (serial)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x2` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1 |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `d7a`

Cromemco D+7A: analog + parallel I/O. Eight ports from BASE (default 18): one parallel port + seven two's-complement A/D-in/D/A-out channels. Reads 1-2 JS-1 joysticks from the host

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x18` | `0x0` .. `0xF8` | Base of the 8-port block (A7..A3 jumpers): parallel at BASE, analog at BASE+1..7. A multiple of 8; default 18 |
| `joystick1` | string | `auto` | text | Which host controller drives JS-1 console 1: 'none', 'auto' (gamepad 0 or the keyboard), 'keyboard', or a device index like 0 |
| `joystick2` | string | `none` | text | Which host controller drives JS-1 console 2: 'none', 'auto' (gamepad 0 or the keyboard), 'keyboard', or a device index like 0 |
| `js1_invert_y` | bool | `false` | `on` \| `off` | Flip console 1's Y axis (a stick whose pot opposes the host's up=negative) |
| `js2_invert_y` | bool | `false` | `on` \| `off` | Flip console 2's Y axis |


### `lpc`

MITS 88-LPC: 88-LP line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Line-buffered: 6-bit codes + PRINT/LINE FEED/CLEAR. CONNECT it to a file, a socket, or a real printer queue

**Units:** `prn` (serial)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x2` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1 |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `pio`

MITS 88-PIO: 8-bit parallel port, units 'out'/'in'. Two ports at BASE+0..1 (default 04). CONNECT a printer, a keyboard, a socket

**Units:** `out` (serial), `in` (serial)

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

**Units:** `keyboard` (serial)

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

Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xCC00` | `0x0` .. `0xFC00` | Screen-RAM base address -- 1 KB-aligned (16x64 = 1024 bytes) |
| `port` | int | `0xCC` | `0x0` .. `0xFC` | I/O port -- scroll (OUT) / status (IN). Low two bits are zero |
| `video` | enum | `normal` | `normal` \| `reverse` | Video polarity (SW1/SW2): normal (light on dark) or reverse |
| `cursor` | enum | `blink` | `off` \| `blink` \| `steady` | Cursor for a byte with bit 7 set (SW3/SW4): off, blink, or steady |
| `width` | string | `auto` | text | Video window width in pixels: 'auto' (default) opens about half the screen wide, or a number like 1024. The height follows the board's own aspect, and the picture is a whole multiple of its pixels so it stays crisp |


## Systems

### `sol`

Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one board. Seven ports F8..FE. Units serial/printer/keyboard (CONNECT) and tape1/tape2 (MOUNT). Brings the WIND/REWIND/EXTRACT verbs and a tape counter

**Units:** `serial` (serial), `printer` (serial), `keyboard` (serial), `tape1` (tape), `tape2` (tape)

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


## Other

### `fp`

Altair front panel: the address switches, SA0..SA15. The top eight double as the SENSE switches, which IN 0FFH reads -- the panel answers no OUT

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

