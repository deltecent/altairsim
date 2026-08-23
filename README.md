# altairsim

A C++ simulator of the **MITS Altair 8800** and the **S-100 bus**.

`altairsim` is a **hardware development bench** that happens to run period software. The S-100 bus is a first-class modeled object rather than an implementation detail, because the point is to develop **new hardware** as well as to run old software.

It boots Altair 4K and 8K BASIC (and the 1975 8080 BASIC 1.0) off cassettes, MITS Programming System II (polled *and* interrupt-driven), CP/M 2.2 off 8″ and 5¼″ floppies, CP/M 3 off CompactFlash and SD cards, Cromemco CDOS, SD Systems SDOS, iCOM FDOS, and the Altair 680b's MON680 monitor — every one a real period artifact, running unmodified.

Every one of those boots is an **acceptance test**: it runs the period software on the whole machine through the real CLI and checks what lands on the terminal. There are more than forty of them. Three CP/M images are tracked in git — one 8″ floppy and the minidisk's two — so a fresh clone boots CP/M and runs those tests without downloading anything first. The eight examples that ship live in `examples/`, one directory each, and `acceptance-examples` boots them from a scratch directory with no repository in sight. The larger images that no test needs are fetched by `tools/fetch-disk-images.sh`.

```
$ altairsim basic4k
altairsim> MOUNT acr0:tape "examples/basic/4K BASIC Ver 3-1.tap"
acr0:tape: mounted examples/basic/4K BASIC Ver 3-1.tap
altairsim> LOAD "examples/basic/LDR4K31.HEX"
loaded 20 bytes from examples/basic/LDR4K31.HEX (0000-0013)
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

**There are no dependencies.** A C++20 compiler and CMake ≥ 3.20 is the entire list. The TOML parser, the JSON encoder, the terminal emulator and the line editor are all in-tree, so a fresh clone builds with nothing to download.

**SDL3 is the one exception, and it is detected rather than required.** Install it and the video boards (`vdm1`, `sol`, `dazzler`, `vdb8024`) open a real window — as does the built-in `terminal:` VT100; leave it out and they build headless against a null display, every test still passes, and the build never asks you for anything. `-DALTAIRSIM_ENABLE_SDL=OFF` forces headless even where SDL3 is present.

```sh
git clone https://github.com/deltecent/altairsim.git
cd altairsim
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -LE slow      # drop -LE slow for the full CPU exercisers
./build/altairsim                    # the default machine
```

**Or just run the one-command build.** `./build.sh` (`build.bat` on Windows) configures a
Release build, builds it, and prints where the binary landed and what version it is — no
flags, no generator to choose, and a plain sentence to act on if CMake is missing. SDL3
stays optional; add `--with-sdl` to link a private static SDL3 and get a window.

**Built and run on Linux, macOS, and Windows.** The code is written to be portable — C++20, no dependencies, and every OS difference confined to `src/platform/` behind a header with zero conditionals — and all three platforms are proven: Linux (Ubuntu/GCC), macOS (a universal `x86_64`+`arm64` binary, so Intel and Apple Silicon both), and Windows on MSVC. See [`docs/building-linux.md`](docs/building-linux.md), [`docs/building-windows.md`](docs/building-windows.md), and [`docs/porting-notes.md`](docs/porting-notes.md).

> **Pre-built packages for every platform.** The downloadable v0.4.0 archives cover macOS
> (Apple Silicon and Intel), Linux `x86_64`, and Windows `x86_64` — each built natively on its
> own platform and self-contained: SDL3 is linked statically, and on Windows so is the C
> runtime, so there is nothing to install and no redistributable to chase. Prefer to build it
> yourself? See [`docs/building-windows.md`](docs/building-windows.md) and its siblings.

**CI runs the suite on every push.** GitHub Actions builds `altairsim` and runs the tests on all three platforms — Linux, macOS, and Windows are each a required check, each configured with `-Werror`/`/WX` so a warning on any toolchain reds the PR. The tests below are the same ones; you can run them yourself with `ctest`.

## What is in the box

S-100 board types — most modeled from their own manuals, a few of our own design. Every one
is added with `BOARDS ADD <type> <id>` or a `[[board]]` in a machine file, and `SHOW BOARDS`
in the monitor prints this list with a one-line description of each (`SHOW BOARD <type>` for one).

**CPUs** — each decodes nothing; it *drives* the bus.

| Board | What it is |
|---|---|
| `8080` | MITS 88-CPU — an 8080A at 2 MHz. |
| `8085` | 8085 core — the 88-CPU's twin, with `RIM`/`SIM` and `TRAP`/`RST 5.5`/`6.5`/`7.5`. |
| `z80` | Zilog Z80 card — the same bus, a different ISA. ZEXALL-validated. |
| `6800` | Altair 680b CPU — a Motorola 6800 at 500 KHz, with memory-mapped I/O. |

**Memory**

| Board | What it is |
|---|---|
| `memory` | RAM/ROM card — a list of regions and `PHANTOM*`. Plain, unbanked. |
| `bankmem` | S-100 bank-switched RAM — one card, four decoders (Vector, Cromemco 64KZ, North Star, ExpandoRAM II). |
| `v2z80rom` | S100Computers V2 Z80 CPU board — its onboard paged MASTER monitor EEPROM at `F000`–`FFFF`. ROM only; pair it with a `z80` CPU and a RAM board. |

**Serial, console and modem**

| Board | What it is |
|---|---|
| `2sio` | MITS 88-2SIO — two 6850 ACIAs. |
| `sio` | MITS 88-SIO — one COM2502 UART. **Inverted** status bits. |
| `uio` | MITS 88-UIO — a 6850 serial port and an 88-ACR cassette section on one card. |
| `sbc` | SD Systems SBC-100/200 — a Z80 SBC: 8251 console, Z80-CTC, parallel port, boot PROM. |
| `pmmi` | PMMI MM-103 — a Bell 103 modem on an S-100 card. |
| `io2` | SSM IO-2 serial — a strap-configurable serial card (SIO Rev 0 default, plus TU-ART, IMSAI SIO2, CompuPro IF2/SS1 profiles). |
| `propio` | S100Computers Console IO — a Parallax-Propeller console, an `io2` subtype. |

**Storage — floppy, hard disk, CompactFlash and SD**

| Board | What it is |
|---|---|
| `dcdd` | MITS 88-DCDD — 8″ hard-sector floppy, up to 16 drives. |
| `mds` | MITS 88-MDS — 5¼″ minidisk. The DCDD's registers, different physics. |
| `icom` | iCOM FD3712/3812 — 8″ floppy with a boot PROM; boots CP/M 2.2 and FDOS. |
| `versafloppy` | SD Systems VersaFloppy I/II — WD FD177x soft-sector; boots SDOS. |
| `tarbell` / `tarbelldd` | Tarbell #1011 (SD) / #2022 (DD) — auto-boots CP/M the moment a disk is in it. |
| `16fdc` / `64fdc` | Cromemco FDC — WD FD1793, a TMS 5501 console and an RDOS boot PROM; boots CDOS. |
| `hdsk` | MITS 88-HDSK Datakeeper — a Pertec hard disk, 256-byte sectors from a linear image. |
| `dualide` | S100Computers IDE-AB — two CompactFlash sockets (A:/B:) for CP/M 3. |
| `dualsd` | S100Computers Dual SD — two microSD sockets for CP/M 3. |

**Cassette** — `acr` (88-ACR, an 88-SIO B plus an FSK modem); `sol` and `uio` also carry tape.

**Video and graphics** — need a `Display`.

| Board | What it is |
|---|---|
| `vdm1` | Processor Technology VDM-1 — memory-mapped 16×64 video. |
| `dazzler` | Cromemco Dazzler — color graphics from a framebuffer in main RAM. |
| `vdb8024` | SD Systems VDB-8024 — an 80×24 video terminal on one board. |
| `sol` | Processor Technology Sol-PC — serial, keyboard, parallel and CUTS tape as one card. |

**Printer and parallel**

| Board | What it is |
|---|---|
| `c700` | MITS 88-C700 — Centronics printer controller. Output-only; `CONNECT` it. |
| `lpc` | MITS 88-LPC — line-buffered line-printer controller. |
| `pio` / `4pio` | MITS 88-PIO / 88-4PIO — 8-bit parallel ports. |
| `d7a` | Cromemco D+7A — analog + parallel I/O; reads host joysticks. |

**680b onboard I/O** — `680io` (6850 console), `680uio` (a second 6850 plus a 6820 PIA), `680kcacr` (Kansas City Standard cassette).

**Interrupts, clock and control**

| Board | What it is |
|---|---|
| `fp` | The front panel — SENSE switches at `IN 0FFH`, and the lamps. |
| `turnkey` | MITS 8800b Turnkey Module — a phantom boot PROM, an integrated 6850, and sense switches. |
| `virtc` | MITS 88-VI/RTC — vectored interrupts (VI0–VI7 → `RST n`) and a real-time clock. |
| `ss1` | CompuPro System Support 1 — a multifunction card: MSM5832 clock/calendar, a 2651 UART, an 8253 interval timer, and dual 8259A interrupt controllers in a master/slave cascade. |
| `hostbridge` | Guest ↔ host file transfer, sandboxed. **Ours, not a period card** — `R`/`W`/`HDIR`. |

**More than thirty ready-built machines** are compiled into the binary; `altairsim --list`
names them all. The Altairs proper — `default`, `original` (as it left Albuquerque),
`altmon`, `amon`, `acuter`, `cuter`, `turnkey`, `rombasic`. The BASIC and PS2 benches —
`basic4k`, `basic8k`, `ps2`, `ps2int`. The disk machines — `minidisk`, `tarbell`,
`tarbelldd`, `icom`. Other CPUs and other makers — `z80`, `8085`, `altair680`, `sbc200`,
`sbc200v`, `dualsd`, `dualide`, `dualidesd`. And the peripheral demos — `vdm1`, `dazzler`,
`sol20`, `lineprinter`, `parallel`, `bankmem`, `compupro`.

**All three Intel cores are exerciser-validated.** The 8080 passes TST8080, 8080PRE, CPUTEST and 8080EXM — all 25 CRC groups of the exerciser; the 8085 passes its own 8085EXM; the Z80 passes ZEXDOC and ZEXALL. Each core passed its gate *before* a single board was built on top of it. They are `ctest` targets, and they run in CI.

## The monitor and debugger

A SIMH/AltairZ80-style command monitor with line editing and history, and a full symbolic
debugger sharing the same prompt. **ATTN (`^E`) is the stop key** — never `^C`, because `^C`
belongs to the guest (CP/M reads it), and a stop key the guest also wants is one the guest
eats. `HELP` lists every command; it comes off the same table the monitor resolves against,
so it cannot drift from what the binary does.

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

**Building and inspecting the machine.** `BOARDS`/`BOARDS ADD`/`BOARDS REMOVE` build it live;
`SHOW`, `SET`, `MOUNT`, `CONNECT` configure it; `CONFIG SAVE`/`CONFIG LOAD` write and reload a
whole machine as a TOML file. `EXAMINE`/`DEPOSIT`, `DUMP`, `FILL`, `MOVE`, `SEARCH` and
`COMPARE` work memory directly; `LOAD` reads Intel HEX.

**Debugging.** `REGS` shows the CPU; `STEP` and `NEXT` single-step (over calls); `DISASM`
disassembles, annotated with symbols. `BREAK <addr>` sets a breakpoint and `BREAK <addr> IF
<expr>` makes it conditional; tracepoints fire an action instead of stopping. `TRACE` records
every bus cycle, and `HISTORY` is a flight recorder you read back after the fact — so a crash
is a thing you rewind into, not a thing you try to reproduce. `SYMBOLS LOAD` pulls names from
a `.PRN` or `.SYM` listing so addresses read as labels everywhere. `SNAPSHOT` and `RESTORE`
freeze and thaw the entire machine to a file. `SET CONSOLE DEBUG` turns on a per-facility
trace log. [`docs/debugger/`](docs/debugger/) walks through all of it.

**An MCP server is built in** (`altairsim --mcp`), so Claude can drive the machine through
typed, structured tools instead of screen-scraping a text CLI. It runs on the *same*
`Machine` object as the monitor — not a wrapper, not a second model of the world. See
[`docs/manual/mcp.md`](docs/manual/mcp.md) and [`docs/DRIVING-WITH-AI.md`](docs/DRIVING-WITH-AI.md).

## Consoles and connections

**Any board that moves characters** can be connected to the console, a TCP socket, a real host serial port, or a **built-in terminal window** the simulator draws itself — VT100, VT52 or H19, via a `terminal:` endpoint, no telnet client and no external emulator. They are interchangeable: the same board reaches any of them.

The modem-control tests are run against a **real null-modem cable** between two USB serial ports, because a claim about a cable deserves a cable. They are opt-in (`ctest -L hw`, pointed at your ports with `ALTAIR_SERIAL_A`/`_B`) and they **skip loudly** when the hardware is absent — a hardware test that quietly passes with no hardware is a green tick that means nothing.

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

**`-L hw` is opt-in and needs actual hardware.** Point it at two serial ports with a
null-modem cable between them via `ALTAIR_SERIAL_A` / `ALTAIR_SERIAL_B`; with the env-vars
unset it skips loudly (exit 77, reported `Skipped`) rather than passing on nothing. The
`serial-hw` case drives the modem-control pins, so the cable must be a *full* null modem —
DTR on each end crossed to **both** DSR and DCD on the other, not a three-wire
TxD/RxD/GND lash-up. On macOS, name the **`/dev/cu.*`** device, not `/dev/tty.*`: `cu`
opens for outbound use without waiting on carrier, which is what a loopback that drives its
own DTR/RTS wants. `docs/building-linux.md` and `docs/building-windows.md` carry the
per-platform detail (finding your ports, the full pinout).

The acceptance tests are not unit tests: they boot period software on the whole machine through the real CLI, and several ship with a **negative control** — the same script against a machine that should *fail*, marked `WILL_FAIL`. If a control ever passes, the test it guards was passing for the wrong reason and is worthless. That is the only reason to believe any of them.

The disk-image tests run on a fresh clone: the 88-MDS and 8″ 88-DCDD images they need are **tracked**. What is *not* tracked is the 8 MB image, which only `acceptance-hostbridge`'s by-hand `build` mode wants — see `CMakeLists.txt` and `tools/fetch-disk-images.sh`. Provenance for both tracked sets is in `disks/mits-88dcdd/cpm22/buffered/README.md` and `disks/mits-88mds/cpm22/README.md`.

## Documentation

**Start with the manual if you want to *run* it, and with `DESIGN.md` if you want to *change* it.**

| Document | What it covers |
|---|---|
| [`docs/manual/`](docs/manual/) | **The User Manual** — boot CP/M, drive the monitor, debug a guest, mount disks and tapes, move files. Written for someone holding a release package and nothing else, so it cites no source file and no repository path. Builds to `altairsim-manual.pdf`, which is what ships. |
| [`docs/devguide/`](docs/devguide/) | **The Developer Guide** — Theory of Operation, and a worked example that adds a new board at port `FFH`. Needs the source, so it does not ship. |
| [`DESIGN.md`](DESIGN.md) | The design, and the reasoning. Read this first. |
| [`DISTRIBUTION.md`](DISTRIBUTION.md) | How a release is built and where it goes — the four packages, the machine each is built on, and the checks that must pass before one ships. Written to be followed step by step on a build machine that has never seen this repository. |
| [`docs/config.md`](docs/config.md) | *Why* the TOML format is shaped as it is, by annotated example. **Not the grammar** — that is the manual's, so there is one normative copy of it. |
| [`docs/cli-commands.md`](docs/cli-commands.md) | Why the monitor's commands rank and abbreviate as they do. **Not a command reference** — `HELP` is, and it comes off the same table the monitor resolves against. |
| [`docs/boards/`](docs/boards/) | One file per board: the real hardware, the register map, how it is simulated, and the quirks it reproduces. |
| [`docs/DRIVING-WITH-AI.md`](docs/DRIVING-WITH-AI.md) | Driving a running guest with an AI assistant over the built-in MCP server. |
| [`docs/sources.md`](docs/sources.md) | Where every hardware fact came from. |
| [`docs/roadmap.md`](docs/roadmap.md) | Milestones and acceptance criteria. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | Hard-won lessons from the prior Python prototype. |
| [`docs/building-linux.md`](docs/building-linux.md), [`docs/building-windows.md`](docs/building-windows.md) | Building and running per platform — prerequisites, the serial-build memory trap, and what was verified. |

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
