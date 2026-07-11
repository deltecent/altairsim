# altairsim

A C++ simulator of the **MITS Altair 8800** and the **S-100 bus**, for Intel Mac, Apple Silicon Mac, Linux, and Windows.

`altairsim` is a **hardware development bench** that happens to run period software. The S-100 bus is a first-class modeled object rather than an implementation detail, because the point is to develop **new hardware** as well as software.

- **No front panel.** The interface is a SIMH/AltairZ80-style command monitor, with history, full line editing, and tab completion.
- **An MCP server** is built in, so Claude can drive the machine through structured, typed tools rather than by screen-scraping a text CLI.
- **Boards are the unit of everything.** The CPU is a board. Memory is a board. Multiple instances of any board can coexist, each independently configured — two 88-2SIO cards and two 88-SIO cards in the same machine, if you want.
- **Boards that move characters** can be connected to the console, a TCP socket, or a real host serial port, interchangeably.

## Status

**Design complete; implementation not started.**

Start with **[`DESIGN.md`](DESIGN.md)**.

## Documentation

| Document | What it covers |
|---|---|
| [`DESIGN.md`](DESIGN.md) | The design. Read this first. |
| [`docs/roadmap.md`](docs/roadmap.md) | Milestones and acceptance criteria. Milestone 1 is CLI + 8080 + RAM + 88-2SIO, and nothing else. |
| [`docs/config.md`](docs/config.md) | The TOML machine-configuration schema, with worked examples. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | **Read before writing the CPU or disk controller.** Hard-won lessons from the prior Python prototype. |
| [`docs/boards/`](docs/boards/) | One file per board: the real hardware, the register map, how it is simulated, its limitations, and the quirks it reproduces. |

## The two ideas the design turns on

**1. Boards respond to bus cycles; the CPU originates them.**

That distinction gives you DMA for free. S-100 has `pHOLD`/`pHLDA` precisely because a backplane can have more than one bus master — a disk controller or a Dazzler takes the bus away from the processor. So a DMA card is simply a board that *becomes* a bus master when granted the bus, using the same interface the CPU already uses. DMA is not a special path bolted onto the bus.

**2. The bus carries signals; it does not invent behavior.**

The bus does not arbitrate vectored interrupts and hand the CPU a vector — that is what an **88-VI board** does. The bus carries `pINT`, carries VI0–VI7, and runs an `IntAck` cycle that boards can claim. Everything else is a board.

Model that honestly and the un-vectored case falls out for free: a board pulls `pINT`, nobody drives the data bus during `IntAck`, the bus floats high, the CPU reads `0xFF` and executes `RST 7`. Which is exactly what a real Altair does — and exactly why the PMMI's factory jumper straight to pin 73 gives you RST 7 with no vector logic anywhere.

The payoff is that the 88-VI has no special privileges. Neither does any *new* interrupt controller you invent. That is the whole point.

## Building

Not yet. CMake ≥ 3.20, C++20, with presets for macOS (universal), Linux, and Windows.

Dependencies (all via `FetchContent`, pinned): a TOML parser, a JSON library, **replxx** (line editing), and **SDL** (graphics/sound — *optional*; the simulator builds and all tests pass without it, so CI runs headless).

## A rule worth knowing up front

**No `#ifdef`s for operating-system differences.** SIMH is riddled with them, and the result is code you cannot read without mentally executing the preprocessor.

OS differences live in an interface header with *zero* conditionals plus one implementation file per OS, selected by CMake. A CI lint greps for `_WIN32`, `__APPLE__`, `__linux__`, `_MSC_VER` and friends anywhere outside `src/platform/` and **fails the build**. Without the lint the rule decays within a month.

See [`DESIGN.md` §2.1](DESIGN.md).
