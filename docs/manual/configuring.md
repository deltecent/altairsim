# Configuring a machine

A machine file is a **TOML** file that describes a machine. It lists the boards in the
backplane, the settings of each board, and the commands to run after the power comes on. This
chapter describes the format in full.

## What TOML is

**TOML** is a plain configuration format. You can write it in an editor, or the `CONFIG SAVE`
command can write it from the machine that you are running. Either way, you can read it again
later without a manual. These rules are enough to read every example in this chapter:

- **`key = value`** gives one setting on each line, for example `clock_hz = 2000000`.
- **Quotes are for text.** A string has quotes (`name = "cpm22"`). A number, `true` or `false`
  has no quotes (`size = 256`, `idle = true`).
- **A `[table]`, with single brackets, is a thing that the machine has one of.** `[machine]` is
  the machine, and `[console]` is your terminal. You write it one time.
- **A `[[table]]`, with double brackets, is a thing that the machine has many of.** Each
  `[[board]]` starts one more board, and each `[[board.drive]]` one more drive. The double
  brackets mean "add one more to the list". Write `[[board]]` for each board.
- **A nested table such as `[board.unit.x]` belongs to the block above it**, by its name. The
  indentation in these examples only makes them easier to read.
- **`#` starts a comment** that continues to the end of the line.

## How a machine file is loaded

There are four ways to load a machine file:

- **Name it on the command line**: `altairsim mine.toml`. A filename that has a `/` or ends in
  `.toml` is a file, not a built-in machine. Use `-f mine` if the filename has neither.
- **Name nothing.** If the working folder has a file called `altairsim.toml`, `altairsim` with
  no arguments loads it. If it has none, you get the built-in `default` machine.
- **Type `CONFIG LOAD mine.toml`** at the monitor, in a machine that is already running. The
  file **replaces** that machine, and it loads all of the file or nothing.
- **Name it as the `base` of another file** (`base = "mine.toml"`). The other file starts from
  this one and gives only what is different.

The first three make the file the machine. The last one uses the file as a start for another.
The machines chapter gives the rules for the command line and for relative paths.

## The file that you will write

Most machine files start from a machine that already exists, and they give only what is
different. This is `examples/cpm/cpm22-buffered.toml` without its comments:

```toml
[machine]
name = "cpm22-buffered"
base = "default"
startup = ["RUN FF00"]

[console]
bsdel = "bs"

[[board]]
id = "dsk0"

  [[board.drive]]
  unit  = 0
  mount = "cpm22b23-56k.dsk"
```

The `default` machine already has everything that a 56K CP/M machine needs. It has a front
panel, an 8080, a 2SIO console, a floppy disk controller, 56K of RAM and the boot PROM at
`FF00`. This file adds three things: the disk in drive 0, the `RUN FF00` command, and the
Backspace setting that CP/M expects.

The `[[board]]` has no `type`, because `dsk0` is already in the machine, and the file changes
it. With `type = "dcdd"`, the file would remove the controller of the base and add a new one
with the default settings. The file still loads, with no error, so you do not see the mistake.
Leave the `type` out.

## Two rules

**A machine file can do what you can type, and nothing more.** It cannot boot a disk, because
there is no `BOOT` command. It can type `RUN FF00` for you, which is what the operator did.
Every key on a board is a property that `SET` changes at the monitor, and every property is a
key that you can write in the file. The board reference at the back of this manual lists every
property of every board. The program prints that reference from the same table that `SET` uses,
so it is always current.

**An unknown table or key is an error, and the machine does not load.** The program tells you
which key it did not know, and the line of the file that the key is on:

```
mine.toml: line 4: unknown [machine] key 'widget'. Valid keys: base name startup
mine.toml: line 7: [[board]] cpu0: cpu0 has no property 'frobnicate'. Valid properties: achieved_hz clock_hz idle
```

For this reason, a setting with a typing mistake never looks as if it worked. You find the
mistake when you load the file.

