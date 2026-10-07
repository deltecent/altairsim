# Driving altairsim with an AI

How to let an **AI assistant** drive the **altairsim** MITS Altair 8800 simulator through its
built-in MCP server — so you can say *"using altairsim, do …"* and it does it, instead of you
cutting and pasting between a chat window and a terminal. It can boot a machine, type at its
console, read what it prints, build a CP/M program, single-step and debug one, and talk to a
program over a real serial port — all through typed tools.

This works with **any MCP-capable assistant**. The examples below use **Claude** as the concrete
client, but the server speaks the open Model Context Protocol and the same steps apply elsewhere.

Every recipe below was **verified end to end** against the CP/M machine that ships in this
package (`examples/cpm/cpm22-buffered.toml`). Point at the `altairsim` you were given.

**New to this?** The `examples/ai-mcp/` folder is a ready-made working directory: register the
server there (below) and ask your assistant to build and fix the little program waiting in it —
a complete, guided round trip through everything this document describes.

## Contents

- Starting the server
- Register the server with your assistant
- Several machines, and several projects
- Watching over its shoulder — and taking the keyboard
- The tools
- Knowing the commands
- The pattern: an expect loop
- Rules for a guest program
- From a bare disk image to a booting machine
- Amending a machine instead of rewriting it — the delta file
- Debugging a behavior: make the machine show you, don't guess
- Investigate a program you did not write
- Attaching a serial port to a card
- Toward a real machine
- Gotchas
- Without the MCP (CLI fallback)
- Where to go next

## Starting the server

```
altairsim <machine> --mcp        # <machine>: a built-in name, or a path to a .toml
```

It speaks line-delimited **JSON-RPC 2.0 on stdio**. Send `initialize`, then `tools/call`.
A machine named on the command line is loaded (disks mounted, boards fitted) but **its
`startup` is NOT run** — under `--mcp` you boot it yourself with the `run` tool, so nothing
blocks before you have control. Switching machines mid-session with `CONFIG LOAD` is safe the
same way: its `startup` runs up to the boot `RUN`, which under `--mcp` **parks** the PC rather
than entering the run loop — so `CONFIG LOAD anymachine.toml` never wedges the server. Advance
it with `run {from: …}` afterward.

`altairsim --list` shows the built-in machines. The CP/M example this guide is written
against is the machine file `examples/cpm/cpm22-buffered.toml`.

Minimal driver:

```python
import subprocess, json
p = subprocess.Popen(["altairsim", "examples/cpm/cpm22-buffered.toml", "--mcp"],
                     cwd=WORKDIR, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     text=True, bufsize=1)
# write {"jsonrpc":"2.0","id":N,"method":"tools/call","params":{"name":..,"arguments":..}}\n
# read one JSON line back per call.  results are in result.structuredContent
```

`cwd` matters: the host-bridge sandbox (`hb0`) defaults to the directory you launch from, so
launch from the directory holding the files you want to move in and out — or aim it elsewhere
with `monitor {command: "SET hb0 HOSTDIR=/path"}`.

## Register the server with your assistant

Starting the server by hand is only for a quick look. To have the **assistant** drive it, you
register `altairsim --mcp` as an MCP server with your client **once**, and from then on you just
talk to the assistant. A client needs two things: the command to run, and to know it speaks over
stdio — both of which `altairsim --mcp` satisfies.

**Register it from the directory you want as the sandbox** — the host bridge (`R`/`W`) and any
relative paths resolve against wherever the server is launched, so launch it where your files
are. If `altairsim` is not on your `PATH`, use its full path in place of `altairsim` below.

**Claude Code (the CLI).** One command, with the machine you want it to drive after the `--`
(everything after `--` is the command the client will run):

```
cd examples/ai-mcp
claude mcp add altairsim -- altairsim cpm-ai.toml --mcp
claude mcp list                       # confirm it is registered and reachable
```

Then start `claude` in that directory and give it the job in plain language:

> *Using altairsim, boot CP/M and show me what is on the disk.*

`claude mcp add` defaults to **local** scope (this project, just you). Add `--scope project` to
write a shareable `.mcp.json` into the directory instead — commit that and anyone who opens the
folder gets the same server. `claude mcp get altairsim` shows how a given one is configured.

**The registered program can be a different one from `altairsim` on your `PATH`.** A server
registered as `./altairsim` runs the copy in that folder. `altairsim` typed in a shell runs the
copy on `PATH`, which can be a different release with different tools. To start the same
program by hand, use the command that `claude mcp get altairsim` shows.

**Claude Desktop, or any other MCP client.** These read a JSON config. Add an `mcpServers` entry
naming the command and its arguments:

```json
{
  "mcpServers": {
    "altairsim": {
      "command": "altairsim",
      "args": ["/absolute/path/to/examples/ai-mcp/cpm-ai.toml", "--mcp"],
      "cwd": "/absolute/path/to/examples/ai-mcp"
    }
  }
}
```

**Use absolute paths here.** The Desktop app currently launches a server in your **home
directory**, not the project (a known Claude Code bug), so a relative machine path won't be found
and the host-bridge sandbox won't land where you expect. Give the machine an absolute path, set
`cwd` to the folder you want as the sandbox, and — if files still don't resolve — pin it from
inside with `monitor {command: "SET hb0 HOSTDIR=/abs/path"}`. On macOS, Claude Desktop's config is
`~/Library/Application Support/Claude/claude_desktop_config.json`; **restart the app** after
editing it. Other clients differ in *where* the config lives, but the `mcpServers` block is the
same shape.

## Several machines, and several projects

Nothing above is one-machine or one-project — the same mechanism scales three ways.

**The server name is a label, not the machine.** In `claude mcp add `**`altairsim`**` -- …`, the
first word names the *server*; register as many as you like under different names and an assistant
sees them all at once, each pointed at its own machine:

```
claude mcp add altair-cpm   -- altairsim cpm-ai.toml --mcp
claude mcp add altair-basic -- altairsim basic4k     --mcp
```

(Names are letters, digits, `-` and `_`.)

