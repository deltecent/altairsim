# Expansion boards on the Altair 680b

The built-in `altair680` machine is a Motorola 6800 with just its onboard console — one
6850 ACIA at `F000/F001`. Two examples here add a period expansion board to it:

- **`altair680-uio.toml`** — the **Universal I/O board** (`680uio`): a second serial port
  and a parallel port.
- **`altair680-kcacr.toml`** — the **KCACR audio-cassette interface** (`680kcacr`): a
  cassette you load and save S-record tapes from.

## A Universal I/O board

`altair680-uio.toml` adds the **Universal I/O board** (`680uio`), the 680b's
general-purpose expansion board: a **second 6850 serial port** and a **6820 PIA parallel
port**, memory-mapped like everything on the 6800.

```
cd examples/altair680
altairsim altair680-uio.toml
```

You land at the MON680 `.` prompt, exactly as the plain `altair680` does — you are still
typing at the **onboard** console. The UI/O is the *second* set of ports, sitting quietly
until the guest talks to it.

## Where the board lives

At its default S9 window (base `F000`) the board decodes:

| Addresses | What |
|---|---|
| `F006` / `F007` | 6850 ACIA — serial control/status and Rx/Tx data (`serial`) |
| `F008`–`F00B` | 6820 PIA-C — sections A/B, control and data/DDR (`p1a`, `p1b`) |
| `F003` | switch inputs (`sense`) — fixed, read-only |
| `F010`–`F013` | 8-bit non-latched output, Drive 1 + Drive 2 |

`SET uio0 base=0xF020` slides the serial+PIA window (the fixed `F003` and `F010`–`F013`
do not move); `SET uio0 pias=2` populates a second 6820 (`p2a`/`p2b` at `F00C`–`F00F`).
From the `altairsim>` monitor (press **^E** at the console), `SHOW MAP uio0` prints the
live map.

## Reaching the second serial from the monitor

The config wires the UI/O's serial line to `out:uio-serial.log` and PIA section `p1a` to
`out:uio-pia.log` (both beside this file). MON680's `M addr` command examines and deposits
a byte at an address, so you can drive the board by hand:

1. `M F006` and deposit `03` (ACIA master reset), then `M F006` again and deposit `D1`
   (÷16, 8N2) — the 6850 is not auto-configured on power-up.
2. `M F007` and deposit a byte — it is transmitted, and appears in **`uio-serial.log`**.

The PIA works the same way: with control bit 2 set (deposit `04` to `F008`), a byte
deposited to `F009` is driven onto section A's lines and lands in **`uio-pia.log`**.

The two `*.log` files are created when the machine starts and are not part of the
repository — they are just where this example's output goes.

See `reference/Altair 680b Universal IO Board.md` for the full register model, and the
built-in `altair680` machine for the base machine this extends.

## A KCACR audio cassette

`altair680-kcacr.toml` adds the **KCACR** (`680kcacr`), the 680b's audio-cassette
interface: a UART at `F010` (status/control) and `F011` (data), recording **Kansas City
Standard** FSK, with a loader/punch PROM at `FD00` (`builtin:kcacr`).

```
cd examples/altair680
altairsim altair680-kcacr.toml
```

A cassette (`kcacr-demo.tap`) is already in the recorder — a tiny Motorola S-record tape
that deposits four bytes (`86 2A 39 00`) at `0200`. From the `.` prompt:

1. `JFD00` — run the loader PROM. It reads the tape's S-records into memory and returns
   to the monitor at the terminating `S9` record. (On a bad tape it prints one letter and
   stops: `C` = checksum/non-hex, `M` = memory error.)
2. `M0200` — examine `0200`. It reads back `86`, the first byte the tape carried.

To **save** memory back to a cassette, `JFD74` and answer the two address prompts with the
start and end (four hex digits each); the loader writes S-records terminated by `S9`.

### Loading real 680b software

The same `JFD00` path loads any S-record cassette. MITS's **680b Cassette BASIC** and the
Editor/Assembler are on deramp.com as `.S19` files (and as `.WAV`), under
`downloads/altair/software/altair_680/`. Save one beside this file, put it in the recorder
in place of the demo tape (`MOUNT kc0:tape "Cassette BASIC V1.1 R3.2.S19"`), `JFD00` to
load it, then `J` its start address — the loader does not care whether the tape holds four
bytes or all of BASIC.

This machine has **16K of RAM** for that reason: BASIC loads across low memory far past the
demo tape's single page. Because the KCACR loader verifies each byte as it writes, a program
that reaches unpopulated RAM stops with an `M` (memory error) partway through the load — so if
you trim the `[[board.region]]` RAM size, keep it large enough for whatever you are loading.

The `680kcacr` reads and writes **Kansas City** modulation only; a plain-MITS 88-ACR tape
is refused. See `reference/Altair 680b KCACR.md` for the board and `roms/KCACR/` for the
loader PROM.
