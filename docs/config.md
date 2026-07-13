# Machine configuration (TOML)

A machine is described by a TOML file. The **same state** is reachable at runtime through the monitor, and `CONFIG SAVE` writes it back out.

**The TOML keys for a board *are* its `properties()`** (see `DESIGN.md` §5) — there is no separate config schema per board. The loader and `CONFIG SAVE` are the same code path, so they cannot drift, and a board added next year is configurable the day it lands with no changes here.

## Which machine you get

| You type | You get |
|---|---|
| `altairsim basic4k` | the built-in `basic4k` — **in every directory on earth** |
| `altairsim cfg/mine.toml`, `altairsim -f mine` | that file |
| `altairsim -m default` | the built-in, always |
| `altairsim -n` | an empty backplane |
| `altairsim` | **`./altairsim.toml` if the working directory has one**, else the built-in `default` |

**`./altairsim.toml` is the only file the simulator *finds* rather than is *given*, and it is found only when the command line names nothing at all.** That restriction is the whole licence for the feature. `looksLikeFile()` (`src/core/machines.h`) refuses to probe the disk on purpose — `altairsim default` must not become a different machine the day somebody saves a file called `default` next to it, because a command line that changes meaning because of its surroundings is a trap. That argument applies to every command that *names* a machine. A bare `altairsim` names none: it is not asking for `default`, it is asking for whatever machine is sensible *here*, and letting the directory answer is the `make(1)` bargain rather than the trap.

**It is never silent.** When the working directory's file is used, the simulator says so on **stderr** — because the failure this can otherwise cause is spending twenty minutes on a machine you did not know you were running. It goes to stderr and not stdout so that a `-s` script's output stays exactly what the script printed.

A found file is an *ordinary* config file, so it can `base` off a built-in and say only what is different:

```toml
# ./altairsim.toml -- the project's machine. Just `altairsim` boots it.
[machine]
name = "myproject"
base = "default"

[[board]]
id    = "dsk0"          # the default's floppy controller...
mount = "disks/cpm.dsk" # ...with this project's disk in it
```

## Where a relative path is relative to

There are two answers, and keeping them apart is the whole of the rule.

> **A path written *inside* a machine file is relative to THAT FILE.**
> **A path *typed* at the prompt is relative to THE SHELL.**

That covers `mount`, `base`, and the `MOUNT`/`LOAD` commands inside a `startup` list. It does **not** cover a `-s` script, whose commands are resolved against the working directory like anything else you could have typed.

The first half is what makes a machine file portable. `tapes/MitsPS2/ps2int.toml` says:

```toml
startup = [
  "MOUNT acr0:tape \"PS2-MON.TAP\"",   # the tape lying beside this file
  "LOAD \"LDRPS2.HEX\"",               # ...and the bootstrap beside it
  "RUN 0",
]
```

…and means the two files in its own directory. So both of these work, and mean the same machine:

```sh
cd tapes/MitsPS2 && altairsim ps2int.toml     # the way you will actually run it
altairsim tapes/MitsPS2/ps2int.toml           # ...and from anywhere else
```

That matters because **`tapes/` and `disks/` are what we ship.** A user gets the binary and those trees — not this repository. A machine file that only resolved from the repository root would be a machine file that only worked for us, and every example in this tree used to be exactly that.

The second half is not a compromise, it is the other half of the same rule. When you type

```
altairsim> MOUNT dsk0:drive1 "scratch.dsk"
```

you mean the `scratch.dsk` you can see in the shell you are standing in — never one sitting next to somebody's example config. A command whose meaning depended on which machine happened to be loaded would be the same trap as a command line that changes meaning with its surroundings, and `acceptance-examples` has a negative control that fails the build if the config's directory ever leaks out of its `startup` list and starts colouring what a human types.

**There is no search path.** A file is looked for in exactly one place. If it is not there, the error names the place it looked — not the name you wrote — because the whole point of a resolved path is to be able to see where it went.