**Scope keeps projects apart.** The default **local** scope files the server under the directory
you ran `claude mcp add` in, so it shows up only when you start `claude` there — register altairsim
once per project, in that project's folder, and they never collide. **`--scope project`** instead
writes a `.mcp.json` into the folder, so the server travels with it (commit or copy the folder and
it comes too) — the right choice for a self-contained machine directory like `examples/ai-mcp/`.
**`--scope user`** makes one entry for every project, which suits a fixed machine that needs no
project files (a built-in like `altmon`).

**The working directory is where your files are.** altairsim's host-bridge sandbox — and any
relative `<machine>` path — resolve against the *server's* working directory. From the **`claude`
CLI** that is the directory you started `claude` in, which is why registering and running from the
machine's own folder just works, and why relative paths in a committed `.mcp.json` stay portable
(use `${CLAUDE_PROJECT_DIR}` in the paths to be robust even when `claude` is started from a
subfolder). The **Desktop app** is the exception noted above — it starts the server in your home
directory — so there, use absolute paths.

## Watching over its shoulder — and taking the keyboard

You do not have to read a transcript after the fact to see what the assistant is doing. Add
`--mirror socket:PORT` next to `--mcp` and a person can `nc localhost PORT` (or `telnet localhost
PORT` — a clean Mac has `nc` but no `telnet`) to watch the **very session the assistant is
driving** — every character the guest prints as it prints it —
and **type back onto the line to take over**, sharing the console with the assistant:

```
altairsim examples/cpm/cpm22-buffered.toml --mcp --mirror socket:2323
```

The assistant keeps driving through `run`/`send`/`recv` exactly as before — the mirror is
invisible to it — while whatever it types and whatever the guest prints also crosses the socket to
you. Type there and the guest reads it as if you had reached over and used the keyboard.
Add `?ro` to watch without being able to type — quote it (`--mirror 'socket:2323?ro'`), since
`?` is a shell wildcard and an unquoted `socket:2323?ro` makes the shell fail with `no matches
found`. One watcher at a time.

`nc` and `telnet` exit each time you restart the simulator, and Windows installs neither. For a
person who wants one window that stays open, the package has `tools/mirror-watch.sh` (macOS,
Linux; bash only) and `tools/mirror-watch.ps1` (Windows; PowerShell only). Each waits for the
mirror, shows it, and reconnects after a restart. With no argument it watches `localhost:2323`;
`PORT` or `HOST PORT` picks another. They are watch-only — to type, use `nc`. On Windows, tell
the person to start it with `powershell -ExecutionPolicy Bypass -File tools\mirror-watch.ps1`.

**On macOS and Linux the mirror can be a pseudo-terminal: `--mirror pty`.** The simulator prints
`altairsim: --mirror: open /tmp/altairsim0 (/dev/ttys004)` on stderr, and the person opens that
name with a terminal program — `screen /tmp/altairsim0`, `minicom -D /tmp/altairsim0` — which
sends each key as it is typed and adds no echo of its own, where `nc` does both badly. There is no
port to pick. The first mirror is `/tmp/altairsim0`, the next `/tmp/altairsim1`; `--mirror
pty:PATH` puts the name where you say, and `?ro` makes it watch-only. The name is a link that the
simulator removes when it stops. With nobody on it the output is dropped, so the person sees the
session from the moment they open it. Windows has no pseudo-terminal: `--mirror pty` is refused
there, so use `socket:PORT`.

Between two `run`s the guest is stopped, so a character you type then waits on the line and is
read on the next `run` — the same as staging input with `send`. To hand the console to a person,
**`start`** the guest instead (see *Leaving the guest running* below): it then runs between calls,
as after `RUN` at the monitor, and answers what the person types at once. (This is the same `|socket:PORT`
mirror the monitor's `CONNECT` offers on any line; the *Serial lines* chapter of the User Manual
covers it in full.)

### A text log a person can follow: `--log`

`--log FILE` next to `--mcp` writes what the guest prints to `FILE` as plain text, as it is
printed. It is an ordinary text file: a person can `cat` it, open it in an editor, search it or
copy from it at any time, and can follow it live with, for example, `tail -f FILE` (PowerShell:
`Get-Content -Wait FILE`). It holds the whole session from the first character, where
a mirror shows only what is printed while a watcher is connected.

```
altairsim examples/cpm/cpm22-buffered.toml --mcp --log session.log
```

The log is watch-only; use `--mirror` when the person must type. The two can be given together.
The file is emptied at each start, a bare name lands in the folder `altairsim` was started in,
and a file that cannot be opened is an error before the session starts. You can read the file
too, when you want the full console history and not only what your last `run` returned.

## The tools

`tools/list` is authoritative — ask it rather than working from the tables below. Each tool's
schema comes off the board itself, so ask `board_types` what a card can be told rather than
guessing.

**Building and inspecting a machine** — fitting boards and reading them back, mapping the bus,
reading and writing memory, the ROMs, reset — is a `board_*`, `bus_*` or `mem_*` tool, and
`tools/list` names them with their arguments.

**Driving a running guest** is the handful below, and they are the ones worth knowing by heart:

| Tool | Args | Does |
|---|---|---|
| `run` | `from?`, `input?`, `until?`, `timeout_ms?` (2000, max 600000), `max_steps?` | Type `input`, advance the guest, return what it printed. Stops on `until` match, a **prompt** (guest idle on console input), `timeout_ms`, `max_steps`, HLT, a breakpoint, a port no board decodes under `SET BUS UNCLAIMED=HALT` (`unclaimed`), a `BREAK TAPE STOP` (`tape-stop`), or a cancel of the request (`notifications/cancelled`) or a ^C sent to the altairsim process (both give `stopped: "interrupted"`) — see `stopped`. `timeout_ms` is a ceiling, not a wait: the call returns as soon as one of the others fires. `from` sets PC first (that is how you boot). Bus and board messages from the run, such as a `SET BUS UNCLAIMED=WARN` line, come back in `warnings`. **Never blocks.** |
| `send` | `text` | Type at the console without running. |
| `recv` | — | Drain output since last read, without running. |
| `regs` | — | CPU registers now (`pc`, `halted`, `registers{}`). |
| `start` | `from?`, `input?` | Start the guest and **leave it running** between calls; returns at once. See *Leaving the guest running*. |
| `stop` | — | End a free run; report `stopped`, `pc`, `steps`, `t_states` for all of it. |

