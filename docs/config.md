# Machine configuration (TOML)

A machine is described by a TOML file. The **same state** is reachable at runtime through the monitor, and `CONFIG SAVE` writes it back out.

**The TOML keys for a board *are* its `properties()`** (see `DESIGN.md` §5) — there is no separate config schema per board. The loader and `CONFIG SAVE` are the same code path, so they cannot drift, and a board added next year is configurable the day it lands with no changes here.

## Verbs

- **`MOUNT`** refers to **host files** (disk and tape images).
- **`CONNECT`** refers to **sockets and serial ports** (and the console).

In TOML these appear as the `mount` and `connect` keys; at the monitor they are the `MOUNT` and `CONNECT` commands. They mean the same thing.

## Endpoints (for `connect`)

| Endpoint | Meaning | |
|---|---|---|
| `console` | The host keyboard and screen. Exactly one unit may hold it — connecting a second **steals** it, and says who from. | **built** |
| `null` | Discard. What an unconnected unit is bound to, which is why an unconnected line is not an error. | **built** |
| `loopback` | A jumper between TX and RX. The guest hears itself. | **built** |
| `socket:2323` | Listening TCP socket — a terminal emulator connects *in*. | not yet |
| `socket:host:port` | Outbound TCP connection. | not yet |
| `serial:/dev/cu.usbserial-X` | Real host serial port (POSIX). | not yet |
| `serial:COM3` | Real host serial port (Windows). | not yet |
| `file:path` | A file, for paper tape. | not yet |

Asking for one that is not built yet **says so by name**, rather than failing as though you had mistyped it.

## Example — the machine that exists today

This is `machines/altmon.toml`, and it runs: `altairsim altmon`.

```toml
[machine]
name    = "altmon"
startup = ["RUN F800"]     # anything you can type, a config can do

# The front panel is a board too, and the SENSE switches are on it -- SA8..SA15,
# which the guest reads at port FF. They are NOT a [machine] key; see below.
[[board]]
type  = "fp"
id    = "fp0"
sense = 0x00

# The CPU is a board like any other (DESIGN.md §3) -- and THE CRYSTAL IS ON IT,
# which is why clock_hz is this board's property and NOT the machine's.
[[board]]
type     = "8080"
id       = "cpu0"
clock_hz = 0               # 0 = run flat out, AND IT IS THE DEFAULT.
                           # 2000000 = the real 88-CPU, real time, real waiting.

# Two 6850 ACIAs on one card. They share NOTHING -- separate baud jumpers,
# separate endpoints, separate interrupt straps -- so almost everything here is a
# UNIT property, not a board one.
[[board]]
type = "2sio"
id   = "sio0"
port = 10                  # HEX: a port is on the wire. Occupies 10-13.

  [board.unit.a]
  connect   = "console"
  baud      = 9600         # DECIMAL: a baud rate never is.
  interrupt = "none"       # none | int | vi0..vi7
                           #   "int" = pINT (pin 73). With no VI board in the machine
                           #   the IntAck cycle floats to 0xFF = RST 7. Real Altair.

  [board.unit.b]
  connect = "null"         # NOT an error: an unconnected 6850 sits there with TDRE
                           #   set forever and talks to nobody, exactly as the card
                           #   does with nothing in the second socket.

# The OTHER serial card -- MITS's first, and the one whose status bits are
# INVERTED (bit CLEAR = ready). One port, so unlike the 2SIO every jumper is a
# BOARD property: that is what they are on the PCB. See docs/boards/mits-88sio.md.
[[board]]
type      = "sio"
id        = "sio1"
port      = 0              # HEX, and MUST BE EVEN. Control at 00, data at 01.
rev       = "1"            # 1 = the errata mod done at the factory. THE DEFAULT.
                           #   On a rev 0 the UART's own flags ALSO appear at
                           #   status bits 5 (DAV) and 1 (TBMT); rev 1 drops them.
baud      = 9600           # DECIMAL. A jumper: software cannot change it.
data_bits = 8              # the NDB1/NDB2 pads. Soldered, not programmable --
stop_bits = 1              #   the NSB pad                 there is no control
parity    = "none"         #   the NPB/POE pads            register on this card.
connect   = "console"

  # TWO straps, not one. The manual is explicit that the input device and the
  # output device may be jumpered to DIFFERENT VI priorities; the card's "BH"
  # (both) pad is just one wire instead of two for the case where they agree.
in_int    = "int"          # the IN pad  (receiver):    none | int | vi0..vi7
out_int   = "none"         # the OUT pad (transmitter): none | int | vi0..vi7
                           #   Software must ALSO enable the interrupt, by writing
                           #   D0 (in) / D1 (out) to the control channel. The strap
                           #   is the wire; the enable is the flip-flop.

[[board]]
type = "memory"
id   = "mem0"
fill = "random"            # real static RAM does not come up zeroed

  [[board.region]]         # A card carries one or more POPULATED regions.
  type = "ram"             #   ram = writes are stored.  rom = writes are not decoded.
  at   = 0000
  size = "48K"             # ALTMON puts its stack at C000 and pushes DOWN

  [[board.region]]
  type  = "rom"
  at    = F800
  mount = "builtin:altmon" # compiled in: nothing to download, same on every OS
```

