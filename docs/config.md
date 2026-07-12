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
type = "memory"
id   = "mem0"
honors_phantom = true      # A JUMPER on real cards: when another board pulls
                           #   PHANTOM* (pin 67), do I switch off? Strap this false
                           #   and a ROM shadowing you produces real bus contention —
                           #   which the simulator will report, as it should.

  [[board.region]]         # A card carries one or more POPULATED regions.
  type = "ram"             #   ram = writes are stored.  rom = writes are not decoded.
  at   = 0x0000
  size = "64K"

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
sense    = 0x00            # port 0xFF — front-panel sense switches.
                           #   NOT decorative: the DBL PROM does `IN 0FFH` at FF22
                           #   and uses bit 4 to pick the 2SIO's stop-bit setting.

startup = [                # monitor commands, run in order once the boards exist.
  "GO FF00",               #   start the DBL boot PROM. There is no BOOT command
]                          #   (DESIGN.md §10.0) — this is the operator's keystroke,
                           #   written down. Anything you can type, a config can do.

[[board]]
type = "88-cpu"
id   = "cpu0"
cpu  = "8080"

# One card, two regions: 64K of RAM, and the DBL boot PROM sitting on top of it.
# This is ONE physical card, so it is ONE board — DESIGN.md §4.2.1.
[[board]]
type    = "memory"
id      = "mem0"
phantom = "all"            # What I ASSERT over my rom regions: none | read | all.
                           #   "all" (default) -> PHANTOM* (pin 67) on reads AND writes:
                           #   the ram region beneath switches off, so reads come from
                           #   ROM and writes vanish. DBL doesn't care either way — it
                           #   copies itself to RAM at 2C00 and runs there, and never
                           #   writes to FFxx at all. ("read" is an UNSOURCED strap; see
                           #   docs/boards/memory.md.) The bus picks no winner — §4.2.
enabled = true             # runtime. A boot ROM that switches itself out after boot
                           #   (an OUT to its own port) sets this false; POWER restores it.

  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "64K"

  [[board.region]]
  type  = "rom"            # A rom region DOES NOT DECODE WRITES. It does not reject
  at    = 0xFF00           #   them — it never answers. Size comes from the image
  mount = "builtin:dbl"    #   (256 bytes -> FF00-FFFF), rounded up to a 100H page.
                           #   DBL 4.1 boot PROM, compiled in — nothing to download,
                           #   same on every OS. `SHOW ROMS` lists the built-ins and
                           #   docs/roms.md records their provenance. A bare path
                           #   ("roms/mine.bin") overrides it.
                           #   To change contents at runtime: LOAD x.hex RAW mem0 (§10.2).
                           #   The operator has a PROM burner; the guest does not.

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
  media    = "fdc8mb"      # 8in | minidisk | fdc8mb — else probed by the BOARD from file size
  readonly = false

  [[board.drive]]
  unit  = 1
  mount = "disks/scratch.dsk"

[[board]]
type    = "hostbridge"     # guest-initiated host file transfer (our own design)
id      = "host"
port    = 0xFE             # 2 ports
hostdir = "./hostfiles"    # SANDBOX. Required. Guest filenames cannot escape this root.
```

## `[machine]` keys

| Key | Meaning |
|---|---|
| `name` | Machine name, shown by `SHOW MACHINE`. |
| `clock_hz` | CPU clock. `0` = run flat out (host-idle aware). |
| `sense` | **Port 0xFF front-panel sense switches.** Read by real period software — the DBL boot PROM reads it to configure the 2SIO. Defaults to `0x00`. |
| `startup` | **A list of monitor commands, executed in order once the backplane is built.** This is how a machine starts itself: there is no `BOOT` command (`DESIGN.md` §10.0), so a config that should boot says `startup = ["GO FF00"]`. |

**`startup` makes the config language and the script language the same language.** Anything you can type at the monitor, a config file can do — and `CONFIG SAVE` round-trips the list verbatim.

> **Caution:** because `startup` runs commands, `CONFIG LOAD` on a `.toml` from an untrusted source executes whatever is in it. Keep `startup` to monitor commands, and treat a machine file like a script, because it is one.

## Memory regions

A `memory` card carries a list of **regions** — the areas that are actually populated. This is not a convenience: a real card may hold **several ROM areas and several RAM areas at once** (a PROM card is a row of sockets at F000/F400/F800/FC00, any of which may be empty), and one card must be one board or `BOARD LIST` stops describing the machine.

| Region key | Meaning |
|---|---|
| `type` | `ram` — writes are stored. `rom` — **writes are not decoded at all.** |
| `at` | Start address. Page-aligned (multiple of 100H). |
| `size` | `ram` only. `1K`…`64K`, or a hex byte count. |
| `mount` | `rom` only. **`builtin:<name>`** for a ROM compiled into the simulator, or a path to a `.hex`/`.bin` on the host. **The region's size comes from the image**, rounded up to a page. |

**`builtin:` ROMs are compiled in**, because there is no portable place to keep ROM images across macOS, Linux, and Windows. `SHOW ROMS` lists them; `docs/roms.md` records each one's source, size, and CRC32. A bare path always overrides — built-ins are a convenience, never a lock-in.

**Addresses no region covers are unpopulated** — an empty socket, a missing chip — so the board does not decode them, nothing drives the bus, and they read `0xFF`. Unpopulated RAM and an empty ROM socket are the same case, handled the same way.

**Regions are sub-units**, so the existing `id:unit` addressing reloads one: `MOUNT mem0:rom0 newdbl.hex`.

See `docs/boards/memory.md` for banking (five real cards, no two alike), `fill`, and the three PHANTOM\* straps.

## Notes

- **`id` is what monitor commands address.** Sub-units are `id:unit` — `MOUNT fdc:drive0 cpm.dsk`, `CONNECT sio2a:b socket:2323`, `SET sio2a:a BAUD=9600`, `MOUNT mem0:rom0 dbl.hex`.
- **A reset never clears RAM. Only powering off does** (`DESIGN.md` §6). `RESET` is the front-panel button; `POWER` is a power cycle, and it is the only thing that loses memory (and the only thing that re-reads ROM images from disk).
- **Port collisions are detected**, not silently resolved. The bus reports contention naming both boards and the address. `SHOW BUS IO` shows the full map; `WHO IO 0x10` is the reverse lookup.
- **Config-time vs runtime properties:** `SHOW <id>` displays which is which. Setting a config-time property on a running machine is rejected with a clear message rather than half-applied.
- **Disk geometry** is probed by the **board**, not by a host service — image size alone is meaningless without knowing which controller wrote it (`DESIGN.md` §7.3). `media` forces the choice when the size is ambiguous.
