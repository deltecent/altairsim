# MITS Altair 8800 front panel (Display/Control board)

**Status:** done. Board type `fp` — `BOARDS ADD fp fp0`.

## The real hardware

The thing on the front of the box: 16 address switches, 8 data switches (the low 8 *are* the low
8 address switches — one row, see below), the RUN/STOP/SINGLE-STEP/EXAMINE/DEPOSIT/RESET/PROTECT
toggles, 36 LEDs, and a key switch. Electrically it is **two boards**, the **Display/Control
board** and the LED/switch board behind the panel, and the D/C board is what plugs into the bus.

It is a card, so it is a `Board` here, with no special case anywhere (`DESIGN.md` §3).

To the *guest* — the only thing this simulator has to get right — the whole panel is **one input
port**. Everything else it does (jamming a `JMP` into the processor, pulling `PRDY` low, pulsing
`MWRITE`) it does to the CPU and the bus, not through a register the program can see.

### The sense switches are the top eight address switches

Not "eight switches next to the address switches" — **the same eight switches**, `SA8`–`SA15`.
The reason is on schematic 880-106 and it is the good kind of reason:

The D/C board already had a bank of 7405 open-collector buffers whose job was strobing
`SA8`–`SA15` onto `D0`–`D7`. That bank exists for **EXAMINE**. The Theory of Operation spells the
sequence out — EXAMINE runs a 2-bit counter and jams a jump into the processor a byte at a time:

> When the Examine switch is depressed the counter (IC J) is started. On the first count, a jump
> instruction (JMP 303) is strobed directly onto the bidirectional data bus at the processor. …
> On the second count, the settings of switches SA 0 through SA 7 are strobed onto the data bus …
> The third count strobes the settings for switches SA 8 through SA 15 onto the bus.

So the hardware to put `SA8`–`SA15` on the data bus was already there. **The sense-switch input is
that same bank, enabled a second time**, by one more gate. MITS got an input port for almost
nothing, and the operator got eight switches that mean two different things depending on what the
machine is doing.

That is why `sense` is a *slice* of one 16-bit switch register in this card and not a field of its
own. Two fields could disagree. On the sheet metal they physically cannot.

### The decode

An 8-input NAND (7430) fed by **A8–A15**, gated with **sINP**. All eight address lines high, on an
input cycle, and the buffers drive the bus.

- **It is on A8–A15, and that is not a different port.** The 8080 puts the port number on *both*
  halves of the address bus during `IN`, so decoding the high half **is** decoding the port.
- **Full 8-bit decode.** Every line is in the NAND. Port `0xFF` and nothing else — no mask, no
  don't-cares.
- **Input only.** The enable is gated with `sINP`; there is no `sOUT` in it. An `OUT 0FFH` is not
  this card's, goes unclaimed, and the byte is discarded by the backplane.

The port is **377 octal**, and the Theory of Operation goes out of its way to disambiguate that in
a footnote — *"This address is listed in octal format. It is the same as the decimal address '255'
listed in the assembly manual."* It is `0xFF`.

### SSW DSB (bus pin 53), and why it is not modeled

The sense switches are wired **directly to the data bus at the processor** — *inside* the CPU
card's input buffers. So when they drive, those buffers have to get out of the way, and the D/C
board asserts `SSW DSB` on pin 53 to do it:

> **SSW DSB** — SENSE SWITCH DISABLE. Disables the data input buffers so the input from the sense
> switches may be strobed onto the bidirectional data bus right at the processor.

We model the **byte**, not the pin. Nothing else in the machine can observe `SSW DSB`, because
nothing else is inside the 8080's own buffers — it is a wire between two cards that both already
agree about what is happening. A board that could see it would be a board that does not exist.

## Sources

| Source | Path | Authority |
|---|---|---|
| Altair 8800 Theory of Operation | `reference/Altair 8800 Theory of Operation.pdf` | **Authoritative.** Has a text layer. The CPU board's gating logic (`SSW DSB`, device address 377o), the D/C board's EXAMINE/DEPOSIT/SS sequences, and the bus definition table (pin 53). |
| Schematic 880-106, "Computer Front Panel Control" | `reference/Altair 8800 front panel schematic.pdf` | **Authoritative for the decode.** A page image — no text layer, read it as an image. The `sINP` + A8–A15 8-input NAND, and the three 7405 buffer banks. |
| Altair 8800 Operator's Manual | `reference/Altair 8800 Operators Manual.pdf` | The switch and LED inventory, and the LED disclaimer quoted below. |
| DBL, the disk boot PROM | `roms/DBL/DBL.ASM` | **In the tree.** The one piece of shipped software that reads this port, and its own comment says what bit 4 means. |

