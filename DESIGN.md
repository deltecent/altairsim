# `altairsim` — Design Document

**Status:** implemented and running. Ten board types, a validated 8080, and period software
that boots — CP/M 2.2 off an 8″ floppy, a 5¼″ minidisk and an 8 MB disk; 4K and 8K BASIC and
MITS Programming System II off cassette. What is *not* built is named where it is described,
and the roadmap has the rest.
**Date:** 2026-07-11, and kept current since.

---

## 0. Ground rules

### 0.1 Where hardware facts come from

**Period manuals and first-hand artifacts only.** Register maps, bit layouts, and timings are sourced from original vendor documentation (MITS and other manufacturers' manuals), from period software's own equate blocks and disassembled PROMs, or from hardware in hand.

**We do not read other emulators' source code to learn how hardware works.** That includes SIMH / AltairZ80. Second-hand facts inherit second-hand mistakes, and the whole value of this project rests on the hardware model being *right*, not on it matching somebody else's model.

> **If a spec is missing, ask Patrick — he will source the manual.** Do not guess, do not reconstruct from memory, and do not go read another simulator. A wrong bit layout that "seems to work" is the most expensive kind of bug in a project like this, because the software will paper over it until one day it doesn't.

When two sources disagree, say so in the board's `.md` and say which one won and why.

Consequences already baked into this design:
- **AltairZ80's port-0xFE "SIMH pseudo device" is not implemented**, and *its* `R.COM`/`W.COM` will not run here. See §12. *(This aged well: as of 2026-07-13 port 0xFE is the **88-VI/RTC's real control register** — 376 octal, straight out of the MITS manual. AltairZ80 put its pseudo-device on top of a port that belongs to an actual MITS card, and we would have had to evict it.)* **We ship our own `R.COM` and `W.COM`** against our own Host Bridge card at 0xB0 (§12.1) — same names, because the muscle memory is worth keeping; different code, different card, different protocol, nothing derived.
- Boards with no manual in the tree (88-HDSK, 88-PIO/4PIO) are **blocked on documentation**, not on code. See §17. None of them block milestone 1. (The **88-ACR** and the **88-VI/RTC** were on this list; both manuals are now in the tree and both cards are built.)

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
- Front panel **GUI**. No window, no LEDs you can look at, no switches you can click. **But the panel itself is a CARD** (`fp`, `docs/boards/mits-frontpanel.md`) — it decodes the sense switches at port `0xFF`, and it latches the address/data/status lines it sees go by, because that is what LEDs soldered to a bus *are*. A GUI is a view onto that card, and it needs no new bus concept to be written.
  > This line used to read *"Front panel GUI (no LEDs, no toggle switches). Sense switches at port 0xFF are a **config value**."* It was wrong in a way worth keeping the scar tissue for. `sense` really was a config value — a byte on the `Machine` that **nothing put on the bus** — so the guest's `IN 0FFH` read the floating bus and got `0xFF` no matter what the operator configured, and DBL's stop-bit test was reading a wire, not a switch. Declaring the panel a non-goal is what made it plausible to fake the one part of it that software actually touches. The switches are on a card now, which is where §3 says they always were. (Patrick, 2026-07-12.)
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

> **`src/platform/` EXISTS as of 2026-07-12** — `serial.h` + `socket.h` + `terminal.h` (pure declarations, zero conditionals, **no OS type in any signature**: no `int fd`, no `HANDLE`, no `termios`), with `posix/` built and proved against two FTDI cables, a null modem and a pty, and `win32/` **written but unbuilt** (`docs/porting-notes.md` says so plainly). CMake picks the directory; that `if(WIN32)` in `CMakeLists.txt` is the only place in the project that asks what OS it is on.
>
> **The rule earned its keep immediately.** The POSIX socket file wanted `MSG_NOSIGNAL` (Linux) / `SO_NOSIGPIPE` (macOS) to stop a hung-up client from killing the process with SIGPIPE — a genuine macOS/Linux divergence, which by this section's own rules would need its own *file*. `signal(SIGPIPE, SIG_IGN)` is plain POSIX, works on both, and needs no branch at all. **The right answer to a platform conditional is usually to stop needing it.**
>
> **THE LINT IS ON, as of 2026-07-12** (`cmake/lint_platform.cmake`, a build dependency of `altair_core` — not a test, because this section says *fails the build*). The terminal was the last thing in the tree with an OS underneath it; it is now `src/platform/terminal.h`, and `lineedit.cpp`'s `#if defined(_WIN32)` — the only conditional compilation in the project — is gone with it.
>
> **The lint greps for OS *headers*, not just OS *macros*, and that is the half that mattered.** Moving the terminal turned up two offenders and only one had a conditional: `src/host/console.cpp` simply `#include`d `<termios.h>` in the open, no `#ifdef` anywhere near it. A macro-only lint — the one this section originally specified — would have called that file **clean**, and it would have compiled here forever and failed on Windows the day someone tried. The `#ifdef` is the symptom. **Reaching for the OS outside the platform layer is the disease**, and the lint is aimed at the disease.

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

Payoff: a board author writes against `ByteStream`, `DiskImage`, `Display`, `EventQueue` and **never sees an OS detail on any platform** — but only if the OS layer is genuinely sealed.

---

## 3. The CPU is a board

In a real Altair the processor *is* a card: the MITS **88-CPU** (8080A), and later the Z80 cards (Ithaca, Cromemco ZPU, TDL) people swapped in. So the CPU lives in `boards/` with everything else and gets `properties()` (`SET cpu0 CLOCK=2000000`, `SHOW cpu0`), `reset(PowerOn|Bus)`, `serialize()`, a slot, and a line in `BOARDS` — no special cases.

But there is a distinction to draw, and drawing it is what makes DMA clean:

> **Boards *respond* to bus cycles. The CPU *originates* them.**

Two concepts; a CPU card is both:

```cpp
class BusMaster {                        // drives cycles onto the bus
    virtual StepResult step(Bus&) = 0;   // one instruction: T-states, and WHY it stopped
};

class Cpu8080Board : public Board, public BusMaster, public CpuCard { ... };  // the 88-CPU
```

