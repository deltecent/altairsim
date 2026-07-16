# `memory` — Static memory card (RAM and/or ROM)

**Status:** milestone 1a. **The first board, and for a while the only one.**

## The real hardware

Period S-100 memory cards are a **base address** and a set of **populated areas**, both set by jumpers, DIP switches, and which sockets you actually filled. Several cards live in one machine and together they make up the memory map.

Three facts about real cards drive this design. The first two are commonly missed; the third is the one that shapes the whole board.

1. **A card was frequently only partly populated.** You bought a 16K card and soldered in 4K of chips, meaning to fill it later. The card decoded its full range but **only answered where the chips actually were.** So "size" and "which addresses respond" are *not* the same thing.
2. **RAM contents are lost when power is removed — and only then.** Pressing the reset button does not clear memory, and a board that clears its store on RESET\* is modeling a machine nobody ever built.
3. **One card could carry both RAM and ROM, in several areas at once.** A PROM card is a row of sockets — four 2708s at F000/F400/F800/FC00 — and any of them may be empty. Combo RAM+ROM cards existed too.

## Why one board and not two

An earlier draft had a `ram` board and planned a separate `rom` board. That is wrong, and fact 3 is why: a real card may hold **two ROM areas and two RAM areas**, and modeling it as four boards would put four lines in `BOARDS` for one physical card — breaking the premise the whole bus model rests on, that a board *is* a card.

So there is **one `memory` board, holding a list of regions.** A region is *an area of the card that is populated with something*:

| Region `type` | Reads | Writes |
|---|---|---|
| `ram` | from the store | **stored** |
| `rom` | from the store | **not decoded** |

That is the only difference between them. A ROM socket and a RAM chip-range are otherwise the same thing, and collapsing them makes the board *smaller*, not bigger: an unpopulated ROM socket and an unpopulated RAM page are now the same case, handled by the same page map, floating to the same `0xFF`.

### There is no write-protect on this board

An earlier draft also modeled the Altair front panel's **PROTECT / UNPROTECT** here. It is gone, at Patrick's direction (2026-07-11), and the reason is worth keeping because someone will otherwise re-add it.

Protect was standing in for *"load a HEX file and treat it as ROM."* It is the wrong tool for that, and the reason is fact 2: **RAM is lost at power-off.** So the image has to be re-loaded on every power cycle — and the protect map *survives* while the bytes do not, leaving a region that is confidently marked protected and full of garbage. A `rom` region re-reads its file on power-up, which is what a PROM does.

> If the front-panel feature is ever wanted as a *replica*, it comes back — but it needs a manual first (its granularity is unsourced; `DESIGN.md` §0.1), and it must not be reintroduced merely because "ROM ought to be write-protectable." **ROM is not protected RAM. It is a region that does not decode writes.**

## Sources

| Source | Path | Authority |
|---|---|---|
| **`s100_bram.c` / `s100_bram.h`** — the five banking types, their ports, bank counts, and select encodings, including the Cromemco 7-bank mask and the Vector/OASIS quirk | `../../simh/Altair8800/s100_bram.c` | **Authoritative.** Patrick Linstruth's own SIMH module (© 2025). Our own prior art, not another project's — see `DESIGN.md` §0.1. |
| Page granularity, size shortcuts | Patrick (2026-07-11) | 256-byte (100H) pages; `48K`/`64K` shortcuts. |
| **A reset never clears RAM; only power-off does** | Patrick (2026-07-11) | See *Reset*, below. |
| **One card may hold several RAM and ROM areas** | Patrick (2026-07-11) | Hence regions, and hence no separate `rom` board. |
| No write-protect | Patrick (2026-07-11) | See above. |

> The **unbanked** card is a *generic* memory board, not a replica of a specific part number — it is not claiming to be an 88-16MCS and should not pretend to be. The **banked** types are specific, named, real cards, and their quirks are theirs.

## Register reference

