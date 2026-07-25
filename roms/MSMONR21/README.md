# MS Monitor 2.10 — SD Systems SBC boot/debug PROM (`builtin:msmonr21`)

The onboard **monitor PROM** for an SD Systems SBC-100 / SBC-200 Z80 single-board
computer. It is a full Z80 monitor — memory, port and program commands plus disk
boot — that runs from the board's PROM window and boots **SDOS / COSMOS** or CP/M
from a [VersaFloppy](../DDB200) controller through the DDBIOS at `F000h`.

This is the **MS** build: its console is the board's **8251 USART at ports
`7Ch` (data) / `7Dh` (status)**, and it auto-measures the console baud rate from
the first character typed, loading the Z80-CTC (`78h`–`7Bh`) time constant to
match. (The sibling **SD** build drives a VDM video console at ports `00/01`
instead; it is not embedded yet.)

- **MS monitor Version 2.10** (SD #7140011-class firmware; the source is dated
  and titled "MS monitor Version 2.10").
- **Load address:** `E000h`. Entry at `E000h`; the DDBIOS returns to the monitor
  at **`E003h`**.
- **Decoded image:** `E000`–`E7FF`, 2048 bytes, CRC32 `64428921`.

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
mount = "builtin:msmonr21"
```

**No built-in machine uses it yet.** Like [`ddb200`](../DDB200), it is embedded
ahead of the SD Systems board work — the SBC-100/200 board with its 8251 console
at `7C/7D` and CTC at `78`–`7B` is still to be built. It ships now as a
first-hand, CRC-checked image for that work to target.

## Files here

| File | What it is |
|---|---|
| `MSMONR21.HEX` | The image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `MSMONR21.Z80` | The MS monitor 2.10 source (console `CDATA EQU 07CH` / `CSTAT EQU 07DH`). |
| `MSMONR21.LST` | Assembler listing — the byte-for-byte record the provenance is checked against. |

**Source:** `~/…/sbc200/sdsource/` (SD Systems archive; the manual,
*SD Systems Monitor*, is on deramp.com). Command set distilled in
`reference/SD Systems Monitor.md`; provenance and the CRC32 test are in
[`docs/roms.md`](../../docs/roms.md).