*(`step()` returns a `StepResult`, never a bare T-state count — §3.1. A count alone
cannot distinguish "this instruction legitimately took N cycles" from "something
went wrong", and §16 records what that cost the prototype.)*

**Built, 2026-07-11.** `src/isa/isa8080.cpp`, `src/cpu/cpu8080.cpp`,
`src/boards/mits-88cpu.cpp`, and the debugger in `src/core/debug.cpp`. The card
decodes nothing — `BOARDS` shows it with no memory and no I/O, and that is the
truth about a processor card, not a gap in the table. It still has a *unit*
(`1 cpu: 8080`), because the processor on the card is a unit like any other. See
`docs/boards/mits-88cpu.md`.

**The payoff is DMA.** S-100 has `pHOLD`/`pHLDA` precisely because a backplane can have more than one bus master — a disk controller or a Dazzler takes the bus away from the processor. With `BusMaster` as a first-class concept, a DMA card is simply a `Board` that *becomes* a `BusMaster` when granted the bus. DMA is not a bolted-on path in the bus; it is the same mechanism the CPU already uses. (It also leaves the door open to master/slave multiprocessor S-100 setups without designing for them now.)

**The chip is not the card:**
- `src/cpu/` — `Cpu8080`, `Cpu8085`, `CpuZ80`: pure instruction cores behind one `CpuCore` interface. No bus, no board, no config. Independently testable, which is what makes the 8080EXM/ZEXALL gate easy to run.
- `src/boards/mits-88cpu.cpp` — the **88-CPU card**: hosts a `CpuCore`, plugs into the bus, owns the clock property, handles `pINT`/`IntAck`, honors both resets, serializes.

A card's cores are **units** (§3.0.1) — a plain 88-CPU has exactly one, and a dual-processor card has two with one active. Swapping the *card* is `BOARDS REMOVE` / `BOARDS ADD`, exactly as you'd swap the physical thing.

**The clock is the CPU board's property**, not the machine's: `SET cpu0 clock_hz=2000000`. It belongs to the card because that is where the crystal is, and because a backplane with no CPU card in it — which is what milestone 1a runs — has no clock rate to speak of.

### 3.0.1 A card may carry more than one processor, and they are units

**Settled 2026-07-11 by Patrick.** A card with an 8080 *and* an 8085 on it — switching itself, or switching when a program does an `OUT` — is a real thing. So **cores are units** (§9), of kind `Cpu`, exactly one of them active:

```
altairsim> SHOW cpu0
  unit   kind   holds
    8080   cpu    active
    8085   cpu    (idle)
```

**This needs no new bus concept whatsoever.** The card decodes the `OUT`, sets its own latch, and reports a different active core. That is structurally identical to bank switching on a memory card (§4.3) and to the Tarbell releasing PHANTOM\* on A5 (§4.2): *the board keeps its own state, and the bus arbitrates nothing.* `Machine` asks the backplane which board is the bus master and which core is live; two cards claiming it is contention and we say so; **no CPU card at all is a real machine you can build**, and it is the one milestone 1a runs.

### 3.0.2 The disassembler belongs to the instruction set — not to the CPU, and certainly not to the card

**Patrick, 2026-07-11:** an 8080 board with an onboard serial port and an 8080 board without one must be able to share the same 8080 CPU glue and the same disassembly.

So "CPU" is three things wearing one name, and they are separated:

| Layer | What it is | Lives in |
|---|---|---|
| **Instruction set** | a **stateless** disassembler: bytes in, text + length out. No registers, no state, no board. Named `"8080"`, `"8085"`, `"z80"` — a registry key, exactly like `"memory"` is for `makeBoard()`. | `src/isa/` — `disassemblerFor("8080")` |
| **Core** | registers + execute. A plain object; **not** a `Board`. | `src/cpu/` — `Cpu8080`, `Cpu8085` |
| **Card** | the thing you pull out with your hand: one or more cores, plus the serial port / boot PROM / CPU-select port that happen to be on *this* card. | `src/boards/` |

Two 8080 cards that differ only in an onboard serial port share the instruction set and the core **completely**, and differ only in the card — which is the only place they differ in reality. The thing they have in common is not the chip and not the board: it is *the way bytes decode*, and that is why it needs a name of its own to be shared by.

**A stateless disassembler runs with no CPU in the machine.** `DISASM FF00 CPU=8080` works *today*, in milestone 1a, against the DBL ROM — the same argument that made the bus testable before the CPU existed (§15), and it means the 8080 decode tables get exercised long before anything executes them.

**But naming the CPU is not the normal case, and must not be** (Patrick). The active core reports which instruction set it speaks, and `DISASM` asks the machine:

```
which = the explicit CPU= argument
      | the active core's own answer      <- THE NORMAL CASE: you never type it
      | error: "no CPU in the machine -- say CPU=8080"
```

The fallout: on the dual-CPU card above, when the guest `OUT`s to switch from the 8080 to the 8085, **`DISASM` follows automatically** — "which instruction set" and "which core is active" are the same question, and it is already being asked.

*(The word `CPU` does double duty — `DISASM ... CPU=8080` names an instruction set, while `BOARDS ADD 8080 cpu0` names a card. That is deliberate: `CPU=` is the word everyone already uses, and inventing a second one to be precise about a distinction the user does not have to care about would cost more than it buys.)*

### 3.0.3 Registers are reflection, and the debugger is a bus observer

**Registers use the same trick as `properties()` (§5).** A core exposes `registers()` — name, width, get, set — and *not* an `Regs8080` struct the monitor knows about:

```cpp
struct RegDef { const char* name; int bits; /* get, set */ };
```

Then `REGS`, `SET REG A=3F`, breakpoint conditions (`BREAK 100 IF A==0`), `SNAPSHOT`, and the MCP schema **all work for a Z80 or a 6502 the day it lands, with no monitor change.** It is the same bet that already paid for `SET`/`SHOW`/TOML/MCP: one schema, no second copy to drift.

#### 3.0.3.1 The status line: the core describes it, the monitor renders it

**Every stop prints one line, and it is DDT's** (Patrick, 2026-07-13) — because three lines is what you read when what you wanted was to *glance*:

```
C0Z1M0E1I0 A=3F B=0000 D=00FF H=8000 S=0100 IE=1 P=0102  MOV A,B
```

That layout is **not** generic — which registers pair up, which are lamps, what each is *called*, and in what order are all real differences between an 8080 and a 6502. But it is also **not** a formatter handed to the core: that would have put a `printf` where the reflection layer is, and every new ISA would owe the monitor a second copy of its register file. So `RegDef` grew a display contract instead, and the split is:

| | |
|---|---|
| **`name`** | **identity.** What you TYPE — `SET REG`, `BREAK … IF`, MCP. |
| **`label`** | **presentation.** What the status line CALLS it. |
| **`show`** | `Flag` (a lamp: label + one digit, no spaces, clustered at the front), `Field` (`LABEL=hex`, in the order the core listed it), or `Off` (reachable by name, but not on the line). |

The 8080 uses that to speak DDT exactly: the sign flag is *named* `S` and *labelled* `M` (minus); parity is `P` and prints as `E` (even); the aux carry is `AC` and prints as `I` (interdigit). `IE` is one bit but it is a **`Field`**, not a lamp — DDT had no letter for it, and interrupt state is too load-bearing to omit on a machine with a VI/RTC in it. **PC is last, and nothing may come between `P=` and the instruction**: they are the pair your eye reads together.

Two label collisions are real, and both are DDT's own: `S=` on the line is the *stack pointer* while the sign flag prints as `M`; `B=` on the line is the *BC pair* while `SET REG B=3F` is still the 8-bit half. Name is identity, label is only paint — so neither is ambiguous to the machine, only to a reader, and DDT taught that reader forty years ago.

**The pairs are not decoration.** `BC`/`DE`/`HL` are `RegDef`s in their own right, over the same bytes as the halves — so `SET REG HL=BEEF` followed by `SET REG L=01` gives `H=BE01`, and `BREAK 100 IF HL==8000` will work with no new machinery.

`bits` no longer means "flag": the renderer keys on `show`, never on width. A register narrower than a nibble prints as a number, which is what makes `IE=1` a field rather than a hex digit.

**Breakpoints and tracing are NOT CPU features.** If a core owned them, every core would reimplement them and they would differ in subtle ways. They don't belong there:

- **`BREAK IO`, `BREAK MEM R|W`, `TRACE`, `HISTORY`** are questions about **bus cycles**, and the bus already broadcasts every cycle to every board (`snoop()`, §4.2.2). The debugger watches that same stream. **CPU-agnostic, and the machinery already exists.**
- **`BREAK <addr>`** is the only CPU-flavoured one, and it is just *"PC equals X after a step"* — one comparison against a register the reflection layer already exposes.

So the debugger lives in `Machine`, drives `cpu->step(bus)`, and asks only generic questions. **An 8085 card inherits the entire debugger for free.**

### 3.1 Core semantics

- **Dispatch:** 256-entry table of function pointers or a `switch`. *Not* a bit-pattern if/elif chain (the Python prototype's linear scan).
- **T-states:** counted per instruction, with the conditional-branch fixups (CALL 17/11, RET 11/5). Unlike the Python prototype, the count is **load-bearing** — it drives `Board::tick()`, baud rate, disk rotation, and the clock throttle.
- **Flags:** `S Z 0 AC 0 P 1 CY`. Get the 8080 `ANA`/`ANI` half-carry rule right: `ac = (a|b) & 0x08`. Z80 gets the full flag set including undocumented bits 3/5 (XF/YF), `WZ`/MEMPTR, and the IX/IY prefix chains.
- **Interrupts** (absent from the Python prototype — its `EI`/`DI` set a flag nothing reads): an `INT` line raised by the bus, an `IntAck` cycle that fetches the instruction from the bus, plus Z80 IM 0/1/2 and 8085 RST 5.5/6.5/7.5 + TRAP.
- **`step()` returns an explicit `StepResult { uint32 tStates; Status status; }` — never a sentinel value.** See §16 for why (the RLC/RRC bug).

### 3.2 Validation is a gate, not a nice-to-have

The CPU is not "done" until **TST8080, 8080PRE, CPUTEST, and 8080EXM** pass (and ZEXALL/ZEXDOC for Z80).

**PASSED for the 8080, 2026-07-11.** All four, including all 25 CRC groups of 8080EXM. Suites and their period source in `tests/cpu/` (from altairclone.com); harness in `tests/cputest.cpp`; `ctest` runs the quick three, `ctest -L slow` runs 8080EXM (~9 minutes, 2.9 billion instructions).

**The harness runs the CP/M `.COM` files with no CP/M and no console card**, via a BDOS stub at `F000` written in *real 8080 machine code*, reached through the real `JMP` at `0005`, writing to a real port on a real board. Trapping `PC == 0005` in C++ would have been less code and was rejected: it would fake the `CALL`, the `RET`, the stack and the `OUT` inside the one program whose job is to check that we implement them correctly. **A validation harness may not emulate the thing it is validating.**

*The Python prototype's own notes say these were never run and that DAA is "not fully tested." That debt is not inherited: `<daa,cma,stc,cmc>` is one of the 25 groups, and it passes.*

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
    // Returns false when the board is disabled, banked away, or is honoring
    // an asserted PHANTOM* — see §4.2. Decode is STATE, not just address math.
    virtual bool     decodes(const BusCycle&) const = 0;
    virtual uint8_t  read (const BusCycle&)       = 0;
    virtual void     write(const BusCycle&)       = 0;

    // Clocked work: baud generation, disk rotation, timers. dt in T-states.
    virtual void     tick(uint64_t dt) {}

    // PHANTOM* (S-100 pin 67). A board may PULL it for a given cycle — that is
    // how a boot ROM shuts off the RAM beneath it. The bus does NOT pick an
    // overlay winner; it carries the signal and the RAM disables itself. §4.2.
    virtual bool     assertsPhantom(const BusCycle&) const { return false; }

    // Interrupts. A board PULLS pINT and holds it; the bus reads the wire and does
    // NOT invent a vector — see §4.4 and §4.4.1.
    //
    // COMBINATIONAL AND PURE, exactly like decodes(). The board announces a change
    // with intChanged(); the bus caches the wire-OR. (This was non-const, and there
    // was a section defending that. Reversed 2026-07-12 — see §4.4.1.)
    virtual bool     assertsInt() const { return false; }   // pINT (S-100 pin 73)
    virtual uint8_t  assertsVi()  const { return 0; }       // VI0..VI7, as a BITMASK
    virtual bool     watchesVi()  const { return false; }   // an 88-VI says yes
    void             intChanged();                          // "my pin moved"

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

### 4.2 PHANTOM* is a signal a board pulls — the bus does not arbitrate an overlay

**An earlier draft got this wrong in exactly the way §4.4 warns about.** It said a memory board has a "phantom mode" of `none | read | write | both`, and that a ROM board with `phantom = "read"` *"overlays the RAM beneath it for reads while writes fall through."* That describes the **bus** looking at two boards, picking a winner, and routing reads to one and writes to the other. The bus does not do that, any more than it invents an interrupt vector.

**What actually happens on the backplane:** PHANTOM\* is **S-100 pin 67**, a line any board may pull low. A boot ROM pulls it; memory boards that are strapped to honor it **disable their own drivers** while it is low. Nobody arbitrates. The overlay is *emergent* — the ROM is the only board still answering, because the RAM switched itself off.

So the model is two ordinary board behaviors, and no bus special case:

```cpp
// Pull PHANTOM* for the cycles I mean to shadow. A board strap:
//   phantom = none | read | all      (see docs/boards/s100-memory.md)
bool MemoryBoard::assertsPhantom(const BusCycle& c) const {
    const Region* r = owner(c.addr);
    if (!r || r->kind != Rom) return false;          // only my ROM shadows anything
    return phantom_ == All || (phantom_ == Read && c.type == Cycle::MemRead);
}

// If SOMEONE ELSE pulls PHANTOM* and I honor it, I am not here.
bool MemoryBoard::decodes(const BusCycle& c) const {
    // `!assertsPhantom(c)` is load-bearing: a ROM card pulls PHANTOM* to shadow
    // the RAM under it, and a card must not switch itself off with a signal it is
    // ITSELF driving -- or nobody drives the address and the ROM reads back FF.
    if (c.phantom && honorsPhantom_ && !assertsPhantom(c)) return false;
    const Region* r = owner(c.addr);
    if (!r) return false;                            // unpopulated page / empty socket
    if (c.type == Cycle::MemWrite && r->kind == Rom) return false;   // ROM never answers a write
    return true;
}
```

> **That `!assertsPhantom(c)` clause was missing from the first draft of this document, and it was a real bug** — caught by the acceptance test for the *default* configuration, which is the one everybody would have used. It is worth keeping the scar visible: the board still answers only questions about *itself* (its own output pin), so the rule is board-local and the bus is still not involved. The fix is one clause, in the board, where it belongs.

The bus's whole job is a **three-pass cycle**: ask every board whether it pulls PHANTOM\*, set `BusCycle::phantom`, run the decode, and then **show the finished cycle to every board** (`snoop()`, §4.2.2). Carrying a signal, running a decode, and letting the cards watch. No decisions.

**A ROM does not decode a write.** It does not reject the write, or ignore it, or log it — it never answers the cycle. Everything else follows from that and needs no rule of its own:

| What else covers the address | ROM's `phantom` strap | The write |
|---|---|---|
| Nothing | any | Nobody latches it. It is gone. |
| Another card's RAM | `read` | **Lands in that RAM.** Reads still come from ROM — shadow-RAM. This is the **Tarbell** boot PROM writing the sector it is loading into the RAM it is sitting on top of. |
| Another card's RAM | `all` | That card honors PHANTOM\* and switches off for writes too. The write vanishes. |

**PHANTOM\* is a LEVEL, and the read/write distinction lives on the *honoring* board.** A card that shadows another asserts the pin and **holds** it, like an interrupt, until something releases it — it does **not** gate it with the read strobe, and it has no opinion about writes. The memory card's jumper is therefore `honors_phantom = none | read | all`, **not a bool**: `read` means *stop answering reads, keep answering writes*, so the byte lands in the RAM under the shadow. That is what lets a Tarbell bootstrap write the sector it is loading into the very RAM its own PROM is covering.

> **Corrected 2026-07-12 by Patrick, from the Tarbell schematic.** This paragraph previously said the reverse in bold — that the gate lived on the asserting card and that `honors_phantom` "must never grow a `read` mode." It was **reasoned, not sourced**, which is the exact failure §0.1 exists to prevent. And the Tarbell's own documentation, quoted in `docs/boards/s100-memory.md`, had said so all along: *"the memory boards installed in the system must allow writes to their RAM, but not reads."* **The memory boards.**

Payoffs, which is how you know it is right:
- **"Writes fall through" is no longer a special rule.** It is what happens when a ROM asserts PHANTOM\* on reads and not on writes. Shadowing *both* is equally expressible, and neither is a case in the bus.
- **`honors_phantom` is a board strap, because on real hardware it is a jumper.** A memory board that ignores PHANTOM\* keeps driving, you get contention (§4.6), and the simulator reports the same bug the real backplane would have handed you.
- **The operator can still write ROM, and the guest still cannot** — because `LOAD … ROM` (§10.2) reaches behind the bus into the board's store. Burning a PROM is not a bus operation on real hardware either; you pull the chip. Model it as a bus write and the bus would have to know *who originated a cycle*, which a real backplane cannot know and no board should ever have to ask.
- **Any new board can shadow memory** without the bus learning about it.

### 4.2.1 Boards enable and disable — including ROMs that vanish after boot

**Decode is state, not address arithmetic.** A memory board is not merely "at 0000–1FFF"; it is at 0000–1FFF *when it is enabled*, on *the live bank*, and *when PHANTOM\* is not shutting it off.

This is not a corner case — it is how S-100 machines boot. **A boot ROM commonly appears at low memory on reset, runs, and is then switched out so the RAM underneath becomes visible**, often by the boot code writing to the ROM board's own I/O port as nearly its last act. The address the ROM occupied is *supposed* to become RAM.

So a memory board needs to be able to turn itself on and off, and there are exactly three honest ways it happens — all of which are ordinary board behavior:

| Trigger | Mechanism |
|---|---|
| **The guest writes to the board's port** | An ordinary `IoOut` the board decodes, mutating its own decode state. The boot ROM disabling itself. **Not a bus special case** — same shape as bank select (§4.3). |
| **Reset** | `reset(PowerOn)` restores the power-up state — which for a boot ROM means **enabled again**, because that is the point of a boot ROM. `reset(Bus)` may or may not, and the board's `.md` must say **concretely** which (§6). Get this wrong and you get the classic "boots from power-on but not from the reset button." |
| **The operator** | `SET mem1 ENABLED=OFF` — a runtime `properties()` value like any other, for debugging. |

`enabled` is therefore a **standard runtime property on every memory board**, and `SHOW BUS MAP` must show it, because a board that is present but decoding nothing is otherwise invisible and maddening:

```
altairsim> SHOW BUS MAP
  range        board    type    state                             notes
  ---------------------------------------------------------------------------------
  0000-BFFF    mem0     memory  enabled, ram, bank 0/4, honors PHANTOM*
  F000-F7FF    mem1     memory  enabled, rom, asserts PHANTOM* on all
  FF00-FFFF    mem1     memory  enabled, rom, asserts PHANTOM* on all
  C000-EFFF    —        —       unpopulated                       reads FF
```

Note that one board occupies **two disjoint ranges**, because a real card carries several populated regions and empty sockets between them (`docs/boards/s100-memory.md`). The map is per-*range*, not per-board.

> **Open — needs a manual, per §0.1.** *Which* boards disable themselves, at *which* port, with *what* bit? That is board-specific and I will not guess it. The mechanism above is general and costs nothing; the specific straps get filled in per board as the manuals arrive. **Patrick: which board do you want first?**

### 4.2.2 `snoop()` — every board sees every cycle

**The address bus is not addressed to anyone.** It is simply present, and any card may watch it whether or not it answers. So `Board` has a third bus method, called **once per completed cycle, on every board**:

```cpp
virtual bool assertsPhantom(const BusCycle&) const;   // COMBINATIONAL. Pure.
virtual bool decodes(const BusCycle&) const;          // COMBINATIONAL. Pure.
virtual void snoop(const BusCycle&);                  // CLOCKED. Latch here, and only here.
```

The first two are called **several times per cycle** — once to resolve PHANTOM\*, again inside each board's own decode — so they must have no side effects. `snoop()` is the clocked half, and it is the **only** place a board may latch what it saw.

**The card that forced this is the Tarbell** (`docs/boards/tarbell-sd.md`). Its 32-byte boot PROM shadows RAM from POC\*, and it releases PHANTOM\* permanently the first time it sees a memory read with **A5 high**. Both halves are load-bearing:

- The release must be **combinational**, because the bootstrap's own first fetch out of the PROM *is* the read with A5 high. If the release waited a cycle, that fetch would happen while memory was still shadowed, read `0xFF` off the floating bus, and no Tarbell would ever have booted.
- The release must also **latch**, or a later data read below `0x20` would re-shadow the PROM over the sector just loaded there.

> **⛔ AND THE TARBELL IS NOT BUILT (Patrick, 2026-07-12).** It is **deferred**, and `docs/boards/tarbell-sd.md` says why: the real card is a **pre-IEEE-696, July 1977** design that has **no PHANTOM\* at all** — it asserts **STATUS DISABLE\*** and drives the S-100 status lines itself. Modelling that honestly would mean building status lines *in order to have something to disable*.
>
> So `snoop()`, `wantsSnoop()` and `decodeIsPageUniform()` have **no shipping board that overrides them** — only `TarbellBoot`, a fixture in `tests/test_phantom.cpp`. That is stated here rather than left to be discovered, and `board.h` argues the case for keeping them: they are **specified, sourced and executed**, which is not the same as speculative. The rule this project applies elsewhere (`disk.h`: *"a virtual left in place for a possibility the owner has **ruled out** is a hook that will never be pulled"*) killed the IMD/TD0 virtuals because IMD was ruled **out**. The Tarbell is ruled **later**. If that ever changes, delete both hooks and `test_phantom.cpp` with them — and not before, because they are the only executable description we have of a card that shadows low memory, and the combinational-release trap above cost a day to find the first time.
>
> **PHANTOM\* itself is unaffected and stays.** It is not Tarbell-only: the memory board asserts it for an ordinary ROM-shadows-RAM card, and `tests/test_memory.cpp` covers all three straps.

The bus does not know any of this happened. It is one flip-flop, on one card, and that is the point: a real backplane has no "notify" mechanism, so neither does this one — `snoop()` is not a callback, it is a card looking at wires that were in front of it the whole time.

**And the debugger watches the same stream from outside the backplane.** `Bus::observe()` hands a callback the very cycles the cards see, and that is the entire implementation of `BREAK IO`, `BREAK MEM`, `TRACE` and `HISTORY` (§3.0.3). They are therefore **not CPU features**, they cost the cores nothing, they work on any processor the day it lands — and they catch a **DMA** transfer or a front-panel `DEPOSIT` just as readily as a `MOV`, because a cycle is a cycle no matter who originated it.

An observer is **armed only while the machine is running.** The monitor's own `DUMP` and `DEPOSIT` are real bus cycles — that is the point of them — so an always-armed `BREAK MEM W` would "fire" on the operator's own `DEPOSIT`, with no program running to stop and nothing sensible to report.

### 4.3 Banking — the strongest evidence in this document that boards must own their decode

A banked memory board registers a bank-select I/O port; a guest write selects which plane is live. This is an ordinary `IoOut` that mutates the board's **own** state — **not** a bus special case, and the same mechanism as §4.2.1's enable/disable.

**What a bank select *does* is the board's business, and this document states no rule about it.** On the five cards below it swaps the 64K plane. On some other card it might not. The bus carries the `OUT` and the board decides — that is the entire point.

If you are ever tempted to hoist banking into the bus or the monitor, read this table first. These are five **real** cards (`docs/boards/s100-memory.md`, sourced from `s100_bram.c`):

| Card | Port | Banks | The data written selects the bank how? |
|---|---|---|---|
| SD Systems ExpandoRAM | **0xFF** | 8 | **Binary** — the byte *is* the bank number |
| Vector Graphic | **0x40** | 8 | **One-hot** — `0x01`→0, `0x02`→1, `0x04`→2, … `0x80`→7 |
| Cromemco | **0x40** | **7** | One-hot, masked `& 0x7F` — **bit 7 is not a bank select** |
| North Star Horizon | **0xC0** | 16 | **Binary** |
| AB Digital Design B810 | **0x40** | 16 | **Binary** |

**Three ports. Two encodings. One card with seven banks.** `OUT 40,04` means *bank 4* on an ExpandoRAM and *bank 2* on a Vector. And the Vector additionally decodes `0x41`/`0x42` as banks 0/1 — not by design, but because **OASIS writes those values** and the card tolerates them; miss it and OASIS does not boot.

No generic "bank number" abstraction survives that table. Any `BANK=<n>` in the monitor (§10.2) or any bank arbitration in the bus would have to pick one card's convention and be silently wrong about the rest. **The board owns its decode, or the model is a lie.**

One more thing the table gives us for free: **three of the five cards sit on port 0x40**, so two of them in one machine is a genuine I/O collision — which §4.6's contention detector catches and names, exactly as a real backplane would have.

### 4.4 Two interrupt models — both required

**(a) Un-vectored: `pINT` alone, no VI board.** A board pulls `pINT` (S-100 pin 73). The CPU finishes the current instruction and, if interrupts are enabled, runs an **`IntAck` bus cycle** (`sINTA`). During that cycle the interrupting board may *drive the data bus*, and whatever it drives is **executed as an instruction** — conventionally an `RST n`. **If no board drives the bus, it floats high and the CPU reads `0xFF` = `RST 7` (0x38).** That is not a fallback hack; it is how the machine actually behaves, and it is why the PMMI's factory jumper (E10→E9, straight to pin 73) yields RST 7 with no vector logic at all. Model the floating bus honestly and this falls out for free.

**(b) Vectored: VI0–VI7, with an 88-VI board.** Eight prioritized request lines, VI0 highest. **The bus does not arbitrate these — the 88-VI board does.** The VI board watches the eight lines, applies priority and its own mask, drives `pINT` itself, then claims the `IntAck` cycle and drives the corresponding `RST n` onto the data bus.

**Consequence for the API:** the bus must *not* pick a winner and ask it for a vector. That is the VI board's job; building it into the bus would make the VI board unimplementable as a board and would fabricate vectored behavior on a machine that has no VI card in it. The bus does exactly three things:

1. carry `pINT` as the wire-OR of every board asserting it,
2. carry VI0–VI7 as eight signals boards can assert and other boards can observe,
3. run an `IntAck` cycle that boards claim like any other bus cycle.

Everything else is a board. The 88-VI is then just another board with no special privileges — and so is any *new* interrupt controller you invent, which is the point of the project.

A board declares its jumpering as a property: `interrupt = none | int | vi0 … vi7`. An 88-2SIO with `interrupt = int` gives RST 7 with no VI board present; the same board with `interrupt = vi2` in a machine containing an 88-VI gives `RST 2`.

**Both halves are now BUILT** — see `docs/boards/mits-88virtc.md`, `machines/ps2int.toml`, and the MITS manual in `reference/88-VI-RTC.pdf`. The design above survived contact with the hardware unchanged: the 88-VI needed no new bus concept beyond the eight wires, because `IntAck` was already a cycle any board could claim.

#### 4.4.1 The board **pulls** pin 73. The bus does not go and ask.

**Corrected 2026-07-12 by Patrick, and this reverses what this section used to say:**

> "Does the bus poll each board for interrupt status, or does a board set interrupt flags that the bus simply checks? **In a real system, the bus doesn't poll a board for interrupt status. The board sets high/low signals on the bus that the CPU reads from the bus. The board then clears the int signal based on its design.**"

He was describing the hardware. He was also describing a bug.

`Bus::intPending()` used to walk the backplane and ask every card `assertsInt()`, **once per instruction** — sixty million times a second, to compute a boolean that changes perhaps a thousand times a second on a busy machine. Once the decode was cached (§4.7) it was the single largest per-instruction cost left in the simulator, and it grew with every card you added: **6.6 ns with two boards, 15.6 ns with six.**

**The interface now is the exact analogue of the decode**, and the two hard-won rules turn out to be one rule:

| | combinational, pure | "it moved" | the bus |
|---|---|---|---|
| address decode | `decodes()` | `decodeChanged()` | caches a page table |
| interrupt | `assertsInt() const` | `intChanged()` | caches a wire-OR count |

> **A board's outputs are pure functions of its state. When its state changes, it says so. The bus caches the rest.**

Reading pin 73 is now an integer test — **1.8 ns, flat in the number of cards in the machine.**

##### What the poll was secretly doing, and where it went

This section previously argued — at length, and correctly, as far as it went — that `assertsInt()` **could not be `const`**, because a board may have to *do something* to answer honestly. A 6850 has to notice that a character has finished arriving, which happens **on the chip's own clock**, with no help from the CPU and no bus cycle involved. The bug that established it is real and is still worth reading:

> **An interrupt-driven driver never reads the status port. Not reading it is the entire point of being interrupt-driven.** So `RDRF` was never set, IRQ never rose, the interrupt never fired, and the operator could type forever with nothing happening.

All of that is true. The mistake was the conclusion. **The card's free-running work is real; making a query do it was not.** The receiver was being advanced inside `assertsInt()` for one reason only: *being asked was the only thing that ever woke the card up.* The poll was serving as the card's clock.

So the work moved to where it belongs, and `assertsInt()` became `const` and pure:

- a **deadline** the card sets for itself (`Clock::at`, §7.5) — for anything emulated time can predict, such as the transmit register draining;
- **`pump()`** — for anything it cannot, such as a human touching a key.

**And that made the model *more* capable, not less.** With the poll, the card could only ever act at a moment when someone was already asking it something. A guest that jumpers the transmit interrupt, sends a character and halts — an entirely ordinary driver — was **declared finished by the run loop**, two thousand T-states before the interrupt it was waiting for. Nothing was pulling pin 73 *yet*, and the old `HLT` check could not tell "nobody is interrupting" apart from "nothing ever will." A card that owns a deadline can. See `tests/test_sio2.cpp` — *"NOBODY IS ASKING: the card acts on its own deadline."*

##### The stale wire is not left to trust

A board that moves its interrupt pin and forgets to call `intChanged()` hangs the guest forever, waiting for an interrupt that already happened — and presents as *"the emulator locks up sometimes"*, which is worth a week of anyone's life. So it is checked, exactly as the decode cache is: **`Bus::setVerify(true)`** re-derives the whole wire from every board's `assertsInt()` on every instruction and aborts on the first disagreement. The unit suites run with it on permanently; the CPU validation gate runs with it under `ALTAIR_VERIFY=1`, over 2.9 billion instructions.

#### 4.4.2 `assertsVi()` is a **bitmask**, not a level — and that is a correction

The sketch in §4.2 originally read `virtual int assertsVi() const { return -1; }` — one level, or nothing. **That is wrong, and the 88-SIO is the counterexample.** It has *two* interrupt straps, one for its input device and one for its output device, and the manual is explicit that they may sit at **different VI priorities**. Both can be asking in the same instant — a character has arrived *and* the transmitter has gone empty — so the card is pulling two of the eight wires at once. An `int` return could only ever have reported one of them, and would have dropped the other silently, and *only* in the case where both fired. That is the worst way to lose an interrupt: rare, timing-dependent, and invisible.

So the wire is what the backplane says it is — **eight independent lines** — and a board reports the subset it is pulling:

```cpp
virtual uint8_t assertsVi() const { return 0; }   // bit n = pulling VIn
virtual bool    watchesVi() const { return false; }
```

`watchesVi()` is the other half, and it is not decoration. A card that *pulls* a VI line announces it with `intChanged()`; the card *watching* those lines is a third party who was told nothing, and whose own `pINT` has just gone stale. The bus therefore calls `intChanged()` on each watcher when a VI line actually moves. It is opt-in because otherwise it is a virtual call to every board in the backplane on every keystroke.

`Bus::setVerify(true)` covers all nine wires, not just pin 73: it re-derives the eight VI lines from every board's `assertsVi()` too, and aborts the same way. A stale VI line hangs the guest exactly as a stale `pINT` does, and is even harder to see.

### 4.5 DMA is bus mastering

A board asserts `pHOLD`; the bus grants (`pHLDA`) at the next instruction boundary; the board then acts as a `BusMaster` — the same interface the CPU uses — and drives its own cycles. Stolen T-states are charged to the clock, so the CPU genuinely loses time rather than getting DMA for free. When the board releases, the CPU resumes mastering.

**Built, 2026-07-16.** `Board::requestsBus()`/`busMaster()` (the pHOLD line and who drives once granted), and `Debugger::serviceDma()` in the run loop, which offers the bus at each instruction boundary — the exact analogue of interrupt sampling, and for the same reason: the CPU never stops mid-instruction, so a grant happens *between* instructions and not inside one. The stolen T-states are charged to the clock right there, which is the whole of "the CPU genuinely loses time." Proven by a real memory-to-memory master in `tests/test_dma.cpp` — genuine RAM→RAM bus cycles, the exact 128-T-state theft read back out of `clock.now()`, and a `BREAK MEM W` that fires on the DMA's own write. **No shipping DMA board yet:** the Dazzler (§7.3) is the concrete one and it is blocked on the SDL `Display` service, not on the bus — which is the point. The mechanism landed first and the board will be additive, exactly as the 88-VI was additive onto the `IntAck` cycle (§4.4).

Three decisions this baked in, each a place the obvious thing is wrong:

- **The DMA board *has* a `BusMaster`; it does not *inherit* one.** The CPU card `: public Board, public BusMaster` because it is the permanent master (§3), and `Machine::master()`/`masters()` find it with a `dynamic_cast<BusMaster*>` — which is *also* how the monitor lists the machine's processors. A DMA board that inherited `BusMaster` would be counted as a CPU and stepped as one. So a transient master is reached **only** through `Board::busMaster()`, under `pHOLD`, and never shows up in the CPU walk.
- **Slot order is the priority — there is no arbitration register.** `Bus::boards()` is backplane order and the first board still pulling `pHOLD` wins. That *is* the S-100 daisy chain; inventing a priority latch on the bus would be the §4.4 PHANTOM\*/VI mistake in a new place.
- **Burst vs. cycle-steal is the board's choice, and the run loop has no flag for it.** `serviceDma()` drives a granted board *while it keeps* `requestsBus()` true. A burst controller holds the line for the whole block and drains in one grant; a cycle-stealer drops it after one transfer and re-arms a `Clock` deadline, so the CPU runs an instruction before the next grant. The two fall out of one loop with no mode anywhere — and the machine's existing "does it have a future?" halt test (`clock.queued() != 0`) already keeps a halted CPU alive while a periodic DMA re-arm is pending, so a transfer runs through a `HLT` for free, through the deadline queue that was already there.

### 4.6 Contention

When two boards `decodes()` the same cycle, log a diagnostic naming both board `id`s and the address, and return the wire-AND of the drivers (real tri-state / open-collector contention pulls low). This is a **feature** — it is the bug you'd chase on a real backplane, surfaced immediately.

`SET BUS CONTENTION=WARN|ERROR|SILENT`.

**A PHANTOM\* shadow is not contention, and must not be reported as such.** Two boards *registered* at one address is normal and correct — a boot ROM over RAM is the whole point of §4.2. Contention is when two boards **both actually `decodes()`** the same cycle, and under PHANTOM\* the RAM returns `false`, so only one board answers. The distinction falls out of the model for free: contention is decided *after* the phantom pass, on who is really driving.

The failure that *does* deserve a warning is a memory board strapped **not** to honor PHANTOM\* sitting under a ROM that asserts it. Then both really do drive, and you get the real bug the real backplane would have handed you.

### 4.6.1 The floating bus — one rule, three consequences

> **If no board drives the bus, it floats high. Every read of an unmapped address or port returns `0xFF`. Writes go nowhere.**

This is a single rule, and three things the design would otherwise need special cases for fall straight out of it:

| Situation | What happens | Why it matters |
|---|---|---|
| **Unpopulated memory** | Reads `0xFF` | Period software **sizes memory by reading**. Return `0x00` instead and every machine looks like it has 64K, and CP/M builds itself wrong. A zero-filled hole also disassembles as a field of `NOP`s, which is a uniquely confusing thing to stare at. |
| **Unmapped I/O port** | Reads `0xFF` | A guest probing for a board that isn't there gets the same answer real hardware gives it. |
| **`IntAck` with no board driving** | Reads `0xFF` = **`RST 7`** | §4.4. Not a fallback hack — it is *why* the PMMI's factory jumper straight to pin 73 gives RST 7 with no vector logic anywhere. |

Model the floating bus honestly once and all three are free. Fake any one of them and you will fake the other two differently.

#### `0xFF` belongs to the bus, and to nothing else

> **Providing `0xFF` when no board answered is the ONLY thing the bus does that a board does not.** That is the entire bus/board overlap, and it stays that size. — *Patrick, 2026-07-11*

So **no board may ever manufacture `0xFF`**, and in particular **no board may seed its own store with it.** A RAM chip does not power up holding `0xFF`; it powers up holding whatever it feels like, which is what `fill = random` is for, and *what a card's chips contain is that card's business* (`docs/boards/s100-memory.md`). The bus does not initialize anyone's memory and has never heard of `fill`.

The failure this prevents is a quiet one, and it was in this code until Patrick caught it. Seed a board's store with `0xFF` — it is the obvious "uninitialized" filler — and `DUMP` shows `FF` for a card whose RAM is fine, `FF` for a card whose RAM was never filled, and `FF` for a card that **isn't in the machine**. One symptom, three causes, and you chase the wrong one. The moment a board can produce `0xFF`, the single signal the bus has stops being a signal.

`tests/test_boundary.cpp` enforces it. Shared *helpers* that several boards call are fine — that is code reuse, not the bus growing opinions.

**But silence is how an unimplemented device becomes an unexplained hang.** The behavior above is correct and must stay; the *diagnostic* is separate. `SET BUS UNCLAIMED=WARN|ERROR|SILENT` (default `WARN`) names the port and the PC:

```
warning: OUT 0FE <- 01 at PC=0113: no board decodes port 0xFE. reads float to 0xFF.
```

Memory reads are **not** warned by default — guests scan memory constantly, and it is normal.

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
    Getter       get;
    Setter       set;          // returns an error string on reject
};
```

Consequences, all of which must be stated in the doc:
- `SET <id> <k>=<v>` and `SHOW <id>` are **fully generic**. `SHOW` prints every property with value, units and legal range.
- **MCP tool schemas are generated from `properties()`** — Claude gets typed, constrained, self-documenting board config instead of guessing at free text.
- **The TOML loader and `CONFIG SAVE` are the same code path.** A board's config keys *are* its properties, so round-tripping is automatic and cannot drift.
- **Tab completion is generated from `properties()`** too (§10.4).
- **A LIST of things is a sub-unit, and it round-trips the same way** — regions on a memory card, drives on a controller. `subUnitTables()` + `addSubUnit()` read them; **`subUnits()` writes them back** (built 2026-07-12). The board renders its own text, because only the board knows that an address is hex and zero-padded (`at = 0x0400`) while a size is decimal with a suffix (`size = "48K"`) — `Value::text(16)` produces neither, and a writer that guessed would be a second, worse copy of what the board already knows. The claim "`subUnits()` is `addSubUnit()`'s inverse" is therefore one a test can simply *execute*: render, feed it straight back in, compare.

  **This closed the last board-specific line in the config layer.** `CONFIG SAVE` used to reach for a `dynamic_cast<MemoryBoard*>` to write `[[board.region]]`, which meant any *other* board with a sub-unit table — a disk controller with four `[[board.drive]]` entries — would **load and silently not save**. You would configure the machine, save it, and get a controller with no drives. `src/config/toml.cpp` now includes no board header at all, and it should never include one again.

- **…and a sub-unit's KEYS are declared, like everything else** — `subUnitProperties(table)` (built 2026-07-14). **This was the one place the claim above was false, and it was the worst possible place: the keys that carry the disk, the ROM and the write-protect tab.** `unit`, `mount`, `readonly`, `media`, `type`, `at`, `size` were known to nothing but a chain of string compares inside each board — so they appeared in no generated reference, no MCP schema and no `SHOW`, and each board hand-wrote its own validation and its own `else { err = "no such key"; }`. That is a **second schema**, written four times, agreeing with the documentation by luck. It was found the way such things always are: Patrick asked me to file a read-only disk mount as a *missing feature*. It was not missing. It had worked for weeks. It was **undiscoverable**, and the reason it was undiscoverable is that nothing in the program could enumerate it.

  A sub-unit key is an ordinary `Property` with **no `get` and no `set`** — the half that is a *description* (kind, choices, range, radix, help) without the half that is an *accessor*, because the drive it describes **does not exist yet**. That is exactly the half a validator, a schema and a documentation generator need, which is why there is no second struct. `Board::loadSubUnit()` is the **one door** — it checks the table, the keys and the values against that declaration and only then lets the board build — and `addSubUnit()` is now **protected**, so the TOML loader, `REGION ADD` and the tests cannot go around it. What is left inside a board is *construction*, plus the one thing a schema cannot express: that `type` is **required**, that a drive needs a `unit`.
- **THERE IS NO CONFIG-TIME-ONLY PROPERTY** (Patrick, 2026-07-12). Every property can be set, always. There was a `runtime` flag here that rejected a SET while the machine ran; it is gone, for two reasons and the second is the real one:
  - **You can only type at the prompt when the machine is STOPPED** — by ATTN, by a breakpoint, by a HLT. That is the front panel's STOP switch. There is no moment at which a `SET` races a running CPU.
  - **On real hardware the rule would be a fiction anyway.** A card being worked on sits on an **extender**, out where you can reach it, and its jumpers get moved with the power on. That is ordinary practice on real boards, not an abuse of them (Patrick, 2026-07-12).

  It was also never enforced: nothing in the simulator ever set the flag the gate was conditioned on, so it had never once fired. A rule the code only pretends to enforce is worse than no rule.

### 5.4 A card can bring a VERB with it (built 2026-07-12)

**`REWIND` should exist when there is a cassette in the machine, and not otherwise.** Putting it in the static command table would mean a verb that is always spelled and never usable on most machines — and the monitor would have to know what a tape *is*, which is exactly the knowledge §7.7 keeps out of it.

```cpp
virtual std::vector<CommandDef> commands() const { return {}; }   // a static table; empty for most
virtual bool runCommand(const std::string& name, const std::vector<std::string>& args,
                        std::ostream& out, std::string& err);
```

`CommandDef` moved from `cli/commands.h` down to **`core/command.h`**, and that move is the whole layering argument: a board may declare a verb, and **a board must never include the CLI**. The dependency runs `cli → core`, one way. The *table* of built-in commands stays up in `cli/`, where it is the monitor's business and nothing in `core` has an opinion about it.

**THE STATIC MENU ALWAYS WINS.** The monitor prefix-resolves against the built-in table first, in its existing priority order, by code that has never heard of boards. Only when *nothing* built-in matches does it ask the cards. So **no card can shorten, shadow or destabilize a built-in abbreviation by being plugged in**: `D` is DUMP and `RE` is REGS on every machine ever booted, whatever is in the slots. `REWIND` is reachable at **`REW`** precisely because the built-ins already own `R` (RUN), `RE` (REGS) and `RES` (RESET) — and `REW` is the first prefix none of them claims.

That ordering has a price, and it is paid in the right place. A card *can* declare a verb **nobody can ever type** — one whose every prefix a built-in claims first. **A user cannot create that; only a board author can**, so it is caught as a **merge gate** in `tests/test_cli.cpp` (over every type in the registry), not at runtime where the error message would be about a word somebody typed rather than about the card that is wrong. This is the same shape as the older invariant it sits next to — *no command name may be a strict prefix of another* — which is also a test and not a runtime check.

**A board verb's first argument names the board** (`<id>` or `<id>:<unit>`), read exactly as MOUNT and CONNECT read it. It has to: two 88-ACRs both declare `REWIND`, and the verb alone cannot say which tape to wind. The monitor enforces that convention once, so no board reimplements it.

---

## 6. RESET semantics

The Altair bus has two distinct reset lines, and boards must honor both. Conflating them is the classic source of "works from power-on but not from the reset button" bugs.

```cpp
enum class Reset {
    PowerOn,   // POC* (pin 76) — power-on clear. What it does is BOARD-SPECIFIC;
               // the board just needs to know it happened. Nothing in software can
               // assert it — only the power supply does.
    Bus,       // RESET* — the front-panel reset button. Warm. CPU PC<-0, boards reset
               // their logic, but memory contents SURVIVE and mounted media / connected
               // streams stay attached.
};
```

**Neither reset clears memory. Only removing power does.** This is not a nuance, it is the rule, and the memory array is the proof: a RAM chip has no POC\* pin. Its contents are indeterminate at power-up because *the chips just powered up*, not because a signal arrived. POC\* and power coming up coincide on real hardware — which is why they share the `POWER` command — but they are different things, and a board that clears its store in its `Reset::PowerOn` handler is modeling a machine nobody built. Model the fill as belonging to **power**, and let both resets leave the store alone.

Get this backwards and it shows up as *"my program vanished when I hit reset"* — which reads like a memory-model bug rather than a reset bug, and costs you a day.

### 6.1 A bus reset does what the board does — and if the board does nothing, it does nothing.

The vocabulary, because two of these get called "reset" and they are not the same signal:

- **Bus reset** — `RESET*`, the front-panel RESET switch. `Reset::Bus`.
- **Power-on-clear** — POC\*, the delayed clear that comes up with the power supply. `Reset::PowerOn`. Nothing in software can assert it.
- **Software reset** — whatever the *guest* writes to a chip to reset it. Not a bus signal at all, and not this section's business — except to say it is **modeled exactly**, because guest software can see, time and depend on every part of it. The 6850's master reset is the worked example: writing `11` into the divide field *latches*, holds the chip down, and inhibits TDRE until a second control write releases it, which is why every 6850 driver does two `OUT`s and why the card is dead after only the first. Round that corner and period software mysteriously half-works.

**The rule for the two bus signals: a board does on a reset exactly what the real board does — and a board that does nothing does nothing.** It is tempting to have every card scrub itself clean on `RESET*` because it feels safe, and it is exactly backwards: a card that resets more of itself than the hardware's reset line physically reaches is inventing a machine nobody built, and the invention is *destructive*. The 88-2SIO is the case that proves it. The MC6850 **has no RESET pin** — 24 pins, and RESET is not among them — so `RESET*` reaches that card's address decoding and nothing else. This tree used to reset both ACIAs on `Bus` anyway, which threw away the guest's word format and interrupt enables *and ate a byte out of the receive register*, on a card where a real bus reset would have preserved all of it.

So: **what a board does on each of the two signals is a fact about the board, it comes from the manual and the data sheet like every other fact (§0.1), and the board's `.md` must state it concretely.**

**What we do *not* model is the exact timing or the internal sequencing of power-on-clear.** A real 6850 comes up held in an internal reset that is only released by the guest's first master reset; we don't reproduce that, because nothing can observe it that does not also program the chip. `Reset::PowerOn` simply leaves every card in a known good state **immediately**, so that a machine is usable the moment it is switched on. That is the one place a bus reset is allowed to be pragmatic rather than literal.

**What POC\* does is board-specific**, and each board's `.md` must say **concretely** what each reset does to it. Examples:
- **`memory`**: both resets clear the bank-select latch to 0 and touch nothing else. `POWER` re-fills RAM regions per `fill` and re-reads ROM regions from their files. See `docs/boards/s100-memory.md`.
- **A boot ROM that disables itself** (§4.2.1): `PowerOn` **re-enables it** — otherwise the machine boots exactly once and never again. Whether `Bus` (the front-panel reset button) also re-enables it is a **board-specific strap, and the board's `.md` must say which.** This is the single most likely place to produce the classic "works from power-on, dead from the reset button" bug, because a warm reset that leaves the ROM switched out drops the CPU onto RAM at 0000 and it executes garbage.
- **88-2SIO**: `Bus` does **nothing at all** to the two 6850s — the chip has no reset pin, so `RESET*` reaches the card's decode logic and stops there. `PowerOn` puts each chip in a known good state (`Mc6850::powerOn` — clears RDRF, zeroes the control register, asserts RTS) and **keeps the `ByteStream` connected**. Neither is the 6850's *master* reset, which is the guest's, arrives as a control byte, and does not clear the control register. See §6.1.
- **88-DCDD**: deselects all drives, unloads the head, invalidates the sector counter on both — but **keeps images mounted**, and does *not* seek to track 0 on a warm reset (real drives don't).
- A DMA board must release the bus.
- CPU: PC←0, interrupts disabled; Z80 also `I`/`R`←0 and IM 0.

CLI:
```
RESET        RESET* / pRESET — "press the reset button". Warm; memory survives.
RESET CPU    CPU only; boards untouched (a debugging convenience, not a real signal).
POWER        Power-cycle: RAM contents are lost, ROM images are re-read, and POC* is
             pulsed. The only thing that clears memory.
```

`RESET` must be safe at any point — the bus asserts it at the next instruction boundary, never mid-cycle.

---

## 7. Host services layer

Boards must **never** touch a socket, a file handle, or `termios` directly. Everything a board needs from the host goes through a small set of generic, platform-abstracted services implemented once in `platform/`. This is what keeps the four targets honest and keeps replay deterministic — if a board can read the host clock or a socket on its own, replay is dead.

### 7.1 `ByteStream` — the generic serial endpoint

**Built, 2026-07-11** (`src/host/stream.h`). **Sockets, real serial ports and modem control landed 2026-07-12.** Implemented: `NullStream`, `LoopbackStream`, `ScriptedStream`, `Console`, **`TcpListenStream`, `TcpConnectStream` (`src/host/tcp.cpp`), `HostSerialStream` (`src/host/hostserial.cpp`)**. Still to come: `FileStream`, `ReplayStream`.

Every board that moves characters (88-SIO, 88-2SIO, 88-ACR, 88-LPC, paper tape, PMMI) talks only to this. `CONNECT` binds an implementation to a unit.

#### The line goes BOTH WAYS (2026-07-12)

`LineStatus` was carrier/CTS/DSR and **input-only**, so RTS was decoded out of the 6850's control register and dropped on the floor — there was nowhere for it to go. Now:

```cpp
struct LineStatus  { bool carrier, cts, dsr, ring; };   // INPUTS  -- the far end drives these
struct LineControl { bool rts, dtr, brk; };              // OUTPUTS -- the CARD drives these
struct LineParams  { long long baud; int dataBits, stopBits; LineParity parity; };

virtual LineStatus status() const;
virtual void setControl(const LineControl&);
virtual bool setParams(const LineParams&, std::string& err);   // false -> the card says so out loud
```

**`true` is ASSERTED, everywhere, in both structs.** The pin-level inversions are real — the 6850's are `/DCD` and `/CTS`, active low — but they are a fact about *that chip's pins* and they stay inside the chip that has them. A stream that exported one chip's polarity would make the 88-SIO wrong for free.

**The stream reports LEVELS; the chip latches EDGES.** The same division as PHANTOM\*: the wire carries a level, and the honoring card decides what it means. The stream says *"carrier is down"* and says it for as long as it is down; the **6850** is what latches that, interrupts on it, holds it after the pin returns, and clears it only on status-then-data. Put the latching in the stream and every stream re-implements it slightly differently.

**The strap lives on the CARD** — `SET sio0:a dcd=wired`, default `ground`. See `docs/boards/mits-2sio.md`.

#### There is ONE baud rate, and it is the card's (Patrick, 2026-07-12)

> *"Do we need emulated character timing with a real serial port attached? The real serial port is the limiting factor."*

The plan called for two — a card baud and an independent endpoint baud. **That is struck.** There is exactly one line rate in a serial card and it is the UART's clock, a jumper; the frame format is whatever the guest wrote into the control register, because those bits *are* what goes on the wire. So **`CONNECT … serial:/dev/tty…` programs the host port from the chip**, and re-programs it whenever the guest rewrites the control register. A card strapped for 300 driving a terminal set to 9600 does not give you a fast link on real hardware — it gives you garbage, and a second baud rate could only ever configure the garbage.

The emulated character timing **stays**: it is the *same* duration the real port takes, not an extra one, and it must stay because the guest can **measure** it (the Mike Douglas BIOS times TDRE to infer the line speed).

#### …and there is NO `baud = 0`. It is not the analogue of `clock_hz = 0` (2026-07-14)

`clock_hz = 0` is free-running and is the default (§8), so `baud = 0` looks like the obvious next tidy-up. **It is not the same kind of number, and the request was declined on evidence.**

- **`clock_hz` is invisible to the guest.** It changes only the mapping from emulated T-states onto wall-clock seconds. Nothing inside the machine can observe it — which is exactly what makes flat out a safe default.
- **`baud` is a duration in T-states** — *inside* emulated time — so the guest **can and does** observe it. TDRE timing is one way (above). The **inter-character gap on receive** is the other, and it is the one that bites: the 6850 has **no handshaking**, so **the baud rate is the only flow control the card has**. In this simulator it is precisely what turns a host *paste* — ten bytes appearing in the pty at once — into a **paced serial stream**.

At `baud = 0` a character occupies the line for **zero T-states**, so the next byte is already in RDRF at the same T-state the guest read the last one, and **the guest's foreground never runs at all**. MITS PS2 is the proof: its ISR hands characters to its foreground through a **one-byte mailbox** and a semaphore, and that handoff requires the foreground to run *between* characters. Give it a zero character time and nine of the ten characters of a typed line are overwritten in the mailbox, and the monitor spins on the semaphore for ever. `baud = 0` is not a 2SIO running flat out; it is an **infinitely fast serial line**, which never existed (§0.1).

And the speed it was supposed to buy is mostly already there: **the baud rate never makes the run loop *sleep***. The throttle is `!clock.free()` (§8), so on a `clock_hz = 0` machine the TDRE wait is not a wall-clock delay at all — it is emulated T-states, executed at host speed. It is not free (a *very* low baud means proportionally more spin instructions for the host to grind through), but it is not the throttle it looks like, and it is not what a free-running default would have removed.

The property refuses `0` (`min = 50`), `tests/test_sio2.cpp` locks it in, and this is why.

And there is **no `flow = rtscts` endpoint setting**, for the same reason: hardware flow control in `termios` hands RTS/CTS to the *OS driver*, and the 6850 owns those pins. Two owners for one pin is a bug that shows up under load. **XON/XOFF was never a board option at all** — it is always software's job, never the card's (Patrick, 2026-07-12). It is bytes in the data stream, which makes it the guest's business, and a card that filtered them would be eating the guest's data.

> **An unconnected line is not an error, and there is no null pointer in the stream path.** A disconnected unit is bound to a `NullStream`, because that is what an unconnected 6850 on a real card *is*: it sits there with TDRE set forever, and software that writes to it works fine and talks to nobody. So no board contains a branch for "what if nothing is plugged in" — there was never a case to handle.

> **A `ByteStream` is NOT a serial line.** It is a *buffered, flow-controlled* source — a pipe, a socket, an OS keyboard queue. It will hold a byte until you take it. A board that models it as a free-running wire (and therefore synthesizes overruns from it) manufactures data loss that the host transport does not have. This cost real debugging; see `docs/boards/mits-2sio.md`.

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

Implementations: `ConsoleStream`, `TcpListenStream` (`socket:2323` — accept, one client, survive disconnect/reconnect), `TcpConnectStream` (`socket:host:port`), `HostSerialStream` (termios vs `SetCommState`/DCB), `FileStream` (paper tape), `NullStream`, `LoopbackStream` (testing), `ReplayStream` (recorded bytes at recorded T-state stamps).

> **A CLIENT CONNECTING *IS* CARRIER APPEARING**, and everything else falls out of it: a telnet client closes its window and DCD drops (and the 6850 latches that, and interrupts); the guest drops DTR and we hang up on the client; the TCP send buffer fills and CTS falls, so TDRE stays clear and the **guest waits rather than losing a byte**. That is what every terminal server ever built did, and it is what will let a PMMI work over a socket without the board learning what TCP is.
>
> **No threads.** §7.5 permits blocking host I/O behind a `ByteStream`, and it turned out not to be needed: a socket and a serial port can both be asked, without waiting, whether they have anything. A thread would buy nothing and would cost the determinism RECORD/REPLAY is built on — the bytes would arrive at whatever T-state the host scheduler felt like. They arrive in `pump()`, at a known point in emulated time.
>
> **RECORD/REPLAY must capture line transitions, not just bytes.** A recording that logs only characters diverges the first time a carrier drop drives an interrupt. `ReplayStream` will replay `(tState, byte)` **and** `(tState, LineStatus)`. Impossible to retrofit; the interface is shaped for it now.

**Discipline: the board asks `readable()`/`writable()`; it never blocks.** All actual I/O is drained/filled by the event loop once per time slice, so `tick()` is pure computation.

### 7.2 `Console` — the host keyboard and screen

**Built, 2026-07-11** (`src/host/console.cpp`, `src/host/filter.cpp`). Implemented: `upper`, `strip7in`, `strip7out`, `crlf`, `echo`, `bell`, `bsdel`, and `attn`. Not yet: `tabs`, `ansi`, `rows`/`cols`, `pace`, `log`.

A `ByteStream` like any other, so a board connecting to it needs no special code. But it is the only stream with a human on the far end, so it owns a configurable **transform chain**, applied inbound from the keyboard and outbound to the screen. Properties are declared through the same `Property` layer as boards, so `SET`/`SHOW`/MCP/completion work on it for free.

> **The transforms are the CONSOLE's, and the console's alone** (Patrick, 2026-07-13) — `SET CONSOLE UPPER=ON`, `[console]` in a config file. Every other endpoint — socket, serial port, tape, file, loopback — is **8-bit clean, always**.
>
> They were briefly moved onto the **line** instead, as a `FilterStream` inside each UART, on the strength of the paragraph below ("a real terminal on a real host serial port wants the same uppercase folding"). **That was wrong, and it was wrong in a way that corrupts data.** A card's connector goes to a modem, a socket or a real `/dev/tty.usbserial`, and the next thing down it is **XMODEM — 8-bit binary**. A `strip7out` on that line masks bit 7 of every byte of the transfer and does it *silently*. Set it once for MITS BASIC, forget, and every binary file that ever leaves the machine is quietly corrupt. The 88-ACR reached this conclusion first and refused the chain outright ("a tape is binary, not text"); every other line deserves the same protection.
>
> **A LINE HAS LINE CODING, NOT FILTERS.** `baud`, `data_bits`, `stop_bits`, `parity` are real, they are **hardware** — the 88-SIO's NDB/NSB/NPB/POE jumpers, the 6850's control register — and they belong to the card. They set how long a character occupies the wire, and on a **real serial port they are programmed into the real port** (`ByteStream::setParams`). A frame is not a filter: a card strapped for 7 data bits sends seven because that is what the hardware does, and it does not mask the guest's byte to do it.
>
> **The rule, in one sentence: the only thing that may alter a byte is the console, because the only thing with a human on the end of it is the console.**

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

~~**Implement the transforms as a reusable filter chain on `ByteStream`, not as console-specific code** — a real terminal on a real host serial port wants the same uppercase folding, so `SET sio2b UPPER=ON` on a socket-connected line works for free.~~

**Struck, 2026-07-13, and left here as the record of a mistake worth not repeating.** It reads well and it is wrong: it optimises for the VT100 you *might* hang off a serial port and forgets the XMODEM you *will* run through it. The chain is still a reusable `FilterStream` (`host/filter.h`) — the console just owns the only one.

**Arbitration:** exactly one unit may hold the console at a time. `CONNECT sio2a:a console` steals it, warning who had it.

### 7.3 `DiskImage` — the generic mountable medium

Every disk/tape board (88-DCDD, 88-HDSK, Tarbell, Disk 1A, North Star, any future controller) sees only this. `MOUNT` binds an implementation to a unit.

**The interface is CHS, not LBA, and the format is per-track.** Both of those are forced by real disks, and an LBA interface with a single global geometry cannot express them:

```cpp
enum class Density { SD, DD };

class DiskImage {
public:
    explicit DiskImage(std::unique_ptr<MediaFile>);   // the host file lives BELOW this

    // The BOARD describes the medium: overall shape, then one or more TRACK RANGES.
    void init(int tracks, int heads, bool interleaved);
    void initFormat(int trackLo, int trackHi, int headLo, int headHi,
                    Density, int sectors, int sectorSize, int startSector);

    bool readSector (int t, int h, int s, uint8_t* buf, size_t* n);
    bool writeSector(int t, int h, int s, const uint8_t* buf, size_t* n);

    bool     readOnly() const;
    bool     readOnlyForced() const;   // the host would not let us write it
    void     sync();
    uint64_t size() const;
};
```

Why each piece is there — each corresponds to a disk that exists:

- **CHS, not LBA.** Every controller in the catalog addresses track/head/sector. An LBA interface would force each board to invent a flattening the hardware never had, and then invert it.
- **Format is declared over *track ranges*, not once for the disk.** Sector size and density genuinely vary *within* one image: a double-density soft-sector controller keeps **track 0 single-density** so the boot PROM can read it. One `Geometry` for the whole disk cannot say that; two `initFormat` calls can.
- **`startSector`.** The 88-DCDD numbers sectors from **0**; most soft-sector controllers number from **1**. This is exactly the off-by-one that silently corrupts a disk.
- **`interleaved`.** Whether a two-sided image stores `T0H0, T0H1, T1H0…` or all of head 0 followed by all of head 1 is a property of the *image*, and it varies by the tool that wrote it.

**Hard-sector vs soft-sector needs no flag** — it falls out of `sectorSize`:

| | `sectorSize` | What the image holds |
|---|---|---|
| **88-DCDD** (hard sector) | **137** | The *whole slot*: sync byte, track/sector header, 128-byte payload, checksum, stop byte, trailer. |
| Tarbell, Disk 1A, North Star… (soft sector) | **128** / 256 | **Payload only.** The header and checksum were in the inter-sector gaps on real media and never made it into the image. |

The board still owns what is *inside* the slot — for the DCDD, that the payload starts at offset 7 on a data track and 3 on a system track, and that a checksum sits at [4]. That is the controller's business, exactly as `docs/boards/mits-dcdd.md` says.

> **Geometry probing belongs to the BOARD, not to this service.** An earlier draft of this section said the opposite — *"geometry probing lives here, once"* — and that was **wrong**. Geometry is a function of **controller × image size**, and the service does not know the controller. 337,568 bytes means a 77-track 8″ floppy *only because* it is a DCDD; 8,978,432 means a 2,048-track FDC+ *only because* it is a DCDD. The same byte count on a Tarbell means something else. So the **board** probes the size, picks among the formats *it* knows, and calls `init`/`initFormat`. The service does offsets and I/O and nothing else.

Worked example — the board configuring the medium:

```cpp
// 88-DCDD, 8 MB FDC+ image. The board knows it is hard-sector.
img.init(2048, 1, /*interleaved=*/false);
img.initFormat(0, 2047, 0, 0, Density::SD, 32, 137, 0);   // whole 137-byte slot

// A soft-sector DD controller whose boot track must stay single-density.
img.init(77, 1, false);
img.initFormat(0,  0, 0, 0, Density::SD, 26, 128, 1);     // track 0
img.initFormat(1, 76, 0, 0, Density::DD, 26, 256, 1);     // everything after
```

**Underneath it: `MediaFile` (`src/host/media.h`) — the host file, and the only thing in the program that opens one.** Buffered, dirty write-back, a write-protect tab, a sync. This is the layer a disk and a tape genuinely *share*.

**And `DiskImage` is ONE CLASS, not a base class** — every implementation this section used to name has dissolved. `ReadOnlyImage` was never a different *image*; it is a medium that says no. `MemoryDisk` was never a different image either; it is a `MemoryMedia`. And **`ImdImage`/`Td0Image` are never coming** (Patrick, 2026-07-12): raw disk images are the only kind this program will ever read, and an IMD file that has to be used here is one that gets converted to raw beforehand, outside it. Those container formats were the **entire** reason `readSector`/`writeSector` were virtual — a format that carries its own per-track sector map needs to override the arithmetic — so with them ruled out, the virtuals go. The image is sector-linear, always. A hook left in for a possibility the owner has ruled out is not extensibility; it is a hook nobody will ever pull, and the next reader has to work out why it is there.

**Write-protect mounts, it does not refuse** — the read-only flag goes on by itself, and the operator is told that it did (Patrick, 2026-07-12). A file the host will not let us write is a disk with the tab out, which is an ordinary disk — so it mounts read-only. What must not happen is the *silent* version: the operator typed no `RO`, so `readOnlyForced()` is true and the board **says so** through `Board::drainLog()`. Discovering it at `sync()` instead — after CP/M has spent an afternoon writing to it and the flush fails with the work gone — is the failure this prevents.

`openMedia(path, readOnly, err)` is the **one seam**, installed by `setMediaResolver()` in `src/main.cpp` and `tests/main.cpp` — the exact shape of `resolveEndpoint()`/`Sio2Board::setResolver()`, and for the same reason: a board asks for a path and gets a medium, and a test swaps the filesystem out for RAM without the board noticing.

**The XMODEM pad.** Both 8″ DCDD images in the tree are 337,664 bytes, not the 337,568 that 77 × 32 × 137 predicts — XMODEM padded them to a 128-byte block boundary. A strict `size == exact` probe therefore rejects **both of the only 8″ disks we have**. Every format match is `exact <= size < exact + 128` (`sizeMatches()`, in `disk.h`, because the trap is in the file format and not in any one controller). The pad is never data, and a write never reaches it: `DiskImage` bounds every access against the declared geometry, and a disk never grows under a write.

*Modeled on `simh.mdsk/Altair8800/altair8800_dsk.c` (© 2025 Patrick A. Linstruth) — our own prior art, not another project's.*

### 7.3.1 `TapeImage` — the sequential medium, and why it is not a `DiskImage`

A cassette has exactly one thing a disk has not: **a position**. It is not that a tape is a worse disk — it is that the head is where it is, and the only way back to the start of the program is to **rewind**. That is the whole of the difference, and it is why `TapeImage` (`src/host/tape.h`) is its own class over the same `MediaFile`: `read`/`write`/`rewind`/`pos`/`atEnd`. The CLI gets a verb, `SHOW` gets a number, and the guest gets the bytes in the order they were recorded.

**And then the adapter that makes the 88-ACR nearly free: a tape *is* a `ByteStream`.** `TapeStream` is 20 lines, and with it the shared 1602 UART needs no cassette-specific code at all — the ACR hands it a `TapeStream` where the 88-SIO hands it a socket, and the only difference left is that the unit is `UnitKind::Tape` (MOUNT) rather than `UnitKind::Serial` (CONNECT). That is §7.1's promise being cashed: the board knows it has a serial line, and does not know what is on the end of it.

> **The prediction held — the card came in at ~250 lines and inherits its whole bus half from the 88-SIO, because it *is* an 88-SIO B. But "the only difference left is the unit kind" was one word short, and the missing word cost a silent data corruption.**
>
> A `TapeStream` also needs a **MODE**. A cassette has ONE head, so read and write share ONE position — they must; it is the same piece of tape. And a UART receives **eagerly**: it pulls a byte off its line the moment it has room, because that is how DAV and an interrupt-driven loader work. So a tape that was readable *and* writable at once had its first byte pulled away by the card **before the guest ever ran**, the head sat at 1, and **every recording began at byte one**. Playback worked perfectly the whole time, which is why no load test could ever have found it.
>
> The fix was the hardware's own and cost nothing: the 88-ACR has **no motor control** — a human pressed the buttons — and a recorder is in PLAY *or* in RECORD, never both. Making that exclusive makes the corruption **unrepresentable** rather than merely unlikely. `tests/test_media.cpp` had asserted the *opposite* (`CHECK(bs.writable())` on a readable stream); that is how it got in. See `docs/boards/mits-88acr.md`.

`readable()` is *there is more tape*, so the byte **waits for the card** — a tape that dropped a byte because the guest was slow would be manufacturing data loss the host does not have, which §7.1 forbids. A real recorder keeps rolling and *can* drop data; we do not model that, and the board's `.md` says so under Limitations.

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

### 7.5 `Clock` — the single source of time

Nothing in the simulator may call `std::chrono::now()` except this. Time is measured in **T-states**, never milliseconds, and it advances only when the CPU retires an instruction — by exactly the count the CPU reported. That is what makes replay deterministic, and it is why the UART's idea of when a character has finished going out is derived from the very instruction stream the guest is timing it against; the two cannot drift.

The crystal is on the **CPU card** (§3, §8), so the card publishes its `clock_hz` here. A board converting a real-world rate (a baud rate, a disk RPM) into T-states asks the clock, and never has to go hunting through the backplane for whichever card holds the oscillator — or discover there isn't one.

```cpp
class Clock {
public:
    uint64_t now() const;              // T-states since POWER. Only power resets it.
    void     advance(uint64_t dt);     // the run loop, with StepResult::tStates

    using Handle = uint64_t;           // an integer, so a stale one is merely stale
    Handle at(uint64_t when, std::function<void()> fn);     // "call me AT T"
    Handle after(uint64_t dt, std::function<void()> fn);
    void   cancel(Handle h);           // cancelling a dead handle is legal
    bool   pending(Handle h) const;
    size_t queued() const;

    long long hz() const;              // published by the CPU card
    uint64_t tStatesPer(long long perSecond) const;   // the ONE place that division lives
    void     power();                  // time restarts; every deadline is gone
};
```

Two properties the boards depend on:

- **Order is total and deterministic.** Events fire in `(when, scheduling order)` — the handle is a monotone counter and breaks the tie. Two boards with deadlines on the same T-state fire in the same order in every replay, on every host. Without an explicit tiebreak this is a divergence that appears once a month and can never be reproduced.
- **Inside a callback, `now()` is when the event was *due*** — not where the instruction that carried time past it happened to end. An instruction is up to 17 T-states long; a board that re-arms with `now() + charTime` would otherwise drift a little further with every character it ever sent.

#### The `EventQueue` came back, and the board cited as proving it unnecessary is the board that proved it necessary

**Reversed 2026-07-12.** This section previously *deleted* the `EventQueue` and argued the case at length. The argument was:

> In this architecture a board is already **polled** for everything the bus can observe about it. `decodes()` is asked on every cycle; `assertsInt()` on every instruction boundary. So a board never needs to be *woken* — it needs to answer *"what time is it?"* when someone finally asks.

**That argument was circular, and the circle was hiding the bug.** The board did not need waking *because we were polling it sixty million times a second*. The poll was not evidence that the queue was unnecessary. **The poll *was* the queue**, run at enormous cost and called something else.

And the poll had to go, because it was never how the machine worked (§4.4): a bus does not interrogate a card for its interrupt status. A card **pulls pin 73 and holds it**. Take the poll away and the board is left holding a deadline it has no way to be present for:

> A 6850 with the transmit interrupt jumpered raises IRQ when its shift register drains. **Nobody touches it. No bus cycle happens.** And the guest is sitting in a `HLT` waiting for precisely that interrupt. If the only way the card can act is to *be asked*, and the only thing that would ask is the CPU that is halted waiting for it, **then nothing ever happens again.**

That is not hypothetical — `tests/test_sio2.cpp` runs exactly that machine, and it is the test that fails without a queue.

**What was right in the old argument, and still is:** TDRE *is* a deadline, not an event, and a board that can answer "what time is it?" when the guest finally reads the status port should do exactly that and **schedule nothing**. The 6850 still does, for the polled case — `nextEdge()` returns "never" on a quiet line with an idle transmitter, which is the commonest state in the machine, and the old model paid the full price of a poll for it. You come to the queue only for a state change that must be **visible to someone else the instant it happens**, and on this bus there is exactly one such thing: **a wire**.

#### And a periodic pump, because a deadline cannot predict a keystroke

Patrick asked whether boards want an event queue, a periodic timer, or both. **Both — and the second one already existed.**

They are not alternatives; they answer different questions. A **deadline** is something emulated time already knows is coming (a character finishing transmission, a disk sector arriving under the head). A **keystroke from the host** is not in emulated time at all — nothing could have scheduled it — so it arrives through `Board::pump()`, once per time slice, which is the one door the outside world comes through (§7.1).

The 6850 needs both, in the same function: `pump()` takes the byte off the line, and if the line has not yet had time to deliver it, sets a **deadline** for when it will.

**No threads.** Board logic stays in emulated time, single-threaded, or `RECORD`/`REPLAY` is dead (§13). Host I/O may thread *behind* the `ByteStream`, where it belongs — that is what the interface is for.

### 7.5.1 `Spindle` — the disk turns whether or not anyone is looking at it

**Built 2026-07-12** (`src/core/spindle.h`), ahead of the two floppy boards that need it, because they need the *same* arithmetic and it should exist once.

A floppy rotates on its own. So the sector under the head is **not state a controller advances — it is a reading taken off the clock**:

```
sector = (now / tPerSector) % sectorsPerTrack
```

**There is no hidden counter and no advance-on-read**, and that is the whole design. The tempting alternative — a counter the card bumps when the guest reads the sector-position port — makes the disk's rotation depend on *how often the guest polls*: a tight loop spins the platter faster than a slow one, the drive runs at the speed of the software watching it, and a recorded session stops replaying identically. Deriving it from `Clock` kills all three at once, and costs less code than the counter would have.

**Two cards need it, for different reasons, which is why it is neither card's:**

- the **88-DCDD** hands the sector number straight to the guest at `IN 0x09`;
- the **Tarbell** never exposes it — but its FD1771's `Read Address` (0xC4) must answer *"which sector is under the head right now"* so the buffered CP/M BIOS can begin a track read where the head already is. A static answer spins that BIOS forever.

It lives in `src/core/` (Patrick, 2026-07-12) because it is **pure time math over a `Clock` and knows nothing else** — not a board, not a `MediaFile`, not a sector's contents. It is not a chip (§7.8: nothing solders a spindle to a card), and it does not belong in `src/host/` beside `DiskImage`, which is bytes and offsets with no notion of time.

**It hands back a 0-based INDEX, and stops there.** A controller that numbers sectors from 1 (the Tarbell does; the DCDD does not) adds its own `startSector`. That off-by-one is the one that **silently corrupts a disk** (§7.3), so it stays in the board where it is visible, and is deliberately *not* buried in here where a reader would have to go looking.

Two invariants it enforces so no board has to:

- **`nextBoundary()` is strictly future**, at every instant of a revolution — by construction, not by a hand-written guard. A deadline armed for `now()` fires inside the drain loop that is running it, re-arms, and the machine never advances again. The UART's `nextEdge()` enforces the same rule by hand (§7.5); here it falls out of the arithmetic.
- **The sector is the unit, not the revolution.** Everything derives from `tPerSector`, so `sectorAt()` and `nextBoundary()` cannot round apart — a board that wakes on the deadline it was given always finds the sector it was promised, at that sector's first T-state. Deriving both independently from `tPerRev` would let them disagree by a T-state, occasionally, which is the worst kind of bug to own.

**The motor does not care what crystal the CPU has.** Rotation comes from `Clock::hz()`, so a 4 MHz machine executes twice as fast and still turns its disks 360 times a minute. Expressing rotation in raw T-states would make overclocking the CPU spin the floppy faster, which is nonsense.

### 7.6 `Log` / `Trace`

One structured diagnostic sink with per-board and per-category masks (`IN`, `OUT`, `READ`, `WRITE`, `IRQ`, `DMA`, `CONTENTION`), mirroring the `DEBTAB` idea in `mits_dsk.c`. Text for the monitor, JSON for MCP, from the same call site.

### 7.7 The two consequences worth stating explicitly

- **A new board written against these services is automatically cross-platform and automatically replayable.** That is the point of the layer, and it is the acceptance test for the API.
- **`CONNECT` and `MOUNT` are generic**, not per-board commands. The monitor resolves an endpoint string to a `ByteStream` or a file to a `DiskImage` and hands it to whichever board declared a unit of that type — so a board written next year gets `MOUNT`/`CONNECT` for free without touching the monitor. Note the division of labor: the *monitor* opens the file; the *board* decides what its bytes mean (§7.3).

---

## 7.8 A chip is not a card (`src/chips/`)

**A board is a PCB with chips on it, and the code says so** (Patrick, 2026-07-12). `src/chips/` holds the parts that get soldered to more than one card: `mc6850.h` (the 6850 ACIA, on both halves of the 88-2SIO), `uart1602.h` (the COM2502 — the 88-SIO, and the 88-ACR when it lands; the same 40-pin part that others second-sourced as the AY-5-1013 and the TR1602), and `wd17xx.h` (the **FD1771**, on the Tarbell and on the controllers after it).

**What a chip talks to is an INTERFACE, never a file.** `Mc6850` has a `ByteStream`; `Wd1771` has a `FloppyDrive` — pins (STEP, DIRC, TR00, WPRT, READY, IP) with a drive on the far end. The FDC owns no image, no geometry and no host file, and it has **no drive-select and no side-select pin** — because the FD1771 hasn't got either. Which drive those pins reach is a *latch on the card*, so the board calls `attach()` and the chip is none the wiser. That absence is the chip/board seam stated in silicon.

**The FD1771 is also where §0.1 earned its keep.** `reference/` contains a WD177X data sheet, and it is a *different chip*: its stepping rates are 6/12/20/30 ms where the FD1771's are **6/6/10/20**, and its record type is one bit where the FD1771's is **two** (it has four data address marks). Building the Tarbell's controller from the sheet that was to hand would have produced something plausible, clean, and wrong — a controller that seeks at the wrong speed and mis-reports deleted records, while looking entirely finished. The right sheet was sourced first (`wd17xx.h` says which), and `tests/test_wd17xx.cpp` keeps a tripwire test on the step-rate table so that nobody "fixes" it back.

**The seam is not "the chip does the work and the board forwards to it."** The 88-SIO is the case that shows where it really falls. Its status word is *inverted* and the 88-2SIO's is not; a shared UART class with a `bool invert` on it is the exact bug this rule exists to prevent. So: **the chip is what the data sheet describes, at its pins, in true sense.** Everything between those pins and the S-100 bus — the inverting buffers, which bit each signal lands on, the board revision that moved them, the interrupt-enable flip-flops in a *different IC*, the port decode — is the CARD's, and it stays on the card. The COM2502 has no interrupt pin at all, so it cannot even answer "when could my interrupt move?"; it publishes its raw deadlines and the board, which owns the enables and the strap to pin 73, works that out. Two cards, one chip, and not one line of polarity shared between them.

**Each chip is modeled from its DATA SHEET, and each board from its MANUAL.** That line is the whole reason the directory exists. A chip built instead from the one BIOS that happens to drive it will implement exactly the subset that BIOS touches and quietly get the rest wrong — and it will look finished while doing it. §0.1 applies to a chip's registers exactly as it applies to a card's ports.

A chip knows nothing about S-100. It has a clock, some pins, and (if it moves bytes) a `ByteStream`. It never learns the endpoint *grammar* either: the monitor installs a resolver on the board, and the board hands the **function** down — which is §7.7's division of labor holding one level further in.

**This does not license sharing between cards that merely resemble each other.** The 88-SIO and the 88-2SIO still share no code, on purpose: they are *different chips with opposite status polarity*, and a common helper with a `bool` flipping the sense is precisely the trap that rule was written to prevent. What licenses sharing is being **the same part**, not filling the same role.

---

## 8. Timing and host idling

- Clock is the **CPU board's** `clock_hz` — not the machine's (§3). The crystal is on the card, and a backplane with no CPU card has no clock rate at all. **`0` = free-running, and it is the DEFAULT** (Patrick, 2026-07-13). The run loop simply does not sleep, so a cassette that took a real Altair 110 seconds comes off in about one. Emulated time is unchanged: `Clock::hz()` remains a 2 MHz **divisor** so no UART ever divides by zero, and a separate `Clock::free()` decides whether we wait. `clock_hz = 2000000` gives back the period machine *and* the period waiting. (Before this, `0` was documented as "runs flat out" and silently did nothing — `setHz(0)` coerced the rate back to 2 MHz and the run loop paced against it.)
- Throttle by comparing accumulated T-states against a monotonic host clock in ~1 ms slices and **sleeping** the remainder — never spin.
- **The CPU card WRITES to the Clock, and that makes re-attaching a card a publish** (issue #34, 2026-07-18). Every other card *reads* the clock and is happy to be handed a different one; this card pushes `clock_hz` and `idle` **into** it. A machine file is assembled into a scratch `Machine` so a bad file cannot damage a running one (§10), so the card announced 2 MHz to the *scratch* Clock and `Machine::replaceWith` then moved it onto the real backplane — whose Clock had never heard of it. `SHOW cpu0` read `2000000` off the card while the run loop free-ran, and **both were telling the truth about different objects**, which is the worst shape a bug can take. The invariant is *this card's clock knows what this card told it*, and it is kept where it can be broken: `Board::attachClock` calls a virtual `clockAttached()`, and the CPU cards republish there.
  - **It survived because every test set the crystal at the monitor.** `SET cpu0 clock_hz=...` runs on a card already on the real backplane, and `CONFIG LOAD` powers the machine again afterwards and republished by luck — so the one road nothing drove was the one every operator takes: put it in the file, start the simulator. A property that can be set two ways needs a test for **each** way, and the file is the way that matters.
- **Idle detection** — **BUILT, 2026-07-13** (Patrick), and it is the CPU card's `idle` property, on by default. It is the *second* sleeping policy and it is **orthogonal to the first**: `free()` asks "do we keep time?", `idle()` asks "do we stand down when there is nothing to do?". Both live on the Clock because that is where the run loop already goes to ask whether to sleep, and both are published by the card that carries the crystal (`boards/mits-88cpu.cpp`). `SET cpu0 idle=off` gets the spin back. **No hardware behaves differently — only the host sleeps, and the guest cannot tell.** Measured: 8 MB CP/M at `A0>` went from **100% of a core to ~3.5%**.
  - **The signals are three, and all must hold**: the guest **said** nothing (`Console::written()`), **received** nothing (`Console::consumed()`), and came to the keyboard and found it **empty** at least once every 32 instructions (`Console::hungry()`). Then, and only then, once it has been that way for an unbroken **20 ms**, the loop naps 4 ms per slice.
  - **"Any data read resets the counter" is the load-bearing clause, exactly as this section always said.** A guest receiving XMODEM *down the console line* is the counter-example that breaks every simpler rule: at 76,800 bps it waits 130 µs for each byte, polling an empty keyboard hundreds of times per slice, and it prints nothing for a whole 128-byte block. By "is it polling?" alone it **is** a prompt — and a first draft that judged it on the poll ratio alone napped straight through a fed console at 4.3% of a core, which would have dragged 7.7 kB/s down to 250 B/s. The difference between a parked machine and a working one is not how it polls; it is that **bytes are arriving**. So a byte crossing into the guest resets everything.
  - The 20 ms **warmup** is the belt to that braces: sub-millisecond gaps inside a transfer can never accumulate into a nap, while a human's gap before finding a key is effectively infinite. It costs a prompt nothing anyone can perceive (measured `DIR` round trip: 13 ms, nap and all).
  - It applies **only when a unit holds the console and stdin is a terminal** — the same gate as the throttle. Under `-s` or a pipe the keyboard is a script, and the run loop's existing end-of-input logic (`starved()`, §7.2) is what ends the run there. `hungry()` and `starved()` are **deliberately different counters**: empty, versus empty *and ended*. Merging them re-introduces the bug that killed a cassette load three slices in.

---

## 9. Devices, `MOUNT`, and `CONNECT`

- A board declares typed **units**, and **a unit is a NAME, not an index** (Patrick, 2026-07-11). A disk unit accepts `MOUNT id:unit <hostfile>` (`UNMOUNT` to release); a serial unit accepts `CONNECT id:unit <endpoint>` (`DISCONNECT`).

  **ONE CARD IS NOT ONE KIND OF THING.** A card may carry drives *and* ROM sockets *and* a serial port — the Tarbell carries a boot PROM and a floppy controller on one board (it is **not built**, see §4.2.1, but it is a real card and the constraint is real), and a controller with its own PROM, scratch RAM and a serial port was a completely ordinary 1977 product. Nothing in the bus model ever assumed otherwise: `decodes()` is asked about every cycle and `BusCycle::type` distinguishes memory from I/O, so one card answers both. `tests/test_units.cpp` builds exactly such a card and proves it.

  So units are named and typed — `MOUNT dj:drive0`, `MOUNT dj:rom0`, `CONNECT dj:tty` — and **the kind is checked**: mounting a disk image onto a serial port is an error with a sentence explaining it. The integer scheme could not be made safe, which is why it is gone: with a flat namespace, `MOUNT dj:4` on a serial unit can only *fail*, never *explain*, because the board has nothing left to distinguish 4-the-drive from 4-the-port. `SHOW <id>` lists the units, and it reads `Board::units()` — the same list MOUNT reads, so they cannot disagree.
- Endpoints: `console` | `socket:PORT` (listening) | `socket:HOST:PORT` (outbound) | `serial:/dev/tty.usbserial-X` or `serial:COM3` | `file:path` | `null`. **Built so far: `console`, `null`, `loopback`.** The resolver **names the unbuilt ones when you ask for one**, rather than failing as though you had mistyped it — a user who types `socket:2323` has a specific expectation and deserves to be told it is not here yet, not left wondering about their syntax.
- Exactly one unit may hold `console` at a time; the monitor arbitrates. **Connecting a second STEALS it and says who from** — two boards reading one keyboard would each get half the characters, which is not hypothetical: it is what happens the first time a machine has two 2SIOs and you forget.
- Disk images are buffered and written back. **The board** probes the image size against the formats *it* knows and declares the layout to `DiskImage` (§7.3); `media = ...` forces the choice when the size is ambiguous, and **the choices belong to the card**: an 88-DCDD takes `8in` and `fdc8mb`, an 88-MDS takes `minidisk`. Naming another card's medium is an error, not a probe — the two controllers are register-compatible (`docs/boards/mits-88mds.md`), so nothing else would have caught it. `readonly` supported (the real board's write-protect).

---

## 10. Monitor CLI

SIMH/AltairZ80-flavored, stable and greppable.

```
CONFIGURATION
  CONFIG LOAD <file.toml>          CONFIG SAVE <file.toml>
                                   (bare LOAD/SAVE mean *memory* — see below)
  BOARDS                           the backplane: id, type, i/o, units, memory
                                   (BOARD too: it is a prefix of BOARDS, not an alias)
  BOARDS TYPES                     every board type compiled in, with its properties
  BOARDS ADD <type> <id> [k=v ...] BOARDS REMOVE <id>
  SHOW <id>                        every property: value, units, legal range
  SET <id> <k>=<v>                 generic; e.g. SET sio2a BAUD=9600, SET mem0 PHANTOM=read
  SHOW ROMS                        every ROM compiled in: name, size, CRC32, description
                                   (use as mount = "builtin:<name>" — see §10.3.1)

INTROSPECTION
  SHOW BUS                         CPU, clock, T-states, pending IRQ, DMA state
  SHOW BUS MAP                     memory decode map: range -> board, type, phantom, bank
  SHOW BUS IO                      I/O decode map: port -> board, direction
  SHOW BUS IRQ                     VI0-VI7: who is strapped where, who is pulling,
                                   who WINS -- and the two silent mis-wirings (a vi*
                                   strap with no 88-VI; an `int` strap with one)
  SHOW BUS CONTENTION              every address/port claimed by more than one board
  WHO <addr> | WHO IO <port>       reverse lookup: who responds here, and why

MEDIA AND CONNECTIONS
  MOUNT <id>:<u> <file> [RO]       UNMOUNT <id>:<u>
  CONNECT <id>:<u> <endpoint>      DISCONNECT <id>:<u>

CONSOLE  -- it CONFIGURES the console; it does not start the machine (RUN does).
  CONSOLE                          show it: properties, and WHICH UNIT HOLDS IT
  CONSOLE <k>=<v>                  set it.  SHOW/SET CONSOLE are the same, said long
  ATTN                             the key that takes the keyboard BACK from a running
                                   guest (default ^E). Tracked on CONSOLE INPUT ONLY:
                                   a unit on a socket or a serial port is not the
                                   console, and its data passes through UNALTERED.
  The transforms -- UPPER, STRIP7IN, STRIP7OUT, CRLF, BSDEL, TABS, ECHO, ANSI, ROWS,
  COLS, PACE, LOG, BELL -- are properties of the LINE, not of the console, so they
  work on a socket too: SET sio0:a UPPER=ON.

  WHICH UNIT IS THE CONSOLE? The one CONNECTed to it. Exactly one may hold it (there
  is one keyboard); connecting a second STEALS it and says who from. A config file
  that names two is REFUSED -- interactively the last cable you plug in is the one
  you meant, but in a file there is no "last": it is a typo.

  THE KEYBOARD IS BUFFERED BY THE HOST. Keys land in a buffer here and a card takes
  characters from it. That is what lets ATTN be watched whether or not anybody is
  reading -- including with no serial card in the machine at all -- and what lets
  MCP inject input that no board can tell from a human's.

MEMORY
  LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [ROM]
                                    HEX carries its own addresses; a flat binary does
                                    not, so it REQUIRES AT. The file's CONTENTS decide
                                    which it is; FORMAT= overrides and always wins. AT
                                    means PUT IT HERE for both: on a HEX file it moves
                                    the image so its FIRST DATA RECORD lands there,
                                    wrapping modulo 64K. ROM is the burner (10.2).
  SAVE <file> <range> [FORMAT=BIN|HEX]
                                    The NAME decides -- .HEX writes Intel HEX, anything
                                    else writes a flat binary -- because SAVE cannot
                                    sniff a file that does not exist yet. FORMAT=
                                    overrides.
  DUMP [<addr>|<range>] [WIDTH=16]  hex + ASCII. A bare <addr> runs to the END OF ITS
                                    PAGE (D 0001 -> 0001-00FF); bare DUMP continues
                                    from there. Page-aligned in and out, so the rows
                                    and the columns both stay put.
  DISASM <range>|<addr> [n] [CPU=8080]
                                    Mnemonics follow the ACTIVE CPU -- you never type
                                    CPU=. It is the override for a machine with no CPU
                                    card in it, or for looking at foreign code (§3.0.2).
  EDIT <addr>                       interactive: show byte, type new value, Enter advances
  EXAMINE [<addr>]                  ONE byte: hex, ASCII, bits. Bare = EXAMINE NEXT.
                                    THE PANEL'S SWITCH IS THE CPU: it jams the address
                                    into the PROGRAM COUNTER and the CPU drives the
                                    address lines and MEMR*. So EX LOADS THE PC (`EX
                                    F800` + RUN starts a ROM, and CONSOLE <addr> is
                                    exactly EXAMINE + RUN), the PC *is* the cursor, and
                                    with NO CPU CARD IT IS AN ERROR -- nothing is driving
                                    the bus. (Look at a CPU-less machine with DUMP: it
                                    runs no cycle, so it needs nobody to drive one.)
  DEPOSIT <addr> <bytes...>
  FILL <range> <byte>
  SEARCH <range> <bytes...>|"str"
  COMPARE <range> <addr> | COMPARE <range> <file>
  MOVE <range> <dest>
  LOAD, DEPOSIT, FILL and MOVE take an optional ROM: program a ROM, by going behind the
  bus into whichever chip answers reads there (§10.2). The read side needs no such word
  -- a ROM answers reads like anything else. EVERY ADDRESS HERE IS A BUS ADDRESS.

I/O
  IN <port>                         run a real IN cycle -- with real side effects
  OUT <port> <byte>                 run a real OUT cycle
  WHO IO <port>                     who WOULD answer -- looks without touching

EXECUTION
  RUN [addr] | STEP [n] | STOP
  RUN is the switch on the panel, and the ONLY way to start the machine. `RUN <addr>`
  is EXAMINE + RUN: it loads the PC first, exactly as the panel does.
    - A unit holds the console -> the GUEST GETS THE KEYBOARD (every key, including
      ^C, which CP/M is entitled to read), and it runs at the CPU card's real clock.
    - Nothing holds it       -> there is nothing to hand over, so it just runs.
  That is not a mode the operator picks. It is a fact about the backplane, and the
  machine already knows it -- which is why GO was DELETED (Patrick, 2026-07-12):
  a "headless run" was never a second thing to be. Both paths stop on a breakpoint,
  on a HLT nothing can wake, and on ATTN, and both say which.

  ATTN (^E) IS THE STOP KEY, NOT ^C. Ctrl-C belongs to the guest. ATTN does not stop
  the machine -- it takes the keyboard back, and a bare RUN resumes where you were.
  RESET | RESET CPU | POWER
  There is NO `SET CPU`. The CPU is a CARD (§3): BOARDS ADD 8080 cpu0, and the clock
  is that board's property -- SET cpu0 clock_hz=2000000. A card carrying both an
  8080 and an 8085 exposes them as UNITS and switches between them itself (§3.0.1).

DEBUG
  BREAK <addr> [IF <expr>] | BREAK IO <port> | BREAK MEM R|W <range> | NOBREAK
  REGS | SET REG <r>=<v>            Generic: registers are reflection (§3.0.3), so a
                                    Z80 works the day it lands with no monitor change.

  EVERY STOP PRINTS WHY, AND THEN ONE LINE (§3.0.3.1). STEP traces in the same line,
  DDT-style: the machine as it stands, WITH the instruction it is about to run.

    ATTN -- the machine is still at 0020. RUN resumes.
    C0Z0M0E0I0 A=00 B=0000 D=0000 H=0000 S=0000 IE=0 P=0020  JMP 0020

  The reasons are ATTN (you took the keyboard back), a breakpoint (which one, and of
  what kind), HLT that nothing can interrupt, ^C, a SCRIPT'S INPUT ENDING, and no CPU
  in the machine. Six different things, and they get six different words -- the monitor
  used to GUESS at two of them from whether a console happened to be attached, and a
  guess is what you write when the reason was never carried in the first place.

  A HALTED 8080 DOES NOT RUN. It leaves HALT for an interrupt or a RESET, and loading
  the PC is neither -- so `RUN <addr>` on a halted machine says HLT again, correctly,
  and the fix is the RESET a human would throw. (This is what the honest stop reason
  caught first, in our OWN test: tests/acceptance/cli.exp had been "testing" ATTN
  against a machine that was halted the whole time, and matching the prompt that came
  back with the HLT.)
  TRACE ON|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]   HISTORY [n]
  SYMBOLS LOAD <file> [REPLACE] | SYMBOLS CLEAR          SHOW SYMBOLS [<name>|<glob>]
                                    A .PRN/.LST listing or a CP/M .SYM, so a name works
                                    wherever an address is typed (§10.3.2). HOST-SIDE like a
                                    breakpoint: survives RESET/POWER/CONFIG LOAD.
  SNAPSHOT <file> | RESTORE <file> | RECORD <file> | REPLAY <file>
  SET BUS CONTENTION=WARN|ERROR|SILENT
