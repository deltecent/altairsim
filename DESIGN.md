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

Payoff: a board author writes against `ByteStream`, `DiskImage`, `Display`, `EventQueue` and **never sees an OS detail on any platform** — but only if the OS layer is genuinely sealed.

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

A card's cores are **units** (§3.0.1) — a plain 88-CPU has exactly one, and a dual-processor card has two with one active. Swapping the *card* is `BOARD REMOVE` / `BOARD ADD`, exactly as you'd swap the physical thing.

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

**Patrick, 2026-07-11:** *"If I make an 8080 board with an onboard serial port, and another 8080 board without one, they should be able to utilize the same 8080 CPU glue and disassembly."*

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

*(The word `CPU` does double duty — `DISASM ... CPU=8080` names an instruction set, while `BOARD ADD 8080 cpu0` names a card. That is deliberate: `CPU=` is the word everyone already uses, and inventing a second one to be precise about a distinction the user does not have to care about would cost more than it buys.)*

### 3.0.3 Registers are reflection, and the debugger is a bus observer

**Registers use the same trick as `properties()` (§5).** A core exposes `registers()` — name, width, get, set — and *not* an `Regs8080` struct the monitor knows about:

```cpp
struct RegDef { const char* name; int bits; /* get, set */ };
```

Then `REGS`, `SET REG A=3F`, breakpoint conditions (`BREAK 100 IF A==0`), `SNAPSHOT`, and the MCP schema **all work for a Z80 or a 6502 the day it lands, with no monitor change.** It is the same bet that already paid for `SET`/`SHOW`/TOML/MCP: one schema, no second copy to drift.

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

### 4.2 PHANTOM* is a signal a board pulls — the bus does not arbitrate an overlay

**An earlier draft got this wrong in exactly the way §4.4 warns about.** It said a memory board has a "phantom mode" of `none | read | write | both`, and that a ROM board with `phantom = "read"` *"overlays the RAM beneath it for reads while writes fall through."* That describes the **bus** looking at two boards, picking a winner, and routing reads to one and writes to the other. The bus does not do that, any more than it invents an interrupt vector.

**What actually happens on the backplane:** PHANTOM\* is **S-100 pin 67**, a line any board may pull low. A boot ROM pulls it; memory boards that are strapped to honor it **disable their own drivers** while it is low. Nobody arbitrates. The overlay is *emergent* — the ROM is the only board still answering, because the RAM switched itself off.

So the model is two ordinary board behaviors, and no bus special case:

```cpp
// Pull PHANTOM* for the cycles I mean to shadow. A board strap:
//   phantom = none | read | all      (see docs/boards/memory.md)
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

**The read/write distinction lives on the *asserting* board, because that is where the gate physically is.** A card that ANDs PHANTOM\* with its read strobe simply does not pull the pin during a write cycle, so memory answers the write like any other and the byte lands in RAM. **`honors_phantom` is therefore a plain bool and must never grow a `read` mode** — the honoring card has no idea any of this is going on, which is exactly why it works with *any* card that pulls the pin.

Payoffs, which is how you know it is right:
- **"Writes fall through" is no longer a special rule.** It is what happens when a ROM asserts PHANTOM\* on reads and not on writes. Shadowing *both* is equally expressible, and neither is a case in the bus.
- **`honors_phantom` is a board strap, because on real hardware it is a jumper.** A memory board that ignores PHANTOM\* keeps driving, you get contention (§4.6), and the simulator reports the same bug the real backplane would have handed you.
- **The operator can still write ROM, and the guest still cannot** — because `RAW <id>` (§10.2) reaches behind the bus into the board's store. Burning a PROM is not a bus operation on real hardware either; you pull the chip. Model it as a bus write and the bus would have to know *who originated a cycle*, which a real backplane cannot know and no board should ever have to ask.
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

Note that one board occupies **two disjoint ranges**, because a real card carries several populated regions and empty sockets between them (`docs/boards/memory.md`). The map is per-*range*, not per-board.

> **Open — needs a manual, per §0.1.** *Which* boards disable themselves, at *which* port, with *what* bit? That is board-specific and I will not guess it. The mechanism above is general and costs nothing; the specific straps get filled in per board as the manuals arrive. **Patrick: which board do you want first?**

### 4.2.2 `snoop()` — every board sees every cycle

**The address bus is not addressed to anyone.** It is simply present, and any card may watch it whether or not it answers. So `Board` has a third bus method, called **once per completed cycle, on every board**:

```cpp
virtual bool assertsPhantom(const BusCycle&) const;   // COMBINATIONAL. Pure.
virtual bool decodes(const BusCycle&) const;          // COMBINATIONAL. Pure.
virtual void snoop(const BusCycle&);                  // CLOCKED. Latch here, and only here.
```

The first two are called **several times per cycle** — once to resolve PHANTOM\*, again inside each board's own decode — so they must have no side effects. `snoop()` is the clocked half, and it is the **only** place a board may latch what it saw.

**The card that forced this is the Tarbell** (`docs/boards/tarbell.md`). Its 32-byte boot PROM shadows RAM from POC\*, and it releases PHANTOM\* permanently the first time it sees a memory read with **A5 high**. Both halves are load-bearing:

- The release must be **combinational**, because the bootstrap's own first fetch out of the PROM *is* the read with A5 high. If the release waited a cycle, that fetch would happen while memory was still shadowed, read `0xFF` off the floating bus, and no Tarbell would ever have booted.
- The release must also **latch**, or a later data read below `0x20` would re-shadow the PROM over the sector just loaded there.

The bus does not know any of this happened. It is one flip-flop, on one card, and that is the point: a real backplane has no "notify" mechanism, so neither does this one — `snoop()` is not a callback, it is a card looking at wires that were in front of it the whole time.

### 4.3 Banking — the strongest evidence in this document that boards must own their decode

A banked memory board registers a bank-select I/O port; a guest write selects which plane is live. This is an ordinary `IoOut` that mutates the board's **own** state — **not** a bus special case, and the same mechanism as §4.2.1's enable/disable.

**What a bank select *does* is the board's business, and this document states no rule about it.** On the five cards below it swaps the 64K plane. On some other card it might not. The bus carries the `OUT` and the board decides — that is the entire point.

If you are ever tempted to hoist banking into the bus or the monitor, read this table first. These are five **real** cards (`docs/boards/memory.md`, sourced from `s100_bram.c`):

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

### 4.5 DMA is bus mastering

A board asserts `pHOLD`; the bus grants (`pHLDA`) at the next instruction boundary; the board then acts as a `BusMaster` — the same interface the CPU uses — and drives its own cycles. Stolen T-states are charged to the clock, so the CPU genuinely loses time rather than getting DMA for free. When the board releases, the CPU resumes mastering.

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

So **no board may ever manufacture `0xFF`**, and in particular **no board may seed its own store with it.** A RAM chip does not power up holding `0xFF`; it powers up holding whatever it feels like, which is what `fill = random` is for, and *what a card's chips contain is that card's business* (`docs/boards/memory.md`). The bus does not initialize anyone's memory and has never heard of `fill`.

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

**What POC\* does is board-specific**, and each board's `.md` must say **concretely** what each reset does to it. Examples:
- **`memory`**: both resets clear the bank-select latch to 0 and touch nothing else. `POWER` re-fills RAM regions per `fill` and re-reads ROM regions from their files. See `docs/boards/memory.md`.
- **A boot ROM that disables itself** (§4.2.1): `PowerOn` **re-enables it** — otherwise the machine boots exactly once and never again. Whether `Bus` (the front-panel reset button) also re-enables it is a **board-specific strap, and the board's `.md` must say which.** This is the single most likely place to produce the classic "works from power-on, dead from the reset button" bug, because a warm reset that leaves the ROM switched out drops the CPU onto RAM at 0000 and it executes garbage.
- **88-2SIO**: 6850 master reset on both (clears RDRF/TDRE, drops RTS), but **keeps its `ByteStream` connected**.
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

### 7.3 `DiskImage` — the generic mountable medium

Every disk/tape board (88-DCDD, 88-HDSK, Tarbell, Disk 1A, North Star, any future controller) sees only this. `MOUNT` binds an implementation to a unit.

**The interface is CHS, not LBA, and the format is per-track.** Both of those are forced by real disks, and an LBA interface with a single global geometry cannot express them:

```cpp
enum class Density { SD, DD };