**What is stored is what you wrote.** `SHOW` prints, and `CONFIG SAVE` writes back, the path *as it appears in the file* — so a machine saved out of `disks/mits-88mds/cpm22/` still says `mount = "CPM56K-1.DSK"` and still loads from its own directory. Only the *narration* ("`mounted …`", "`loaded …`") names where the file actually was, because that is a report of what happened rather than a record of what was asked for.

Absolute paths and the `builtin:` scheme are never re-based: `mount = "builtin:dbl"` is a ROM in the binary, not a file, and must never become one.

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
idle     = true          # ...and so is the OTHER sleeping policy, and it is the DEFAULT.
                         # A guest at a prompt does nothing but poll an empty keyboard,
                         # and running that flat out cost a whole host core. Now it costs
                         # about 3%. The guest cannot tell -- no board behaves differently
                         # and emulated time is untouched; only the HOST sleeps. It is
                         # independent of clock_hz (a 2 MHz machine idles too), and
                         # `idle = false` gets the spin back.

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
  media    = "fdc8mb"      # dcdd: 8in | fdc8mb.  mds: minidisk.  Else probed by the BOARD
                           # from the file size. The choices are the CARD's -- `minidisk` on a
                           # dcdd is an error, because it is a different controller.
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
| `base` | **Start from another machine, then say what is different.** A built-in name (`"default"`) or a path to a `.toml`. Must come before the first `[[board]]`. See below. |
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

**A `startup` entry is a command line, so it can quote a filename** — `\"` and `\\` are the two escapes the parser knows, and it refuses any other rather than quietly eating the backslash:

```toml
startup = [
  "MOUNT acr0:tape \"tapes/4KBasic31/4K BASIC Ver 3-1.tap\"",
  "LOAD \"tapes/4KBasic31/LDR4K31.HEX\"",
  "RUN 0",
]
```

The quotes are not decoration. The monitor's tokenizer needs them because **every period tape in the tree has a space in its name**, so until the escape existed this could not be written at all: each `"` toggled the string, the entry was cut at the backslash, and the machine came up with an empty recorder and no error. `CONFIG SAVE` escapes on the way out too, or it would write a file it could not read back.

> **Caution:** because `startup` runs commands, `CONFIG LOAD` on a `.toml` from an untrusted source executes whatever is in it. Keep `startup` to monitor commands, and treat a machine file like a script, because it is one.

## `base` — start from a machine, and say what is different

A CP/M machine is *the default Altair with a floppy in drive 0*. That is the whole of it, and `base` lets the file say exactly that and nothing else:

```toml
[machine]
name = "cpm22-8mb"
base = "default"          # fp0, cpu0, sio0 (console), dsk0 (88-DCDD), mem0 (56K + DBL PROM)

startup = ["RUN FF00"]

[[board]]                 # no `type` -> the card ALREADY in the machine with this id
id = "dsk0"

  [[board.drive]]
  unit  = 0
  mount = "disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK"
```

**This is not a convenience, it is a defect class.** Hand-copying a five-card backplane is how you end up with a machine that boots CP/M into a terminal that is not there — because the one card you forgot to copy was the 2SIO. That is not hypothetical; it is what happened to the two files this feature replaced, and both booted far enough to look fine.

**A file with no `base` is a complete machine, exactly as before.** The key is explicit rather than assumed, and that is a deliberate call: if every file silently inherited the default, then `4k` — a machine *defined by what it does not have* — would have to **remove** a floppy controller, a 2SIO and 52K of RAM to describe a bare 1975 Altair, and silence would stop meaning "nothing". One line at the top tells you what the backplane starts as; without that line, the file **is** the backplane.

A base may be a **built-in name** or a **path**, decided by spelling and never by probing the disk (`looksLikeFile()`) — the same rule the command line uses, so `base = "default"` cannot change meaning the day somebody saves a file called `default` in the working directory. Bases may nest, up to 8 deep; two files that name each other are an error, not a hang.

### The four `[[board]]` forms

