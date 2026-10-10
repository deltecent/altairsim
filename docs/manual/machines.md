# Machines

A **machine** is a backplane with boards in it. The boards, their settings and their state are
the whole machine. That is all that `altairsim` needs to know.

You choose the machine in one of three ways. You give the name of a **built-in** machine, you
give a **file**, or you give **nothing** and get the default. This chapter tells you how the
program makes that choice, and what a path means.

## The command line

```
altairsim [machine] [options]
```

Give the machine first, and then the options for that machine. The program also accepts the
options before the machine: `altairsim mine.toml -s check.cmd` and
`altairsim -s check.cmd mine.toml` do the same thing.

| Option | |
|---|---|
| `machine` | a built-in name, **or** a machine file if it contains a `/` or ends in `.toml` |
| `-m, --machine <name>` | **always** a built-in name, never a file |
| `-f, --file <path>` | **always** a file, never a built-in name |
| `-n, --none` | an empty backplane. No boards, no memory, nothing |
| `-l, --list` | list the built-in machines and exit |
| `-s, --script <file>` | run a command script, then exit with its status. Paths in it are relative to the script's folder |
| `-x, --exec <cmd>` | run one monitor command, then exit. You can give it more than one time |
| `-i, --interactive` | after `--script` or `--exec`, stay in the monitor |
| `--mcp` | run as an MCP server on stdio |
| `--mirror <sock>` | with `--mcp`, copy the console to a socket so that a person can watch |
| `-v, --version` | print the version and exit |
| `-h, --help` | print this help and exit |

**Give one machine only.** If you give a name *and* `-m`, or `-f` *and* `-n`, the program
prints *give ONE machine* and stops. It does not choose one for you.

## How a bare word resolves

```
$ altairsim basic4k                     a BUILT-IN, by name
$ altairsim examples/cpm/cpm22-buffered.toml             a FILE, by path
```

The program decides from the word only. **It never looks for a file on your disk:**

| The word | What it is |
|---|---|
| contains `/` or `\` | a **file** |
| ends in `.toml` | a **file** |
| anything else | a **built-in name** |

Because of this rule, **`altairsim basic4k` means the same machine in every folder.** A file
called `basic4k` in your folder does not change it. A command in a script or in a README
always means the same thing.

For a file, you must give the `.toml` extension, as in `altairsim mymachine.toml`, or a `/`,
as in `altairsim ./mymachine`. To say which one you mean, use `-m` or `-f`:

```
$ altairsim -m basic4k                  a built-in. Stop looking for a file
$ altairsim -f ./basic4k                a file called `basic4k`, no extension, right here
```

## The one file the simulator *finds*

If you give **nothing**, and the working folder has a file called `altairsim.toml`, the program
loads that machine. It tells you that it did:

```
$ altairsim
altairsim: no machine named -- using ./altairsim.toml (`-m default` for the built-in).
AltairSim X.Y.Z -- 8080, full speed.
machine: bench.  HELP for commands.
altairsim>
```

The first line tells you which file the program loaded. The program prints it before anything
else. The `machine:` line gives the name that is written *in* the file, `bench` here.

This is the **only** file that the program finds by itself. It does this only when the command
line gives no machine. If you give a built-in name, a file or `-n`, the program ignores
`./altairsim.toml`.

Put an `altairsim.toml` in a project folder, and `altairsim` with no arguments loads your
machine. With no `altairsim.toml` and no arguments, you get the built-in `default` machine.

## The built-in machines

A **built-in machine is a TOML machine file that is stored in the program.** It uses the same
format, keys and rules as a machine file that you write. It is in the program so that it is
always there.

```
$ altairsim --list
```

This command lists the built-in machines, one on each line, with a sentence about each. The
list comes from the program, so it always has every machine. The machine reference at the back
of this manual has the same table. At the `altairsim>` prompt, `SHOW MACHINES` gives the list.

To see what is in a built-in machine, its boards and its startup commands, give its name:

```
$ altairsim basic4k -x 'SHOW MACHINE'
```

At the prompt, type `SHOW MACHINE basic4k`. `SHOW MACHINE` with no name shows the machine that
you are running now.

To get the machine file as **text that you can edit**, with every board and every setting:

```
$ altairsim basic4k -x 'CONFIG SAVE mine.toml'
$ altairsim mine.toml
```

`CONFIG SAVE` saves the machine that you are running, and the file loads back as the same
machine. **You can use each built-in machine as an example.** Find the one that is nearest to
what you want, save it, and edit the file.

You can also start *from* a built-in machine with `base`, and write only what is different.
The configuring chapter tells you how.

## How the machine is wired: `SHOW BUS`

`SHOW MACHINE` tells you which boards are in the machine. `SHOW BUS` tells you how they are
wired together. It shows what answers at each memory address, what answers at each port, and
what happens on an interrupt. Use it first on a machine that you built. For example, type
`altairsim basic4k -x 'SHOW BUS'`, or type `SHOW BUS` at the prompt:

```
altairsim> SHOW BUS
MEMORY
  0000-0FFF  mem0     ram
  unmapped: 1000-FFFF  (floats to FF)