class DiskImage {
public:
    // The BOARD describes the medium: overall shape, then one or more TRACK RANGES.
    void init(int tracks, int heads, bool interleaved);
    void initFormat(int trackLo, int trackHi, int headLo, int headHi,
                    Density, int sectors, int sectorSize, int startSector);

    virtual bool readSector (int t, int h, int s, uint8_t* buf, size_t* n) = 0;
    virtual bool writeSector(int t, int h, int s, const uint8_t* buf, size_t* n) = 0;

    virtual bool   readOnly() const = 0;
    virtual void   sync() = 0;
    virtual size_t size() const = 0;
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

The board still owns what is *inside* the slot — for the DCDD, that the payload starts at offset 7 on a data track and 3 on a system track, and that a checksum sits at [4]. That is the controller's business, exactly as `docs/boards/88-dcdd.md` says.

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

Implementations: `RawImageFile` (the common case — buffered, dirty write-back), `ReadOnlyImage`, `MemoryDisk`, and later `ImdImage`/`Td0Image` (which carry their own per-track format, and so fit this shape rather than fighting it).

*Modeled on `simh.mdsk/Altair8800/altair8800_dsk.c` (© 2025 Patrick A. Linstruth) — our own prior art, not another project's.*

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
- **`CONNECT` and `MOUNT` are generic**, not per-board commands. The monitor resolves an endpoint string to a `ByteStream` or a file to a `DiskImage` and hands it to whichever board declared a unit of that type — so a board written next year gets `MOUNT`/`CONNECT` for free without touching the monitor. Note the division of labor: the *monitor* opens the file; the *board* decides what its bytes mean (§7.3).

---

## 8. Timing and host idling

- Clock is the **CPU board's** `clock_hz` (default 2,000,000) or `0` for free-running — not the machine's (§3). The crystal is on the card, and a backplane with no CPU card has no clock rate at all.
- Throttle by comparing accumulated T-states against a monotonic host clock in ~1 ms slices and **sleeping** the remainder — never spin.
- **Idle detection** — steal this from the Python prototype; it is what makes automation work. Count consecutive console-status reads that return RDRF=0 **with no intervening I/O of any kind**. Any data read, char write, or disk port access resets the counter. Past a threshold, the machine is *provably parked at a prompt*: the run loop reports `idle`, and **the host process sleeps instead of emulating a spin loop.** This is what stops a CP/M prompt from pinning a core, and what lets automated builds terminate promptly instead of burning 20M steps.

---

## 9. Devices, `MOUNT`, and `CONNECT`

- A board declares typed **units**, and **a unit is a NAME, not an index** (Patrick, 2026-07-11). A disk unit accepts `MOUNT id:unit <hostfile>` (`UNMOUNT` to release); a serial unit accepts `CONNECT id:unit <endpoint>` (`DISCONNECT`).

  **ONE CARD IS NOT ONE KIND OF THING.** A card may carry drives *and* ROM sockets *and* a serial port — the Tarbell we already model carries a boot PROM and a floppy controller, and a controller with its own PROM, scratch RAM and a serial port on one board was a completely ordinary 1977 product. Nothing in the bus model ever assumed otherwise: `decodes()` is asked about every cycle and `BusCycle::type` distinguishes memory from I/O, so one card answers both. `tests/test_units.cpp` builds exactly such a card and proves it.

