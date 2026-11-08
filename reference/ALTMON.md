# ALTMON 1.3 — 1K ROM Monitor for the Altair 8800

Source: [`roms/ALTMON/ALTMON.ASM`](../roms/ALTMON/ALTMON.ASM) and its listing `ALTMON.PRN`
(Mike Douglas, version 1.3, 02/13/2024).

ALTMON is a 1K monitor in ROM for an Altair with an 88-2SIO. Mike Douglas based it on the
Vector Graphic 2.0C monitor. It lets you examine and change memory, read and write I/O ports,
load an Intel HEX file and run a program. This file is written from the source listing, not
from a manual. ALTMON has no manual. **Emulated** as `builtin:altmon` in the `altmon` machine.
All the sample output in this file comes from a run of that machine.

## 1. Quick reference

| Item | Value |
|---|---|
| ROM | `F800`–`FBF4` (1013 bytes), `org 0F800h` |
| Cold entry (`monit`) | `F800`. Sets up both 2SIO channels, then prints the banner. |
| Command loop (`start`) | `F81F`. Resets SP to `C000`, prints the prompt. A jump here keeps the 2SIO setup. |
| Console | 88-2SIO channel A: status `10h`, data `11h`. TDRE = bit 1, RDRF = bit 0. |
| Second port | 88-2SIO channel B: status `12h`, data `13h`. Only `H`/`L` read it. |
| 2SIO setup | Control byte `03` (master reset), then `11h` (÷16, 8 data bits, 2 stop bits, no parity), on both channels. |
| Banner | `CR LF LF ALTMON 1.3 ` |
| Prompt | `CR LF *` |
| Stack | SP = `C000` (equate `SPTR`). Needs RAM in page `BF00`–`BFFF`. |
| RAM it writes | Stack page `BF00`–`BFFF`. The `IN`/`OUT` stub at `BFCE`–`BFD0`. The `H`/`L` port number at `BFE0`. |
| Disk boot (`B`) | Jumps to `FF00` (the DBL PROM). |
| Other ROM (`R`) | Jumps to `FC00`. |
| Turnkey option | Equate `TKYDSBL`. When non-zero, ALTMON reads port `FF` at start to switch off a Turnkey board's PROMs. The build in this repository has `TKYDSBL = 0`. |
| Output | Bit 7 of each character is set to 0 before it goes to the console. |

## 2. How to type a command

ALTMON reads keys one at a time. It has no line editor and no backspace.

- Type one letter. ALTMON prints the command's full name and a space at once. For example,
  `O` prints `OUT `. The letter itself is not echoed.
- Lower case is accepted, for the letter and for hex digits.
- Type each value with an exact number of hex digits:

  | Value | Digits |
  |---|---|
  | An address (`SSSS`, `FFFF`, `DDDD`) | 4 |
  | A byte or a port (`DD`, `PP`) | 2 |
  | The `H`/`L` port number (`P`) | 1 |

- Do not type a separator. After the last digit of a value, ALTMON prints a space itself.
  A command starts when its last digit is typed. No `CR` is needed.
- A space, `CR`, `ESC`, `Ctrl-C`, or any other character that is not a hex digit cancels the
  command. ALTMON goes back to the `*` prompt.
- A range includes its end address.
- Letters `B` to `T` are commands. ALTMON ignores any other key at the prompt and prints a new
  prompt.

> **Trap: a space cancels.** The command summary in the source prints `D SSSS FFFF` with
> spaces, but the spaces are only for the reader. Type `D01000117`. If you type `D 0100`, the
> space cancels the `D`, and each later digit gives a new `*` prompt:
>
> ```
> *DUMP  
> *
> *
> *
> ```

While a command runs, ALTMON checks the console between lines or between bytes:

- `ESC` or `Ctrl-C` stops the command and goes back to the prompt.
- A space pauses the output. A second space continues it.
- Any other key is discarded.

`ESC` is never echoed, because ALTMON does not want to send an escape sequence to a smart
terminal.

## 3. Commands

