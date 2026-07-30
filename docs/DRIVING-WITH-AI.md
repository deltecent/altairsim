# Driving altairsim with an AI

How to let an **AI assistant** drive the **altairsim** MITS Altair 8800 simulator through its
built-in MCP server — so you can say *"using altairsim, do …"* and it does it, instead of you
cutting and pasting between a chat window and a terminal. It can boot a machine, type at its
console, read what it prints, build a CP/M program, single-step and debug one, and talk to a
program over a real serial port — all through typed tools.

This works with **any MCP-capable assistant**. The examples below use **Claude** as the concrete
client, but the server speaks the open Model Context Protocol and the same steps apply elsewhere.

Every recipe below was **verified end to end** against the CP/M machine that ships in this
package (`{{MACHINE_CPM}}`): the assemble-and-extract build and the serial attach both run
green. Point at the `altairsim` you were given.

**New to this?** The `examples/ai-mcp/` folder is a ready-made working directory: register the
server there (below) and ask your assistant to build and fix the little program waiting in it —
a complete, guided round trip through everything this document describes.

## Starting the server

```
altairsim <machine> --mcp        # <machine>: a built-in name, or a path to a .toml
```

It speaks line-delimited **JSON-RPC 2.0 on stdio**. Send `initialize`, then `tools/call`.
A machine named on the command line is loaded (disks mounted, boards fitted) but **its
`startup` is NOT run** — under `--mcp` you boot it yourself with the `run` tool, so nothing
blocks before you have control.

`altairsim --list` shows the built-in machines. The CP/M example this guide is written
against is the machine file `{{MACHINE_CPM}}`.

Minimal driver:

```python
import subprocess, json
p = subprocess.Popen(["altairsim", "{{MACHINE_CPM}}", "--mcp"],
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

## The tools

`tools/list` is authoritative — it returns **19** tools on this build. Each tool's schema comes
off the board itself, so ask `board_types` what a card can be told rather than guessing.

**Build / inspect a machine:** `board_types`, `board_list`, `board_get`, `board_add`,
`board_set`, `who`, `bus_map`, `bus_io`, `bus_contention`, `mem_dump`, `mem_deposit`,
`mem_load`, `roms`, `reset`.

**Drive a running guest:**

| Tool | Args | Does |
|---|---|---|
| `run` | `from?`, `input?`, `until?`, `timeout_ms?` (2000), `max_steps?` | Type `input`, advance the guest, return what it printed. Stops on `until` match, a **prompt** (guest idle on console input), `timeout_ms`, `max_steps`, HLT or breakpoint — see `stopped`. `from` sets PC first (that is how you boot). **Never blocks.** |
| `send` | `text` | Type at the console without running. |
| `recv` | — | Drain output since last read, without running. |
| `regs` | — | CPU registers now (`pc`, `halted`, `registers{}`). |

**`monitor`** `{command}` runs any one monitor command (`CONNECT`, `MOUNT`, `SET`, `IN`,
`OUT`, `DISASM`, …) and returns its text — the escape hatch for anything without a dedicated
tool.

## The pattern: an expect loop

One `run` per guest command, matching the prompt each time:

```
run {from: 0xFF00, until: "A>"}                  # boot CP/M via the DBL PROM
run {input: "DIR\r", until: "A>"}                # a command, read the reply
run {input: "ASM FOO\r", until: "A>", timeout_ms: 20000}
```

`\r` submits a CP/M line. `run` also returns on its own when the guest reaches a prompt
(`stopped: "idle"`), so you rarely need to guess a timeout for interactive commands — set a
generous `timeout_ms` only for long silent work (assembling, a disk load).

## Recipe: build a CP/M program end to end

Machine: the CP/M config (`{{MACHINE_CPM}}`). Its disk already carries `ASM.COM`, `LOAD.COM`
and the host-bridge `R/W/HDIR.COM`. Launch altairsim **from the directory holding your
source**, or aim the sandbox elsewhere: `monitor {command: "SET hb0 HOSTDIR=/path"}`.

```
run {from: 0xFF00, until: "A>"}                          # boot
run {input: "R FOO.ASM\r",  until: "A>"}                 # host -> CP/M (host-bridge)
run {input: "ASM FOO\r",    until: "A>", timeout_ms: 20000}   # -> FOO.HEX + FOO.PRN
run {input: "LOAD FOO\r",   until: "A>"}                 # -> FOO.COM
run {input: "W FOO.COM\r",  until: "A>"}                 # CP/M -> host (binary, default)
run {input: "W FOO.HEX FOO.HEX T\r", until: "A>"}        # T = text (trims trailing ^Z)
run {input: "W FOO.PRN FOO.PRN T\r", until: "A>"}
```

A clean assembly prints `END OF ASSEMBLY` (and a `USE FACTOR`); `LOAD` reports the load
address range. The host bridge:

- `R <hostfile> [cpmfile]` — host → CP/M. `W <cpmfile> [hostfile] [B|T]` — CP/M → host.
- **`B` (binary) is the default and is what a `.COM` needs**; use **`T`** for text
  (`.PRN`/`.HEX`/`.TXT`) so the trailing `^Z` padding is trimmed. Never `T` a `.COM`.
- `HDIR [pattern]` lists the host side. Before any utility exists on a fresh disk, paste a
  source in with `PIP FOO.ASM=CON:` (end with `^Z`).

**Source files must be CR/LF.** `R` copies bytes verbatim; DR `ASM.COM` needs CR/LF line
endings, and an LF-only file assembles to nothing (`000H USE FACTOR`). `ASM.COM` also has **no
`INCLUDE`** directive (that is M80's `MACLIB`) — each `.ASM` carries its own equates.

**Work on a copy of the disk.** CP/M writes to the mounted image; the `.dsk` files are not
redistributable and there is no undo — copy the machine directory first if you are about to
write in anger. This is a **track-buffered** BIOS: it holds the current track in RAM and
commits it when CP/M changes track or warm-boots. Getting back to a live `A>` prompt is a warm
boot, so end every session at `A>` before you unmount or snapshot, or the last write is lost.

## Investigate a program you did not write

Building is half of it; the other half is taking a binary apart to see how it works — a monitor
loader, a period utility, a game — which is what the machine is really for. The whole debugger is
reachable over MCP, most of it through the `monitor` tool:

- **`monitor {command: "DISASM <addr> <count>"}`** — disassemble live memory. The address is
  hex (`0x100`); the count is a number of instructions.
- **`monitor {command: "SYMBOLS LOAD prog.PRN"}`** — teach it the names from an assembler
  listing, and `DISASM` then reads `JZ DONE` and `CALL BDOS` instead of bare addresses.
- **`monitor {command: "BREAK <addr>"}`** (or `BREAK <addr> IF <expr>`) — stop when execution
  reaches a point, conditionally on a register or memory value. A breakpoint set at a program's
  load address trips the moment CP/M's loader jumps into it, before its first instruction.
- **`monitor {command: "EXAMINE <addr>"}`** then **`monitor {command: "STEP <n>"}`** — jam the
  PC and single-step, watching each instruction and the registers after it.
- **`regs`** for the CPU state at any moment, **`mem_dump`** for memory (including code the
  program modified as it ran), **`HISTORY`**/`TRACE` for what led up to a stop.

The pattern for "explain what this does": load or run the code far enough to have it in memory,
`BREAK` where you want to start looking, `DISASM` the region, then `STEP` through the interesting
part reading the registers — the same way you would at a front panel, but with the assistant
doing the bookkeeping. The `examples/ai-mcp/` walkthrough does exactly this to find a planted bug:
it breaks at the load address, disassembles the loop, and single-steps until a register shows the
wrong value — then fixes the source and reassembles. The full command set is in the User Manual's
**Debugging** chapter (`altairsim-manual.pdf`).

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

- **CR/LF sources** — the single most common assemble-to-nothing cause (see above).
- **The CP/M BIOS trashes registers.** `CONST`/`CONIN`/`CONOUT` may clobber any register. Keep
  loop state in **memory**, not a register (this BIOS preserves `HL` but not `B`).
- **Card base vs. channel register.** `board_set … port` is the card's **hex** base (`0x14`),
  but a guest program may prompt in **decimal** for a specific register. For channel `b` of a
  card based at `0x14`, the 6850 control/status port is `0x16` = **22** and its data register is
  the next port, `0x17`. Feeding a program a card base instead aims it two ports high — at
  undecoded I/O that floats to `0xFF`, so it "receives" an endless stream of `0xFF`.
- **High bytes in output** — a serial terminal can print any byte; the server escapes
  non-UTF-8 bytes as `\u00XX` so the JSON stays valid. Read `output` as text.
- **Debug at runtime** — `mem_dump` the (possibly self-modified) code and `regs` mid-run.

## Without the MCP (CLI fallback)

If you cannot run the MCP server, the same machine answers the monitor:

```
altairsim {{MACHINE_CPM}} -x 'BOARDS' -i          # run a command, then stay interactive
altairsim {{MACHINE_CPM}} -s script.cmd           # run a command script, exit with status
```

But note: a bare monitor **`RUN` blocks on stdin under a pipe** (stdin is the script/JSON-RPC
channel), so anything that reads the guest console wants the MCP `run` tool or a real TTY /
`expect`. `ATTN` = **`^E`** returns from a running guest to the monitor; **`^C` belongs to
CP/M** (warm boot), so the guest keeps it. There is no `BOOT` verb — the DBL boot PROM at
`FF00` is the boot command (`RUN FF00`).

## Where to go next

The **User Manual** (`altairsim-manual.pdf`, shipped beside this file) is the full reference —
the machines, the boards, the monitor, serial, disks, and the MCP server in depth. This guide
is the operator's crib for driving it all through MCP.
