# The MCP server

```
$ altairsim --mcp
```

That runs `altairsim` as an **MCP (Model Context Protocol) server** on stdin and stdout, so
that an AI assistant — Claude, or anything else that speaks MCP — can drive the machine
through **typed, structured tools** instead of screen-scraping a text terminal.

It is not a wrapper, and it is not a second model of the world. **The MCP server runs on the
same machine object as the monitor.** What the tools see is exactly what `SHOW` sees, because
it is the same machine, answering the same questions, through a different door.

## What the tools do

Enough to operate the machine:

- List the board types available, and every property each one has — **with its type, its
  default and its legal range.**
- List the boards actually in the machine.
- Get and set any property on any board.
- Add a board; mount a disk or a tape into it; wire a serial line to an endpoint.
- Examine, deposit, fill, search, save and disassemble memory.
- Run the machine, step it, set breakpoints, and read the bus flight recorder.
- Snapshot the whole machine's state and restore it.

The five you will see named are `board_types`, `board_list`, `board_get`, `board_set` and
`board_add`. The rest follow the monitor's own vocabulary.

## Driving a running guest

Building a machine is half of it; the other half is **operating one that is running** —
typing at its console and reading what it prints. Four tools do that, and they are what
let an assistant boot CP/M, run `ASM`, and talk to a program over a serial port entirely
through MCP:

- **`run`** — advance the guest a bounded slice and return what it printed. It is the
  expect loop in one call: pass `input` to type a line, `until` to stop when a string
  (a prompt like `A0>`) appears, `from` to set the PC first (booting is `from` the boot
  PROM). It **also stops on its own when the guest reaches a prompt** — spinning on the
  console with nothing to say — so you get control back without guessing a timeout. Every
  stop says why in `stopped`: `match`, `idle`, `timeout`, `steps`, `halt`, `breakpoint`.
- **`send`** — type at the console without running (then `run` to let it be read).
- **`recv`** — drain what the guest has printed since you last looked, without running.
- **`regs`** — the CPU registers right now.

The shape of a session is therefore: `run {from: 0xFF00, until: "A0>"}` to boot, then
`run {input: "ASM FOO\r", until: "A0>"}` per command, reading the reply each time. A `run`
**never blocks** — it runs the guest flat out for at most `timeout_ms` (default 2000) and
returns — so a `tools/call` always comes back, unlike a bare `RUN` through the `monitor`
tool, which under a pipe waits on a stdin that is the JSON-RPC channel itself.

Under `--mcp` the console line is quietly re-seated onto an in-memory terminal the server
owns (there is no host keyboard behind a pipe), which is what `send`/`run`/`recv` read and
write. Everything else on the machine — a second serial board wired to a real port, a
socket — keeps running and is serviced on every `run` slice, so a program shuttling bytes
between the console and a modem port works exactly as it would at a real terminal.

## Watching over your shoulder — `--mirror`

Add `--mirror socket:PORT` alongside `--mcp` and a person can `telnet localhost PORT` to
**watch the very session the assistant is driving** — every character the guest prints —
and **type back onto the line to take over**, sharing the console:

```
$ altairsim cpm --mcp --mirror socket:2323
```

It wraps the assistant's console in the same mirror the monitor offers (`<endpoint>|socket:
PORT`, see the *Serial lines* chapter). The assistant keeps driving through `run`/`send`/
`recv` exactly as before — the mirror is transparent to it — while whatever it types and
whatever the guest prints also crosses the socket to the watcher. Add `?ro`
(`--mirror socket:2323?ro`) to make it watch-only. One watcher at a time.

The watcher never sets the pace, and one thing follows from that: the guest only advances
**during a `run`**, so a character the watcher types between runs waits on the line and is
read on the next `run` — the same as `send` staging input for the next `run`. While a `run`
is in flight the two share the console live.

## Debugging and inspecting

The monitor's debugger is here too, structured. `step` advances a set number of instructions
and hands back the register file and where the CPU came to rest; `breakpoints` lists, adds and
removes the same breakpoints `BREAK` sets — and because they are the machine's breakpoints, a
`run` or a `step` stops when one fires. `disasm` decodes memory through a non-invasive read, so
it works on a ROM and even with no processor running. `bus_trace` returns the always-on flight
recorder — the last cycles every board saw, with who drove and who answered — and `bus_irq`
reports the interrupt lines the way `bus_map` reports the decode. `snapshot` and `restore` save
the whole machine's state and read it back into a machine built the same way.

None of these block, and none of them need the console: they are questions about the machine,
answered the same way `SHOW` answers them. When a typed tool does not reach a corner you need —
a conditional breakpoint, an octal listing — the `monitor` tool runs any monitor command and
returns its text.

## The schemas describe themselves

**Every tool's schema comes off the same reflection layer as the TOML keys and the
`SET`/`SHOW` commands.** There is one description of what a board is and what it can be
asked, and the machine file parser, the monitor, and the MCP server all read it.

The consequence is the point: **a board added tomorrow is drivable by an assistant the day
it lands, with no new code.** Nobody writes an MCP tool for the new board. The board declares
its properties, as it must anyway to be configurable at all, and the tool schema is that
declaration.

So there is no tool reference in this manual. Start the server and ask it what it has —
`tools/list` returns every tool the server exposes, and `board_types` every board type;
their answers are authoritative in a way a printed list could never be.

## Configuring an assistant to use it

MCP clients differ, but they all want the same two things: a command to run, and the fact
that it speaks over stdio. The command is `altairsim <machine> --mcp`, and it does. Register it
**once** and from then on you talk to the assistant, not to the server.

**Claude Code (the command line)** takes it as one command — everything after `--` is what it
will run, and it is best run from the directory you want the machine's files to resolve against:

```
claude mcp add altairsim -- altairsim <machine> --mcp
claude mcp list                       # confirm it registered and is reachable
```

**Claude Desktop, or any client that reads a JSON config**, wants an `mcpServers` entry naming
the command and its arguments (a `cwd` sets the working directory, since a desktop app has no
shell to inherit one from):

```json
{
  "mcpServers": {
    "altairsim": { "command": "altairsim", "args": ["<machine>", "--mcp"] }
  }
}
```

The `<machine>` is a built-in name or a machine file, exactly as on the command line. If
`altairsim` is not on your `PATH`, give its full path as the command.

You are not limited to one. Register several machines under different names (`altair-cpm`,
`altair-basic`) and an assistant sees them all at once; `claude mcp add`'s *scope* decides whether
a server is tied to the one project directory you added it from (the default), travels with a
folder as a `.mcp.json` (`--scope project`), or is available everywhere (`--scope user`). One thing
to know for file exchange: the host-bridge sandbox is the server's working directory. The `claude`
command line sets that to wherever you started it, so a relative machine path and the sandbox line
up with the folder you are in; the desktop app currently starts the server in your home directory
instead, so there give the machine an absolute path.

`DRIVING-WITH-AI.md`, in this package, is the briefing written for the assistant itself — drop it
in a working directory and the assistant has the recipes for booting, building and debugging over
these tools. The `examples/ai-mcp/` folder is a ready-made such directory, with a walkthrough that
has an assistant find and fix a bug entirely over MCP.