**A table or a key that you write two times is an error also.** Write `[machine]`,
`[console]`, `[display]` and `[terminal]` one time each in a file. Write a
`[board.unit.<name>]` one time below its `[[board]]`, and write a key one time in its table.
This mistake usually occurs when you paste one machine file into another. The error gives
both lines:

```
mine.toml: line 8: [console] is already at line 5 -- a table is written one time
mine.toml: line 6: 'name' is already set at line 2
```

**The wrong brackets on a table are an error also.** `[board]` with single brackets does not
add a board, and `[[console]]` with double brackets is not the console. The error gives the
correct form:

```
mine.toml: line 6: [board] must be [[board]] -- double brackets, one for each
mine.toml: line 4: [[console]] must be [console] -- a table that is written one time
```

Each key must be below a table. A key above the first table is an error.

## The tables

| Table | What it is |
|---|---|
| `[machine]` | the name of the machine, its base and its startup commands. Three keys only |
| `[[board]]` | a board. One entry for each board |
| `[board.unit.<name>]` | one unit *on* the board above it, for example a serial channel or a tape deck |
| `[[board.region]]` | a memory region on a `memory` board |
| `[[board.drive]]` | a drive on a disk controller |
| `[console]` | **your terminal.** It is not a board |
| `[display]` | **your video window.** It is not a board |
| `[terminal]` | the built-in terminal window. The serial chapter describes it |

## `[machine]`: three keys

```toml
[machine]
name    = "cpm22"
base    = "default"
startup = ["RUN FF00"]
```

### `name`

The name of the machine. **The name is a label for you, and nothing more.**

- **The program shows it in three places:** the `machine:` line that it prints when the
  monitor starts, the first line of `SHOW MACHINE`, and the title bar of the video window.
- **`SET MACHINE name=` changes it** at the monitor, and `CONFIG SAVE` saves it.
- **The program does not find a machine file by its name.** The command line and the `base`
  key find a file by its path. The name in the file can be different from the file name.
- **A file with no `name` has the name of its base.** A file that starts from `default` and
  gives no name is called `default`.

### `base`: start from a machine, and write what is different

```toml
base = "default"               # a built-in machine
base = "../cpm22/cpm22.toml"   # or a file
```

The program reads the value with **the same rule as the command line**. A value that contains a
`/` or ends in `.toml` is a file. Any other value is the name of a built-in machine. The
machines chapter gives the rule. A file path is relative to the file that names it.

- **The program reads `base` before every other key**, in any order in the file.
- **`base` must come before the first `[[board]]`.** A file cannot change a backplane and then
  start from a different one.
- **A base can have its own `base`, up to 8 levels deep.**

With `base`, a machine file contains **only what is different** from its base. You do not copy
every board, so a change to the base reaches every file that uses it.

#### A file that starts from a file

A base can be a machine file that you wrote, and that file can have a base of its own. These
two files are in the same folder as the disk image. The first file is a CP/M machine:

```toml
# cpm22.toml
[machine]
name    = "cpm22"
base    = "default"
startup = ["RUN FF00"]

[[board]]
id = "dsk0"

  [[board.drive]]
  unit  = 0
  mount = "cpm22b23-56k.dsk"
```

The second file is the same machine at 2 MHz. It contains the one setting that is different:

```toml
# cpm22-2mhz.toml
[machine]
name = "cpm22-2mhz"
base = "cpm22.toml"

[[board]]
id       = "cpu0"
clock_hz = 2000000
```

- **The chain has three levels:** `cpm22-2mhz.toml`, then `cpm22.toml`, then the built-in
  machine `default`.
- **The second file gets the disk and the `startup` list from the first.** It does not write
  them again. A file that has its own `startup` list uses that list and not the list of its
  base.
- **`base = "cpm22.toml"` is the file in the same folder**, because the path is relative to the
  file that names it.
- **A change to `cpm22.toml` reaches the two machines.**

If two files name each other as a base, the chain has no end. The program stops at 8 levels
and does not load the machine. The message gives the file that you loaded and the line of its
`base`:

```
a.toml: line 3: base = "b.toml": more than 8 levels deep -- do two files name each other?
```

### `startup`: the operator's commands

```toml
startup = ["RUN FF00"]
```

`startup` is a list of **ordinary monitor commands**. The program runs them in order after it
builds the machine, and it shows each one on a `startup>` line.

A path in a `startup` command is relative to the machine file, the same as every other relative
path. A `startup` line and the same command that you type find the same file.

### What `[machine]` does not accept

```toml
[machine]
clock_hz = 2000000        # ERROR
sense    = 0x80           # ERROR
```

The program **rejects both, and it tells you where each one goes**:

```
mine.toml: line 4: clock_hz belongs to the CPU BOARD, not to [machine] --
  the crystal is on the board. Put it in the CPU's [[board]]:
      [[board]]
      type     = "8080"
      id       = "cpu0"
      clock_hz = 2000000
```

The crystal is on the processor board, and the sense switches are on the front panel. Neither
one is a property of the machine.

## `[[board]]`: four forms

**What a `[[board]]` entry does depends on two things.** Does it have a `type`? Did the base
already use its `id`?

| Write | What it does |
|---|---|
| `type` + a **new** `id` | **ADD** the board |
| `type` + an `id` **from the base** | **REPLACE** the board |
| **no** `type` + an `id` | **MODIFY** the board |
| `remove = true` + an `id` | **REMOVE** the board |

**Every `[[board]]` needs an `id`.** You use the `id` to name the board at the monitor, and a
file that uses this one as its base uses the `id` to name the board.

### ADD: `type` + a new id

```toml
[[board]]
type = "virtc"
id   = "vi0"
```

The machine now has this board. **In a file with no `base`, this is the only form**, because
there is no board to modify, replace or remove.

### REPLACE: `type` + an id that the base already used

```toml
[[board]]
type = "2sio"
id   = "sio0"
port = 0x20
```

If the base had a board called `sio0`, the program removes it and adds a new `2sio` in its
place. **All the settings that the base made on that board are lost**, also the ones that you
did not write. You get the defaults of the type, and the settings that you write here.

**In a file with a `base`, you almost always want MODIFY.** Leave the `type` out.

`CONFIG SAVE` saves every board with its `type` and all its keys. Do not copy a board from a
saved file into a file that has a `base`, because that is a REPLACE. Write the `id` and the keys
that you change.

### MODIFY: no `type`

```toml
[[board]]
id       = "cpu0"
clock_hz = 2000000
```

**When you leave out the `type`, you change the board that is already there.** All the settings
that the base made on `cpu0` stay. Only the clock changes.

### REMOVE: `remove = true`

```toml
[[board]]
id     = "acr0"
remove = true
```

The program removes the board from the backplane, and its ports are no longer decoded. No other
entry in the file can name it.

### The same id two times in one file

**A `type` with an id that the same file already used is an error.** It is not a replace. Two
boards with the same id in one file are almost always a block that you copied and did not
rename. The file does not load, and the error names the id.

An id from your *base* is different. It is a board that you got from the base, and you can
replace it.

## Every other key on a `[[board]]` is a property

`type`, `id` and `remove` tell the program which board the entry is. **Every other key goes to
the board**, and the board accepts it or rejects it by name:

```toml
[[board]]
id     = "dsk0"
drives = 8
```

**The board reference at the back of this manual lists the keys of every board.** The boards
chapter tells you what each board is.

## `[board.unit.<name>]`: settings for one unit

Some boards have more than one independent part. An 88-2SIO has **two 6850 ACIAs**. Unit `a` and
unit `b` each have their own baud rate, their own interrupt setting and their own connection, so
each one has its own table:

```toml
[[board]]
id = "sio0"                    # no type: the board is already in the machine

  [board.unit.a]
  connect = "telnet:2323"      # a property of unit a

  [board.unit.b]
  baud    = 1200               # unit b has its own settings
  connect = "socket:2324"
```

This sample is for a file whose base has the board `sio0`, such as `base = "default"`. It has no
`type`, and it gives only the keys that change. The other settings of the board and of each unit
stay.

