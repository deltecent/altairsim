# CompuPro CPU 8085/88

Source: [CPU 8085/88 Technical Manual.pdf](#) (CompuPro division, Godbout Electronics,
Hayward CA; component layout marked **© 1981 Godbout Electronics**, board **161 Rev F**;
manual dated **2/83**; proofread with SpellGuard). 31 pp, **real text layer**. Fetched from
s100computers.com (`.../Hardware Manuals/CompuPro/CPU 8088-88 Manual.pdf`).

The **CompuPro CPU 8085/88** is an IEEE 696 / S-100 **dual-processor** board carrying both an
**Intel 8085A-2** (8-bit, U41) and an **Intel 8088** (16-bit-internal / 8-bit-bus, U20) on one
card. Only one runs at a time; the other is held asleep on its `HOLD` line, and software swaps
between them with a single `IN` from one shared I/O port. The 8085 runs existing 8-bit S-100
software (CP/M and friends) unchanged; the 8088 gives 16-bit power and 1 MB direct addressing
while keeping the 8-bit external bus so old hardware still works. A CompuPro **Memory Manager**
latch extends either processor to the full 16 MB (A16–A23) of the IEEE S-100 address space.
The board's own hardware synthesizes every S-100 status/control signal from the two CPUs'
native pins, so from the bus it looks like one well-behaved S-100 CPU board.

This is a distilled emulation reference: it keeps the programmer-visible model — the swap
port, the Memory Manager latch, the power-on-jump, the on-line-state options, and the
reset/initialization behavior — plus the switch/jumper settings that select them. The gate-
level circuit description (how each S-100 signal is synthesized from flip-flops and PROM U30)
is summarized to what an emulator needs; it is faithfully documented in the manual's Circuit
Description (pp. 13–19) if the timing ever matters.

---

## 1. The two processors and how they come up

| | 8085 | 8088 |
|---|---|---|
| Chip | 8085A-2 (U41) | 8088 (U20) |
| Bus width | 8-bit | 8-bit external (16-bit internal) |
| Direct address reach | 64 K (16 addr bits) | 1 MB (20 addr bits) |
| Speed (standard board) | **2 or 6 MHz**, switch-selected (S4) | **8 MHz**, fixed |
| First-time entry point | `0000H` (or power-on-jump target, if enabled) | `FFFF0H` |

- **On power-up the 8085 is always in control** and running; the 8088 is held (asleep) "just
  as if it were never turned on." The first time each processor comes on line it runs its
  **normal power-up sequence regardless** of the Reset-On-Swap switches (§3, S1-4/S1-5).
- The 8085's normal power-up entry is `0000H`; with power-on-jump enabled it instead executes
  a forced `JMP` to a 256-byte page boundary (§4).
- The 8088's first-ever entry is `FFFF0H` (its architectural reset vector).
- Speeds "apply to the standard board" — a given board may have different crystals for faster
  operation. **The 8088 always runs at 8 MHz** on the standard board; S4 affects the 8085 only.

## 2. The swap port and the Memory Manager port — one address, two sides

**A single I/O port serves both the processor swap (read side) and the Memory Manager (write
side).** Its address is set by DIP switch **S3** (§3). The **CompuPro standard is port `FDH`**;
all CompuPro-supplied software assumes it.

- **Swap (INPUT from the port):** any `IN` from the port toggles which CPU is on line. The
  value read is meaningless — the manual says `FFH` is returned in the accumulator — but **the
  `IN` destroys A**, so save it first (a `PUSH PSW` before, `POP PSW` after) if it matters. The
  swap is edge-triggered off the port access, not off the data.
- **Memory Manager (OUTPUT to the port):** an `OUT` latches a byte that becomes the upper
  S-100 address bits, extending the current processor's reach to 16 MB (see §5).

Both functions decode the same address (S3), so they are configured together.

### What happens when a processor "comes on line"

The first time is always the normal power-up sequence. **Every time thereafter**, each CPU's
behavior on coming on line is chosen (per-CPU) by the Reset-On-Swap switches:

