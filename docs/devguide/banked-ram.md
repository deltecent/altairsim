# Banked memory — the model, and where it was wrong

> **Status: the fix has landed.** Banking is now its own board, **`bankmem`**
> (`docs/boards/bankmem.md`, `src/boards/bankmem.{h,cpp}`), with one decoder per real card;
> the `memory` board is plain, unbanked RAM/ROM again. The `b810` type is gone and the
> SIMH-derived `bank_type` table is deleted. This page is kept as the **audit that led there** —
> it records what the manuals say, what the old model said, and why the two diverged. The
> *decisions* taken are recorded at the end (*The fix that landed*).

This page is a developer-facing audit of how `altairsim` modelled S-100 memory-bank switching
(`src/boards/s100-memory.h` `BankType`/`BankSpec`, `src/boards/s100-memory.cpp` `kBanks[]`)
against the **period manuals** for the real cards, distilled in `reference/`. It exists because
that audit turned up a problem worth writing down before it is fixed: **most of the banked
encodings we ship do not match the hardware, and the reason is that they were taken from another
emulator's source instead of a manual.**

Nothing here changes code. It records what the manuals say, what our model says, and where the
two diverge, so the correction (a separate branch — see *The follow-on*) starts from facts. The
per-card facts below are sourced to the five new references:

- [`reference/SD Systems ExpandoRAM.md`](../../reference/SD%20Systems%20ExpandoRAM.md) — the original ("I")
- [`reference/SD Systems ExpandoRAM II.md`](../../reference/SD%20Systems%20ExpandoRAM%20II.md)
- [`reference/Vector Graphic 64K Dynamic RAM.md`](../../reference/Vector%20Graphic%2064K%20Dynamic%20RAM.md)
- [`reference/North Star HRAM.md`](../../reference/North%20Star%20HRAM.md)
- [`reference/Cromemco 64KZ RAM.md`](../../reference/Cromemco%2064KZ%20RAM.md) (covers 64KZ and 64KZ-II)

## The cardinal rule we broke

`docs/sources.md` states the rule at the top: **"Period manuals and first-hand artifacts only.
Never read another emulator's source to learn how hardware works — that explicitly includes SIMH
/ AltairZ80. Second-hand facts inherit second-hand mistakes."**

The banked-memory encodings in `kBanks[]` were taken from Patrick's SIMH `s100_bram.c`, not from
the period manuals — and it shows. Of the four banked types we can now check against a manual,
**only `vram` (Vector Graphic) is right.** `eram`, `cram`, and `hram` each diverge from the real
card, in exactly the way the rule warns about: `s100_bram.c` is a *generalization*
(`{port, banks, one-hot?, mask}` swapping a whole 64K plane), and the real cards are not
generalizations — each is a different mechanism, and three of them do not fit that shape at all.
The comment block in `s100-memory.cpp:14` and the prose in `docs/boards/s100-memory.md:213`
(*"five real cards, and no two alike"*) present the SIMH table as if it were the hardware. It is
not. This is a genuine violation of the project's central sourcing discipline, discovered only
when the actual manuals were read.

## The scorecard

| `bank_type` | Real card (manual) | Real scheme | Our model (`kBanks[]`) | Verdict |
|---|---|---|---|---|
| `eram` | SD Systems ExpandoRAM **I** | **No I/O port at all** — flat memory-mapped; a DIP switch straps 4 chip-groups to fixed 8K/16K address slices | port **FF**, 8 banks, binary (byte = bank), swap 64K plane | **✗ wrong** |
| `eram` | SD Systems ExpandoRAM **II** | port **FF**, byte = **page number** (0–9); an 82S130 PROM + octal board-select switches decode it to a 32K/48K **partition**; 4 chip-banks/board | (same `eram` row) | **✗ wrong** (port matches, encoding does not) |
| `vram` | Vector Graphic 64K | port **40**, **one-hot** `1<<bank`, 8 banks, reset → bank 0 | port 40, 8 banks, one-hot | **✓ correct** |
| `cram` | Cromemco 64KZ / 64KZ-II | port **40**, byte = **8-bit mask of active banks** (bit N ⇒ bank N on/off, *several at once*), 8 banks | port 40, **7** banks, one-hot **select-one**, mask `0x7F` | **✗ wrong** |
| `hram` | North Star HRAM | port **C0**, bit 0 = on(0)/off(1), bits 1–7 = **one-hot bank toggle**, ≤ 6 usable banks, old-off-then-new-on | port C0, **16** banks, binary, mask `0x0F`, swap 64K plane | **✗ wrong** |
| `b810` | *(claims "AB Digital Design B810")* | **unsourced** — no manual; cannot be verified | port 40, 16 banks, binary | **✗ remove** |