A key in a unit table is a command at the monitor. `connect` in `[board.unit.a]` is
`CONNECT sio0:a telnet:2323`, and `baud` in `[board.unit.b]` is `SET sio0:b baud=1200`. The
serial chapter shows each `CONNECT` command with its machine-file form. The board reference
tells you which boards have units, and the keys of each unit.

## `[[board.region]]`: memory

A `memory` board is **a list of regions**. For this reason, one memory board can have 56K of RAM
and a boot PROM at the top of memory.

```toml
[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0x0000            # HEX: it is an address
  size = "56K"             # DECIMAL: it is a count

  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  mount = "turnmon.bin"    # relative to this file
```

| Key | |
|---|---|
| `type` | **required.** `ram` or `rom` |
| `at` | the address where the region starts. **Hex** |
| `size` | the size of a `ram` region. **Decimal**. You can use the `K` and `M` suffixes. A `rom` region takes its size from its image, rounded up to a page, so it ignores `size` |
| `mount` | a ROM image: a file path, or `builtin:<name>` |

A size with a suffix needs quotes, as in `size = "56K"`, because TOML does not accept `56K` as a
number. A plain number needs no quotes: `size = 256`.

### An empty socket

**A `rom` region with no `mount` is an empty socket**, even when it has a `size`. It decodes
nothing, so a read there gets `FF`. That is what an S-100 bus gives when no board drives it. It is not zero, and it is not an
error. A real board with an empty PROM socket gives the same result.

## `[[board.drive]]`: disks

A disk controller has drives, and a drive holds a disk image.

```toml
[[board]]
id = "dsk0"

  [[board.drive]]
  unit     = 0             # DECIMAL: it is a drive number
  mount    = "cpm.dsk"     # relative to this file
  readonly = false
```

| Key | |
|---|---|
| `unit` | the drive number. **Decimal** |
| `mount` | the image file |
| `readonly` | refuse every write at the controller, so that the file cannot change. The disks chapter tells you more. `writeprotect` is the same key |
| `media` | the disk format. Use it when the controller cannot detect the format from the image |
| `create` | make an empty file if the file is not there, and then mount it. This is `MOUNT … CREATE` in the file |

The controller usually detects the format from the image. Use `media` when it cannot, for
example for an image with no header or with an unusual layout. `media` also sets the size of a
**blank** disk, because a blank disk matches no format. The disks chapter describes the formats
and `create`.

Without `create`, a `mount` that names a missing file is an **error, and the machine does not
load**.

Each `[[board.drive]]` is one `MOUNT` command. The disks chapter shows each `MOUNT` command with
its machine-file form.

## Numbers: hex on the bus, decimal for counts

The machine file uses the same number rule as the monitor. *The Monitor* gives the full rule.
**A value that the processor sees on the bus is hex. A value that never goes on the bus is
decimal.**

```toml
port  = 10        # a port is on the bus, so it is HEX. This is port sixteen.
at    = 0xFF00    # an address: hex
sense = 80        # the sense switches: hex, 0x80
baud  = 9600      # a rate: DECIMAL
size  = 256       # a count: DECIMAL
```

**`port = 10` is port sixteen.** A port is on the bus, so it is hex, and every listing from 1976
put the 2SIO at 10. To make the base clear in your own files, use a marker:

- `0x10`, `$10` or `10h` for hex
- `0o20` or `20q` for octal
- `0b10000` for binary
- `"#16"` for decimal. The quotes are necessary, because `#` starts a TOML comment
- a `K` or `M` suffix, which is always a decimal count

To read and print in octal, set `base = "octal"` in `[console]`. This changes the base, not the
rule. `SHOW` prints a port in hex with a `0x` marker, for example `port 0x20`, whatever form you
wrote.

## `[console]`: your terminal, which is not a board

```toml
[console]
stop      = 0x05      # the STOP key, Ctrl-E. attn= also works
base      = "hex"     # hex or octal: how you read and write addresses, ports and bytes
upper     = false
strip7in  = false
strip7out = false
crlf      = false
echo      = false
bell      = true
bsdel     = "bs"
```