- **RESET mode** (S1-4 `ON` for the 8085 / S1-5 `ON` for the 8088): a RESET is issued as the
  CPU comes on line, so it restarts — the 8088 at `FFFF0H`; the 8085 at `0000H`, or a
  power-on-jump if enabled. Useful for development.
- **Sleep / resume-in-place mode** (S1-4 `OFF` / S1-5 `OFF`): the CPU simply picks up where it
  left off, "as if nothing had happened." Often more practical in a real-time system.

The two CPUs can be in different modes. **A typical hand-off** (manual's example, sleep mode
both): 8085 sets up the 8088's code at `FFFF0H`, `PUSH PSW`, `IN FDH` → 8085 sleeps and 8088
wakes at `FFFF0H`; 8088 does its work, leaves task info in RAM, `IN FDH` → 8085 resumes,
`POP PSW`. **Rule of thumb:** a processor should not modify the other's execution or stack area.

## 3. Switches and jumpers

Four switch banks and a set of option jumpers. Physical locations are given for orientation;
only the resulting programmer-visible model matters to an emulator.

### S1 — mode / option DIP (8-position, between U6 and U7)

| Pos | Label | Function | `ON` | `OFF` |
|:---:|:---:|---|---|---|
| 1 | **XA3** | Tri-state A16–A23 when `ADSB*` asserted (during DMA) | device supplies all 24 bits | most older DMA — leave off |
| 2 | **XAC** | Clear the extended-address (Memory Manager) latch on `RESET*` | cleared on RESET | left unchanged on RESET |
| 3 | **IOW** | Insert **one wait state into every I/O cycle** | wait state enabled | inhibited |
| 4 | **5RS** | 8085 **Reset-On-Swap** | 8085 reset when it comes on line | 8085 resumes in place |
| 5 | **8RS** | 8088 **Reset-On-Swap** | 8088 reset when it comes on line | 8088 resumes in place |
| 6 | **JOR** | 8085 power-on-jump on **every** `RESET*` vs. power-on only | jump on every reset | jump at power-on only |
| 7 | **MW** | Board generates the S-100 `MWRITE` signal | board drives MWRITE | inhibited (front panel / other generator present) |
| 8 | **POJ** | Power-on-jump feature enable | enabled | disabled |

- The extended-address latch is **always cleared on power-up** regardless of XAC; XAC only
  governs whether a later `RESET*` also clears it.
- **IOW** is meant for 6 MHz operation, where older I/O boards (especially UART cards) may not
  keep up without a wait state.
- **⚠ With DMA devices, both 5RS and 8RS MUST be OFF.**
- **MW**: the IEEE S-100 rule is exactly one MWRITE generator per system. Turn MW `OFF` if a
  front panel (e.g. IMSAI 8080) or another CPU card already generates MWRITE; `ON` otherwise.

### S2 — power-on-jump address DIP (8-position, between U25 and U26)

Sets **A8–A15** of the page the 8085 jumps to on power-on (a 256-byte-page boundary in the
lower 64 K). **`ON` = "1", `OFF` = "0".**

| Pos | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Addr bit | A8 | A9 | A10 | A11 | A12 | A13 | A14 | A15 |

Example — jump to `E000H`: positions 1–5 `OFF`, positions 6–8 `ON`.

### S3 — Memory Manager / swap port address DIP (8-position, between U33 and U34)

Sets **A0–A7** of the shared port address. **Polarity is inverted vs. S2: `ON` = "0",
`OFF` = "1".**

| Pos | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Addr bit | A0 | A1 | A2 | A3 | A4 | A5 | A6 | A7 |

**For the standard port `FDH` (1111 1101): position 2 `ON`, all other positions `OFF`.**

### S4 — 8085 clock speed (paddle switch, red handle, upper-right corner)

- **Left-most = 2 MHz** (safe starting point; needed with slow memory, older hardware such as
  the IMSAI front panel, or software with undocumented 2-MHz timing loops).
- **Right-most = 6 MHz.**
- Affects the **8085 only** — the 8088 is always 8 MHz. **Not designed to be changed while
  running** ("may work while running, but your program may also bomb").

