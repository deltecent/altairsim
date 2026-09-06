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
| `-v, --version` | print the version and exit |
| `-h, --help` | print this help and exit |

**Give exactly one machine.** A positional name *and* a `-m`, or a `-f` *and* a `-n`, is an
error — the program says *give ONE machine* and stops. It does not guess which one you meant.

## How a bare word resolves — and why it never looks at your disk

```
$ altairsim basic4k                     a BUILT-IN, by name
$ altairsim examples/cpm/cpm22-buffered.toml             a FILE, by path
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
altairsim: no machine named -- using ./altairsim.toml (`-m default` for the built-in).
AltairSim 1.0.0 -- 8080, full speed.
machine: bench.  HELP for commands.
altairsim>
```

**The first line is the announcement**, and it is printed before anything else so you cannot
miss it. The `machine:` line after it names the machine *the file* declares — `bench` here,
not the file it came out of — so the two lines together say both halves: where it came from,
and what it turned out to be.

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

names them, one to a line, each with a sentence saying what it is — and it is the live list,
so it cannot be short of a machine the way a list typed into a chapter can. The machine
reference at the back of this manual is that same table. From the `altairsim>` prompt the
list is `SHOW MACHINES`.

To see what is in one — its backplane and its startup — name it:

```
$ altairsim -x 'SHOW MACHINE' basic4k
```

or, at the prompt, `SHOW MACHINE basic4k`. A bare `SHOW MACHINE` there is the machine you are
running now.

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

## The path rule: one base directory

This is the rule that lets an example directory be copied anywhere and still boot. It is one
sentence:

> **A relative path resolves against the machine's directory** — the folder the machine file
> was loaded from.

That folder is the base for *everything*: the disks and PROMs the machine file itself mounts,
**and** the `MOUNT`, `LOAD`, `SAVE`, `DO` and `-s` paths you type at the prompt. One directory,
one answer, whether the path was written by the file's author or by you.

When `examples/cpm/cpm22-buffered.toml` says `mount = "cpm22b23-56k.dsk"`, it means *the disk in
this folder* — and it goes on meaning that after you copy the folder to your desktop, rename it,
or mail it to someone. That is why the examples are self-contained directories, and why the
quick start's `cp -R` actually works. And when you then type

```
altairsim> MOUNT dsk0:drive1 cpm22b23-56k.dsk
```

you get the **same file**, from the **same folder** — the one the machine came from — no matter
which directory you launched `altairsim` from. Typed paths used to resolve against your shell
instead, which is how the identical disk could show up under two different names; that split is
gone.

A **built-in** machine has no directory of its own, so its base is the directory you launched
from — the only anchor it has.

### When it bites, and what it looks like

The rule is invisible until a file is missing, and then it can look like a typo that is not one.
Keep your machine files in a `machines/` folder, write a path meaning *the folder you launched
from*, and you get the one confusing case:

```toml
[[board.drive]]
unit  = 0
mount = "disks/Kermit/cpm.dsk"      # meant: the disks/ up beside machines/
```

`altairsim -f ./machines/8800c.toml` then says:

```
./machines/8800c.toml: dsk0: 'machines/disks/Kermit/cpm.dsk': no such file
  ('disks/Kermit/cpm.dsk' is relative to the machine's directory, ./machines/)
```

**The disk is not missing.** It was looked for beside the machine file, because that is the
machine's directory and that is where relative paths point. Write it the way the machine sees it:

```toml
mount = "../disks/Kermit/cpm.dsk"   # up out of machines/, then down into disks/
```

…or keep the machine file next to what it mounts, which is what every shipped example does. The
same `../` applies whether the path is in the file or you type it — because both resolve against
the one base.

### Ask the machine, rather than working it out

You do not have to hold this in your head. `SHOW PATHS` prints the base, for the machine you are
actually running:

```
altairsim> SHOW PATHS
  base directory     /home/you/altair/disks/cpm22
                     Everything resolves against this -- what a machine file
                     mounts, and the MOUNT / LOAD / SAVE / DO / -s you type.
                     It is the directory the machine was loaded from.

  hb0 sandbox        /home/you/altair/disks/cpm22/xfer
                     THE GUEST'S SANDBOX, and the only real fence here:
                     R.COM/W.COM cannot leave it. It is not a base for
                     anything you type. Set with `hostdir`.
```

Two entries, and only the second is a fence — the base is where paths point, the sandbox is
where the guest is confined.

Boot a **built-in** machine and the base is the directory you launched from, because a built-in
carries no folder of its own:

```
  base directory     /home/you/altair
                     ...
                     This machine is built in, so it is the directory you
                     launched from.
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
$ altairsim -x 'DUMP 0 F' examples/cpm/cpm22-buffered.toml
```

`-x` runs one monitor command against the machine and exits. It is **repeatable**, and the
commands run in the order you gave them:

```
$ altairsim -x 'MOUNT dsk0:drive0 mine.dsk' -x 'RUN FF00' -i examples/cpm/cpm22-buffered.toml
```

`-i` is the difference between a query and a start-up: without it the program exits when the
commands are done; with it you are dropped into the monitor with the machine exactly as your
commands left it. `-i` alone, with no `-x` or `-s`, does nothing.

`-s` runs a **script** — a file of monitor commands, one per line, the same ones you type:

```
$ altairsim -s boot.cmd examples/cpm/cpm22-buffered.toml
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
things a `[[board]]` entry can mean. The **boards** chapter is what the boards *are*.
