# Machines

A **machine** is a backplane with boards in it. Which boards, with what settings, in what
state — that is all a machine is, and it is the only thing `altairsim` needs to be told.

You tell it in one of three ways: name a **built-in**, name a **file**, or say **nothing** and
take the default. This chapter is about how that choice is made, and about the one rule that
decides what a path means.

## The command line

```
altairsim [options] [machine]
```

| Option | |
|---|---|
| `machine` | a built-in name, **or** a config file if it contains a `/` or ends in `.toml` |
| `-m, --machine <name>` | **always** a built-in name — never a file |
| `-f, --file <path>` | **always** a file — never a built-in name |
| `-n, --none` | an empty backplane. No boards, no memory, nothing |
| `-l, --list` | list the built-in machines and exit |
| `-s, --script <file>` | run a command script, then exit with its status |
| `-x, --exec <cmd>` | run one monitor command, then exit. Repeatable |
| `-i, --interactive` | after `--script`/`--exec`, stay in the monitor |
| `--mcp` | run as an MCP server on stdio |
| `-v, --version` `-h, --help` | |

**Give exactly one machine.** A positional name *and* a `-m`, or a `-f` *and* a `-n`, is an
error — the program says *give ONE machine* and stops. It does not guess which one you meant.

## How a bare word resolves — and why it never looks at your disk

```
$ altairsim basic4k                     a BUILT-IN, by name
$ altairsim {{MACHINE_CPM}}             a FILE, by path
```

The rule is **purely syntactic**. The filesystem is **never probed**:

| The word | What it is |
|---|---|
| contains `/` or `\` | a **file** |
| ends in `.toml` | a **file** |
| anything else | a **built-in name** |

That is deliberate, and it is worth being clear about why, because a simulator that guessed
would be more convenient exactly until the day it was not.

**`altairsim basic4k` means `basic4k` in every directory on earth.** It means the same thing on
your machine and on mine. It cannot be hijacked by a file called `basic4k` that happens to be
lying next to you — because the program never asks whether such a file exists. A command in a
script, a line in a README, a habit in your fingers: all of them keep meaning what they meant.

The cost is that `altairsim mymachine.toml` needs the extension, and `altairsim ./mymachine`
needs the `./`. That is a small price, and if you want the question settled explicitly, settle
it:

```
$ altairsim -m basic4k                  a built-in. Stop looking for a file
$ altairsim -f ./basic4k                a file called `basic4k`, no extension, right here
```

`-m` and `-f` are how you say what you mean when the syntax will not say it for you.

## The one file the simulator *finds*

If you name **nothing at all**, and the working directory contains a file called
`altairsim.toml`, that machine is loaded — and it says so:

```
$ altairsim
altairsim 0.1.0 (v0.1.0-37-gcc64cca) -- 8080, full speed.
machine: ./altairsim.toml
altairsim>
```

This is the **only** file the simulator finds rather than is given, and it only happens when
the command line names nothing whatsoever. Name a built-in, a file, or `-n`, and `./altairsim.toml`
is ignored — you asked for something, so you get it.

Put one in a project directory and `altairsim`, bare, is your machine. **It announces itself
when it does**, so you are never running something you did not know about.

With no `altairsim.toml` and no arguments, you get the built-in `default`.

## The built-in machines

A **built-in is a TOML machine file compiled into the binary.** There is nothing privileged
about it: same format, same keys, same rules as one you write yourself. It is in the program
only so that it is always there.

```
$ altairsim --list
```

names them. They are `default`, `4k`, `altmon`, `basic4k`, `basic8k`, `ps2`, `ps2int`,
`minidisk`, `lineprinter`, `cuter`, `vdm1`, `sol20` and `z80`; the machine reference at the back
of this manual says what each one is.

To see what is in one:

```
$ altairsim -x 'SHOW MACHINE' basic4k
```

And to get it as **text you can edit** — the actual machine file, every board, every setting:

```
$ altairsim -x 'CONFIG SAVE mine.toml' basic4k
$ altairsim mine.toml
```

`CONFIG SAVE` writes the machine you are actually running, and it round-trips. **Which makes
every built-in a worked example.** Find the one closest to what you want, save it out, and edit
it.

Or better, do not copy it at all — start *from* it with `base`, and write down only what is
different. The configuring chapter is about that.

## The empty backplane — `-n`

```
$ altairsim -n
```

No boards. No memory. No processor. `-n` is a bare chassis, and every `BOARDS ADD` from there
is yours. It is the honest starting point when you are building a machine up board by board,
and it is the one way to be certain nothing is in there that you did not put there.

## The path rule, and it has two halves

This is the rule that lets an example directory be copied anywhere and still boot. It is one
principle stated twice:

> **A path written inside a machine file is relative to that file.**
>
> **A path you type at the prompt is relative to your shell.**

Both are true at once, and they do not conflict, because they are the same idea: **a path means
what its author could see.**

The author of a machine file could see the directory the machine file is in. When
`{{MACHINE_CPM}}` says `mount = "cpm22b23-56k.dsk"`, it means *the disk lying next to me* — and
it goes on meaning that after you copy the folder to your desktop, rename it, or mail it to
someone. That is why the examples are self-contained directories, and why the quick start's
`cp -R` actually works.

You, at the prompt, can see your own working directory. When you type

```
altairsim> MOUNT dsk0:drive1 scratch.dsk
```

you mean *the file next to me*, because that is the only thing `scratch.dsk` could sensibly
mean when you are the one typing it. The simulator does not go rummaging in the machine file's
directory for a file you named with your own hands.

The corollary is worth stating, because it catches people: **a path in a `startup` command is
inside the machine file, so it is relative to the machine file** — even though `startup`
commands look exactly like things you type. They were written by the file's author, so they
mean what the file's author could see.

### When it bites, and what it looks like

The rule is invisible until a file is missing, and then it can look like a typo that is not one.
Keep your machine files in a `machines/` folder, write a path meaning *the folder you launched
from*, and you get the one confusing case:

```toml
[[board.drive]]
unit  = 0
mount = "disks/Kermit/cpm.dsk"      # meant: the disks/ I can see from my shell
```

`altairsim -f ./machines/8800c.toml` then says:

```
./machines/8800c.toml: dsk0: 'machines/disks/Kermit/cpm.dsk': no such file
  ('disks/Kermit/cpm.dsk' is relative to the machine file that wrote it, in ./machines/)
