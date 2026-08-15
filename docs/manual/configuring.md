# The machine file

A machine file is **TOML**. It lists the boards in the backplane, what is set on each of them,
and what to do once the power is on. That is all it does, and there is nothing else it can do.

This chapter is the normative description of the format.

## What TOML is (and why this file is one)

The machine file is written in **TOML** — *Tom's Obvious, Minimal Language*, a
configuration format Tom Preston-Werner published in 2013 and settled at version 1.0 in
2021. We picked it the way you pick a good wrench: it is honest, it fits the hand, and it
never pretends to have done something it didn't. A file you write by hand and read back a
year later without a manual — that is the whole job, and TOML keeps the one promise the
rest of this chapter also makes: it means exactly what it says, or it refuses to load.

You need only a handful of rules to read every example below:

- **`key = value`** — one setting per line: `clock_hz = 2000000`.
- **Quotes are for text.** A string is quoted (`name = "cpm22"`); a number or a
  true/false is bare (`size = 256`, `idle = true`).
- **`[table]` appears once** — `[machine]` is the machine, `[console]` is your terminal.
- **`[[table]]` — doubled brackets — repeats.** Each `[[board]]` starts another board.
- **A nested `[board.unit.x]` belongs to the block above it**, by name — the indentation
  in these examples is only for the eye.
- **`#` starts a comment** to the end of the line. A comment that begins **`#>`** is a
  *note* — the file prints it to you when it loads (see below).

That is the whole of the syntax. Everything below is which keys go where.

## The one thing to know first

**Anything you can type, a config can do — and nothing more.** A machine file has no special
powers. It cannot boot a disk, because there is no `BOOT` verb; what it can do is *type
`RUN FF00` for you*, which is what the operator did. Every board setting in a machine file is
a setting you could have made with `SET` at the prompt, and every `SET` you make at the prompt
is a key you could have written in the file. **A board's properties *are* its TOML keys.**

There is no separate config schema anywhere in the program. That is why the board reference at
the back of this manual is exhaustive, and why it cannot drift out of date: it is printed from
the same table the monitor resolves against.

## Nothing is silently ignored

**Any unknown table or key is a hard error, with a sentence saying so.** The machine does not
load.

```
mine.toml: unknown [machine] key 'widget'
mine.toml: [[board]] cpu0: cpu0 has no property 'frobnicate'. Known: clock_hz idle achieved_hz
```

This is not pedantry. **A configuration that looks like it set something and did not is worse
than one that will not load**, because you will spend the afternoon debugging the machine
instead of the typo. A misconfiguration in this program cannot be silent.

## Notes the operator sees — `#>`

An ordinary `#` comment is for whoever *reads the file*. A comment that begins **`#>`** is for
whoever *loads it*: its text is printed to you when the machine comes up, right after the
`loaded …` line and before the `startup` commands run. It is where the file's author leaves the
one or two sentences you need to actually use the machine.

```toml
#> Boots CP/M 2.2 from drive A.
#> Type DIR at the A> prompt, and DIR B: for the blank second disk.

[machine]
name = "cpm22"
base = "default"
startup = ["RUN FF00"]
```

- **One `#>` line is one printed line.** Write as many as you like, in a row or scattered
  through the file — they print in the order they appear.
- **`#>` on its own prints a blank line**, so you can space a note into a short paragraph.
- **A `#>` can trail a setting**, too: `name = "cpm22"  #> the buffered variant`.
- It is still a comment. It **sets nothing**, and `CONFIG SAVE` does not write it back — a
  saved machine is the backplane, not the prose around it. If a note is worth keeping, keep it
  in the file you wrote by hand.

An ordinary `#` comment, as always, is seen by no one but the reader of the file.

## The tables

| Table | What it is |
|---|---|
| `[machine]` | the machine's identity. Three keys, no more |
| `[[board]]` | a board. One entry per board |
| `[board.unit.<name>]` | one unit *on* the board above — a serial channel, a tape deck |
| `[[board.region]]` | a memory region on a `memory` board |
| `[[board.drive]]` | a drive on a disk controller |
| `[console]` | **your terminal.** Not a board — see below |
| `[display]` | **your video window.** Not a board either — see below |

