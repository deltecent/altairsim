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

| Type | What it is |
|---|---|
| [`memory`](#memory) | RAM/ROM card: a list of regions, PHANTOM*, and five banking schemes |
| [`8080`](#8080) | MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus |
| [`z80`](#z80) | Generic Z80 CPU card. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core |
| [`2sio`](#2sio) | MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3 |
| [`sio`](#sio) | MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits |
| [`dcdd`](#dcdd) | MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits |
| [`mds`](#mds) | MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s |
| [`acr`](#acr) | MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the REWIND verb |
| [`c700`](#c700) | MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file |
| [`vdm1`](#vdm1) | Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display |
| [`sol`](#sol) | Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one card. Seven ports F8..FE. Units serial/printer/tape/keyboard |
| [`fp`](#fp) | Altair front panel: the SENSE switches at port FF (read-only), and the lamps |
| [`virtc`](#virtc) | MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE |
| [`hostbridge`](#hostbridge) | Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN CARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM |


## `memory`

RAM/ROM card: a list of regions, PHANTOM*, and five banking schemes

### `[[board.region]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `type` | enum | `ram` \| `rom` | RAM, or ROM (which needs a `mount`, unless you want an empty socket) |
| `at` | int | `0x0` .. `0xFFFF` | Where it starts. An address: 0000, F800 |
| `size` | int | `1` .. `65536` | How much. Decimal, and it takes a suffix: 48K, 1024, 2M |
| `mount` | string | text | The ROM image. A file (relative to THIS FILE), or builtin:<name> |

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `honors_phantom` | enum | `all` | `none` \| `read` \| `all` | A JUMPER. Another board pulls PHANTOM* -- do I switch off? none \| read \| all |
| `phantom` | enum | `all` | `none` \| `read` \| `all` | What I ASSERT over my rom regions: none \| read \| all |
| `bank_type` | enum | `none` | `none` \| `eram` \| `vram` \| `cram` \| `hram` \| `b810` | none\|eram\|vram\|cram\|hram\|b810 -- five real cards, no two alike |
| `banks` | int | — | — | how many banks this card has. The card decides: it follows bank_type **(read-only — not a key you may set)** |
| `bank` | int | `0` | `0` .. `15` | The live bank |
| `fill` | enum | `random` | `zero` \| `random` | RAM contents at power-on: zero \| random (real RAM is not zeroed) |
| `seed` | int | `1` | any | Seed for fill=random. Goes in the snapshot, or replay is dead. |
| `pages` | string | — | — | the composite page map -- which pages this card answers for. Derived from the regions you declared **(read-only — not a key you may set)** |


## `8080`

MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus

**Units:** `8080` (cpu)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the card. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: T-states per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


## `z80`

Generic Z80 CPU card. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core

**Units:** `z80` (cpu)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the card. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: T-states per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


## `2sio`

MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3

**Units:** `a` (serial), `b` (serial)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x10` | `0x0` .. `0xFC` | Base address. The card decodes four ports: BASE+0 .. BASE+3 |

### Unit `a` — `[board.unit.a]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |

### Unit `b` — `[board.unit.b]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


## `sio`

MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits

**Units:** `tty` (serial)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x0` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control at BASE, data at BASE+1 |
| `rev` | enum | `1` | `0` \| `1` | Board revision. 1 = the errata mod done at the factory (see the .md) |
| `baud` | int | `9600` | `50` .. `25000` | Line rate. A JUMPER on the real card -- software cannot change it |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character. The NDB1/NDB2 pads |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits. The NSB pad: GND = 1, +V = 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | The NPB/POE pads: none \| odd \| even |
| `in_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the IN pad is soldered (RX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `out_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the OUT pad is soldered (TX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


## `dcdd`

MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive on the daisy chain |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | The write-protect tab. The DRIVE senses it, so the guest is never told: it writes, the head is inhibited, the bytes go nowhere |
| `media` | enum | `8in` \| `fdc8mb` | Force the format instead of probing the image's size |

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x8` | `0x0` .. `0xFD` | Base address. The card decodes three ports: BASE+0 .. BASE+2 |
| `drives` | int | `4` | `1` .. `16` | Drives on the daisy chain |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |


## `mds`

MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive on the daisy chain |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | The write-protect tab. The DRIVE senses it, so the guest is never told: it writes, the head is inhibited, the bytes go nowhere |
| `media` | enum | `minidisk` | Force the format instead of probing the image's size |

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x8` | `0x0` .. `0xFD` | Base address. The card decodes three ports: BASE+0 .. BASE+2 |
| `drives` | int | `4` | `1` .. `4` | Drives on the daisy chain |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |
| `motor` | enum | `free` | `free` \| `real` | free: always at speed (default). real: 1 s spin-up, and it stops after 6.4 s |


## `acr`

MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the REWIND verb

**Units:** `tape` (tape)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x6` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control at BASE, data at BASE+1 |
| `rev` | enum | `1` | `0` \| `1` | Board revision. 1 = the errata mod done at the factory (see the .md) |
| `baud` | int | `300` | `50` .. `25000` | Line rate. A JUMPER on the real card -- software cannot change it |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character. The NDB1/NDB2 pads |
| `stop_bits` | int | `1` | `1` .. `2` | Stop bits. The NSB pad: GND = 1, +V = 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | The NPB/POE pads: none \| odd \| even |
| `in_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the IN pad is soldered (RX): none \| int \| vi0..vi7 *(interrupt strap)* |
| `out_int` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the OUT pad is soldered (TX): none \| int \| vi0..vi7 *(interrupt strap)* |

### Unit `tape` — `[board.unit.tape]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `mode` | enum | `play` | `play` \| `record` | The button that is down on the recorder: play \| record |


## `c700`

MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file

**Units:** `prn` (serial)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x2` | `0x0` .. `0xFE` | Base address -- MUST BE EVEN. Control/status at BASE, data at BASE+1 |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


## `vdm1`

Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xCC00` | `0x0` .. `0xFC00` | Screen-RAM base address -- 1 KB-aligned (16x64 = 1024 bytes) |
| `port` | int | `0xCC` | `0x0` .. `0xFC` | I/O port -- scroll (OUT) / status (IN). Low two bits are zero |
| `video` | enum | `normal` | `normal` \| `reverse` | Video polarity (SW1/SW2): normal (light on dark) or reverse |
| `cursor` | enum | `blink` | `off` \| `blink` \| `steady` | Cursor for a byte with bit 7 set (SW3/SW4): off, blink, or steady |


## `sol`

Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one card. Seven ports F8..FE. Units serial/printer/tape/keyboard

**Units:** `serial` (serial), `printer` (serial), `tape` (serial), `keyboard` (serial)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xF8` | `0x0` .. `0xF8` | Base I/O port (decodes BASE+0..6). Fixed at F8 on a real Sol-PC |

### Unit `serial` — `[board.unit.serial]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |
| `baud` | int | `9600` | `1` .. `1000000` | Serial line speed (the strap on the Sol-PC's serial UART) |
| `data_bits` | int | `8` | `5` .. `8` | Serial word length: 8, 7, or 6 (the Sol-PC DIP) |

### Unit `printer` — `[board.unit.printer]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |

### Unit `tape` — `[board.unit.tape]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |

### Unit `keyboard` — `[board.unit.keyboard]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this line (CONNECT sets this) |


## `fp`

Altair front panel: the SENSE switches at port FF (read-only), and the lamps

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `sense` | int | `0x0` | `0x0` .. `0xFF` | The SENSE switches, SA8..SA15 -- what IN 0FFH reads |
| `data` | int | `0x0` | `0x0` .. `0xFF` | The DATA switches, SA0..SA7. Not readable by the guest -- see the .md |


## `virtc`

MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE

### Board properties

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


## `hostbridge`

Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN CARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xB0` | `0x0` .. `0xFE` | Base port. Two ports: BASE+0 command/status, BASE+1 data |
| `hostdir` | string |  | text | The sandbox root. Guest names resolve here and CANNOT escape it. Empty = the shell's working directory |
| `hostdir_root` | string | — | — | LIVE: the sandbox root as RESOLVED -- the actual directory the guest is fenced into. Read-only; `hostdir` is what was written. **(read-only — not a key you may set)** |
| `readonly` | bool | `false` | `on` \| `off` | Refuse OPEN_WRITE and DELETE -- the guest may read the host, not change it |

