# Tarbell single-density floppy disk controller

**Status: not implemented.** The disk-controller half of this card is a later milestone. The **boot PROM and PHANTOM\* half is specified here now**, because it is the card that settles what PHANTOM\* means for the whole bus, and it is already proven against the bus API by `tests/test_phantom.cpp`.

> **Do not implement the FDC half from this document.** It contains nothing about the FD1771, the ports, or the disk format, because none of that has been sourced yet. See §0.1: ask Patrick, he will find the manual.

## Why this card matters out of all proportion to its size

Because of thirty-two bytes. The Tarbell's boot PROM is the reason `phantom = read` exists on the `memory` board, and the reason `Board::snoop()` exists on the bus. Both were designed for it before it was written, and it is the only card so far that needs either.

## What it does — sourced (Patrick, 2026-07-11)

1. It carries a **32-byte boot PROM**.
2. **POC\* asserts PHANTOM\*.** Power on the machine and the PROM is shadowing.
3. While PHANTOM\* is asserted, **the memory boards in the system must allow writes to their RAM, but not reads.**
4. **As soon as the Tarbell sees a memory read with A5 set**, it knows the PROM is no longer needed and **disables PHANTOM\***.

## What that implies, and why the design bends to it

### The bootstrap writes *through* itself

Point 3 is the whole reason `phantom = read` exists (`memory.md`). A 32-byte bootstrap cannot do anything useful except **load a real boot sector from disk into RAM** — and the RAM it is loading into is the RAM its own PROM is shadowing. So writes *must* reach the RAM while reads are still coming back from the PROM.

The mechanism is not a special case anywhere. The Tarbell **gates PHANTOM\* with the read strobe**: it does not pull the pin during a write cycle at all. So the memory board never sees PHANTOM\* asserted on a write, and answers it like any other write.

**The honoring board needs no strap for this, and must never grow one.** `honors_phantom` stays a bool. The read/write distinction lives on the *asserting* card, because that is where the gate physically is.

### The release is combinational, not clocked

Point 4 has a sharp edge. The bootstrap's **first fetch outside the PROM** is itself a read with A5 set — so if PHANTOM\* dropped on the *next* cycle, that fetch would happen while memory was still shadowed, read `0xFF` from the floating bus, and no Tarbell would ever have booted. **The release must take effect on the very cycle that triggers it.**

Hence the split in `Board`:

- `assertsPhantom()` is **combinational and pure** — it tests A5 off the address bus, so the triggering cycle is already un-shadowed.
- `snoop()` is the **flip-flop** — called once per completed cycle, it latches the release so it *stays* released.

Both halves are needed. Without the latch, a data read back down at `0x0004` (below A5) would re-shadow the PROM over the sector that was just loaded there.

### A5 is a wire, not a threshold

**`c.addr & 0x0020`. Not `c.addr >= 0x20`.** They differ: `0x0040` is above the PROM but has bit 5 *clear*, so it does **not** release PHANTOM\*. The card decodes one address line, and "the PROM is no longer needed" is the Tarbell's *reasoning*, not its *circuit*. Tidying this into a range compare would be a plausible, clean, wrong simulation of a wire — `tests/test_phantom.cpp` fails if anyone does.

### The PROM stops driving when it stops shadowing

The same signal that gates PHANTOM\* gates the PROM's own output drivers, so once released the PROM no longer decodes. It has to be that way: if it kept answering `0x0000`–`0x001F` after the memory board came back, the two would both drive and that is **real bus contention**, which we report and do not arbitrate (§4.6).

## Open — needs a manual before any of it is written

| Question | Why it matters |
|---|---|
| **Does front-panel RESET\* re-assert PHANTOM\*, or only POC\*?** | You said POC. If RESET\* does *not* re-arm it, you cannot reboot the machine without power-cycling it — which is a strange thing for a disk controller to require, so I suspect the two are tied. **But I am not going to decide this by deciding what would be convenient.** Modeled as POC\*-only until sourced. |
| **Where does the 32-byte PROM decode?** | `0x0000`–`0x001F` is implied by the A5 trick and by the 8080 starting at zero, and that is what the test assumes — but *implied* is not *sourced*, and the card may well have a strap. |
| Ports, FD1771 registers, disk format | Nothing here. The FDC is not designed. |