`[console]` is **not a `[[board]]`**, and it is not in the backplane. It describes *the terminal
where you type*, which is on your desk, at the other end of the connection. The Altair knows
nothing about it.

| Key | |
|---|---|
| `stop` | the byte of the STOP key. **Hex.** The default is `05`, which is `Ctrl-E`. `attn` is the same key |
| `base` | `hex` or `octal`: how the monitor reads and prints addresses, ports and bytes. `octal` is split octal, as on the MITS front panel |
| `history` | how many lines the monitor's command history file keeps. The default is 50. `0` stops saving it |
| `log` | a file that gets a copy of the session. `off` stops it |
| `upper` | change input to upper case |
| `strip7in` | clear bit 7 of every byte that the guest receives |
| `strip7out` | clear bit 7 of every byte that the guest sends |
| `crlf` | change line endings |
| `echo` | show typed characters locally |
| `bell` | let the guest ring the bell of your terminal |
| `bsdel` | `off`, `bs` or `del`: what your Backspace key sends |

**These settings are the only part of the program that changes a byte.** They belong to the
console, because a person reads text there. **Every serial line is 8-bit clean.** No board has a
setting that clears bit 7, because a line can carry XMODEM, and a line that clears bit 7 cannot
carry a file. For example, to fix the garbled `MEMORY SIZ?` prompt of MITS BASIC, set
`strip7out` on the console. Do not set `data_bits = 7` on the board.

When you connect a unit to something other than the console, these settings do not apply to it.
Examples are `CONNECT sio0:a socket:2323` and a real serial port. The other end gets the bytes
as the guest wrote them, with all eight bits. The serial chapter tells you why, and what to set
instead.

## `[display]`: your video window, which is not a board

```toml
[display]
focus = true
crt   = true
```

`[display]` is like `[console]`. It describes *the window on your screen*, not the board that
draws into it. A machine with two video boards still has one operator with one keyboard, so
these settings are in one place, not on each board. The settings apply to every video window of
the machine.

The size of a window is different. Each video board opens its own window, so its size is the
`width` property of that board. The boards chapter describes `width`.

| Key | |
|---|---|
| `focus` | whether the video window comes to the front and gets the keyboard when it opens. Default `false` |
| `keyboard` | whether the keys that you type in the window go to the machine's console: `console` (default) or `none` (display only) |
| `crt` | show the picture as on the original monitor, with a soft glow and a 4:3 shape, and not as sharp square pixels. Default `false` |

**`focus`.** With `focus = false`, the default, the terminal keeps the keyboard. The window
opens behind your other windows, and when the machine stops, you can type at `altairsim>` at
once. You can also click in the window and type there. Your keys then go to the same console as
the keys of the terminal. With `focus = true`, the window comes to the front when it opens, and
it keeps the keyboard when the machine stops. Use this for a **Sol-20**, where the window *is*
the console.

**`keyboard`** decides whether a video window is a keyboard at all. With `console`, the default,
the keys that you type in the window go to the console. This is correct for a **Sol-20**. Set
`none` for a board that only shows a picture, such as a **Dazzler**. The window still shows the
picture and still comes to the front, but its keys do **not** go to the console. They control a
joystick, if the machine has a `d7a`. Only `Ctrl-E` works in the window, to stop the machine and
give you the monitor.

**`crt`** changes how the window shows the picture. It does not change what the machine draws.
With `crt = false`, the default, each pixel is a sharp square, scaled by a whole number. With
`crt = true`, the window looks like a monitor of the period. The picture is stretched to a 4:3
shape, and the rows are softened into each other. A VDM-1 draws 512×208 and a VDB draws
640×240. Neither picture was square, because the tube stretched it to 4:3. Type
`SET DISPLAY crt=on` or `crt=off` at the monitor, and the open window changes at once. With
`crt = true`, a window with a `width` is that many pixels wide.

## Notes for the operator: `#>`