All three PDFs are from **deramp.com** (authorized — `docs/sources.md`), under
`010-S100 Computers and Boards/00-MITS/20-Altair Systems/11-Altair 8800/`. They are not in git;
`reference/` never is.

## Register reference

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `0xFF` | **Not decoded.** Unclaimed; the byte is discarded. | The SENSE switches: `SA8`–`SA15` → `D0`–`D7`. `SA8` is bit 0. |

`SA12` is **bit 4**, and that is the bit period software actually tests. See below.

## How it is simulated

- **Decodes** `Cycle::IoRead` on port `0xFF`, and nothing else. Not memory, not `IoWrite`, not
  `IntAck`.
- **`wantsSnoop()` is true.** The lamps are wired to the backplane — that is not a metaphor, it is
  what an LED soldered to a bus line does — so the card watches every cycle and latches the
  address, the data and the status. This needed **no new bus concept**: `snoop()` already existed
  (`core/board.h`), and it is the clocked half of the board interface, which is exactly where a
  latch belongs.
- **Interrupts:** none. The panel does not drive pin 73. (`STOP` is not an interrupt — it pulls
  `PRDY`, which is a different wire and a different idea.)
- **Bus master:** no. See "Limitations".
- **Properties:** `sense` (hex, `SA8..SA15`) and `data` (hex, `SA0..SA7`) — both are halves of one
  16-bit switch register, and setting either leaves the other alone — plus `fps` (default `256`), the
  cap on how many `L` frames per second the graphical bridge unit puts on the wire. `SET fp0 FPS=<n>`
  retunes it live; see "The graphical panel bridge" below.

### Reset

- **`Reset::PowerOn`** (POC*, cold): the **lamps go out**. The switches do not move.
- **`Reset::Bus`** (RESET*, the button on this very panel): **nothing.** The switches do not move.

**A toggle is a toggle.** Nothing on a real panel moves a switch except a finger, and neither reset
is a finger. The asymmetry — power kills the lamps but not the switches — is the hardware's, and it
is the only thing `power()` has to say.

## The graphical panel bridge (the `gui` unit)

The panel is a card, and a **graphical** front panel is a *view* onto that card (`DESIGN.md` §3) —
the same relationship a windowed terminal has to a serial board. The view lives in another process,
**altairsim-fp** (a separate repo: an OpenGL front panel that is a **TCP server**), and this card
**dials out** to it. Nothing new in the monitor and no CLI flag: the board declares one connectable
unit and the generic `CONNECT`/`DISCONNECT` machinery does the rest (`DESIGN.md` §7.7).

```
> CONNECT fp0:gui socket:HOST:PORT      # dial the running bridge
> DISCONNECT fp0:gui                     # unplug it, and stop redialling
> SET fp0 FPS=60                         # retune the frame cap live
```

- **One unit, `gui`, `UnitKind::Serial`.** The board owns the `ByteStream` (a card with nothing
  plugged in holds a `NullStream`, never a null pointer); the monitor resolves the endpoint string
  and hands the stream down. `socket:HOST:PORT` is an ordinary built-in endpoint.
- **The board does its I/O in `pump()`, once per emulated slice — no thread.** That is what keeps
  the bridge deterministic under RECORD/REPLAY: the panel is touched at a known point in emulated
  time, like every other board (`DESIGN.md` §7, §7.6).
- **Persistent reconnect, on the host clock.** The bridge is opened and closed out of band (a human
  launches or quits a window), so the board never gives up on its own: if the line drops it redials
  `endpoint_` on a capped exponential backoff (0.25s → 4s), measured on `std::chrono::steady_clock`
  — **host wall time, not emulated time**, so a flat-out guest and an idle one behave identically and
  the guest cannot measure any of it. `DISCONNECT fp0:gui` is the one explicit stop. The first
  failure and each successful (re)connect are logged **once**, never per retry.

### What crosses the wire

The wire is line-based and pinned by altairsim-fp's own **`docs/panel-protocol.md`** (version 1) —
that repo is the authority for the *format*; this section does not duplicate it. altairsim is the
authority for the *content*.