### Option jumpers

| Jumper | Location | Purpose |
|---|---|---|
| **J1** | — | processor connector (see layout) |
| **J2** | upper-right, 16-pin socket | **IMSAI-type front panel** cable plugs in here |
| **J6** | lower-left corner | **install only for an IMSAI-type front panel.** Installing J6 makes the board **no longer meet the IEEE S-100 spec.** Also set S1-7 (MW) `OFF` when using a front panel |
| **J7** | between U16 and U17, pads **A/B/C** | **Reset option.** Shipped with **A–B connected** (a trace on the solder side) → both CPUs reset on `POC*`/`RESET*`/reset button. To make the **8088 reset only at power-on** (reset button no effect on it): cut the A–B trace, jumper **B–C**. Normal use leaves A–B. |

## 4. Power-on-jump

When enabled (S1-8 POJ `ON`), the board makes the 8085 start execution somewhere other than
`0000H` by **forcing three bytes onto the data-in bus at addresses 0, 1, 2** as the 8085 fetches
its first instruction:

| Fetch address | Byte forced | Meaning |
|:---:|:---:|---|
| 0 | `C3H` | 8085 `JMP` opcode |
| 1 | `00H` | low byte of jump target (page-aligned) |
| 2 | **S2 setting** | high byte of jump target (the page, A8–A15) — see §3 |

After the third forced byte, normal bus reads resume, so the jump lands on the `xx00H` boundary
selected by S2. The circuit re-enables after `POC*`, and — if **JOR** (S1-6) is `ON` — after
every `RESET*` as well; JOR `OFF` restricts it to power-on. If POJ is disabled, the 8085 starts
at `0000H` as usual.

## 5. Memory Manager (16 MB extended addressing)

An 8-bit **output** to the shared port (§2) is latched (U28, 74LS273) and driven onto the S-100
**A16–A23** lines, so an otherwise-16-bit processor reaches the full IEEE S-100 16 MB. This is a
bank-select-style scheme, but the latch lives in **one** place in the system (on this CPU card)
rather than being duplicated on every memory board — the physical memory boards are standardized
because A16–A23 are IEEE-defined bus lines.

- **With the 8085 in control:** all 8 latched bits appear on A16–A23. The 8085 provides A0–A15.
- **With the 8088 in control:** the 8088 natively drives A0–A19 (its 20 address bits map to
  A0–A19 on the bus), so **only the upper four latched bits are used** → they drive A20–A23,
  while A16–A19 come straight from the 8088. The full byte is still latched, and **all eight
  bits reappear on the bus when the 8085 comes back on line.**
- **The latch is always cleared to 0 on power-up.** It may also be cleared on each bus `RESET*`
  depending on S1-2 (XAC). Its outputs tri-state on `POC*`, `RESET*`, or `ADSB*` (the latter can
  be ignored by the Memory Manager via a DIP position).

## 6. S-100 bus interface (synthesized signals)

Neither the 8085 nor the 8088 emits S-100 signals directly, so the board synthesizes them. An
emulator modeling this at the pin level should know:

- **Data:** both CPUs' bidirectional data buses are tied and buffered (DO via U37, DI via U38).
  The DI buffer is disabled by `POJ*` or by the S-100 **RUN** line (IMSAI-type panels force data
  in over J2 while RUN is asserted). A pull-up keeps RUN benign on systems that don't drive it.