**Control bytes: use `\uXXXX`.** `input` and `text` are raw bytes — whatever you pass reaches
the guest untouched, control characters included, and every line in the machine is 8-bit
clean. Write a control byte as the JSON escape it is: `\u0003` for ^C, `\u001a` for ^Z,
`\u001b` for ESC.

```
send {text: "\u0003"}                        # break a running MBASIC program
run  {input: "\u001a", until: "A>"}          # ^Z ends a PIP copy from the console
```

**If your client sends the argument text as written**, a `\r` or a `\u0003` in `input` reaches
the guest as those characters, not as one byte. Use the `TYPE` command through `monitor`
instead. `TYPE` decodes its own escapes: `\r`, `\"` for a quote, `\^C` for Ctrl-C (`\^Z`,
`\^[` for ESC), and `\xHH` for any byte. Then a bare `run` lets the guest read the keys.

```
monitor {command: 'TYPE "PRINT \"HI\"\r"'}   # a line with quotes, ended by a CR
monitor {command: 'TYPE "\^C"'}               # Ctrl-C
run     {until: "Ok"}
```

`\x03` is **not** JSON — there is no `\x` escape in the format — and it is not rejected
either: it reaches the guest as the three ordinary characters `x03`. If a control byte seems
to vanish while printable text gets through, that is the reason.

**`monitor`** `{command}` runs any one monitor command (`CONNECT`, `MOUNT`, `SET`, `IN`,
`OUT`, `DISASM`, …) and returns its text — the escape hatch for anything without a dedicated
tool.

## Knowing the commands

You do not have to memorize the monitor. Two ways to get the whole surface:

- **`cheatsheet.md`, shipped beside this file** — the full `altairsim [options]` block, every
  monitor command with its abbreviation and usage, every board and machine, the `CONNECT`
  endpoint table, and a machine-file skeleton. It is generated from the program, so it matches
  the binary you were given. Read it once for the lay of the land.
- **Ask the running machine.** `monitor {command: "HELP"}` lists every command;
  `monitor {command: "HELP <cmd>"}` prints one command's abbreviation, usage and detail
  (`?` is the same as `HELP`). For the MCP/board surface, `tools/list` and `board_types`
  self-describe.

## The pattern: an expect loop

One `run` per guest command, matching the prompt each time:

```
run {from: 65280, until: "A>"}                   # boot CP/M via the DBL PROM (65280 = FF00)
run {input: "DIR\r", until: "A>"}                # a command, read the reply
run {input: "ASM FOO\r", until: "A>", timeout_ms: 20000}
```

`\r` submits a CP/M line. `run` also returns on its own when the guest reaches a prompt
(`stopped: "idle"`), so you rarely need to guess a timeout for interactive commands.

**Send one line in each `run`, and wait for its output before the next.** The server gives
`input` to the guest at the baud rate of the board, with no pause after a carriage return. A
person cannot type that fast. A guest that has no type-ahead buffer loses the characters that
arrive while it is busy with the last line: a line arrives with its first characters missing
(`FILE` arrives as `LE`), and the guest gives no error. The MITS Programming System II monitor
on `ps2int` does this, because it reads the console with interrupts and keeps one character
only. On `ps2int`, `input: "DEP 5124\r0\r100\r252\r"` in one call loses the line `100` and
the `25` of `252`; the same four lines in four calls arrive complete. A guest that polls the port, such as `ps2` or
CP/M, takes each character when it is ready and loses none, but the rule is correct for these
guests too. This is the guest and not the simulator: real hardware at the same baud rate loses
the same characters.

**Make `until` the full prompt, and text that the guest does not print again.** A `run` stops
at the first place where `until` occurs in the output. If `until` is only the start of a longer
prompt, the call returns before the guest prints the remaining characters. For example, the
assembler of MITS Programming System II has the prompt `*ASM*`, and its editor has the prompt
`*`. With `until: "*"`, the call returns after the first `*` of `*ASM*`, and the next call gets
`ASM*`. Use `until: "*ASM*"`. A prompt that the guest prints more than one time gives the same
problem. A disk that runs `PROFILE.SUB` prints `A>` more than one time, and the guest
discards input that arrives while the file runs. Match text that only the state you want
prints.

**`timeout_ms` is a ceiling, not a wait.** The call ends the moment `until` matches or the
guest reaches a prompt, so a budget larger than the job costs you nothing — a 50-second
assembly under `timeout_ms: 120000` returns in 50 seconds, not 120. There is no reason to
trim it to what you expect the work to take, and no need to re-issue `run` by hand to walk a
long job forward. Set it to the worst case you are willing to sit through and let `until` end
the call. The maximum is 600000 (ten minutes); anything larger is clamped to it.

**`from` is a JSON number, and JSON has no hex.** Write the decimal value: `65280` for `FF00`,
`64512` for `FC00`, `63488` for `F800`. A string such as `"0xFF00"` is not a number, so the server
refuses the call and tells you the number to send: `` `from` must be a JSON number, not a string:
"0xFF00" is 65280 ``. Every tool checks its arguments this way, so a wrong type or a missing
required argument is an error, and never a silent 0.

### Stopping a `run` early, and `status`

A `run` ends by itself at `timeout_ms`. Two things stop it sooner, and both return
`stopped: "interrupted"` with what the guest printed so far:

- **Cancel the request.** Send the standard `notifications/cancelled` with the request id of the
  `run`. The server reads its input while a `run` goes on, so it sees the cancel at once. A cancel
  that names another request, or that arrives after the `run` returned, is ignored, and it never
  applies to the next call. Other requests sent during a `run` wait, and are answered in order after
  it returns.
