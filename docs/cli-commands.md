# Monitor commands and their abbreviations

**Settled 2026-07-11 by Patrick.** The table in `src/cli/commands.cpp` is the only copy of this; HELP and everything below are generated from it.

## The rule

**The table is in priority order, and the first command whose name starts with what you typed wins.** That is the whole algorithm — there is no minimum-abbreviation column, no priority number, and nothing that treats a one-letter word specially. One letter is just a short prefix.

`D` dumps because DUMP is listed above DEPOSIT, DISASM and DISCONNECT. It follows, without anyone deciding it, that DEPOSIT needs `DE` and DISASM needs `DI`. Reorder the table and every abbreviation in the monitor re-derives itself, including the ones printed by HELP.

This is why **UNMOUNT is not called DISMOUNT**: it's the plainer word, it takes `U` (which nothing else wanted), and removing it from the D-cluster is what let DISASM fall from `DISA` to `DI`. Nobody worked that out — the table did.

**The one invariant:** no command name may be a strict prefix of another. If one were, its full, correctly-spelled name would resolve to whichever came first and there would be no way left to type the other. Renaming REGS to REG would break exactly this; `tests/test_cli.cpp` fails if anyone tries.

## The ranking

The nine that own their prefix, in Patrick's words: **DUMP, STEP, RESET, HISTORY, MOUNT, BREAK, EDIT, CONFIG, GO.**

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
| `G` | GO | |
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
| `BO` | BOARD | |
| `REG` | REGS | beats REGION |
| `REGI` | REGION | |
| `DI` | DISASM | |
| `U` | UNMOUNT | not DISMOUNT — see above |
| `DISC` | DISCONNECT | |
| `CONS` | CONSOLE | `CONSOLE [addr]` — the guest takes the keyboard; **ATTN (^E)** gives it back |
| `CONN` | CONNECT | `console \| null \| loopback` today |
| `P` | POWER | |
| `T` | TRACE | *waiting on the debugger* |
| `STO` | STOP | *waiting on a monitor that runs alongside the machine — ATTN leaves CONSOLE today* |
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

  D[UMP]            S[TEP]*           R[ESET]           H[ISTORY]*
  M[OUNT]           B[REAK]*          E[DIT]*           C[ONFIG]
  G[O]*             SE[T]             SH[OW]            DE[POSIT]
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