## `[machine]` — and it has exactly three keys

```toml
[machine]
name    = "cpm22"
base    = "default"
startup = ["RUN FF00"]
```

### `name`

What the machine is called. That is the whole of it.

### `base` — start from a machine, and say what is *different*

```toml
base = "default"          # a built-in
base = "../cpm22/cpm22.toml"   # or a file
```

The value resolves by **the same syntactic rule as the command line**: contains a `/` or ends
in `.toml` → a file; otherwise → a built-in name. (The machines chapter explains why the
filesystem is never probed.) A file path is relative to *this* file.

Two rules about where it goes:

- **`base` is processed before every other key**, whatever order the file is written in.
- **`base` must appear before the first `[[board]]`.** You cannot inherit a backplane you have
  already started modifying.

`base` nests **up to 8 deep**. A machine built on a machine built on `default` is fine.

This is the key that makes the format worth using. Without it, every variant of a machine is a
copy of a hundred lines, and the day you change one of them you change it in six files. With
it, a machine file says only **what is different**, and reads as the diff it actually is.

### `startup` — the operator's keystrokes, written down

```toml
startup = ["RUN FF00"]
```

An array of **ordinary monitor commands**, run once the machine is built. Any command. They run
in order, and you see them run — that is the `startup>` line in the quick start.

**Paths inside a `startup` command are relative to the machine file**, not to your shell, because
the file's author wrote them and the file's author could see the file's directory. This is the
one place the two halves of the path rule sit next to each other, and it is worth remembering.

### What `[machine]` will *not* take

```toml
[machine]
clock_hz = 2000000        # ERROR
sense    = 0x80           # ERROR
```

Both are **rejected, with an explanation**:

```
mine.toml: clock_hz belongs to the CPU BOARD, not to [machine] --
  the crystal is on the board. Put it in the CPU's [[board]]:
      [[board]]
      type     = "8080"
      id       = "cpu0"
      clock_hz = 2000000
```

The crystal is soldered to the **88-CPU card**. The sense switches are on the **front panel**.
Neither is a property of "the machine" — the machine is just the box they are plugged into. If
you pull the CPU card out, the crystal goes with it.

These two get a bespoke error rather than the generic *unknown key* because they are the two
people reach for first, and being told *where the thing actually lives* is more use than being
told it isn't here.

## `[[board]]` — and it has four forms

This is the heart of the format. **What a `[[board]]` entry means depends on whether it has a
`type`, and whether its `id` is one the base already used.**

| Write | And it means |
|---|---|
| `type` + a **new** `id` | **ADD** the board |
| `type` + an id **from the base** | **REPLACE** the board outright |
| **no** `type` + an id | **MODIFY IN PLACE** |
| `remove = true` + an id | **PULL THE BOARD OUT** |

**`id` is always mandatory.** It is how you refer to the board at the prompt, and how a later
file refers to it here.

### ADD — `type` + a new id

```toml
[[board]]
type = "virtc"
id   = "vi0"
```

A board that was not there is now there. **In a file with no `base`, this is the only form** —
there is nothing to modify, replace or remove.

### REPLACE — `type` + an id the base already used

```toml
[[board]]
type = "2sio"
id   = "sio0"
port = 0x20
```

If the base had a board called `sio0`, it is **gone** — pulled out and thrown away — and a fresh
`2sio` is fitted in its place. **Everything the base set on that board is lost**, including the
settings you did not mention. You get the type's defaults, plus whatever you write here.

That is what "replace" means, and it is almost never what you want. You want:

### MODIFY IN PLACE — no `type`

```toml
[[board]]
id   = "cpu0"
clock_hz = 2000000
```

**Leave the `type` out and you are reaching into the board that is already there.** Everything
the base set on `cpu0` stays set; you change the crystal and nothing else.

