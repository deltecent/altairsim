# Monitor commands and their abbreviations

**Settled 2026-07-11 by Patrick.** The table in `src/cli/commands.cpp` is the only copy of this; HELP and everything below are generated from it.

## The rule

**The table is in priority order, and the first command whose name starts with what you typed wins.** That is the whole algorithm — there is no minimum-abbreviation column, no priority number, and nothing that treats a one-letter word specially. One letter is just a short prefix.

`D` dumps because DUMP is listed above DEPOSIT, DISASM and DISCONNECT. It follows, without anyone deciding it, that DEPOSIT needs `DE` and DISASM needs `DI`. Reorder the table and every abbreviation in the monitor re-derives itself, including the ones printed by HELP.

This is why **UNMOUNT is not called DISMOUNT**: it's the plainer word, it takes `U` (which nothing else wanted), and removing it from the D-cluster is what let DISASM fall from `DISA` to `DI`. Nobody worked that out — the table did.

**The one invariant:** no command name may be a strict prefix of another. If one were, its full, correctly-spelled name would resolve to whichever came first and there would be no way left to type the other. Renaming REGS to REG would break exactly this; `tests/test_cli.cpp` fails if anyone tries.

## The ranking

The nine that own their prefix, in Patrick's words: **DUMP, STEP, RESET, HISTORY, MOUNT, BREAK, EDIT, CONFIG, GO.**

