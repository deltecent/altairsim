# Serial I/O

Every card that moves characters — the 88-2SIO, the 88-SIO, the ACR cassette, the line
printer, the paper-tape reader, the PMMI modem, the Turnkey's onboard 6850 — is built on the
same three layers, and the whole point of them is that a card only ever touches the top one.

> **A board knows it has a serial line. It does not know, and must never learn, what is on the
> other end of it.**

That one sentence is why `CONNECT sio:a socket:2323` needs no code in the 2SIO, why a card
written next year is cross-platform, scriptable and replayable for free, and why there is a
whole chapter here instead of a paragraph in each board's source.

The three layers, top to bottom:

| Layer | Who | Owns |
|---|---|---|
| The **card** | a `Board` (`src/core/board.h`) | port decode, register bits, polarity, interrupt straps — everything the *manual* describes |
| The **stream** | a `ByteStream` (`src/host/stream.h`) | non-blocking byte movement, the modem pins, the line rate |
| The **endpoint** | `resolveEndpoint` (`src/host/endpoint.h`) | the grammar: `console`, `socket:2323`, `serial:`, `in:`/`out:`, … |

A card reaches down to the stream. It **never** reaches down to the endpoint — it does not know
the grammar, and it must not learn it.

## The ByteStream contract, and the one discipline

`src/host/stream.h` is the seam. Read it; it is short and every comment in it is load-bearing.
The interface a card actually uses:

```cpp
virtual size_t read(uint8_t* buf, size_t n) = 0;   // NON-BLOCKING
virtual size_t write(const uint8_t* buf, size_t n) = 0;
virtual bool readable() const = 0;  // -> drives RDRF / RxRDY
virtual bool writable() const = 0;  // -> drives TDRE / TxRDY
virtual std::string describe() const = 0;   // round-trips the operator's spec
```

The discipline is one line and the whole subsystem stands on it:

> **THE BOARD NEVER BLOCKS.** It asks `readable()`/`writable()` and moves on.

A UART that blocked would stop emulated time, and stopping emulated time to wait for a human is
precisely how an emulator comes to drop characters. So `read()` returning `0` is **not an error
and not an end of file** — it is a quiet line, a poll that found nothing, exactly what an `IN`
from a real UART's data port gives you when nobody has typed. A stream never signals EOF up into
a board; a board polls forever, because that is what the silicon does.

Two more methods complete the contract, and both exist so a board stays pure:

- **`pump()`** is called once per time slice **by the run loop, never by the board**. It is the
  one place a stream may talk to the host: accept a socket, drain a keyboard, poll a file. A
  card's `pump()` is a one-liner that forwards to `stream_->pump()`. Everything a board does in
  `read()`/`write()` is pure computation over its own state; anything that touches the outside
  world happens here, at a known point in emulated time. That seam is what would let a recorded
  session replay identically.
- **`drainLog()`** is what the far end wants said out loud — a print job that could not be
  spooled, a serial port that cannot do the baud it was asked for. The board drains it off its
  units' streams (the default `Board::drainLog()` does exactly this) and the monitor prints it
  after every command and run.

### The unconnected line is not a special case

```cpp
class NullStream : public ByteStream {
    bool readable() const override { return false; }
    bool writable() const override { return true; }   // TDRE set forever
    size_t write(const uint8_t*, size_t n) override { return n; }  // consumed, gone
};
```

