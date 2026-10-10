# The MCP server

```
$ altairsim <machine> --mcp
```

This command runs `altairsim` as an **MCP (Model Context Protocol) server** on stdin and stdout.
An AI assistant, such as Claude or any other program that uses MCP, can then control the machine
with **typed tools**. It can boot the machine, type at its console, read what it prints, set
breakpoints and look at memory. It does not have to read text from a terminal screen.

The MCP server uses the same machine as the monitor. What the tools see is what `SHOW` sees.

You do not use the tools yourself. You register the server with your assistant one time, and
then you tell the assistant what you want. This chapter tells you how to set it up, how to watch
the assistant work, and how to run a project with it.

## Set up your assistant

An MCP client needs two things: a command to run, and the fact that the server uses stdio. The
command is `altairsim <machine> --mcp`. `<machine>` is a built-in name or a machine file, as on
the command line.

**Claude Code** (the command line) takes it as one command. Everything after `--` is what it
runs. Run it from the folder that you want the machine's files to resolve against:

```
claude mcp add altairsim -- altairsim <machine> --mcp
claude mcp list                       # confirm it registered and is reachable
```

**Claude Desktop, or any client that reads a JSON configuration**, needs an `mcpServers` entry
with the command and its arguments. A `cwd` sets the working folder, because a desktop
application has no shell to get one from:

```json
{
  "mcpServers": {
    "altairsim": { "command": "altairsim", "args": ["<machine>", "--mcp"] }
  }
}
```

If `altairsim` is not on your `PATH`, give its full path as the command.

You can register more than one machine. Register each one under a different name, such as
`altair-cpm` and `altair-basic`, and an assistant sees all of them. The *scope* of
`claude mcp add` decides where a server is available:

- the default: only in the project folder that you added it from
- `--scope project`: in a `.mcp.json` file that moves with the folder
- `--scope user`: everywhere

**The working folder is also the file-transfer folder.** The host-bridge sandbox is the server's
working folder. The `claude` command line uses the folder that you started from, so a relative
machine path and the sandbox both point at your folder. The desktop application currently starts
the server in your home folder, so there, give the machine a full path.

## A first session

The `examples/ai-mcp/` folder is ready for a first session. It has a CP/M machine and a small
program, `HELLO.ASM`, with one bug in it. Its README has the steps. In short:

1. Go to the folder, and register the machine from there:

   ```
   $ cd examples/ai-mcp
   $ claude mcp add altairsim -- altairsim cpm-ai.toml --mcp
   ```

2. Start your assistant in the same folder, and give it the job in one sentence:

   > *Using the altairsim MCP tools, boot the CP/M machine and assemble and run HELLO.ASM off
   its > disk. It should print HELLO, WORLD. If it does not, debug it in the simulator and fix
   the > source.*

The assistant boots the machine, assembles and runs the program, sees the wrong output, steps
through the loop to find the fault, corrects the source and assembles it again. It does all of
this through the MCP tools. Say "using the altairsim MCP tools" and "off its disk", so that the
assistant works on the disk in the machine, and not on a copy of the file on your computer.

## Watch it work: `--mirror`

Add `--mirror socket:PORT` with `--mcp`, and you can type `nc localhost PORT` (or `telnet
localhost PORT`; a new Mac has `nc` but no `telnet`) in a second window to **watch the
session that the assistant controls**, with every character that the guest prints. You can also
**type on the line to take over**, and share the console:

```
$ altairsim examples/cpm/cpm22-buffered.toml --mcp --mirror socket:2323
```

This is the same mirror that the monitor has (`<endpoint>|socket:PORT`, see the chapter *Serial
ports, sockets and telnet*). The assistant works as before, and the mirror changes nothing for
it. What it types, and what the guest prints, also go over the socket to you. Add `?ro` to make
the mirror watch-only. **Put quotes around it on the command line**, because `?` is a shell
wildcard:

```
$ altairsim examples/cpm/cpm22-buffered.toml --mcp --mirror 'socket:2323?ro'
```

### A watcher that connects again

`nc` and `telnet` stop each time the assistant starts the simulator again, and Windows installs
neither. The package has two scripts in `tools/` that do not have these problems:

- `tools/mirror-watch.sh`, for macOS and Linux. It needs only bash.
- `tools/mirror-watch.ps1`, for Windows. It needs only PowerShell.

Run one script in a second window. It waits for the mirror, shows its output, and connects
again after the simulator starts again. With no argument it watches `localhost:2323`. Give a
port, or a host and a port, to watch a different mirror. `tools/README.pdf` gives the commands,
and tells you how to let Windows run the script.

The scripts only watch. They do not send the keys that you type. One watcher can connect at a
time. The guest prints only while it runs. Until then, the script shows `Connected` and nothing
more.

### A mirror for a terminal program: `--mirror pty`

On macOS and Linux, the mirror can be a **pseudo-terminal**. A pseudo-terminal looks like a
serial port to a terminal program, such as `screen`, `minicom` or `cu`. A terminal program
sends each key when you press it, and does not print the key a second time. There is no port
number to select.

