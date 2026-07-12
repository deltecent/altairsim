# 88-2SIO — MITS Dual Serial Interface

**Status: BUILT, 2026-07-11.** `src/boards/mits-2sio.cpp`, `tests/test_sio2.cpp`.

**The proof vehicle** — a fully-modeled 2SIO exercises every interface in the design (console, TCP socket, host serial, interrupts, multi-unit boards, multiple instances), which is why it is the only peripheral in milestone 1.

**It runs ALTMON.** `altairsim altmon`, then `CONSOLE`: Mike Douglas's 1K monitor PROM prints its banner, takes commands, and dumps memory — through this card, over a real 6850, on the real bus. That is not a test we wrote; it is a program someone wrote for real hardware, and it either works or it does not.

```
$ altairsim altmon
startup> CONSOLE F800

ALTMON 1.3
*DUMP F800 F80F
F800 3E 03 D3 10 D3 12 3E 11 D3 10 D3 12 31 00 C0 CD  >.....>.....1...
```

Those bytes are ALTMON's own first sixteen, and they are `MVI A,3 / OUT 10 / OUT 12 / MVI A,11 / OUT 10 / OUT 12` — the ROM reading its own 2SIO initialization back to us *through the card it initializes*.

> **A trap, from ALTMON's own listing.** Its `ahex` routine reads exactly four hex digits and **aborts to the prompt on any byte below `'0'`** — so a space is not a separator, it is a *cancel*. `DF800F80F`, not `D F800 F80F`. The spaces in ALTMON's own printed command summary are typography, not grammar. This cost twenty minutes and is exactly the kind of thing DESIGN.md §0.1 sends you to the source for.

## The real hardware

Two independent **Motorola 6850 ACIA**s on one S-100 card. Base address is jumper-selectable; channel A sits at BA+0/BA+1 and channel B at BA+2/BA+3. Baud rate is a jumper/clock on the board, not software-settable. Each ACIA's interrupt request can be jumpered to the S-100 interrupt lines.

Default base **0x10**, so: channel A = 0x10 (control write / status read) + 0x11 (data), channel B = 0x12 + 0x13.

## Sources

Motorola 6850 datasheet. `roms/ALTMON/ALTMON.ASM` — Mike Douglas's monitor, whose equates (`CONS equ 10h`, `COND equ 11h`, `TBE equ 2`, `RDA equ 1`) confirm the base port, the register layout **and the true-sense status bits**, and whose first four instructions are the canonical initialization sequence:

```asm
monit   mvi  a,3          ;reset 6850 uart
        out  CONS
        out  CONS+2       ;2nd 2SIO port as well
        mvi  a,11h        ;8N2
        out  CONS
        out  CONS+2
```

