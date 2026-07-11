# Host Bridge — guest ⇄ host file transfer

**Status:** not implemented (milestone 7). **This is our own design, not a period card.**

## Why this exists

A guest program running under CP/M sometimes needs to read or write a file on the host — to pull in a source file, to drop out a build artifact.

AltairZ80 solves this with a "SIMH pseudo device" at port 0xFE and a pair of guest utilities (`R.COM`/`W.COM`). **We do not implement it.** It is not real Altair hardware; it is another simulator's invention, and reimplementing its protocol would mean deriving from that simulator's source, which this project does not do (`DESIGN.md` §0.1). `R.COM` and `W.COM` will not run under `altairsim`.

Instead we design our own card. That is, after all, what this project is *for* — and it makes the Host Bridge **the first genuinely new piece of hardware built against the board API**, which is a far better test of that API than reimplementing a card whose shape we already know.

> **Note:** most host↔guest file movement should use the **MCP disk tools** (`disk_ls` / `disk_get` / `disk_put`), which operate on a mounted image **with the machine not even running** — no guest cooperation, no booting, no console driving. The Host Bridge is for the case where the *guest* must initiate.

## Design principles

1. **It is an ordinary `Board`.** Two I/O ports, `properties()`, both resets, `serialize()`. No bus special cases. If it needs one, the board API is wrong.
2. **The host filesystem is sandboxed.** Guest-supplied filenames resolve against a configured `hostdir` root and **cannot escape it** — no `..`, no absolute paths, no symlink traversal out. A guest program must never be able to write anywhere on the host disk. This is a hard requirement, not a nicety.
3. **It does not duplicate state that other boards own.** AltairZ80's device accreted a grab-bag of unrelated host services (RTC, timers, bank select, host sleep). We don't. A clock is a clock board; bank select belongs to the memory board. Duplicating that state would make snapshots and replay diverge.
4. **Blocking is not allowed.** Host I/O completes through the `EventQueue` like any other board's timed work; the guest polls a status bit. A board that blocks the emulation thread on a host `read()` breaks throttling and replay.

## Proposed register interface

Two ports, base address configurable (`port` property).

| Addr | OUT (write) | IN (read) |
|---|---|---|
| BA+0 | Command | Status |
| BA+1 | Data (filename bytes, file data) | Data (file data, directory entries) |

### Status (IN BA+0)

| Bit | Name | Meaning |
|---|---|---|
| 0 | `RDY` | Ready to accept a command |
| 1 | `DAV` | Read data available at BA+1 |
| 2 | `TBE` | Write buffer can accept a byte |
| 3 | `EOF` | End of file reached |
| 4 | `ERR` | Last operation failed; read the error code |
| 5–7 | — | reserved |

### Commands (OUT BA+0)

| Cmd | Name | Protocol |
|---|---|---|
| 0x01 | `OPEN_READ` | Then write the filename to BA+1, NUL-terminated. Poll `RDY`. Then read bytes from BA+1 while `DAV`, until `EOF`. |
| 0x02 | `OPEN_WRITE` | Then write the filename, NUL-terminated. Then write bytes to BA+1 while `TBE`. |
| 0x03 | `CLOSE` | Flush and close the open file. **Required** — an unclosed write is not committed. |
| 0x04 | `DIR_FIRST` | Begin enumeration of `hostdir`. Read NUL-terminated names from BA+1. |
| 0x05 | `DIR_NEXT` | Next name; `EOF` when exhausted. |
| 0x06 | `DELETE` | Then write the filename. |
| 0x07 | `ERROR` | Read a one-byte error code from BA+1. |
| 0x08 | `RESET` | Abort any operation, close any open file, clear error. |

Error codes: `0x00` none, `0x01` not found, `0x02` permission denied, `0x03` **outside sandbox**, `0x04` host I/O error, `0x05` no file open, `0x06` disk full.

> This interface is a **proposal, not settled**. It should be reviewed against the CLI transcript exercise (`docs/roadmap.md` Step 0) and against a hand-traced pass of the board API before anything is built. Design it as though someone were going to fabricate it.

## Properties

| Property | Runtime? | Notes |
|---|---|---|
| `port` | config | Base address (2 ports) |
| `hostdir` | config | **The sandbox root.** Required; no default. |
| `readonly` | yes | Refuse `OPEN_WRITE` and `DELETE` |
| `interrupt` | config | `none \| int \| vi0..vi7` — optional; the guest can poll instead |

## Reset

- `Reset::PowerOn` and `Reset::Bus` both: abort any operation, **close any open file without committing a partial write**, clear the error latch, reset the directory enumerator.
- `hostdir` survives both — it is configuration, not state.

## Guest utilities

Ours, written in 8080 assembly and assembled with the period toolchain (M80/L80):

- `HGET.COM <name>` — host file → CP/M file
- `HPUT.COM <name>` — CP/M file → host file
- `HDIR.COM` — list the host directory

These live in the repo with their source, and they are part of the milestone-7 acceptance test.

## Limitations

- No random access — sequential read/write only. Sufficient for file transfer; a guest wanting seek should use a disk image.
- No subdirectories. `hostdir` is flat, which sidesteps a whole class of path-escape bugs.
- CP/M filenames are 8.3 and uppercase; host names that don't fit are surfaced by `HDIR` but may not be openable by name from a CP/M program. Decide and document the mapping rule when this is built.

## Verification (milestone 7)

- `HPUT` a file from CP/M, verify its bytes on the host.
- `HGET` it back into a different CP/M file, verify identical.
- `HDIR` lists what's actually in `hostdir`.
- **Sandbox tests, and these are the important ones:** a guest asking for `../../etc/passwd`, an absolute path, and a symlink pointing outside the root must all fail with error `0x03` and touch nothing.
