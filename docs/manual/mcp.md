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
- Add a board.
- Examine and deposit memory.
- Run the machine, and step it.

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
write. Everything else on the machine — a second serial card wired to a real port, a
socket — keeps running and is serviced on every `run` slice, so a program shuttling bytes
between the console and a modem port works exactly as it would at a real terminal.

## The schemas describe themselves

**Every tool's schema comes off the same reflection layer as the TOML keys and the
`SET`/`SHOW` commands.** There is one description of what a board is and what it can be
asked, and the machine file parser, the monitor, and the MCP server all read it.

The consequence is the point: **a board added tomorrow is drivable by an assistant the day
it lands, with no new code.** Nobody writes an MCP tool for the new card. The card declares
its properties, as it must anyway to be configurable at all, and the tool schema is that
declaration.

So there is no tool reference in this manual. Start the server and ask it what it has —
`tools/list` returns every tool the server exposes, and `board_types` every board type;
their answers are authoritative in a way a printed list could never be.

## Configuring an assistant to use it

MCP clients differ, but they all want the same two things: a command to run, and the fact
that it speaks over stdio. The command is `altairsim --mcp`, and it does.