`roms/DBL/DBL.ASM` (the boot PROM's own 2SIO equates). Both are in this repository — first-hand period artifacts, which is what §0.1 asks for.

## Register reference

### Control register (write) — the Python prototype ignored this entirely; don't.

| Bits | Meaning |
|---|---|
| 0–1 | Clock divide: `00` = ÷1, `01` = ÷16, `10` = ÷64, **`11` = master reset** |
| 2–4 | Word select: data bits / parity / stop bits (8 standard combinations) |
| 5–6 | Transmit control: `00` RTS low + **TIE off**; `01` RTS low + **TIE on** (transmit interrupt enable); `10` RTS high + TIE off; `11` RTS low + transmit break |
| 7 | **RIE** — receive interrupt enable |

Period software writes `0x03` (master reset) then `0x11` (÷16, 8N2). An **interrupt-driven** driver sets bit 7 and/or the `01` transmit-control field — that is the path milestone 1 must prove.

### Status register (read) — **true sense**

| Bit | Name |
|---|---|
| 0 | **RDRF** — receive data register full |
| 1 | **TDRE** — transmit data register empty |
| 2 | DCD |
| 3 | CTS |
| 4 | FE |
| 5 | OVRN |
| 6 | PE |
| 7 | **IRQ** — this ACIA's interrupt request |

> **Trap:** the 88-SIO's status bits are **inverted**, the 88-2SIO's are not. A machine with both boards has both conventions live at once. Do not share code between them without thinking.

## How it is simulated

- Decodes I/O reads and writes on BA+0 … BA+3, **and no memory address at all.**
- **Two units** (`a`, `b`), each an independent 6850 with its own `ByteStream`, its own baud jumper, its own interrupt strap and its own transform chain. They share *nothing*: modeling them as one object with an index would be modeling the PCB rather than the chips on it.
- **Multiple board instances** at different base ports: `sio0` at 0x10, `sio1` at 0x14.
- Bit 7 (IRQ) reflects the **chip's own** request, jumper or no jumper — it is a pin, and it does not care what you soldered to it. Where that request *goes* is the unit's `interrupt` property (`none | int | vi0..vi7`), which is the **wire**. With `interrupt = int` and no VI board present, the `IntAck` cycle finds nobody driving the bus, the CPU reads a floating `0xFF`, and executes **RST 7**. A `vi*` strap with no 88-VI in the machine correctly does *nothing* — exactly as a wire to an empty slot would.
- **TDRE is a DEADLINE driven from the `Clock`**, at the configured baud rate and the word format the guest actually selected — see below.

### Properties

The board has one. **Everything else belongs to a unit**, because everything else is genuinely per-chip: `SHOW sio0` prints all three tables.

| Property | Scope | Runtime? | Notes |
|---|---|---|---|
| `port` | board | config | Base address. Hex — it is on the wire. |
| `baud` | unit | **yes** | `SET sio0:a BAUD=9600`. Decimal — it never is. |
| `interrupt` | unit | config | `none \| int \| vi0..vi7` |
| `connect` | unit | yes | Endpoint. `CONNECT` sets it. |
| `upper`, `strip7in`, `strip7out`, `crlf`, `echo`, `bell`, `bsdel` | unit | yes | The transform chain (DESIGN.md §7.2) — **the LINE's, not the console's**, so they work identically on a socket. |

In a config file that is `[board.unit.a]`, and `CONFIG SAVE` round-trips all of it.

```toml
[[board]]
type = "2sio"
id   = "sio0"
port = 10

  [board.unit.a]
  connect = "console"
  baud    = 9600
```

### Reset

- `Reset::PowerOn` and `Reset::Bus` both: 6850 master reset — clear RDRF, leave the transmitter ready.
- **Both keep the `ByteStream` connected.** A warm reset does not unplug the terminal, and a guest that reset its UART and found the console gone would be a baffling thing to debug.

## Quirks reproduced (and what breaks if you don't)

**TDRE is a deadline, not a flag.** The Python prototype hardwired `TDRE = 1` (always ready to send). That is not merely an approximation:

> The Mike Douglas Altair CP/M BIOS **infers the line speed by timing how long TDRE stays clear**. It counts ~21 µs ticks while polling; if the 16-bit counter's high byte reaches ≥ 11, it concludes **110 baud Teletype** and sets `sndNull`, so a NULL is sent after every CR.

Hardwire TDRE and you **silently change what the guest decides to do**. So a write to the data register records `now + charTime` and the status bit is a *comparison against the clock*, not a stored flag. `tests/test_sio2.cpp` checks that TDRE is still clear one T-state early and set one T-state later.

**The character time follows the word format**, and falls out of the control register the guest wrote — 8N2 is 11 bits on the wire, 7E1 is 10. A guest that configures a Teletype gets a Teletype's timing, and nothing in the code was ever told what a Teletype is.

**Honor the control register.** Master reset (`0x03`) must actually reset — ALTMON's very first two instructions are `MVI A,3 / OUT 10h`, so if that write does nothing, every machine that starts with a master reset starts wrong. The interrupt-enable bits must actually enable interrupts; that is acceptance test 4, and it caught a real bug (above).

## Limitations

- Baud is a board jumper on real hardware, so `baud` is a property rather than a register. Software cannot change it, which is correct.
- DCD and CTS are driven from the `ByteStream`'s `status()`. The pins are `/DCD` and `/CTS` — **the status bit is SET when the line is NEGATED** — and a console or a file has no modem control in any real sense, so both are asserted and both bits read 0. That is exactly what strapping the pins to ground on the connector does, and what period installers did constantly.
- Parity and framing errors are not synthesized. They report **line noise**, and there is no line: a `ByteStream` delivers the byte that was sent or it delivers nothing. Synthesizing them would mean inventing a noise model, which would mean inventing a probability — the kind of number §0.1 says to *ask* about rather than make up.

### OVRN is never set, and that is a correction

**An earlier version of this document said OVRN was modeled. It was, it was wrong, and it has been removed.** The reasoning is worth keeping, because the mistake is an easy one:

A real 6850 overruns because a serial line is **free-running** — the sender clocks bits down the wire whether or not the receiver is keeping up, and a character that arrives while the last one is still unread is lost. So the first implementation pulled a byte off the stream every character-time regardless of `RDRF`, raised OVRN, and dropped it.

**It lost data immediately.** ALTMON echoes the full command name as you type (`D` → `DUMP `), and while it was busy transmitting those five characters it was not reading the receiver — so the address typed after the `D` went on the floor and the dump silently did nothing.

The bug was not the pacing. **It was believing a `ByteStream` is a serial line.** It is not: it is a buffered, flow-controlled source — a pipe, a socket, an OS keyboard queue — and it will happily hold the byte until we take it. Inventing an overrun from it does not reproduce a hardware behaviour; it *manufactures* data loss the host transport does not have, and it breaks transfers that would have worked on the real thing.

So the receiver is still **paced at the baud rate** — that part is real, it is what stops a guest reading faster than the line allows, and it is the same clock TDRE is timed against — but it only ever takes a byte when the register is free. Status bit 5 is therefore always zero.

If a **host serial port** endpoint ever lands, an overrun there is a genuine hardware event and the stream can report one — from the place that actually knows, which is not this board.

## Verification (milestone 1 acceptance)

| | | |
|---|---|---|
| 1 | 4K MITS BASIC answers `PRINT 2+2` on the console | **not yet** — ALTMON runs instead, which proves the same path |
| 2 | Same session over a **TCP socket** | **not yet** — `socket:` endpoints are unimplemented |
| 3 | Same over a **host serial port** | **not yet** — `serial:` endpoints are unimplemented |
| 4 | An **interrupt-driven** console echo, with **no VI board** | **DONE** — `tests/test_sio2.cpp` |
| 5 | **Two 2SIO boards at once**, four channels, independently configured | **DONE** — the base port is a jumper, and two cards coexist |
| 6 | The card raises an interrupt **while nobody is asking it anything** | **DONE** — the guest halts; the card's own deadline wakes it |

### Test 4 is the one that mattered, and it found a real bug

It is the acceptance test the whole interrupt design stands on: the 6850 raises IRQ because a character arrived, the jumper takes it to `pINT`, the 8080 acknowledges at an instruction boundary, **nobody claims the `IntAck` cycle** because there is no 88-VI card in the machine, the bus floats to `0xFF`, and `0xFF` is `RST 7`. The vector is not *chosen* by anything — it is what an empty backplane reads as.

The first implementation **failed it**, and the failure is worth recording:

> The ACIA only took a character off the line when the guest **read a register**. But an interrupt-driven driver *never reads the status port* — not reading it is the entire point of being interrupt-driven. So `RDRF` was never set, IRQ never rose, and the interrupt never fired. The operator could type forever and nothing would happen.

A 6850's receive shift register fills **on the chip's own clock** and owes the CPU nothing. The interface has to be able to say so. Every *polled* test in the suite passed throughout — only this one could have caught it.

### Test 6 is where that fix turned out to be half a fix

**The card's free-running work was real. Putting it inside `assertsInt()` was not.**

The receiver was advanced inside that query for exactly one reason: being asked was the only thing that ever woke the card up. The bus called `assertsInt()` on every instruction, and the card quietly used the poll as its clock. It worked, and it was the wrong shape — no backplane interrogates a card for its interrupt status (**DESIGN.md §4.4.1**, and Patrick, who said so). A card **pulls pin 73 and holds it**.

Take the poll away and the hole is obvious:

> The transmit interrupt is jumpered. The guest writes a character and **halts**. TDRE goes true when the character has finished going out — and at that moment **nobody is touching the chip.** No bus cycle runs. No register is read. The CPU is parked in a `HLT` waiting for exactly this interrupt.
>
> If the only way the card can act is to *be asked*, and the only thing that would ask is the CPU that is halted waiting for it, **the machine is dead.**

That is an entirely ordinary driver, and the old run loop **declared it finished** — two thousand T-states before the interrupt it was waiting for. So the card now owns its own clock:

- **`Acia::nextEdge()`** — the next moment this chip's IRQ pin could move *with nobody touching it*. The card sets a `Clock` deadline for it (**§7.5**) and wakes itself. On a quiet line with an idle transmitter the answer is **"never"**, and no timer is set at all — which is the commonest state in the machine, and precisely the one the poll was paying full price for.
- **`pump()`** — for the thing a deadline cannot predict: a human touching a key. Nothing in emulated time saw that coming, so no timer could have been set for it.

Both, and they are not alternatives — they answer different questions. It is the same function on this card (`Sio2Board::refresh()`), and it is the answer to *"event queue, or periodic timer?"*: **both, and you already had the second one.**

`assertsInt()` is now `const` and **pure**: it reads two pins and ORs them, filtered by what is actually soldered to the wire. It does no work. The card tells the backplane when the pin moves (`Board::intChanged()`), and forgetting to is caught by `Bus::setVerify(true)`, which this suite runs with permanently on.