```

**The disk is not missing.** It was looked for beside the machine file, because that is where the
rule says a machine file's paths point. Write it the way the machine file sees it:

```toml
mount = "../disks/Kermit/cpm.dsk"   # up out of machines/, then down into disks/
```

…or keep the machine file next to what it mounts, which is what every shipped example does.

Note what is *not* affected: once the machine is up, `MOUNT dsk0:drive1 disks/Kermit/cpm.dsk`
typed at the prompt needs no `../`, because you are the one typing it. The `../` belongs to the
file, not to you.

### Ask the machine, rather than working it out

You do not have to hold this in your head. `SHOW PATHS` prints every base at once, for the
machine you are actually running:

```
altairsim> SHOW PATHS
  what you type      /home/you/altair
                     MOUNT, LOAD, SAVE and -s scripts resolve against this.

  machine file       /home/you/altair/disks/cpm22
                     `mount`, `base` and the MOUNT/LOAD lines in `startup`
                     resolve against THIS, not the cwd -- so a machine file
                     names the disks lying beside it and goes on naming them
                     from wherever you launch it.

  hb0 sandbox        /home/you/altair/disks/cpm22/xfer
                     THE GUEST'S SANDBOX, and the only real fence here:
                     R.COM/W.COM cannot leave it. It is not a base for
                     anything you type. Set with `hostdir`.
```

Three lines, three different directories, and they are allowed to differ — that is the rule
working, not a fault.

Boot a **built-in** machine and the middle line says so, because then there is no file and
nothing was re-based:

```
  machine file       (none -- this machine is built in to the binary)
```

`SHOW MOUNTS` is the companion: every disk, tape and ROM in the machine and what is in each,
across all the boards at once.

```
altairsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  dsk0:drive0  disk  cpm22b23-56k.dsk
  dsk0:drive1  disk  (empty)
  dsk0:drive2  disk  (empty)
  dsk0:drive3  disk  (empty)
  mem0:rom0    rom   builtin:dbl  (read-only)

  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.
```

**Empty drives are listed, not hidden.** The 88-DCDD has four, one disk is in it, and the other
three doors are open — which is the machine, and worth seeing.

That last line is the command telling you what the middle column is worth, and it is why the two
belong together: `SHOW MOUNTS` tells you what the machine was told, and `SHOW PATHS` tells you
what that meant.

### None of this is a sandbox

The path rule decides **where a path points**, and confines nothing. A machine file may mount any
file on your disk — with `..`, or with an absolute path — and it will be opened.

The one real fence is the Host Bridge's **`hostdir`**, which limits how far a CP/M program running
*inside* the machine can reach when it reads and writes host files. That is a different mechanism
for a different purpose — see *Moving files in and out* — and nothing you write in a machine file
moves it.

## Running a command and leaving — `-x` and `-s`

`altairsim` does not have to be interactive.

```
$ altairsim -x 'SHOW MACHINE' default
$ altairsim -x 'DUMP 0 F' {{MACHINE_CPM}}
```

`-x` runs one monitor command against the machine and exits. It is **repeatable**, and the
commands run in the order you gave them:

```
$ altairsim -x 'MOUNT dsk0:drive0 mine.dsk' -x 'RUN FF00' -i {{MACHINE_CPM}}
```

`-i` is the difference between a query and a start-up: without it the program exits when the
commands are done; with it you are dropped into the monitor with the machine exactly as your
commands left it. `-i` alone, with no `-x` or `-s`, does nothing.

`-s` runs a **script** — a file of monitor commands, one per line, the same ones you type:

```
$ altairsim -s boot.cmd {{MACHINE_CPM}}
```

**The exit status is non-zero if any command failed.** That is the whole point: `altairsim -s`
is a program you can put in a shell script, a Makefile, or a build, and test the result of.

```sh
if altairsim -s check.cmd mine.toml; then
    echo "machine is sane"
fi
```

## Which chapter next

The **configuring** chapter is the machine file itself: every table, every key, and the four
things a `[[board]]` entry can mean. The **boards** chapter is what the fourteen boards *are*.
