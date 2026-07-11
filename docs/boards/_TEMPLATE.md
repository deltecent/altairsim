# <Board name>

<!--
  NO BOARD IS MERGED WITHOUT THIS FILE. The board and its documentation are one deliverable.

  The **Limitations** and **Quirks** sections are the load-bearing ones. They are what you
  will want in two years when something doesn't boot, and what lets someone else — or Claude —
  work on this board without rediscovering everything.

  Writing them forces the honest question "what did I NOT actually implement?", which is
  exactly the question a simulator author most wants to avoid.
-->

**Status:** <not implemented | milestone N | done>

## The real hardware

What it is, who made it, what it was for, when. The chips on it (6850 ACIA, MC6860, 1771 FDC, …).
How it was configured in period: jumpers, DIP switches, straps.

## Sources

| Source | Path | Authority |
|---|---|---|
| | | |

Cite the authoritative source for each fact: manual + section, a period BIOS's equates, a
disassembled PROM. **If two sources disagree, say so and say which one won.**

## Register reference

Every port, every bit, **read AND write** (they often differ — see the PMMI). Timing requirements.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| | | |

## How it is simulated

The mapping from real hardware to this design's primitives:

- Which bus cycles it decodes.
- What it does on `tick()`, and what it schedules on the `EventQueue`.
- Which `ByteStream` / `DiskImage` / `Display` it uses.
- If it has media: **hard-sector or soft-sector?** What `sectorSize`, `startSector`, and per-track formats does it declare (`DESIGN.md` §7.3)? How does it probe geometry from image size — and what does it do when the size is ambiguous?
- How it interrupts: `pINT` or a VI line; what the `interrupt` property accepts.
- Whether it masters the bus (DMA).
- Its `properties()`.

### Reset

**Concretely**, what each reset does to this board:

- `Reset::PowerOn` (POC*, cold):
- `Reset::Bus` (RESET*, warm — memory survives, media stays mounted):

## Quirks reproduced

The non-obvious behaviors period software depends on. **Each with a note on what breaks if you get it wrong.**

| Quirk | If you get it wrong |
|---|---|
| | |

(Inverted status bits, reads with side effects, shared registers, non-queueing interrupts,
sector counters that advance every *other* read, …)

## Limitations and deliberate departures

**What is NOT modeled, and why.** Where the simulation is an approximation, and **what class of
software would notice**. Anything that "works" but for the wrong reason.

## Verification

How we know it's right: which period software exercises it, what the test asserts.

## References
