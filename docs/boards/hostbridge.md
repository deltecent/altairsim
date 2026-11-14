# Host Bridge — guest ⇄ host file transfer

**Status:** built. **This is our own design, not a period card** — MITS never made it, nobody ever fabricated it, and it is the one board in the default machine that is an anachronism on purpose.

Type `hostbridge`. Two I/O ports, default base `0xB0`. In the default machine as `hb0`.

## Why this exists

A guest running under CP/M has no way to reach a file on the host, and the host has no way to reach a file inside a mounted disk image. Both directions were blocked:

- **Host-side image surgery is deferred** (`DESIGN.md` §12.2). Reading a file out of a `.dsk` needs the *controller's* sector layout **and** the *image's* CP/M DPB and software skew, and nothing in the simulator can infer that pair from the bytes.
- **A host-transfer pseudo-device at port 0xFE is refused** (`DESIGN.md` §0.1, §12). In a real Altair 0xFE is the **88-VI/RTC's control register**, a card we now have.

So the Host Bridge is not a convenience. **It is the supported guest↔host file path**, which is why it ships in the default machine rather than off in an example, and why its sandbox is load-bearing rather than defensive.

The board also lets a guest set the processor's crystal (issue #918). A `SUBMIT` file can run `SIMCLK 0` before a slow assembly and `SIMCLK 2000000` after it. Before this, only the monitor could change `clock_hz`, and a `SUBMIT` file cannot reach the monitor.

It is also the first genuinely new card built against the board API, which makes it a real test of that API rather than a rehash of a card whose shape we already knew.

## The port, and why not 0x30

`0x30` was the obvious pick and it is **wrong**: the WD179X floppy controller defaults to `0x30–0x33` and the Cromemco 64FDC puts its control register at `0x34`. A host-transfer card does not go on top of a widely-cloned S-100 disk address.

A census of every default I/O base in `src/boards/`, against the wider S-100 catalog, leaves exactly two empty 16-port holes: **`B0–BF`** and **`D0–DF`**. The card takes `0xB0–0xB1`.

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
| 0 | `RDY` | Ready for a command. Clear only while a `SET_CLOCK` waits — see *The clock*. |
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
| `0x09` | `DIR_LONG` | Like `DIR_FIRST`, but each entry streams **three** NUL-terminated fields — `name`, `size`, `date`. Walk it with `DIR_NEXT`. |
| `0x0A` | `SET_CLOCK` | Then the rate in Hz as decimal digits, NUL-terminated. `0` is flat out. Wait for `RDY`, then check `ERR`. |
| `0x0B` | `GET_CLOCK` | BA+1 yields the rate in force as decimal digits, NUL-terminated. `0` is flat out. |

`DIR_LONG` is what `HDIR` uses for its `ls -l`-style listing. `size` is a decimal byte count, or the literal `<DIR>` for a directory; `date` is the host mtime as `MM/DD/YY`. It builds and walks the very same enumerator as `DIR_FIRST` (so `DIR_NEXT` advances either, and `DIR_FIRST` and `RESET` clear either) — the only difference is how many fields each entry hands back. `DIR_FIRST` still streams a bare name, so a guest built against the older, name-only protocol is unaffected.

Error codes — these are on the wire, so they are published and will not be renumbered:

`0x00` none · `0x01` no such file · `0x02` permission denied · `0x03` **outside the sandbox** · `0x04` host I/O error · `0x05` no file open · `0x06` too large (8 MB cap) · `0x07` bad name, or a bad clock rate · `0x08` ambiguous (see *Case*) · `0x09` the machine refused the request (see *The clock*).

### The one rule

> **Any OUT to the command port abandons the stream in flight.**

You can walk away from a half-finished transfer at any point, for any reason, and the next command simply works. A `SET_CLOCK` that has not been applied yet is dropped too, so a guest must wait for `RDY` before its next command. That single invariant deletes the entire family of caveats that would otherwise attach to a streaming device — *the calling program must request every byte of a result, or the device is left in an undefined state* — and with it any reason for a utility to open by resetting the card repeatedly. Ours reset it once.