> **GO is gone (Patrick, 2026-07-12).** `RUN` is the switch on the front panel, and there was never a second thing for GO to be — see [RUN](#run-is-the-switch-on-the-panel) below. RESET keeps `R`, so RUN costs `RU`.

| Type | Command | Notes |
|---|---|---|
| `D` | DUMP | |
| `S` | STEP | |
| `R` | RESET | |
| `H` | HISTORY | *waiting on the debugger* |
| `M` | MOUNT | |
| `B` | BREAK | |
| `E` | EDIT | *waiting on the line editor* |
| `C` | CONFIG | |
| `RU` | RUN | RESET owns `R`; `G` now names nothing at all |
| `SE` | SET | beats SEARCH — you type it far more often |
| `SH` | SHOW | |
| `DE` | DEPOSIT | the front panel keeps its word; it costs one letter |
| `EX` | EXAMINE | the panel's other switch. Bare `EX` = EXAMINE NEXT |
| `I` | IN | runs a real IN cycle |
| `O` | OUT | runs a real OUT cycle |
| `L` | LOAD | |
| `SA` | SAVE | |
| `F` | FILL | |
| `SEA` | SEARCH | |
| `COM` | COMPARE | |
| `MOV` | MOVE | |
| `W` | WHO | |
| `BO` | BOARDS | `BOARD` works too — it is a prefix, not an alias |
| `REG` | REGS | beats REGION |
| `REGI` | REGION | |
| `DI` | DISASM | |
| `U` | UNMOUNT | not DISMOUNT — see above |
| `DISC` | DISCONNECT | |
| `CONS` | CONSOLE | **configures** the console. It does not start the machine — RUN does |
| `CONN` | CONNECT | `console | null | loopback | socket:PORT | socket:HOST:PORT | serial:DEVICE` |
| `P` | POWER | |
| `T` | TRACE | *waiting on the debugger* |
| `STO` | STOP | *waiting on a monitor that runs alongside the machine — ATTN leaves a RUN today* |
| `SN` | SNAPSHOT | *waiting on the debugger* |
| `REST` | RESTORE | *waiting on the debugger* |
| `REC` | RECORD | *waiting on the debugger* |
| `REP` | REPLAY | *waiting on the debugger* |
| `N` | NOBREAK | |
| `HE` | HELP | or `?` |
| `Q` | QUIT | the only way out — there is no EXIT |

## Two deliberate breaks with SIMH

**`D` dumps.** SIMH's `D` is DEPOSIT and its `E` is EXAMINE; on the Altair itself, DEPOSIT and EXAMINE are the two front-panel switches. Patrick: *"Most ROM monitors use D for dump. It has always annoyed me that SIMH's D was Deposit."* It also puts the shortest key on the keyboard on the command that cannot destroy anything, and makes you type two letters to change memory. That is the better default regardless of heritage.

**`E` edits and `EX` examines.** There is no EXIT — `QUIT` is the one word for leaving, so `E` and `EX` go to the two commands you actually type.

## HELP has two forms

**Bare `HELP` lists the names and nothing else** — the whole set in about ten lines:

```
altairsim> HELP

  D[UMP]            S[TEP]            R[ESET]           H[ISTORY]*
  M[OUNT]           B[REAK]           E[DIT]*           C[ONFIG]
  RU[N]             SE[T]             SH[OW]            DE[POSIT]
  EX[AMINE]         I[N]              O[UT]             L[OAD]
  ...
```

When you type HELP you are almost always hunting for a name you half-remember, and a wall of usage lines is the worst possible shape for that: it doesn't fit on a screen, so the thing you were looking for scrolls off the top. `*` marks a command that resolves but isn't built yet.

**`HELP <command>`** is where the usage and the examples live:

```
altairsim> HELP D

  D[UMP]
  DUMP [<addr>|<range>] [WIDTH=16]

  Hex and ASCII. A bare address runs to the END OF ITS PAGE, and a bare DUMP
  continues from there -- so the rows and the columns both stay page-aligned
  however you first landed. WIDTH is a count, so it is decimal.
    D 100        0100-01FF, a whole page
    D 0001       0001-00FF: stops on the boundary, last line full
    D            the next page
```

Note that `HELP D` works — the argument goes through the same prefix resolver as everything else, so you never have to spell a command out just to ask about it.

**Both forms are generated from the command table.** A hand-written help text is a second list of commands, and a second list of commands is a list that is wrong.

## EXAMINE and DEPOSIT are the front panel's two switches

DUMP answers *"what is around here."* **EXAMINE answers *"what is AT here,"*** which is a different question and deserves its own verb — paging 256 bytes to read one is how you lose the byte in the noise.

**Bare `EXAMINE` is the panel's EXAMINE NEXT**: it steps one byte. `EX 100`, then `EX`, `EX`, `EX` walks memory a byte at a time, exactly as the switch does.

```
altairsim> EX 100
0100  C3  .  11000011
altairsim> EX
0101  00  .  00000000
altairsim> EX
0102  F8  .  11111000
altairsim> EX
0103  41  A  01000001
```

The bits are there because the panel showed them on eight LEDs, and because when you are down to one byte you are usually looking at a flag.

EXAMINE keeps **its own cursor**, separate from DUMP's. They step by different amounts — a byte versus a page — and sharing one latch would mean a `D` silently threw your examine position 256 bytes down the road.

### EXAMINE *is* the CPU

The panel has **no address latch of its own.** EXAMINE stops the processor, jams the address switches into the **program counter**, and the CPU drives the address lines and MEMR\*. Everything else follows from that one fact.

**`EX <addr>` is a `JMP <addr>` you can see the destination of.** It loads the PC, so STEP afterwards executes *there* — and `RUN <addr>` is exactly EXAMINE followed by RUN, which is the pair of switches you throw on the panel.

```
altairsim> EX F800
F800  3E  >  00111110
altairsim> STEP
F800  3E 03   MVI A,03
```

**The PC is the cursor** — not a copy of it. Bare `EX` (EXAMINE NEXT) steps the program counter, because that is the only counter the panel has. Keeping a private latch beside it would mean a bare `EX` after a STEP quietly dragged the PC *backwards* to wherever the latch had been left.

**With no CPU card, EXAMINE is an error, not a degraded mode.** Nothing is driving the address lines. Patrick: *"it is the CPU that drives the address lines and memory read bus signals. No CPU, none of that works."*

```
altairsim> EX 0
no CPU in this machine.  BOARDS ADD 8080 cpu0
```

**`RAW <id>` is the exception, and only because it is not a bus cycle at all.** That is the PROM burner reaching behind the bus into a board's store (§10.2) — it needs no CPU, touches no PC, and carries its own cursor. Which is precisely why it can write a ROM when a bus write cannot.

**EXAMINE is the only memory command that needs a CPU.** DUMP, DEPOSIT, FILL, SEARCH, COMPARE and MOVE all work on an empty backplane, because you have to be able to debug the simulator without a processor in it. Patrick: *"All commands that manipulate memory other than EX are fine without a CPU because we need to be able to debug the simulator without a CPU."*

## BOARDS is the backplane

**The command is plural, and both spellings work.** `BOARD` is a *prefix* of `BOARDS`, and prefixes are the whole resolver — so `BOARDS`, `BOARD` and `BO` are one command, with no alias, no second table entry, and nothing to keep in sync. A bare `BOARDS` lists them; you do not have to say `LIST`.

```
altairsim> BOARDS
  ID    TYPE    I/O    UNITS            MEMORY
  ----  ------  -----  ---------------  ---------------------------
  cpu0  8080    -      1 cpu: 8080      -
  sio0  2sio    10,12  2 serial: a*, b  -
  mem0  memory  -      1 rom: rom0      0000-DFFF  ram  56K
                                        FF00-FFFF  rom  dbl  phantom:all

  * holds the console
```

**Each decoded range gets its own line, and says what it is.** The old listing printed `mem:0000-DFFF,FF00-FFFF` and stopped there — which cannot answer the only question worth asking about that card: *which of those is the ROM, and which ROM is in it?* Both facts were in the map all along and were being thrown away. A card carries several regions, and squashing them into one comma list is exactly what hid the difference.

**An empty socket is not in the memory column, because it decodes nothing.** It shows up in UNITS instead, as `rom1(empty)` — there is a socket, and there is no chip in it. Those pages float to `FF`, as they do on the bench.

**UNITS is what you type at `MOUNT` and `CONNECT`**, which is why the designations are there and not merely the count. `*` marks the unit holding the console.

## RUN is the switch on the panel

**`RUN [addr]` is the only way to start the machine**, and `RUN <addr>` is EXAMINE + RUN — it loads the PC first, exactly as you would on the panel.

**GO was deleted for it (2026-07-12).** There was never a second thing for GO to be. A *headless* run — no terminal handover, ^C to stop — is not a mode the operator picks; it is simply what happens when **nothing holds the console**, and the machine already knows that. Whether your keys reach the guest is a fact about the backplane, not a question for you:

| the backplane | what RUN does |
|---|---|
| a unit holds the console | the guest gets the keyboard — every key, including ^C — and the machine runs at the CPU card's real clock |
| nothing holds the console | there is nothing to hand over, so it just runs, flat out |

Both stop on a breakpoint, on a HLT nothing can wake, and on ATTN, and both say which. That is why GO had nothing left to be.

### ATTN is the stop key. ^C is not.

**Ctrl-C belongs to the guest** — CP/M reads it, and a stop key the guest also wants is one that either breaks the guest or gets eaten by it. So the way out is **ATTN (^E)**, and it is the same key whatever is in the backplane: with a console, with no console, on a terminal, always.

The host intercepts it before the guest is ever offered the byte, so **the guest cannot disable it** — it is a key on the *front panel*, not on the terminal. And **ATTN does not stop the machine**: it takes the keyboard back, and a bare `RUN` resumes from exactly where you were.

```
altairsim> RUN F800
[console -- ^E returns to the monitor]

ALTMON 1.3
*
                                    ← you press ^E
[monitor -- the machine is still at F83C. RUN resumes]
```

**ATTN is tracked on console input and nowhere else.** A unit on a socket, a serial port or a loopback is *not* the console, and its data passes through untouched — `05` down a socket is a byte of somebody's protocol, and scanning a modem line for a key that only exists on the operator's terminal would be corrupting the data, not a feature.

## CONSOLE configures the console — it does not run the machine

```
altairsim> CONSOLE
console  (the host keyboard and screen)

  property         value            legal
  attn             0x5              1..31

  held by  sio0:a

altairsim> CONSOLE attn=1D          ← make it ^]
```

`CONSOLE k=v` sets, bare `CONSOLE` shows. (`SET CONSOLE` and `SHOW CONSOLE` are the same thing said the long way.) It used to *enter* console mode, and that was wrong twice over: a command that starts the CPU because you asked to look at a setting is a trap, and "start the machine" already has a name.

### Every property is settable

There is no "runtime vs config-time" column, and no property that `SET` will refuse. **You can only type at the prompt when the machine is stopped** — by ATTN, by a breakpoint, by a HLT, which is the panel's STOP switch — so there is no moment at which a `SET` could race a running CPU. And on real hardware the rule would be a fiction anyway: Patrick — *"when working on boards, they are often accessed on an extender card and changed while the power is on."*

There *was* such a gate. It never once fired, because nothing in the simulator ever set the flag it was conditioned on. A rule the code only pretends to enforce is worse than no rule at all.

### The endpoints

`CONNECT <id>:<unit> <endpoint>`. The **monitor** knows this grammar and no board is permitted to, which is why `CONNECT sio0:a serial:/dev/tty.usbserial-AL009KFH` needed **not one line of code in the 2SIO**.

| Endpoint | What it is |
|---|---|
| `console` | The host keyboard and screen. Exactly one unit may hold it. |
| `null` | A DB-25 with nothing behind it. Writes vanish; reads are quiet. Not an error — an unconnected 6850 works fine and talks to nobody. |
| `loopback` | TX jumpered to RX — **and RTS→CTS, DTR→DCD/DSR**, exactly like the loopback plug in the drawer. The one endpoint that can test modem control with no hardware. |
| `socket:2323` | **Listen.** One client at a time; the listener survives a disconnect, so the next telnet is the phone ringing again. **A client connecting *is* carrier appearing.** |
| `socket:host:port` | **Call out.** Non-blocking: a session still being established is a phone still ringing, and the card correctly sees no carrier yet. |
| `serial:/dev/tty…` | A **real serial port**, and the one place where the pins are the pins. The card programs its baud and frame; `SET sio0:a cts=wired` and the far end can genuinely stop your transmitter. |

A device that is not there does **not** silently become a `NullStream` — it is an error, and `serial:` lists the ports that *are* on the host, because a cable that enumerated under a different name is ten minutes of a person doubting the simulator.

### Which unit is the console?

**The one that is cabled to it** — the console is an endpoint like any other, so with three 2SIOs and six ports it is simply whichever unit you connected:

```
altairsim> CONNECT sio0:a console
altairsim> CONNECT sio1:b console
console: taken from sio0:a
```

**Exactly one unit may hold it, because there is exactly one keyboard.** Two boards reading it would each get half your keystrokes — invisible until you are debugging why every other character vanished. Interactively, connecting a second one **steals** it and says who from: you are moving the cable, and the last port you plug in is the one you meant.

**A config file that names two consoles is refused.** There is no "last" about a file — it is a typo, not a decision — so it fails the load and names both offenders.

### The keyboard is buffered by the host

Keys land in a buffer belonging to the *host*, and a card takes characters from it. That is what lets ATTN be watched whether or not anybody is reading — while the guest is busy computing, and even when there is no serial card in the machine at all. It is also what lets anything *type for you*: an injected byte and a human's are indistinguishable to the board, because at the level the board sees, there is no difference. MCP's `send`/`expect` will be built on exactly that.

It does not make the UART any less real: the 6850 still holds **one** character, still sets RDRF when it does, and still takes the next only when the guest has cleared the last. The buffer is the *line*, and a line is buffered and flow-controlled. It is a real keyboard buffer, so it is finite — type past the end and the keys are dropped, and the drop is counted rather than silently swallowed.

And because looking at one byte is exactly when you need to know it *is* a byte:

```
altairsim> EX 8000
8000  FF  .  11111111   (nobody drives this -- the bus floated it)
```

## Commands that do not exist yet still resolve

TRACE, SNAPSHOT, RECORD, HISTORY and the rest are all in the table, and typing `T` today prints:

```
altairsim> T
TRACE: not implemented yet -- waiting on the debugger.
```

**This is the point, not an oversight.** If only the built commands were listed, `S` would mean SHOW today and silently start meaning STEP the day the CPU lands — and someone's fingers would keep typing `S` and get something else. Abbreviations are a contract with muscle memory, so the contract is fixed now, before anyone has any muscle memory to break.

## DUMP: a page at a time, and the columns never move

**A bare address dumps to the end of its page.** `D 100` is not a request to see one byte — nobody has ever wanted that — it is *"show me what's at 0100"*, and the answer is a page.

**`DUMP` with no argument at all** continues from where the last dump stopped. Type `D`, `D`, `D` and you page through memory, which is how you actually read it: you rarely know the address of the thing you're looking for, only that it's somewhere after the thing you just saw.

**Everything stays page-aligned — rows as well as columns.** `D 0001` opens on the `0000` line with the `0000` column left *blank*, so the byte at `0001` sits under the `01` heading; and it **stops at `00FF`**, not 256 bytes later:

```
altairsim> D 0001
0000     42 43 00 00 00 00 00  00 00 00 00 00 00 00 00   BC.............
0010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................
...
00F0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................

altairsim> D
0100  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................
```

The whole reason to print a hex address on every line is that the *column position* tells you the low nibble without counting — so if the columns shifted with the start address, every byte would be off by one and you wouldn't notice until it mattered. The same argument decides the end: counting out exactly 256 bytes from `0001` would dangle a one-byte line at `0100` and throw every subsequent `D` off the page grid. **Stopping on the boundary means the last line is always full and the next `D` opens cleanly on `0100`** — one accidental keystroke on the start address doesn't misalign the rest of the session.

**An explicit range means exactly what it says.** `D 100-10F` and `D 100/20` don't expand; only the bare address form does. That distinction is deliberate: `range` is shared with `FILL`, `MOVE`, `SEARCH` and `SAVE`, and a bare address quietly meaning 256 bytes *there* would be a footgun — `FILL 100 5A` must fill one byte.

Giving DUMP a range or an address moves the resume mark; nothing else does.

## IN and OUT run *real* bus cycles

```
altairsim> I 10
port 10 -> FF   (nobody answered -- the bus floated it)

altairsim> O 10 41
port 10 <- 41   (nobody decodes this port -- the byte is gone)
```

These are the same `ioRead`/`ioWrite` the CPU will run once it exists, through the same decode — so **they have real side effects**. An `IN` from a UART's data port *consumes* the byte, and the guest will never see it. That is not a wart to be papered over; poking a live port is the oldest way there is to find out whether a card is alive.

If you want to look without touching, that is what **`WHO IO <port>`** is for — it reports who *would* answer without running a cycle.

Both commands report **whether anybody actually answered**, which is the whole reason the bus/board boundary exists: `FF` from a board and `FF` from an empty slot are the same byte and completely different faults.

## Numbers: on the wire → hex, never on the wire → decimal

**Settled 2026-07-11 by Patrick** (DESIGN.md §10.0.1). The base belongs to the **operand**, not to the command line.

| | |
|---|---|
| **HEX** — the machine sees it | addresses, ports, data bytes |
| **DECIMAL** — only you see it | counts, widths, sizes, baud rates, unit numbers |

```
D 100            dump from 0100h
D 100/20         0100h..011Fh    (LEN is part of the address expression: hex)
DE 100 C3 00 F8  bytes are hex
I 10             IN from port 10h
D 0 WIDTH=10     ten bytes per line -- a width is a count, so it is DECIMAL
SET sio0 baud=9600                nine thousand six hundred
```

A single global base was never really available — `baud=9600` cannot mean 38400 — so the rule had to bend somewhere. It bends where it means something. The alternative ("everything in a command is hex") buys one sentence of simplicity and pays for it with `STEP 20` stepping 32 times, silently, forever.

**Overrides work everywhere, both directions**, because a rule you can't type your way out of is a trap: `0x20`, `$20` and `20h` force hex; `#32` forces decimal; `0b1010` is binary; `1_000` is just spacing.

**A `K`/`M` suffix is always decimal** — `10K` is 10,240, never 16K, which is why nobody has ever had to ask. So `0x10K` is a contradiction and is **rejected**, not guessed at:

```
altairsim> REGION ADD mem0 type=ram at=8000 size=0x10K
mem0: a K/M suffix is always decimal -- drop the hex marker: '0x10K'
```

Note that `at=F000` needs no `0x` — it's an address, so it's already hex.

## The backspace problem, and why there is a line editor

`^H` was appearing in the input line. The cause is that **a terminal has exactly one erase character**: the tty driver's `VERASE` is a single byte, and whether your backspace key sends BS (`0x08`) or DEL (`0x7F`) depends on the emulator, the OS and `$TERM`. When the two disagree, the byte you send is not the byte the driver erases with — so it is just a control character, and the driver puts it in the line, where it prints as `^H`.

**There is no VERASE setting that fixes this**, because there is no single right answer. So `src/cli/lineedit.cpp` takes the terminal out of canonical mode and edits the line itself, treating **both `0x08` and `0x7F` as backspace**. Then it does not matter which one your terminal chose, and it does not matter what OS you are on.

It also brings history (↑/↓), cursor movement (←/→, Ctrl-A/E), Ctrl-U, Ctrl-W, and Ctrl-C to abandon a line. Anything else that arrives as a stray control byte is **dropped, not inserted** — which is how `^H` got into the line to begin with.

A pipe, a script, or `--mcp` is not a terminal, and takes a plain `getline` path that never touches terminal state.

**Still to come:** tab completion, driven by `Board::properties()` (DESIGN.md §10.4) — the same reflection layer that already backs SET, SHOW, the TOML loader and the MCP schemas.
