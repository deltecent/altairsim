# Testing/ — the real-wire XMODEM harness

This is not the unit-test suite (that's `ctest` in `build/`). This is a **hardware-in-the-loop**
rig: it runs a real `altairsim` guest against a **real** XMODEM peer over a **real** null-modem
cable, so a transfer either keeps up with data arriving off a physical wire or it doesn't. There
is no emulated clock or emulated timeout at the far end to hide behind.

It exists because two bugs could only be settled against real hardware:

- **Bug #6 — a faster `clock_hz` was *slower*.** The run loop's idle nap fired *during* a transfer
  running on a non-console line, because the "a byte arrived, don't nap" defence only watched the
  console. `nap6.sh` is the decisive experiment (prediction written before the run); the fix
  shipped as `Machine::rxBytes()` + `shouldPace()`.
- **PCPUT's emulated ACK window.** When the sim is the *sender*, it waits 4 *emulated* seconds per
  ACK (~8e6 cycles, calibrated to 2 MHz). Raising `clock_hz` shrinks that window in *real* time
  while the wire stays real, so this direction must eventually break — and the clock where it
  breaks is the answer. `hostrecv.exp` isolates that direction.

## Hardware setup

Two USB serial adapters wired to each other with a **null-modem** cable, 8N1:

| Port                              | Role in the rig        |
|-----------------------------------|------------------------|
| `/dev/cu.usbserial-AB0NW409`      | sys1 / host-sender end |
| `/dev/cu.usbserial-AL009KFH`      | sys2 / host-recv end   |

The device paths are hardcoded in the scripts — edit them for your adapters. **Run `cablecheck.py`
first**: if the cable or an 8-bit strap is wrong, nothing downstream means anything.

## Files

| File            | What it is                                                                         |
|-----------------|------------------------------------------------------------------------------------|
| `cablecheck.py` | Proves the null-modem cable: writes a known string each way, then all 256 byte values (XMODEM is binary — a 7-bit strap would eat this). Run this before blaming the simulator. |
| `xmodem.py`     | A real host-side XMODEM sender/receiver over a real UART (raw termios, no emulation). Speaks both checksum and CRC-16; the receiver chooses. `xmodem.py send\|recv <port> <baud> <file>`. |
| `hostsend.exp`  | **host → sim.** Host runs `xmodem.py send`; sim runs `PCGET`. Tests the sim as *receiver* — can it keep up with real data? Reports `retries=`, the honest measure. Takes `<timeout-seconds>`, env `SIM` + `BAUD`. |
| `hostrecv.exp`  | **sim → host.** Sim runs `PCPUT`; host runs `xmodem.py recv`. Tests the sim as *sender* / its ACK window. Takes `<timeout-seconds>`, env `BAUD`. |
| `sys1.toml.in`  | Machine template for the **sender** (`PCPUT`). A: is the pristine 8 MB image, read-only. `@CLOCK@`/`@BAUD@` substituted. |
| `sys2.toml.in`  | Machine template for the **receiver** (`PCGET`). A: is a writable scratch copy; C: borrows `CRC.COM` from a 77-track image. `@CLOCK@`/`@BAUD@`/`@IDLE@` substituted. |
| `bothsweep.sh`  | The main driver: HOST ↔ SIM, both directions, across a `BAUD` × `CLOCK` grid. Prints a PASS/FAIL table with seconds, bytes/s, and retries. |
| `nap6.sh`       | Bug #6's decisive experiment: sweeps `idle=true\|false` × `clock` and looks for the inversion to vanish when `idle=false`. Uses a **snapshot** binary via `$SIM` (a ~25 min run must not have the binary rebuilt out from under it). |

`Testing/Temporary/` is CTest scratch output and is git-ignored.

## Prerequisites

- `expect`, `python3`, and a built `altairsim` in `../build/`.
- Two USB serial adapters on a null-modem cable (paths above).
- The CP/M disk images referenced by the templates (absolute paths in `sys*.toml.in`).

## Running

```sh
# 0. Sanity-check the cable.
python3 cablecheck.py

# 1. Full both-directions sweep (override the grids via env).
BAUDS="9600 19200" CLOCKS="2000000 0" ./bothsweep.sh

# 2. Bug #6 experiment against a frozen binary.
cp ../build/altairsim ./altairsim-nap6
SIM=./altairsim-nap6 ./nap6.sh
```

The `.exp` drivers can also be run alone: `SIM=../build/altairsim BAUD=9600 expect -f hostsend.exp 120`.

> **Note on paths.** The scripts carry hardcoded absolute paths (device nodes, disk images, and
> `hostrecv.exp` even hardcodes `build/altairsim` instead of `$SIM`). They were a personal bench
> rig, not a portable suite — expect to edit paths before running on another machine.