I/O
  FF         fp0      IN  --     SENSE switches SA8..SA15 -> D0..D7
  00         sio0     IN  OUT    COM2502 -- status / interrupt enables
  01         sio0     IN  OUT    COM2502 -- data
  06         acr0     IN  OUT    88-SIO B UART -- status / interrupt enables
  07         acr0     IN  OUT    88-SIO B UART -- data, via the modem, to the cassette
INTERRUPTS
  CPU     INTE off         interrupts are DISABLED; nothing will be acknowledged
  pINT    idle             pin 73
  88-VI   (none)           nothing watches VI0-VI7; an acknowledged interrupt
                           floats to FF, which the 8080 executes as RST 7
```

The output has three sections:

- **`MEMORY`** lists each range and the board that answers it. A range with no board is
  `unmapped`. This is not an error: a read from it returns `FF`, the same as an empty socket on
  a real backplane. A boot PROM that the program has switched out is not in the list.
- **`I/O`** lists each port and the board that answers it. The `IN` and `OUT` columns show which
  direction the board answers. `--` means that the board does not answer that direction. A port
  that is not in the list returns `FF` on an `IN`.
- **`INTERRUPTS`** is the short form of `SHOW BUS IRQ`. It shows whether the processor accepts
  interrupts, whether the interrupt pin is asserted, and whether an 88-VI board is there to
  read lines VI0 to VI7. It lists each line that has a board set to it. A board that is set to a
  line that nothing reads does not give an error. The machine runs, and it never gets the
  interrupt. A `WARNINGS` part shows you this fault.

To see only one section, or more detail, add a view: `SHOW BUS MAP`, `SHOW BUS IO` or
`SHOW BUS IRQ`. `SHOW BUS CONTENTION` shows two boards that answer the same address or port. The
boards chapter has an example. The Monitor document has the full list of views.

## The empty backplane: `-n`

```
$ altairsim -n
```

`-n` gives a machine with no boards, no memory and no processor. You add every board yourself
with `BOARDS ADD`. Use it when you build a machine one board at a time. It is also the only
way to be sure that the machine has nothing in it that you did not add.

### From an empty backplane to a machine file

These steps build a machine at the prompt, save it to a file, and load it again:

```
$ altairsim -n
altairsim> BOARDS ADD 8080 cpu0
cpu0: 8080 added
altairsim> BOARDS ADD 2sio sio0 port=10
sio0: 2sio added
altairsim> SET sio0:a connect=console
sio0:a: connect=console
altairsim> BOARDS ADD memory mem0 fill=random
mem0: memory added
altairsim> REGION ADD mem0 type=ram at=0 size=56K
mem0:0: ram  0000-DFFF  56K
altairsim> REGION ADD mem0 type=rom at=FF00 mount=builtin:dbl
mem0:1: rom  FF00-FFFF  builtin:dbl
altairsim> CONFIG SAVE mine.toml
saved mine.toml
altairsim> QUIT
```

To get the machine back, start with an empty backplane again and load the file:

```
$ altairsim -n
altairsim> CONFIG LOAD mine.toml
loaded mine.toml: 3 board(s)
```

`$ altairsim mine.toml` does the same thing in one step.

Three things in these steps are important:

- **You can give a board's settings on the `BOARDS ADD` line** (`port=10`), or set them later
  with `SET`. The result is the same.
- **`SET` sets one property in each command**, and the board must exist first.
- **A memory board has no memory until you add regions to it.** Use `REGION ADD`. Do not forget
  the ROM region, because it holds the code that `RUN` starts.

Two more commands are often part of a machine. `CONFIG SAVE` saves both of them:

- **`STARTUP ADD RUN FF00`** records the command that starts the machine. Use `MOUNT` to put a
  disk in a drive.
- **`SET MACHINE name=<name>`** gives the machine a name. A machine that you build from `-n` is
  called `none` until you give it a name. `CONFIG SAVE` saves the name. The name is a label:
  the `base =` key of another file finds this machine by the path of its file, not by its name.

## The path rule: one base directory

This rule lets you copy an example folder to any place, and the machine still boots:

> **A relative path resolves against the machine's directory**, which is the folder that the
> machine file was loaded from.

That folder is the base for the disks and PROMs that the machine file mounts. It is also the
base for the `MOUNT`, `LOAD`, `SAVE` and `DO` paths that you type. The rule is the same for a
path in the file and a path that you type.

A **script** follows the same idea. A path in a script is relative to the script's folder, as a
path in a machine file is relative to the machine file. This is true for a script that you run
with `DO` and for a script that you run with `-s`.

`examples/cpm/cpm22-buffered.toml` has `mount = "cpm22b23-56k.dsk"`. This means *the disk in
this folder*. It still means that after you copy the folder, rename it or send it to another
person. For this reason, each example is a folder that has all its files, and the `cp -R` in
the quick start works. If you then type this:

```
altairsim> MOUNT dsk0:drive1 cpm22b23-56k.dsk
```

you get the **same file**, from the **same folder**, the folder of the machine file. The folder
that you started `altairsim` from does not change this.

A **built-in** machine has no folder of its own. Its base is the folder that you started the
program from.

### When the rule causes a problem

You see the rule only when a file is missing. When that happens, the error can look like a
typing mistake. For example, you keep your machine files in a `machines/` folder. You write a
path that starts from the folder that you start the program from:

```toml
[[board.drive]]
unit  = 0
mount = "disks/Kermit/cpm.dsk"      # meant: the disks/ up beside machines/
```

`altairsim -f ./machines/8800c.toml` then prints the error, with the line of the
`[[board.drive]]` in your file:

```
./machines/8800c.toml: line 12: dsk0: 'machines/disks/Kermit/cpm.dsk': no such file
  ('disks/Kermit/cpm.dsk' is relative to the machine's directory, ./machines/)
```

**The disk is not missing.** The program looked for it in the folder of the machine file,
because that is the machine's directory. Write the path from that folder:

```toml
mount = "../disks/Kermit/cpm.dsk"   # up out of machines/, then down into disks/
```

You can also keep the machine file in the same folder as its disks, as every example in the
package does. The same `../` applies to a path that you type, because both start from the same
base.

### Ask the machine

`SHOW PATHS` prints the base for the machine that you are running:

```
altairsim> SHOW PATHS
  base directory     /home/you/altair/disks/cpm22
                     What a machine file mounts, and the MOUNT / LOAD / SAVE /
                     DO you type, resolve against this. A path inside a DO or
                     -s file is relative to that file.
                     It is the directory the machine was loaded from.

  hb0 sandbox        /home/you/altair/disks/cpm22/xfer
                     THE GUEST'S SANDBOX, and the only real fence here:
                     R.COM/W.COM cannot leave it. It is not a base for
                     anything you type. Set with `hostdir`.
```

The two entries do different jobs. The base directory is where relative paths start. The
sandbox is the only folder that the guest can read and write.

For a **built-in** machine, the base is the folder that you started the program from:

```
  base directory     /home/you/altair
                     ...
                     This machine is built in, so it is the directory you
                     launched from.
```

`SHOW MOUNTS` lists every disk, tape and ROM in the machine, for all the boards:

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

**Empty drives are in the list.** The 88-DCDD has four drives. This machine has a disk in one of
them.

`SHOW MOUNTS` shows each path as it was written. `SHOW PATHS` tells you which folder those paths
are relative to. Use the two commands together.

### The path rule is not a sandbox

The path rule tells the program **where a path points**. It does not limit anything. A machine
file can mount any file on your disk, with `..` or with an absolute path.

The only limit is the **`hostdir`** of the Host Bridge. It limits the files that a CP/M program
*in* the machine can read and write on your computer. This is a different mechanism for a
different purpose. The chapter *Moving files in and out* describes it. Nothing in a machine
file's paths changes it.

## Running a command and leaving: `-x` and `-s`

You can use `altairsim` without typing at the prompt.

```
$ altairsim default -x 'SHOW MACHINE'
$ altairsim examples/cpm/cpm22-buffered.toml -x 'DUMP 0 F'
```

`-x` runs one monitor command on the machine, and then the program exits. You can give `-x` more
than one time. The commands run in the order that you give them:

```
$ altairsim examples/cpm/cpm22-buffered.toml -x 'MOUNT dsk0:drive0 mine.dsk' -x 'RUN FF00' -i
```

Without `-i`, the program exits when the commands are done. With `-i`, you get the monitor, and
the machine is as your commands left it. `-i` with no `-x` or `-s` has no effect.

`-s` runs a **script**. A script is a file of monitor commands, one on each line. These are the
same commands that you type at the prompt. Most example folders have a script, with the
extension `.ini`. For example, `cpm22-buffered.ini` builds the same CP/M machine as
`cpm22-buffered.toml`, and boots it:

```
$ cd examples/cpm
$ altairsim -s cpm22-buffered.ini
```

At the `altairsim>` prompt, `DO cpm22-buffered.ini` runs the same script.

**A path in a script is relative to the script's folder.** The script above mounts
`cpm22b23-56k.dsk`, the disk beside it. So the script also works from another folder. Give the
path to the script from the folder that you are in:

```
$ altairsim -s examples/cpm/cpm22-buffered.ini
```

**The exit status is not zero if a command failed.** You can use `altairsim -s` in a shell
script, a Makefile or a build, and test the result:

```sh
if altairsim mine.toml -s check.cmd; then
    echo "machine is sane"
fi
```

This command has one script and one machine. The program loads the machine `mine.toml`, and
runs its startup commands. It then runs each line of `check.cmd` on that machine. A script
takes no arguments.

## A script that runs at every start: `~/altairsim.ini`

If there is a file called `altairsim.ini` in your home folder, `altairsim` runs it each time
that you start the monitor. It is a script, the same kind as `-s` runs: monitor commands, one on
each line. Use it for settings that belong to you and not to a machine, for example where the
debug output goes. This line writes it to a file in your home folder:

```
# ~/altairsim.ini
SET CONSOLE DEBUG=altairsim-debug.log
```

The file runs after the machine is loaded and before the machine's own `startup` commands, so a
machine that starts running at once still gets your settings. The program shows each line
behind `ini>` as it runs it.

- **No file is not an error.** Without the file, nothing happens.
- **A command that fails does not stop the file.** The program shows the error, runs the next
  line, and the exit status is not changed.
- **A path in the file is relative to your home folder.**
- **`--mcp`, `-x` and `-s` do not run it.** Their output is read by a program, and a file in
  your home folder must not change it. To use the same lines in a script, name the file:
  `altairsim -s ~/altairsim.ini`.

## Which chapter next

The **configuring** chapter describes the machine file: every table, every key, and the four
forms of a `[[board]]` entry. The **boards** chapter describes each board.