**Unbanked (`bank_type = none`): no registers at all.** The board decodes memory cycles only, claims no I/O port, and the guest cannot reconfigure it. Regions are *configuration*.

**Banked: exactly one write-only I/O port**, whose address, bank count, and **data encoding all depend on which card you are modeling.** See below — this is the interesting part.

## Regions

```toml
[[board]]
type = "memory"
id   = "mem0"
honors_phantom = "all"         # a jumper: another board pulls PHANTOM*, do I switch off?
                               #   none | read | all.  "read" = off for reads, still
                               #   answering writes -- what a Tarbell needs beneath it.

  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "48K"                 # 0000-BFFF

  [[board.region]]
  type = "rom"
  at   = 0xF000
  mount = "roms/monitor.bin"   # 2048 bytes -> decodes F000-F7FF

  [[board.region]]
  type = "rom"
  at   = 0xFF00
  mount = "roms/dbl.hex"       # 256 bytes  -> decodes FF00-FFFF
```

**A `rom` region's size comes from its image**, rounded up to a 100H page. A short image decodes a short range — drop a 256-byte boot PROM anywhere and it occupies one page.

### Built-in ROMs — `mount = "builtin:<name>"`

**The common ROMs are compiled into the simulator.** There is no portable place to put ROM images across macOS, Linux, and Windows — `/usr/share`, `~/Library`, `%APPDATA%`, and "next to the binary" are four different answers, and every one of them is a support question. ROMs are small (a boot PROM is 256 bytes; a monitor is 2K), so they are embedded and the question disappears:

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  mount = "builtin:dbl"        # compiled in. Works on a fresh checkout, on any OS,
                               #   with no download and nothing to install.

  [[board.region]]
  type  = "rom"
  at    = 0xF000
  mount = "roms/my-monitor.hex" # a bare path is still a host file. Yours wins.
```

The `builtin:` prefix follows the same scheme idiom the design already uses for `connect` (`socket:`, `serial:`, `file:`, `console`) — so it needs no new grammar, and `CONFIG SAVE` round-trips the string `builtin:dbl` rather than a wad of bytes.

**Every built-in ROM must have a provenance row in `docs/roms.md`** — what it is, where the dump came from, its exact size, and its CRC32. This is `DESIGN.md` §0.1 applied to binaries: *an embedded blob with no source is a second-hand fact*, and a ROM that silently differs from the real part is the single most expensive bug this project could ship, because every piece of software above it would be debugged against the wrong ground truth. A unit test verifies each CRC at build time, so a corrupted embed fails the build instead of failing a user.

```
altairsim> SHOW ROMS
  name       size   crc32     description
  ---------------------------------------------------------------------
  dbl        256    ????????  MITS 88-DCDD disk boot loader, rev 4.1
  ...

  Built in. Use  mount = "builtin:<name>"  in a [[board.region]].
  A bare path loads a host file instead — see docs/roms.md for provenance.
