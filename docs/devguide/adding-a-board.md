# Writing a board

We are going to build a card. A real one — it plugs into the backplane, it decodes a bus
cycle, it holds state, it has a setting you can put in a machine file, and when we are done
the monitor will know about it, `CONFIG SAVE` will write it out, and an AI assistant driving
the machine over MCP will be able to configure it. None of that last part will cost us a
line of code.

The card is **eight lamps and a latch**. `OUT 0FFH` lights them. That is the whole thing.

The finished source is in the tree at **`examples/boards/lamp/lamp.h`**, and
`tests/test_lamp.cpp` drives it on a real bus — so it compiles and it is tested, which is
not something you can say about most tutorial code. Read along, or write it yourself.

## Why port FF, which looks taken

The front panel is already at port `0xFF` — that is where the SENSE switches live. So this
looks like a collision, and picking `FF` looks like a mistake.

Ask the machine:

```
altairsim> WHO IO FF
port FF OUT: nobody (an OUT here goes nowhere)
```

```
altairsim> SHOW BUS IO
I/O
  FF        fp0      read  SENSE switches SA8..SA15 -> D0..D7
  10        sio0     read/write 6850 'a' -- status/control, data
  ...
```

**The front panel answers `IN FF`. It does not answer `OUT FF`.** That is not a simplification
in the model — it is the real card. The SENSE switch buffer's enable is gated with `sINP`;
there is no `sOUT` anywhere near it. On a real Altair an `OUT 0FFH` is not the panel's, and
the byte goes nowhere at all.

And the bus knows the difference, because **the bus decodes each direction separately** —
`ioRead_[256]` and `ioWrite_[256]` are two different tables (`src/core/bus.h`), for the same
reason the real backplane has two different strobes.

> **The bus routes by cycle type, not just by address.**

So `OUT FF` is a genuinely empty slot in a stock machine, and our card can have it without
disturbing anything. Keep that sentence in mind while writing `decodes()` — it is the single
easiest thing to get wrong, and getting it wrong here would break the SENSE switches.

## 1. The card

`examples/boards/lamp/lamp.h`. A board is a class that inherits `Board`
(`src/core/board.h`) and answers some questions.

### Its name

```cpp
std::string type() const override { return "lamp"; }
```

This is the word a machine file uses: `type = "lamp"`. It is the **chip or the common
name**, never a catalog number — nobody ever asked for an 88-CPU, they asked for an 8080.

### What it decodes

```cpp
bool decodes(const BusCycle& c) const override {
    if (!enabled_) return false;                 // a card switched off drives nothing
    if (c.type != Cycle::IoWrite) return false;  // OUT only. The panel owns IN.
    return c.port() == port_;
}
```

That middle line is the whole lesson of this chapter. Delete it and the card answers `IN FF`
too — the SENSE switches stop working, and the bus starts reporting contention with `fp0`.

Two rules about `decodes()`, and they are not negotiable:

- **It must be pure and combinational.** It answers *"if this cycle happened, would I drive
  the bus?"* and it does nothing else. No side effects, no counters, no latching.
- **The bus caches the answer.** It keeps one slot per port and per page, so a decode is a
  table lookup rather than a walk over every card in the machine. Which is why a `decodes()`
  with a side effect in it is a bug that will not show up for a month: it does not get called
  when you think it does.

If a board's decode ever *changes*, it must say so — `decodeChanged()`. (You will see below
that we get that for free.)

### What it does with the cycle

```cpp
void write(const BusCycle& c) override { latch_ = c.data; }
```

We claimed the cycle, so the byte is ours.

We never override `read()`. We never say yes to a read, so we are never asked one — and an
`IN FF` from our port floats to `0xFF`, which is exactly what an output-only card does.

### Power and reset

```cpp
void reset(Reset) override { latch_ = 0; }
void power() override      { latch_ = 0; }
```

These are **two different events** and a card is entitled to treat them differently.
`Reset::Bus` is the RESET* line — the button on the panel. `Reset::PowerOn` is the power
coming up, and it is the only thing that loses RAM. For our lamps they mean the same thing:
the lights go out. For a memory card they emphatically do not.

