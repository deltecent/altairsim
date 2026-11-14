---
name: altairsim-hostbridge
description: Rules for the altairsim host bridge utilities `R`, `W` and `HDIR`, which copy files between the host folder and a CP/M disk in a running altairsim machine, and `SIMCLK`, which sets the processor's crystal from the guest. Use this whenever a task moves a file into or out of a CP/M guest — a source file to assemble, a `.COM` or `.HEX` to bring back, a `.BAS` program — when `R` or `W` gives an error, when a `SUBMIT` file must change the clock speed, when a disk does not have the utilities, and before you use a host tool such as cpmtools on a disk image.
---

# The host bridge: `R`, `W`, `HDIR` and `SIMCLK`

This skill needs the `altairsim` skill, which tells you how to boot a machine and type at it.
Each rule here was replayed on `examples/cpm/cpm22-buffered.toml`.

The host bridge is a board (`hb0`) with four CP/M programs on the disk. It is the way to
move a file into or out of a guest. Do not use a host tool to write into a disk image. Such
tools do not know the disk formats of the Altair.

```
HDIR [pattern]                   list the host folder: size, date, name
R <hostfile> [cpmfile]           host -> CP/M
W <cpmfile> [hostfile] [B|T]     CP/M -> host
SIMCLK [hz]                      set or show the processor's crystal
```

```
run {input: "HDIR *.ASM\r", until: "\nA>"}
run {input: "R HELLO.ASM\r", until: "\nA>"}          # prints: R: HELLO.ASM -> HELLO.ASM
run {input: "W HELLO.COM\r", until: "\nA>"}          # binary, from the extension
run {input: "W HELLO.PRN\r", until: "\nA>"}          # text, from the extension
run {input: "W NOTES.DOC NOTES.DOC T\r", until: "\nA>"}   # text, because you said so
```

## The rules

1. **`W` chooses text or binary from the extension. Type `B` or `T` only to change that.**
   `B` writes every byte of every 128-byte record. `T` stops at the first Ctrl-Z (`1A`).
   With no mode, these extensions are text: `ASM` `MAC` `Z80` `HEX` `PRN` `LST` `SYM` `INC`
   `SUB` `TXT`. `W` then prints `(text, by extension)` on the transfer line. Each other
   extension is binary, `.COM`, `.BAS` and `.DOC` included. For a text file with another
   extension, type `T`, or it gets the remainder of its last record at its end. Never type `T` for a
   binary file: `T` cuts it short at its first `1A`.
2. **The guest sees one host folder.** That folder is the sandbox. `R` and `W` take a name
   in that folder or in a folder below it (`R SUB/DEEP.TXT`). The board refuses `..`, an
   absolute path and a drive letter.
3. **Find the sandbox with `SHOW PATHS`.** `monitor {command: "SHOW PATHS"}` prints the
   folder on the line `hb0 sandbox`. The default is the folder that `altairsim` was started
   from. To change it: `monitor {command: "SET hb0 hostdir=/full/path"}`.
4. **Do not type a host name in lower case and expect it to matter.** CP/M changes the
   command line to upper case before `R` sees it. The board looks for an exact name first,
   then ignores case. `R NOTES.TXT` finds `notes.txt`.
5. **Use `HDIR` when a name is not found.** `HDIR` prints the true host names.
6. **Wildcards work.** `R *.ASM` copies each file that matches.
7. **A CP/M name is 8.3.** A folder name does not come across: `R SUB/DEEP.TXT` writes
   `DEEP.TXT`. A second argument sets the CP/M name.

`R` does not change a byte. A text file must have CR/LF line ends before you send it. See
the `altairsim-cpm-text` skill.

## `SIMCLK`: the crystal, from the guest

`SIMCLK 0` runs the processor flat out. `SIMCLK 2000000` runs it at 2 MHz. `SIMCLK` with no
rate shows the rate. The rate is in Hz, as decimal digits only.

```
run {input: "SIMCLK 0\r", until: "\nA>"}           # prints: SIMCLK: flat out
run {input: "SIMCLK 2000000\r", until: "\nA>"}     # prints: SIMCLK: 2000000 Hz
```

`SIMCLK` sets `clock_hz` on the CPU board, the same as `SET cpu0 clock_hz=N`. Use it in a
`SUBMIT` file: `SIMCLK 0` before `ASM`, and `SIMCLK 2000000` after `LOAD`. At 2 MHz each
command loads from the floppy at the speed of a real one, about one second.

| Message | Cause |
|---|---|
| `SIMCLK: clock_hz must be 0..100000000` | The CPU board refused the rate. The rate did not change. |
| `SIMCLK: bad clock rate -- give the rate in Hz as decimal digits` | The rate was not digits, for example `2MHZ`. |
| `SIMCLK: unknown host bridge command` | The altairsim is older than `SIMCLK`. |

## What the errors mean

| Message | Cause |
|---|---|
| `R?` or `W?` | The utility is not on this disk. See below. |
| `R: NAME: no such file` | The file is not in the sandbox. Use `HDIR`, then `SHOW PATHS`. |
| `'..' is outside the host directory` | The name has a `..` in it. Move the file, or change `hostdir`. |
| `an absolute path is outside the host directory` | The name starts with `/`. Give a name in the sandbox. |
| `W: the host bridge is read-only` | `readonly` is on. `monitor {command: "SET hb0 readonly=off"}`. |

## A disk without the utilities

The disk in `examples/cpm` has `R.COM`, `W.COM`, `HDIR.COM` and `SIMCLK.COM`. A disk that you bring, or a
disk that you format, does not. The package has the four programs in `hostbridge/`, as
`.HEX` and as `.COM`. `LOAD.COM` must be on the disk.

1. Start a file from the console: `run {input: "PIP R.HEX=CON:\r", timeout_ms: 3000}`.
2. Queue the HEX file: `monitor {command: 'PASTE "hostbridge/R.HEX"'}`. `PASTE` finds a
   relative name in the folder of the machine file.
3. Let PIP read it: `run {timeout_ms: 60000}`. The call stops with `idle`, and the output
   ends with `:00010000FF`.
4. End the file with Ctrl-Z: `run {input: "\u001a", until: "A>"}`.
5. Make the program: `run {input: "LOAD R\r", until: "\nA>"}`. Check that the output has
   `FIRST ADDRESS 0100`.
6. Copy the others with `R`. Set `hostdir` to the `hostbridge` folder first, then
   `R W.COM`, `R HDIR.COM` and `R SIMCLK.COM`.

A machine that has no `hb0` board needs one. `board_add {type: "hostbridge", id: "hb0"}`
adds it at port B0.
