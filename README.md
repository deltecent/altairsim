# altairsim

A C++ simulator of the **MITS Altair 8800** and the **S-100 bus**.

`altairsim` is a **hardware development bench** that happens to run period software. The S-100 bus is a first-class modeled object rather than an implementation detail, because the point is to develop **new hardware** as well as to run old software.

It boots Altair 4K and 8K BASIC off a cassette, MITS Programming System II (polled *and* interrupt-driven), and CP/M 2.2 off both an 8″ floppy and a 5¼″ minidisk — every one of them a real period artifact, running unmodified.

The BASIC, PS2 and minidisk boots are **acceptance tests**: they run the period software on the whole machine through the real CLI and check what lands on the terminal. CP/M on the 8″ 88-DCDD boots and writes, but is not yet automated — the disk images are not ours to redistribute, so they are not in this repository.

```
$ altairsim basic4k
altairsim> MOUNT acr0:tape "tapes/4KBasic31/4K BASIC Ver 3-1.tap"
acr0:tape: mounted tapes/4KBasic31/4K BASIC Ver 3-1.tap
altairsim> LOAD "tapes/4KBasic31/LDR4K31.HEX"
loaded 20 bytes from tapes/4KBasic31/LDR4K31.HEX (0000-0013)
altairsim> RUN 0
[console -- ^E returns to the monitor]

MEMORY SIZE?
TERMINAL WIDTH?
WANT SIN? Y

742 BYTES FREE

ALTAIR BASIC VERSION 3.1
[FOUR-K VERSION]
OK
```

That is the whole of it: put the tape in, toggle in the bootstrap MITS printed in the manual, and run it from zero. Nothing is faked — the bytes come off a `.tap` image through a modeled 88-ACR.

## Building

**There are no dependencies.** A C++20 compiler and CMake ≥ 3.20 is the entire list. The TOML parser, the JSON encoder and the line editor are all in-tree, so a fresh clone builds with nothing to download.

```sh
git clone https://github.com/deltecent/altairsim.git
cd altairsim
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -LE slow      # drop -LE slow for the full 8080 exerciser
./build/altairsim                    # the default machine
```

**Built and run on Linux, macOS, and Windows.** The code is written to be portable — C++20, no dependencies, and every OS difference confined to `src/platform/` behind a header with zero conditionals — and all three platforms are now proven: Linux (Ubuntu/GCC), macOS (a universal `x86_64`+`arm64` binary, so Intel and Apple Silicon both), and Windows on MSVC. See [`docs/building-linux.md`](docs/building-linux.md), [`docs/building-windows.md`](docs/building-windows.md), and [`docs/porting-notes.md`](docs/porting-notes.md).

**CI runs the suite on every push.** GitHub Actions builds `altairsim` and runs the tests on all three platforms — Linux, macOS, and Windows are each a required check. The tests below are the same ones; you can run them yourself with `ctest`.

## What is in the box

Eleven board types — ten modeled from their own manuals, and one of ours — and nine machines
built out of them:

| Board | What it is |
|---|---|
| `8080` | MITS 88-CPU — an 8080A. Decodes nothing; it *drives* the bus. |
| `z80` | Zilog Z80 CPU card — the same bus as the 88-CPU, a different ISA. ZEXALL-validated. |
| `memory` | RAM/ROM card — a list of regions, `PHANTOM*`, and five banking schemes. |
| `2sio` | MITS 88-2SIO — two 6850 ACIAs. |
| `sio` | MITS 88-SIO — one COM2502 UART. **Inverted** status bits. |
| `acr` | MITS 88-ACR — cassette. An 88-SIO B plus an FSK modem. |
| `dcdd` | MITS 88-DCDD — 8″ hard-sector floppy, up to 16 drives. |
| `mds` | MITS 88-MDS — 5¼″ minidisk. The same registers as the DCDD, different physics. |
| `virtc` | MITS 88-VI/RTC — vectored interrupts (VI0–VI7 → `RST n`) and a real-time clock. |
| `fp` | The front panel — SENSE switches at port `FF`, and the lamps. |
| `hostbridge` | Guest ↔ host file transfer, sandboxed. **Ours, not a period card** — `R`/`W`/`HDIR`. |

`altairsim --list` names the machines: `default`, `4k` (the Altair as it actually left Albuquerque), `altmon`, `basic4k`, `basic8k`, `ps2`, `ps2int`, `minidisk`, `z80`.