### The transform chain is the CONSOLE's, and only the console's

`UPPER`, `STRIP7OUT`, `CRLF`, `BSDEL` and the rest belong to `[console]` — **not** to a board and **not** to a unit. Every other endpoint (socket, serial port, tape, file) is **8-bit clean, always**, because the next thing down that line may be XMODEM and a filter would corrupt it silently (DESIGN.md §7.2).

```toml
[console]
upper     = true         # fold keyboard input to uppercase -- MITS BASIC wants caps
strip7in  = true
strip7out = true         # the Teletype ignores bit 8; MITS BASIC sets it as a terminator
bsdel     = "bs"         # off | bs (fold DEL->BS) | del. A perennial CP/M annoyance.
crlf      = false        # usually WRONG to turn on: period software sends its own LF
echo      = false        # local echo, for half-duplex hardware
bell      = true
```

What a **board** gets instead is **line coding** — and only where it is really a jumper:

```toml
[[board]]
type      = "sio"        # the 88-SIO's word format IS a set of solder pads
id        = "sio0"
data_bits = 8            # NDB1/NDB2. A FRAME, not a mask -- see docs/boards/mits-88sio.md
stop_bits = 2            # NSB
parity    = "none"       # NPB/POE
```

On a real serial port those are programmed into the real port. On the **88-2SIO** there is no such property at all: the 6850's word format is a register the *guest* writes.

At the monitor that is `SET sio0:a UPPER=ON`, and `SHOW sio0` prints it.

`[console]` holds only what is genuinely about a *terminal*:

```toml
[console]
attn = 05                  # HEX. The key that drops from CONSOLE back to the monitor.
                           #   The guest NEVER SEES this byte, so it cannot disable it.
```

`rows`, `cols`, `pace`, `ansi`, `tabs` and `log` are specified in DESIGN.md §7.2 but **not built yet**.

### A second 2SIO — same type, own config, different base port

```toml
[[board]]
type = "2sio"
id   = "sio1"
port = 14
```

## Example — a CP/M machine (milestone 3+)