An unconnected unit is bound to a `NullStream` — TDRE set, RDRF clear, forever, and a write goes
nowhere without complaint. That is exactly how an unconnected 6850 sits on a real card. Because
of it there is **no null pointer anywhere in a board's stream path, and therefore no `if (nothing
is plugged in)` branch in any board.** Bind the stream to a `NullStream` in the constructor and
never let it be null again.

Two more streams are worth knowing because they are how you test:

- **`LoopbackStream`** jumps TX to RX *and loops the modem pins back too* (RTS→CTS, DTR→DCD/DSR).
  It is the one endpoint that tests modem control with no hardware — the plug in the drawer.
- **`ScriptedStream`** keeps the two directions **separate**: a test types into `feed()` and reads
  what the guest printed out of `out()`. This is what makes a guest program a unit test — a
  monitor's banner and its answer to a command are just bytes it wrote to a serial port, and a
  test that can see them asserts on them with no terminal, no thread, and no timing in the
  picture. It also counts empty polls (`hungry()`), which is how the MCP run loop knows a prompt
  is spinning on input versus a loader that is merely quiet while it works.

## The endpoint grammar lives in exactly one place

```cpp
std::unique_ptr<ByteStream> resolveEndpoint(const std::string& spec, std::string& err);
```

`src/host/endpoint.cpp` is the **only** file in the program that knows the endpoint grammar —
`console`, `socket:PORT`, `socket:HOST:PORT`, `serial:DEVICE`, `null`, `loopback`, `in:PATH`,
`out:PATH`, `file:PATH`, `printer:QUEUE`. `CONNECT` and `MOUNT` are generic monitor commands, not
per-board ones (DESIGN.md §7.7): the monitor opens the endpoint, the board decides what the bytes
mean. That division is why a serial card gets every backend the day it lands without one line
changing in the monitor.

It never guesses. `CONNECT sio:a consle` is an error carrying the list of what it could have
meant, not a silent `NullStream` that leaves you wondering why the terminal is dead.

Two helpers exist so a board can *use* the grammar without *reimplementing* it:

- **`rebaseEndpointPaths` / `rebasingResolver`** rewrite only the PATH-bearing specs (`in:`/`out:`)
  so a machine-file relative path is resolved against the machine file's directory, not the
  shell's cwd. The grammar of *which* specs carry a path stays in `endpoint.cpp`; the board only
  supplies its config dir. Remember the path **as the operator wrote it** for `describe()`, and
  rebase only the copy you hand the resolver — otherwise a relative path double-rebases on
  `CONFIG SAVE` + reload.
- **`parsePort` / `parseHostPort`** let a board validate a `HOST:PORT` string (the PMMI's
  `dial`/`answer` settings do this) using the same split the resolver uses.

### The capture tap is a decorator ByteStream

`ENDPOINT|FILE` taps a line to a hex log — a poor man's protocol analyzer. It is worth studying
because it shows how far a `ByteStream` decorator gets you with **zero** board or monitor changes.

`TeeStream` (`src/host/tee_stream.h`) wraps an inner stream, copies every byte past in both
directions to a log file, and forwards everything else — `readable`/`writable`, the modem pins, the
line rate, `pump`, `pacesItself` — verbatim. It is the same shape as `FilterStream`
(`src/host/filter.h`), with one difference that matters:

> A **filter mutates bytes**, so there is exactly one of them and it lives on the console (a
> `strip7out` on a binary transfer corrupts it silently). A **tee never touches a byte** — it
> observes and forwards — so it is 8-bit clean by construction, and the "one filter, on the
> console" rule does not apply. You may tap *any* line: a socket, a real serial port, a tape.

The grammar is a single character. `resolveEndpoint` checks for a **`|`** *before* the prefix
dispatch (so `socket:23|cap.hex` is not mistaken for a socket spec), splits on the first one,
recurses on the left for the wrapped stream, and parses the right as `FILE[?opts]`. Because the
inner is resolved by the same function, the tap composes with everything — `in:tape.tap?cps=300|trace.log`
taps a paced paper-tape reader. `describe()` returns `inner->describe() + "|" + fileSpec`, so the
whole tap round-trips through `SHOW` and `CONFIG SAVE`, and the `|` re-triggers the branch on reload.
Two consequences fall out of the grammar living in one place: `rebaseEndpointPaths` had to learn to
split on `|` and rebase **both** sides (a machine-file relative log path is config-relative like any
other), and the timestamps come from an **injectable host wall clock** (the printer/tape pattern),
never the emulated `Clock` — a trace whose timestamps freeze at a monitor prompt is a trace of nothing.

### Installing the resolver — in both mains

The grammar travels to a board as a function it is handed, never as knowledge it holds:

```cpp
static void setResolver(EndpointResolver r);   // on the board (or the Sio2Port section)
```

Install it **once in each composition root**: `src/main.cpp` (the CLI) *and* `tests/main.cpp`
(the test binary). Miss the test main and every unit test that connects an endpoint gets a board
whose resolver is null. A card that embeds a `Sio2Port` inherits the section's one resolver and
needs no `setResolver` of its own.

## The modem pins go both ways, and the chip owns polarity

```cpp
struct LineStatus  { bool carrier=true, cts=true, dsr=true; bool ring=false; };  // far end -> card
struct LineControl { bool rts=false, dtr=false, brk=false; };                    // card -> far end
```

One rule governs every pin on every stream:

> **`true` is always "asserted".**

The pin-level inversions are real — the 6850's carrier and CTS pins are `/DCD` and `/CTS`, active
low — but that is a fact about *that chip's pins*, and it stays inside the chip that has them. A
stream that reported "carrier = true, meaning the pin is high, meaning there **isn't** one" would
be exporting one chip's polarity to every other card in the machine, and the 88-SIO would be
wrong for free. The wire carries a level in true sense; the honoring chip decides what it means —
the same division as `PHANTOM*`.

A level has **no memory**. The stream says "carrier is down" for as long as carrier is down; the
*chip* does the latching (a 6850 holds its DCD flag after the pin returns and clears it only on a
status-then-data read). Put the latch in the stream and every stream re-implements it slightly
differently.

A console or a file has none of these pins in any real sense, so it **asserts them all** — which
is exactly what strapping DCD and CTS to ground on the connector does, and what period installers
did constantly. That, again, is why there is no "what if nothing is plugged in" branch anywhere.

### The line rate is the card's, and there is only one

```cpp
struct LineParams { long long baud=9600; int dataBits=8, stopBits=1; LineParity parity=None; };
virtual bool setParams(const LineParams&, std::string& err);
```

There is exactly one line rate in a serial card and it is the UART's clock — a jumper. When a unit
is connected to a **real host serial port**, the *card programs the port*: `SET sio0:a BAUD=300`
restraps the card and the host UART follows. Only a `serial:` endpoint honors `setParams`, and it
is the only stream that may **refuse** — an FTDI cable that cannot do 76800 baud is a fact about
the world, and the card says so out loud through `drainLog()` rather than run at the wrong speed
in silence. A `false` return does not mean the connection failed: the card stays strapped to what
it is strapped to and goes on pacing the guest at that rate, because that pacing is the half the
guest can actually measure.

A second, independent baud rate on the endpoint was considered and struck. It could only ever
configure a *mismatch*, and a 6850 strapped for 300 driving a terminal set to 9600 does not give
you a fast link on real hardware — it gives you garbage. Every stream but a real serial port
ignores `setParams` entirely: a socket has no baud rate, and pretending it did would pace an
emulation against a fiction.

One related flag closes the loop. `pacesItself()` returns true for a stream that carries its own
cadence — a cassette releases bytes at a wall-clock rate the CPU's speed cannot drag. When it is
true the UART must **not** also impose its emulated line-rate gate, or the two clocks fight (see
`Uart1602::poll` and `host/tape.h`). Every other line has no cadence but the one the UART's baud
gives it.

## A chip is not a card, and a serial section is not one either

`theory.md` states the chip/card seam: a chip is modeled from its **data sheet**, a card from its
**manual**, and the inverting buffers and interrupt-enable flip-flops that live between the chip's
pins and the S-100 bus are the *card's*. Serial adds one more object in the middle.

A concrete UART is a part in `src/chips/`: `Mc6850` (both halves of the 2SIO), `Uart1602` (the
COM2502 — the 88-SIO and the ACR), `Intel8251` (the SBC console). A chip knows nothing about
S-100; it has a clock, some pins, and a `ByteStream`.

But the *glue* that turns `IN 10h` into "read channel A's status register" — decode, the even→
status/odd→data dispatch, the single card-owned Clock deadline, interrupt aggregation, connect/
units/properties, SNAPSHOT — is needed by **every** card that carries a 6850, and copying it onto
each is how a bug gets fixed on one card and stays wrong on the others. So it lives once, in
**`Sio2Port` (`src/chips/sio2port.h`)**, a reusable serial *section*:

```cpp
struct ChannelDef { std::string name; uint8_t offset; };   // the 2SIO has {"a",0},{"b",2}
Sio2Port(std::vector<ChannelDef> channels, std::function<void()> onIntChanged);
```

`Sio2Port` is **not a `Board`** — no `type()`, no id, no bus. The owning card holds one as a
member and forwards the bus and lifecycle calls to it (see `src/boards/mits-turnkey.cpp`, one
channel, and the 2SIO, two). It cannot reach the card's protected `clock_` or `intChanged()`, so
it is handed both at construction. Parameterize it by the channels the card has — one or four —
and the whole serial mechanism comes with it.

### TDRE is a deadline, not a flag

A UART does not know a character has finished going out because the guest asked — it knows because
*time passed*. So a serial section sets a `Clock` deadline for the moment the byte clears the
shift register, and `writable()`/TDRE reflects that deadline, not an instantaneous flag. `pump()`
takes an arriving byte off the line; if the line has not yet had time to deliver it, the deadline
covers the gap. This is the single most common thing a naive UART gets wrong — read §4.4.1 of
DESIGN.md before you put any work inside `assertsInt()` (theory.md explains why the poll must
never be doing the card's clocking).

## Interrupts

The full interrupt model — `assertsInt()`/`assertsVi()` as pure levels, the wire-OR the bus
keeps, `intChanged()` after any pin move, `irqJumperProperty()` for the `none|int|vi0..vi7`
vocabulary — is in `theory.md` and is not serial-specific; a parallel card wires interrupts the
same way. For a serial board, the only things to remember:

- Give the board its strap with `irqJumperProperty("interrupt", …, irq_)` and it gets the ten
  choices, the spelling, tab completion and the `SHOW BUS IRQ` line for free.
- Override `assertsInt()`/`assertsVi()` to report the *settled* pin from the chip's state — a
  character waiting with the jumper installed asserts, and keeps asserting until the guest reads
  the character. It is a level; there is no queue to lose.
- Call `intChanged()` wherever the pending flag can move: a register written, a byte taken off
  the line, a deadline coming due. A missing one hangs the guest forever waiting for an interrupt
  that already happened — and it presents as *"the emulator locks up sometimes"*.

An embedded `Sio2Port` aggregates its channels' pins and drives the callback the card bound to
its own `intChanged()`, so a card that forwards to a section gets this right by construction.

## Two shapes of serial board

Everything above supports two ways to build a serial card, and knowing which you are writing tells
you where the code goes.

**The chip-backed card** emulates a specific UART. It embeds a `Sio2Port` (or a bare chip),
forwards bus/lifecycle/connect to it, and adds only the card-specific glue: the base-port jumper,
the inverting buffers, the interrupt straps. The 88-2SIO, the Turnkey, the SBC are this. The
manual tells you the polarity and the port map; the data sheet tells you the chip. **This is the
right shape when you are modeling real silicon** — and the project rule stands: never give the
hardware a behavior it never had to make software happy.

**The UART-agnostic card** does not model a chip at all. **`UsioBoard` (`src/boards/usio.h`)** —
the "universal serial board", `type() == "usio"` — holds a `std::unique_ptr<ByteStream>` directly
and *synthesizes* a status byte from the stream:

```cpp
uint8_t s = 0;
if (stream_->readable() != rdrActiveLow_)  s |= (1u << rdrBit_);   // XOR = active-low inversion
if (stream_->writable() != tdreActiveLow_) s |= (1u << tdreBit_);
```

The operator *describes* an abstract interface with straps rather than picking a chip: which port
is status/control, which port is data, which status bit means receive-data-ready and which means
transmit-data-empty, and whether each is active-low. Control-port writes are accepted and ignored
— there is no chip to program. To make common cards turnkey it ships **built-in profiles** in one
table (`usioBuiltins()` — `tuart`, `imsai-sio2`, `compupro-if2`, `compupro-ss1`), each just a
bundle of those straps and trivial
to extend: add one struct and its name becomes a `profile` choice and appears in the generated
docs. USIO is **polled, with no interrupts** — a deliberate first phase, because without a working
control/interrupt-enable register a strapped TX-empty interrupt would storm (TDRE is asserted at
idle). This is the right shape when the goal is to *reach* an abstract serial interface some
software expects, not to reproduce a particular board.

## How to add a serial board

The general playbook is `adding-a-board.md`; the serial-specific steps on top of it:

1. **Own a stream, never null.** Hold a `std::unique_ptr<ByteStream>`, bind it to a `NullStream`
   in the constructor. Or embed a `Sio2Port` and let it hold the streams.
2. **`readable()`→RDRF, `writable()`→TDRE.** Synthesize your status byte from those; apply your
   card's polarity here, in true sense at the pins.
3. **Expose one serial unit** per channel (`units()` → `UnitKind::Serial`), wire
   `connect`/`disconnect`/`unitStream`, and route `pump()` to the stream.
4. **Install the resolver in both mains** — `src/main.cpp` and `tests/main.cpp` — via your
   board's (or `Sio2Port`'s) `setResolver`.
5. **If you touch a real serial port**, push `LineParams` through `setParams` on connect and on
   any baud change, and surface a refusal through `drainLog()`.
6. **If you model real silicon**, put the chip in `src/chips/` from its data sheet, keep the
   inverting buffers and interrupt-enable bits on the card, and reuse `Sio2Port` if it is a
   6850. If instead you want a strap-configured abstract interface, `UsioBoard` already exists —
   add a profile, do not write a new board.
7. **Test with a `ScriptedStream`** bound through the real `connect()` path: `feed()` bytes at the
   card, assert on `out()`, and check the status bits land where the straps put them.

Then regenerate the board reference (`cmake --build build --target docs-reference`) so the
generated `ref/boards.md` picks up your properties, and add the board to the changelog and the
prose board chapter — neither is test-enforced, so both drift silently.
