# What is in the package

```
altairsim                the program. One file, nothing to install.
QUICK-START.pdf          boot CP/M with one command. Start here.
altairsim-manual.pdf     this manual.
altairsim-changelog.pdf  what changed in this release, and in the releases before it.
altairsim-cheatsheet.pdf every command and option, formatted for reading.
altairsim-monitor.pdf    the altairsim> prompt: how to control the machine from the console.
altairsim-debugger.pdf   breakpoints, stepping, and how to look at the bus.
migrating.pdf            for users of AltairZ80 (SIMH) or z80pack.
DRIVING-WITH-AI.md       for an AI assistant that controls the machine. See below.
cheatsheet.md            the same reference as plain text, for an AI assistant to read.
LICENSE                  the MIT license of altairsim.
LICENSE-SDL3             the license of SDL3, which is built into the program.
LICENSE-MAME-HD63484     the notice for the source of the HD63484 model's structure (MAME).
examples/                machines that boot, with their media.
hostbridge/              the file-transfer utilities: source, HEX and COM.
tools/                   scripts that show a mirror in a second window.
skills/                  the AI briefing again, and the rules for guest programs, as Agent Skills.
```

That is the whole package. You do not install a library or a runtime. You do not have to write
a machine file before the program starts.

**Start with `QUICK-START.pdf`.** It boots CP/M with one command. It shows you the `Ctrl-E` key
and the `RUN` and `QUIT` commands, which move you between the running machine and the monitor.
That one page is enough to get a machine running. This manual gives the full detail.

If you used another Altair simulator before, read `migrating.pdf`. It tells you what carries
over from **AltairZ80 (SIMH)** or **z80pack**, what has a different name here, and what you
cannot do here.

`altairsim-changelog.pdf` is the release history. It tells you what each version added.

`altairsim-monitor.pdf` and `altairsim-debugger.pdf` are two more documents that ship beside
this manual. This manual describes the simulated hardware. *The Monitor* describes the
`altairsim>` prompt, where you start and stop the machine. *The Debugger* describes how to find
a fault from that prompt. Each document tells you at the top where to start.

`altairsim` is one program that needs no other files. It uses one outside library, **SDL3**,
to open the window for the video boards. SDL3 is built into the program, so its license is in
the package as `LICENSE-SDL3`. `LICENSE-MAME-HD63484` is in the package for a similar reason. The
model of the HD63484 video controller uses the structure of the MAME model, and the MAME license
tells you to keep its notice with that code.

