# SD Monitor 2.10 — SD Systems SBC boot/debug PROM (`builtin:sdmonv21`)

The onboard **monitor PROM** for an SD Systems SBC-100 / SBC-200 Z80 single-board
computer. It is a full Z80 monitor — memory, port and program commands plus disk
boot — that runs from the board's PROM window and boots **SDOS / COSMOS** or CP/M
from a [VersaFloppy](../DDB200) controller through the DDBIOS at `F000h`.

This is the **SD** build: its console is the **SD Systems VDB-8024 video display
board**, an intelligent terminal that the host sees as two I/O ports —
**`00h` (status)** and **`01h` (keyboard-in / display-out)**. The monitor polls
status bit D1 for a received key (`IN 00H` / `AND 02H`) and reads the byte from
`01h`. (The sibling **MS** build, [`msmonr21`](../MSMONR21), drives an 8251 serial
console at ports `7C/7D` with CTC auto-baud instead.)

- **SD monitor Version 2.10** (the source is dated and titled "SD monitor
  Version 2.10"; `CDATA EQU 01H` / `CSTAT EQU 00H`).
- **Load address:** `E000h`. Entry at `E000h`; the DDBIOS returns to the monitor
  at **`E003h`**.
- **Decoded image:** `E000`–`E7FF`, 2048 bytes, CRC32 `4B101B4B`.

## What it does

- Prompt is a period `.` — no sign-on banner. Operands are hex only; `.` aborts
  the current command.
- Commands: `D/E/F/M/L/T/V` (memory), `I/O/P` (ports), `B/G/S/X/H` (breakpoint,
  go, single-step, register-display mode, hex arithmetic), and `C/R/W/Z/Q`
  (boot / read / write / format / read a diskette via the VersaFloppy + DDBIOS).
  Breakpoints are a 3-byte `JMP` patched over user code; a hit restores the code
  and drops into single-step.
- Keeps the guest Z80 register image in high RAM at **`FFE6h`–`FFFFh`** (SP, IY,
  IX, the alternate set, `L H E D C B`, interrupt flag, `I`, `F`, `A`, `PC`);
  `G`/`S` load it, breakpoints and steps save it. The monitor stack is at
  `STKTOP = FFC0h`. (Command syntax and the full register-save map are in
  `reference/SD Systems Monitor.md`.)

## Use it

```toml
[[board.region]]
type  = "rom"
at    = 0xE000
mount = "builtin:sdmonv21"
```

**No built-in machine uses it yet.** Like [`msmonr21`](../MSMONR21) and
[`ddb200`](../DDB200), it is embedded ahead of the SD Systems board work — the
SBC-100/200 board and the VDB-8024 video console (ports `00/01`) are still to be
built. It ships now as a first-hand, CRC-checked image for that work to target.

## Files here

| File | What it is |
|---|---|
| `SDMONV21.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `SDMONV21.Z80` | The SD monitor 2.10 source (console `CDATA EQU 01H` / `CSTAT EQU 00H`). |
| `SDMONV21.LST` | Assembler listing — the byte-for-byte record the provenance is checked against. |

**Source:** SD Systems archive (the *SD Systems Monitor* manual and the VDB-8024
manual are on deramp.com). Command set distilled in
`reference/SD Systems Monitor.md`; the video console in
`reference/SD Systems VDB-8024.md`; provenance and the CRC32 test are in
[`docs/roms.md`](../../docs/roms.md).