- **Send the process a `^C`** (`kill -INT`). The first `^C` is caught and only interrupts the `run`.
  A second `^C` before the server has reported the first one ends the server. A server started in
  the background, or with `nohup`, ignores `^C`.

Either way the machine is left as it was, and the next `run` starts clean. A `^C` with no `run` in
progress does nothing to the guest, unless the guest is free-running (`start`, below). A `^C` then
ends the free run, with `stop_reason: "interrupted"`.

**`status` never waits.** It is answered by the reader thread, not the worker, so it answers even
while a `run` (or a long `monitor`, `mem_load` or `snapshot`) is in progress. It returns the board
id, `in_flight` (whether the worker is busy on any request), and the `pc` and `steps` of the last
`run` or free run. Those two go stale: a `step` or a `monitor` command moves the real PC without
changing them, and `steps` restarts at zero on the next `run`. `generation` is the one field that
always climbs, so use it to tell "still advancing" from "stuck on the same slice". Poll `status`
before you cancel, to see whether the `run` is still alive. `running` and `stop_reason` report the
free run (next section).

### Leaving the guest running: `start` and `stop`

A `run` is bounded: the guest stops when the call returns. Some jobs need the guest to keep going
between calls — a server on the guest that must answer its clients in time, two machines that
talk to each other and must run side by side, or a person who takes over the console through
`--mirror`. For these, use `start`:

```
start  {from: 65280}               # boot, and keep running; returns at once
send   {text: "TNFSD\r"}           # the guest reads it by itself -- no run needed
recv   {}                          # what it has printed so far
status {}                          # running: true
stop   {}                          # stopped: "requested", steps, t_states
```

- **The guest runs until** `stop`, a `HLT`, a breakpoint, `SET BUS UNCLAIMED=HALT`, a
  `BREAK TAPE STOP` or a `^C` to the process. Never on a timeout, and never because it is idle.
  `status` gives `running`, and once it has stopped, `stop_reason` says why.
- **Every other tool works while it runs.** The server runs the guest in short slices, and
  answers each request between two slices, with the guest paused. `send`, `recv`, `regs`,
  `mem_dump`, `breakpoints`, `monitor` — all answer in well under a millisecond. Only `run` and
  `step` are refused until you `stop`, because they move the guest themselves.
- **Output collects until `recv`.** The server holds the newest 1 MiB. If a guest prints more
  than that before you read it, `recv` reports how many older bytes it let go, in `dropped`.
- **`SET cpu0 clock_hz=N` paces the guest to that crystal**, as at the monitor, so a real serial
  device or a network peer gets wall-clock time to reply. Without it the guest runs flat out, and
  at a prompt the server naps between slices, so an idle guest does not use a whole core.
- **Only an interrupt or RESET ends a `HLT`.** `start {from: ...}` on a halted processor stops
  again at once with `halt`, as `run` does. Send `reset` first.
- **Two machines are two servers.** Start each with its own `altairsim --mcp`, and `start` both.
  They then run side by side, each on its own server.

**The console under `--mcp`.** There is no host keyboard behind a pipe, so the server moves the
console line onto an in-memory terminal that `send`, `run` and `recv` read and write. Every other
line keeps running and is serviced on every `run` slice: a second serial board on a real port, a
socket. A program that moves bytes between the console and a modem port works as it would at a real
terminal.

## Rules for a guest program

This document is about the simulator. The rules of a program that runs in the guest are in
separate skills. Each skill is one short Markdown file. In the package, each one is a folder
in `skills/`. Read the file for the program that the task uses:

| Skill | Read it when the task… |
|---|---|
| `skills/altairsim-cpm-build/SKILL.md` | builds a CP/M program: `ASM` and `LOAD`, or `M80` and `L80` |
| `skills/altairsim-hostbridge/SKILL.md` | moves a file into or out of the guest: `R`, `W`, `HDIR` |
| `skills/altairsim-cpm-text/SKILL.md` | makes or edits a text file for CP/M: CR/LF line ends, the 8.3 name |
| `skills/altairsim-mbasic/SKILL.md` | types at MBASIC, or enters a BASIC program |

