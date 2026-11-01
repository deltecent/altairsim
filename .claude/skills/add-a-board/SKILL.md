---
name: add-a-board
description: Add a new S-100 board (or CPU board) to the simulator — the wiring checklist that surrounds the board class itself: registry, CMake, boardCategory, tests, the generated reference, the two docs no test guards, and the board's own docs/boards/*.md. Use when adding, emulating or implementing a board, card or controller, or when a board compiles and you need to know what else it must touch before it ships.
---

# Add a board

**The tutorial is `docs/devguide/adding-a-board.md`** — a worked board (the `tests/boards/lamp/`
card) from `type()` through properties and tests. Read §1–5 for the shape; this skill is the
part that bites *after* the board compiles. Everything here fails somewhere that does not point
back at the board you just wrote.

Plan in plan mode first (`work-task`), and prove the board is wanted: check the GitHub issues —
a declined board is recorded there with its reason.

## Before you write a line

- **Settle the name first.** Do this step before you touch a file.
  1. Choose the registry key (`type()`), the class name and the name for the docs.
  2. Check the names against `DESIGN.md` §0.3 ("board", not "card").
  3. Check that the names are not already in the registry.
  4. Show the names to the maintainer. Ask the maintainer to confirm them or give others.
  5. Wait for the answer. A rename later touches the registry, the class, CMake,
     `boardCategory`, the tests, the generated reference and `docs/boards/*.md`.
- **Model it from a period source.** `reference/*.md` if we have one, and if we do not, the scan
  goes through the `pdf-to-reference` skill first. Never from another emulator's source.
- **Reuse the chip.** `src/chips/` already holds the 6850, 8251, 8253, 8259A, 2651, MSM5832,
  WD17xx, TMS5501 and more. A card is usually a port decode and a strap file wrapped around a
  chip that exists — the MSM5832's header literally says "the next card with one gets this for
  free", and the RTC-100 took it up unchanged.
- **Read the relevant `DESIGN.md` section.** Most surprises here are deliberate.

## Quick anatomy (all in `src/core/board.h`)

`type()` · `decodes()` (**pure and combinational** — the bus caches it) · `read()`/`write()` ·
`properties()` (the whole config layer; **no setter = read-only**, and CONFIG SAVE skips it) ·
`units()`/`unitStream()` · `connect()`/`disconnect()` · `ioMap()` (documentation, never decode) ·
`reset()`/`power()` · `pump()` (**the only place a board touches the host** — never in a bus
cycle) · `serialize()`/`deserialize()`.

**The base address is a `port` property, not a constructor argument.** Validate the shape the
card's address decode really had (even-only, multiple of 4, a 16-port block) and say why in the
error string.

**Interrupts.** `irqJumperProperty("interrupt", …, irq_)` buys the whole `none | int | vi0..vi7`
vocabulary, tab completion and `SHOW BUS IRQ` — *if the card really has a VI connector*. A card
that vectors itself instead (jams its own `RST`/`CALL` on acknowledge) claims the `Cycle::IntAck`
cycle in `decodes()`/`read()` and should not offer VI straps it never had. Either way, override
`assertsInt()`/`assertsVi()` and **call `intChanged()` wherever the pending flag moves** — a
needless call costs a virtual call, a missing one hangs the guest forever.

**Time is wall time.** A board's oscillator is its own crystal, not the CPU's: schedule off
`clock_->tStatesPer(rate)`, cancel and re-arm like `mits-88virtc.cpp`. Do not leave a deadline
queued when the feature is strapped off — `Clock::queued()` is one of the things the run loop
reads to decide a `HLT` has finished, so an idle card must let the machine stand down.

## The wiring — every one of these, or it does not ship

1. **`CMakeLists.txt`** — the `.cpp` into `altair_core`, and the test `.cpp` into `altair_tests`.
2. **`src/boards/registry.cpp`** — the include, the `{"type", "one-line summary", "one-paragraph description"}` row
   (the summary must fit the `SHOW BOARDS` column — `test_cli` checks 78 columns),
   and the factory line.
3. **`tools/gen-reference.cpp` → `boardCategory()`** — **an uncategorized board is a hard error**
   that reds every CI leg, and the failure names a doc target, not your board.
4. **The endpoint resolver, in BOTH composition roots** — `src/main.cpp` *and* `tests/main.cpp` —
   if the board has a line. Only `main.cpp` and every test that `CONNECT`s it gets a null
   resolver and fails somewhere unhelpful.
5. **Path as written.** Look through `resolvePath()`; **remember the spec the user typed**. Store
   the resolved path and `CONFIG SAVE` rebases it, then the next load rebases it again.
6. **`tests/test_<board>.cpp`**, plus its `void test_<board>();` in `tests/test.h` and its row in
   `tests/main.cpp`'s table.
7. **`docs/boards/<vendor>-<board>.md`** from `docs/boards/_TEMPLATE.md`. **Limitations** and
   **Quirks** are the load-bearing sections: they are where you answer "what did I not actually
   implement?"
8. **`docs/manual/boards.md`** — the summary-table row *and* a prose section. **No test guards
   this**; it once drifted nine boards behind.
9. **`docs/changelog/changelog.md`** — a line under `## Unreleased`. Add that header above
   the newest release if it is not there. Also unguarded.
10. **`cmake --build build --target docs-reference`**, then commit the regenerated
    `docs/manual/ref/*.md`. A ctest byte-diffs them. **Edit the emitter, never the `.md`.**
11. **A sample machine** is a TOML in `machines/` (`base = "default"`), if the board deserves
    one — embedded by `cmake/embed_machines.cmake`. Optional; a board is usable via `BOARDS ADD`.
12. **`reference/README.md` and `docs/sources.md`** — if the board was modeled from a scan, its
    rows stop saying "not emulated" and name the board.

## Testing

Drive **real bus cycles** (`m.bus.ioWrite` / `ioRead`), not the board's methods, so the test
exercises the decode too. Where the manual shipped a driver, **the driver is the oracle** — a
manual's prose and its own listings disagree more often than you would think, and the listings
ran on the hardware.

Cover: the port map and that moving the base moves all of it; every status/control bit; reset
and power (and what deliberately survives them); the interrupt, including that the acknowledge
does what the card really does; snapshot/restore round-trip; and each property's validation.

```sh
cmake -B build -DWERROR=on && cmake --build build -j   # CI's gate, on all three toolchains
./build/altair_tests <board>                           # the suite you added
ctest --test-dir build -LE slow                        # registry + generated docs are cross-cutting
```

Read the `100% tests passed out of N` line — the absence of the word "error" is not a pass.

## Finish

Prove it in the running program, not only in a unit test: `BOARDS ADD <type> <id>`, `SHOW <id>`,
`SET`, `CONFIG SAVE` round-trip, and exercise the ports (`IN`/`OUT` at the monitor, or a guest
program driven over `--mcp` — never a hand-rolled expect script).

Then stop for the maintainer's review (`work-task`), and ship with `ship-change`.