| Form | Means |
|---|---|
| `type` + a **new** `id` | **Add** a card. The only form that exists in a file with no `base`, and the only one that existed at all before `base` did. |
| `type` + an `id` **from the base** | **Replace** it outright. Naming a card's type means you are specifying the whole card, not amending it. |
| **no `type`** + an `id` | **Modify in place** — properties, unit properties, and anything appended to its lists. This is the common one: *"the base's floppy controller, with a disk in it."* |
| `remove = true` | **Pull the card out** of the slot. |

**Replace exists because a list cannot be amended into a smaller one.** Regions are a list, so adding a 24K region to a base's 56K memory board would *overlap* it — two boards answering `0000–5FFF`, which is bus contention — rather than replace it. Re-declaring the card with its `type` says "this is the whole card now":

```toml
[[board]]
type = "memory"      # `type` on an id the base brought -> a FRESH card
id   = "mem0"
fill = "random"

  [[board.region]]
  type = "ram"
  at   = 0000
  size = "24K"

  [[board.region]]
  type  = "rom"      # ...and you must re-state the PROM, because you replaced the whole
  at    = FF00       # card. Leave it out and there is nothing at FF00 to RUN.
  mount = "builtin:dbl"
```

**A duplicate `id` within one file is still an error**, and replace is deliberately scoped around that check: a second `[[board]]` with a copy-pasted id is a *typo*, while the same thing against a base is *intent*. Conflating them would have thrown away the one diagnostic that catches the commonest mistake in a hand-written machine file.

**`CONFIG SAVE` never writes a `base`.** It writes the backplane it can see — every card, inherited or not — so a saved machine stands on its own. That is the only honest thing it can do, because a base may be a file, and a file can change under you.

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

## `[board.unit.x]` and `[[board.thing]]` are not the same thing

They look alike and they are opposites, so it is worth being blunt about which is which:

- **`[board.unit.a]`** — *settings on a unit the board already has.* Every key in it goes through the same property path as `SET sio0:a BAUD=9600` at the monitor, so a config file can never set something the monitor would refuse. It is **generic**: any board with a unit that has settings gets this for free, from `units()` and `unitProperties()`, and `CONFIG SAVE` writes it back from that same pair.
- **`[[board.region]]`, `[[board.drive]]`** — *a **list** of things the board owns.* The board builds these itself (`addSubUnit()`), because only it knows what a region or a drive is.

**This distinction was a real bug, not bookkeeping.** `CONFIG SAVE` wrote `[board.unit.<name>]` for *every* board with unit settings, but the loader treated it as a board-declared table and only the 2SIO had declared it. So saving any machine with a cassette or a disk in it produced a file that would not load:

```
ps2int.toml: board 'acr0' (acr) has no [[board.unit]] table
```

A save you cannot load is not a save. Both halves are generic now, and `tests/test_machines.cpp` round-trips **every** built-in machine through `CONFIG SAVE` and back, checking that saving the reloaded machine is byte-identical — a fixed point, so nothing can be silently dropped on the way through.

See `docs/boards/s100-memory.md` for banking (five real cards, no two alike), `fill`, and the three PHANTOM\* straps.

## Notes

- **`id` is what monitor commands address.** Sub-units are `id:unit` — `MOUNT fdc:drive0 cpm.dsk`, `CONNECT sio2a:b socket:2323`, `SET sio2a:a BAUD=9600`, `MOUNT mem0:rom0 dbl.hex`.
- **A reset never clears RAM. Only powering off does** (`DESIGN.md` §6). `RESET` is the front-panel button; `POWER` is a power cycle, and it is the only thing that loses memory (and the only thing that re-reads ROM images from disk).
- **Port collisions are detected**, not silently resolved. The bus reports contention naming both boards and the address. `SHOW BUS IO` shows the full map; `WHO IO 0x10` is the reverse lookup.
- **Config-time vs runtime properties:** `SHOW <id>` displays which is which. Setting a config-time property on a running machine is rejected with a clear message rather than half-applied.
- **Disk geometry** is probed by the **board**, not by a host service — image size alone is meaningless without knowing which controller wrote it (`DESIGN.md` §7.3). `media` forces the choice when the size is ambiguous.
