# Host Bridge — guest ⇄ host file transfer

**Status:** built (milestone 7). **This is our own design, not a period card** — MITS never made it, nobody ever fabricated it, and it is the one board in the default machine that is an anachronism on purpose.

Type `hostbridge`. Two I/O ports, default base `0xB0`. In the default machine as `hb0`.

## Why this exists

A guest running under CP/M has no way to reach a file on the host, and the host has no way to reach a file inside a mounted disk image. Both directions were blocked:

- **Host-side image surgery is deferred** (`DESIGN.md` §12.2). Reading a file out of a `.dsk` needs the *controller's* sector layout **and** the *image's* CP/M DPB and software skew, and nothing in the simulator can infer that pair from the bytes.
- **AltairZ80's port-0xFE "SIMH pseudo device" is refused** (`DESIGN.md` §0.1, §12). It is another simulator's invention, not Altair hardware, and implementing its protocol would mean deriving from that simulator's source. It also sits on 0xFE — which in a real Altair is the **88-VI/RTC's control register**, a card we now have.

So the Host Bridge is not a convenience. **It is the supported guest↔host file path**, which is why it ships in the default machine rather than off in an example, and why its sandbox is load-bearing rather than defensive.

It is also the first genuinely new card built against the board API, which makes it a real test of that API rather than a rehash of a card whose shape we already knew.

## The port, and why not 0x30

`0x30` was the obvious pick and it is **wrong**: the WD179X floppy controller defaults to `0x30–0x33` and the Cromemco 64FDC puts its control register at `0x34`. Putting a host-transfer card on top of a widely-cloned S-100 disk address is precisely the mistake AltairZ80 made with 0xFE.

A census of every default I/O base in both catalogs — this simulator's `src/boards/` and AltairZ80's ~35 S-100 devices — leaves exactly two empty 16-port holes: **`B0–BF`** and **`D0–DF`**. The card takes `0xB0–0xB1`.

```
00-01 88-SIO   06-07 88-ACR   08-0A 88-DCDD/MDS   0E-0F Dazzler   10-13 88-2SIO
20-23 88-4PIO  30-34 WD179X + 64FDC               40/C0 bank select
A0-A7 88-HDSK  C0 PMMI        F8-FC Tarbell       FE 88-VI/RTC     FF front panel
```

If you move the card with `SET hb0 PORT=..`, **reassemble the utilities** — the port is an `EQU` in each `.ASM`. They deliberately do not scan the bus for the bridge: a blind `IN` across 256 ports of an unknown machine would take a byte out of a UART's receive register and step a floppy controller's head on the way past.

## Registers

| Addr | OUT | IN |
|---|---|---|
| BA+0 | Command | Status |
| BA+1 | Data | Data |

### Status (IN BA+0)

| Bit | Name | Meaning |
|---|---|---|
| 0 | `RDY` | Ready for a command. Always set — see *Buffering*. |
| 1 | `DAV` | A byte is waiting at BA+1 |
| 2 | `TBE` | BA+1 will accept a byte |
| 3 | `EOF` | The stream you are draining has run dry |
| 4 | `ERR` | The last operation failed — ask `ERROR` |
| 5–7 | — | **always read zero** |

Bits 5–7 read zero on purpose: an `IN` from an undecoded port floats the bus and reads `0xFF`, so a status of `0xFF` means *there is no card here*. Do not lean on that, though — use `IDENT`, which is the real probe, and which every utility does first.

### Commands (OUT BA+0)

| Cmd | Name | Protocol |
|---|---|---|
| `0x00` | `IDENT` | BA+1 yields `"ALTAIRSIM HOSTBRIDGE 1"`, NUL-terminated |
| `0x01` | `OPEN_READ` | Then the name at BA+1, NUL-terminated. **The NUL commits it.** Check `ERR`. Then read BA+1 while `DAV`. |
| `0x02` | `OPEN_WRITE` | Then the name. Then write bytes to BA+1. Then `CLOSE`. |
| `0x03` | `CLOSE` | Commit. **Required** — an unclosed write is discarded. |
| `0x04` | `DIR_FIRST` | Then a glob, NUL-terminated (empty ⇒ `*`). Then read NUL-terminated names. |
| `0x05` | `DIR_NEXT` | The next name; `EOF` when the list runs out |
| `0x06` | `DELETE` | Then the name |
| `0x07` | `ERROR` | BA+1 yields the code byte, **then a NUL-terminated message** |
| `0x08` | `RESET` | Abort, discard any uncommitted write, clear the error latch |

Error codes — these are on the wire, so they are published and will not be renumbered:

`0x00` none · `0x01` no such file · `0x02` permission denied · `0x03` **outside the sandbox** · `0x04` host I/O error · `0x05` no file open · `0x06` too large (8 MB cap) · `0x07` bad name · `0x08` ambiguous (see *Case*).

### The one rule

> **Any OUT to the command port abandons the stream in flight.**