```

Built-ins are a **convenience, never a lock-in**: a path always overrides, so anyone with a different dump of the same part can use it without patching the simulator.

**`F800`–`FEFF` above is an empty socket.** The board does not decode it. If nothing else in the machine does either, it floats to `0xFF` (see below). This needed no new mechanism: an empty ROM socket and an unpopulated RAM page are the same case.

**Regions are sub-units**, exactly like the 88-DCDD's drives and the 88-2SIO's units, so the existing `id:unit` addressing already reloads a socket with no new monitor syntax:

```
altairsim> MOUNT mem0:rom0 newdbl.hex
mem0:2 — rom, FF00-FFFF (256 bytes, 1 page), from newdbl.hex
```

### An empty socket is still a socket

A `rom` region with **no `mount`** is a socket with no chip in it, and that is an ordinary thing for a card to be — a four-socket PROM board with two chips in it is a machine somebody actually owned. It decodes nothing, so those pages float; it still has a unit name, so you can `MOUNT` a chip into it.

**`UNMOUNT` pulls the chip. It does not unsolder the socket.** The region stays, empty, and keeps its name. This is not fussiness: the sockets are **numbered**, so erasing the region would renumber every socket behind it — pull the chip out of `rom0` and the chip sitting in `rom1` would silently *become* `rom0`, and `MOUNT mem0:rom0` would then put it in the wrong socket. You cannot unsolder a socket by pulling its chip.

`BOARDS` shows it as `rom1(empty)` in UNITS, and it is **absent from the memory column** — because the memory map is a map of what is decoded, and an empty socket is not.

## How it is simulated

**The page is the unit of everything: 256 bytes (100H).** A 64K space is 256 pages, `00`–`FF`. The board keeps one page map — *which region, if any, owns this page?* — and it drives `decodes()`:

```cpp
bool MemoryBoard::decodes(const BusCycle& c) const {
    // Someone ELSE is shadowing me; I switch OFF. A card does not shut itself
    // off with a signal it is itself driving -- without `!assertsPhantom(c)` a
    // ROM card honors its own assertion, nobody drives the address, and the ROM
    // reads back FF. That was a real bug; the acceptance tests caught it.
    if (c.phantom && honors(c) && !assertsPhantom(c)) return false;
    const Region* r = owner_[page(c.addr)];
    if (!r) return false;                            // empty socket / unpopulated page
    if (c.type == Cycle::MemWrite && r->kind == Rom) return false;   // <- ROM never answers a write
    return true;
}

uint8_t MemoryBoard::read (const BusCycle& c) { return store_[plane(c.addr)]; }
void    MemoryBoard::write(const BusCycle& c) { store_[plane(c.addr)] = c.data; }
```

That is the whole board. Note what `write()` does **not** contain: any check at all. **A write that reaches the board is stored, unconditionally** — because a real static RAM chip that is selected with `WE` asserted *stores the byte*; it has no opinion. And a write can only reach the board if `decodes()` let it, which a ROM region never does.

### Writes to ROM — the board does not decode them

**A `rom` region does not "reject" a write, or "ignore" it, or log it. It never answers the cycle.** This is the single most important line in this document, because everything else falls out of it and nothing needs a special case.

What happens *next* is emergent, decided by what else is in the machine — **the bus arbitrates nothing** (`DESIGN.md` §4.2):

| What else covers that address | This board's `phantom` strap | A guest write does |
|---|---|---|
| Nothing | any | **Nothing latches it. The byte is gone.** The write half of the floating bus. |
| Another card's RAM | `read` | **Lands in that RAM.** Reads still come back from ROM — shadow-RAM. This is how the **Tarbell** boot PROM loads a sector into the RAM it is sitting on top of. |
| Another card's RAM | `all` *(default)* | That card honors PHANTOM\* and switches itself off for writes too. **The write vanishes.** |

The `read` row is a genuine footgun and it is *supposed* to be: write `42` to a shadowed address, read it back, and you get the ROM byte. That is correct hardware and it will still confuse you at 1am, so `DUMP` annotates bytes that came from a phantom overlay (§10.2).

### Getting bytes *into* a ROM region

The guest cannot write ROM. **The operator can**, and the mechanism already exists — `RAW <id>` (`DESIGN.md` §10.2) reaches behind the bus, straight into the board's store:

```
altairsim> LOAD dbl.hex RAW mem0        ; the PROM burner. Not a bus cycle.
mem0: loaded 256 bytes from dbl.hex (FF00-FFFF, region 2)

altairsim> DEPOSIT FF00 41              ; a bus cycle. mem0 doesn't decode it.
FF00: no board decodes writes here (mem0 region 2 is rom). byte discarded.
                                        ; ^ not silence. see Quirks.