```

### 10.0.0 The command line, and the built-in machines

**Settled 2026-07-11 by Patrick.** This closes open finding **F4** (the command-line grammar was undefined).

```
altairsim [options] [<machine>]

  <machine>            a BUILT-IN name, or a FILE if it has a '/' in it or ends .toml.
                       Omitted: `default`.
  -m, --machine <n>    ALWAYS a built-in name -- never a file.
  -f, --file <path>    ALWAYS a file -- never a built-in name.
  -n, --none           empty backplane. No boards at all.
  -l, --list           list the built-in machines and exit.

  -s, --script <file>  run a command script, then exit with its status.  (was `-c`)
  -x, --exec <cmd>     run one monitor command (repeatable), then exit.
  -i, --interactive    after --script/--exec, stay in the monitor.
      --mcp            MCP server on stdio.
  -v, --version        -h, --help
```

`-s script.cmd` is the CI/regression entry point: a script that fails exits non-zero. `-x` is the same thing for one-liners, which is what makes a bug report reproducible in a single pasteable line.

**A built-in machine is a TOML file that lives in `.rodata`.** `machines/*.toml` are build-time inputs — CMake embeds them byte-for-byte, exactly as it does the ROM images, and `loadTomlText()` parses them at runtime with the *same* parser that reads a config off disk.

Two things follow, and both are requirements rather than conveniences:

- **The shipped binary is self-contained.** altairsim is delivered as **one executable plus documentation**. It must never go looking for a `machines/` directory, an install prefix, or anything relative to `argv[0]` — a simulator that cannot find its own default machine when copied to a USB stick is a simulator that does not start. `tests/test_machines.cpp` passes with the source tree deleted.
- **There is exactly one machine language.** Building the default machine in C++ (`m.add("memory", "mem0")`) would have been fewer lines and a quiet second dialect that nobody could copy, edit, or diff. Instead the machines we ship are written in the format users write, so they double as worked examples and *cannot drift from it* — if the config format changes under them, they stop loading and a test goes red.

**File or built-in is decided by SPELLING, never by probing the filesystem.** If the answer depended on the working directory, `altairsim default` would mean one thing today and something else the day somebody saves a file called `default` next to it. A command line whose meaning changes with its surroundings is a trap, and it is the kind that gets sprung at 2am. `-f ./default` and `-m default` never guess.

The built-ins, as of milestone 1a — both are honest about having **no CPU card**, because there is no 8080 yet:

| | |
|---|---|
| `default` | 56K RAM at `0000-DFFF`, **and the real DBL 4.1 boot PROM at `FF00`**. What you get with no arguments. |
| `4k` | 4K RAM, no ROM. The Altair as MITS shipped it; the machine 4K BASIC was written for. |

**`default` carries the boot PROM, and `4k` carries the period accuracy.** A bare Altair 8800 had no ROM at all — you toggled the bootstrap in from the front panel, and the DBL PROM only existed if you had bought the disk system. That machine is `4k`, and having it frees `default` to be a different thing: *the machine you actually want when you type `altairsim` and nothing else.* On a real disk Altair the PROM was there, and a default with an empty `FF00` is a machine you must repair before it is any use — which is the opposite of what a default is for. So `altairsim` followed by `D FF00` shows you the boot loader, as it should.

### 10.0.1 The number base: on the wire → hex, never on the wire → decimal

**Settled 2026-07-11 by Patrick.** This closes open finding **F3**.

> **The base is a property of the OPERAND, not of the command line.**

| | |
|---|---|
| **HEX** — the machine sees it | addresses, ports, data bytes, register values, opcodes |
| **DECIMAL** — only you see it | step counts, dump widths, history depth, sizes, baud rates, unit numbers |

`DUMP 100` starts at `0100h`. `STEP 20` steps twenty times. `SET sio0 baud=9600` is nine thousand six hundred. The 8080 never holds a step count or a baud rate, so those are not hex; it holds an address on sixteen pins, so that is.

**A single global base was never actually on the table.** `baud=9600` cannot mean 38400, so the rule was always going to bend somewhere — and given that, it bends where it *means* something instead of where it happened to fall. This is also why the alternative ("everything in a command is hex") was rejected: it buys one sentence of simplicity and pays for it with `STEP 20` stepping 32 times, silently, forever.

**Overrides work everywhere, in both directions**, because a rule you cannot type your way out of is a trap: `0x20` / `$20` / `20h` force hex, `#32` forces decimal, `0b1010` is binary, `1_000` is spacing.

**A `K`/`M` suffix is always behind a decimal number** (Patrick) — `10K` is 10,240, never 16K, which is why in fifty years nobody has had to ask. The suffix therefore carries its own base and overrides the caller's default. `0x10K` demands hex *and* a suffix that is decimal by definition; there is no right answer, so **it is rejected rather than guessed at**.

**There is exactly one parser** — `parseNumber(text, out, err, base)` in `core/value.cpp` — and the caller passes the default base, because the caller is the only one who knows what kind of quantity it is. Properties carry theirs as `radix` (§10.4), which is the same rule reaching the same answer through the reflection layer. **No call site may pre-chew its input to get the base it wanted**; three of them used to (the CLI prepended `"0x"` to `at=`, the property layer prepended `"0x"` to any radix-16 value, and the region parser stripped its own `K`), and all three were quietly wrong in a different way.

Pinned by `tests/test_numbers.cpp`. The failures this catches are all silent: every one of these tokens parses fine under the wrong base, so nothing crashes — you just size a card at 16K when you wrote 10K, and find out much later.

### 10.0 There is no `BOOT` command. The config file runs commands instead.

An earlier draft listed `BOOT <id>[:u]`. **It is removed, because it has no honest meaning.**

The 8080 begins at `0000`; a boot PROM lives at `FF00`. Something must bridge that, and a simulator has only two ways to do it:

1. **Synthesize a bootstrap internally** and jam it into memory (what SIMH's `BOOT` does). **Forbidden by §0.1** — it is fabricated hardware, and it means the machine boots in a way no real Altair ever booted.
2. **Do what the operator does: start execution at the PROM.** `GO FF00`.

The real hardware does it with a **power-on jump**: a turnkey/PROM board that forces the processor to the PROM address after a reset. That is a genuine S-100 feature and it may eventually be modeled as a **board property** — but it needs the 88-TURNKEY manual first (§17), and no simulator convenience should stand in for it in the meantime.

So the monitor keeps only the honest verb, `GO <addr>` — and to spare you typing it every session, **a machine config can carry a list of monitor commands to run once the backplane is built**:

```toml
[machine]
name = "cpm-dev"

startup = [                     # monitor commands, run in order after boards are created
  "RUN FF00",                   # the DBL PROM. This is the operator's keystroke, not fake hardware.
]

# NOTHING ELSE IS A [machine] KEY, and the two that used to be are the argument for
# why. `clock_hz` was here (the crystal is on the CPU CARD) and so was `sense` (the
# switches are on the FRONT PANEL, which is also a card). Both are board properties
# now, and both were REFUSALS, not silent migrations -- a config that looks like it
# set something and did not is worse than one that will not load.
[[board]]
type     = "8080"
id       = "cpu0"
clock_hz = 2_000_000

[[board]]
type  = "fp"
id    = "fp0"
sense = 0x00                    # SA8..SA15, read at port 0xFF
```

Three things fall out of this, and they are the reason it is the right shape:

- **The config language and the script language become one language.** A `startup` entry is an ordinary monitor command, so anything you can type, a config can do — and `altairsim -s script.cmd` and `CONFIG LOAD` stop being two different worlds.
- **`BOOT`'s special-casing disappears.** No verb needs to know what a "boot device" is, and a new disk controller written next year needs no monitor change to be bootable.
- **It is transparent.** `SHOW MACHINE` prints the startup commands; `CONFIG SAVE` round-trips them verbatim. Nothing happens that the user cannot see written down.

**"Anything you can type" has to be literally true, or it is a slogan.** A `startup` entry is a command line, and a command line **quotes its filenames** — the monitor's tokenizer requires it, because every period artifact in the tree has a space in its name (`4K BASIC Ver 3-1.tap`). For a long time the array parser toggled on every `"` with no escape handling, so `MOUNT acr0:tape \"...\"` was silently cut at the backslash and the machine came up with an empty recorder. The one thing `startup` exists for could not be expressed. `\"` and `\\` are now understood on both sides — and **any other escape is refused rather than quietly eaten**, so a Windows path written with single separators fails here instead of somewhere else, later, as a shorter and wrong path.

### 10.0.2 `base` — a config file may be a DELTA on another machine

A CP/M machine is *the default Altair with a floppy in drive 0*. Before `base`, saying that took a five-card backplane restated by hand — and **hand-copying a backplane is a defect class, not a chore**: the two CP/M files that motivated this shipped with the 2SIO left out, booted CP/M into a terminal that was not there, and looked fine doing it.

```toml
[machine]
name = "cpm22-8mb"
base = "default"          # fp0, cpu0, sio0, dsk0 (88-DCDD), mem0 (56K + DBL PROM)

startup = ["RUN FF00"]

[[board]]                 # no `type` -> the card ALREADY in the machine with this id
id = "dsk0"

  [[board.drive]]
  unit  = 0
  mount = "disks/mits-88dcdd/cpm22/8mb/CPM22-8MB-56K.DSK"
```

**The base is named, never assumed** (Patrick, 2026-07-13). An implicit default was the other option and it is the wrong one: `4k` is a machine **defined by what it does not have**, so it would have to *remove* a floppy controller, a 2SIO and 52K of RAM to describe a bare 1975 Altair, and **silence would stop meaning "nothing"**. A file with no `base` is a complete machine, exactly as before; one line at the top tells you what a delta starts from, and without that line the file *is* the backplane.

The four `[[board]]` forms — **add** (`type` + a new id), **replace** (`type` + an id from the base), **modify in place** (no `type`), and **remove** (`remove = true`) — are documented in `docs/config.md`. Two of them are load-bearing:

- **Replace exists because a list cannot be amended into a smaller one.** Regions are a *list*, so adding a 24K region to a base's 56K memory board would **overlap** it — two boards driving `0000–5FFF`, which is contention — not shrink it. Naming a card's `type` means *"this is the whole card now."*
- **A duplicate id within one file is still an error**, and replace is scoped around that check on purpose. A second `[[board]]` with a copy-pasted id is a **typo**; the same thing against a base is **intent**. Conflating them would discard the one diagnostic that catches the commonest mistake in a hand-written machine file.

**`CONFIG SAVE` never writes a `base`.** It writes the backplane it can see — every card, inherited or not — so a saved machine stands on its own. That is the only honest thing it can do, because a base may be a *file*, and a file can change under you.

**This is what makes `default` a contract.** The machine `base = "default"` starts from is a front panel, an 8080, an 88-2SIO console, an **88-DCDD**, 56K, and the DBL PROM. Adding a card to it is no longer free — other files now depend on what is in it.

> **Caution, and it must be in the docs:** `CONFIG LOAD` on a machine file now *executes commands*. Loading a `.toml` from an untrusted source runs whatever is in its `startup` list. Keep `startup` to monitor commands only, and say so out loud.

### 10.1 `SET`/`SHOW` are generic

Implemented once against `Board::properties()`; they know nothing about baud rates, phantom modes, or disk geometry. Adding a board adds its settings to the CLI for free. **Every property is settable** — see §5: you can only type at the prompt when the machine is already stopped, and a real card on an extender has its jumpers moved with the power on.

### 10.2 Memory access: through the bus, or behind it?

- **Default: through the bus.** `DUMP`, `DEPOSIT`, `IN`, `OUT` see exactly what the CPU sees — live bank, PHANTOM overlays applied, ROM not decoding writes, contention reported. Addresses are **bus addresses**, 0x0000–0xFFFF. This is the only view that tells the truth about a misbehaving decode, and it is why `IN 10` really does consume a UART's character: that is what an `IN` *is*.
- **`peek`: through the decode, but *without a cycle*.** Same PHANTOM\* resolution, same bank, same board — but no strobe, no side effect, no `snoop()`. **`DISASM`, `WHO` and the debugger's display use this, and they must.** *(Corrected 2026-07-11: §10.2 originally put `DISASM` in the first group. That was wrong, and quietly so — a disassembler built on real reads works perfectly against RAM and then, the first time someone disassembles a page with a UART mapped into it, **eats the console's input**. The bug would only appear when the memory map was unlucky.)* A board that cannot answer without side effects returns false, and the byte reads `FF` — which is honest, because on real hardware the data bus is only defined *during* a cycle.
- **`ROM`: behind the bus**, into whichever chip answers reads at that address. A **write-side** qualifier on `LOAD`, `DEPOSIT`, `FILL` and `MOVE`, and nothing else. Addresses are bus addresses like everywhere else.

**EVERY ADDRESS IN THIS MONITOR IS A BUS ADDRESS, 0x0000–0xFFFF.** There is exactly one address space the operator can type, and it is the one the CPU sees. *(Patrick, 2026-07-17: board-local offsets are out as too confusing — every address refers to the 64K address space.)*

**`ROM` is the PROM burner, and that is not a metaphor.** A ROM region does not decode a write cycle (§4.2), so `DEPOSIT FF00 41` cannot possibly reach it — nor should it, because on real hardware a bus write can't program a PROM either. You pull the chip and put it in a programmer, which is *not a bus operation*. `LOAD dbl.hex ROM` is exactly that, and it is why **the operator can write ROM while the guest cannot**, with no `writable` flag to leak and no originator tag on the bus.

It finds the chip by asking who answers a **read** there, which is the whole trick: a ROM does not decode a write, so asking who would take a write is asking the wrong question on precisely the chip you are trying to program. `Bus::respondersTo()` runs the real decode, PHANTOM\* and all — so a shadowed board does not answer, and you cannot burn a chip the machine cannot currently see, any more than the CPU could read it. Nobody home and contention are reported, never guessed at: `Machine::burn()` is the one implementation, and the monitor and MCP are two front ends onto it.

*(Superseded 2026-07-17. This was **`RAW <id>`**: it named a board, addressed that board's store by a **board-local offset**, and worked for reads as well as writes. All three are gone. **The board id** carried no information — through the bus you never name a board, the address picks it, so naming one was a second way to say a thing the address already said. **The offsets** were a second address space, and the same digits meaning two things depending on a qualifier is a trap laid for the operator. **The read side** existed only to reach a store the bus could not see — a bank that is not selected, or a phantomed-out board — and both of those are `properties()`: `SET mem0 bank=3`, `SET mem0 phantom=…`. Select the thing you want to look at, exactly as the guest has to. What was left was the one thing a bus cycle genuinely cannot do, and that is this.)*

The alternative — giving `BusCycle` an `origin = Cpu | Monitor | Dma` field so ROM could accept "monitor" writes — was rejected. A real backplane cycle carries no such tag; that is *why* a front-panel DEPOSIT is indistinguishable from a CPU write, and why a real ROM ignores both. Add the tag and every board built hereafter has to reason about it.

**There is no `BANK=` qualifier, and there must not be one.** Bank *count*, bank *size*, and the bank-select *port* are all board-specific: Cromemco, CompuPro, and Alpha Micro memory boards bank in incompatible ways. A `BANK=<n>` argument in the monitor would hardcode one banking model into a CLI that is supposed to know nothing board-specific — the same error as a bus that invents interrupt vectors (§4.4).

Instead, **banking is reached through the board**: **the live bank is a `properties()` value like any other.** `SHOW mem0` reports `banks` and `bank`; a board whose banking is software-selectable exposes `bank` as a runtime property. The generic `SET`/`SHOW` layer (§5) carries it, so the monitor, the TOML loader, MCP, and tab completion all get it for free — and a memory board with a scheme nobody has thought of yet still works. `SET mem0 bank=3`, then read ordinary addresses — which is what the guest does, because it is all the guest can do.

*(This used to have a second answer: `RAW <id>` addressed the store linearly, so bank 3 simply **was** offset `0x30000` and `DUMP 30000-300FF RAW mem0` needed no new syntax. That answer went with `RAW`, above, and the argument does not miss it — the objection to `BANK=` was never that a workaround existed, it was that the monitor must not learn one card's banking model. Selecting a bank and reading normal addresses concedes nothing to that. The store **is** still planed that way internally — `MemoryBoard::plane()`, and `storeSize()`/`storeAt()` let a test say so — but that is the card's business now, not a thing anybody types.)*

`DUMP`/`DISASM` annotate output when bytes came from a phantom overlay or from a bank that is not currently live, so a confusing read explains itself.

### 10.3 Intel HEX

Loader accepts record types **00** (data), **01** (EOF), **03**/**05** (start address — captured, optionally sets PC). **Validate every record's checksum and fail loudly with the record number** — a silently truncated load is a miserable bug to chase. Types 02/04 are accepted if they resolve within 64K, else error. `AT <addr>` biases the record addresses.

