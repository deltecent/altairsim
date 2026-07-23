# MITS 88-PIO — Parallel I/O Board

**Status:** implemented, `type = "pio"` — **polled** (see *Limitations*)

## The real hardware

The MITS **88-PIO** (June 1975) interfaces any 8-bit parallel device to the Altair 8800. It is
**discrete TTL** — a pair of 8212 8-bit latches (IC G out, IC H in) plus 74Lxx gating — **not**
a PIA, so the direction of each port is fixed in the wiring and there is no data-direction
register to program (that is the [88-4PIO](mits-884pio.md)'s job). One latch buffers the byte
going out to an output device; the other buffers the byte coming in from an input device. The
board therefore drives a printer **and** reads a keyboard at once, on two separate connectors.

Two consecutive ports, an even/odd pair like the [88-C700](mits-88c700.md): **Control/Status at
the even base**, **Data at the odd address above it**. Address bit A0 picks the channel and is
not part of the card decode, so the base is always even. Seven jumpers (I1–I7) strap the card to
any of 128 even base addresses.

## Sources

| Source | Path | Authority |
|---|---|---|
| *Parallel I/O Board Documentation*, MITS, © 1975 | `reference/MITS 88-PIO.md` (distilled from `MITS 88-PIO.pdf`) | **Authoritative.** The two-port even/odd model and the A0 split, the status bits (DI0/DI1), the interrupt-enable bits (DO0/DO1), the SBO/SBI strobe + BO/BIN handshake, and the two interrupt paths. |
| I/O Address Selection Chart | same, §5 | **Authoritative** for the base-address jumper encoding. |

## Register reference

Two ports. Control/Status at an **even** base, Data at base+1.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| BASE+0 | **Control.** DO0 = enable output-device interrupt, DO1 = enable input-device interrupt (the "BOTH" jumper ORs them). | **Status.** DI0 = output device ready, DI1 = input device has a byte. |
| BASE+1 | Data — a byte to the output device | Data — the byte the input latch holds |

### The status word (reference §3) — **ACTIVE HIGH**

| bit | name | HIGH means |
|---|---|---|
| 0 | DI0 | the output device will accept a new byte |
| 1 | DI1 | the input device has sent a byte the guest may now read |

### The control word (reference §3)

| bit | name | effect |
|---|---|---|
| 0 | DO0 | enable the output-device interrupt |
| 1 | DO1 | enable the input-device interrupt |

## How it is simulated

**A bidirectional latch pair** (`src/boards/mits-88pio.{h,cpp}`). The board owns two
`ByteStream`s — `out_` and `in_` — and the port decode around them. No chip: like the C700 there
is no character-time frame, but unlike the C700 there is a real receive path.

- **Decodes** `IoRead` and `IoWrite` at both `BASE` and `BASE+1`, no memory. *Both ports answer
  both directions* — the crucial difference from the C700. The 88-PIO has a real input latch
  (8212 IC H) that drives the bus on `IN BASE+1`, so claiming that read is correct: the card
  genuinely drives it. (When nothing is connected the latch holds 0 — a real input port with no
  device reads its floating lines.) `port` **must be even** — the decode ignores A0 and uses it
  to pick the channel, so an odd base is refused with a sentence saying why.
- **The status** is derived from the streams: DI0 tracks the output line's `writable()`; DI1 is
  the input latch's full flag.
- **A byte written to `BASE+1`** goes out on the output line verbatim. **A byte read from
  `BASE+1`** hands over the latched input byte and empties the latch.
- **`pump()`** is the one door to the outside world (DESIGN.md §7.1): it drains the output line
  and pulls **one** byte off the input line into the latch when the latch is empty. The latch is
  one byte deep — modeling the 8212 H set by the device's strobe — so DI1 stays true until the
  guest reads the data port.
- **The line is raw, 8-bit clean.** No transform chain — that belongs to the console alone
  (DESIGN.md §7.2).
- **Two connectable lines, `out` and `in`**, like the Sol-PC's printer and keyboard. Where the
  bytes go is the operator's `CONNECT` (DESIGN.md §7.7): a `file:` to capture a printout, the
  `console` or a `socket:` for a keyboard, `loopback`, or `null`. Each line round-trips its
  endpoint through a per-unit `connect` property for `CONFIG SAVE`.
- Does **not** master the bus, and asserts no interrupt.

### Reset

- `Reset::PowerOn` (POC*, cold) and `Reset::Bus` (RESET*, warm) both clear the interrupt-enable
  bits and empty the input latch. **The lines stay connected** — a reset does not unplug a device.

## Quirks reproduced

- **The odd data port reads.** This is the exact converse of the C700's issue-#26 rule: the C700
  is output-only and must *not* claim `IN` at its data port, but the 88-PIO's input latch does
  drive it, so it must. Get this wrong (float the read, or return a manufactured `0xFF`) and a
  keyboard driver reads nothing, or the bus's "nobody answered" value is impersonated.
- **One-byte input latch.** DI1 goes true when a byte arrives and false when the guest reads the
  data port; the next byte is delivered on the next `pump()`. A driver that reads the data port
  without polling DI1 will re-read a stale byte — which is exactly what the hardware does.

## Limitations and deliberate departures

**The interrupt structure is not modeled — the card is polled.** The DO0/DO1 enable bits are
stored (so a snapshot round-trips them and a future interrupt model can read them), but **no
request is raised and no wire is pulled**. A polled driver — poll DI0/DI1, move a byte — is
complete and is how parallel routines of the era typically worked. Interrupt support is a
deliberately separate addition, consistent with the C700's stance (issue #26).

**No SBO/SBI/BO/BIN handshake lines.** The real card exposes strobe inputs and ready outputs to
coordinate with the external device. Through a `ByteStream` those are subsumed by
`readable()`/`writable()` and the one-byte latch; there is no separate handshake wire to model.

## Verification

`tests/test_pio.cpp`, with `Bus::setVerify(true)` on:

1. **The card** — two units `out` and `in`, both serial.
2. **Decode** — an even/odd pair answering **both** directions on **both** ports, not the port
   after them, no memory; an **odd base is refused with a reason** and the decode follows the base.
3. **Status is active-high** — idle DI0 set (output ready), DI1 clear (nothing waiting).
4. **Output** — a byte written to the data port lands on the output line, in order, and the line
   is raw (CR/LF/SO/DEL pass through).
5. **Input** — a byte from the input device is latched on `pump()`, sets DI1, is read back from
   the data port, and clears DI1; the latch is one byte deep (one `pump()` per byte).
6. **The control channel** — the interrupt-enable bits are stored, raise **no** request and pull
   **no** wire (the polled-card contract); the status word is DI0/DI1 and nothing else.
7. **`connect`/`disconnect`** — round-trips the per-unit endpoint spec and names the real units.

## References

- `reference/MITS 88-PIO.md` — the distilled *Parallel I/O Board Documentation* (MITS, 1975).
- `docs/boards/mits-884pio.md` — the programmable 6820-based cousin.
- `docs/boards/mits-88c700.md` — the output-only card this one borrows its even/odd shape from.
