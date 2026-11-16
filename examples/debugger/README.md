# Learning the symbolic debugger

A 54-byte program, its symbols, and a walk through the monitor's debugger — load it,
disassemble it by name, single-step it, break on a label, and run it until it prints.

```
cd examples/debugger
altairsim debugger.toml
```

You land at the `altairsim>` prompt with an empty machine: an 8080, 32K of RAM, and an
88-2SIO for the console. Nothing is loaded yet — that is the first thing you will do, and
the point of the exercise is that you do it all by hand.

The program is `HELLO.ASM`: it prints `HELLO, WORLD` through the 2SIO and halts. It is small
on purpose, so that every label, every `EQU`, and every kind of operand is right there to be
looked at.

## 1 — Load the symbols, then the program

Two separate files, because they are two separate things. `HELLO.PRN` is the assembler's
**listing** — it is where the *names* live. `HELLO.HEX` is the assembled **bytes**. Loading
the symbols does not put a single byte in memory; loading the hex does not teach the monitor
a single name.

```
altairsim> SYMBOLS LOAD HELLO.PRN
14 symbol(s) from HELLO.PRN
altairsim> LOAD HELLO.HEX
loaded 54 bytes (1 page) from HELLO.HEX (0100-0135)
```

## 2 — Disassemble it, and read it by name

`DISASM` takes a range, and a symbol is accepted anywhere an address is — so name the range:

```
altairsim> DISASM START-DONE
START:
0100  31 46 01  LXI SP,STACK
0103  3E 03     MVI A,03
0105  D3 10     OUT 10
0107  3E 15     MVI A,15
0109  D3 10     OUT 10
010B  21 27 01  LXI H,MSG
LOOP:
010E  7E        MOV A,M
010F  B7        ORA A
0110  CA 1A 01  JZ DONE
0113  CD 1B 01  CALL PUTC
0116  23        INX H
0117  C3 0E 01  JMP LOOP
DONE:
011A  76        HLT
```

The four instructions after `LXI SP,STACK` set up the 2SIO. A 2SIO channel is held in reset
when the machine is switched on. A program that runs with no ROM before it must write a master
reset (`03`) and then the word format (`15`) to the control port. Until it does, the channel
sends nothing.

Three things are happening on top of the plain hex, and each has a rule worth knowing.

**A label heads its own line.** `START:`, `LOOP:`, `DONE:` — the way an assembler listing
prints them, so a jump destination announces itself where it lands.

**An operand reads as the name it points at.** `JZ DONE`, `CALL PUTC`, `JMP LOOP`,
`LXI H,MSG` — every 16-bit address the program references is shown as a symbol.

**`LXI SP,STACK` is annotated even though `STACK` is an `EQU`, not a label.** This is the
same reason `CALL 0005` reads as `CALL BDOS` in CP/M: a program *label* heads a line, but an
*operand* is a value the instruction points at, and there an `EQU` that is really an address
is exactly what you want to see. (A real label still wins when both share a value.)

Now disassemble the subroutine, and watch what does **not** get named:

```
altairsim> DISASM PUTC 7
PUTC:
011B  F5        PUSH PSW
PWAIT:
011C  DB 10     IN 10
011E  E6 02     ANI 02
0120  CA 1C 01  JZ PWAIT
0123  F1        POP PSW
0124  D3 11     OUT 11
0126  C9        RET
```

`IN 10` is **not** shown as `IN TTYS`, even though `TTYS EQU 10H` is loaded (`SHOW SYMBOLS
TTYS` proves it). That is deliberate: a port is a **byte**, not an address, and only a 16-bit
operand is treated as an address. The count you meant is not the location you didn't — so
`ANI 02` stays `02`, not `TXRDY`, and `MVI A,03` stays `03`, not `MRESET`, and `JZ PWAIT` right below it still reads as the label,
because *that* operand is an address.

## 3 — Single-step, and watch the registers

`EXAMINE` loads the program counter — it is the front-panel switch that jams an address into
the PC — so `EXAMINE START` puts you at `0100`. Then `STEP` runs one instruction and shows
the machine, with the next instruction disassembled symbolically:

```
altairsim> EXAMINE START
0100  31  1  00110001
C0Z0M0E0I0 A=00 BC=0000 DE=0000 HL=0000 SP=0000 IE=0 PC=0100  LXI SP,STACK
altairsim> STEP 7
C0Z0M0E0I0 A=00 BC=0000 DE=0000 HL=0000 SP=0146 IE=0 PC=0103  MVI A,03
C0Z0M0E0I0 A=03 BC=0000 DE=0000 HL=0000 SP=0146 IE=0 PC=0105  OUT 10
C0Z0M0E0I0 A=03 BC=0000 DE=0000 HL=0000 SP=0146 IE=0 PC=0107  MVI A,15
C0Z0M0E0I0 A=15 BC=0000 DE=0000 HL=0000 SP=0146 IE=0 PC=0109  OUT 10
C0Z0M0E0I0 A=15 BC=0000 DE=0000 HL=0000 SP=0146 IE=0 PC=010B  LXI H,MSG
C0Z0M0E0I0 A=15 BC=0000 DE=0000 HL=0127 SP=0146 IE=0 PC=010E  MOV A,M
C0Z0M0E0I0 A=48 BC=0000 DE=0000 HL=0127 SP=0146 IE=0 PC=010F  ORA A
```

Read down the `PC=` column: the stack pointer `SP=` fills in after `LXI SP,STACK`. `A=03` and
`A=15` are the two control words that go to port `10`. `HL=` becomes `0127` (that is `MSG`)
after `LXI H,MSG`, and `A=48` after `MOV A,M` — `48` is `'H'`, the
first byte of the string. `STEP` with no count does one instruction; `NEXT` is the same but
runs a `CALL` to its return instead of descending into it.

## 4 — Break on a name, and run

A breakpoint takes a symbol, too. Set one on the subroutine and run:

```
altairsim> EXAMINE START
0100  31  1  00110001
C0Z0M0E0I0 A=48 BC=0000 DE=0000 HL=0127 SP=0146 IE=0 PC=0100  LXI SP,STACK
altairsim> BREAK PUTC
breakpoint 1: pc     011B
altairsim> RUN
[console -- ^E returns to the monitor]

breakpoint 1 (pc     011B) -- stopped at 011B
10 instructions, 92 T-states.
C0Z0M0E1I0 A=48 BC=0000 DE=0000 HL=0127 SP=0144 IE=0 PC=011B  PUSH PSW
```

It stopped the first time the program reached `PUTC`, with `A=48` — the `'H'` it was about to
send. Clear it and set one on `DONE` instead, and this time let it finish:

```
altairsim> NOBREAK 1
breakpoint 1 cleared.
altairsim> EXAMINE START
0100  31  1  00110001
C0Z0M0E1I0 A=48 BC=0000 DE=0000 HL=0127 SP=0144 IE=0 PC=0100  LXI SP,STACK
altairsim> BREAK DONE
breakpoint 1: pc     011A
altairsim> RUN
[console -- ^E returns to the monitor]
HELLO, WORLD

breakpoint 1 (pc     011A) -- stopped at 011A
822 instructions, 7405 T-states.
C0Z1M0E1I0 A=00 BC=0000 DE=0000 HL=0135 SP=0146 IE=0 PC=011A  HLT
```

There it is: the program ran, printed `HELLO, WORLD` through the 2SIO, and stopped exactly at
`DONE` — the byte *before* it would have executed the `HLT`. Type `RUN` once more with no
breakpoint set and it runs the `HLT` and stands there halted, which is the machine waiting for
you.

## 5 — A few more you now have

| Command | Try |
|---|---|
| `DUMP MSG` | the string, dumped by name — `DUMP` resolves a symbol but does not annotate the bytes, because there is no instruction there to say which are addresses |
| `SHOW SYMBOLS` | all fourteen; `SHOW SYMBOLS T*` filters with a glob |
| `BREAK PUTC IF A==2C` | a conditional breakpoint — stop at `PUTC` only when it is about to send `2C`, the `,`; the machine prints `HELLO` and stops there |
| `TRACE ON` / `HISTORY` | log every cycle as it runs, or show the run-up to a stop |

`SYMBOLS CLEAR` forgets the names again; disassemble after that (`DISASM 100` — with the names
gone you are back to typing the address) and every operand is plain hex once more, which is a
good way to see exactly how much the symbols were buying you.

## The files

| File | What it is |
|---|---|
| `debugger.toml` | The machine: an 8080, 32K of RAM, and a 2SIO console. No ROM, no disk — nothing you do not need to single-step a program. |
| `HELLO.ASM` | The source, for reading. Labels, `EQU`s, a loop, and a subroutine. |
| `HELLO.PRN` | The assembler **listing** — the file `SYMBOLS LOAD` reads. Labels feed the name→line and name→operand annotation; `EQU`s feed operands only. |
| `HELLO.HEX` | The assembled **bytes**, in Intel HEX — the file `LOAD` reads. |

`HELLO.ASM` and `HELLO.PRN` are the same program seen two ways: the listing is the source with
the assembler's address and object-code columns added on the left. Edit the source and
reassemble, and both the bytes and the names move together.
