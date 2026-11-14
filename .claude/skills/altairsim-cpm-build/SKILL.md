---
name: altairsim-cpm-build
description: How to build a CP/M program inside an altairsim machine — assemble 8080 source with Digital Research `ASM` and `LOAD`, or with Microsoft `M80` and `L80`, run it, and bring the `.COM`, `.HEX` and `.PRN` back to the host. Use this whenever a task assembles, builds or rebuilds a CP/M program or an 8080 `.ASM` or `.MAC` file, when `ASM` prints an error letter or the program has a wrong byte, and before you look for a cross-assembler on the host.
---

# Build a CP/M program in the machine

This skill needs the `altairsim` skill, which tells you how to boot a machine and type at it.
Each step and rule here was replayed on `examples/cpm/cpm22-buffered.toml`. The disk of that
machine has `ASM`, `LOAD`, `M80`, `L80`, `DDT` and the host bridge utilities.

Build in the machine. The assembler that the program was written for is on the disk, and no
host tool is necessary.

Two other skills hold rules that this one uses:

- `altairsim-cpm-text`: the source file must have CR/LF line ends.
- `altairsim-hostbridge`: `R` and `W`, which move the files.

## The steps, with `ASM` and `LOAD`

Start `altairsim` in the folder that has the source, so that `R` finds it.

```
run {from: 65280, until: "A>"}                              # boot CP/M
run {input: "R FOO.ASM\r", until: "\nA>"}                   # host -> CP/M
run {input: "ASM FOO\r", until: "\nA>", timeout_ms: 120000} # makes FOO.HEX and FOO.PRN
run {input: "LOAD FOO\r", until: "\nA>"}                    # makes FOO.COM
run {input: "FOO\r", until: "\nA>"}                         # run it
run {input: "W FOO.COM\r", until: "\nA>"}                   # CP/M -> host, binary
run {input: "W FOO.PRN\r", until: "\nA>"}                   # the listing, as text
```

Use the prompt that your BIOS prints: on the 8 MB Altair hard disk it is `A0>`, so use
`until: "\nA0>"`. To test against a real serial device after the build, set `clock_hz` on the
running machine; the recipe is in "Driving a real serial device" in `DRIVING-WITH-AI.md`.

A correct assembly prints the next free address, a `USE FACTOR` line and `END OF ASSEMBLY`,
and no other line:

```
CP/M ASSEMBLER - VER 2.0
0112
000H USE FACTOR
END OF ASSEMBLY
```

`LOAD` prints `FIRST ADDRESS 0100` for a program that starts at `ORG 100H`.

## Read what `ASM` prints

**`ASM` shows an error as one letter before the source line. It still writes the `.HEX`
file.** A build that has an error letter makes a program with a wrong byte in it. Read the
output of each assembly. If a line starts with a letter, correct the source.

```
S               MY_VAL  EQU     42H
E0100 3E00              MVI     A,MY_VAL
```

The rules of `ASM` that cause these errors:

- **A symbol cannot have an underscore.** `MY_VAL EQU 42H`
  gets an `S` error. Each line that uses the symbol gets an `E` error and assembles `00`.
- **An instruction name cannot be a symbol.** `JMP EQU 0C3H` gets an `E` error, and `ASM`
  assembles the line as a `JMP` instruction.
- **Upper case and lower case are the same.** A label `fujiRd` and `FUJIRD EQU 52H` are one
  name. The `EQU` gets a `P` error, the label gets a `V` error, and `MVI A,FUJIRD` assembles
  `00`.
- **The line after the banner is `0000`, or correct lines get errors.** The source file has
  LF line ends. See the `altairsim-cpm-text` skill.

When a constant is important, read the `.PRN` file and check the bytes. A `00` where a value
must be shows that a symbol was not defined.

## Disk space

The disk in `examples/cpm` has 6K free, and each file takes 2K or more. One build makes
three files beside the source. When the disk is full, `ASM` prints `OUTPUT FILE WRITE ERROR`,
and the `.HEX` file is empty or not complete.

- Look at the space: `run {input: "STAT\r", until: "\nA>"}`.
- Erase the files of a build after you copy them out: `ERA FOO.HEX`, `ERA FOO.PRN`.
- 6K does not hold most builds. Mount a copy of the disk, and erase a file that the build does
  not need, such as `ERA LADDER.COM`. Do not change the disk in `examples/cpm`.

## The steps, with `M80` and `L80`

`M80` makes a relocatable `.REL` file, and `L80` makes the `.COM` file. The source has no
`ORG 100H`. `L80` puts the program at `100H`.

```
run {input: "R FOO.MAC\r", until: "\nA>"}
run {input: "M80 =FOO\r", until: "\nA>", timeout_ms: 120000}     # makes FOO.REL
run {input: "L80 FOO,FOO/N/E\r", until: "\nA>", timeout_ms: 120000}  # makes FOO.COM
```

`M80` prints `No Fatal error(s)` for a correct assembly. The source starts with `.8080` and
ends with `END START`, where `START` is the first instruction.