The one thing a command does **not** abandon is the **directory enumerator**. That is not an exception — a listing is not a stream — and it is what lets `R *.ASM` do a whole `OPEN_READ` and transfer *between* two `DIR_NEXT`s without losing its place. `DIR_FIRST`, `DIR_LONG` and `RESET` are the only things that disturb it.

## Buffering, and an honest deviation

`OPEN_READ` slurps the whole file into memory once; `OPEN_WRITE` accumulates and `CLOSE` spills it in one go; `DIR_FIRST` snapshots the name list. Every subsequent `IN`/`OUT` on BA+1 is then a memcpy — **zero host I/O in the bus path**, which is what "boards must never block" (`DESIGN.md` §8) actually demands.

Two properties fall out for free rather than being rules someone has to remember: *an unclosed write is not committed*, and *a bus reset discards a partial write*.

**The deviation, stated plainly:** the slurp at `OPEN` and the spill at `CLOSE` are **synchronous** — they are not routed through the `EventQueue`. They are single, bounded, non-recurring host operations, not per-byte work in a hot loop. No file operation clears `RDY`, and no guest waits for one. The guest side already polls status, so this can be made asynchronous later without touching a single line of 8080. Files above a hard **8 MB** cap (a full CP/M volume) are refused with `0x06` — and the size is asked for *before* anything is allocated.

## The clock

`SET_CLOCK` asks for a new crystal on the processor board, and `GET_CLOCK` reports the crystal in force. The rate is in Hz, and `0` means flat out, as `clock_hz = 0` does.

**The crystal stays on the processor board.** The Host Bridge does not change the clock itself. It holds the request, and the debugger applies it at the end of the run slice, a few thousand instructions at most. The debugger does a `SET cpu0 clock_hz=N`, so the CPU board's own range check runs, and `SHOW cpu0` shows the new rate.

**`SET_CLOCK` is the one command that makes the guest wait.** After the NUL, `RDY` reads clear until the debugger has applied the rate. When `RDY` comes back, `ERR` tells the guest the result:

- **Refused** (`0x09`): the CPU board did not take the rate. `ERROR` returns the board's own words, for example `clock_hz must be 0..100000000`. The rate is unchanged.
- **Bad name** (`0x07`): the text was not decimal digits, or it had more than ten. The board refuses it at once, and `RDY` stays set. `ERROR` returns `bad clock rate -- give the rate in Hz as decimal digits`.

**Any command drops a request that is still waiting**, by the rule above. A guest that does not wait for `RDY` loses its request. `RESET` and both bus resets drop it too.

`SET_CLOCK` clears the error latch, like every operation. `GET_CLOCK` is a report, like `IDENT`, and leaves the latch alone.

An older altairsim does not know these commands. It sets `ERR` with `unknown host bridge command`, and `SIMCLK` prints that text.

## The sandbox

Guest names resolve against `hostdir` and **cannot leave it**. This is a hard requirement, not a nicety: the card is in the default machine, so a rogue CP/M program is running against your working directory.

Refused with `0x03`:

