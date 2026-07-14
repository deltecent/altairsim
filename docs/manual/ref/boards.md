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
| [`2sio`](#2sio) | MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3 |
| [`sio`](#sio) | MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits |
| [`dcdd`](#dcdd) | MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits |
| [`mds`](#mds) | MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s |
| [`acr`](#acr) | MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the REWIND verb |
| [`fp`](#fp) | Altair front panel: the SENSE switches at port FF (read-only), and the lamps |
| [`virtc`](#virtc) | MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE |
| [`hostbridge`](#hostbridge) | Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN CARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM |


## `memory`

RAM/ROM card: a list of regions, PHANTOM*, and five banking schemes

**Takes a `[[board.region]]` list.**

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `honors_phantom` | enum | `all` | `none` \| `read` \| `all` | A JUMPER. Another board pulls PHANTOM* -- do I switch off? none \| read \| all |
| `phantom` | enum | `all` | `none` \| `read` \| `all` | What I ASSERT over my rom regions: none \| read \| all |
| `bank_type` | enum | `none` | `none` \| `eram` \| `vram` \| `cram` \| `hram` \| `b810` | none\|eram\|vram\|cram\|hram\|b810 -- five real cards, no two alike |
| `banks` | int | `1` | — | how many banks this card has. The card decides: it follows bank_type **(read-only — not a key you may set)** |
| `bank` | int | `0` | `0` .. `15` | The live bank |
| `fill` | enum | `random` | `zero` \| `random` | RAM contents at power-on: zero \| random (real RAM is not zeroed) |
| `seed` | int | `1` | any | Seed for fill=random. Goes in the snapshot, or replay is dead. |
| `pages` | string | `(none)` | — | the composite page map -- which pages this card answers for. Derived from the regions you declared **(read-only — not a key you may set)** |


## `8080`

MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus

**Units:** `8080` (cpu)

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the card. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |


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
| `lines` | string | `DCD CTS RTS brk` | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |

### Unit `b` — `[board.unit.b]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where this channel's IRQ is jumpered: none \| int \| vi0..vi7 *(interrupt strap)* |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | `DCD CTS RTS brk` | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
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

**Takes a `[[board.drive]]` list.**

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0x8` | `0x0` .. `0xFD` | Base address. The card decodes three ports: BASE+0 .. BASE+2 |
| `drives` | int | `4` | `1` .. `16` | Drives on the daisy chain |
| `interrupt` | enum | `none` | `none` \| `int` \| `vi0` \| `vi1` \| `vi2` \| `vi3` \| `vi4` \| `vi5` \| `vi6` \| `vi7` | Where the card's interrupt is soldered *(interrupt strap)* |


## `mds`

MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s

**Units:** `drive0` (disk), `drive1` (disk), `drive2` (disk), `drive3` (disk)

**Takes a `[[board.drive]]` list.**

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
| `vi_enabled` | bool | `false` | — | LIVE: is the 88-VI structure enabled? (control bit 7; POC clears it) **(read-only — not a key you may set)** |
| `level_live` | bool | `false` | — | LIVE: is the current-level comparison in circuit? (control bit 3) **(read-only — not a key you may set)** |
| `rtc_pending` | bool | `false` | — | LIVE: has the RTC's interrupt flip-flop set? (cleared by control bit 4) **(read-only — not a key you may set)** |
| `current_level` | int | `0` | — | LIVE: the current interrupt level (control bits 0-2, ones-complement on the wire). Nothing at this level or below may interrupt while level_live **(read-only — not a key you may set)** |


## `hostbridge`

Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN CARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM

### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `port` | int | `0xB0` | `0x0` .. `0xFE` | Base port. Two ports: BASE+0 command/status, BASE+1 data |
| `hostdir` | string |  | text | The sandbox root. Guest names resolve here and CANNOT escape it. Empty = the shell's working directory |
| `readonly` | bool | `false` | `on` \| `off` | Refuse OPEN_WRITE and DELETE -- the guest may read the host, not change it |