An ordinary `#` comment is for the person who *reads the file*. A comment that starts with
**`#>`** is for the person who *loads it*. The program prints its text when the machine loads,
before the `startup` commands run. Use it to tell the operator how to use the machine.

```toml
#> Boots CP/M 2.2 from drive A.
#> Type DIR at the A> prompt, and DIR B: for the blank second disk.

[machine]
name = "cpm22"
base = "default"
startup = ["RUN FF00"]
```

- **Each `#>` line prints as one line.** You can write as many as you want, together or in
  different places in the file. They print in the order of the file.
- **A `#>` with no text prints a blank line**, so that you can make short paragraphs.
- **A `#>` can follow a setting** on the same line: `name = "cpm22"  #> the buffered variant`.
- **Under `--mcp`, the notes go to stderr.** Stdout carries only the MCP messages there, but you
  still see the notes in the terminal.
- **A note sets nothing, and `CONFIG SAVE` does not save it.** To keep a note, keep the
  file that has it, and do not save over that file.

## A machine with no base

This small file has every part that a machine needs, and it loads. It has no `base`, so every
board is an ADD:

```toml
[machine]
name    = "tiny"

[[board]]
type = "fp"                # the front panel: the sense switches at port FF
id   = "fp0"
sense = 0x00

[[board]]
type     = "8080"          # the processor is a board. The crystal is on it.
id       = "cpu0"
clock_hz = 0               # 0 is as fast as possible. This is the default.

[[board]]
type = "2sio"              # the console board
id   = "sio0"
port = 10                  # HEX: port sixteen

  [board.unit.a]
  baud    = 9600           # DECIMAL: nine thousand six hundred
  connect = "console"

[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "16K"

[console]
strip7out = true
```

## Saving and loading at the monitor

```
altairsim> SET MACHINE name=mine
altairsim> CONFIG SAVE mine.toml
altairsim> CONFIG LOAD mine.toml
```

**`CONFIG SAVE` saves the machine that you are running now.** It saves every board and every
property, with every change that you made with `SET`. When you load the file, you get the same
machine. Give the machine a name with `SET MACHINE name=` before you save it.

**`CONFIG SAVE` marks each setting that has the default value.** The mark is a comment that
names the release the default belongs to:

```toml
baud = 9600  # default X.Y.Z
stop_bits = 2
```

Here `baud` has the default value, and `stop_bits` does not. A setting without a mark is a
value that was set on purpose. The comment does nothing when you load the file. A default can
change in a new release, so the mark tells you which release it was true for.

**`CONFIG SAVE` saves a new file each time. It does not edit the file that is there.** If you
save to the name of a machine file that has comments or a `base` line, these things from that
file are not in the new file:

- **Each `#` comment and each `#>` note.** The only comments in the saved file are the
  `# default` marks.
- **The `base` line.** The saved file contains every board in full, so that it does not need a
  base.

To keep that file as it is, save to a different name.

**`CONFIG LOAD` replaces the machine that you have**, the same as a machine file on the command
line. You cannot undo it, except with a file that you saved. It **loads all of the file or
nothing**. The program builds the new machine first. If the file does not load, you keep the
machine that you had.

To write a machine file fast, build the machine at the monitor with `BOARDS ADD` and `SET`, and
save it. The machines chapter shows the steps, under "From an empty backplane to a machine
file".

`STARTUP` builds the `startup` list at the monitor:

```
altairsim> STARTUP ADD MOUNT dsk0:drive0 "CP-M 2.2.dsk"
altairsim> STARTUP ADD RUN FF00
altairsim> STARTUP
  1  MOUNT dsk0:drive0 "CP-M 2.2.dsk"
  2  RUN FF00
```

`STARTUP ADD` adds a line as you typed it, with its quotes and spaces, because a startup entry
is a command line. `STARTUP REMOVE <n>` removes one line, and `STARTUP CLEAR` removes them all.
`CONFIG SAVE` saves the list as `startup = [...]`, so the file boots the machine in the way
that you tested at the monitor.