- **Address:** the 8085 multiplexes A0–A7 on its data bus during the first part of a cycle; a
  74LS373 (U35) latches them under the common **SYSALE** signal (the OR of both CPUs' ALE).
  A8–A15 are buffered (U36) onto the bus. U35/U36 tri-state on `ADSB*`.
- **Status:** decoded through PROM **U30 (G165)** from the active CPU's native status pins —
  8085: `S0`, `S1`, `IO/M*`; 8088: `SSO`, `IO/M*`, `DT/R*`. The internal **`8/5*`** signal (high
  when the 8088 is in control) selects which decode the PROM uses. Status buffered by U39,
  tri-stated by `SDSB*`.
- **Control:** `pSYNC` (from a two-flop SYSALE delay), `pSTVAL*` (pSYNC NANDed with inverted
  system clock), `pDBIN` (from either CPU's `RD*` **or** `INTA*`, gated by pSYNC), `pWR*` (the
  CPUs' `WR*`, delayed one clock so data is valid before the leading edge per S-100), `pHLDA`,
  and `MWRT` (`pWR*` NOR `sOUT`, DIP-disconnectable). `NMI*`/`pINT*` are steered to whichever CPU
  is on line via the `8/5*` line.
- **Ready / wait:** both S-100 `RDY` lines are ANDed with the on-board I/O wait-state generator
  (S1-3 IOW). The wait generator fires **only on I/O cycles** (its clear is tied to `IO/M*`,
  inactive during memory cycles), injecting exactly one wait state per I/O cycle when enabled.
- **Clocking:** the 8284 (U19) clocks the 8088; a separate oscillator (U8) built from a 4 MHz
  crystal provides the S-100 2 MHz clock on pin 49 and the 8085's 2 MHz option; the 8085's 6 MHz
  option comes from its built-in clock generator off a crystal (see the crystal note below).
  When the active processor changes, the system clock source is switched to match (U12a + U24).

## 7. Emulation defaults and gotchas

- **Default the swap / Memory Manager port to `FDH`** (CompuPro standard; S3 pos 2 `ON`, rest
  `OFF`). All CompuPro software assumes it.
- **On reset/power-up:** 8085 in control at `0000H` (or POJ target), 8088 held; the extended-
  address latch cleared to 0.
- **`IN FDH` (or wherever S3 points) is the swap trigger and clobbers A** — the returned byte is
  garbage (`FFH`). Model it as a side-effecting read, not a data read.
- **`OUT` to that same port is the Memory Manager**, not the swap; the write side and read side
  are independent functions on one address.
- **8088 vs 8085 address extension differ:** with the 8088 in control only the top 4 latch bits
  matter (A20–A23); with the 8085 all 8 do (A16–A23). Getting this wrong silently mislocates 8088
  memory above 1 MB.
- **Wait state is I/O-only** and one cycle, gated by S1-3 — do not apply it to memory cycles.
- **⚠ The crystal values contradict between the two sources in this manual.** The Circuit
  Description (p. 13) says the 8284 divides a **24 MHz** crystal (X2) by three for the 8088's
  8 MHz, and the 8085's 6 MHz comes from a **12 MHz** crystal (X3). The Parts List (p. 22) lists
  **X2 = 15 MHz** and **X3 = 10 MHz** (which would give 5 MHz, not 8/6). X1 = 4 MHz agrees in both
  (the 2 MHz bus/8085 oscillator). This is an internal inconsistency in the document — the prose
  matches the cover's "8088 at 8 MHz / 8085 at 6 MHz" claim and is the more likely intent; treat
  the Parts List figures as suspect. It does not affect the emulated programmer's model, only any
  cycle-accurate clock model. Do not silently pick one — flag it if timing ever depends on it.

## 8. Key ICs (Parts List, p. 22)

| Ref | Part | Role |
|---|---|---|
| U41 | 8085A-2 | 8-bit CPU |
| U20 | 8088 | 16-bit CPU (8-bit bus) |
| U19 | 8284 | 8088 clock generator |
| U30 | G165 | status-decode PROM (8085 vs 8088 status via `8/5*`) |
| U34 | 25LS2521 | 8-bit comparator — swap/MM port address decode |
| U28 | 74LS273 | Memory Manager extended-address latch |
| U33 | 74LS373 | A16–A23 extended-address output latch |
| U29 | 74LS157 | 8085/8088 nibble mux into the MM latch |
| U35 | 74LS373 | A0–A7 system address latch (SYSALE) |
| U7 | 74LS221 | dual one-shot — Reset-On-Swap pulse (~2 µs, R3/R4/C5/C6) |
| U42, U43 | 7805 | on-board +5 V regulators |

Crystals: **X1 = 4 MHz** (agreed). X2 / X3 — see the §7 contradiction note.