The **Developer Guide** is not in the package. It is with the source, at the address in
[What is not in the package: the source](#what-is-not-in-the-package-the-source). You need it
only if you want to build a board of your own.

### `DRIVING-WITH-AI.md`

This document is for an **AI assistant**, not for you. It tells the assistant how to control the
machine through the MCP interface of the program. To use it:

1. Make a working folder. If your assistant reads Agent Skills, install the skills as this
   section shows below. If it does not, put `DRIVING-WITH-AI.md` and the `skills` folder in the
   working folder. The document names files in `skills/`, which have the rules for programs such
   as `ASM` and MBASIC.
2. Start an assistant in that folder.
3. Ask for what you want, for example: *"Using altairsim, boot CP/M and show me what is on the
   disk."*

If you do not use an AI assistant, you can ignore this document. Nothing else needs it.

`skills/` contains **Agent Skills**. Each folder in it is one skill. `skills/altairsim/` is the
same briefing as `DRIVING-WITH-AI.md`. Each other folder, such as `skills/altairsim-mbasic/`,
has the rules for one program that runs in the machine. Agent Skills come from Anthropic, for
its Claude assistants. Claude finds a skill from its description, so when you ask for a machine,
Claude reads the briefing itself. You do not have to tell it which file to open. To install the
skills for Claude Code, copy every folder in `skills/` to one of these places:

- `.claude/skills/` in your project folder, for that project only
- `~/.claude/skills/` in your home folder, for every project

The folders need no other files, so they work in either place. Some other assistants can also
read skills. Their documentation tells you where to put the folder. If your assistant does not
read skills, give it `DRIVING-WITH-AI.md` and the `skills` folder as the steps above show.

The quick reference lists every option, every monitor command, every board and every machine.
The program generated it, so it matches the program that you have. It ships in two forms with
the same content. `altairsim-cheatsheet.pdf` is for you. `cheatsheet.md` is plain text, for an
AI assistant to read.

## The machines are in the program

You do not need any files to run a machine. The machine descriptions are built into the program,
and when you give a name, that machine boots:

```
$ altairsim --list                what the built-in names are
$ altairsim altmon                a monitor in ROM, on a terminal
$ altairsim sol20                 a Processor Technology Sol-20, running SOLOS
```

A built-in machine is an ordinary machine file that is stored inside the program. It uses the
same TOML format that you would write yourself. `CONFIG SAVE mine.toml` saves any running
machine to a file that you can edit.

**Some built-in machines have their software in ROM and need nothing else.** Examples are
`altmon`, `amon`, `sol20`, `vdm1`, `rombasic`, and the SD Systems `sbc200` and `sbc200v`. The
other machines have at most a boot PROM, and they start with empty drives. They need media,
which the next section is about. The machines chapter gives the full detail.

## The examples, media included

`examples/` contains complete machines. **Each example is a folder with its media in it.** Every
example boots as soon as you unzip the package. You do not have to get or mount anything.

**Every folder has its own README**, in Markdown and as a PDF. The README describes the
example: what the machine is, what is in the drive or the tape reader, what to type, and what
the machine should print. This manual does not list the examples. Look in `examples/`, and read
the README of the example that you want.

```
$ ls examples/
$ altairsim examples/cpm/cpm22-buffered.toml
```

**You can move an example folder anywhere.** It still boots after you copy it, rename it or send
it to another person. The machines chapter tells you how the paths in a machine file work.

The examples chapter describes some of the examples in detail. More examples, with their
documentation, are at https://altairsim.com.

## The file-transfer utilities

`hostbridge/` contains `R`, `W` and `HDIR`. The file-transfer chapter uses these programs to
move files between CP/M and your computer. The folder has the 8080 **source**, and the assembled
`.HEX` and `.COM` files.

You do not need this folder to use the utilities on the CP/M disk in the package. That disk
already has the `.COM` files. The folder is for a disk that does not have the utilities. The
file-transfer chapter tells you how to put them on that disk, by pasting `R.HEX` through the
console.

## The watch scripts

`tools/` contains two scripts that show the console of the machine in a second window, while
an AI assistant controls the machine: `tools/mirror-watch.sh` for macOS and Linux, and
`tools/mirror-watch.ps1` for Windows. Each script waits for the mirror, shows its output, and
connects again after the simulator starts again. `tools/README.pdf` gives the commands. The
chapter *The MCP server* tells you how to start a mirror.

## What is *not* in the package: everything else to run

**The media in `examples/` is all the media in the package.** The other built-in machines that
need a disk or a tape, such as `basic8k`, `ps2` and `minidisk`, start with an empty drive:

```
$ altairsim basic4k -x "SHOW MOUNTS"
altairsim> SHOW MOUNTS
  UNIT       KIND  HOLDS
  acr0:tape  tape  (empty)

  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.
```

You supply the media, and you `MOUNT` it. The disks chapter and the tapes chapter tell you how.
When those chapters name an image that is not in `examples/`, the name shows the form of the
command. It is not a file that you have.

Most disk and tape images are not ours to give away, so they are not in the package. More
examples, with their media and their documentation, are at https://altairsim.com.

## What is *not* in the package: the source

The **source code** of the simulator is not in the package. `altairsim` is an open project under
the MIT license. You can get the source here:

**<https://github.com/deltecent/altairsim>**

The only source in the package is in `hostbridge/`. That is the 8080 source of the file-transfer
utilities, not the source of the simulator.

You do not need the source to use anything in this manual. If you want to **build a board of
your own**, you need the source and the *Developer Guide*. This manual tells you how to use the
machine. It does not tell you how to add to the program.

## Reporting a bug, or asking for something

Report a bug or ask for a feature in the **Issues** tab of the repository:

**<https://github.com/deltecent/altairsim/issues>**

Search the issues first. If nobody has reported your problem, open a new issue. You need a
GitHub account.

Ask a question or talk about `altairsim` in the **Discussions** tab of the repository:

**<https://github.com/deltecent/altairsim/discussions>**

Use **Q&A** when you need help to do something, for example to boot a disk or set up a board.
Use **Show and tell** to show a machine or a program that you made. An issue is for a bug or a
feature request. If you open an issue that is only a question, we move it to Discussions.

**A useful bug report lets another person see what you saw.** Include:

- The **version**. This is the line that `altairsim` prints when it starts, or that
  `altairsim --version` prints. Paste the whole line. Between releases, the part after the
  version number tells which source built your copy. Also give your operating system.
  `SHOW VERSION` prints the same information from the monitor. It also has a `video` row that
  tells whether your copy can open a window. Include it in a report about a video board. The
  `tree` row tells whether the source had changes that were not committed when your copy was
  built.
- The **machine**. Give the name of the built-in machine, or paste the machine file. A machine
  file is a small text file.
- **What you typed and what happened.** Paste the terminal output, with the prompts. The monitor
  shows every command, so the pasted output is a full record.
- What you expected, when that is not clear.

If the guest software failed and the simulator did not, tell us which software it is and where
you got it. Some period programs failed on real hardware too. With the image, we can find out
which kind of failure it is.

**A feature request is also an issue.** Tell us what you are trying to do, not only the setting
that you want. The machine often has a way to do it already. If it does not, your problem tells
us what the answer should be. If you ask for an S-100 board that is not here, name the manual
that documents the board. We build each period board from its manual.
