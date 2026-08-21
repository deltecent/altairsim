# The PS II monitor loader, inside

The MITS **Programming System II** — Processor Technology's editor, assembler, monitor and
debugger for the Altair — boots from cassette in a way that repays a close read, because the
mechanism it uses to bring in its monitor is the same mechanism the rest of the package uses to
bring in everything else. Booting the monitor is **two stacked loaders**: a tiny hand-toggled
bootstrap that installs a block-load interpreter, and that interpreter reading the monitor
itself. Once you see the second one you have the whole Software Package II — the editor, the
assemblers and the debugger are nothing but data for it.

> **The payoff of digging through the bootstrap is the block-load protocol underneath it.**
> The bootstrap is a one-time trick to get that interpreter into memory when nothing yet can
> interpret anything. Everything after boot — including every tool you `EDT`, `ASM` or `DBG`
> from tape — is addressed, checksummed blocks fed to the same resident engine.

The source assets live in `tapes/MitsPS2/`: `LDRPS2.ASM`/`LDRPS2.HEX` (the toggle-in
bootstrap), `PS2-MON.TAP` (the monitor tape), `PS2-EDT.BIN`/`PS2-ASM.BIN`/`PS2-AM2.BIN`/
`PS2-DBG.BIN` (the tools), and the operator machine files `ps2.toml` and `ps2int.toml` (the
second is the interrupts variant — an 88‑VI/RTC and sense switch A9). Boot either with
`altairsim ps2` / `altairsim ps2int`; to watch the loader run, drive it over `--mcp` and set a
breakpoint (see `docs/manual/mcp.md`).

## Layer 1 — the hand-toggled bootstrap

`LDRPS2.HEX` is the twenty bytes you would toggle in at `0x0000` on real hardware. It reads the
88‑ACR one byte at a time and copies the second-stage loader **downward** into
`0x0F00–0x0FAD`, then jumps to it. The whole thing is in `tapes/MitsPS2/LDRPS2.ASM`; the core
is a five-instruction loop:

```
        lxi     h,0FAEh          ; H=0F (page), L=AE (=174 = leader byte = byte count)
loop    lxi     sp,stack         ; init SP so a RET jumps back to loop
        in      06h              ; ACR status
        rrc                      ; new byte ready?
        rc                       ;   no  -> RET -> loop
        in      07h              ; the byte
        cmp     l                ; is it the leader byte?
        rz                       ;   yes -> RET -> loop  (skip leader)
        dcr     l                ; not leader: step the address down
        mov     m,a              ; store it (reverse order)
        rnz                      ; more to go? -> RET -> loop
        pchl                     ; L hit 0 -> jump to 0F00, the code we just loaded
```

The trick worth naming is that **register `L` does triple duty**: it is the expected leader
byte, the remaining byte count, and the low byte of the descending store address, all at once.
That only works because the payload is exactly **174 bytes** and the tape's leader byte is
`0xAE` — the same number. `H:L` starts at `0x0FAE`; each stored byte decrements `L`; when `L`
reaches zero the last byte has landed at `0x0F00` and `PCHL` jumps there. One register, because
the tape was authored so the three quantities coincide.

This is the **4K BASIC v3.2** bootstrap (`lxi h,0FAEh`), *not* the 8K one. The distinction is
invisible from the tape: the leader byte `0xAE` is the low byte of the load address, so it is
identical for the 8K loader (which would place the code a page higher) — nothing on the tape
tells the two apart, and neither can you until you disassemble what landed. Load this second
stage 4K too high and every one of its own jumps lands in the empty page below it and the
machine wanders off silently. The `LDRPS2.ASM` header spells this ambiguity out at length; the
`ps2.toml` / `ps2int.toml` comments repeat the warning where an operator meets it.

## Layer 2 — the block-load protocol

The 174 bytes that land at `0x0F00` are the real loader, and it is general-purpose. It:

1. **Reads the sense switches** (`IN 0FFH`) to learn which serial port pair the console is on.
2. **Self-modifies its own `IN` instructions** — it patches the port operands of its own read
   instructions to the UART pair the switches selected, so one image serves whichever card is
   present. (This is why a linear disassembly of the loader as it sits on tape is only *half*
   the story: some of its bytes are rewritten before they run. See the next section for the
   sharpest instance.)
3. **Runs a command loop** over two commands read from the console/tape stream:
   - `'<'` — load a block: a length, a load address, that many data bytes, and a checksum. The
     block is range-checked, stored, **read back and verified**, and checksummed before the
     loader accepts it.
   - `'x'` — read an address and **execute** (jump to it).

`PS2-MON.TAP` after the bootstrap payload is exactly this: eleven `'<'` blocks, the low
RST-vector block loaded **last**, and a final `x 0040` that warm-starts into the freshly
assembled monitor. The monitor is not a single image with an entry point of its own — it is
a handful of checksummed blocks and a jump, delivered by the interpreter the bootstrap just
installed.

## The `0x0F27` byte — filler *and* code

While reading the second stage under `DISASM`, one byte reads as nonsense: `0x0F27`. It looks
like filler that only exists to make the loader assemble, and removing it "to clean up the
disassembly" is exactly the wrong move — because **the byte is both filler and code, decoded
two different ways depending on how control reaches it.**

The sense-switch setup ahead of it is a chain of conditional jumps, one per valid switch
setting. Two paths run *through* this region:

| Reached at | Bytes | Decodes as | When |
|---|---|---|---|
| `0F27` | `DA 06 06` | `JC 0606` | all the preceding conditional jumps fell through |
| `0F28` | `06 06` | `MVI B,06` | a preceding conditional jump landed on `0F28` |

The single byte `DA` at `0F27` is either the opcode of a `JC` (when execution flows straight
into it) or skipped entirely (when a jump lands one byte past it, on the `06 06` that then reads
as `MVI B,06`). No linear disassembler can render this honestly: a disassembly assigns each byte
to exactly one instruction, and this byte belongs to two. That is the real answer to "why does
the loader disassemble into garbage here" — the garbage is inherent to an overlapping
instruction, not a defect in the tool and not a region you could mark as "data" without lying
the other way. Following it needs a breakpoint at the main-loop entry and a walk through each
valid sense-switch path (`docs/manual/mcp.md`), not a static listing.

One theory that did *not* survive the digging: that the filler exists to shift the byte stream
past a leader-match collision, or that the monitor reuses this second-stage loader to bring in
its extensions. The reuse theory is disproved directly — loading the editor overwrites
`0x0F00–0x0FAD`, where the second-stage loader sits, so it cannot still be resident to be
reused. The extensions come in by a different door.

## The tools are pure block-load data

That different door is the block-load interpreter, still resident from the monitor boot. The
editor tape is the clean example. `PS2-EDT.BIN` (1920 bytes) has **no leader trick and no
second-stage loader** — just an `EDT` name fragment, eight `'<'` blocks landing at
`0x0A40–0x1150`, and a closing `x 0A40`. It is nothing but addressed, checksummed data for the
engine the monitor already installed; live, an `EDT` load reads 1910 of the 1920 bytes (the last
ten are trailing padding) and a dump at `0x0A40` matches the tape byte for byte.

This is the I/O-Table design the PS II manual describes. `EDT` loads a program of that name from
wherever the symbolic name `ABS` points — `ABS` defaults to `TY` (the console), which is why
`OPN ABS,AC` must first repoint `ABS` at the cassette driver before the load will come off tape.
The same shape covers the rest of the package: `PS2-ASM.BIN`, `PS2-AM2.BIN` and `PS2-DBG.BIN`
are all block-load data over the resident interpreter. Understand the monitor boot and you have
understood how every one of them arrives.