### `eram` — matches neither ExpandoRAM

The `eram` byte-is-the-bank / 64K-plane-swap model matches **neither** real ExpandoRAM. The
original (I) has *no I/O port whatsoever* — its J1 pinout carries only memory-bus signals, and
its "banks" are four fixed chip-groups strapped to address slices by a DIP switch; it is an
ordinary static-address memory board, not a bank switcher. The II *does* use port FF (so our port
is right), but the byte it takes is a **page number** decoded by an on-board 82S130 PROM into a
32K or 48K partition, with the board's identity set by octal board-select switches — not a bank
number, and not a 64K plane. Our `eram` is the SIMH generalization sitting on the II's port.

### `vram` — correct, and worth keeping as the proof

Vector Graphic is exactly what we model: port 40H, one-hot `1<<bank`, 8 banks, and bank 0
force-enabled at reset (the manual's theory-of-operation even explains the latch-clear + DO0
inversion that makes reset electrically equal to writing `0x01`). The **OASIS quirk** (`0x41`/
`0x42` tolerated as banks 0/1) is empirical, not in this manual, and stays empirical. This is the
one banked type whose encoding an implementer can trust today.

### `cram` — wrong mechanism, not just wrong numbers

Cromemco's BANK SELECT byte is a **bit-mask**: *"BIT 0 (LSB) controls BANK 0, BIT 1 controls
BANK 1 … a logic 1 control word bit activates its corresponding memory BANK; a logic 0 …
deactivates"* (64KZ manual p. 17). Several banks can be active at once — `OUT 40H,28H` activates
banks 3 **and** 5. We model a **one-hot select-one** with only 7 banks and bit 7 masked off. That
is wrong on the bank count (8, not 7), on the masking (bit 7 *is* bank 7), and on the core
semantics (mask-enable-many vs select-one). The "seven banks / bit 7 is not a bank" story in the
code and docs is a SIMH artifact, not Cromemco hardware.

### `hram` — the byte is not a bank number