| Keys | Name printed | What it does |
|---|---|---|
| `B` | `BOOT` | Jumps to `FF00`, the Altair disk boot loader PROM. |
| `C SSSS FFFF DDDD` | `COMP` | Compares `SSSS`–`FFFF` with the block at `DDDD`. Prints one line for each byte that is different. |
| `D SSSS FFFF` | `DUMP` | Dumps memory in hex and ASCII. |
| `E SSSS FFFF DDDD` | `EXCH` | Exchanges block `SSSS`–`FFFF` with the block at `DDDD`. |
| `F SSSS FFFF DD DD` | `FIND2` | Searches `SSSS`–`FFFF` for a sequence of two bytes. |
| `G SSSS` | `GOTO` | Runs the program at `SSSS`. |
| `H P` | `HEXLOAD` | Loads an Intel HEX file from 2SIO channel `P`. |
| `I PP` | `IN` | Reads port `PP` and prints the byte. |
| `J SSSS` | `GOTO` | The same as `G`. |
| `K SSSS FFFF DD` | `FILL` | Writes `DD` to each byte of `SSSS`–`FFFF`. |
| `L P` | `HEXLOAD` | The same as `H`. It does not run the program. |
| `M SSSS FFFF DDDD` | `MOVE` | Copies `SSSS`–`FFFF` to `DDDD`. |
| `N` | `RAMTOP` | Finds the first address, from `0000` up, that is not RAM. |
| `O PP DD` | `OUT` | Writes `DD` to port `PP`. |
| `P SSSS` | `PGM` | Examines and changes memory one byte at a time, from `SSSS`. |
| `Q SSSS FFFF` | `CSUM` | Prints the 8-bit sum of the bytes in `SSSS`–`FFFF`. |
| `R` | (nothing) | Jumps to `FC00`. |
| `S SSSS FFFF DD` | `FIND1` | Searches `SSSS`–`FFFF` for one byte. |
| `T SSSS FFFF` | `TEST` | Tests memory `SSSS`–`FFFF` until you stop it. Writes over the data. |

### 3.1 The output of each command

**`D` (dump).** Each line has the address, 16 bytes in hex, two spaces, and 16 characters.
A byte below `20h`, or `7Fh` and above, shows as `.`. ALTMON always prints whole lines of 16
bytes, so the last line can go past the end address. The lines start at `SSSS`, not at a 16-byte
boundary.

```
*DUMP 0100 0117 
0100 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41  AAAAAAAAAAAAAAAA
0110 75 A8 F3 6B 9C 9C C3 53 60 3D 43 26 20 6B 2F 1C  u..k...S`=C& k/.
```

**`P` (program memory).** ALTMON prints the byte at the address and a dash. Then type one of
these:

| Key | Result |
|---|---|
| Two hex digits | Writes the new byte, then goes to the next address. |
| Space | Keeps the byte, then goes to the next address. |
| `CR` | Starts a new line. The address does not change and the byte is not printed again. |
| `ESC`, `Ctrl-C`, or any other key | Ends `P`. |

The addresses are not printed. Count the bytes from `SSSS`. In this example, the user types
`P0100`, `3E`, a space, `CR`, `C9`, and `ESC`:

```
*PGM 0100 
41-3E 41- 41-
C9 41-
*
```

The result is `3E` at `0100`, `0101` without a change, and `C9` at `0102`. You can type all
the digits without a stop. `P0400081004` writes `08 10 04` at `0400`–`0402`.

**`C`, `S` and `F`.** Each prints one line for each result, with an address and two bytes.

| Command | Address | First byte | Second byte |
|---|---|---|---|
| `C` | The address in the first block | The byte in the first block | The byte in the second block |
| `S` | The address of the match | The byte searched for | The byte after the match |
| `F` | The address of the match | The first byte of the match | The second byte of the match |

```
*COMP 0100 0103 0108 
0100 3E 41
0102 C9 41
*FIND1 0100 0110 C9 
0102 C9 41
```

**`N` (RAM top).** ALTMON reads each byte from `0000` up, writes its complement and reads it
back. ALTMON restores each byte. At the first address that does not change, it prints the
address, the byte there, and the complement that it tried to write. In the `altmon` machine
the RAM ends at `F7FF`:

```
*RAMTOP 
F800 3E C1
```

**`Q` (checksum).** The 8-bit sum, modulo 256, of the bytes in the range:

```
*CSUM F800 FBF4 B8
```

**`I` and `O` (ports).** `I` prints the byte on the same line. `O` prints nothing more.

```
*OUT 58 30 
*IN 59 00
```

**`T` (memory test).** ALTMON writes a pseudo-random pattern to the range, then reads it
back. It prints `.` after each pass, and does not stop by itself. On an error it prints the
address, the byte it wrote, and the byte it read. The test skips the stack page `BF00`–`BFFF`.
Press `ESC` to stop it.

**`M` (move).** `M` copies one byte at a time from the low address up. A move to a higher
address that overlaps the source writes over the source before it is read.

**`R`.** `R` prints nothing. It jumps to `FC00` at once. If nothing is at `FC00`, the
processor runs whatever bytes it reads there. Only a reset brings the prompt back.

## 4. Running a program

`G SSSS` (or `J SSSS`) jumps to `SSSS` with these registers:

- SP = `BFFE`. The word at `BFFE` is `F81F`, the address of the command loop.
- HL = `SSSS`.

Because the return address is on the stack, a program that ends with `RET` goes back to the
`*` prompt. The program must leave SP as it found it. In this example, `0100` holds `3E 41 C9`
(`MVI A,41h` then `RET`):

```
*GOTO 0100 
*
```

## 5. Reading and writing ports by hand

`I` and `O` build a 3-byte stub in RAM at `BFCE`: `IN pp` or `OUT pp`, then `RET`. ALTMON
then calls the stub. Each `I` or `O` is one bus cycle on the port, with nothing before or
after it. You can drive a board one register at a time this way.

For example, an AMD 9511A on a CompuPro System Support 1 at base `50h` has its data port at
`58h` and its command port at `59h` (`CompuPro System Support 1.md`). The 9511A takes each
operand least-significant byte first, and gives the result most-significant byte first
(`AMD 9511A.md`). This sequence adds `3000h` and `4000h` with the SADD command (`6Ch`):

1. Type `O5800`. Then type `O5830`. This pushes `3000h`.
2. Type `O5800`. Then type `O5840`. This pushes `4000h`.
3. Type `O596C` to start SADD.
4. Type `I59` to read the status. Do the next step only when bit 7 (BUSY) is 0.
5. Type `I58`. Then type `I58` again. This pops the result, high byte first.

```
*OUT 58 00 
*OUT 58 30 
*OUT 58 00 
*OUT 58 40 
*OUT 59 6C 
*IN 59 00
*IN 58 70
*IN 58 00
```

The result is `7000h`.

## 6. Loading an Intel HEX file

`H P` (or `L P`) reads an Intel HEX file from 2SIO channel `P`. `P` = `0` reads channel A,
the console. Any other digit reads channel B. To load a file:

1. Type `H0`. ALTMON prints `HEXLOAD 0 `.
2. Send the file to the console from your terminal program, as plain text.
3. Wait for the `*` prompt.

ALTMON prints the load address of each record on a new line, so you can see the load
progress:

```
*HEXLOAD 0 
0200
0210
0220
0230