### Its settings — and this is the part that pays

```cpp
std::vector<Property> properties() override {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "port";
        x.help  = "the port this card latches. Write-only -- an IN here is not ours";
        x.kind  = Kind::Int;
        x.radix = 16;               // ON THE WIRE -> HEX. A port is a thing the 8080 sees.
        x.min   = 0;
        x.max   = 0xFF;
        x.get   = [this] { return Value::ofInt(port_); };
        x.set   = [this](const Value& v, std::string&) {
            port_ = (uint8_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "lamps";
        x.help  = "what the guest last wrote -- the eight LEDs";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.get   = [this] { return Value::ofInt(latch_); };
        // NO SETTER.
        p.push_back(std::move(x));
    }
    return p;
}
```

**This vector is the entire configuration layer.** There is no schema file, no parser, no
registration call, and nowhere else to declare anything. `SET`, `SHOW`, the TOML loader,
`CONFIG SAVE`, the MCP tool schemas, tab completion, and the User Manual's generated board
reference are all written **once**, against this, and know nothing about any particular card.

Two details worth stealing:

- **`radix = 16`**, because a port is a thing the processor can see. On the wire → hex; never
  on the wire → decimal. Get this wrong and `port = 10` in a machine file quietly means ten
  instead of sixteen.
- **`lamps` has no setter**, and that absence *is* the signal. `SHOW` prints `(read-only)`,
  `CONFIG SAVE` leaves it out of the file, and the manual's reference marks it. A setter that
  merely *refused* would stop a `SET` and fool all three at once — which is a mistake that was
  in this codebase, on the memory card, until this manual went looking.
- **We never call `decodeChanged()`** in the `port` setter. The property layer calls it for us
  after any successful set, precisely so that a board author cannot forget.

### What it tells the operator

```cpp
std::vector<MapEntry> ioMap() const override {
    return {{port_, port_, "write", "lamp latch -- D0..D7"}};
}
```

This is what `BOARDS`, `SHOW BUS IO` and `WHO` print. **It is documentation, not decode** —
the bus never consults it. A card whose `ioMap()` disagreed with its `decodes()` would work
perfectly and lie to you, which is worse than a card that does not work.

## 2. Put it in the registry

Two lines and an include, in `src/boards/registry.cpp`:

```cpp
#include "../../examples/boards/lamp/lamp.h"

// ...in boardTypes():
{"lamp", "A write-only latch: OUT FF lights eight LEDs. The Developer Guide builds this"},

// ...in makeBoard():
if (type == "lamp") return std::make_unique<LampBoard>();
```

That is all. `registry.h` promises *"adding a board type is one line here and nothing anywhere
else"*, and it means it.

(Our card is a header, so it needs no entry in `CMakeLists.txt`. A real one — with a `.cpp`
— adds its source to the `altair_core` library alongside the others.)

## 3. Build it, and watch what you get for free

```
$ cmake --build build -j
```

The card is now in the catalogue, with its settings and their help text, and nobody wrote a
line of code to put it there:

```
altairsim> BOARDS TYPES
  ...
  lamp       A write-only latch: OUT FF lights eight LEDs. The Developer Guide builds this
               port             the port this card latches. Write-only -- an IN here is not ours
               lamps            what the guest last wrote -- the eight LEDs
```

Fit one, and ask the machine the same question we asked at the start:

```
altairsim> BOARDS ADD lamp lamp0
lamp0: lamp added

altairsim> WHO IO FF
port FF OUT: lamp0
```

It said `nobody` an hour ago. Now light the lamps:

```
altairsim> OUT FF 55
port FF <- 55

altairsim> SHOW lamp0
lamp0  (lamp)

  property         value            legal
  port             0xFF             0..255
  lamps            0x55             (read-only)
```

`OUT FF 55` ran a **real output cycle on a real bus** — the same path the 8080's `OUT`
instruction takes, because there is only one. The card decoded it, latched it, and `SHOW`
read it back out of the reflection layer. `lamps` is marked read-only, and it worked that out
from the missing setter.

