# altairsim — Developer Guide

**For someone with the source.** This document explains how the machine works inside, and how
to add hardware to it.

It is deliberately a *different document* from the **User Manual** (`docs/manual/`). The
manual ships inside the distribution package — a zip with the binary, a PDF, and some disks
and tapes — so it may not reference a single file the reader does not have: no `src/`, no
CMake, no `DESIGN.md`. This guide is under no such constraint, because you cannot write a
board without the source in front of you.

| Chapter | |
|---|---|
| [Building it](building.md) | Build, test, and the doc targets. |
| [Theory of operation](theory.md) | The bus, boards, memory, I/O, interrupts and reset. |
| [Writing a board](adding-a-board.md) | A card at port FFh, end to end — and it compiles. |

## Where the rest of it lives

| | |
|---|---|
| [`DESIGN.md`](../../DESIGN.md) | The design, **and the reasoning** — including the arguments we lost and reversed. Read it before changing anything load-bearing. |
| [`docs/boards/`](../boards/) | One file per board: the real hardware, the register map, the quirks it reproduces, and — the load-bearing part — **what is not modelled**. |
| [`docs/sources.md`](../sources.md) | Where every hardware fact came from. Period manuals and datasheets, never another emulator's source. |
| [`docs/config.md`](../config.md) | Why the machine-file format is shaped the way it is. (The format *itself* is specified in the User Manual.) |
| [`docs/cli-commands.md`](../cli-commands.md) | Why the monitor's commands rank and abbreviate as they do. |

## Two rules that will save you a day

**Never invent a hardware feature to fix a software symptom.** MITS BASIC sets bit 7 of the
last character of every message, and a modern terminal prints garbage. The fix is *not*
`data_bits = 7` on the card — the real card sent all eight bits and the *Teletype* ignored the
eighth. Mask it in the UART and you fix BASIC's prompt and silently corrupt XMODEM through the
same port. Look in the host, the filter and the monitor before you touch the hardware.

**Every board ships with its `.md`.** The board and its documentation are one deliverable, and
`docs/boards/_TEMPLATE.md` is the form. The **Limitations** and **Quirks** sections are the
ones that matter: they force the honest question *"what did I not actually implement?"*, which
is precisely the question a simulator author most wants to avoid.