North Star's port-C0 byte is **bit 0 = on/off command, bits 1–7 = a one-hot address of which
bank the command acts on** — banks are toggled individually (software must switch the old bank off
before the new one on), and one of bits 5/6/7 is spent on parity, leaving ≤ 6 usable banks. We
model a 16-bank binary plane-swap. Verified directly against the scan (`MVI A,08H` = "turn on bank
3", `MVI A,09H` = "turn off bank 3"). Wrong on encoding, bank count, and protocol.

### `b810` — unsourced, remove it

`b810` ("AB Digital Design B810") has no manual behind it and cannot be verified (§0.1 forbids
shipping hardware behavior we cannot source). Patrick's decision: **remove it.** It is also the
"fifth card" that props up the *"five real cards"* and *"three of the five share port 0x40"*
narrative in the code and docs — remove it and that narrative must be re-counted (the real
port-0x40 sharers among the sourced cards are `vram` and `cram`).

## The fix that landed

The correction became the `bankmem` board (`docs/boards/bankmem.md`). What was done, and the
design decisions taken:

- **Banking is its own board, `bankmem`, with a `card=` variant strap** — one class, four
  per-card decoders (`vector` one-hot select-one, `cromemco64kz` 8-bit mask, `northstar`
  on/off + one-hot toggle, `expandoram2` PROM page-select). This was chosen over piling more knobs
  onto `memory`: the real cards do not share one parameterization, and `DESIGN.md` §4.3 argues a
  board must own its decode. The unifying model is *segments* (a RAM slice + address window +
  enabled flag); the port write toggles the flags per the card's rule.
- **`memory` is plain RAM/ROM again** — no `bank_type`, no `banks`/`bank` properties, no I/O port,
  a flat 64K store. This is also the honest model of the ExpandoRAM I (no port at all).
- **`b810` removed** — unsourced (§0.1).
- **`eram` retired** — the ExpandoRAM I is plain memory; the ExpandoRAM II is `card=expandoram2`.
- **`expandoram2` is a documented approximation** — a binary page-select, because the real 82S130
  PROM decode is not transcribable from the scan. The caveat is stated in the board doc and in the
  code; a PROM dump would let the *page decode* be made faithful. **What the PROM's stock variants
  do is modelled, though** (follow-up, `feat/bankmem-partition-ram`): `partition=ex32|ex48` gives a
  fixed **common region** shared by every page (EX-32 = 32K banked + 32K common; EX-48 = 48K + 16K),
  and `ram=` sets board size up to 256K. That is what a banked CP/M needs — the resident OS and the
  bank-switch routine live in common and survive the page `OUT`. SD Systems' own banked CP/M 3 does
  exactly this (bank number → port FF, `COMBAS=C0` ⇒ `partition=ex48`); the general mechanism is now
  in place, while board-select across multiple coordinated boards remains the approximated part.
- **`v2z80` was kept as its own board** — its onboard paged **ROM** overlay (two 4K EEPROM pages at
  F000-FFFF, phantom-shadowing RAM, tied to a CPU card's identity) is a different thing from an
  S-100 banked-RAM card; folding it into `bankmem` (a RAM board) would reintroduce the very
  "ROM on a banked card is unsourced" tension the design refuses. Closed as: stays separate.

The grep-verified footprint the fix touched (kept here as the record of what moved):

| Location | What is there |
|---|---|
| `src/boards/s100-memory.h:52` | `enum class BankType { None, Eram, Vram, Cram, Hram, B810 }` |
| `src/boards/s100-memory.cpp:22–29` | `kBanks[]` table (the wrong encodings) |
| `src/boards/s100-memory.cpp:33` | `parseBankType` loops `for (i = 0; i < 6; ++i)` |
| `src/boards/s100-memory.cpp:479,481` | `bank_type` property help + `choices` |
| `tests/test_memory.cpp:201,242,252–253,292,300` | banking tests: `hram`/`b810` binary/16 loop; `vram`+`b810` port-0x40 contention test |
| `docs/boards/s100-memory.md:213–249,277,302,344,420` | the "five real cards" table, the OASIS quirk, the `bank_type` enum, the contention example |
| `docs/manual/boards.md:87,291` and `docs/manual/ref/boards.md:173` | manual prose + the **generated** ref (regenerate via `cmake --build build --target docs-reference`, never hand-edit) |
| `DESIGN.md:515,523,525,529` | the `b810` row and the "three ports / two encodings / seven banks / three of the five" argument |
| `docs/roadmap.md:40,55,243` | the same "five real cards / five schemes" framing |

⚠ Note the "**two encodings**" claim in `DESIGN.md`/`s100-memory.md` is itself wrong: the real
cards use at least *four* distinct mechanisms (static address-strap, PROM page-select, one-hot
select-one, bit-mask, on/off-plus-one-hot-toggle) — the "two encodings" count only holds inside
the SIMH generalization.

## Open design questions — now settled

*(Both were settled with Patrick before the fix; the resolutions are folded into "The fix that
landed" above and repeated inline here.)*

1. **Should the `v2z80` banked ROM fold into the banked-memory mechanism?** *Resolved: no — it
   stays its own board.* The V2 Z80 CPU
   board's onboard 8 KB EEPROM is mapped as **two 4 KB pages** switched by `OUT D3H` bit 1
   (`reference/v2-z80-cpu-board.md`, `src/boards/v2z80.{h,cpp}`) — a genuine banked ROM, today
   implemented as its own bespoke board. Its full memory-manager banking (ports D2H/D3H) is
   deliberately *not* modeled (the Dual SD target is flat-64K CP/M 3). The question: is this the
   *same kind of thing* as `memory`-board bank switching, such that a corrected banked-memory
   facility should absorb it — or is a CPU board's onboard paged EEPROM legitimately its own
   board? Bears on whether "banking" is a memory-board feature or a reusable mechanism.

2. **New board type, or more knobs on `memory`?** *Resolved: a new board type, `bankmem`, with a
   `card=` variant strap.* Banked RAM is materially more complex than the
   plain RAM/ROM the `memory` board otherwise models, and — now that we know the real cards —
   they do not share one parameterization: a static address-strap (ExpandoRAM I), a PROM
   page-select (ExpandoRAM II), a one-hot select (Vector), a bit-mask (Cromemco), and an
   on/off-plus-one-hot toggle (North Star) are five different decoders. The options are (a) keep
   piling per-card knobs onto `memory`, which is how we got the one-size-fits-none SIMH table, or
   (b) split banked cards into their own board type(s) that own their decode — which is exactly
   what `DESIGN.md` argues boards should do. This is the load-bearing decision for the follow-on
   and should be made deliberately, not by extending `kBanks[]`.

## See also

- `docs/boards/s100-memory.md` — the board's design doc (currently states the SIMH model as fact;
  corrected in the follow-on).
- `DESIGN.md` §10.2 — why there is no `BANK=` in the monitor (the conclusion survives; the
  "two encodings" supporting argument does not).
- `docs/sources.md` — the sourcing rule this page documents breaking.
