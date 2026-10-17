# Running it

`altairsim` is one program file. Unzip the package, and run the program from a terminal.

```
$ ./altairsim
AltairSim X.Y.Z -- 8080, full speed.
machine: default.  HELP for commands.
altairsim>
```

On **Windows**, the program is `altairsim.exe`:

```
> altairsim.exe
```

You can run the program from any folder. Put its folder on your `PATH`, and type `altairsim`.

The `altairsim>` prompt is **the monitor**. The machine has power, but it is stopped. No program
runs until you start one.

## macOS: the first run

macOS marks a file that comes from the internet. It does not run a marked program that is not
signed. The first time that you start the program, macOS shows one of these messages:

- macOS 27: a dialog with the title *"altairsim" Not Opened* and the text *Apple could
  not verify "altairsim" is free of malware that may harm your Mac or compromise your privacy.*
- Earlier versions: *"altairsim" cannot be opened because the developer cannot be verified.*

**Press Done. Do not press Move to Trash.** Move to Trash is the default button, so the Return
key deletes the program. After you press Done, remove the mark:

```
$ xattr -dr com.apple.quarantine ./altairsim
```

Start the program again. macOS does not show the message again for this copy.

Do this one time for each download. Each new archive has a new mark, so you do it again for
each release. You can also allow the program in System Settings, in Privacy & Security, with
**Allow Anyway**. That way needs an administrator password.

A browser or a mail client adds the mark. If you got the archive with `curl` or `scp`, the file
may have no mark. In that case, the command does nothing, and that is correct.

## Getting help, and getting out

| Type | To |
|---|---|
| `HELP` | list every command |
| `HELP DUMP` | show the usage and examples for one command |
| `QUIT` | leave the program |

**There is no `EXIT` command.** Type `QUIT`, or only `Q`.

You can type a short form of a command. `HELP` shows each command with brackets, for example
`D[UMP]`, `DE[POSIT]` and `RES[ET]`. The part before the bracket is the short form. You can type
the short form, the full name, or any length between them.

**Commands are not case-sensitive.** Board names, board types, settings and their values are
not case-sensitive either. The same is true for endpoints such as `socket:2323` and for
command-line options such as `--mcp`. A file name, a host name and a serial device keep their
case. This manual writes commands in capitals because they are easier to read.

## Which machine you get

When you run `altairsim` with no arguments, you get the machine called `default`. It is a 56K
Altair with a console, a floppy disk controller and a boot PROM. If the folder that you are in
has a file called `altairsim.toml`, you get that machine instead.

To get a different machine, give its name or its file:

```
$ altairsim examples/cpm/cpm22-buffered.toml     a machine file: this one boots CP/M
$ altairsim basic4k                     a BUILT-IN machine, by name
$ altairsim --list                      what the built-in names are
```

A **built-in** machine is a machine file that is stored in the program. It uses the same format
as the machine files in `examples/`. To see what is in a built-in machine, load it and look:

```
$ altairsim basic4k -x BOARDS
```

To get a built-in machine as a file that you can edit, type `CONFIG SAVE mine.toml`. This
command writes the machine that you are running to a file, and that file boots.

The machines chapter describes every command-line option. It also tells you how `altairsim`
decides if a word is the name of a built-in machine or the name of a file.