And the front panel is still sitting on the same port, untroubled:

```
altairsim> IN FF
port FF -> 00      <- still the SENSE switches
```

One port. Two cards. No contention. **Because the bus routes by cycle type.**

## 4. Put it in a machine

```toml
[machine]
name = "lamps"
base = "default"

[[board]]
type = "lamp"
id   = "lamp0"
port = FF          # radix 16 -- no 0x needed
```

Nobody taught the TOML loader what a lamp is. It asked the card for its properties, found one
called `port`, and set it. A board added next year is configurable, scriptable, drivable by
an assistant, and documented **the day it lands**.

## 5. Test it

`tests/test_lamp.cpp` is the model. It does three things, and the middle one is the important
one:

```cpp
// A REAL OUT CYCLE on the bus -- not a method call on the board.
m.bus.ioWrite(0xFF, 0x55);
CHECK(lamp->properties()[1].get().i() == 0x55, "OUT FF latches the byte");

// The card is WRITE-ONLY, and the proof is that the read FLOATS.
CHECK(m.bus.ioRead(0xFF) == 0xFF, "an IN from FF is not the lamp's -- the bus floats");
```

…and then it pins down the claim this whole chapter rests on, with both cards in one machine:

```cpp
BusCycle in {Cycle::IoRead,  0xFF};
BusCycle out{Cycle::IoWrite, 0xFF};

CHECK(m.bus.respondersTo(in).size()  == 1, "IN FF: the front panel, and ONLY the front panel");
CHECK(m.bus.respondersTo(out).size() == 1, "OUT FF: the lamp, and ONLY the lamp");
CHECK(m.bus.drain().empty(),               "...and the bus says NOTHING. It is not contention.");
```

**Assert the thing your design depends on, not the thing that is easy to assert.** If the bus
ever stopped decoding by direction, this chapter would be teaching a falsehood — and that
test is what would say so.

> **One trap, and it caught this test.** `m.bus.attach(board)` wires a card to the backplane;
> it does not hand it to the **machine**. Lifecycle events — `reset()`, `power()`, `pump()` —
> are dispatched by `Machine` over the boards it *owns*. A board that is only on the bus will
> answer cycles all day and never hear the RESET line. A card that comes from a machine file
> goes in through `Machine::add()` and gets both.

## What to do next

Our card answers a cycle. A more interesting one **asks for something**:

- **Interrupts.** Add an `IrqJumper` and push `irqJumperProperty("interrupt", ..., irq_)` into
  `properties()` — that one call buys you the whole `none | int | vi0..vi7` vocabulary, tab
  completion, and the flag that lets `SHOW BUS IRQ` find your strap. Then override
  `assertsInt()` / `assertsVi()`, and **call `intChanged()` from every place your pending flag
  could move.** A spurious call costs a virtual call. A missing one hangs the guest forever.
- **Time.** A card with a deadline uses the `Clock` it was handed. A UART absolutely needs
  one: transmit-buffer-empty is a deadline, not a flag.
- **The outside world.** Anything that talks to a socket, a file or a keyboard does it in
  `pump()` — **never inside a bus cycle**. That seam is what keeps `read()` and `write()` pure
  computation over state, and it is what a deterministic replay would be built on. The
  **88-C700 printer** (`src/boards/mits-88c700.{h,cpp}`, `docs/boards/mits-88c700.md`) is the
  smallest shipped card that does this for real: a bare latch that sinks its data port to a
  `ByteStream`, so `CONNECT lpt0:prn file:printout.txt` captures the printout and the card never
  learns what a file is. It is a good next read after this lamp.
- **Sub-units.** A card with a *list* of things — regions on a memory card, drives on a
  controller — declares `subUnitTables()` and gets `[[board.region]]` / `[[board.drive]]` in
  TOML for free.

And whatever you build: **it ships with its `.md`.** A board and its documentation are one
deliverable, and the **Limitations** and **Quirks** sections are the load-bearing ones —
they are what forces the honest question *"what did I not actually implement?"*, which is
exactly the question a simulator author most wants to avoid. `docs/boards/_TEMPLATE.md` is
the form.
