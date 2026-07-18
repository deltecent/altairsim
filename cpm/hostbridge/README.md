# The Host Bridge utilities

`R.COM`, `W.COM` and `HDIR.COM` — how a file gets into and out of a running CP/M guest.

```
HDIR [pattern]                  what is on the host
R  <hostfile> [cpmfile]         host -> CP/M      R *.ASM     R SRC/FOO.ASM
W  <cpmfile> [hostfile] [B|T]   CP/M -> host      W *.HEX     W FOO.TXT T
```

They talk to the `hostbridge` card (`docs/boards/hostbridge.md`), which is in the default
machine at port **0xB0** and is rooted at a sandboxed `hostdir` — by default, the directory
you ran `altairsim` from.

## What is in here

| | |
|---|---|
| `R.ASM` `W.ASM` `HDIR.ASM` | the sources. 8080, `ORG 100H`, BDOS only. |
| `HB.INC` | the master equates — ports, commands, status bits, error codes |
| `R.COM` `W.COM` `HDIR.COM` | **the programs, ready to run** |
| `R.HEX` `W.HEX` `HDIR.HEX` | what `ASM` produced, and what `LOAD` turned into the `.COM` |
| `R.PRN` `W.PRN` `HDIR.PRN` | the assembler listings |

**The `.COM`, `.HEX` and `.PRN` are the only build artifacts committed anywhere in this
repository**, and they are here for one reason: so that somebody who clones it has working
utilities without first pasting 28 KB of assembler into `PIP`. They are not built by CMake
and no host toolchain produces them.

## They were built inside the machine, and that is how you rebuild them

There is no cross-assembler. These are assembled by **CP/M's own `ASM.COM` and `LOAD.COM`**,
off the disk you are booting — and then `W` copies them straight back out to the host,
which is the same card this whole directory is about.

The source reaches the disk through the console, which is the one channel that exists
before any file-transfer utility does:

```
A> PIP R.ASM=CON:
   ...paste R.ASM...
   ^Z
A> ASM R                 -> R.HEX and R.PRN
A> LOAD R                -> R.COM
A> W R.COM               -> back out to the host, byte for byte (B is the default)
A> W R.HEX  R.HEX  T     -> text: T trims the ^Z padding CP/M added
A> W R.PRN  R.PRN  T
```

...with `hostdir` pointed at this directory. After the first one is built, `R.COM` itself is
how the other two sources get in, so nothing needs pasting twice.

**If you change a `.ASM`, rebuild and commit all three of its artifacts.** A checked-in
binary that nobody checks is a binary that rots — edit `R.ASM`, forget to rebuild, and
`R.COM` quietly goes on being the old program while every test still passes.

`tests/acceptance/hostbridge.cmake` enforces this, in two tests that check different halves
of the chain — and **only one of them runs by default**:

| test | proves | needs |
|------|--------|-------|
| `acceptance-hostbridge` | the committed `.HEX` still `LOAD`s to the committed `.COM`, and that `.COM` works | nothing — the disk is tracked |
| `acceptance-hostbridge-build` | the committed `.ASM` still *assembles* to the committed `.HEX`, `.PRN` and `.COM` | the 8 MB image, which is **not** in git |

**So the `.ASM` → `.HEX` half is not checked on a fresh clone.** `PIP`ping 78 KB of source
into the guest needs 78 KB of free disk, and the tracked floppy has 18K; no choice of
fixture fixes that. If you edit a `.ASM`, fetch the 8 MB image
(`disks/mits-88dcdd/cpm22/8mb/README.md`) and run `ctest -R hostbridge-build` before you
commit — otherwise nothing will tell you the `.HEX` beside it went stale.

Both modes then *use* what they built. So if a `.ASM` and the card ever disagree about the
protocol, the assembler still succeeds and the *transfer* fails — in the test, not in your
hands.

## HB.INC is the master, and nothing includes it

DR's `ASM.COM` has no `INCLUDE` directive — that is M80's `MACLIB`. So each `.ASM` carries
its own copy of the ports, commands, status bits and error codes. `HB.INC` is where they
are *defined*: if a number in it and a number in a `.ASM` ever disagree, `HB.INC` is right
and the other is a typo.

## Every disk operation is a BDOS call

No BIOS entry points. No `IN`/`OUT` to a disk controller. No assumption about a DPB, a
sector size, a skew table, or which card the drive is on. The only ports these programs
touch are the bridge's own two.

That is why one `R.COM` runs on an 8″ 88-DCDD, an 8 MB image, an 88-MDS minidisk, and any
BIOS anybody writes later — and the acceptance test proves it rather than claiming it: it
builds `R.COM` on an 8 MB 88-DCDD image and `LOAD`s the same hex on a minidisk behind a
**different controller**, then round-trips the same bytes.

## If you move the card, reassemble

The port is an `EQU` at the top of each file. These programs deliberately do **not** scan
the bus looking for the bridge: a blind `IN` across 256 ports of an unknown machine would
take a byte out of a UART's receive register and step a floppy controller's head on the way
past. A program that guesses at hardware is a program that breaks other hardware.

They do check that the card is *there*, with `IDENT`, before doing anything — because an
`IN` from a port nobody decodes floats the bus and reads `0FFH`, and `0FFH` has `DAV` set.
Without that check, `R` on a machine with no bridge in it would cheerfully write a disk full
of `0FFH` bytes and call it a file.