```
$ altairsim examples/cpm/cpm22-buffered.toml --mcp --mirror pty
altairsim: --mirror: open /tmp/altairsim0 (/dev/ttys004)
```

1. Read the name that the program prints. The first mirror is `/tmp/altairsim0`, and the next
   one is `/tmp/altairsim1`.
2. Open that name in a second window:

   ```
   $ screen /tmp/altairsim0
   ```

3. To leave `screen`, press `Ctrl-A`, then `K`, then `Y`.

The name in `/tmp` is a link to the device in the brackets. The device has a different name at
each start, and the link keeps the same one. The program removes the link when it stops.

To select the name, give a path: `--mirror pty:/tmp/console`. Add `?ro` to make the mirror
watch-only, with quotes as for a socket: `--mirror 'pty?ro'`.

When no program has the name open, the mirror discards what the guest prints. You see the
output from the time that you open it. Windows has no pseudo-terminal, and `--mirror pty` is
refused there. Use `--mirror socket:PORT`.

Without the quotes, the shell tries to find a file called `socket:2323?ro`, and fails before
`altairsim` sees it. (zsh reports `no matches found`.) One watcher can connect at a time.

After a `run`, the guest stops until the assistant runs it again. A character that you type
while the guest is stopped waits on the line. To take over the console, ask the assistant to
**`start`** the guest. After `start`, the guest runs all the time, as it does after `RUN` at the
monitor, and it answers what you type at once. The assistant can still use its other tools. The
guest runs until the assistant uses `stop`.

### A mirror for a hardware terminal: `--mirror serial:DEVICE`

The mirror can be a **serial port** on your computer. A terminal on that port, or a second
computer with a terminal program, watches the session and can type on it. This works on every
platform.

```
$ altairsim examples/cpm/cpm22-buffered.toml --mcp --mirror serial:/dev/cu.usbserial-AL009KFH
altairsim: --mirror: open /dev/cu.usbserial-AL009KFH at 9600 baud
```

The port is 8 data bits, no parity and 1 stop bit, with no flow control, at 9600 baud. Add
`?baud=N` for a different rate, and `?ro` to make the mirror watch-only. Quote the option, as
for a socket: `--mirror 'serial:/dev/cu.usbserial-AL009KFH?baud=19200'`. On Windows the device
is a name such as `COM3`.

The program cannot tell when a terminal is on the port. It sends the output at all times, at the
speed of the port. The guest does not wait for it. While the guest is stopped between two `run`s,
the program keeps sending what is still queued, so a long listing finishes on the terminal.

Keys that you type reach the guest only while it runs. Ask the assistant to **`start`** the guest
to take over the console, as for a socket.

## Keep a text log: `--log`

Add `--log FILE` with `--mcp`, and the program writes each character that the guest prints to
`FILE`, at the time that the guest prints it:

```
$ altairsim examples/cpm/cpm22-buffered.toml --mcp --log session.log
```

The file holds the text that a terminal shows, with nothing added. It is an ordinary text file,
and you can use it as you use any other file: show it with `cat` or `type`, open it in an
editor, search it, or copy it. You can do this while the session continues.

One use is to follow the session in a second window while the guest prints:

```
$ tail -f session.log
PS> Get-Content -Wait session.log
```

The first command is for macOS and Linux, and the second is for PowerShell on Windows.

- The log holds the full session, from the first character.
- The log only records. To type at the guest, use `--mirror`. You can give `--mirror` and
  `--log` together.
- The program empties the file each time that `altairsim` starts.
- A file name with no folder is in the folder where you start `altairsim`.
- If the program cannot open the file, it stops with an error before the session starts.

This is the `fmt=text` tap that the monitor has (`<endpoint>|FILE?fmt=text`, see the chapter
*Serial ports, sockets and telnet*).

## Start a project of your own

When you want to write your own software, start in the same way as the example. **Make a folder
of your own, put a machine in it, and point the assistant at that folder, not at the copy in the
package.**

**Put the machine and its images at the top of the folder.** Copy a machine that boots the
system that you want to build on into a new folder, and work there. The whole `examples/ai-mcp/`
folder is a good start, with its disk and machine file. Register the server from inside that
folder, as above, so that the machine file and the host-bridge sandbox both resolve there. The
guest writes to the disk image as it runs, and the assistant leaves build files beside it. If
you keep the copy in the package unchanged, you always have a clean one to start again from.
Also keep a spare copy of the machine and its new disk, so that "start again" gives you a known
state, not what the guest last wrote.

**Give it local information to read.** Put `DRIVING-WITH-AI.md` and the `skills` folder in the
folder. `DRIVING-WITH-AI.md` is the briefing for the assistant itself, with the steps for booting
and debugging over MCP. The files in `skills` have the rules for programs that run in the
machine, such as the assembler and MBASIC.
Keep a second folder, for example `Reference`, for anything else that you want the assistant to
use: the parts of this manual that are important to your project, and your own source material,
converted to plain Markdown. An assistant reads Markdown most reliably. These copies go out of
date when `altairsim` changes, and the assistant cannot tell an old copy from a current one.
Update them when you update `altairsim`.

