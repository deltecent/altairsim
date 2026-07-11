# Machine configuration (TOML)

A machine is described by a TOML file. The **same state** is reachable at runtime through the monitor, and `CONFIG SAVE` writes it back out.

**The TOML keys for a board *are* its `properties()`** (see `DESIGN.md` §5) — there is no separate config schema per board. The loader and `CONFIG SAVE` are the same code path, so they cannot drift, and a board added next year is configurable the day it lands with no changes here.

## Verbs

- **`MOUNT`** refers to **host files** (disk and tape images).
- **`CONNECT`** refers to **sockets and serial ports** (and the console).

In TOML these appear as the `mount` and `connect` keys; at the monitor they are the `MOUNT` and `CONNECT` commands. They mean the same thing.

## Endpoints (for `connect`)

| Endpoint | Meaning |
|---|---|
| `console` | The host keyboard and screen. Exactly one unit may hold it. |
| `socket:2323` | Listening TCP socket — a terminal emulator connects *in*. |
| `socket:host:port` | Outbound TCP connection. |
| `serial:/dev/cu.usbserial-X` | Real host serial port (POSIX). |
| `serial:COM3` | Real host serial port (Windows). |
| `file:path` | A file, for paper tape. |
| `null` | Discard. |

## Example — a milestone-1 machine

```toml
[machine]
name     = "basic-dev"
clock_hz = 2_000_000       # 0 = run flat out (host-idle aware)

# The CPU is a board like any other (DESIGN.md §3).
[[board]]
type = "88-cpu"
id   = "cpu0"
cpu  = "8080"              # 8080 | 8085 | z80

[[board]]
type = "ram"
id   = "ram0"
base = 0x0000
size = 0x10000             # 64K
phantom = "none"           # none | read | write | both

[[board]]
type      = "88-2sio"
id        = "sio2a"
port      = 0x10           # occupies 0x10-0x13
interrupt = "int"          # none | int | vi0..vi7
                           #   "int" = pINT (pin 73). With no VI board in the machine,
                           #   the IntAck cycle floats to 0xFF = RST 7. Real Altair behavior.

  [board.unit.a]
  connect = "console"
  baud    = 9600

  [board.unit.b]
  connect = "socket:2323"
  baud    = 19200

# A second 2SIO — same type, own config, different base port.
[[board]]
type = "88-2sio"
id   = "sio2b"
port = 0x14

  [board.unit.a]
  connect = "serial:/dev/cu.usbserial-1410"
  baud    = 1200

[console]
upper    = true            # fold keyboard input to uppercase — MITS BASIC wants caps
strip7in = true
bsdel    = "del"
attn     = "ctrl-e"        # drops from the console back to the monitor
rows     = 24
cols     = 80
```

## Example — a CP/M machine (milestone 3+)

```toml
[machine]
name     = "cpm-dev"
clock_hz = 2_000_000

[[board]]
type = "88-cpu"
id   = "cpu0"
cpu  = "8080"

[[board]]
type    = "ram"
id      = "ram0"
base    = 0x0000
size    = 0xFF00
phantom = "none"

[[board]]
type    = "rom"
id      = "turnkey"
base    = 0xFF00
mount   = "roms/dbl.bin"   # DBL 4.1 boot PROM
phantom = "read"           # wins reads over the RAM beneath; writes fall through

[[board]]
type = "88-2sio"
id   = "sio2a"
port = 0x10
  [board.unit.a]
  connect = "console"

[[board]]
type   = "88-dcdd"
id     = "fdc"
port   = 0x08              # occupies 0x08-0x0A
drives = 4

  [[board.drive]]
  unit     = 0
  mount    = "disks/CPM22-8MB-56K-SIM.DSK"
  media    = "fdc8mb"      # 8in | minidisk | fdc8mb — else probed from file size
  readonly = false

  [[board.drive]]
  unit  = 1
  mount = "disks/scratch.dsk"

[[board]]
type    = "simh"           # host file transfer: R.COM / W.COM
id      = "simh"
port    = 0xFE
hostdir = "./hostfiles"    # SANDBOX. Guest filenames cannot escape this root.
```

## Notes

- **`id` is what monitor commands address.** Sub-units are `id:unit` — `MOUNT fdc:0 cpm.dsk`, `CONNECT sio2a:b socket:2323`, `SET sio2a:a BAUD=9600`.
- **Port collisions are detected**, not silently resolved. The bus reports contention naming both boards and the address. `SHOW BUS IO` shows the full map; `WHO IO 0x10` is the reverse lookup. (Note the 88-VI's default port must **not** be 0xFE — that is the SIMH device.)
- **Config-time vs runtime properties:** `SHOW <id>` displays which is which. Setting a config-time property on a running machine is rejected with a clear message rather than half-applied.
- **Disk geometry** is probed from the image file size by the `BlockDevice` service; `media` forces it.