`ASM.COM` has **no `INCLUDE`** directive (that is M80's `MACLIB`) — each `.ASM` carries its
own equates.

**Work on a copy of the disk.** CP/M writes to the mounted image; the `.dsk` files are not
redistributable and there is no undo — copy the machine directory first if you are about to
write in anger.

**One buffer can stand between a guest write and the bytes on your host disk, and it is the
guest's.** The BIOS of this machine is **track-buffered**: it holds the current track in RAM,
and writes it to the disk when CP/M asks for another track or drive, or waits for a key. Not
every CP/M BIOS buffers, and you cannot tell from the prompt which kind you have. **Assume that
the BIOS buffers.** A guest at the `A>` prompt is waiting for a key, so end at `A>` before you
read the image on the host, unmount it or take a snapshot; a guest stopped in the middle of a
program can still hold the last track. altairsim adds no buffer of its own: each sector the
guest writes goes into the host `.dsk` at once, so no `UNMOUNT` is needed first.

**One altairsim per image.** Each process reads the image into memory when it mounts it, so a
second altairsim on the same `.dsk` does not see what the first one wrote, and its own writes
go on top of them.

## From a bare disk image to a booting machine

The build skill starts from a machine that already has its disk wired in. When all you have
is a **bare image** — a `.dsk` or a `.imd` off a real machine — and no machine file, three
things trip up a cold start:

- **`.imd` converts to `.dsk` on MOUNT.** `monitor {command: "MOUNT dsk0:drive0 mydisk.imd"}`
  converts the ImageDisk to a raw `mydisk.dsk` beside it (asking the controller for geometry)
  and mounts *that*; a raw `.dsk` mounts as-is. The convert happens **only in the `MOUNT`
  command** — a TOML `mount = "mydisk.imd"` line does **not** convert. So the from-cold path
  is: `MOUNT` the `.imd` once to produce the `.dsk`, then reference that `.dsk` in the machine
  file.
- **Drive units are `drive0..driveN`, not `0`.** `MOUNT dsk0:0 …` is rejected — a disk
  controller names its units `drive0`, `drive1`, … (unlike a serial card's `a`/`b`/`tty`).
  When in doubt, `monitor {command: "SHOW MOUNTS"}` prints every mountable unit spelled out as
  `id:driveN` with what it holds.
- **Geometry is read from the image's size, on the same card.** A disk controller sizes itself
  from the byte count, not a mode switch or a different board — an 8,978,432-byte image comes up
  as `fdc8mb` (2048 tracks × 32 sectors of 137-byte hard sectors) on the very same 88-DCDD card
  a 330 KB floppy uses. There is no separate 8 MB card to fit: mount the image and the card
  sizes to it.
- **Build the machine file from the skeleton.** The guide always started from an existing
  machine; to write one from scratch, copy the **"A machine file, in one look"** skeleton in
  `cheatsheet.md` (beside this file) — it shows a `[[board.drive]]` with `unit`/`mount`. Pick a
  disk controller with `board_types` (or `monitor {command: "SHOW BOARDS"}`), give it a
  `[[board.drive]]` pointing at your `.dsk`, and add the boot PROM / `startup` line that
  controller boots from. The User Manual's **Disks** chapter (`altairsim-manual.pdf`) walks
  the same ground in depth.

## Amending a machine instead of rewriting it — the delta file

You rarely want to write a whole machine from scratch. A machine file that names `base =
"<other.toml>"` is a **delta**: it starts from that machine and changes only what you name.
Inside it, what a `[[board]]` block *does* is decided by its `type` and `id` together, and the
four cases are easy to get backwards — the difference between "amend the console card to connect
channel B" and "throw the console card away and fit a new one":

- **`type` + a *new* `id`** → **add** a board.
- **`type` + an `id` the base already has** → **replace** that whole card.
- **no `type`, just the `id`** → **modify in place** — change a property, leave the rest.
- **`remove = true`** → pull the board out.

Redeclaring a `type` for an `id` *this same file* already declared is an error. So to nudge one
property of an existing board, name it by `id` with **no** `type`; the moment you add a `type`
you are replacing the card, not editing it. (`board_set` over MCP is the live equivalent of the
no-`type` modify-in-place; `board_add` is the add.)

## Debugging a behavior: make the machine show you, don't guess

**Read this before you form a single theory.** When a guest misbehaves — a character dropped, a
byte mistimed, a loop that runs when it shouldn't — **the simulator already knows exactly what
happened. Get it to tell you before you decide what it is.** The debugger records every
instruction with its registers and every bus cycle; a breakpoint plus a history dump *shows* you
the cause. A hypothesis about what the guest "probably" does is almost always wrong, and each
wrong guess costs a round trip to disprove. One trace replaces a dozen guesses.

**What NOT to do** (each of these wastes hours):

- **Do not speculate a mechanism and then build on it.** "It's probably pacing / a look-ahead /
  an overrun" is a guess. Confirm it in a trace or throw it away. Do **not** propose a fix for a
  cause you have not observed.
- **Do not hand-decode bytes into instructions.** `DISASM` is the authoritative decoder — the
  same decode the CPU uses. Eyeballing opcodes invents instructions that are not there (and
  reading a PROM-shadowed region by hand yields garbage that looks like real code).
- **Do not add `printf`/file logging in the hot path.** It perturbs timing and hides the very
  timing bug you are chasing (a Heisenbug). The built-in recorder is passive — use it.
- **Do not fight the console with `expect`/pty prompt-matching.** Drive the monitor over `--mcp`
  (`monitor {command: …}`): one command in, clean text out, nothing to mis-sync.

**The method that works:**

1. **Reproduce deterministically** — the smallest input that shows the symptom, every time.
2. **Break on the exact event, not a guessed address.** `BREAK IO R <port>` stops on a port
   read, `BREAK IO W <port>` / `BREAK MEM R|W <addr>` on I/O or memory, `BREAK <addr>` (or
   `BREAK <addr> IF <expr>`) on code. An `IO`/`MEM` break needs **no** reverse-engineering to
   place — you break on the read/write itself. To stop on the *byte* an `IN` read rather than
   the fact of the read, use `BREAK IO R <port> LOADS <expr>` — it is judged after the
   instruction, so the register holds what just arrived (the status bit that finally came up,
   the byte that was out of range).
3. **Sweep, then read.** The `HISTORY` recorder is **always on**, so the moment a break fires the
   run-up to it is already captured — `HISTORY CPU 500` dumps the last 500 instructions with their
   registers, no arming needed. To go forward instead, `STEP 500` runs quietly and `HISTORY CPU
   500` dumps what it just ran. Either way, read what actually executed — do not summarize it in
   your head, read it.
4. **Follow the one datum.** Track the specific byte in `A`, the register, or the memory write
   through those instructions: where it is stored (`LD (HL),A`), where control branches, and who
   called the code (stack pointer depth and the return address). Run a **working** case beside a
   **failing** one and find the single instruction where they diverge.
5. **Only then design the fix** — against the confirmed cause, never the theory.

Worked example — the CDOS console dropping a character from a pasted command. `BREAK IO R 1`
(the TMS 5501 data port), type `DIR`, then `STEP`/`HISTORY` from each read and follow the byte in
`A`. The trace shows, as fact: every typed byte *is* read from the UART exactly once; which byte
reaches the command line and which is thrown away; and the exact branch where a kept byte and a
dropped byte part company — a dropped byte is read, stashed to a scratch address, and never
dispatched because control returns into the *previous* character's handler. "The console probably
loses bytes somewhere" was a guess that led nowhere for hours; the `HISTORY` dump answered it in
minutes. Reach for the trace first.

### The debugger commands — reach for the one that fits

The whole debugger is reachable over MCP, nearly all of it through `monitor {command: …}`; only
`regs` and `mem_dump` have dedicated tools. You do not memorize these — `monitor {command: "HELP
<cmd>"}` prints any one's syntax — but you do need to know they *exist*, because the right command
turns a guess into a fact. Grouped by what you are trying to see:

**Where the processor is, and moving it forward**

| To… | Command | Why it is the one |
|---|---|---|
| See the CPU now | `regs` (or `REGS`) | Free on every stop — you rarely type it. The last column is the next instruction, already disassembled. |
| Run one instruction, or *n* | `STEP` / `STEP 20` | Real bus cycles through the real decode — it *is* the machine moved forward one instruction. Prints the registers after each. |
| Step **over** a `CALL`/`RST` | `NEXT` (`N`) | Runs the callee as `RUN` does (paced by `clock_hz`, flat out by default) and stops the instant it returns — so you stay in the code you are reading instead of touring a print routine. On anything else it is a single step. |
| Jam the PC and look | `EXAMINE <addr>` | Sets PC to `<addr>` (the front-panel switch), then shows the register line and the instruction `STEP` will run. |

**Stopping on the exact event**

| To… | Command | Why it is the one |
|---|---|---|
| Reach a code address | `BREAK <addr>` / `BREAK <lo>-<hi>` | PC lands **on** it, nothing there has run yet — `STEP` runs it fresh. |
| Catch whatever writes/reads memory | `BREAK MEM W <addr>` / `BREAK MEM R <addr>` | Watches the **bus**, not an instruction — so it catches a DMA write no CPU instruction made, and works unchanged on any processor. This is how you find who clobbers a byte. |
| Catch a port access | `BREAK IO R <port>` / `BREAK IO W <port>` | The same, on I/O — break on the read/write itself, no address to reverse-engineer. |
| Stop only in the case you care about | `BREAK <addr> IF <expr>` | Condition on the registers. **A bare word is that register; a literal needs a leading zero** — `0A` is ten, `A` is the accumulator. `== != < > <= >= && \|\| & \|` and parens. Works on `MEM`/`IO` breaks too. |
| Stop on the byte an `IN` **read** | `BREAK IO R <port> LOADS <expr>` | Judged *after* the instruction, so the register holds the byte that just arrived. `IF` sees the inputs, `LOADS` the result. |
| Stop when a cassette auto-stops | `BREAK TAPE STOP` | A device watch — halts inside the loader the moment the tape parks, without knowing the loader's end address. |
| List / clear | `BREAK` / `NOBREAK [id]` | Ids are plain decimals, not bus addresses. |

**Reading and changing memory**

| To… | Command | Why it is the one |
|---|---|---|
| Read a block | `mem_dump` / `DUMP <addr>` | Peeks — runs no bus cycle, consumes nothing. Hex plus ASCII; read a string straight out of the right column. |
| Disassemble | `DISASM <addr> <count>` | Peeks, and decodes for the CPU actually in the machine. **Must start on an opcode** — one byte off and the listing is fiction (it re-syncs a line or two later, so it can look right while its first instruction is a phantom). Single-step to a known boundary if unsure. |
| Patch one byte / a run | `DEPOSIT <addr> <bytes>` / `EDIT <addr>` | A **real** bus write — it says so if nothing decodes the address, rather than pretending. `EDIT` also assembles an instruction in place (`IN 10` → `DB 10`). |
| Name things | `SYMBOLS LOAD prog.PRN` | Then `BREAK START`, and `DISASM` reads `CALL BDOS`, not a bare address. A `.PRN`/`.LST` listing is richer than a `.SYM` (it marks `EQU`s); an L80 `.SYM` holds globals only. |
| Find / fill / move / compare | `SEARCH` `FILL` `MOVE` `COMPARE` | `COMPARE <range> <file>` checks what the machine loaded against what you meant to load. |

**The bus and the boards**

| To… | Command | Why it is the one |
|---|---|---|
| Poke a board like the guest would | `IN <port>` / `OUT <port> <val>` | **Real** bus cycles with every side effect — consumes a UART byte, advances a sector counter. Poke a board without writing guest software. |
| Ask who answers | `WHO <addr>` / `WHO IO <port>` | No cycle run. When an `IN` gives you `FF`, `WHO` tells you whether a board answered with that byte or **the bus floated because nobody decodes it** — the single most useful disambiguation on this machine. Also flags contention and `PHANTOM*`. |
| See the whole backplane | `SHOW BUS MAP\|IO\|IRQ\|CONTENTION` | `IRQ` is the *only* window on interrupt wiring — a board strapped to a line nobody listens to fails in total silence. `CONTENTION` finds two boards on one port in a machine you built yourself. |
| Hear a board narrate itself | `SHOW DEBUG`, then `SET <ch> DEBUG=<flag>` | Instrumented parts (`dsk0` sector/seek, `6850` serial, `socket` connect) describe what they do in their own terms, each line prefixed with the PC that drove it. |

**The machine over time**

| To… | Command | Why it is the one |
|---|---|---|
| See what led to the stop | `HISTORY [n]` / `HISTORY BUS [n]` | A flight recorder that is **always on** — the run-up to any break is already recorded. Each `HISTORY` line reads like a `STEP`; `HISTORY BUS` is raw cycles naming *who drove* and *who answered* (DMA names the board; a floated read shows `--`). |
| Trace a region as it runs | `TRACE ON [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]` | Logs every matching cycle. A **tracepoint** — `BREAK <addr> TRACE ON` and `BREAK <addr> TRACE OFF` — flips tracing on entering a subroutine and off leaving it, without ever stopping the machine. An assistant has no console to trace to, so through the `monitor` tool the file is necessary: `TRACE ON <file>`, and for a tracepoint `TRACE ON <file>` then `TRACE OFF` first. The `bus_trace` tool gives the same cycles with no file. |
| Copy the whole session | `SET CONSOLE log=session.txt` | Guest output and your input to a host file as they happen. |
| Save and return to a moment | `SNAPSHOT <file>` / `RESTORE <file>` | Saves *state*, not configuration — `RESTORE` reads it back into a machine of the same shape (build the shape first with a machine file or `CONFIG LOAD`). |

The full reference, with a worked session for each, is the **Debugger** document
(`altairsim-debugger.pdf`, shipped beside this file); every command's one-line syntax is in
`cheatsheet.md`.

### Drive it through a persistent `--mcp` session, not a pty

The debugging loop above only works if controlling the machine is *effortless* — one command in,
its answer out, decide the next. You get that by keeping **one `--mcp` process open** (the minimal
driver near the top of this guide) and sending one `tools/call` per step: `monitor {command: …}`
is literally "enter a monitor command, read its text, enter the next," with **no console echo and
no prompt to match**. `run`, `regs`, `send`, `recv` fill in the rest. That is the loop for
stepping and tracing.

**Do not reach for `expect` or a raw pty to drive the interactive monitor for this.** A pty echoes
your keystrokes back *interleaved* with the machine's output, and matching the `altairsim> ` prompt
races the **stale** prompt already sitting in the buffer — so your captures come back empty or as
fragments of the next command, and a `RUN` followed by typed input races the monitor against the
guest over who reads the line. If you catch yourself logging a whole session to a file to grep
afterward, you have already lost the loop: stop and drive it over `--mcp`.

This is also the only way to reliably send a real **carriage return** — and that is the single
biggest "the simulator is broken" false alarm, so it earns its own gotcha:

- **A `\r` reaches the guest as 0x0D only if your client JSON-*escapes* it.** The server feeds
  the bytes of the decoded `input` string verbatim, so `json.dumps({"input": "DIR\r"})` from a
  persistent Python session puts a genuine 0x0D on the wire. But some tool wrappers pass the two
  characters `\` and `r` literally, or send an Enter as LF (0x0A) — and **CCP, `DDT` and
  `ASM.COM` all accept LF**, so nothing looks wrong until a program that does single-character
  reads and *requires* a true CR. `SYSGEN`'s drive prompts are exactly that: fed anything but
  0x0D they loop `Invalid drive name` forever. When Enter seems ignored, stop guessing at the
  program — drive from the persistent Python `--mcp` session, where `\r` is a real 0x0D.

Two more gotchas once you do:

- **`notifications/initialized` gets no reply.** After `initialize`, send it as a JSON-RPC
  *notification* (no `id`) and do **not** try to read a line back for it — waiting for a response
  that never comes hangs the driver. Then begin your `tools/call`s.
- **Confirm the guest is actually at its prompt before you type.** Under `--mcp` a machine's
  `startup` parks, and some boots idle through a PROM countdown/banner that `run`'s idle heuristic
  reads as "done." Loop `run` until the guest reaches its interactive prompt — press Enter with
  `run {input: "\r"}` and watch the prompt echo back — *before* you feed a command, or a boot-time
  reader swallows your first characters (an early `DIR` typed too soon showed up as the cold loader
  eating `DIR` while only the tail reached the OS).

## Investigate a program you did not write

Building is half of it; the other half is taking a binary apart to see how it works — a monitor
loader, a period utility, a game — which is what the machine is really for. The same command crib
above is the whole toolkit; the moves that take a program apart are `SYMBOLS LOAD prog.PRN` (so
`DISASM` reads `CALL BDOS`, not a bare address), `BREAK <addr>` at the load address (it trips the
moment CP/M's loader jumps in, before the first instruction), then `DISASM` the region and `STEP`
through it — `NEXT` over the calls you already trust.

The pattern for "explain what this does": load or run the code far enough to have it in memory,
`BREAK` where you want to start looking, `DISASM` the region, then `STEP` through the interesting
part reading the registers — the same way you would at a front panel, but with the assistant
doing the bookkeeping. The `examples/ai-mcp/` walkthrough does exactly this to find a planted bug:
it breaks at the load address, disassembles the loop, and single-steps until a register shows the
wrong value — then fixes the source and reassembles. The full command set with a worked session for
each is the **Debugger** document (`altairsim-debugger.pdf`), and every command's syntax is in
`cheatsheet.md` beside this file.

## Attaching a serial port to a card

A serial channel `CONNECT`s to an endpoint: `console | null | loopback | serial:/dev/tty… |
socket:PORT | socket:HOST:PORT`.

```
board_add {type: "2sio", id: "sio1"}                                # a second 88-2SIO card
board_set {id: "sio1", key: "port", value: "14"}                    # base 0x14 -- PORT IS HEX
monitor  {command: "SET sio1:b BAUD=9600"}                          # baud is a unit strap
monitor  {command: "CONNECT sio1:b serial:/dev/tty.usbserial-XXXX"} # a real host port
monitor  {command: "CONNECT sio1:b loopback"}                       # TX->RX plug, for self-test
```

One 88-2SIO is **two channels**: `a` at `base+0/base+1`, `b` at `base+2/base+3`. Address them
`sio1:a` / `sio1:b` (the SIMH-style `2SIO1:B` is the same thing). Base ports do not collide —
each card owns four ports, so `sio0` at 0x10 and `sio1` at 0x14 coexist.

Framing (8N1 …) is **not** a setting — the guest writes it into the 6850 control register and a
real host port is reprogrammed to follow. During every `run`, each connected line is serviced,
so a program shuttling bytes between the console and a modem port works live: bytes the far end
sends arrive on the guest console, and bytes typed on the console arrive at the far end.

### 88-2SIO / 6850 register crib

Ports (base `B`): `B+0` ch-A control(write)/status(read), `B+1` ch-A data; `B+2`/`B+3` ch-B.

- **Status (read), true sense** (the 88-**SIO** is inverted — do not confuse them):
  `RDRF=0x01` (rx full), `TDRE=0x02` (tx empty), `DCD=0x04`, `CTS=0x08`, `IRQ=0x80`.
- **Control (write):** bits 0-1 divide (`11`=master reset), bits 2-4 word select, bits 5-6
  transmit/RTS control (`00`=RTS **low/asserted**+TIE off, `10`=RTS **high/deasserted**+TIE off,
  `11`=break), bit 7 = RIE.
- **Always two writes:** `0x03` (master reset — latches and *holds* the chip) then a real
  divide+word-select. 8N1 = **`0x15`** (÷16, RTS asserted, no interrupts); 8N2 = `0x11`. To
  drop RTS without disturbing framing, write `0x55`. Master reset does **not** clear the other
  control bits; a bus RESET does **not** touch the 6850 (it has no reset pin).

### Driving a real serial device — the clock is the trap

The moment the far end is real hardware with real reply latency, the CPU clock stops being a
performance knob and becomes a **timing** one. A period BIOS times a serial read with an
instruction-count busy-loop calibrated for one clock — Mike Douglas's `srByte` does `LXI B,41667`
= "1 s at 2 MHz". `clock_hz` rescales that constant: `SET cpu0 clock_hz=4000000` halves the
timeout, `clock_hz=0` (flat-out, the default) burns it in microseconds. Run the guest too fast
and its timeout shrinks below the device's actual reply latency — the read times out, retries,
and desyncs **before the answer arrives**. Pin `clock_hz` to the clock the guest's constants
assume — **2 MHz for classic Altair code** — unless you have measured headroom.

How much margin you need is timeout-vs-latency, not the clock alone. A per-transaction protocol
(the device pauses to seek or process between requests) needs a big margin; a back-to-back
streaming transfer tolerates far less — the same guest code has desynced at 4 MHz on a per-sector
protocol yet run clean well past 20 MHz on a whole-track one. And a serial poll loop is
I/O-bound: past a modest clock, raising `clock_hz` buys **no** throughput, it only erodes the
margin. Don't reach for a faster clock to "speed up" a wire-bound transfer.

**Watch the wire.** Tee a line to a chronological hex dump — the single best view of exact TX/RX
interleaving and timing:

```
monitor {command: "CONNECT sio0:b serial:/dev/cu.usbserial-XXXX |cap.log?fmt=dump&ts=elapsed&gap=50"}
```

`gap` is a bare millisecond integer (`gap=50`, not `50ms`; `0` = never break a row on a pause).
The tap lives **in the sim process**, so it stops on `QUIT` — a late reply a device streams after
you have quit never reaches the log.

**One owner per host port.** Exactly one process may hold `/dev/cu.…`; close any `pyserial` (or
other altairsim) that has it, or the attach fails busy. altairsim already flushes stale RX on open
(`tcflush`), so an external flush is redundant — and worse, opening the port from pyserial toggles
DTR/RTS, which can knock a device out of its current mode. Let the sim own the port.

**`timeout_ms` bounds a live transfer too.** Traffic on a real device does not extend the budget —
it never has since #487. Give a streaming read a `timeout_ms` as long as its worst case (up to
600000 ms) and let `until` end the call early, same as any other command; a call that hits
`timeout_ms` mid-transfer returns `stopped: "timeout"` with what it read so far, which is a normal
result to resume `run` (no new `from`) on, not a failure. Watch a destination pointer climb via
`regs`/`mem_dump` if you want to confirm it is still making progress rather than stuck.

## Toward a real machine

The reason the serial attach matters: the endpoint a channel `CONNECT`s to is the only thing that
changes between the simulator and the metal. Build and debug a program on the simulated machine —
where you can single-step it and dump its memory — and when it works, `CONNECT` the same channel
to `serial:/dev/cu.…` (a USB-to-serial cable to a real 8800, an 8800c, or any period machine) and
send the identical bytes at real hardware. The guest program does not know the difference; only
the endpoint moved. That makes the simulator a bench for the real machine: prove it here, then run
it there, and when they disagree you have a known-good side to compare against.

(On macOS use the `/dev/cu.*` name, not `/dev/tty.*` — `cu` does not block waiting for carrier.)

## Gotchas

- **The CP/M BIOS trashes registers.** `CONST`/`CONIN`/`CONOUT` may clobber any register. Keep
  loop state in **memory**, not a register (this BIOS preserves `HL` but not `B`).
- **Card base vs. channel register.** `board_set … port` is the card's **hex** base (`0x14`),
  but a guest program may prompt in **decimal** for a specific register. For channel `b` of a
  card based at `0x14`, the 6850 control/status port is `0x16` = **22** and its data register is
  the next port, `0x17`. Feeding a program a card base instead aims it two ports high — at
  undecoded I/O that floats to `0xFF`, so it "receives" an endless stream of `0xFF`.
- **Disk units are `driveN`, and `.imd` converts on MOUNT** — `MOUNT dsk0:0 …` is rejected;
  the unit is `drive0`. `SHOW MOUNTS` spells them out. A `.imd` becomes a raw `.dsk` on
  interactive `MOUNT` only, not through a TOML `mount =` line (see the bare-image section).
- **High bytes in output** — a serial terminal can print any byte; the server escapes
  non-UTF-8 bytes as `\u00XX` so the JSON stays valid. Read `output` as text.
- **Debug at runtime** — `mem_dump` the (possibly self-modified) code and `regs` mid-run.

## Without the MCP (CLI fallback)

If you cannot run the MCP server, the same machine answers the monitor:

```
altairsim examples/cpm/cpm22-buffered.toml -x 'BOARDS' -i          # run a command, then stay interactive
altairsim examples/cpm/cpm22-buffered.toml -s script.cmd           # run a command script, exit with status
```

But note: a bare monitor **`RUN` blocks on stdin under a pipe** (stdin is the script/JSON-RPC
channel), so anything that reads the guest console wants the MCP `run` tool or a real TTY /
`expect`. `STOP` = **`^E`** returns from a running guest to the monitor; **`^C` belongs to
CP/M** (warm boot), so the guest keeps it. There is no `BOOT` verb — the DBL boot PROM at
`FF00` is the boot command (`RUN FF00`).

## Where to go next

The **User Manual** (`altairsim-manual.pdf`, shipped beside this file) is the full reference —
the machines, the boards, the monitor, serial, disks, and the MCP server in depth. This guide
is the operator's crib for driving it all through MCP, and **`cheatsheet.md`** (also beside this
file) is the at-a-glance list of every option and command when you just need the syntax.