- an absolute path (a leading `/` or `\`)
- a drive letter (any `:`)
- a `..` **component** — note *component*, not substring: `A..B.TXT` is a perfectly legal name
- **a symlink that resolves out of the root**, which is the only one that takes real work

The symlink gate is a **component-wise** prefix check against the canonicalized root, not a string compare — a string compare would happily conclude that `/tmp/sandbox-evil` lives inside `/tmp/sandbox`. `tests/test_hostdir.cpp` is where that claim is proved, and it runs against a **real filesystem with real symlinks**, because a symlink escape cannot be tested against a fake one.

**Subdirectories are supported** (`R SRC/FOO.ASM`), and **both separators work on every host**: `/` and `\` are accepted and normalized everywhere. So one assembled `R.COM` runs against a Mac, a Linux box and a Windows box without being told which. There is no command for asking which separator the host uses, because the guest should not have to care.

## Case

The CP/M CCP **folds the command tail to upper case before the program runs**. `R readme.txt` is therefore not a thing a CP/M program can ever see — it arrives as `README.TXT`, and the information is gone.

So the card compensates. An **exact** match always wins. Failing that it folds case and looks again: if exactly one host file matches, that is the one; if **several** do, it refuses with `0x08` rather than guess. A *write* of a name nobody has creates it exactly as asked.

**Every part of a path folds, not only the last.** `R SRC/FOO.ASM` and `W *.ASM OUT` reach the board in upper case too, and on a case-sensitive host the folder is `src` or `out`. `RealHostDir::resolve()` walks the path one part at a time. A part that does not exist as typed is looked up with the fold, in the folder found so far. A part before the last must match a folder, and two folders that fold to one name give `0x08`. A part with no match is kept as typed, so a write still creates a new file, and a read still fails with `0x01`. Before this, only the last part folded, so `R SRC/FOO.ASM` failed on Linux when the folder was `src`. macOS and Windows hid the fault, because their file systems fold case themselves.

The burden belongs on the card, not on the human: there is no switch to tell the utilities which case to look for, because nobody should have to remember which of their files are in which.

The other direction is `R`'s job. A host name that comes back off a wildcard match keeps the host's case, and a CP/M directory entry holds upper case — so `R` folds the name it builds the FCB from. Without that, `R *.asm` stores `lower.asm`, and nothing in CP/M can name it again: the CCP upper-cases everything typed, so `ERA`, `DIR` and the assembler all look straight past it.

**A host name that starts with `.` matches only a glob that starts with `.`**, as in a shell. `R` drops the `.` from a CP/M name, so `.DS_Store` would become ` .DS_`. That CP/M name has an empty first part, and the CCP cannot type it. `*.*`, `*` and the empty glob therefore skip dot files, in `R` and in `HDIR`. `.*` and a full name such as `.gitignore` still reach them. Give `R` a second argument for the CP/M name, as in `R .gitignore GITIGNOR.TXT`. The rule is in `hostGlobMatch()`, so the real and the in-memory host directory agree (issue #829).

## Properties

| Property | Default | Notes |
|---|---|---|
| `port` | `B0` | Base address, 2 ports. Radix 16 — it is on the wire. |
| `hostdir` | `""` | The sandbox root. **Empty means the directory you ran `altairsim` from.** |
| `readonly` | `off` | Refuse `OPEN_WRITE` and `DELETE` — a one-way street, out of the host only |

`hostdir = ""` is the directory you launched from, even for a machine file loaded from somewhere else — so `R FOO.ASM` finds the file in the shell you are standing in. A relative `hostdir` you *write* is a path like any other, and resolves against the machine's directory (`docs/config.md`). Aim it somewhere else with the existing `-x`:

```
altairsim -x 'SET hb0 HOSTDIR=/tmp/xfer' -i
```

**This sandbox and the path rule in `docs/config.md` are different things, and only one of them is a fence.** `resolvePath()` appears in the line above, so the resemblance is easy to over-read: that call decides *what a relative `hostdir` points at*, exactly as it does for a `mount`, and then it is finished. The confinement is not its doing — it belongs entirely to `HostDir` (`src/host/hostdir.h`), which rejects `..`, absolute paths, drive letters and symlink escapes on every name the **guest** sends, and answers `Outside` when it says no.

So the two answer different questions. `configDir_` answers *where does this path point*, and confines nothing: a machine file may name any file on the disk. `hostdir` answers *how far may a CP/M program reach*, and is the only boundary in the system. Changing where machine-file paths resolve would not move this sandbox by an inch.

**There is no `interrupt` property.** Every operation completes inside the `OUT` that asked for it, so there is nothing to wait for and nothing to fire on. A jumper that could only ever mean "already done" would be a lie about the card.

## Reset

`Reset::PowerOn` and `Reset::Bus` both abort any transfer, **discard any uncommitted write**, drop a `SET_CLOCK` that has not been applied, clear the error latch and reset the enumerator. `port`, `hostdir` and `readonly` survive both — they are jumpers, not state.

## Guest utilities

`cpm/hostbridge/{R,W,HDIR,SIMCLK}.ASM`. 8080 only, `ORG 100H`, assembled **inside the machine** with `ASM.COM` and `LOAD.COM` — no host toolchain, no cross-assembler.

```
R  <hostfile> [cpmfile]         host -> CP/M      R *.ASM     R SRC/FOO.ASM
W  <cpmfile> [hostfile] [B|T]   CP/M -> host      W *.HEX     W FOO.DAT T
W  <pattern> [hostfolder] [B|T]                   W *.ASM SRC
HDIR [pattern]                  what is on the host
SIMCLK [hz]                     set or show the crystal   SIMCLK 0    SIMCLK 2000000
```

`R` and `W` are short because they are typed constantly. `HDIR` exists because without a way to see what is there to read, you type a host name from memory, get it wrong, and cannot tell whether the file is missing or your spelling is.

`SIMCLK` with a rate sends `SET_CLOCK`, waits for `RDY`, and then prints the rate in force, `SIMCLK: 2000000 Hz` or `SIMCLK: flat out`. If the board set `ERR`, it prints the board's error text, for example `SIMCLK: clock_hz must be 0..100000000`. With no rate, it prints the rate in force. The command tail goes to the board as typed: the board checks the digits, so `SIMCLK` has no parser of its own.

### Every disk operation is a BDOS call

No BIOS entry points. No `IN`/`OUT` to a disk controller. No assumption about a DPB, a sector size, a skew table, or which card the drive is on. The only ports the utilities touch are the bridge's own two.

That is what makes the same `R.COM` work on an 88-DCDD 8″ floppy, an 8 MB image, an 88-MDS minidisk, and any BIOS anybody writes later — and the acceptance test **proves** it rather than asserting it: it builds `R.COM` and `W.COM` on an 8 MB 88-DCDD image, then `LOAD`s the *same hex* on a 5.25″ minidisk behind a **different controller** and round-trips the same bytes.

### `W`: `B`, `T`, and the extension when neither is given

CP/M stores whole 128-byte records and **keeps no byte count anywhere**, so a file's true length is simply not recorded. A text file marks its own end with a `^Z`, by convention, and **CP/M itself pads nothing**: what follows that `^Z` in the last record is whatever the program that wrote the file left in its buffer. Some fill the record with `^Z` — our own `R` does — and some write one `^Z` and leave the rest as it was. Coming back out, none of it can be told from data.

- **`B`** — every byte of every record, exactly. A `.COM` survives the trip; a text file arrives with the rest of its last record on the end — the `^Z`, and up to 126 bytes of whatever followed it.
- **`T`** — stop at the first `^Z`. A text file comes back clean; **a binary file containing a `1AH` comes back truncated**.

**With no switch, `W` reads the extension of the CP/M name.** `ASM` `MAC` `Z80` `HEX` `PRN` `LST` `SYM` `INC` `SUB` `TXT` are text; everything else, `.COM` included, is binary. A switch always wins. A wildcard chooses for each match, so `W *.*` is correct for a mixed set — which no single switch ever was.

**The list names text, not binary, and that is what makes it safe.** `W` used to refuse to look at the extension at all, and the objection was sound for the list it had in mind: a list of names that "are really binary" truncates the first binary file that is not on it. This list is the other way round. A file that is not on it is copied exactly, so a missing name costs the tail of one record and never data. Every name on it is a source, listing or hex file, which a `^Z` ends by definition. `.BAS` and `.DOC` are left off on purpose: MBASIC saves tokens unless told otherwise, and a word processor's file can hold a `1AH`.

**The choice is never silent.** When the extension picked text, the transfer line says so — `W: FOO.HEX -> FOO.HEX (text, by extension)`. With a switch, or for a binary file, the line is what it always was.

### `W`: a wildcard into a host folder

With a wildcard, a second word is a host folder: `W *.ASM SRC` writes `SRC/FOO.ASM`, as `cp *.asm src` does. Each file keeps its own name, so a folder is the only thing the second word can name. `W` adds the `/` unless the word ends in `/` or `\`. A lone `B` or `T` is still the switch, so a folder with one of these names is typed `B/`. `W *.* OUT T` gives a folder and a switch.

**The folder must exist, and `W` checks it once, before the first file.** It sends `DIR_FIRST` with the folder as the glob (`OUT/`). That listing fails on a missing folder, or on a file, and it creates and changes nothing on the host. `W` prints the board's error, `W: NOPE: no such file`, and exits without a copy. A check for each file would print one error for each match. `W` also refuses a folder name too long to leave room for a 12-character `FILENAME.EXT` in its name buffer.

### 8.3 mapping

The CP/M name defaults to the host name uppercased, with characters CP/M cannot store simply **dropped**, and then the base truncated to 8 and the extension to 3. What is dropped is a space, a control character, anything that is not ASCII, and `< > . , ; : = ? * [ ] | \ /` — the characters that end a name for the CCP, and the two path separators. Everything else stays, `-` and parentheses included: `notes[old].txt` becomes `NOTESOLD.TXT`, and `my-notes(2).txt` becomes `MY-NOTES.TXT`, the `(2)` falling past the eighth character. A space is dropped because a name stored with one could never be typed at the CCP again. Dropping rather than refusing is the friendly choice for a name that came off a host filesystem. A subdirectory prefix cannot survive (`SRC/FOO.ASM` → `FOO.ASM`), because a CP/M file has no directory to live in. An explicit second argument overrides all of it, and **`HDIR` always prints true host names** — so when `R` cannot name something, you can still see what it was.

## Limitations

- **The default `hostdir` is the shell's working directory, so guest software can read and write it.** That is the deliberate cost of the card being in the default machine, and it is documented rather than discovered. It cannot go anywhere *else* — see *The sandbox* — and `SET hb0 READONLY=ON` makes it read-only.
- Sequential only, no random access. A guest that wants seek should use a disk image.
- Whole files only: an 8 MB cap, and the file is held in host memory while it is in flight.
- A CP/M program cannot ask for a lower-case name. See *Case*.

## Verification

- `tests/test_hostdir.cpp` — the sandbox, against a **real** filesystem: `../SECRET.TXT`, `../../etc/passwd`, `/etc/passwd`, `\etc\passwd`, `C:\windows`, and a **symlink pointing out** (both a file and a directory) must each fail `0x03` and touch nothing.
- `tests/test_hostbridge.cpp` — the protocol, against a `MemHostDir` with `bus.setVerify(true)`: `IDENT`; the stream rule; the enumerator surviving a transfer; an unclosed write discarded; both resets; `readonly`; every error code; and `!decodes(0x30)`, *because that is a floppy controller*. In a machine with a CPU board: `GET_CLOCK`; `SET_CLOCK` through `Debugger::run()` to `clock_hz` and the `Clock`; a refused rate; bad digits; a request dropped by a command, a reset and a bus reset; and a snapshot with a request waiting.
- `tests/acceptance/hostbridge.cmake` — the whole chain, on a scratch copy of the image, in two modes. `acceptance-hostbridge` `PIP`s the committed `.HEX` in through the console, `LOAD`s each, and round-trips **256 bytes containing every value from `00` to `FF`** — including `00`, `1A` and `FF`, the three a careless transfer mangles. It runs on the **tracked** floppy, so it needs no download. `acceptance-hostbridge-build` (`slow`) instead `PIP`s the four `.ASM` in and assembles them, proving the sources still build the committed artifacts; 64 KB of source needs the 8 MB image, which is not in git, so that one skips without it. Both modes also pin `W`'s choice of mode: a `.TXT` with no switch comes back trimmed, a switch wins over the extension in both directions, and one `W MIX.*` brings back two text files trimmed and a binary whole. Both modes run `SIMCLK`: a refused rate and bad digits, then 2 MHz and back to flat out, then 2 MHz to the end. A run loop that does not re-base its pacing when the rate changes freezes the guest at the switch, and the test then fails as a hung machine. `SHOW cpu0`, from the host side, must show `clock_hz 2000000` at the end. Either way the verdict is read off the host's disk, not the terminal: *"2 records"* means the guest counted to two, not that the bytes are right.
