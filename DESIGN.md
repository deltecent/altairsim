# `altairsim` — Design Document

**Status:** design approved, implementation not started.
**Date:** 2026-07-11

---

## 0. Ground rules

### 0.1 Where hardware facts come from

**Period manuals and first-hand artifacts only.** Register maps, bit layouts, and timings are sourced from original vendor documentation (MITS and other manufacturers' manuals), from period software's own equate blocks and disassembled PROMs, or from hardware in hand.

**We do not read other emulators' source code to learn how hardware works.** That includes SIMH / AltairZ80. Second-hand facts inherit second-hand mistakes, and the whole value of this project rests on the hardware model being *right*, not on it matching somebody else's model.

> **If a spec is missing, ask Patrick — he will source the manual.** Do not guess, do not reconstruct from memory, and do not go read another simulator. A wrong bit layout that "seems to work" is the most expensive kind of bug in a project like this, because the software will paper over it until one day it doesn't.

When two sources disagree, say so in the board's `.md` and say which one won and why.

Consequences already baked into this design:
- **AltairZ80's port-0xFE "SIMH pseudo device" is not implemented**, and `R.COM`/`W.COM` are not supported. See §12.
- Boards with no manual in the tree (88-HDSK, 88-VI, 88-ACR, 88-PIO/4PIO) are **blocked on documentation**, not on code. See §17. None of them block milestone 1.

### 0.2 Doc discipline

Every board ships with a `docs/boards/<board>.md` on the standard template (§14). No board is merged without one. The **Limitations** and **Quirks** sections are the load-bearing ones.

---

## 1. Purpose, goals, non-goals

`altairsim` is a C++ simulator of the MITS Altair 8800 and the S-100 bus, running on Intel Mac, Apple Silicon Mac, Linux, and Windows, built with CMake.

It is a **hardware development bench** that happens to run period software. The S-100 bus is a first-class modeled object, not an implementation detail, because the point is to develop **new hardware** as well as software. The interface is a SIMH/AltairZ80-style command monitor plus an MCP server so Claude can drive the machine efficiently.

**Goals**
- Emulate the Altair and the S-100 bus as closely as practical.
- Make writing a *new* S-100 board straightforward, and make a new board behave the way a real card would on a real backplane.
- Multiple instances of any board, each independently configured (e.g. two 88-2SIO *and* two 88-SIO boards at once).
- Boards that move characters can use the console, a TCP socket, or a real host serial port — interchangeably.
- An efficient, structured interface for Claude (MCP), not screen-scraping of a text CLI.

**Non-goals (v1)**
- Front panel GUI (no LEDs, no toggle switches). Sense switches at port 0xFF are a config value.
- Plugin ABI / dynamically loaded boards. All boards are compiled in and self-register.
- Cycle-exact modeling *within* a bus cycle (T1…T5 signal states).

---

## 2. Portability and build

- C++20, no compiler extensions. MSVC / clang / gcc.
- CMake ≥ 3.20 with presets (`CMakePresets.json`) for macOS (universal `arm64;x86_64`), Linux, Windows.
- **Dependencies, all via `FetchContent` with pinned versions:** a TOML parser, a JSON library, **replxx** (line editing, §10), and **SDL** (graphics/sound, §7). SDL is **optional** (`ALTAIRSIM_ENABLE_SDL`); the simulator must build and every acceptance test must pass with it off, so CI can run headless. Nothing else is a hard dependency of the core.
- Strict on warnings. `-fno-exceptions` is not required, but the hot bus path must be exception-free.
- **Endianness and alignment:** snapshots must be byte-exact across all four targets. Serialize explicitly; never `memcpy` a struct.

### 2.1 No `#ifdef`s. OS differences live in separate files.

**Hard architectural rule, enforced by CI.** SIMH is riddled with conditional compilation and the result is code you cannot read without mentally executing the preprocessor. We are not doing that.

OS differences are expressed as **an interface in a header with no conditionals at all**, plus **one implementation file per OS**, selected by CMake at configure time:

```
src/platform/
├── platform.h          # the contract. Pure declarations. Zero #ifdefs.
├── terminal.h  serial.h  socket.h  fs.h  time.h  console.h
├── posix/
│   ├── terminal_posix.cpp     # termios raw mode, restore-on-signal
│   ├── serial_posix.cpp       # open / tcsetattr / cfsetspeed
│   ├── socket_posix.cpp       # BSD sockets, poll
│   ├── fs_posix.cpp
│   ├── time_posix.cpp         # clock_gettime, nanosleep
│   ├── macos/ serial_macos.cpp   # only where macOS genuinely differs from Linux
│   └── linux/ serial_linux.cpp   #   (serial enumeration, custom baud rates)
└── win32/
    ├── terminal_win32.cpp     # SetConsoleMode, ENABLE_VIRTUAL_TERMINAL_*
    ├── serial_win32.cpp       # CreateFile, SetCommState/DCB, OVERLAPPED
    ├── socket_win32.cpp       # Winsock, WSAStartup
    ├── fs_win32.cpp
    └── time_win32.cpp         # QueryPerformanceCounter, timeBeginPeriod
```

CMake selects the right directory. Nothing else in the tree ever asks what OS it is on.

Rules that make this hold:
- **Opaque handles.** Where the underlying type differs (`int fd` vs `HANDLE` vs `SOCKET`), the interface exposes an opaque handle or a pimpl, never a raw platform type. This is what usually forces people back into `#ifdef`; head it off in the interface design.
- **macOS/Linux divergence inside POSIX** gets a `posix/macos/` or `posix/linux/` override *file*, never an `#ifdef __APPLE__` inside a shared file.
- **The only permitted conditionals are feature toggles, not OS toggles** — e.g. `ALTAIRSIM_ENABLE_SDL`, which lives in CMake.

**Enforcement:** a CI lint greps for `_WIN32`, `__APPLE__`, `__linux__`, `__unix__`, `_MSC_VER` anywhere outside `src/platform/*/` and **fails the build**. Without the lint this rule decays within a month: the first time someone needs one small thing on Windows an `#ifdef` appears "just this once," and you are SIMH again.

Payoff: a board author writes against `ByteStream`, `BlockDevice`, `Display`, `EventQueue` and **never sees an OS detail on any platform** — but only if the OS layer is genuinely sealed.

---

## 3. The CPU is a board

In a real Altair the processor *is* a card: the MITS **88-CPU** (8080A), and later the Z80 cards (Ithaca, Cromemco ZPU, TDL) people swapped in. So the CPU lives in `boards/` with everything else and gets `properties()` (`SET cpu0 CLOCK=2000000`, `SHOW cpu0`), `reset(PowerOn|Bus)`, `serialize()`, a slot, and a line in `BOARD LIST` — no special cases.

But there is a distinction to draw, and drawing it is what makes DMA clean:

> **Boards *respond* to bus cycles. The CPU *originates* them.**

Two concepts; a CPU card is both:

```cpp
class BusMaster {                       // drives cycles onto the bus
    virtual uint64_t step(Bus&) = 0;    // run one instruction; returns T-states consumed
};

class Cpu88 : public Board, public BusMaster { ... };   // the 88-CPU card
```

**The payoff is DMA.** S-100 has `pHOLD`/`pHLDA` precisely because a backplane can have more than one bus master — a disk controller or a Dazzler takes the bus away from the processor. With `BusMaster` as a first-class concept, a DMA card is simply a `Board` that *becomes* a `BusMaster` when granted the bus. DMA is not a bolted-on path in the bus; it is the same mechanism the CPU already uses. (It also leaves the door open to master/slave multiprocessor S-100 setups without designing for them now.)

**The chip is not the card:**
- `src/cpu/` — `Cpu8080`, `Cpu8085`, `CpuZ80`: pure instruction cores behind one `CpuCore` interface. No bus, no board, no config. Independently testable, which is what makes the 8080EXM/ZEXALL gate easy to run.
- `src/boards/cpu_88.cpp` — the **88-CPU card**: hosts a `CpuCore`, plugs into the bus, owns the clock property, handles `pINT`/`IntAck`, honors both resets, serializes.

The board's `cpu` property selects the core (`8080` in milestone 1; `8085`/`z80` later) — exactly how you'd swap the physical card.

### 3.1 Core semantics

- **Dispatch:** 256-entry table of function pointers or a `switch`. *Not* a bit-pattern if/elif chain (the Python prototype's linear scan).
- **T-states:** counted per instruction, with the conditional-branch fixups (CALL 17/11, RET 11/5). Unlike the Python prototype, the count is **load-bearing** — it drives `Board::tick()`, baud rate, disk rotation, and the clock throttle.
- **Flags:** `S Z 0 AC 0 P 1 CY`. Get the 8080 `ANA`/`ANI` half-carry rule right: `ac = (a|b) & 0x08`. Z80 gets the full flag set including undocumented bits 3/5 (XF/YF), `WZ`/MEMPTR, and the IX/IY prefix chains.
- **Interrupts** (absent from the Python prototype — its `EI`/`DI` set a flag nothing reads): an `INT` line raised by the bus, an `IntAck` cycle that fetches the instruction from the bus, plus Z80 IM 0/1/2 and 8085 RST 5.5/6.5/7.5 + TRAP.
- **`step()` returns an explicit `StepResult { uint32 tStates; Status status; }` — never a sentinel value.** See §16 for why (the RLC/RRC bug).

### 3.2 Validation is a gate, not a nice-to-have

The CPU is not "done" until **TST8080, 8080PRE, CPUTEST, and 8080EXM** pass (and ZEXALL/ZEXDOC for Z80). These run in CI as a headless `altairsim` batch script.

*The Python prototype's own notes say these were never run and that DAA is "not fully tested." Do not inherit that debt.*

---

## 4. The S-100 bus — the load-bearing spec

The bus owns arbitration. Boards never see each other.

```cpp
enum class Cycle { OpFetch, MemRead, MemWrite, IoIn, IoOut, IntAck, Halt };

struct BusCycle {
    uint16_t addr;
    uint8_t  data;      // valid on writes / out
    Cycle    type;
    bool     phantom;   // PHANTOM* asserted this cycle
};

class Board {
public:
    virtual ~Board() = default;
    virtual const char* type() const = 0;

    // Decode: does this board respond to this cycle?
    virtual bool     decodes(const BusCycle&) const = 0;
    virtual uint8_t  read (const BusCycle&)       = 0;
    virtual void     write(const BusCycle&)       = 0;

    // Clocked work: baud generation, disk rotation, timers. dt in T-states.
    virtual void     tick(uint64_t dt) {}

    // Interrupts. A board may drive pINT directly, and/or assert one of VI0-VI7.
    // The bus does NOT invent a vector — see §4.4.
    virtual bool     assertsInt() const { return false; }   // pINT (S-100 pin 73)
    virtual int      assertsVi()  const { return -1; }      // VI0..VI7, or -1

    // Bus mastering (DMA): assert pHOLD; when granted pHLDA, the bus calls
    // busMaster() and this board drives cycles itself, exactly as the CPU does.
    virtual bool        requestsBus() const { return false; }
    virtual BusMaster*  busMaster()         { return nullptr; }

    // Configuration reflection — see §5.
    virtual std::span<const Property> properties() const = 0;

    // Reset — see §6.
    virtual void     reset(Reset kind) = 0;

    // State (snapshot / restore).
    virtual void     serialize(Writer&) const = 0;
    virtual void     deserialize(Reader&)     = 0;
};
```

### 4.1 Registration and decode

A board declares I/O port ranges and memory address ranges at construction (from its TOML config). The bus builds decode tables. `decodes()` is the second-level check for boards whose decode is conditional (bank-enabled, phantomed, drive-selected).

### 4.2 PHANTOM

A memory board's phantom mode is `none | read | write | both`. A ROM board with `phantom = "read"` overlays the RAM beneath it for reads while writes fall through to the RAM — exactly how the Altair turnkey boot PROM at 0xFF00 coexists with a full 64K RAM board. Document it with that worked example.

### 4.3 Banking

RAM boards optionally register a bank-select I/O port; a write selects which of N pages is live. This is an ordinary I/O board write that mutates the board's own decode state — **not** a bus special case.

### 4.4 Two interrupt models — both required

**(a) Un-vectored: `pINT` alone, no VI board.** A board pulls `pINT` (S-100 pin 73). The CPU finishes the current instruction and, if interrupts are enabled, runs an **`IntAck` bus cycle** (`sINTA`). During that cycle the interrupting board may *drive the data bus*, and whatever it drives is **executed as an instruction** — conventionally an `RST n`. **If no board drives the bus, it floats high and the CPU reads `0xFF` = `RST 7` (0x38).** That is not a fallback hack; it is how the machine actually behaves, and it is why the PMMI's factory jumper (E10→E9, straight to pin 73) yields RST 7 with no vector logic at all. Model the floating bus honestly and this falls out for free.

**(b) Vectored: VI0–VI7, with an 88-VI board.** Eight prioritized request lines, VI0 highest. **The bus does not arbitrate these — the 88-VI board does.** The VI board watches the eight lines, applies priority and its own mask, drives `pINT` itself, then claims the `IntAck` cycle and drives the corresponding `RST n` onto the data bus.

**Consequence for the API:** the bus must *not* pick a winner and ask it for a vector. That is the VI board's job; building it into the bus would make the VI board unimplementable as a board and would fabricate vectored behavior on a machine that has no VI card in it. The bus does exactly three things:

1. carry `pINT` as the wire-OR of every board asserting it,
2. carry VI0–VI7 as eight signals boards can assert and other boards can observe,
3. run an `IntAck` cycle that boards claim like any other bus cycle.

Everything else is a board. The 88-VI is then just another board with no special privileges — and so is any *new* interrupt controller you invent, which is the point of the project.

A board declares its jumpering as a property: `interrupt = none | int | vi0 … vi7`. An 88-2SIO with `interrupt = int` gives RST 7 with no VI board present; the same board with `interrupt = vi2` in a machine containing an 88-VI gives `RST 2`.

### 4.5 DMA is bus mastering

A board asserts `pHOLD`; the bus grants (`pHLDA`) at the next instruction boundary; the board then acts as a `BusMaster` — the same interface the CPU uses — and drives its own cycles. Stolen T-states are charged to the clock, so the CPU genuinely loses time rather than getting DMA for free. When the board releases, the CPU resumes mastering.

### 4.6 Contention

When two boards `decodes()` the same cycle, log a diagnostic naming both board `id`s and the address, and return the wire-AND of the drivers (real tri-state / open-collector contention pulls low). This is a **feature** — it is the bug you'd chase on a real backplane, surfaced immediately.

`SET BUS CONTENTION=WARN|ERROR|SILENT`.

### 4.7 Fast path

Plain unbanked, non-phantom RAM resolves to a direct pointer through a page table; the full `BusCycle` dispatch runs only for pages with a non-trivial decoder. Document this so nobody "optimizes" it away later.

---

## 5. Board properties — one generic interface, board-specific contents

`SET sio2a BAUD=9600` must work without the monitor knowing what a baud rate is. So a board **describes its own configuration**, and the monitor, TOML loader, MCP server, tab-completion, and `CONFIG SAVE` all drive that one description.

```cpp
struct Property {
    std::string  name;         // "baud"
    Type         type;         // Int | Enum | Bool | String | Hex | Path
    std::string  description;  // "Serial line rate"
    std::string  units;        // "bps"
    Constraint   constraint;   // range, or enum value list
    bool         runtime;      // settable while running, vs config-time only
    Getter       get;
    Setter       set;          // returns an error string on reject
};
```

Consequences, all of which must be stated in the doc:
- `SET <id> <k>=<v>` and `SHOW <id>` are **fully generic**. `SHOW` prints every property with value, units, legal range, and whether it is runtime-settable.
- **MCP tool schemas are generated from `properties()`** — Claude gets typed, constrained, self-documenting board config instead of guessing at free text.
- **The TOML loader and `CONFIG SAVE` are the same code path.** A board's config keys *are* its properties, so round-tripping is automatic and cannot drift.
- **Tab completion is generated from `properties()`** too (§10.4).
- `runtime = false` properties are rejected with a clear message while the machine runs, rather than silently taking effect at the next reset.

---

## 6. RESET semantics

The S-100 bus has two distinct reset lines, and boards must honor both. Conflating them is the classic source of "works from power-on but not from the reset button" bugs.

```cpp
enum class Reset {
    PowerOn,   // POC* — power-on clear. Cold. Every board returns to power-up state.
    Bus,       // RESET* — the front-panel reset button. Warm. CPU PC<-0, boards reset
               // their logic, but memory contents SURVIVE and mounted media / connected
               // streams stay attached.
};
```

Each board's `.md` must say **concretely** what each reset does to it. Examples:
- **88-2SIO**: 6850 master reset on both (clears RDRF/TDRE, drops RTS), but **keeps its `ByteStream` connected**.
- **88-DCDD**: deselects all drives, unloads the head, invalidates the sector counter on both — but **keeps images mounted**, and does *not* seek to track 0 on a warm reset (real drives don't).
- A DMA board must release the bus.
- CPU: PC←0, interrupts disabled; Z80 also `I`/`R`←0 and IM 0.

CLI:
```
RESET        RESET* / pRESET — "press the reset button". Warm; memory survives.
RESET CPU    CPU only; boards untouched (a debugging convenience, not a real signal).
POWER        POC* — full cold start.
```

`RESET` must be safe at any point — the bus asserts it at the next instruction boundary, never mid-cycle.

---

## 7. Host services layer

Boards must **never** touch a socket, a file handle, or `termios` directly. Everything a board needs from the host goes through a small set of generic, platform-abstracted services implemented once in `platform/`. This is what keeps the four targets honest and keeps replay deterministic — if a board can read the host clock or a socket on its own, replay is dead.

### 7.1 `ByteStream` — the generic serial endpoint

Every board that moves characters (88-SIO, 88-2SIO, 88-ACR, 88-LPC, paper tape, PMMI) talks only to this. `CONNECT` binds an implementation to a unit.

```cpp
class ByteStream {
public:
    virtual size_t read (uint8_t* buf, size_t n) = 0;  // non-blocking; 0 if empty
    virtual size_t write(const uint8_t* buf, size_t n) = 0;
    virtual bool   readable() const = 0;               // -> drives RDRF
    virtual bool   writable() const = 0;               // -> drives TDRE
    virtual void   flush() = 0;
    virtual Status status() const = 0;                 // carrier/DSR/CTS
};
```

Implementations: `ConsoleStream`, `TcpListenStream` (`socket:2323` — accept, one client, survive disconnect/reconnect), `TcpConnectStream` (`socket:host:port`), `HostSerialStream` (termios vs `SetCommState`/DCB; exposes baud/parity/stop/flow), `FileStream` (paper tape), `NullStream`, `LoopbackStream` (testing), `ReplayStream` (recorded bytes at recorded T-state stamps).

**Discipline: the board asks `readable()`/`writable()`; it never blocks.** All actual I/O is drained/filled by the event loop once per time slice, so `tick()` is pure computation.

### 7.2 `Console` — the host keyboard and screen

A `ByteStream` like any other, so a board connecting to it needs no special code. But it is the only stream with a human on the far end, so it owns a configurable **transform chain**, applied inbound from the keyboard and outbound to the screen. Properties are declared through the same `Property` layer as boards, so `SET`/`SHOW`/MCP/completion work on it for free.

| Property | Meaning |
|---|---|
| `upper` | Fold keyboard input to uppercase. Essential — much period software only accepts caps. |
| `strip7in` / `strip7out` | Mask the high bit, independently per direction. |
| `crlf` | CR→CRLF on output; Enter→CR vs LF on input. |
| `bsdel` | Map host Backspace to BS (0x08) or DEL (0x7F). A perennial CP/M annoyance. |
| `tabs` | Expand outbound tabs at N columns. |
| `echo` | Local echo (for half-duplex hardware). |
| `ansi` | Run outbound bytes through the VT100/ANSI screen model, or pass raw. |
| `rows`, `cols` | Screen size — **and the answer to an `ESC[6n` DSR query**, which is how a guest discovers terminal size. (Lifted from the Python prototype, but as a property rather than a hardcoded sniff.) |
| `pace` | Throttle output to the configured baud, so a 110-baud Teletype *looks* like one. Off by default. |
| `attn` | The escape key that drops from console back to the monitor (e.g. `Ctrl-E`). **Non-negotiable** — without it a guest that swallows all input traps you with no way out. |
| `log` | Tee the session to a host file. |
| `bell` | Ring the host bell on 0x07, or ignore. |

**This list will grow.** That is precisely why it is a property table rather than hardcoded flags: adding one means adding a row, and the CLI, TOML, MCP, and completion pick it up automatically.

**Implement the transforms as a reusable filter chain on `ByteStream`, not as console-specific code** — a real terminal on a real host serial port wants the same uppercase folding, so `SET sio2b UPPER=ON` on a socket-connected line works for free.

**Arbitration:** exactly one unit may hold the console at a time. `CONNECT sio2a:a console` steals it, warning who had it.

### 7.3 `BlockDevice` — the generic mountable medium

Every disk/tape board (88-DCDD, 88-HDSK, minidisk, any future controller) sees only this. `MOUNT` binds an implementation to a unit.

```cpp
class BlockDevice {
public:
    virtual bool     readBlock (uint64_t lba, uint8_t* buf, size_t n) = 0;
    virtual bool     writeBlock(uint64_t lba, const uint8_t* buf, size_t n) = 0;
    virtual uint64_t size() const = 0;
    virtual bool     readOnly() const = 0;
    virtual void     sync() = 0;
    virtual Geometry geometry() const = 0;   // probed from file size, or forced
};
```

Implementations: `RawImageFile` (the common case — buffered, dirty write-back, geometry probed with a `media=` override), `ReadOnlyImage`, `MemoryDisk`, and later `ImdImage`/`Td0Image`.

**Geometry probing lives here, once** — not duplicated in each controller. The 88-DCDD's byte-offset math (`137 * spt * track + 137 * sector`) stays in the *board*, because that is the controller's business; file access is the service's.

### 7.4 `Display` and `Audio` — SDL

For boards with graphics or sound (**VDM-1**, **Cromemco Dazzler**, music boards, and whatever you build next). Backed by SDL, and **optional**: compiled in only with `ALTAIRSIM_ENABLE_SDL`. A headless build must still pass every acceptance test.

**Boards never call SDL.** They see only:

```cpp
class Display {
public:
    virtual Surface* acquire(int w, int h, PixelFormat) = 0;
    virtual void     present(Surface*) = 0;
    virtual void     setPalette(std::span<const Color>) = 0;
};

class Audio {
public:
    virtual void   push(std::span<const int16_t> samples) = 0;   // clocked from EventQueue
    virtual size_t queued() const = 0;
};
```

The two boards this must serve are usefully different, and the API should be hand-checked against both:
- **VDM-1** is *memory-mapped*: a 1K text window the CPU writes into, plus a character-generator ROM; renders 16×64 characters. Its keyboard is a **separate parallel board** — so the SDL window's keystrokes must route back through a **`ByteStream`**, not a private path.
- **Dazzler** is *DMA-driven*: it steals bus cycles to read a bitmap out of main memory. It needs the `requestsBus()`/`busMaster()` path, and it is the concrete reason DMA is in the bus model at all.

Two constraints that are painful to retrofit:
1. **The SDL event loop does not own the main loop.** The simulator's clock and `EventQueue` own emulated time; the display is pumped once per time slice. Letting SDL drive would put the host frame rate in charge of emulated time and wreck both throttling and replay.
2. **On macOS, SDL requires the window and event pump on the main thread.** That is an OS constraint, not an SDL preference, and it dictates the threading model: main thread pumps SDL, emulation runs elsewhere, they communicate through queues. **Decide this now** — discovering it after the machine loop is written means restructuring the program.

**Keystrokes from an SDL window are an *input*** — they go through the recorded event queue like everything else, or replay breaks the first time a Dazzler game is involved.

### 7.5 `Clock` / `EventQueue` — the single source of time

Nothing in the simulator may call `std::chrono::now()` except this. Boards schedule future work in **T-states**, not milliseconds:

```cpp
class EventQueue {
public:
    Handle schedule(uint64_t deltaTStates, std::function<void()> fn);
    void   cancel(Handle);
    void   advance(uint64_t tStates);   // fires everything due
};
```

This is what makes replay deterministic and what lets `tick()` be cheap — a board with nothing to do until sector 7 rotates under the head schedules one event instead of being polled every instruction.

### 7.6 `Log` / `Trace`

One structured diagnostic sink with per-board and per-category masks (`IN`, `OUT`, `READ`, `WRITE`, `IRQ`, `DMA`, `CONTENTION`), mirroring the `DEBTAB` idea in `mits_dsk.c`. Text for the monitor, JSON for MCP, from the same call site.

### 7.7 The two consequences worth stating explicitly

- **A new board written against these services is automatically cross-platform and automatically replayable.** That is the point of the layer, and it is the acceptance test for the API.
- **`CONNECT` and `MOUNT` are generic**, not per-board commands. The monitor resolves an endpoint string to a `ByteStream` or a file to a `BlockDevice` and hands it to whichever board declared a unit of that type — so a board written next year gets `MOUNT`/`CONNECT` for free without touching the monitor.

---

## 8. Timing and host idling

- Clock is `clock_hz` (default 2,000,000) or `0` for free-running.
- Throttle by comparing accumulated T-states against a monotonic host clock in ~1 ms slices and **sleeping** the remainder — never spin.
- **Idle detection** — steal this from the Python prototype; it is what makes automation work. Count consecutive console-status reads that return RDRF=0 **with no intervening I/O of any kind**. Any data read, char write, or disk port access resets the counter. Past a threshold, the machine is *provably parked at a prompt*: the run loop reports `idle`, and **the host process sleeps instead of emulating a spin loop.** This is what stops a CP/M prompt from pinning a core, and what lets automated builds terminate promptly instead of burning 20M steps.

---

## 9. Devices, `MOUNT`, and `CONNECT`

- A board declares typed **units**. A disk unit accepts `MOUNT id:unit <hostfile>` (`DISMOUNT` to release); a serial unit accepts `CONNECT id:unit <endpoint>` (`DISCONNECT`).
- Endpoints: `console` | `socket:PORT` (listening) | `socket:HOST:PORT` (outbound) | `serial:/dev/tty.usbserial-X` or `serial:COM3` | `file:path` | `null`.
- Exactly one unit may hold `console` at a time; the monitor arbitrates.
- Disk images are buffered and written back. Geometry auto-detected by file size, overridable with `media = "8in" | "minidisk" | "fdc8mb"`. `readonly` supported (the real board's write-protect).

---

## 10. Monitor CLI

SIMH/AltairZ80-flavored, stable and greppable.

```
CONFIGURATION
  CONFIG LOAD <file.toml>          CONFIG SAVE <file.toml>
                                   (bare LOAD/SAVE mean *memory* — see below)
  BOARD LIST                       instances: id, type, ports, memory, status
  BOARD TYPES                      every board type compiled in, with its properties
  BOARD ADD <type> <id> [k=v ...]  BOARD REMOVE <id>
  SHOW <id>                        every property: value, units, legal range, runtime?
  SET <id> <k>=<v>                 generic; e.g. SET sio2a BAUD=9600, SET rom0 PHANTOM=read

INTROSPECTION
  SHOW BUS                         CPU, clock, T-states, pending IRQ, DMA state
  SHOW BUS MAP                     memory decode map: range -> board, type, phantom, bank
  SHOW BUS IO                      I/O decode map: port -> board, direction
  SHOW BUS IRQ                     VI0-VI7 assignments, pending lines, priority
  SHOW BUS CONTENTION              every address/port claimed by more than one board
  WHO <addr> | WHO IO <port>       reverse lookup: who responds here, and why

MEDIA AND CONNECTIONS
  MOUNT <id>:<u> <file> [RO]       DISMOUNT <id>:<u>
  CONNECT <id>:<u> <endpoint>      DISCONNECT <id>:<u>
  DISK LS|GET|PUT <id>:<u> ...     host-side CP/M filesystem access to a mounted image

CONSOLE
  SHOW CONSOLE                     every property, value, legal values
  SET CONSOLE <k>=<v>              UPPER, STRIP7IN, STRIP7OUT, CRLF, BSDEL, TABS, ECHO,
                                   ANSI, ROWS, COLS, PACE, ATTN, LOG, BELL  (list will grow)
  CONSOLE                          enter the console; ATTN key returns to the monitor

MEMORY
  LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [RAW <id>]    format autodetected
  SAVE <file> <range> [FORMAT=BIN|HEX] [RAW <id>]
  DUMP <range> [WIDTH=16]           hex + ASCII
  DISASM <range>|<addr> [n]         mnemonics follow the selected CPU
  EDIT <addr>                       interactive: show byte, type new value, Enter advances
  DEPOSIT <addr> <bytes...>
  FILL <range> <byte>
  SEARCH <range> <bytes...>|"str"
  COMPARE <range> <addr> | COMPARE <range> <file>
  MOVE <range> <dest>
  All of the above take an optional BANK=<n> to reach a non-live bank.

EXECUTION
  GO [addr] | STEP [n] | STOP | BOOT <id>[:u]
  RESET | RESET CPU | POWER
  SET CPU 8080|8085|Z80            SET CLOCK <hz>|UNLIMITED

DEBUG
  BREAK <addr> [IF <expr>] | BREAK IO <port> | BREAK MEM R|W <range> | NOBREAK
  REGS | SET REG <r>=<v>
  TRACE ON|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]   HISTORY [n]
  SNAPSHOT <file> | RESTORE <file> | RECORD <file> | REPLAY <file>
  SET BUS CONTENTION=WARN|ERROR|SILENT
```

`altairsim -c script.cmd` runs a command script non-interactively and exits with a status code — the CI/regression entry point.

### 10.1 `SET`/`SHOW` are generic

Implemented once against `Board::properties()`; they know nothing about baud rates, phantom modes, or disk geometry. Adding a board adds its settings to the CLI for free. Config-time vs runtime settability is enforced by the board and displayed by `SHOW`, so `SET fdc DRIVES=8` on a running machine is rejected clearly rather than half-applied.

### 10.2 Memory access: through the bus, or behind it?

- **Default: through the bus.** `DUMP`, `DISASM`, `DEPOSIT`, `EXAMINE` see exactly what the CPU sees — live bank, PHANTOM overlays applied, ROM boards rejecting writes, contention reported. This is the only view that tells the truth about a misbehaving decode.
- **`RAW <id>`: behind the bus**, straight into one board's backing store. This is how you `LOAD dbl.bin AT 0xFF00 RAW turnkey` — a ROM board correctly refuses a bus write, so loading its image *must* bypass the bus. Same mechanism reaches a non-selected bank, and lets you inspect a phantomed-out board the CPU cannot see.

`DUMP`/`DISASM` annotate output when bytes came from a phantom overlay or a non-live bank, so a confusing read explains itself.

### 10.3 Intel HEX

Loader accepts record types **00** (data), **01** (EOF), **03**/**05** (start address — captured, optionally sets PC). **Validate every record's checksum and fail loudly with the record number** — a silently truncated load is a miserable bug to chase. Types 02/04 are accepted if they resolve within 64K, else error. `AT <addr>` biases the record addresses.

Writer emits 00/01 with a configurable record length (default 16) and an `03` start record if an entry point is given. **Round-trip is a test case:** `SAVE x.hex 0-FFFF` then `LOAD x.hex` must reproduce memory byte-for-byte.

Binary is a flat image: `LOAD` needs `AT <addr>`, `SAVE` needs an explicit range. Autodetection sniffs for a leading `:` and printable HEX records; `FORMAT=` forces it.

The same engine backs the MCP tools (`mem_load`, `mem_save`, `mem_dump`, `disasm`) — one implementation, two front ends.

### 10.4 Line editing and history

The monitor is a REPL you will spend hours in, so it gets a real line editor, like SIMH's:

- **History** with Up/Down, persisted across sessions (`~/.altairsim_history`), Ctrl-R searchable.
- **Full editing**: Left/Right, Ctrl-A/Ctrl-E, word motion, Ctrl-W/Ctrl-U/Ctrl-K, Home/End/Delete.
- **Tab completion driven by the same `Board::properties()` reflection** — it completes command names, then board `id`s, then that board's property names, then that property's legal enum values. `SET sio2a BA<Tab>` → `BAUD=`, and `BAUD=<Tab>` offers the valid rates. **No completion tables to maintain**; a board added later is completable the day it lands.

**Implementation:** vendor **replxx** (BSD, C++, real cross-platform including Win32 console, UTF-8, history, completion). `linenoise` is smaller but its Windows support is a fork of a fork. This is a deliberate exception to "no third-party deps in the core" — hand-rolling a line editor that behaves on Windows *and* macOS *and* Linux is weeks of work with a poor payoff.

**Terminal-mode handoff.** The line editor and the emulated console both want raw mode and must trade it cleanly: `CONSOLE` hands the terminal to the guest, the `ATTN` key hands it back. **The terminal must be restored on every exit path, including a crash or a signal** — a simulator that leaves your shell in raw mode after a segfault is its own kind of bug. Windows console mode flags need the same care.

---

## 11. MCP server

`altairsim --mcp` (stdio) or `--mcp-port N` (TCP, attach to a long-lived machine). Tools mirror the monitor but are **structured** — every result is JSON, no screen-scraping.

The high-value tools are borrowed from the Python prototype's agent API, which is the right shape:

- `run(max_steps, idle_threshold) -> {reason: halt|breakpoint|idle|max_steps, steps, t_states}`
- `send(text)` — queue keystrokes to the console unit
- `expect(pattern, max_steps, idle_threshold) -> {found, steps, output}` — run until the pattern appears; on failure return the tail of captured output
- `screen() -> {rows, cols, grid}` — VT100/ANSI screen emulator, so a test asserts on a **screen grid** rather than a byte stream. Essential for full-screen guest apps.
- `step`, `regs`, `breakpoints`, `snapshot`, `restore`, `bus_trace`
- `mem_dump`, `mem_deposit`, `mem_load`, `mem_save`, `mem_search`, `mem_fill`, `disasm` — same `raw`/`bank` qualifiers as the monitor, same engine
- `mount`, `connect`, `disk_ls`, `disk_get`, `disk_put`
- `reset(kind: bus|cpu|power)`
- `board_list`, `board_types`, `board_get(id)`, `board_set(id, key, value)` — **schemas generated from `Board::properties()`**
- `bus_map()`, `bus_io()`, `bus_irq()`, `bus_contention()`, `who(addr)` — structured, not the ASCII table

**MCP is a first-class interface, not a wrapper.** The monitor and MCP both sit on one `Machine` API and one `properties()` reflection layer, so they cannot drift. Any board added later is fully drivable from both without touching either.

---

## 12. Host file transfer

Two complementary mechanisms, serving different needs.

> **We do not implement AltairZ80's port-0xFE "SIMH pseudo device", and `R.COM`/`W.COM` are not supported.** That device is not real Altair hardware — it is another simulator's invention, and its only purpose is compatibility with that simulator's own guest utilities. Reimplementing it would mean deriving from AltairZ80's source, which this project does not do (§0.1). Instead we design our own host bridge as if it were a real S-100 card — which is, after all, what this project is *for*.

### 12.1 The Host Bridge board (guest-initiated)

A board of our own design (`docs/boards/hostbridge.md`), documented to the same standard as any period card. It is an ordinary `Board` — it claims two I/O ports, has `properties()`, honors both resets, and serializes. No bus special cases.

It is also **the project's first genuinely new piece of hardware**, which makes it a real test of the board API rather than a rehash of a known card.

**Sandboxing is a hard requirement**, not a nicety: guest-supplied filenames resolve against a configured `hostdir` root and **cannot escape it**. A guest program must never be able to write anywhere on the host disk.

Guest-side utilities (`HGET.COM`, `HPUT.COM`, `HDIR.COM`) are ours, written in 8080 assembly and assembled with the period toolchain.

### 12.2 MCP disk-image tools (host-initiated)

The Python prototype's `DiskImage` already knew how to walk the CP/M directory, decode extents, and `extract_file`/`write_file`. Lift that into a host-side CP/M filesystem library and expose it over MCP: `disk_ls(id:unit)`, `disk_get(id:unit, name)`, `disk_put(id:unit, name, bytes)`.

This works on a **mounted image with the guest not running at all** — no `R.COM`, no console driving, no boot. For Claude this is far more efficient than typing at a CP/M prompt, and it is the right tool for staging a test fixture or pulling out a build artifact.

---

## 13. Snapshots and deterministic replay

- **Snapshot** = versioned, explicitly-serialized CPU + bus + every board's `serialize()`. Byte-identical across platforms.
- **Replay** = snapshot + an event log (keystrokes, socket input, SDL keystrokes, DMA completions) stamped with **T-state timestamps**, so a session replays exactly.

This is the bug-reproduction and regression-test foundation, and it is why **every source of nondeterminism (host time, socket arrival, RNG, window events) must funnel through one clocked event queue.**

---

## 14. Every board ships with a `.md` — a gate, not a nicety

**No board is merged without `docs/boards/<board>.md`.** The board and its documentation are one deliverable. Template: `docs/boards/_TEMPLATE.md`.

The **Limitations** and **Quirks** sections are load-bearing. They are what you will want in two years when something doesn't boot, and what makes it possible for someone else — or Claude — to work on a board without rediscovering everything. Writing them *forces* the honest question "what did I not actually implement?", which is exactly the question a simulator author most wants to avoid.

---

## 15. Testing

- **CPU:** 8080EXM / CPUTEST / TST8080 / 8080PRE; ZEXALL / ZEXDOC for Z80. A hard CI gate.
- **Bus:** unit tests for PHANTOM overlay, bank switching, contention detection, VI priority, DMA cycle stealing, floating-bus RST 7.
- **Boards:** golden-image tests — boot CP/M 2.2 from `CPM22-8MB-56K-SIM.DSK`, run `M80`/`L80`, compare output.
- **End-to-end:** MCP `expect` scripts, headless in CI on all four platforms.
- **Lint:** the no-`#ifdef` check (§2.1).

---

## 16. Lessons from the prior work — read before writing code

See `docs/porting-notes.md` for the full list. The ones that will bite:

1. **Idle detection** is what makes automation work (§8). Steal it.
2. **The BIOS track-buffer flush requires CONIN.** The 8 MB Altair CP/M BIOS does *not* write to the DCDD when a file is closed — BIOS WRITE only fills an in-memory `trkBuf` and marks it dirty. The real port-0x0A write happens in `invFlush`, **called from the BIOS CONIN entry**. Consequence: *never* flush the disk right after a CP/M file op — the directory update from BDOS Close sits in `trkBuf` until the next BDOS function 1. Run back to the `A0>` prompt first.
3. **The RLC/RRC sentinel bug.** In the Python core, a carry-bit local named `cy` shadowed the cycle-count variable `cy`, so `step()` returned 0, which the run loop read as "breakpoint hit" — silently killing M80/L80 after ~2.8M steps. **This is why `step()` returns an explicit `StepResult`, never a sentinel.**
4. **BDOS clobbers every register** (A,B,C,D,E,H,L; only SP preserved). Always call BDOS at 0005H, never BIOS directly.
5. **Delay physical drive select until READ/WRITE** — don't seek on select alone.
6. **The DCDD's dirty-buffer write-back ordering** (`out08` select, `in09` sector read, `out09` step all flush first, in that specific order relative to invalidating sector/byte position). Getting it wrong silently corrupts disks.
7. **M80 symbols are only 6 characters significant** — the #1 guest-toolchain gotcha.

---

## 17. Blocked on documentation

**None of these block milestone 1.** Per §0.1, each is blocked on a *manual*, not on code. **Ask Patrick and he will source it** — do not reconstruct, guess, or read another simulator.

| Board | What's missing | Needed by |
|---|---|---|
| **88-ACR** | Cassette-specific control bits (motor control, if any). Only the port numbers (0x06/0x07) and the inverted-SIO bit sense are known. | Milestone 5 |
| **88-PIO / 88-4PIO** | Bit layouts and handshake (CA1/CB2) semantics. Only port numbers are known: PIO 0x04/0x05; 4PIO 0x20–0x23. | Milestone 7 |
| **88-VI / RTC** | Register layout, priority scheme, RST vector generation. Nothing at all in the tree. | Milestone 6 |
| **88-HDSK** | Ports, command protocol, geometry, image format. Nothing at all in the tree. | Milestone 7 |
| **PMMI** (deferred) | The **E1–E7 pad → VI0–VI7 correspondence** — the manual says only to consult your CPU/VI card manual. Everything else is recovered. | If/when PMMI is built |

**Available and sufficient:** the 88-2SIO (MITS *Theory of Operation* manual, `s100-manuals/MITS/ALTAIR_8800/`), the 88-DCDD (`mits_dsk.c` + `BIOS.ASM` + `CLAUDE.md`), and the PMMI (its manual, `pmmi-cpm22/`).

**One inconsistency to settle:** the Python prototype and `BIOS.ASM` disagree on the 88-DCDD's `I` and `Z` status-bit positions. `mits_dsk.c` is authoritative — reconcile against it (§`docs/boards/88-dcdd.md`).

---

## 18. Roadmap

See `docs/roadmap.md`. Milestone 1 is **CLI + MCP + 8080 + bus (incl. interrupts) + RAM + 88-2SIO**, and nothing else.