```

That distinction is not a simulator convenience — it is physically what happens. Burning a PROM is *not a bus operation*; you pull the chip and put it in a programmer. Modeling it as a bus write would require the bus to know who originated a cycle, which a real backplane cannot know and which no board should ever have to ask.

Intel HEX carries its own load addresses, so the file places the bytes; a `.bin` needs `AT`. The loader is already specified (§10.3), checksums every record, and fails loudly with the record number.

### The floating bus

**An unpopulated page needs no special case.** The board just doesn't decode it. If no *other* board does either, nobody drives the bus, it floats high, and **the CPU reads `0xFF`** — the same rule that yields `RST 7` on an unvectored interrupt (`DESIGN.md` §4.4). One mechanism; unpopulated memory and empty ROM sockets both fall out of it for free.

A **write** to an unclaimed address is the mirror image: nobody latches it, and it is simply gone.

### PHANTOM\* — this board both honors and asserts it

`honors_phantom` is a **jumper on real cards**, so it is a config property (`DESIGN.md` §4.2). When another board pulls PHANTOM\* (S-100 pin 67) for a cycle, a card strapped to honor it **takes itself off the bus for that cycle**. Nobody arbitrates; the asserting board is simply the only one still answering.

`phantom` is the other half — **this** board's assert strap, `none | read | all`:

- **`all` (default)** — assert on reads *and* writes in my ROM regions. A ROM that shadows completely. This is what you want when you mean "treat this as ROM."
- **`read`** — assert on reads only. Writes fall through to whatever is underneath: **shadow RAM**.
- **`none`** — never assert. If another card's RAM sits under this one's ROM, they both drive, and that is **real bus contention** — reported (§4.6), and exactly the bug a real backplane would have handed you. Do not "fix" it in the bus.

> ### `phantom = read` — sourced. The card is the Tarbell.
>
> **Settled 2026-07-11 by Patrick.** The **Tarbell single-density floppy disk controller** carries a 32-byte boot PROM and asserts PHANTOM\* **on reads only**: while it is shadowing, *"the memory boards installed in the system must allow writes to their RAM, but not reads."* That is `phantom = read`, exactly, and it is a real card doing a real job — the bootstrap needs to **write the sector it is loading into the RAM underneath itself.** See `docs/boards/tarbell-sd.md`.
>
> **This box used to say the opposite,** and the history is worth keeping. Earlier drafts justified `read` by claiming it was "what the DBL boot PROM wants." **I fabricated that**, and the ROM disproves it: DBL copies itself to `2C00` and runs there, so it never writes to `FFxx` at all. The strap was right; my reason for it was invented. The lesson is not "the guess worked out" — it is that a fabricated citation was **indistinguishable from a real one** until someone checked, and the real one turned out to be a different card doing a different thing for a different reason. §0.1 stands.
>
> **Where the read/write distinction lives — corrected 2026-07-12 by Patrick, from the Tarbell schematic.** It lives on the **honoring** board, which is why `honors_phantom` is `none | read | all` and not a bool. The Tarbell holds PHANTOM\* asserted continuously, like an interrupt, from RESET until A5 releases it — on reads *and* writes. It does **not** AND the pin with its read strobe.
>
> This box previously argued the reverse, in bold, and told the reader that `honors_phantom` "must never grow a `read` mode." That was reasoned, not sourced. And the quote directly above it — *"the memory boards installed in the system must allow writes to their RAM, but not reads"* — was saying so all along. **The memory boards.** Reading past a source to keep an argument is the same failure as inventing one.

## Banking — five real cards, and no two alike

Bank switching is **not** one mechanism with a parameter. It is a different mechanism on every card, and this table is why `DESIGN.md` §10.2 forbids a `BANK=` qualifier in the monitor: any CLI-level banking syntax would have to pick one of these and be wrong about the other four.

| `bank_type` | Card | Port | Banks | Data written selects the bank how? |
|---|---|---|---|---|
| `none` | *(plain unbanked memory)* | — | 1 | n/a |
| `eram` | **SD Systems ExpandoRAM** | **0xFF** | 8 | **Binary.** `data` *is* the bank number. |
| `vram` | **Vector Graphic** | **0x40** | 8 | **One-hot.** `0x01`→0, `0x02`→1, `0x04`→2, … `0x80`→7. |
| `cram` | **Cromemco** | **0x40** | **7** | **One-hot, masked `& 0x7F`.** Only 7 banks — **bit 7 is not a bank select** on this card. |
| `hram` | **North Star Horizon** | **0xC0** | 16 | **Binary.** |
| `b810` | **AB Digital Design B810** | **0x40** | 16 | **Binary.** |

Read that table twice before designing anything generic on top of it:

- **Three different ports** (0x40, 0xC0, 0xFF).
- **Two different encodings** — `0x04` means *bank 4* on an ExpandoRAM and *bank 2* on a Vector.
- **Cromemco has seven banks, not eight**, and masks the top bit off, because bit 7 does something else on that card.
- **Three of the five cards share port 0x40.** Two of them in one machine is a real I/O collision, and the contention detector (§4.6) should say so by name rather than letting one silently shadow the other.

**What a bank select does is the board's business, and this document states no general rule.** On these five cards it swaps the whole 64K plane — the store is `banks × 64K`, and the live bank offsets every access:

```cpp
uint8_t MemoryBoard::read(const BusCycle& c) {
    return store_[bank_ * 0x10000 + (c.addr - base_)];
}
```

Banking and the page map are **orthogonal and both apply**: the page map decides *whether this board answers at all*; the bank decides *which plane is behind it*. An unpopulated page is unpopulated in every bank.

> **None of the five banked cards carries ROM**, so whether a real combo card's ROM regions swap with the RAM planes is **unknown**, and this design does not guess. `bank_type != none` together with a `rom` region is **rejected at config time** until a real card with both turns up and brings a manual (§0.1). The bus, of course, does not care either way — it carries the `OUT` and the board decides.

### The OASIS quirk — reproduce it, and say why

The Vector Graphic card decodes `0x41` and `0x42` as banks 0 and 1 — i.e. **bit 6 is ignored**. That is not a Vector design feature anyone documented; it is that **OASIS writes those values**, and the card happens to tolerate them.

> **Get this wrong and OASIS does not boot**, and it fails in the worst possible way — a bank select that quietly lands on the wrong plane, so the machine runs and then behaves insanely later.

A bank select the card cannot decode is **not** silently swallowed. It is logged, because it is nearly always a bug in the guest or in your `bank_type`:

```
altairsim> SET mem0 BANK_TYPE=vram
mem0: banked, Vector Graphic. port 40 (write-only), 8 banks, one-hot select.