- **`L <addr:04x> <data:02x> <status:02x> <flags:02x>`** (this card → bridge). `addr`/`data` are the
  raw address- and data-bus lamps; `status` is the **8080 status word off the last bus cycle,
  forwarded verbatim** — the same latched word §4 describes (see "the status word is a bus signal"
  below), **`WO̅` active-low** (`0` on a write/output, `1` on a read/input). The real Altair panel
  displays `WO̅` **raw, with no inversion**: the WO LED is **lit on a read and dark on a write**
  (Operator's Manual §4), and an M1 opcode fetch — a memory read — lights MEMR, M1 and WO together.
  Neither side inverts. `flags` is `0`: the §3
  machine-control group (RUN/WAIT/HLDA/…) is not modelled — "absent rather than wrong."
- **Sent throttled and only when it changed.** Each `1/fps` interval the frame is built and put on
  the wire *only* if it differs from the last one sent **and** the socket can take it
  (`writable()` — TCP backpressure). An idle or halted guest costs nothing.
- **Handshake:** `HELLO altairsim-fp 1` both ways; each side adopts `min(local, remote)`.
- **`W <sw:04x>` / `S <sense:02x>`** (bridge → this card) drive `setSwitches()` / `setSense()` — the
  on-screen operator throwing the address/sense switches. A guest `IN 0FFH` then reads the sense byte
  through the ordinary `read()` path. Unknown or malformed lines are ignored (forward-compat).

### The status word is a bus signal (not a lamp this card invents)

`snoop()` copies `BusCycle::status` (`core/bus.h`, `enum Status8080`) into the latch **verbatim** —
no `switch` on the cycle type, no lookup table. The status word is the 8080's, latched at SYNC and
carried on the backplane; the CPU/bus layer is its emitter and this card is a forwarder. The
authority for what the eight bits *mean* is the **Altair 8800 Operator's Manual §4** (the real
hardware), Intel 8080 order: MEMR, INP, M1, OUT, HLTA, STACK, `WO̅`, INTA. This is why `busStatus()`
is named for a bus signal and not "lamp bits": the card forwards the active-low word as-is and the
bridge lights the lamps straight from it — `WO̅` included, drawn raw (lit on read, dark on write),
with no inversion on either side.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The sense switches are `SA8`–`SA15`, the **high** half of the address switch row. | You read the DATA switches instead, `SET fp0 SENSE=` sets the wrong eight bits, and DBL picks the wrong stop-bit count while everything still *looks* fine. |
| `IN 0FFH` only. `OUT 0FFH` is **not** decoded. | A card that latched an OUT would silently absorb a byte the real machine discards, and would contend with any board that legitimately wants port FF for output. |
| Nothing moves a switch but a hand — not RESET, not POWER. | A panel that cleared its switches on POWER would be a panel that cannot be *set from a config file*, since `power()` runs after the TOML is read. Every machine would boot with the switches at zero. |
| The lamps show the last cycle **that went by**, not the last cycle *this card answered*. | Dark lamps, or lamps that only twitch on port FF. |
| A read's data lamps show the byte that came **back** — including `0xFF` from the floating bus. | Half the lamps dark forever. This one needed a real fix in `Bus::settle()`; see below. |

### The bus fix this card forced

`BusCycle::data` was documented "valid on writes", and during the cycle that is true — nobody has
driven the bus yet when `decodes()` and `read()` are asked. But `snoop()` runs **after the cycle
completes**, and by then a byte *has* been driven: by the board that answered, or by the floating
bus, which drives `0xFF` just as surely.

So every read path in `Bus` now back-fills `data` with the byte that came back before the snoopers
and observers see it. That is not a convenience for this card — **it is what the wire is doing**,
and the DATA lamps are eight LEDs that do not care which direction the byte was going. `TRACE` and
`BREAK MEM` get the same corrected cycle, and they are better for it.

## Limitations and deliberate departures

**The panel is not a bus master, and EXAMINE / DEPOSIT / RUN / STEP still live in the monitor.**
On real hardware those switches *are* this board: EXAMINE jams a `JMP` into the CPU, DEPOSIT pulses
`MWRITE`. Here they are monitor commands that originate ordinary bus cycles — which is the right
call for a terminal, and `docs/cli-commands.md` argues it at length. `BusCycle` deliberately
carries no `origin` field, so a monitor DEPOSIT is *already* indistinguishable from a CPU write,
exactly as it is on the backplane. The switches those commands stand in for (`SA0`–`SA15`) live on
this card, which is where a graphical panel will find them.

