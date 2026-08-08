# A Universal I/O board on the Altair 680b

The built-in `altair680` machine is a Motorola 6800 with just its onboard console — one
6850 ACIA at `F000/F001`. This example adds the **Universal I/O board** (`680uio`), the
680b's general-purpose expansion board: a **second 6850 serial port** and a **6820 PIA
parallel port**, memory-mapped like everything on the 6800.

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