**Watch what it does, because it uses what it already knows.** An assistant does not read the
files in your folder and then do what they say. It chooses its next step from everything in
front of it, and from everything that it learned in training. What it learned is very large, and
one line in a local file has little weight against it. For this reason, when the job looks like
a common one, such as booting CP/M, assembling a file or copying something to a disk, the
assistant first tries the way that it has seen most often, even when the files in your folder
say something else. For example, when you ask it to get a program onto a disk, it tries a host
tool such as `cpmtools`. That tool does not understand the hard-sectored disks of the Altair.
The way that works is to build inside the machine over the host bridge, as
`skills/altairsim-cpm-build/SKILL.md` says. The longer a session runs, the more the assistant goes back to the common way.

For this reason:

- Watch what it sends and reads back, with `--mirror`. Do not trust its own summary of what it
  did.
- Correct it at the first wrong step, before it builds on that step.
- Point it at the file that you mean, and tell it to use that file and only that file.
- Be most careful when its answer looks the most usual. That is when a habit has most probably
  taken the place of something that is special about your machine.

**Start and stop each session in the same way.** A short instruction that you give every session
saves time. At the end, tell it to clean up: close any processes that still run, write down what
it learned, and save where you are in a note in the folder. At the start, tell it to read that
note and the `Reference` folder, boot the machine, and tell you where you stopped. The note in
the folder lets a session continue. Without it, the assistant must work out the state again each
time, and it gets it wrong.

**Do not rename the project folder in the middle of a project.** Claude Code keeps its memory of
the project under the full path of the folder, outside the folder itself. After a rename, the
folder looks like a new project to it. The registration from the section above is also tied to
that path by default. `claude mcp list` in the renamed folder no longer shows `altairsim`, and
the assistant cannot reach the machine until you run `claude mcp add` again. `--scope project`
prevents this, because the registration is then in a `.mcp.json` file that moves with the
folder.

If it happens, register the server again, tell the assistant the old and the new folder names,
and tell it to start from the note in the folder.

## What the assistant can do

The tools let an assistant operate the whole machine:

- List the available board types, and every property of each type, with its type, its default
  and its legal range.
- List the boards in the machine, get and set any property, and add a board.
- Mount a disk or a tape, and connect a serial line to an endpoint.
- Examine, deposit, fill, search, save and disassemble memory.
- Run the machine, type at its console and read what it prints.
- Start the machine and let it run between tool calls, for example to run a server on the guest,
  or two machines that talk to each other. Then stop it.
- Step it, set breakpoints, and read the bus history and the interrupt lines.
- Save the state of the whole machine in a snapshot, and restore it.

When no tool does what it needs, such as a conditional breakpoint or an octal listing, the
`monitor` tool runs any monitor command and returns its text.

This manual has no tool reference, because the server describes its own tools. `tools/list`
returns every tool, and `board_types` returns every board type. Their answers always match your
copy of the program. A new board is available to an assistant as soon as it is in the program,
because the tools come from the same description of the board as its machine-file keys.

## If you write your own client

Most people use an assistant, and the assistant reads the tools itself. If you write a program
that talks to the server directly, read `DRIVING-WITH-AI.md`. It describes the protocol, the
`run` tool and its stop reasons, how to stop a `run` early, and the `status` tool. These points
are the most important:

- The server uses line-delimited **JSON-RPC 2.0**. Send `initialize`, and then `tools/call`.
- A tool result has text in `content`. A tool that reports values also gives them as JSON fields
  in `structuredContent`, such as `running` and `stop_reason` from `status`. A program reads
  `structuredContent` and does not parse the text.
- A machine named on the command line is loaded, but **its `startup` list does not run**. The
  client boots it with `run`.
- A `run` **never blocks**. It stops when `until` matches, when the guest waits at a prompt, or
  at `timeout_ms`. `timeout_ms` is a limit, not a wait.
- `start` returns at once and **leaves the guest running** until `stop`, a `HLT` or a
  breakpoint. All other tools work while it runs. `status` tells you if it still runs, and if
  not, why it stopped.
- JSON has no hex form, so `from` is a decimal number: `65280` is `FF00`. If you send a string
  such as `"0xFF00"`, the server refuses the call and tells you the number to send.
- A control byte is a JSON `\uXXXX` escape: `\u0003` is `Ctrl-C`. `\x03` is not JSON.
- Some clients send the text as you write it, and do not make a JSON escape from it. With such
  a client, use the `TYPE` command through the `monitor` tool. `TYPE` has its own escapes:
  `\r`, `\"`, `\^C` for `Ctrl-C` and `\xHH` for a byte. `HELP TYPE` gives the full list.
