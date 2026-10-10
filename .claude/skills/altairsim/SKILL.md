---
name: altairsim
description: Drive the altairsim MITS Altair 8800 / S-100 simulator over its built-in MCP server — boot a machine, type at a guest's console and read what it prints, single-step and debug a program, or talk to a real serial device. Use this whenever a task involves altairsim, an Altair 8800, CP/M 2.2, 8080/Z80 assembly on a period machine, or a `.toml` machine file — and whenever you would otherwise reach for expect or a pty to poke a running guest.
---

# Driving altairsim

`altairsim <machine> --mcp` speaks line-delimited JSON-RPC 2.0 on stdio. If it is registered
as an MCP server, its tools are already in your list; otherwise start it yourself and send
`initialize`, `notifications/initialized`, then `tools/call`.

A registered server can run a different program from `altairsim` on `PATH`: a server
registered as `./altairsim` runs the copy in that folder, which can be a different release.
When you start the program yourself, use the command that `claude mcp get <name>` shows.

**`--mcp` does not run the machine file's `startup`.** The machine is loaded — disks mounted,
boards fitted — but parked, so nothing blocks before you have control. You boot it.

## The loop

One `run` per guest command, matching the prompt each time:

```
run {from: 65280, until: "A>"}                  # boot CP/M via the DBL PROM at FF00 (65280)
run {input: "DIR\r", until: "A>"}               # a command, and what it printed back
run {input: "ASM FOO\r", until: "A>", timeout_ms: 120000}
```

- `run` never blocks. It returns on `until`, on the guest going idle at a prompt, on
  `timeout_ms`, `max_steps`, HLT, a breakpoint, a port no board decodes under
  `SET BUS UNCLAIMED=HALT` (`unclaimed`), a `BREAK TAPE STOP` (`tape-stop`), or a cancel of
  the request or a ^C to the process (`interrupted`) — read `stopped` to learn which. Bus
  warnings from the run come back in `warnings`.
- **Numbers are JSON numbers.** `"from": "0xFF00"` is refused with the value to send
  (`65280`); it is never read as 0.
- **`timeout_ms` is a ceiling, not a wait.** A 50-second assembly under `timeout_ms: 120000`
  returns in 50 seconds. Set the worst case you will sit through and let `until` end the call.
- **Never pick an `until` that recurs.** A disk that auto-runs `PROFILE.SUB` reprints `A>`
  several times, and input sent while a SUB is running is swallowed. Match something unique
  to the state you want.
- **Make `until` the full prompt.** `run` stops at the first place `until` occurs. The PS II
  assembler prompt is `*ASM*`: `until: "*"` returns after its first `*`, before the remaining
  characters. Use `until: "*ASM*"`.
- **Control bytes are JSON escapes:** `\u0003` for ^C, `\u001a` for ^Z, `\u001b` for ESC.
  `\x03` is not JSON — it reaches the guest as the three characters `x03`, which is why a
  control byte can seem to vanish while printable text gets through.
- **If your client sends the text as written** — a `\r` arrives as the two characters `\`
  and `r` — type with `monitor {command: 'TYPE "DIR\r"'}` and then a bare `run`. `TYPE`
  decodes `\r`, `\"`, `\^C` (Ctrl-C; `\^Z`, `\^[` for ESC) and `\xHH` itself.

`send {text}` types without running, `recv {}` drains output without running, `regs {}` reads
the CPU. `monitor {command}` runs any one monitor command (`MOUNT`, `SET`, `DISASM`, `IN`,
`OUT`, `CONNECT`, …) and returns its text — the escape hatch for anything without a tool.

**When the guest must keep running between calls** — a server on the guest, two machines that
talk to each other, a person taking over through `--mirror` — use `start {from?, input?}`. It
returns at once and the guest runs until `stop {}`, a HLT or a breakpoint. Every other tool
still works meanwhile; `recv` collects the output, `status` says `running` or why it stopped.
`run` and `step` are refused until you `stop`.

**For a person who wants to watch**, start the server with `--log FILE`: it writes what the
guest prints to `FILE` as plain text. It is an ordinary file — they can `cat` it, open it, or
follow it live, for example with `tail -f FILE` (PowerShell: `Get-Content -Wait FILE`). It is
watch-only; `--mirror` is for a person who must type.

**For a person who wants to type**, start the server with `--mirror`. Three sinks:

- `--mirror socket:PORT` — every platform. The person runs `nc localhost PORT` (or `telnet`).
- `--mirror pty` — macOS and Linux only. The simulator prints
  `altairsim: --mirror: open /tmp/altairsim0 (/dev/ttys004)` on stderr; the person opens that
  name with a terminal program, for example `screen /tmp/altairsim0`. A terminal program sends
  each key as it is typed and does not echo it, so prefer this sink where it exists.
  `--mirror pty:PATH` picks the name. Windows refuses it (exit 2): use `socket:PORT` there.

- `--mirror serial:DEVICE` — every platform (`COM3` on Windows). For a person with a hardware
  terminal on a serial port. 8N1 at 9600 baud, no flow control; `?baud=N` picks another rate.
  The simulator cannot tell when a terminal is there, and it never makes the guest wait.

`?ro` makes any sink watch-only; quote it (`--mirror 'pty?ro'`). Tell the person the name or
the port — they cannot see the server's stderr. Their keys reach the guest only while it runs,
so `start` the guest before you hand the console over.

## Do not hand-roll a pty

Driving a guest with `expect` or a bare pty fights console pacing and recurring prompts. Use
`--mcp`. That is what it is for.

## Ask, don't guess

- `tools/list` is authoritative for the tool surface; `board_types` reports what a board can
  be told, straight off the board itself.
- `monitor {command: "HELP"}` lists every monitor command; `HELP <cmd>` gives one in detail.
- `cheatsheet.md`, beside this skill, is the whole command surface as plain text — generated
  from the program, so it matches the binary you have.

## Rules for a guest program

This skill is about the simulator. The rules of a program that runs in the guest are in
separate skills, beside this one. Load the skill for the program that the task uses:

- `altairsim-cpm-build` — build a CP/M program: `ASM` and `LOAD`, or `M80` and `L80`.
- `altairsim-hostbridge` — move a file into or out of the guest: `R`, `W`, `HDIR`.
- `altairsim-cpm-text` — make or edit a text file for CP/M: CR/LF line ends, the 8.3 name.
- `altairsim-mbasic` — type at MBASIC, or enter a BASIC program.

## The full briefing

`references/driving-with-ai.md` is the long form and the place to look before anything past a
boot-and-type: registering the server with a client, building a machine from a bare disk
image, the debugger commands worth reaching for, attaching a real serial port, and the
gotchas that cost an hour each — a BIOS that trashes registers, card base versus channel
register. Read it when the task goes past typing at a prompt.

(In a clone of the repository rather than a release package, that file is
`docs/DRIVING-WITH-AI.md` and the cheatsheet is `docs/manual/ref/cheatsheet.md`.)