; guest does OUT 40,03 -- not a valid one-hot value
bank: invalid select 0x03 for vram at PC=0119 (mem0). bank unchanged (still 0).
```

## Fill on power-up — real RAM does not come up zeroed

`fill = zero | random`, applied to `ram` regions. **`random` is the honest default for a bench**, because real static RAM powers up in an indeterminate state, and software that *assumes* zeroed memory is buggy software that a zero-filling simulator will never catch. `zero` is available because it makes failures reproducible when you are chasing something else.

> **`random` must take a `seed`, and the seed must go in the snapshot** — otherwise it is a source of nondeterminism outside the `EventQueue`, and deterministic replay (§13) is dead the first time you need it. Exactly the class of thing §7.5 exists to prevent.

`rom` regions are not filled. They are **re-read from their files** on power-up.

## Properties

| Property | Type | Runtime? | Meaning |
|---|---|---|---|
| `region` | Region[] | config | The populated areas. Each is `type` (`ram`\|`rom`), `at` (page-aligned), and either `size` (ram) or `mount` (rom). |
| `pages` | PageMap | **yes** | The composite page map, as a range list — which pages this board answers for. Derived from the regions; editable at runtime to punch holes in a partly-populated card. |
| `honors_phantom` | Enum | config | A **jumper**. Another board pulls PHANTOM\* — do I switch off? `none` (never), `read` (reads only), `all`. Default `all`. |
| `phantom` | Enum | config | What **I** assert over my `rom` regions: `none \| read \| all`. Default `all`. |
| `bank_type` | Enum | config | `none \| eram \| vram \| cram \| hram \| b810`. **Determines the select port, the bank count, and the data encoding** — see the table. |
| `banks` | Int | config | Number of banks, 1–16. Constrained by `bank_type` (Cromemco caps at 7). |
| `bank` | Int | **yes** | The live bank. The *guest* sets this by writing the select port; you can also set it from the monitor to inspect a plane the guest isn't looking at. |
| `fill` | Enum | config | `random` (default) or `zero`. Applies to `ram` regions. |
| `seed` | Int | config | RNG seed for `fill=random`. **Snapshotted**, so replay stays deterministic. |

There are **no board-specific commands.** `ENABLE` and `DISABLE` are range edits to `pages`, expressed through the generic `SET` (§5), and the monitor learns nothing about memory:

```
altairsim> SET mem0 DISABLE=C000-CFFF
mem0: pages C0-CF unpopulated (4K). reads return FF.
```

`SHOW mem0` renders the map as a page grid, because a 256-bit bitmap printed as a range list is unreadable the moment it has holes in it:

```
altairsim> SHOW mem0
  mem0 — memory (static memory card)

  property        value      runtime?  legal
  ------------------------------------------------------------------
  honors_phantom  all        no        none | read | all
  phantom         all        no        none | read | all
  bank_type       none       no        none | eram | vram | cram | hram | b810
  fill            random     no        random | zero
  seed            12345      no        0..2^32-1

  region  type  range        source
  ------------------------------------------------------
  0       ram   0000-BFFF    48K
  1       rom   F000-F7FF    roms/monitor.bin  (2048 bytes)
  2       rom   FF00-FFFF    roms/dbl.hex      (256 bytes)

  page map (each cell = 100H):     . ram    R rom    - unpopulated
        x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF
   0x    .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .
   ...
   Bx    .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .
   Cx    -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
   Dx    -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
   Ex    -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
   Fx    R  R  R  R  R  R  R  R  -  -  -  -  -  -  -  R

  48K ram, 2.25K rom, 13.75K unpopulated (reads return FF).
  rom regions do not decode writes; this board asserts PHANTOM* on all cycles over them.