The absence of `type` is the whole signal. It reads oddly for about a day and then reads as
exactly what it is: *I am not fitting a board, I am adjusting one.*

### REMOVE — `remove = true`

```toml
[[board]]
id     = "acr0"
remove = true
```

The board is pulled out of the backplane. Its ports stop being decoded. Nothing else in the file
may mention it.

### The error that catches a copy-paste

**`type` + an id that *this same file* has already declared is an error.** Not a replace — an
error. Within one file, declaring the same board twice is never something you meant; it is a
block you copied and forgot to rename. The file will not load, and it will tell you which id.

(Across files it is different: an id from your *base* is a board you inherited, and replacing it
is a legitimate thing to want.)

## Everything else on a `[[board]]` is a property

`type`, `id` and `remove` are the only keys the config layer understands. **Every other key is
handed straight to the board.**

```toml
[[board]]
type = "2sio"
id   = "sio0"
port = 0x10          # the 2sio knows what a port is. The config layer does not.
```

The config layer knows nothing about ports, baud rates, sense switches or drive counts. It
cannot, and it does not try. It routes the key to the board and the board accepts it or
rejects it by name:

```
mine.toml: [[board]] cpu0: cpu0 has no property 'frobnicate'. Known: clock_hz idle achieved_hz
```

**The full key list for every board is the board reference at the back of this manual.** The
boards chapter says what the boards *are*.

## `[board.unit.<name>]` — settings that belong to one unit

Some boards carry more than one independent thing. An 88-2SIO is **two 6850 ACIAs**, not one chip
with two channels: unit `a` and unit `b` have their own baud rate, their own interrupt strap,
their own endpoint, and they share nothing at all. So they get their own tables.

```toml
[[board]]
type = "2sio"
id   = "sio0"
port = 0x10                    # the BOARD's property -- both chips live at this base

  [board.unit.a]
  baud    = 9600               # channel A's property
  connect = "console"

  [board.unit.b]
  baud    = 1200               # channel B is a different chip. It does not care.
  connect = "socket:2323"
```

A key in `[board.unit.a]` is exactly the key `SET sio0:a baud=9600` takes at the prompt. It is
the same property, reached two ways.

The board reference lists which boards have units, and what each unit takes.

## `[[board.region]]` — memory

A `memory` board is **a list of regions**, which is why one physical card can carry 56K of RAM
and a boot PROM at the top of memory. The regions are the board.

```toml
[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0x0000            # HEX -- it is an address
  size = "56K"             # DECIMAL -- it is a count

  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  size  = 256
  mount = "turnmon.bin"    # relative to THIS FILE
```

| Key | |
|---|---|
| `type` | **required.** `ram` or `rom` |
| `at` | the address it decodes. **Hex** |
| `size` | how big. **Decimal**; `K` and `M` suffixes work |
| `mount` | a ROM image: a file path, or `builtin:<name>` |

(A size with a suffix is written as a string — `size = "56K"` — because `56K` is not a number
TOML will accept bare. A plain count needs no quotes: `size = 256`.)

### An empty socket

**A `rom` region with no `mount` is an empty socket.** It decodes nothing, and reads there float
to `FF` — because that is what an S-100 bus with nobody driving it does. It is not zeros, and it
is not an error. It is an unpopulated socket on a card that has one, which is a thing a real
machine could be, and software that reads it gets `FF`.

## `[[board.drive]]` — disks

A disk controller addresses drives; a drive holds an image.

```toml
[[board]]
id = "dsk0"

  [[board.drive]]
  unit     = 0             # DECIMAL -- it is a drive number
  mount    = "cpm.dsk"     # relative to THIS FILE
  readonly = false
```

| Key | |
|---|---|
| `unit` | the drive number. **Decimal** |
| `mount` | the image file |
| `readonly` | refuse every write at the controller, so the host file cannot change. For a disk you mean to read — see the disks chapter. `writeprotect` is the same key under the name the rest of the program uses; write either |
| `media` | force a format instead of probing the image |
| `create` | make the file, empty, if it is not there — then mount it. `MOUNT … CREATE`, written down |

