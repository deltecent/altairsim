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
| `socket:2323` | Listening TCP socket — a terminal emulator connects *in*. | **built** |
| `socket:host:port` | Outbound TCP connection. | **built** |
| `serial:/dev/cu.usbserial-X` | Real host serial port (POSIX). | **built** |
| `serial:COM3` | Real host serial port (Windows). | **built** |
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
type = "2sio"
id   = "sio2a"
port = 0x10
  [board.unit.a]
  connect = "console"

[[board]]
type   = "dcdd"
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

# GUEST <-> HOST FILE TRANSFER, and it is OUR OWN CARD -- MITS never made it.
# It is in the DEFAULT machine, so you usually inherit it rather than write this.
#
# NOT PORT 0xFE, which an earlier draft of this file said and which is wrong twice
# over: 0xFE is the 88-VI/RTC's real control register, and it is where AltairZ80 put
# its pseudo-device. 0x30 is wrong too -- that is the WD179X floppy controller. B0.
[[board]]
type    = "hostbridge"
id      = "hb0"
port    = B0               # 2 ports, B0-B1
hostdir = "./hostfiles"    # THE SANDBOX ROOT. Guest names cannot escape it: no `..`
                           # component, no absolute path, no drive letter, no symlink
                           # that resolves out. Subdirectories inside it are fine, and
                           # both `/` and `\` work on every host.
                           #
                           # EMPTY (the default) MEANS THE DIRECTORY YOU RAN altairsim
                           # FROM -- which is the same rule every typed path follows,
                           # and which is what makes `R FOO.ASM` work on the file you
                           # are looking at with nothing configured.
readonly = false           # ON makes it a one-way street: out of the host only.
```

Then, at the CP/M prompt (`cpm/hostbridge/*.ASM`, assembled in the machine with
`ASM`/`LOAD`):

```
HDIR              what is on the host
R  FOO.ASM        host -> CP/M          R *.ASM      R SRC/FOO.ASM
W  FOO.ASM        CP/M -> host          W *.HEX      W FOO.TXT T   (T = trim ^Z)
```

## The reference moved — and this file did not

**The normative TOML reference now lives in the User Manual** (`docs/manual/configuring.md`),
which is the document that ships to users and is therefore the document that has to be
complete. Every `[machine]` key, all four `[[board]]` forms, the region and drive tables, the
number rule, `[console]` — they are specified there, once.

They used to be specified *here as well*, and that was the bug. Two normative descriptions of
one format is precisely the "second schema" this project refuses everywhere else in the code,
committed in the docs instead — and the copy here had already drifted (it claimed a machine
could have "config-time properties" that are "rejected on a running machine", a rule that was
deleted in July 2026 and never existed in the code by the time anybody read the sentence).

**What stays here is the part that is not a specification: the arguments.** The worked examples
above, and the reasoning in them — why `clock_hz` is the CPU card's property and not the
machine's, why the SENSE switches are the front panel's, why the transform chain belongs to the
console and to nothing else, why a second 2SIO needs nothing but a different base port. That is
what a design document is for, and none of it belongs in a user manual.

If you are looking for **what a key does**, read the manual. If you are looking for **why the
format is shaped like this**, you are in the right place.

## And the per-board keys are not written down anywhere by hand

A board's `properties()` **are** its TOML keys. There is no separate schema, and there is no
hand-maintained table of them — `docs/manual/ref/boards.md` is *printed from the binary* by
`tools/gen-reference.cpp`, and a test fails if it is stale.

That is not a convenience. The first table of the memory card's defaults that anybody typed out
by hand, reading the source carefully, got three of eight rows wrong.
