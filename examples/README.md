# examples

**Machines that boot.** Each directory here is self-contained: a `.toml` that describes the
machine, the media it needs lying beside it, and a note saying what you will see. Copy any one of
them anywhere and it still runs — a path inside a machine file resolves against **that file**, not
against the directory you launched from.

## In the release

These five examples come in the release package. Each folder holds a machine file, the media
that it needs and a README that tells you what to type.

```
altairsim examples/cpm/cpm22-buffered.toml     # CP/M 2.2b on an 8" floppy
altairsim examples/cpm/cpm22-terminal.toml     # ...the same, console in a built-in VT100 window
altairsim examples/basic4k/basic4k.toml        # Altair 4K BASIC, off a 1975 cassette
altairsim examples/basic1/basic1.toml          # Altair BASIC 1.0, the first one, off a cassette
altairsim examples/debugger/debugger.toml      # a bench for learning the symbolic debugger
```

| | What it is |
|---|---|
| [`cpm/`](cpm/) | Mike Douglas's track-buffered **CP/M 2.2b v2.3**, 56K, booted by the DBL PROM from an 8" floppy. `A>` in one command. `cpm22-terminal.toml` boots the same disk with its console in a **built-in VT100 window** the simulator draws itself — no telnet client, no external emulator. |
| [`basic4k/`](basic4k/) | **Altair 4K BASIC 3.1** read off a period `.tap` by the bootstrap that MITS shipped, unmodified: `MEMORY SIZE?`. |
| [`basic1/`](basic1/) | **Altair BASIC 1.0**, the first Altair BASIC, read off a period `.tap` by its own bootstrap. The bootstrap never jumps into BASIC, so you press `Ctrl-E` and type `RUN 0`, as an operator did in 1975: `MEMSIZ?`. |
| [`debugger/`](debugger/) | A 54-byte program with its **symbols** and a guided walk through the monitor's debugger: `SYMBOLS LOAD`, symbolic `DISASM`, single-step, break on a label, run. |
| [`ai-mcp/`](ai-mcp/) | A working directory for an **AI assistant driving altairsim over MCP**: a CP/M machine and a tiny `HELLO.ASM` with one deliberate bug the assistant assembles, runs, single-steps to find, and fixes — all through the simulator's MCP tools. See `DRIVING-WITH-AI.md`, or the `altairsim` Agent Skill, which an assistant that reads skills loads by itself: `skills/altairsim/` in the package, `.claude/skills/altairsim/` in this repository. |

## More machines

More ready-to-run machines, with more documentation, are at https://altairsim.com. Each one has a
README that says what it is and what to type.