`media` is the escape hatch. The controller normally works out the format from the image, and
normally it is right; when it is not — a headerless image, an unusual geometry — you say so.
It is also what says how big a **blank** disk is, since a blank one matches no format at all.
The disks chapter covers the formats, and `create`.

Without `create`, a `mount` naming a file that is not there is an **error and the machine does
not load** — the same rule as everywhere else here, that a thing which looks like it worked
and did not is the worst outcome available.

## Numbers: one rule, and it is not negotiable

> **On the wire → HEX. Never on the wire → DECIMAL.**

An address, a port, a data byte, a sense-switch setting is something the 8080 sees on the bus.
It is written **hex**, and it is written hex *without a prefix*:

```toml
port  = 10        # 0x10. SIXTEEN. This is the 2SIO's default port.
at    = 0xFF00    # an address
sense = 80        # 0x80
```

A count, a size, a baud rate, a drive number is a **quantity**. The 8080 never sees it. It is
written **decimal**:

```toml
baud   = 9600     # nine thousand six hundred
drives = 4        # four
size   = 56       # fifty-six bytes
unit   = 0
```

**`port = 10` is port sixteen.** Read that line again, because it is the one that will get you,
and it is the one the rule exists to make predictable. A port is on the wire. Ports are hex. The
88-2SIO lives at 10 hex and every listing from 1976 writes it that way.

If you want to be explicit — and in your own files, be explicit — say so:

| | |
|---|---|
| `0x10`, `$10`, `10h` | **hex**, whatever the key |
| `0o20`, `20q` | **octal**, whatever the key |
| `0b10000` | **binary**, whatever the key |
| `#16` | **decimal**, whatever the key |
| `56K`, `1M` | always **decimal**. A suffix implies a count |

The board reference prints every default **in its own base**, so there is never a question about
which one a given key is.

### Octal, if you prefer it

The rule above is about **classes** — which side of the line a number falls on. It is not about
which base you *type*, and every marker in that table works in a machine file. So you can write a
machine the way its own documentation wrote it:

```toml
port  = 0o20              # the 2SIO, as a MITS manual would have printed it
sense = 0b10001110        # eight switches, eight digits
```

That is how you write the file. How the **monitor** reads and prints back at you is a separate
setting, and it is a key in the machine file too — so a machine can start in octal and stay
there:

```toml
[console]
base = octal
```

Now a bare number the 8080 can see is octal both ways: `EXAMINE 100` means address `0o100`, and
addresses, ports and data bytes come back in **split octal**, each byte its own `000`–`377`
group, the way the front-panel lamps are grouped. Counts and baud rates are unaffected — they
were never in that class. `[console]` is the section after next, and the monitor chapter shows
what a session in octal looks like.

Board settings keep their own base regardless: `SHOW` prints a port as `0x20` whichever way you
wrote it, because a property's base belongs to the property.

## `[console]` — your terminal, which is not a board

```toml
[console]
attn      = 0x05      # the key that gets you back to the monitor. ^E
base      = hex       # hex | octal -- how you read/write addresses, ports, bytes
upper     = false
strip7in  = false
strip7out = false
crlf      = false
echo      = false
bell      = true
bsdel     = "off"
```

`[console]` is **not a `[[board]]`** and it is not in the backplane. It describes *the terminal
you are sitting at* — a piece of equipment on your desk, on the far end of a cable, in 2026. The
Altair never knew anything about it.

| Key | |
|---|---|
| `attn` | the escape byte. **Hex.** Default `05` = `^E` |
| `base` | `hex` \| `octal` — how the monitor reads and prints the wire class (addresses, ports, bytes). `octal` is split octal, the MITS front-panel convention |
| `upper` | fold input to upper case |
| `strip7in` | clear bit 7 of everything the guest receives |
| `strip7out` | clear bit 7 of everything the guest sends |
| `crlf` | translate line endings |
| `echo` | echo typed characters locally |
| `bell` | let the guest ring your terminal's bell |
| `bsdel` | `off` \| `bs` \| `del` — what your Backspace key sends |

