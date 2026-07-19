# Running it

`altairsim` is a single executable. Unzip the package and run it from a terminal.

```
$ ./altairsim
altairsim 0.1.0 -- 8080, full speed.
machine: default.  HELP for commands.
altairsim>
```

That prompt is **the monitor**. The machine exists — it has memory, a processor, a console
board and a floppy controller in it — but it is not running. Nothing has been started. This
is the equivalent of standing in front of the real Altair with the power on and your hands
on the switches.

You are not obliged to keep it in the current directory. Put it on your `PATH` and
`altairsim` works from anywhere.

## macOS: the first run

macOS marks anything that arrives from the internet and refuses to run it until you say
otherwise. If you see *"cannot be opened because the developer cannot be verified"*, clear
the mark:

```
$ xattr -dr com.apple.quarantine ./altairsim
```

Do that once. It is not a comment on the program; it is what macOS does to every unsigned
binary that arrives in a zip, whoever wrote it.

The mark is put there by whatever *fetched* the file — a browser, a mail client — so if you
pulled the zip down with `curl` or `scp` there may be nothing to clear. The command says
nothing and succeeds either way, which is why it is `-dr` and not `-d`: plain `-d` reports an
error when the flag is already absent, and that error is not a problem.

## Getting help, and getting out

| Type | To |
|---|---|
| `HELP` | list every command |
| `HELP DUMP` | the usage and worked examples for one command |
| `QUIT` | leave |

**There is no `EXIT`.** `QUIT` is the word, and `Q` is enough of it.

Commands resolve by **prefix**, so you type as much as it takes to be unambiguous and no
more — `HELP` shows each command with its shortest form in brackets: `D[UMP]`, `DE[POSIT]`,
`RES[ET]`. Type the part before the bracket. And **commands are not case-sensitive**, nor
are the names of the boards in the machine; this manual writes them in capitals only because
it is easier to read.

## Which machine you get

Running `altairsim` with no arguments gives you a machine called `default` — a 56K Altair
with a console and a floppy controller, which is the machine most period software expects.

Naming something gets you something else:

```
$ altairsim {{MACHINE_CPM}}     a machine file: this one boots CP/M
$ altairsim basic4k                     a BUILT-IN machine, by name
$ altairsim --list                      what the built-in names are
```

A **built-in** is a machine file that lives inside the program. There is nothing special
about it — it is written in the same format as the ones under `examples/`. To see what
is actually in one, boot it and look:

```
$ altairsim -x BOARDS basic4k
```

…and if you want it as a file you can edit, `CONFIG SAVE mine.toml` writes out the machine
you are actually running, and what it writes will boot.

The full story — every command-line option, and how `altairsim` decides whether a word is a
built-in name or a filename — is in the machines chapter.