**Both CPUs are validated.** The 8080 passes TST8080, 8080PRE, CPUTEST and 8080EXM — all 25 CRC groups of the exerciser; the Z80 passes ZEXDOC and ZEXALL. Each core passed its gate *before* a single board was built on top of it. They are `ctest` targets, and they run in CI.

## The interface

A SIMH/AltairZ80-style command monitor with line editing and history: `SHOW`, `SET`, `BOARDS`, `MOUNT`, `CONNECT`, breakpoints (including conditional — `BREAK <addr> IF <expr>`), single-stepping, disassembly, a bus-cycle `TRACE`, and a `HISTORY` flight recorder. **ATTN (`^E`) is the stop key** — never `^C`, because `^C` belongs to the guest (CP/M reads it), and a stop key the guest also wants is one the guest eats.

```
altairsim> BOARDS
  ID    TYPE    I/O       UNITS                       MEMORY
  ----  ------  --------  --------------------------  ------------------------------
  fp0   fp      FF        -                           -
  cpu0  8080    -         1 cpu: 8080                 -
  sio0  2sio    10,12     2 serial: a*, b             -
  dsk0  dcdd    08,09,0A  4 disk: drive0(empty), ...  -
  mem0  memory  -         1 rom: rom0                 0000-DFFF  ram  56K
                                                      FF00-FFFF  rom  dbl  phantom:all

  * holds the console
```

**An MCP server is built in** (`altairsim --mcp`), so Claude can drive the machine through typed, structured tools instead of screen-scraping a text CLI. It runs on the *same* `Machine` object as the monitor — not a wrapper, not a second model of the world.

**Any board that moves characters** can be connected to the console, a TCP socket, or a real host serial port, interchangeably. The modem-control tests are run against a **real null-modem cable** between two USB serial ports, because a claim about a cable deserves a cable. They are opt-in (`ctest -L hw`, pointed at your ports with `ALTAIR_SERIAL_A`/`_B`) and they **skip loudly** when the hardware is absent — a hardware test that quietly passes with no hardware is a green tick that means nothing.

## Configuring a machine

A machine is a TOML file, and **the TOML keys for a board *are* its properties** — there is no separate config schema anywhere, for any board. The loader, `SET`/`SHOW`, `CONFIG SAVE` and the MCP tool schemas all come off one reflection layer, so they cannot drift, and a board added next year is configurable the day it lands.

A built-in machine is one of these files, compiled into the binary. There is one machine language, and the machines we ship are written in it.

```toml
# ./altairsim.toml -- a bare `altairsim` in this directory boots it.
[machine]
name = "myproject"
base = "default"          # start from a machine, and say what is DIFFERENT

[[board]]
id    = "dsk0"            # the default's floppy controller...
mount = "disks/cpm.dsk"   # ...with this project's disk in it
```

`./altairsim.toml` is the one file the simulator *finds* rather than is *given*, and it is found **only when the command line names nothing**. `altairsim basic4k` means `basic4k` in every directory on earth. See [`docs/config.md`](docs/config.md).

## The rules this project actually runs on

**Each chip is modeled from its datasheet; each board from its manual.** A chip built from the one BIOS that happens to drive it implements the subset that BIOS uses and quietly gets the rest wrong. Where the seam between chip and card falls is a *fact about the chip*, not a house style: the 88-SIO's status word is inverted and the 88-2SIO's is not, so a shared UART class with a `bool invert` on it is precisely the bug that `src/chips/` exists to prevent.

**Never invent a hardware feature to fix a software symptom.** MITS BASIC sets bit 7 of the last character of every message. The real card sent all eight bits and the *Teletype* ignored the eighth — so the fix is a transform on the line, not `data_bits = 7` masking inside the UART, which would fix BASIC's prompt and silently corrupt XMODEM.

**Boards respond to bus cycles; the CPU originates them.** That distinction gives you DMA for free: a DMA card is a board that *becomes* a bus master when granted the bus (`pHOLD`/`pHLDA`), using the same interface the CPU already uses. DMA is never a special path bolted onto the bus.

**The bus carries signals; it does not invent behavior.** The bus does not arbitrate vectored interrupts and hand the CPU a vector — that is what an 88-VI board does. Model it honestly and the un-vectored case falls out for free: a board pulls `pINT`, nobody drives the data bus during `IntAck`, the bus floats high, the CPU reads `0xFF` and executes `RST 7`. Which is exactly what a real Altair does, and exactly why the PMMI's factory jumper straight to pin 73 gives you `RST 7` with no vector logic anywhere. The payoff: the 88-VI has no special privileges, and neither does any *new* interrupt controller you invent.