Because they are the terminal's, they reach as far as the terminal does and no further: send a
board's console unit somewhere else — `CONNECT sio0:a socket:2323`, or out a real serial port —
and the far end gets the bytes exactly as the guest wrote them, all eight bits. The settings are
not undone; the byte just no longer passes through the console, and every line in the machine is
8-bit clean. The serial chapter has the full story, and what to set instead.

## `[display]` — your video window, which is not a board either

```toml
[display]
focus = true
crt   = true
```

Same idea as `[console]`, one table down: it describes *the window on your desk in 2026*, not the
card that draws into it. A VDM-1 has no opinion about window managers, and a machine with two
video boards still has one operator with one keyboard — so these settings live here, once, rather
than on each board. (Window *size* is the exception: how big a picture opens is the board's own,
so `width` is a property of each video board — see [Boards](boards.md).)

| Key | |
|---|---|
| `focus` | whether the video window comes to the front and takes keyboard focus when it opens. Default `false` |
| `keyboard` | whether a focused window's keystrokes reach the machine's console: `console` (default) or `none` (display-only). See below |
| `crt` | paint the window like the original monitor — scan lines and the tall 4:3 tube — instead of crisp square pixels. Default `false`. See below |

With `focus = false` — the default — the terminal keeps the keyboard. The window opens behind
whatever you were doing, and when the guest stops you can type at `altairsim>` immediately. You
can still click into the window and type there; the keys join the terminal's on one stream.

With `focus = true` the window comes to the front when it opens and keeps the keyboard when the
guest stops. That is what a **Sol-20** wants, because there the window *is* the console and the
terminal is the back door.

How *big* the window opens is set per video board, not here — see the `width` property in
[Boards](boards.md). Each board's video-out could drive its own monitor on a real Altair, so the
size belongs to the board whose picture it frames.

`keyboard` decides whether a video window is a *keyboard* at all — which is separate from whether it
has focus. Default `console`: a focused window is a keyboard, and its keys join the terminal's on the
one console stream, which is right for a **Sol-20** where the window *is* the console. Set it to
`none` for a **display-only** board like a **Dazzler**: the window still shows the picture and still
comes to the front, but its keystrokes do **not** reach the console — they drive a joystick (a
`d7a`, if the machine has one), and only `Ctrl-E` in the window is honored, stopping the guest and
handing you back the monitor. This is why typing in a Dazzler game window does not land at the CP/M
prompt.

`crt` changes how the picture is *painted*, not what the machine draws. With `crt = false` — the
default — you get today's look: the board's pixels as crisp squares, scaled up a whole number of
times so a 1970s pixel stays a sharp square on a modern panel. Set it to `true` for the period
monitor instead. The short, wide raster these boards scan — a VDM-1 is 512×208, a VDB is 640×240 —
was never square: it was stretched to fill a 4:3 tube, and you saw the gap between each scan line.
`crt = true` reproduces both, stretching the picture to the 4:3 shape and laying a dark line
between the rows. It is a matter of taste — some prefer the crisp look, some the tube — and you can
flip it live with `SET DISPLAY crt=on` / `off` while the machine runs; the open window re-fits at
once. Both looks share one window size, set by the board's `width`.

On a build without SDL3 these keys are still accepted and simply have no window to apply to, so a
machine file that asks for them stays portable.

## The transform chain belongs to the console, and only to the console

This section is here because the temptation to solve it in the wrong place is very strong, and
solving it in the wrong place silently corrupts your data.

**MITS BASIC sets bit 7 of the last character of every message**, as a string terminator. That is
not a bug. It is how the interpreter marks the end of a prompt. Send all eight bits to a modern
terminal and every prompt in the program arrives wearing garbage:

