# altairsim — User Manual

A simulator of the **MITS Altair 8800** and the **S-100 bus**.

This is the manual that ships in the package. It describes the program you have, the disks and
tapes you have, and nothing else.

## Getting started

| | |
|---|---|
| [What altairsim is](introduction.md) | What it does, and what it does not do. |
| [What is in the package](package.md) | The binary, this manual, the disks and tapes. |
| [Running it](running.md) | Unzip and go. |
| [**Quick start**](quick-start.md) | **CP/M in one command.** Get out with `^E`, back in with `RUN`, out with `QUIT`. |

## Quick reference

| | |
|---|---|
| [Quick reference](ref/cheatsheet.md) | One page: the command line, every command, the machine-file skeleton, the boards. |

## Driving the machine

| | |
|---|---|
| [The monitor](monitor.md) | The `altairsim>` prompt: prefix commands, the number rule, naming a card, ATTN. |
| [Debugging](debugging.md) | Breakpoints, stepping, disassembly, and looking at the bus itself. |
| [Machines](machines.md) | The command line, the built-in machines, and where a path is relative to. |
| [Machine files](configuring.md) | The TOML format, in full. |
| [Boards](boards.md) | What each card is, and what it is for. |

## Using it

| | |
|---|---|
| [Disks](disks.md) | `MOUNT`, drive geometry, and the track-buffer trap. |
| [Tapes](tapes.md) | The cassette interface, and loading BASIC the way MITS meant you to. |
| [Serial, sockets and telnet](serial.md) | Wiring a card to your terminal, a TCP port, or a real UART. |
| [Moving files in and out](file-transfer.md) | `HDIR`, `R` and `W` at the CP/M prompt. |
| [Worked examples](examples.md) | Two complete sessions, start to finish. |
| [Driving it from an AI assistant](mcp.md) | The MCP server. |

## When it goes wrong

| | |
|---|---|
| [Troubleshooting](troubleshooting.md) | The things that catch everybody. |
| [Glossary](glossary.md) | S-100, PHANTOM\*, hard-sector, BDOS, and the rest. |

## Reference

**Generated from the program itself** — every default, range and help string below is printed
from the same table the monitor resolves against, so it cannot disagree with what you have.

| | |
|---|---|
| [Every monitor command](ref/commands.md) | All of them, with usage and examples. |
| [Boards and their parameters](ref/boards.md) | Every card, every key, every default. |
| [The built-in machines](ref/machines.md) | |

---

*Want to build a board of your own? That needs the source, and a different document — the
**Developer Guide**.*