```toml
[machine]
name     = "cpm-dev"

startup = [                # monitor commands, run in order once the boards exist.
  "RUN FF00",              #   start the DBL boot PROM. There is no BOOT command
]                          #   (DESIGN.md §10.0) — this is the operator's keystroke,
                           #   written down. Anything you can type, a config can do.

# The CPU is a card, and THE CRYSTAL IS ON IT -- which is why clock_hz is this
# board's property and not the machine's.
[[board]]
type     = "8080"
id       = "cpu0"
clock_hz = 2_000_000

# ...and so is the front panel, which is where the SENSE switches live. DBL reads
# them at FF22 (`IN 0FFH`, bit 4) to pick the 2SIO's stop bits, so this is not
# decoration -- see docs/boards/mits-frontpanel.md.
[[board]]
type  = "fp"
id    = "fp0"
sense = 0x00               # SA8..SA15. Bit 4 is switch A12: up = 1 stop bit.

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
                           #   docs/boards/s100-memory.md.) The bus picks no winner — §4.2.
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
| `startup` | **A list of monitor commands, executed in order once the backplane is built.** This is how a machine starts itself: there is no `BOOT` command (`DESIGN.md` §10.0), so a config that should boot says `startup = ["RUN FF00"]`. |

**That is the whole table, and it is short on purpose.** A `[machine]` key is a thing that is true
of the *backplane*, and almost nothing is. Two keys used to be here and both were **moved onto the
card that physically carries them**:

| Was | Is | Why |
|---|---|---|
| `[machine] clock_hz` | `[[board]] type = "8080"`, `clock_hz` | The crystal is on the CPU card. A machine with no CPU in it has no clock rate — which is not a missing value, it is the truth about the machine. |
| `[machine] sense` | `[[board]] type = "fp"`, `sense` | The switches are on the front panel, and the front panel is a card. |

**Both are hard errors, not silent migrations.** Loading a file with either key fails, and the
error hands you the `[[board]]` block that replaces it. That is deliberate: `sense` in particular
spent months parsing into a byte that *nothing put on the bus* — no board decoded port `0xFF`, so
`IN 0FFH` read the floating bus and returned `0xFF` whatever you wrote here. A config that looks
like it set the switches and did not is worse than one that will not load.

**`startup` makes the config language and the script language the same language.** Anything you can type at the monitor, a config file can do — and `CONFIG SAVE` round-trips the list verbatim.

> **Caution:** because `startup` runs commands, `CONFIG LOAD` on a `.toml` from an untrusted source executes whatever is in it. Keep `startup` to monitor commands, and treat a machine file like a script, because it is one.

## Memory regions

A `memory` card carries a list of **regions** — the areas that are actually populated. This is not a convenience: a real card may hold **several ROM areas and several RAM areas at once** (a PROM card is a row of sockets at F000/F400/F800/FC00, any of which may be empty), and one card must be one board or `BOARDS` stops describing the machine.

| Region key | Meaning |
|---|---|
| `type` | `ram` — writes are stored. `rom` — **writes are not decoded at all.** |
| `at` | Start address. Page-aligned (multiple of 100H). |
| `size` | `ram` only. `1K`…`64K`, or a hex byte count. |
| `mount` | `rom` only. **`builtin:<name>`** for a ROM compiled into the simulator, or a path to a `.hex`/`.bin` on the host. **The region's size comes from the image**, rounded up to a page. **Omit it and the region is an empty socket:** it decodes nothing, and you can `MOUNT` a chip into it later. |

**`builtin:` ROMs are compiled in**, because there is no portable place to keep ROM images across macOS, Linux, and Windows. `SHOW ROMS` lists them; `docs/roms.md` records each one's source, size, and CRC32. A bare path always overrides — built-ins are a convenience, never a lock-in.

**Addresses no region covers are unpopulated** — an empty socket, a missing chip — so the board does not decode them, nothing drives the bus, and they read `0xFF`. Unpopulated RAM and an empty ROM socket are the same case, handled the same way.

**Regions are sub-units**, so the existing `id:unit` addressing reloads one: `MOUNT mem0:rom0 newdbl.hex`.

See `docs/boards/s100-memory.md` for banking (five real cards, no two alike), `fill`, and the three PHANTOM\* straps.

## Notes

- **`id` is what monitor commands address.** Sub-units are `id:unit` — `MOUNT fdc:drive0 cpm.dsk`, `CONNECT sio2a:b socket:2323`, `SET sio2a:a BAUD=9600`, `MOUNT mem0:rom0 dbl.hex`.
- **A reset never clears RAM. Only powering off does** (`DESIGN.md` §6). `RESET` is the front-panel button; `POWER` is a power cycle, and it is the only thing that loses memory (and the only thing that re-reads ROM images from disk).
- **Port collisions are detected**, not silently resolved. The bus reports contention naming both boards and the address. `SHOW BUS IO` shows the full map; `WHO IO 0x10` is the reverse lookup.
- **Config-time vs runtime properties:** `SHOW <id>` displays which is which. Setting a config-time property on a running machine is rejected with a clear message rather than half-applied.
- **Disk geometry** is probed by the **board**, not by a host service — image size alone is meaningless without knowing which controller wrote it (`DESIGN.md` §7.3). `media` forces the choice when the size is ambiguous.