You can walk away from a half-finished transfer at any point, for any reason, and the next command simply works. That single invariant deletes the entire family of caveats AltairZ80's device carries — *"the calling program must request all bytes of the result, otherwise the pseudo device is left in an undefined state"* — and with it the reason its utilities open by sending a reset **128 times in a row**. Ours send it once.

The one thing a command does **not** abandon is the **directory enumerator**. That is not an exception — a listing is not a stream — and it is what lets `R *.ASM` do a whole `OPEN_READ` and transfer *between* two `DIR_NEXT`s without losing its place. `DIR_FIRST` and `RESET` are the only things that disturb it.

## Buffering, and an honest deviation

`OPEN_READ` slurps the whole file into memory once; `OPEN_WRITE` accumulates and `CLOSE` spills it in one go; `DIR_FIRST` snapshots the name list. Every subsequent `IN`/`OUT` on BA+1 is then a memcpy — **zero host I/O in the bus path**, which is what "boards must never block" (`DESIGN.md` §8) actually demands.

Two properties fall out for free rather than being rules someone has to remember: *an unclosed write is not committed*, and *a bus reset discards a partial write*.

**The deviation, stated plainly:** the slurp at `OPEN` and the spill at `CLOSE` are **synchronous** — they are not routed through the `EventQueue`. They are single, bounded, non-recurring host operations, not per-byte work in a hot loop. `RDY` is therefore always set and no guest ever waits. The guest side already polls status, so this can be made asynchronous later without touching a single line of 8080. Files above a hard **8 MB** cap (a full CP/M volume) are refused with `0x06` — and the size is asked for *before* anything is allocated.

## The sandbox

Guest names resolve against `hostdir` and **cannot leave it**. This is a hard requirement, not a nicety: the card is in the default machine, so a rogue CP/M program is running against your working directory.

Refused with `0x03`:

- an absolute path (a leading `/` or `\`)
- a drive letter (any `:`)
- a `..` **component** — note *component*, not substring: `A..B.TXT` is a perfectly legal name
- **a symlink that resolves out of the root**, which is the only one that takes real work

The symlink gate is a **component-wise** prefix check against the canonicalized root, not a string compare — a string compare would happily conclude that `/tmp/sandbox-evil` lives inside `/tmp/sandbox`. `tests/test_hostdir.cpp` is where that claim is proved, and it runs against a **real filesystem with real symlinks**, because a symlink escape cannot be tested against a fake one.

**Subdirectories are supported** (`R SRC/FOO.ASM`), and **both separators work on every host**: `/` and `\` are accepted and normalized everywhere. So one assembled `R.COM` runs against a Mac, a Linux box and a Windows box without being told which. (AltairZ80's device has a command for asking the guest what the host's path separator is. The guest should not have to care, and here it does not.)

## Case

The CP/M CCP **folds the command tail to upper case before the program runs**. `R readme.txt` is therefore not a thing a CP/M program can ever see — it arrives as `README.TXT`, and the information is gone.

So the card compensates. An **exact** match always wins. Failing that it folds case and looks again: if exactly one host file matches, that is the one; if **several** do, it refuses with `0x08` rather than guess. A *write* of a name nobody has creates it exactly as asked.

AltairZ80 solves this with an `L` switch on its utilities, which puts the burden on the human to remember which of their files are in which case. The burden belongs here.

## Properties

| Property | Default | Notes |
|---|---|---|
| `port` | `B0` | Base address, 2 ports. Radix 16 — it is on the wire. |
| `hostdir` | `""` | The sandbox root. **Empty means the directory you ran `altairsim` from.** |
| `readonly` | `off` | Refuse `OPEN_WRITE` and `DELETE` — a one-way street, out of the host only |

`hostdir = ""` is not a special case: it is exactly what `Board::resolvePath()` already does with an empty `configDir_` — *a path typed is relative to the shell*. Aim it somewhere else with the existing `-x`:

```
altairsim -x 'SET hb0 HOSTDIR=/tmp/xfer' -i
```

**This sandbox and the path rule in `docs/config.md` are different things, and only one of them is a fence.** `resolvePath()` appears in the line above, so the resemblance is easy to over-read: that call decides *what a relative `hostdir` points at*, exactly as it does for a `mount`, and then it is finished. The confinement is not its doing — it belongs entirely to `HostDir` (`src/host/hostdir.h`), which rejects `..`, absolute paths, drive letters and symlink escapes on every name the **guest** sends, and answers `Outside` when it says no.

So the two answer different questions. `configDir_` answers *where does this path point*, and confines nothing: a machine file may name any file on the disk. `hostdir` answers *how far may a CP/M program reach*, and is the only boundary in the system. Changing where machine-file paths resolve would not move this sandbox by an inch.

**There is no `interrupt` property.** Every operation completes inside the `OUT` that asked for it, so there is nothing to wait for and nothing to fire on. A jumper that could only ever mean "already done" would be a lie about the card.

## Reset

`Reset::PowerOn` and `Reset::Bus` both abort any transfer, **discard any uncommitted write**, clear the error latch and reset the enumerator. `port`, `hostdir` and `readonly` survive both — they are jumpers, not state.

## Guest utilities

`cpm/hostbridge/{R,W,HDIR}.ASM`. 8080 only, `ORG 100H`, assembled **inside the machine** with `ASM.COM` and `LOAD.COM` — no host toolchain, no cross-assembler.

```
R  <hostfile> [cpmfile]         host -> CP/M      R *.ASM     R SRC/FOO.ASM
W  <cpmfile> [hostfile] [B|T]   CP/M -> host      W *.HEX     W FOO.TXT T
HDIR [pattern]                  what is on the host
```

**The names are AltairZ80's; the code is not.** Its `R.COM` and `W.COM` talk to a pseudo-device at 0xFE and were written in SPL; ours talk to this card at 0xB0 and are 8080 assembler. Neither will run in the other simulator. The muscle memory is worth keeping; nothing else is shared. `HDIR` has no ancestor at all — AltairZ80 ships no way to see what is there to read, so you type a host name from memory, get it wrong, and cannot tell whether the file is missing or your spelling is.

### Every disk operation is a BDOS call

No BIOS entry points. No `IN`/`OUT` to a disk controller. No assumption about a DPB, a sector size, a skew table, or which card the drive is on. The only ports the utilities touch are the bridge's own two.

That is what makes the same `R.COM` work on an 88-DCDD 8″ floppy, an 8 MB image, an 88-MDS minidisk, and any BIOS anybody writes later — and the acceptance test **proves** it rather than asserting it: it builds `R.COM` and `W.COM` on an 8 MB 88-DCDD image, then `LOAD`s the *same hex* on a 5.25″ minidisk behind a **different controller** and round-trips the same bytes.

### `W` defaults to binary, and that is the opposite of AltairZ80

CP/M stores whole 128-byte records and **keeps no byte count anywhere**, so a file's true length is simply not recorded — the last record is padded, by convention with `^Z`. Coming back out, those pad bytes are indistinguishable from data. Only the human knows whether the file was text.

- **`B` (default)** — every byte of every record, exactly. A `.COM` survives the trip; a text file arrives with up to 127 trailing `^Z`.
- **`T`** — stop at the first `^Z`. A text file comes back clean; **a binary file containing a `1AH` comes back truncated**, which is why it is not the default.

AltairZ80's `W` defaults to text and keeps an exception list of extensions (`.COM`, `.REL`, `.DAT`…) it treats as binary instead — so a file whose name is not on the list and whose contents contain a `1AH` is silently truncated. Defaulting to exact costs a few pad bytes. Defaulting to text costs your data. (Same instinct as the `strip7out` scar.)

### 8.3 mapping

The CP/M name defaults to the host name uppercased, base truncated to 8 and extension to 3, with characters CP/M cannot store simply **dropped** — `my-notes(2).txt` becomes `MYNOTES2.TXT`. Dropping rather than refusing is the friendly choice for a name that came off a host filesystem. A subdirectory prefix cannot survive (`SRC/FOO.ASM` → `FOO.ASM`), because a CP/M file has no directory to live in. An explicit second argument overrides all of it, and **`HDIR` always prints true host names** — so when `R` cannot name something, you can still see what it was.

## Limitations

- **The default `hostdir` is the shell's working directory, so guest software can read and write it.** That is the deliberate cost of the card being in the default machine, and it is documented rather than discovered. It cannot go anywhere *else* — see *The sandbox* — and `SET hb0 READONLY=ON` makes it read-only.
- Sequential only, no random access. A guest that wants seek should use a disk image.
- Whole files only: an 8 MB cap, and the file is held in host memory while it is in flight.
- A CP/M program cannot ask for a lower-case name. See *Case*.

## Verification

- `tests/test_hostdir.cpp` — the sandbox, against a **real** filesystem: `../SECRET.TXT`, `../../etc/passwd`, `/etc/passwd`, `\etc\passwd`, `C:\windows`, and a **symlink pointing out** (both a file and a directory) must each fail `0x03` and touch nothing.
- `tests/test_hostbridge.cpp` — the protocol, against a `MemHostDir` with `bus.setVerify(true)`: `IDENT`; the stream rule; the enumerator surviving a transfer; an unclosed write discarded; both resets; `readonly`; every error code; and `!decodes(0x30)`, *because that is a floppy controller*.
- `tests/acceptance/hostbridge.cmake` — the whole chain, on a scratch copy of the image. It `PIP`s all three sources in through the console, `ASM`/`LOAD`s each, and round-trips **256 bytes containing every value from `00` to `FF`** — including `00`, `1A` and `FF`, the three a careless transfer mangles. The verdict is read off the host's disk, not the terminal: *"2 records"* means the guest counted to two, not that the bytes are right.