**No `#ifdef`s for operating-system differences.** SIMH is riddled with them and is unreadable as a result. OS differences live in an interface header with *zero* conditionals plus one implementation file per OS, selected by CMake — no OS type ever appears in a signature, so no caller ever needs a conditional to name one. A lint greps for `_WIN32`, `__APPLE__`, `__linux__` and the OS headers anywhere outside `src/platform/`, and it is a **build dependency of the library, not a test** — a rule you can merge and fix later is a rule you have already lost.

**A validation harness may not emulate the thing it is validating.** The CP/M CPU suites run with no CP/M and no console card, through a BDOS stub written in *real 8080 machine code*, reached through the real `JMP` at `0005`, writing to a real port on a real board. Trapping `PC == 0005` in C++ would have been less code and was rejected: it would fake the `CALL`, the `RET`, the stack and the `OUT` inside the one program whose whole job is to decide whether we implement them correctly.

## Tests

```sh
ctest --test-dir build -LE slow     # unit + acceptance
ctest --test-dir build              # ...plus 8080EXM, the full exerciser
ctest --test-dir build -L hw        # modem control, against a real null-modem cable
```

The acceptance tests are not unit tests: they boot period software on the whole machine through the real CLI, and several ship with a **negative control** — the same script against a machine that should *fail*, marked `WILL_FAIL`. If a control ever passes, the test it guards was passing for the wrong reason and is worthless. That is the only reason to believe any of them.

The 88-MDS acceptance tests need a CP/M disk image that is not in this repository; CMake says so at configure time and skips them. See `disks/mits-88mds/cpm22/README.md`.

## Documentation

**Start with the manual if you want to *run* it, and with `DESIGN.md` if you want to *change* it.**

| Document | What it covers |
|---|---|
| [`docs/manual/`](docs/manual/) | **The User Manual** — boot CP/M, drive the monitor, mount disks and tapes, move files. Written for someone holding a release package and nothing else, so it cites no source file and no repository path. Builds to `altairsim-manual.pdf`, which is what ships. |
| [`docs/devguide/`](docs/devguide/) | **The Developer Guide** — Theory of Operation, and a worked example that adds a new board at port `FFH`. Needs the source, so it does not ship. |
| [`DESIGN.md`](DESIGN.md) | The design, and the reasoning. Read this first. |
| [`docs/config.md`](docs/config.md) | *Why* the TOML format is shaped as it is, by annotated example. **Not the grammar** — that is the manual's, so there is one normative copy of it. |
| [`docs/cli-commands.md`](docs/cli-commands.md) | Why the monitor's commands rank and abbreviate as they do. **Not a command reference** — `HELP` is, and it comes off the same table the monitor resolves against. |
| [`docs/boards/`](docs/boards/) | One file per board: the real hardware, the register map, how it is simulated, and the quirks it reproduces. |
| [`docs/sources.md`](docs/sources.md) | Where every hardware fact came from. |
| [`docs/roadmap.md`](docs/roadmap.md) | Milestones and acceptance criteria. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | Hard-won lessons from the prior Python prototype. |
| [`docs/building-linux.md`](docs/building-linux.md) | Building and running on Linux — prerequisites, the serial-build memory trap, and what was verified. |

**Sourcing rule: period manuals and datasheets, never another emulator's source.** Reading past a source to preserve an argument is the same failure as fabricating one.

## License

This project is licensed under the [MIT License](LICENSE) (© 2026 Patrick Linstruth),
**except** for the third-party Z80 instruction exercisers in `tests/cpu/z80/`
(`ZEXDOC.COM`, `ZEXALL.COM`, and their `zexdoc.z80` / `zexall.z80` sources), which
are Frank Cringle's exerciser as extended by J.G. Harston and Patrik Rak and are
distributed under the **GNU General Public License v2.0** — see
[`tests/cpu/z80/LICENSE`](tests/cpu/z80/LICENSE) and
[`tests/cpu/z80/PROVENANCE.md`](tests/cpu/z80/PROVENANCE.md). Those files are test
fixtures the simulator *runs* as guest programs; they are not linked into or derived
from the simulator, and their GPL terms do not extend to the rest of this repository.
