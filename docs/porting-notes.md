# Porting notes — what the prior work taught us

Lessons from the Python prototype (`../AltairClaude/cpm_sim`), its `SIMULATOR.md` and `CLAUDE.md`, and `mits_dsk.c`. **Read this before writing the CPU or the disk controller.**

## What to steal

### 1. Idle detection — this is what makes automation work

A CP/M program waiting at a prompt never halts; it spins on the SIO status port forever.

The prototype counts **consecutive console-status reads that return RDRF=0 with no intervening I/O**. *Any* other activity — a data read, a char write, any disk port access — resets the counter. Past a threshold (default 1000), the machine is **provably parked at a prompt and its output has settled**.

Two payoffs:
- The run loop reports `idle`, so automated builds (M80/L80) terminate promptly on error instead of burning 20M steps.
- **The host process can sleep instead of emulating a spin loop** — which is what stops a CP/M prompt from pinning a host core.

### 2. The `send` / `expect` / `screen` agent API

The prototype's best idea, and the right shape for the MCP surface:

- `send(text)` — queue keystrokes into the serial input buffer
- `expect(pattern, max_steps, idle_threshold) -> (found, steps)` — run until the pattern appears in a rolling window of console output; on failure, return the tail of captured output
- `run(max_steps, idle_threshold) -> (reason, steps)` with `reason ∈ halt | breakpoint | idle | max_steps`
- `screen()` — a **VT100/ANSI screen emulator** so a test can assert on a *screen grid* rather than a byte stream. Essential for testing full-screen guest apps (editors).

### 3. The `ESC[6n` DSR reply

The prototype sniffs the output stream for a Device Status Report query and injects `ESC[<rows>;<cols>R` into the input buffer, so guest programs can discover terminal size. Keep it — but as a **`Console` property**, not a hardcoded sniff.

### 4. `mits_dsk.c`'s device model

Its `s100_bus_addio(port, count, handler, name)` / `s100_bus_remio(...)` shape is the right instinct, and its `DEBTAB` debug masks (`IN_MSG`, `OUT_MSG`, `READ_MSG`, `WRITE_MSG`, `SECTOR_STUCK_MSG`, `TRACK_STUCK_MSG`) are the model for the `Log`/`Trace` category masks.

> **Note:** `s100_bus.h` **does not exist anywhere in the tree.** That API was aspirational. Defining it properly is the core of this project.

---

## What NOT to inherit

### 1. The RLC/RRC sentinel bug — the reason `step()` returns a struct

In the Python core, a carry-bit local named `cy` **shadowed the cycle-count variable** also named `cy`. `step()` therefore returned `0`, which the run loop interpreted as **"breakpoint hit"** — silently killing M80 and L80 after ~2.8M steps.

The bug was not the shadowing. The bug was **using a sentinel return value** (`0` = breakpoint, `-1` = halt) that a plausible typo could forge.

> **Therefore: `step()` returns `StepResult { uint32_t tStates; Status status; }`. Never a sentinel.**

### 2. An unvalidated CPU

The prototype's own notes say `DAA` is "complex, not fully tested" and that **TST8080 / 8080PRE / 8080EXM / CPUTEST were never run** — with the comment "These MUST be run before trusting the emulator."

**Validation is a hard CI gate (milestone 2), not a to-do.**

### 3. No interrupts at all

`EI`/`DI` set `self.interrupts_enabled`, and **nothing ever reads it.** There is no `interrupt()` method, no INT pin, no vector injection. This is what happens when a simulator is built polled and interrupts are "added later."

**This is why the full `pINT` / `IntAck` / floating-bus-RST-7 path is in milestone 1**, driven by a board (the 88-2SIO) that genuinely needs it.

### 4. Linear-scan dispatch

The prototype decodes by bit pattern in a long if/elif chain (`elif op & 0xC7 == 0x06:` …). Compact, but it is the worst performance shape available. **Use a 256-entry table or a `switch`.**

### 5. A flat 64K bytearray for memory

No banking, no ROM, no write protection, no PHANTOM, no board abstraction. Every address is RAM. There is nothing to reuse here.

### 6. The BDOS/BIOS shims

`bdos.py`, `bios.py`, `cpm.py`, `cpm_real.py` are an evolutionary dead end (the author's "real BDOS at 0xB606" investigation was a misdiagnosis — `AltairSystem` boots the same real CP/M binary and runs M80/L80 fine). **Do not port them.** Boot real CP/M on the emulated 8080, as `AltairSystem` does.

---

## Traps that will bite you

### The BIOS track-buffer flush requires CONIN

The 8 MB Altair CP/M BIOS does **not** write to the DCDD when CP/M closes a file. BIOS WRITE only copies into an in-memory `trkBuf` (32 × 137 = 4,384 bytes, each slot prefixed with a status byte: 0x00 = good, 0xFF = undefined) and marks it dirty.

**The actual port-0x0A write happens in `invFlush`, called from the BIOS CONIN entry** — the BIOS uses console input as its flush trigger.

> **Consequence: never flush the disk image right after a CP/M file operation.** The directory update from BDOS Close sits in `trkBuf` until the next BDOS function 1. **Run back to the `A0>` prompt first.**

### The DCDD's dirty-buffer write-back ordering

`out08` (select), `in09` (sector read), and `out09` (step) **all flush the dirty buffer first**, in that specific order relative to invalidating the sector/byte position. Getting this wrong **silently corrupts disks**.

Also: partial-sector writes (133-byte system-track sectors that never reach the full 137) must not be lost — write bytes through as they arrive.

### Delay physical drive select until READ/WRITE

Don't seek on select alone.

### BDOS clobbers every register

A, B, C, D, E, H, L — only SP is preserved. This repeatedly bit the prototype's author in guest assembly (print loops where HL went to 0, the code then read `mem[0]` = `0xC3`, and spewed garbage forever).

**Always call BDOS at 0005H, never BIOS directly** — BIOS entry points have no register contract.

### Guest toolchain trivia

- **M80 symbols are only 6 characters significant.** The #1 gotcha.
- `%Mult. Def. Global` is a *warning*, not an error.
- Use `CSEG`, never `ORG 0100H`.
- CP/M's command line is capped at 127 chars.

---

## Reference: the 56K CP/M memory map (binary-verified)

| | |
|---|---|
| TPA | 0x0100 – 0xADFF |
| CCP base | 0xAE00 |
| BDOS base | 0xB600 (**entry 0xB606**) |
| BIOS base | 0xC400 |
| BIOS jump table | 0xC580 (17 × 3 bytes) |
| BIOS cold start | 0xC47A |
| WBOOT | 0xC4CC |

The 2nd-stage boot loader is read from **track 0, sectors 1–2** (229 bytes, ORG 0), loaded to 0xAE00, then jumps to the BIOS cold start.
