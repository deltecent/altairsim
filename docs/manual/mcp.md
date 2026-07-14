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

## The schemas describe themselves

**Every tool's schema comes off the same reflection layer as the TOML keys and the
`SET`/`SHOW` commands.** There is one description of what a board is and what it can be
asked, and the machine file parser, the monitor, and the MCP server all read it.

The consequence is the point: **a board added tomorrow is drivable by an assistant the day
it lands, with no new code.** Nobody writes an MCP tool for the new card. The card declares
its properties, as it must anyway to be configurable at all, and the tool schema is that
declaration.

So there is no tool reference in this manual. Start the server and ask it what it has —
that is what `board_types` is for, and its answer is authoritative in a way a printed list
could never be.

## Configuring an assistant to use it

MCP clients differ, but they all want the same two things: a command to run, and the fact
that it speaks over stdio. The command is `altairsim --mcp`, and it does.