**M1 and STACK are now lit, and one bit is still absent — the reason is the bus, not the card.**
This card **has** these lamps and only forwards what the bus carries (see "The graphical panel
bridge"), so lighting a bit was a change at the *source* with **none here**.

**The CPU now generates the whole 8080 status word** (Operator's Manual §4) and asserts it on the
cycle it originates; the bus carries it verbatim. So **`M1`** lights on every opcode fetch and
**`STACK`** on an 8080 stack push/pop — both arrived here the moment the CPU began sending them,
because `snoop()` forwards the word unchanged. `MEMR`, `INP`, `OUT`, `WO̅` and `INTA` are true as
before. Two notes:

| Bit | Status |
|---|---|
| **HLTA** | Still `0` at the source. Halt-acknowledge is a machine-control indicator, not something a memory cycle carries; the bridge composes it from the CPU's halt flag, not from `BusCycle::status`. |
| **STACK on a Z80** | A Z80 has **no** STACK status output, so a Z80 never lights it — its push/pop are ordinary memory cycles. STACK is an 8080-only lamp. |

**The §3 machine-control indicators are a different group entirely — not the status word.** `INTE`,
`WAIT`, `HLDA`, `PROT` (and `RUN`) are **pins** on the processor and the memory cards, not bus
cycles, and `snoop()` will never see one. `INTE` is the 8080's interrupt-enable flip-flop; `WAIT` and
`HLDA` are what the panel does *to* the CPU, not what it watches. These belong to the wire `flags`
byte, which stays `0`, and are deferred entirely.

**The lamps are honest about being a blur, and so was MITS.** From the Operator's Manual, on the
indicator LEDs:

> While running a program, however, LEDs may appear to give erroneous indications.

They show whatever went by last. At 2 MHz that was a blur in 1975 and it is a blur here.

**PROTECT/UNPROTECT is not modeled**, and was deliberately removed from the memory card once
before — see `docs/boards/s100-memory.md`, which explains why it needs a manual first and must not
come back merely because "ROM ought to be write-protectable."

## Verification

`tests/test_frontpanel.cpp` — the port, the switch row, the unclaimed `OUT`, both resets, the
lamps, the TOML round-trip, and the refusal of the old `[machine] sense` key. The bridge unit adds
socket sections that play the **server** role (altairsim dials out): the two `HELLO`s, `L` frames
tracking a running guest with `WO̅` active-low on the wire (write → `D1=0`, read → `D1=1`), `W`/`S`
round-tripping to `switches()`/`sense()` and a guest `IN 0FFH`, and a dropped listener re-accepting
to prove the persistent redial (with the backoff shortened for the test).

**The acceptance test is DBL, and it is not arranged.** The disk boot PROM in `roms/DBL` reads this
port at `FF22` to configure the console, and its own source says what the bit means
(`DBL.ASM:166`):

```
;INITIALIZE THE 2SIO. READ TEH SENSE SWITCHES TO DETERMINE THE
;NUMBER OF STOP BITS. IF SWITCH A12 IS UP, IT'S ONE STOP BIT.
;OTHERWISE. IT'S 2 STOP BITS.

FF22  DB FF     IN   SENSE      ;READ SENSE SWITCHES
FF24  E6 10     ANI  10H        ;GET STOP BIT SELECT FOR 2SIO
FF26  0F        RRC             ;MAKE IT ACIA WORD SELECT 0
FF27  0F        RRC
FF28  C6 10     ADI  10H        ;WORD SELECT 2 FOR 8 BIT DATA
FF2A  D3 10     OUT  10H        ;8 BITS, 1-2 STOPS, NO PARITY
```

Switch **`SA12`** — bit 4 — **up** gives control byte `0x14` (8N1); **down** gives `0x10` (8N2).
Both boot CP/M. The difference is the length of a character on the wire, 10 bit-times vs 11, which
the 6850 model uses to pace the line.

You can watch it happen:

```
> SET fp0 SENSE=A5
> BREAK IO R FF
> RUN FF00
breakpoint 1 (io r   FF) -- stopped at 2C11
A=A5 ...
2C11  E6 10     ANI 10
```

`A5` is what was on the switches. (The `IN` is at `2C0F`, not `FF22`, because DBL copies itself
into RAM at `2C00` and runs there — the EPROM was too slow to execute from. See `docs/roms.md`.)

**What this port used to return: `0xFF`, always.** Before this card existed, `sense` was a byte on
the `Machine` that nothing put on the bus. No board decoded port `0xFF`, so `IN 0FFH` read the
floating bus, and DBL's bit-4 test was reading a **wire**, not a switch — it got a 1 every time and
the machine ran 8N1 by luck. `tests/test_frontpanel.cpp` keeps a tripwire on the floating case, so
a machine with *no* panel still honestly returns `0xFF`.

## Related

- **The 88-TURNKEY board also cares about port FF** — an `IN` from it is how you tell the Turnkey
  board to switch its PROM out. That board would **snoop** the read for its side effect, not
  *answer* it, so it would not contend with this card for the port. ALTMON has the code for it
  (`ALTMON.ASM`, `TKYDSBL`), assembled out by default.
- `docs/cli-commands.md` — EXAMINE, DEPOSIT and RUN, and why they are the panel's switches.
- `docs/roms.md` — DBL's provenance, and what it actually does.