```
MEMORY SIZ?
```

The fix is **`strip7out` on the `[console]`**:

```toml
[console]
strip7out = true
```

And the reason that is the *right* fix — rather than a plausible one — is that it is what
actually happened. **The real machine sent all eight bits.** The 88-SIO put the byte on the wire
exactly as BASIC handed it over. And the **Teletype ignored the eighth**, because a Model 33 is
a 7-bit terminal and always was. The stripping was done by the terminal, at the far end, in
1976. So it is done by the terminal here.

Now the two wrong fixes, because they both look reasonable and they are both traps:

- **It is not a 7-bit strap on the card.** You could set `data_bits = 7` on the SIO and the
  prompt would come out clean. You would also have quietly made that port unable to carry a
  binary byte — and the day you run XMODEM through it, every byte with the top bit set is
  mangled. **Line coding is a *frame*, not a *mask*.** It is real hardware and it is modelled,
  but it is not this.
- **It is not a filter on the line.** Same failure, one layer up, and just as silent.

**Every serial line in this simulator is 8-bit clean**, because a line carries XMODEM, and a
line that eats bit 7 is a line you cannot trust with a file. The transforms — `strip7out`,
`upper`, `crlf` and the rest — are properties of the **console**, and the console is the one
place in the system where mangling bytes for a human's benefit is the correct thing to do.

## A complete machine file

Small, whole, and it works. No `base` — so every board is an ADD:

```toml
[machine]
name    = "tiny"
startup = ["RUN 0"]

[[board]]
type = "fp"                # the front panel: the sense switches at port FF
id   = "fp0"
sense = 0x00

[[board]]
type     = "8080"          # the CPU is a board. The crystal is on it.
id       = "cpu0"
clock_hz = 0               # 0 = flat out. This is the default.

[[board]]
type = "2sio"              # the console board
id   = "sio0"
port = 10                  # HEX. Port SIXTEEN.

  [board.unit.a]
  baud    = 9600           # DECIMAL. Nine thousand six hundred.
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

## A `base` delta — and this is the one you will actually write

This is genuine. It is how the CP/M example is built, and it is nine lines:

```toml
[machine]
name    = "cpm22"
base    = "default"        # a front panel, an 8080, a 2SIO console, a floppy
                           # controller, 56K of RAM and the boot PROM at FF00
startup = ["RUN FF00"]     # the operator's own keystrokes, written down

[[board]]
id = "dsk0"                # NO type: modify the controller the base already has

  [[board.drive]]
  unit  = 0
  mount = "cpm.dsk"        # relative to THIS FILE
```

Everything a 56K CP/M machine is, `default` already was. The only thing this file has to say is
*which floppy is in drive 0*, and *press RUN at FF00*. **That is the whole of the difference,
and so that is the whole of the file.**

Note what is not here: no `type` on the `[[board]]`, because `dsk0` already exists and we are
adjusting it, not fitting it. Had we written `type = "dcdd"`, we would have thrown the base's
controller away and got a fresh one with default settings — and it would still have worked, and
we would never have known we had done it. Leave the `type` out.

## Saving and loading at the prompt

```
altairsim> CONFIG SAVE mine.toml
altairsim> CONFIG LOAD mine.toml
```

**`CONFIG SAVE` writes the machine you are actually running** — every board, every property, as
it stands right now, including everything you changed with `SET` since you started. It
**round-trips**: load what it wrote and you get the machine back.

**`CONFIG LOAD` is the whole machine, so it replaces the one you have** — the same thing that
naming the file on the command line does, and there is no undo but the file you saved it to.
It is also **all or nothing**: the machine is built off to one side first, so a file that will
not load leaves you exactly where you were rather than halfway between two machines.

Which makes it the fastest way to write a machine file. Build the machine at the prompt with
`BOARDS ADD` and `SET` until it is what you want, then save it, then edit the file down to the
parts you care about — or give it a `base` and delete the rest.