*
```

How the loader reads a file:

- It skips each character until it finds a `:`. Line ends are not needed.
- A record with a byte count of `00` ends the load. ALTMON does not read the record type.
  A data record of any type goes into memory.
- It does not run the program. The address in the end record is not used. Use `G` to run the
  program.
- On a checksum error it prints ` ERR` and stops the load. The bytes of the bad record are
  already in memory.
- After the end record or an error, ALTMON reads and discards console input until it gets
  no character for about 0.25 seconds. The loop is timed for a 2 MHz 8080. This stops the
  rest of the file from being read as commands. ALTMON discards input on channel A only.
- While it loads from channel B, `ESC` or `Ctrl-C` on the console stops the load.

```
*HEXLOAD 0 
0200 ERR 
*
```

> **altairsim note.** In `--mcp`, the 0.25-second wait after a load is a loop that polls
> the console, so `run` stops it as idle. Each `run` moves it on a little. Before a load,
> send `monitor {command: "SET cpu0 idle=false"}`, and give the `run` an `until` of `*`.

## 7. Example: test a math chip with a short program

This is the method of issue #908: a small program sends operands from a table to the 9511A,
starts one command, counts the status polls until BUSY is 0, and stores the results. The
program is at `0200`, the table at `0400`, and the results at `0420`.

| Address | Contents |
|---|---|
| `0400` | Number of operand bytes to push |
| `0401` | The 9511A command byte |
| `0402` | Number of result bytes to pop |
| `0403`… | The operand bytes, in push order (least-significant byte first) |
| `0420` | Status after the command |
| `0421`–`0422` | Poll count, low byte first |
| `0423`… | The popped result, most-significant byte first |

The program, for a System Support 1 at base `50h`, as an Intel HEX file:

```
:1002000021000446234E23562378B7CA16027ED314
:10021000582305C3090279D35901000003DB59B7FC
:10022000FA1C022120047723712370237AB7C8DBDC
:0702300058772315C32C02CF
:00000001FF
```

`Q02000236` gives `A6` when the program loaded correctly. For a different board base, change
the ports `58` and `59` at `0210`, `0218`, `021E` and `0230`, then fix each checksum.

To run one case, FADD of 1.0 (`01800000`) and 3/4 LSB (`69C00000`):

1. Type `H0` and send the HEX file. Wait for the `*` prompt.
2. Type `P0400081004000080010000C069`, then press `ESC`.
3. Type `G0200`.
4. Type `D04200426`.

```
*GOTO 0200 
*DUMP 0420 0426 
0420 00 B6 03 01 80 00 01 8C A3 37 A9 0C A3 E4 FC D9  .........7......
```

The status is `00` and the result is `01800001`. That is altairsim's result for #908 item 1.
The real chip in #908 gave `01800000`. The poll count (`03B6`) is not a time on its own: it
depends on the processor clock. In altairsim, set `clock_hz=2000000` on the CPU board to
compare poll counts with a 2 MHz machine.