```

> **Note the last two lines.** "13.75K unpopulated (reads return FF)" is the difference between a user understanding their machine and filing a bug — a hole in memory is invisible otherwise. And the PHANTOM\* line is there because a write silently vanishing is the single most confusing thing this board can do to you.

## Multiple cards

Several `memory` boards coexist, each with its own regions and maps — which is what a real backplane looks like, and it exercises multi-board decode and contention (§4.6) from the first milestone:

```toml
# A plain 16K RAM card at the bottom of memory.
[[board]]
type = "memory"
id   = "mem0"
fill = "random"           # real RAM powers up indeterminate
seed = 12345              # ...but reproducibly so. Snapshotted; replay stays deterministic.
  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "16K"

# A banked card: SD Systems ExpandoRAM, 8 banks of 64K, selected by OUT 0FFH.
[[board]]
type      = "memory"
id        = "bram0"
bank_type = "eram"        # port and encoding come from the card, not from us
banks     = 8
  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "64K"
```

Overlap two cards and the bus reports it, naming both. That includes **I/O** overlap: put a `vram` and a `b810` card in one machine and they both want port 0x40 — a real collision, reported as one.

## Reset

**A reset never changes memory. RAM is lost only when the machine is powered off.** (Patrick, 2026-07-11.) Say it out loud, because the code will be tempted to conflate the two.

| Event | Monitor | `ram` contents | `rom` contents | `pages` | `bank` |
|---|---|---|---|---|---|
| **Power applied** | `POWER` | **indeterminate** — filled per `fill` | **re-read from their files** | configured | configured |
| **`Reset::PowerOn`** (POC\*) | *(part of `POWER`)* | untouched | untouched | untouched | **0** |
| **`Reset::Bus`** (RESET\*, the front-panel button) | `RESET` | untouched | untouched | untouched | **0** |

POC\* and power coming up coincide on real hardware, which is why they share the `POWER` command — but they are **different things**, and the memory array is the proof: a RAM chip has no POC\* pin. Its contents are indeterminate because *the chips just powered up*, not because a signal arrived. Model the fill as belonging to power, and let both resets leave the store alone.

**What both resets *do* clear is the bank select latch — `bank` returns to 0.** That is load-bearing and not obvious: a warm reset leaves the machine's RAM exactly as it was, but the select latch clears, so the CPU restarts looking at plane 0. Leave the old bank selected and a machine that reset cleanly comes back executing whatever was in the wrong plane.

And the trap in the other direction: *"my program vanished when I hit reset"* reads like a memory-model bug rather than a reset bug, and will cost you a day.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **A `rom` region does not decode writes** — it does not reject them | Reject them *in the board* and "writes fall through to the RAM underneath" becomes a special case in the bus, which is the mistake §4.2 exists to prevent. |
| **Unpopulated pages read `0xFF`**, not `0x00` | Memory-sizing routines find 64K on every machine and CP/M builds itself the wrong size. A zero-filled hole also disassembles as a field of `NOP`s, which is a uniquely confusing thing to stare at. |
| **A write to ROM with nothing beneath is silently gone** | It has to be — nothing latched it. But **report it at the monitor** when the operator does it by hand, or `DEPOSIT` looks broken. Do *not* report it for guest writes: period software scans memory by writing and reading back, and it would spew. |
| **A reset preserves memory; only power-off clears it** | Clear on reset and the user's program vanishes on a reset-button press — which reads like a memory-model bug, not a reset bug. |
| **Both resets clear `bank` to 0** | Preserve the bank and a cleanly-reset machine comes back executing the wrong plane. |
| **One-hot vs binary bank select is per-card** | `OUT 40,04` means *bank 4* on an ExpandoRAM and *bank 2* on a Vector. Pick one encoding for all cards and half your machines silently run in the wrong bank. |
| **Cromemco has 7 banks and masks `& 0x7F`** | Treat bit 7 as a bank select and a Cromemco bank switch decodes as a nonexistent bank 7. |
| **Vector/OASIS accepts `0x41`/`0x42` as banks 0/1** | **OASIS does not boot.** And it fails late and insanely, because the select lands on the wrong plane rather than erroring. |
| **A partially-populated card decodes only where chips are** | Model `size` as a plain contiguous range and holes become inexpressible — a real 16K card with 4K fitted cannot be represented at all. |
| **RAM powers up with indeterminate contents** | Zero-fill and you will never catch guest code that assumes zeroed memory — until it runs on real hardware. |

## Limitations and deliberate departures

- **The unbanked card is generic, not a specific period part.** Its jumper semantics are ours. A modeled MITS 88-16MCS would get its own doc.
- **A `rom` region's size comes from its image file**, rounded up to a page. Real cards decode a whole chip's worth (a 2708 covers 1K whether or not the image fills it), and `at` would have to be chip-aligned. We take the file's word instead — simpler, and it lets a 256-byte boot PROM sit at FF00, which a strict 1K-chip model could not express.
- **`bank_type != none` plus a `rom` region is rejected.** None of the five real banked cards carries ROM, so combo-card banking semantics are unsourced and are not invented (§0.1).
- **The bank select port is derived from `bank_type`, not independently settable.** On real cards it may be strappable; we do not have those manuals, so we do not invent straps.
- **Bank select is write-only, and its port is not readable.** A read of the select port is not decoded by this board, so it floats to `0xFF` like any unclaimed port — unless another board claims it, in which case you get contention, correctly.
- **POC\*'s 200 ns minimum pulse width is not modeled.** It is an analog property of the reset circuit (and on real machines, an RC network that drifts — many owners fit a dedicated supervisor IC to get a clean edge). We assume a clean POC\*. Nothing in the digital model depends on the width.
- **No parity, no error detection, no wait states.** Period cards had none worth modeling.

## Verification (milestone 1a acceptance)

No CPU exists in milestone 1a, so **the monitor is the bus master** — `DEPOSIT` and `DUMP` originate real bus cycles with nothing else in the machine, exactly as the Altair front panel did. That is not a workaround; it is a free early test of the `BusMaster` abstraction (§3).

**RAM and the floating bus**

1. A 48K `ram` region; `DUMP C000-C0FF` returns **all `FF`**. The hole is real and reads float.
2. `LOAD test.bin AT 0100`, `DUMP`, `SAVE`, `COMPARE` — byte-identical round trip. Same for Intel HEX.
3. `FILL 0000-BFFF 00`, then `RESET` → contents **survive**. `POWER` → contents **re-filled** per `fill`.
4. `fill=random` with a fixed `seed` → **byte-identical memory across two runs**, and the seed survives `SNAPSHOT`/`RESTORE`.

**ROM regions**

5. A `rom` region at FF00 from a 256-byte file decodes **FF00–FFFF and nothing else**; `DUMP FF00` shows the image.
6. `DEPOSIT FF00 41` (a bus write) is **not decoded** — `DUMP FF00` is unchanged, and the monitor **says so** rather than silently succeeding.
7. `LOAD other.hex RAW mem0` **does** change it. The operator has a PROM burner; the guest does not.
8. An **empty socket** between two ROM regions (F800–FEFF above) reads `FF`, exactly like unpopulated RAM. One mechanism.
9. A short image decodes a short range: a 2048-byte file at F000 owns F000–F7FF, and F800 is not this board's problem.

**PHANTOM\* — all three straps, because the difference is silent**

10. `phantom=all` (default), ROM over another card's RAM: reads come from ROM, and **a write vanishes** — the RAM beneath does not receive it.
11. `phantom=read`, same setup: reads come from ROM, and **a write lands in the RAM beneath**. Read it back through the bus and you get the ROM byte; read it `RAW` from the RAM card and you get `41`. Shadow RAM, correctly.
12. `honors_phantom=false` on the RAM card beneath: **both boards drive → contention reported**, naming both. This is a bug the real backplane would have handed you, and it must not be silently resolved.
13. A phantom shadow is **not** reported as contention (§4.6) — only case 12 is.

**Banking**

14. `bank_type=eram` (port 0xFF, binary). Write bank 3 to the port; `DEPOSIT`/`DUMP` land in plane 3; `SET mem0 BANK=0` and the same address holds different bytes. **The planes are genuinely separate memory.**
15. `bank_type=vram` (port 0x40, **one-hot**). Writing `0x04` selects **bank 2**, not bank 4 — this test fails loudly if one-hot is treated as binary.
16. **The OASIS quirk:** with `vram`, `0x41` selects bank 0 and `0x42` selects bank 1. If this fails, OASIS will not boot.
17. **Cromemco:** `bank_type=cram` rejects `BANKS=8`, and a select of `0x80` is not decoded as a bank.
18. An undecodable select (`0x03` on a one-hot card) **leaves the bank unchanged and logs it** — it does not silently land somewhere plausible.
19. **Two banked cards both claiming port 0x40** (`vram` + `b810`) → **I/O contention reported**, naming both. Three of the five real cards share that port; this is not hypothetical.
20. Warm `RESET` → memory survives, **`bank` returns to 0**.
21. A `rom` region on a card with `bank_type != none` is **rejected at config time** — we do not guess combo-card banking.

**Round trip**

22. Two `memory` cards at overlapping bases → **contention reported**, naming both.
23. `CONFIG SAVE` then `CONFIG LOAD` reproduces the regions, page map, and bank config exactly.

## References

- `DESIGN.md` §4.1 (decode), §4.2 (PHANTOM\*), §4.6 (contention, the floating bus), §5 (properties), §6 (reset), §10.2 (`RAW`), §10.3 (Intel HEX), §10.3.1 (built-in ROMs).
- `docs/roms.md` — the built-in ROM registry and its provenance rule.