Writer emits 00/01 with a configurable record length (default 16) and an `03` start record if an entry point is given. **Round-trip is a test case:** `SAVE x.hex 0-FFFF` then `LOAD x.hex` must reproduce memory byte-for-byte.

Binary is a flat image: `LOAD` needs `AT <addr>`, `SAVE` needs an explicit range. Autodetection sniffs for a leading `:` and printable HEX records; `FORMAT=` forces it.

The same engine backs the MCP tools (`mem_load`, `mem_save`, `mem_dump`, `disasm`) — one implementation, two front ends.

### 10.3.1 Built-in ROMs

**The common ROMs are compiled into the binary.** There is no portable place to keep ROM images across the four targets — `/usr/share`, `~/Library`, `%APPDATA%`, and "next to the executable" are four different answers, each of which becomes a support question and a platform special case in exactly the code §2.1 exists to keep clean. ROMs are small (a boot PROM is 256 bytes), so embedding them makes the problem vanish: a fresh checkout boots on any OS with nothing to download.

Source images live in **`roms/`** in the repo. At build time CMake turns each into a byte array in a generated translation unit, and a registry maps `builtin:<name>` → `{span<const uint8_t>, crc32, description}`.

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  mount = "builtin:dbl"      # compiled in
# mount = "roms/mine.bin"    # a bare path is a host file, and it always wins
```

`builtin:` reuses the scheme idiom already established for `connect` (`socket:`, `serial:`, `file:`), so it adds no grammar. Consequences worth stating:

- **The board never knows the difference.** A region takes a `span<const uint8_t>`; whether it came from `.rodata` or from a file the host service read is not the board's business (§7). `builtin:` is resolved by the config loader, above the board.
- **No filesystem access at runtime** for a built-in.
- **`CONFIG SAVE` round-trips the name, not the bytes.**
- **Built-ins are a convenience, never a lock-in** — a path overrides, so anyone with a different dump of the same part uses it without patching the simulator.

**Every built-in ROM has a provenance row in `docs/roms.md`** — source, exact size, CRC32 — and a unit test verifies the CRC at build time. This is §0.1 applied to binaries: **a ROM image is a hardware fact**, and an embedded blob of unknown lineage is the worst kind of second-hand fact, because every piece of software above it would then be debugged against the wrong ground truth and it would look like a software bug for a very long time. Nothing is embedded without a source.

### 10.3.2 Symbol files — `.PRN` and `.SYM`

**A symbol table is NOT `LOAD`, and the wrong answer is silent.** `LOAD` is memory all the
way down: every format it accepts becomes bytes in the 64K address space, and its sniffer is
a two-way branch — a leading `:` is Intel HEX, everything else is a flat binary. So
`LOAD prog.SYM AT 100` does not error; it deposits the symbol file's ASCII into RAM. A symbol
table has no address space to land in — it is the debugger's *names* for one — so it is its
own top-level verb, `SYMBOLS`, the way `CONFIG` is a separate verb precisely because what it
loads is not memory. `core/symbols.h` mirrors `core/hex.h`: one implementation, and the
`SYMBOLS` command, the `startup` re-load, and the MCP tool are its front ends.

**Host-side, like a breakpoint.** The table belongs to no card — it is a view *of* the address
space, not a property *in* it — so it lives on `Machine`, not a board, and the "no
machine-level board state" rule (`machine.h`) does not reach it. It survives `RESET`, `POWER`
and `CONFIG LOAD` exactly as breakpoints do, and `SYMBOLS CLEAR` is its `NOBREAK`. A machine
file may name a symbol file in `startup`; `CONFIG SAVE` round-trips the **filename, not the
parsed table**, the same bargain `builtin:` makes for a ROM (§10.3.1).

**Reference first, display later.** A loaded symbol resolves anywhere a *true address* is
typed (`BREAK`, `DUMP`, `EXAMINE`, a `BREAK … IF` operand) — not where a port or a byte is,
which is why the resolution is gated per call-site and not folded into the one overloaded
address parser. A symbol wins over a bare hex literal, with the same leading-zero escape that
tells the register `A` from the number `0A`. Annotating disassembly (`JMP 0100` → `JMP START`)
is deferred: an `Insn` melts its operand into text before anyone sees it, and `%W` is not
always an address (`LXI B,%W` is a constant, `JMP %W` is not), so correct annotation needs an
operand-kind split across both opcode tables. The reverse (address→name) map is built now so
that work is display-only when it comes.

**Two formats, and absolute addresses only.** A **`.PRN`/`.LST`** is an assembler listing (CP/M
`ASM`, Microsoft `M80`, DR `MAC` — one column geometry: address in columns 2–5, `=` in column
7 marks an `EQU`, source at column 17). Because it marks an `EQU`, it tells a constant from a
program label, and **only labels feed the reverse map** — otherwise a `BDOS EQU 5` makes
address `0005` render as `BDOS`. A **`.SYM`** is the flat `HHHH NAME` symbol file DR `MAC`/`RMAC`
write and `SID` reads; it has no label/constant distinction, so it feeds name→value only.
(Microsoft **`L80` writes no `.SYM`** — it prints its map to the console. The `.SYM` is an
assembler product, not the linker's.) A relocatable `M80` listing marks its addresses with a
trailing apostrophe and is **refused, by the line** — a module-relative address referenced as
if absolute is a silent wrong answer. A `.SYM` is post-link and absolute already.

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
- `mem_dump`, `mem_deposit`, `mem_load`, `mem_save`, `mem_search`, `mem_fill`, `disasm` — same `raw` qualifier as the monitor, same engine
- `mount`, `connect`
- `reset(kind: bus|cpu|power)`
- `board_list`, `board_types`, `board_get(id)`, `board_set(id, key, value)` — **schemas generated from `Board::properties()`**
- `bus_map()`, `bus_io()`, `bus_irq()`, `bus_contention()`, `who(addr)` — structured, not the ASCII table

**MCP is a first-class interface, not a wrapper.** The monitor and MCP both sit on one `Machine` API and one `properties()` reflection layer, so they cannot drift. Any board added later is fully drivable from both without touching either.

---

## 12. Host file transfer

Two complementary mechanisms, serving different needs.

> **We do not implement AltairZ80's port-0xFE "SIMH pseudo device", and *AltairZ80's* `R.COM`/`W.COM` will not run here.** That device is not real Altair hardware — it is another simulator's invention, and its only purpose is compatibility with that simulator's own guest utilities. Reimplementing it would mean deriving from AltairZ80's source, which this project does not do (§0.1). Instead we design our own host bridge as if it were a real S-100 card — which is, after all, what this project is *for*.
>
> **Our own `R.COM` and `W.COM` do exist, and they are ours** (§12.1). We reuse the *names*, because the muscle memory is worth keeping and there is no reason to make people learn a second pair. Nothing else is shared: theirs talk to a pseudo-device at 0xFE and were written in SPL; ours talk to a card of our own at 0xB0 and are 8080 assembler. Neither will run in the other simulator, and that is the honest outcome — not an accident, and not a compatibility goal we quietly dropped.

### 12.1 The Host Bridge board (guest-initiated) — **BUILT**

A board of our own design (`docs/boards/hostbridge.md`), documented to the same standard as any period card. It is an ordinary `Board` — it decodes two I/O ports, has `properties()`, honors both resets, and serializes. No bus special cases.

It is also **the project's first genuinely new piece of hardware**, which makes it a real test of the board API rather than a rehash of a known card. The API held: it needed nothing.

**Sandboxing is a hard requirement**, not a nicety: guest-supplied filenames resolve against a configured `hostdir` root and **cannot escape it** — no `..` component, no absolute path, no drive letter, and no symlink that resolves out. A guest program must never be able to write anywhere on the host disk. The card itself touches no file handle (§7): all of it lives in `src/host/hostdir.{h,cpp}`, where it is tested against a **real** filesystem with **real** symlinks, because a symlink escape cannot be tested against a fake one.

The card sits at **0xB0**, and that number took a census. `0x30` was the obvious pick and it is wrong — the WD179X floppy controller defaults there and the Cromemco 64FDC sits on top of it, which is exactly the mistake AltairZ80 made with 0xFE. `B0–BF` and `D0–DF` are the only empty 16-port holes left in either catalog.

Guest-side utilities (`R.COM`, `W.COM`, `HDIR.COM`) are ours, written in 8080 assembly and assembled **inside the machine** with the period toolchain — `ASM.COM` and `LOAD.COM`, off the CP/M disk. No cross-assembler, no host tooling: `cpm/hostbridge/*.ASM` reaches the disk by `PIP R.ASM=CON:` and is built from there, which is what the acceptance test does on every run.

**Every disk operation in them is a BDOS call** — no BIOS entries, no `IN`/`OUT` to a controller, no assumption about a DPB, a sector size or a skew table. That is what makes one `R.COM` work on an 8″ 88-DCDD, an 8 MB image, an 88-MDS minidisk, and any BIOS anybody writes later. It is proved, not asserted: the acceptance test builds the utilities on an 8 MB 88-DCDD image and then `LOAD`s the same hex on a minidisk behind a **different controller**, and round-trips the same 256 bytes.

### 12.2 Host-side filesystem access to an image — **deferred, and here is the hard part**

The intent is real: read and write files in a mounted image **with the guest not running at all** — no boot, no console driving. For Claude that is far more efficient than typing at a CP/M prompt, and it is the right way to stage a test fixture or pull out a build artifact.

**But `DISK LS` cannot be a generic monitor command, and an earlier draft of this design had it as one. That was wrong.** Reading a file out of an image requires *two* independent facts, and the simulator holds neither in a place a generic command can reach:

1. **The sector layout** — which is a property of the **controller**. The 88-DCDD is **hard-sector**: its images contain the entire 137-byte slot, headers and checksum included, so the payload lives at offset 7 of each slot (3 on a system track). Tarbell, Disk 1A, and North Star are **soft-sector**: their images hold the payload *only*, because the headers lived in the inter-sector gaps and never reached the file. A reader that does not know which kind it is holding reads garbage.
2. **The CP/M filesystem parameters** — the DPB, the reserved-track offset, and the BIOS *software* skew. These belong to the **image's CP/M**, not to the controller, and two images on the same controller can legitimately differ.

So a naive `DISK LS` needs a wrapper per controller **×** per image format. Its apparent genericity would be a lie, and the failure mode is the worst kind: it produces a plausible-looking directory listing off a misparsed image.

**Therefore: no `DISK` verbs in the monitor and no `disk_*` tools over MCP, for now.** The capability is deferred, not cancelled; §7.3's `DiskImage` gives it a foundation (the *controller* declares the layout, so half the problem is already solved in the right place), and the remaining half — a named CP/M filesystem descriptor supplying DPB and skew — is a later design task. `cpmtools`' `diskdefs` is the precedent, and it exists precisely because this cannot be inferred.

Until then, **§12.1's Host Bridge is the supported path**, and host-side image surgery is what `cpmtools` is for.

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
| ~~**88-ACR**~~ | ~~Cassette-specific control bits (motor control, if any).~~ **DISCHARGED 2026-07-12 — the card is BUILT.** The manual is in the tree, and the answer to the open question was **there is no motor control at all**: no transport register, nothing the guest can write that reaches the recorder, and an operator who pressed the buttons with their finger. The row had also guessed wrong about *what* was unknown — the ports and the bit sense were never the interesting part. See `docs/boards/mits-88acr.md`. | ~~Milestone 5~~ |
| **88-PIO / 88-4PIO** | Bit layouts and handshake (CA1/CB2) semantics. Only port numbers are known: PIO 0x04/0x05; 4PIO 0x20–0x23. | Milestone 7 |
| ~~**88-VI / RTC**~~ | ~~Register layout, priority scheme, RST vector generation. Nothing at all in the tree.~~ **DISCHARGED 2026-07-13 — the card is BUILT.** The manual is in the tree (`reference/88-VI-RTC.pdf`) and answered all three: one write-only control port at **376Q (0xFE)**, **VI0 highest / VI7 lowest**, level *n* → `RST n`. It also **contradicted itself**, and the tie was broken by disassembling the only real client we have — the PS2 monitor's own service routine — which proved bits 0–2 are the *ones-complement* of the level and that bit 3 gates the compare. **When the document and the artifact disagree, disassemble the artifact.** See `docs/boards/mits-88virtc.md`. | ~~Milestone 6~~ |
| **88-HDSK** | Ports, command protocol, geometry, image format. Nothing at all in the tree. | Milestone 7 |
| **PMMI** (deferred) | The **E1–E7 pad → VI0–VI7 correspondence** — the manual says only to consult your CPU/VI card manual. Everything else is recovered. | If/when PMMI is built |
| **88-TURNKEY / PROM** | **How power-on jump works.** A turnkey board forces the CPU to the PROM address after reset; the mechanism is undocumented in the tree. Nothing is blocked — `startup = ["GO FF00"]` covers it honestly (§10.0) — but modeling POJ as a real board property is the correct long-run answer, and it would test whether a `Board` can claim an `OpFetch` cycle the way the 88-VI claims an `IntAck`. | Nice-to-have; blocks nothing |

**Available and sufficient:** the 88-2SIO, the 88-SIO, the 88-ACR, the 88-DCDD and the Tarbell — every one of them from a **period manual**, all now in `reference/` and listed in `docs/sources.md`.

> **🔴 THIS SECTION USED TO NAME `mits_dsk.c` AS AUTHORITATIVE FOR THE 88-DCDD, AND IT WAS FLATLY WRONG.**
> `mits_dsk.c` is **SIMH**, and §0.1 — the first rule in this document — says we do not learn hardware
> from another emulator's source. The line sat here contradicting the rule it shares a document with,
> and it cost real time: the DCDD's rotation was modeled on SIMH's advance-on-read counter, which makes
> the platter spin at the speed of whatever loop is polling it. **Both the "inconsistency to settle"
> and its recommended arbiter are struck.** The `I`/`Z` status bits were settled from the 88-DCDD
> manual and the in-tree `BOOT.ASM`/`BIOS.ASM`, and `docs/boards/mits-dcdd.md` records which source won
> and why.

---

## 18. Roadmap

See `docs/roadmap.md`. Milestone 1 is **CLI + MCP + 8080 + bus (incl. interrupts) + RAM + 88-2SIO**, and nothing else.