  So units are named and typed — `MOUNT dj:drive0`, `MOUNT dj:rom0`, `CONNECT dj:tty` — and **the kind is checked**: mounting a disk image onto a serial port is an error with a sentence explaining it. The integer scheme could not be made safe, which is why it is gone: with a flat namespace, `MOUNT dj:4` on a serial unit can only *fail*, never *explain*, because the board has nothing left to distinguish 4-the-drive from 4-the-port. `SHOW <id>` lists the units, and it reads `Board::units()` — the same list MOUNT reads, so they cannot disagree.
- Endpoints: `console` | `socket:PORT` (listening) | `socket:HOST:PORT` (outbound) | `serial:/dev/tty.usbserial-X` or `serial:COM3` | `file:path` | `null`.
- Exactly one unit may hold `console` at a time; the monitor arbitrates.
- Disk images are buffered and written back. **The board** probes the image size against the formats *it* knows and declares the layout to `DiskImage` (§7.3); `media = "8in" | "minidisk" | "fdc8mb"` forces the choice when the size is ambiguous. `readonly` supported (the real board's write-protect).

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
  SET <id> <k>=<v>                 generic; e.g. SET sio2a BAUD=9600, SET mem0 PHANTOM=read
  SHOW ROMS                        every ROM compiled in: name, size, CRC32, description
                                   (use as mount = "builtin:<name>" — see §10.3.1)

INTROSPECTION
  SHOW BUS                         CPU, clock, T-states, pending IRQ, DMA state
  SHOW BUS MAP                     memory decode map: range -> board, type, phantom, bank
  SHOW BUS IO                      I/O decode map: port -> board, direction
  SHOW BUS IRQ                     VI0-VI7 assignments, pending lines, priority
  SHOW BUS CONTENTION              every address/port claimed by more than one board
  WHO <addr> | WHO IO <port>       reverse lookup: who responds here, and why

MEDIA AND CONNECTIONS
  MOUNT <id>:<u> <file> [RO]       UNMOUNT <id>:<u>
  CONNECT <id>:<u> <endpoint>      DISCONNECT <id>:<u>

CONSOLE
  SHOW CONSOLE                     every property, value, legal values
  SET CONSOLE <k>=<v>              UPPER, STRIP7IN, STRIP7OUT, CRLF, BSDEL, TABS, ECHO,
                                   ANSI, ROWS, COLS, PACE, ATTN, LOG, BELL  (list will grow)
  CONSOLE                          enter the console; ATTN key returns to the monitor

MEMORY
  LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [RAW <id>]    format autodetected
  SAVE <file> <range> [FORMAT=BIN|HEX] [RAW <id>]
  DUMP [<addr>|<range>] [WIDTH=16]  hex + ASCII. A bare <addr> runs to the END OF ITS
                                    PAGE (D 0001 -> 0001-00FF); bare DUMP continues
                                    from there. Page-aligned in and out, so the rows
                                    and the columns both stay put.
  DISASM <range>|<addr> [n] [CPU=8080]
                                    Mnemonics follow the ACTIVE CPU -- you never type
                                    CPU=. It is the override for a machine with no CPU
                                    card in it, or for looking at foreign code (§3.0.2).
  EDIT <addr>                       interactive: show byte, type new value, Enter advances
  EXAMINE [<addr>]                  ONE byte: hex, ASCII, bits. Bare = EXAMINE NEXT,
                                    the front-panel switch. Its own cursor, not DUMP's.
  DEPOSIT <addr> <bytes...>
  FILL <range> <byte>
  SEARCH <range> <bytes...>|"str"
  COMPARE <range> <addr> | COMPARE <range> <file>
  MOVE <range> <dest>
  All of the above take an optional RAW <id> to address one board's store directly (§10.2).

I/O
  IN <port>                         run a real IN cycle -- with real side effects
  OUT <port> <byte>                 run a real OUT cycle
  WHO IO <port>                     who WOULD answer -- looks without touching

EXECUTION
  GO [addr] | STEP [n] | STOP
  RESET | RESET CPU | POWER
  There is NO `SET CPU`. The CPU is a CARD (§3): BOARD ADD 8080 cpu0, and the clock
  is that board's property -- SET cpu0 clock_hz=2000000. A card carrying both an
  8080 and an 8085 exposes them as UNITS and switches between them itself (§3.0.1).

DEBUG
  BREAK <addr> [IF <expr>] | BREAK IO <port> | BREAK MEM R|W <range> | NOBREAK
  REGS | SET REG <r>=<v>            Generic: registers are reflection (§3.0.3), so a
                                    Z80 works the day it lands with no monitor change.
  TRACE ON|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]   HISTORY [n]
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
name     = "cpm-dev"
clock_hz = 2_000_000
sense    = 0x00                 # port 0xFF front-panel sense switches

startup = [                     # monitor commands, run in order after boards are created
  "GO FF00",                    # the DBL PROM. This is the operator's keystroke, not fake hardware.
]
```

Three things fall out of this, and they are the reason it is the right shape:

- **The config language and the script language become one language.** A `startup` entry is an ordinary monitor command, so anything you can type, a config can do — and `altairsim -s script.cmd` and `CONFIG LOAD` stop being two different worlds.
- **`BOOT`'s special-casing disappears.** No verb needs to know what a "boot device" is, and a new disk controller written next year needs no monitor change to be bootable.
- **It is transparent.** `SHOW MACHINE` prints the startup commands; `CONFIG SAVE` round-trips them verbatim. Nothing happens that the user cannot see written down.

> **Caution, and it must be in the docs:** `CONFIG LOAD` on a machine file now *executes commands*. Loading a `.toml` from an untrusted source runs whatever is in its `startup` list. Keep `startup` to monitor commands only, and say so out loud.

### 10.1 `SET`/`SHOW` are generic

Implemented once against `Board::properties()`; they know nothing about baud rates, phantom modes, or disk geometry. Adding a board adds its settings to the CLI for free. Config-time vs runtime settability is enforced by the board and displayed by `SHOW`, so `SET fdc DRIVES=8` on a running machine is rejected clearly rather than half-applied.

### 10.2 Memory access: through the bus, or behind it?

- **Default: through the bus.** `DUMP`, `DISASM`, `DEPOSIT` see exactly what the CPU sees — live bank, PHANTOM overlays applied, ROM not decoding writes, contention reported. Addresses are **bus addresses**, 0x0000–0xFFFF. This is the only view that tells the truth about a misbehaving decode.
- **`RAW <id>`: behind the bus**, straight into one board's backing store. Addresses are **board-local offsets** into that board's store, which may be far larger than 64K. This is how you inspect a phantomed-out board the CPU cannot see — and how you get bytes *into* a ROM.

**`RAW` is the PROM burner, and that is not a metaphor.** A ROM region does not decode a write cycle (§4.2), so `DEPOSIT FF00 41` cannot possibly reach it — nor should it, because on real hardware a bus write can't program a PROM either. You pull the chip and put it in a programmer, which is *not a bus operation*. `LOAD dbl.hex RAW mem0` is exactly that, and it is why **the operator can write ROM while the guest cannot**, with no `writable` flag to leak and no originator tag on the bus.

The alternative — giving `BusCycle` an `origin = Cpu | Monitor | Dma` field so ROM could accept "monitor" writes — was rejected. A real backplane cycle carries no such tag; that is *why* a front-panel DEPOSIT is indistinguishable from a CPU write, and why a real ROM ignores both. Add the tag and every board built hereafter has to reason about it.

**There is no `BANK=` qualifier, and there must not be one.** Bank *count*, bank *size*, and the bank-select *port* are all board-specific: Cromemco, CompuPro, and Alpha Micro memory boards bank in incompatible ways. A `BANK=<n>` argument in the monitor would hardcode one banking model into a CLI that is supposed to know nothing board-specific — the same error as a bus that invents interrupt vectors (§4.4).

Instead, **banking is reached through the board, two ways, both of which already exist**:

1. **`RAW <id>` addresses the board's store linearly.** On a 4 × 64K banked memory board, bank 3 simply *is* offset `0x30000`. `DUMP 30000-300FF RAW mem0` needs no new syntax and no new concept.
2. **The live bank is a `properties()` value like any other.** `SHOW mem0` reports `banks` and `bank`; a board whose banking is software-selectable exposes `bank` as a runtime property. The generic `SET`/`SHOW` layer (§5) carries it, so the monitor, the TOML loader, MCP, and tab completion all get it for free — and a memory board with a scheme nobody has thought of yet still works.

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

> **We do not implement AltairZ80's port-0xFE "SIMH pseudo device", and `R.COM`/`W.COM` are not supported.** That device is not real Altair hardware — it is another simulator's invention, and its only purpose is compatibility with that simulator's own guest utilities. Reimplementing it would mean deriving from AltairZ80's source, which this project does not do (§0.1). Instead we design our own host bridge as if it were a real S-100 card — which is, after all, what this project is *for*.

### 12.1 The Host Bridge board (guest-initiated)

A board of our own design (`docs/boards/hostbridge.md`), documented to the same standard as any period card. It is an ordinary `Board` — it claims two I/O ports, has `properties()`, honors both resets, and serializes. No bus special cases.

It is also **the project's first genuinely new piece of hardware**, which makes it a real test of the board API rather than a rehash of a known card.

**Sandboxing is a hard requirement**, not a nicety: guest-supplied filenames resolve against a configured `hostdir` root and **cannot escape it**. A guest program must never be able to write anywhere on the host disk.

Guest-side utilities (`HGET.COM`, `HPUT.COM`, `HDIR.COM`) are ours, written in 8080 assembly and assembled with the period toolchain.

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
| **88-ACR** | Cassette-specific control bits (motor control, if any). Only the port numbers (0x06/0x07) and the inverted-SIO bit sense are known. | Milestone 5 |
| **88-PIO / 88-4PIO** | Bit layouts and handshake (CA1/CB2) semantics. Only port numbers are known: PIO 0x04/0x05; 4PIO 0x20–0x23. | Milestone 7 |
| **88-VI / RTC** | Register layout, priority scheme, RST vector generation. Nothing at all in the tree. | Milestone 6 |
| **88-HDSK** | Ports, command protocol, geometry, image format. Nothing at all in the tree. | Milestone 7 |
| **PMMI** (deferred) | The **E1–E7 pad → VI0–VI7 correspondence** — the manual says only to consult your CPU/VI card manual. Everything else is recovered. | If/when PMMI is built |
| **88-TURNKEY / PROM** | **How power-on jump works.** A turnkey board forces the CPU to the PROM address after reset; the mechanism is undocumented in the tree. Nothing is blocked — `startup = ["GO FF00"]` covers it honestly (§10.0) — but modeling POJ as a real board property is the correct long-run answer, and it would test whether a `Board` can claim an `OpFetch` cycle the way the 88-VI claims an `IntAck`. | Nice-to-have; blocks nothing |

**Available and sufficient:** the 88-2SIO (MITS *Theory of Operation* manual, `s100-manuals/MITS/ALTAIR_8800/`), the 88-DCDD (`mits_dsk.c` + `BIOS.ASM` + `CLAUDE.md`), and the PMMI (its manual, `pmmi-cpm22/`).

**One inconsistency to settle:** the Python prototype and `BIOS.ASM` disagree on the 88-DCDD's `I` and `Z` status-bit positions. `mits_dsk.c` is authoritative — reconcile against it (§`docs/boards/88-dcdd.md`).

---

## 18. Roadmap

See `docs/roadmap.md`. Milestone 1 is **CLI + MCP + 8080 + bus (incl. interrupts) + RAM + 88-2SIO**, and nothing else.
