# examples

**Machines that boot.** Each directory here is self-contained: a `.toml` that describes the
machine, the media it needs lying beside it, and a note saying what you will see. Copy any one of
them anywhere and it still runs — a path inside a machine file resolves against **that file**, not
against the directory you launched from.

## In the release

These eight are what the distribution zip carries — the machines `docs/manual/quick-start.md`
names. Each is a `DIR` line in `docs/package.map`, `tools/build-package.sh` assembles them, and
`acceptance-examples` boots every one out of a scratch directory with no repository in sight —
because "does it work here" and "does it work where we hand it to people" turned out to be
different questions.

```
altairsim examples/cpm/cpm22-buffered.toml     # CP/M 2.2b on an 8" floppy
altairsim examples/cpm/cpm22-terminal.toml     # ...the same, console in a built-in VT100 window
altairsim examples/basic/basic4k.toml          # Altair 4K BASIC, off a 1975 cassette
altairsim examples/diskbasic/diskbasic.toml    # Altair Disk Extended BASIC 4.1 on an 8" floppy
altairsim examples/hdsk/hdsk.toml              # CP/M off an 88-HDSK hard disk
altairsim examples/acr/mitstapes.toml          # CP/M + WRTAPE, writing MITS cassettes to an 88-ACR
altairsim examples/printing/printer.toml       # an 88-C700 printer, wired to a real host printer
altairsim examples/debugger/debugger.toml      # a bench for learning the symbolic debugger
```

| | What it is |
|---|---|
| [`cpm/`](cpm/) | Mike Douglas's track-buffered **CP/M 2.2b v2.3**, 56K, booted by the DBL PROM from an 8" floppy. `A>` in one command. `cpm22-terminal.toml` boots the same disk with its console in a **built-in VT100 window** the simulator draws itself — no telnet client, no external emulator. |
| [`basic/`](basic/) | **Altair 4K BASIC 3.1** (`basic4k.toml`) read off a period `.tap` by the bootstrap MITS shipped, unmodified: `MEMORY SIZE?`. Beside it, **8080 BASIC VER 1.0** (`basic1.toml`) — the first Altair BASIC, 1975 — booted the raw two-step way its primitive bootstrap forced: `RUN 1800`, `^E`, `RUN 0` → `MEMSIZ?`. Both also come as `.wav` audio cassettes for the 88-ACR (`basic4k-wav.toml`, `basic1-wav.toml`). |
| [`diskbasic/`](diskbasic/) | **Altair Disk Extended BASIC 4.1** (MITS, 1977) on an 8" floppy behind an 88-DCDD, booted by the DBL PROM. Unlike the cassette BASIC, this one has a filesystem — `SAVE` by name, a directory, `DSKINI`. `MEMORY SIZE?` |
| [`hdsk/`](hdsk/) | **CP/M 2.2** booting off an Altair **88-HDSK "Datakeeper"** hard disk — an outboard controller moving whole sectors over an 88-4PIO — loaded by the HDBL PROM. `A0>` |
| [`acr/`](acr/) | Mike Douglas's **MITS Tapes** CP/M disk and **WRTAPE**, the utility that writes any of the MITS distribution BASICs back out through an **88-ACR** — a bootable audio cassette you can then load on `basic4k` / `basic8k` / `ps2`, or on real hardware. |
| [`printing/`](printing/) | An **88-C700 line printer**, and a banner program that prints through it. The README sets up a real printer on your host — a network printer over `socket:`, or a CUPS queue over `printer:` — and a page comes out. Per-OS host setup (macOS, Linux; Windows pending). |
| [`debugger/`](debugger/) | A 46-byte program with its **symbols** and a guided walk through the monitor's debugger: `SYMBOLS LOAD`, symbolic `DISASM`, single-step, break on a label, run. |
| [`ai-mcp/`](ai-mcp/) | A working directory for an **AI assistant driving altairsim over MCP**: a CP/M machine and a tiny `HELLO.ASM` with one deliberate bug the assistant assembles, runs, single-steps to find, and fixes — all through the simulator's MCP tools. See `DRIVING-WITH-AI.md`. |

## More machines — the `altairsim-machines` companion

There are more folders under `examples/` than the nine above: Cromemco boards, the SD Systems
and Tarbell and iCOM disk systems, a Sol-20, the dual-card storage boards, and others. They are
real, tested machines — each keeps its acceptance test — that simply do not travel in the core
zip. They are published on their own through **`altairsim-machines`**, a companion distribution
that mirrors them out of this tree. Browse the folders here to see what is available; each
carries its own README saying what it is and what to type.
