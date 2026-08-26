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
blocks before you have control. Switching machines mid-session with `CONFIG LOAD` is safe the
same way: its `startup` runs up to the boot `RUN`, which under `--mcp` **parks** the PC rather
than entering the run loop — so `CONFIG LOAD anymachine.toml` never wedges the server. Advance
it with `run {from: …}` afterward.

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

## Watching over its shoulder — and taking the keyboard

You do not have to read a transcript after the fact to see what the assistant is doing. Add
`--mirror socket:PORT` next to `--mcp` and a person can `telnet localhost PORT` to watch the
**very session the assistant is driving** — every character the guest prints as it prints it —
and **type back onto the line to take over**, sharing the console with the assistant:

```
altairsim {{MACHINE_CPM}} --mcp --mirror socket:2323
```

The assistant keeps driving through `run`/`send`/`recv` exactly as before — the mirror is
invisible to it — while whatever it types and whatever the guest prints also crosses the socket to
you. Type at your `telnet` and the guest reads it as if you had reached over and used the keyboard.
Add `?ro` to watch without being able to type — quote it (`--mirror 'socket:2323?ro'`), since
`?` is a shell wildcard and an unquoted `socket:2323?ro` makes the shell fail with `no matches
found`. One watcher at a time.

The guest only advances **during a `run`**, so a character you type between the assistant's runs
waits on the line and is read on its next `run` — the same as staging input with `send`. While a
`run` is in flight you and the assistant share the console live. (This is the same `|socket:PORT`
mirror the monitor's `CONNECT` offers on any line; the *Serial lines* chapter of the User Manual
covers it in full.)

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

**`ASM.COM`'s own symbol rules fail *quietly*** — these are Digital Research's, not the
simulator's, but you meet them the instant you assemble period source, and each one assembles a
wrong byte instead of an error you can see:

- A symbol name is **letters, digits, `?` and `@` only — no underscore.** An underscore throws
  an `S` error, the symbol resolves to `0`, and every later use silently assembles `00`.
- A **reserved mnemonic cannot be a symbol.** `JMP EQU 0C3H` is read as a `JMP` *instruction*,
  so the name is never defined.
- `ASM.COM` is **case-insensitive**, so a label equal to an `EQU` under case folding collides:
  `fujiRd` and `FUJIRD EQU 52H` are one name (a phase error `P`, and `MVI A,FUJIRD` assembles
  the wrong byte).

When a constant matters, read the `.PRN` back and check the object bytes — `00` where a value
belongs is the tell that a symbol went unresolved or was defined twice.

**Work on a copy of the disk.** CP/M writes to the mounted image; the `.dsk` files are not
redistributable and there is no undo — copy the machine directory first if you are about to
write in anger. This is a **track-buffered** BIOS: it holds the current track in RAM and
commits it when CP/M changes track or warm-boots. Getting back to a live `A>` prompt is a warm
boot, so end every session at `A>` before you unmount or snapshot, or the last write is lost.

**Two flushes stand between a guest write and the bytes on your host disk.** The BIOS commits
its track buffer on console input or a warm boot; altairsim commits the host `.dsk` on
**`UNMOUNT`** (or `QUIT`). So to read a freshly written image back on the host, first get to the
`A>` prompt *and* `UNMOUNT` — and release the file from any other altairsim still holding it, or
a stale write clobbers what you just made.

## From a bare disk image to a booting machine

The recipe above starts from a machine that already has its disk wired in. When all you have
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
| Step **over** a `CALL`/`RST` | `NEXT` (`N`) | Runs the callee at full speed and stops the instant it returns — so you stay in the code you are reading instead of touring a print routine. On anything else it is a single step. |
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
| Trace a region as it runs | `TRACE ON [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]` | Logs every matching cycle. A **tracepoint** — `BREAK <addr> TRACE ON` and `BREAK <addr> TRACE OFF` — flips tracing on entering a subroutine and off leaving it, without ever stopping the machine. |
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

**A live transfer holds `run` open.** During a streaming read `run` gets a wall-clock grace
window: it keeps going while bytes are still crossing and returns only when the wire quiets or
`until` matches — it will **not** cut a transfer at `timeout_ms`. (On older builds a long read
could return `stopped: "idle"` mid-transfer; if you see that, resume with `run` and no new `from`
and watch a destination pointer climb via `regs`/`mem_dump` until it completes.)

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
- **Disk units are `driveN`, and `.imd` converts on MOUNT** — `MOUNT dsk0:0 …` is rejected;
  the unit is `drive0`. `SHOW MOUNTS` spells them out. A `.imd` becomes a raw `.dsk` on
  interactive `MOUNT` only, not through a TOML `mount =` line (see the bare-image section).
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
is the operator's crib for driving it all through MCP, and **`cheatsheet.md`** (also beside this
file) is the at-a-glance list of every option and command when you just need the syntax.
