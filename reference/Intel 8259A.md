# Intel 8259A Programmable Interrupt Controller

Source: Intel *8259A/8259A-2/8259A-8 Programmable Interrupt Controller* datasheet and
application note **AP-59**, as reprinted in the *CompuPro System Support 1 Technical
Manual* (© 1981, pp.38–61) — see `reference/CompuPro System Support 1.md`. This is a
distilled emulation reference for the chip; the board-specific wiring (the master/slave
cascade, the source assignments, the `PHANTOM*` interrupt-acknowledge trick) lives with
the board in that file and in `docs/boards/compupro-ss1.md`.

The 8259A takes eight prioritized interrupt request inputs (IR0–IR7), applies a mask and
a priority rule, drives a single INT output, and — during the CPU's interrupt-acknowledge
(INTA) — puts the vector for the winning request on the data bus. Two of them cascade to
give fifteen inputs. It is the chip behind `src/chips/intel8259a.{h,cpp}`.

---

## 1. The two ports

The chip has one address line, A0, so a card decodes it into two adjacent ports. Which
register a byte hits depends on the **initialization state machine**, not on a third
address — the same port carries ICW1 then OCW2/OCW3 once initialized.

| A0 | Write | Read |
|:--:|---|---|
| 0 | ICW1 (if D4=1), else OCW2 or OCW3 (D3 picks) | IRR or ISR (per the last OCW3) |
| 1 | ICW2/ICW3/ICW4 during init, else OCW1 (the mask) | IMR (the mask) |

Three internal registers drive everything:

- **IRR** — Interrupt Request Register: which inputs are currently asking.
- **ISR** — In-Service Register: which levels are currently being serviced.
- **IMR** — Interrupt Mask Register (OCW1): a 1 bit masks that level off.

---

## 2. Initialization — ICW1…ICW4

A write to the **A0=0** port with **D4=1** is ICW1 and starts the sequence; the following
writes to the **A0=1** port are ICW2, then ICW3 (only in cascade mode), then ICW4 (only if
requested). ICW1 also clears IMR and ISR, resets priority to fully-nested (IR0 highest),
drops special-mask mode, points reads at the IRR, and resets the edge-sense latch.

**ICW1** (A0=0, D4=1):

| Bit | Name | Meaning |
|:--:|---|---|
| D4 | 1 | marks this byte as ICW1 |
| D3 | LTIM | 1 = **level-triggered**, 0 = edge-triggered |
| D2 | ADI | call-address interval: 1 = 4, 0 = 8 (MCS-80/85 only) |
| D1 | SNGL | 1 = single (no ICW3), 0 = cascade |
| D0 | IC4 | 1 = ICW4 will follow |

**ICW2** (A0=1): the vector base. In MCS-80/85 mode it is A15–A8 of the CALL address; in
8086 mode its high five bits (D7–D3) are the vector number's high bits.

**ICW3** (A0=1, cascade only): on the **master**, a bit set for each IR line that has a
slave attached; on a **slave**, its 3-bit slave ID (which master IR it hangs off).

**ICW4** (A0=1, if IC4=1):

| Bit | Name | Meaning |
|:--:|---|---|
| D4 | SFNM | special fully-nested mode |
| D3 | BUF | buffered mode |
| D2 | M/S | master/slave (in buffered mode) |
| D1 | AEOI | automatic end-of-interrupt |
| D0 | µPM | 1 = 8086/8088, **0 = MCS-80/85** |

---

## 3. The interrupt vector

**MCS-80/85 mode** (µPM=0) — what an 8080/8085/Z80 executes. The 8259A answers INTA with a
3-byte **CALL**: the opcode `CD`, then a 16-bit address. That address is:

- A15–A8 = ICW2;
- A7–A5 (interval 4) or A7–A6 (interval 8) = the matching bits of ICW1;
- the remaining low bits = the IR level, scaled by the interval (×4 or ×8).

So with ICW1 interval 4 and ICW2 = `02`, level *n* calls `0x0200 + n*4`.

**8086 mode** (µPM=1). The 8259A answers each INTA with a single **vector byte**:
`(ICW2 & 0xF8) | level`. No CALL, no address bytes.

---

## 4. Priority, masking and end-of-interrupt

**Fully-nested** (the default): IR0 is highest, IR7 lowest. A request interrupts only if it
is unmasked and no equal-or-higher-priority level is currently in service (its ISR bit set).
Acknowledging a level sets its ISR bit — which blocks it and everything below it until an
**EOI** clears it. Priority can **rotate**, moving the just-serviced level to the bottom.

**OCW2** (A0=0, D4=0, D3=0) — the EOI / rotate / set-priority family (D7–D5 = R, SL, EOI;
D2–D0 = a level):

| Byte | Command |
|:--:|---|
| `20` | non-specific EOI — clear the highest-priority in-service bit |
| `60`+L | specific EOI — clear level L's in-service bit |
| `A0` | rotate on non-specific EOI — clear the highest, make it lowest priority |
| `E0`+L | rotate on specific EOI — clear L, make it lowest priority |
| `C0`+L | set priority — make L the lowest priority (no EOI) |

**OCW3** (A0=0, D4=0, D3=1): the read-register select (RR=D1, RIS=D0 — the next A0=0 read
returns ISR if RIS, else IRR), the special-mask-mode enable (ESMM=D6, SMM=D5), and the poll
command (P=D2).

**OCW1** (A0=1, after init): the interrupt mask (IMR) — a 1 bit masks that level off.

---

## 5. Cascade

In a master/slave cascade the slave's INT output feeds one of the master's IR inputs (the
one whose ICW3 bit is set on the master, matching the slave's ICW3 ID). When that master
input wins an acknowledge, the **slave** supplies the vector, and **both** chips set an
in-service bit — so the handler must send an EOI to **both** the slave and the master.

---

## 6. Emulation notes

- **INT is a pure function of (programmed state, the live IR input levels).** The IR lines
  are wires driven by other chips whose outputs move on their own clocks (a timer's OUT, a
  UART's RxRDY, an S-100 VI line), so the model does not cache the request lines: it is
  handed the current levels on every query and computes — the same stance the 88-VI takes
  reading `bus.viLines()` live. In **level-triggered** mode (the usual choice for those
  level sources, and the System Support 1's default) the request register simply follows
  the pins; in **edge-triggered** mode a rising-edge latch is updated at the card's sample
  points.
- **The `PHANTOM*` interrupt-acknowledge trick may be unnecessary in a given emulator.** On
  real hardware an 8080/Z80 issues one INTA pulse and fetches the CALL's two address bytes
  as ordinary memory reads, so a board must assert `PHANTOM*` to blank memory during them
  (System Support 1 §4.4). A CPU model that instead issues an INTA cycle for *every*
  injected byte gets the address bytes from the controller directly, so there is nothing
  for `PHANTOM*` to suppress — as is the case in this simulator (see
  `docs/boards/compupro-ss1.md`).
- **Modeled:** ICW1–4 sequencing; OCW1/2/3; fully-nested priority with rotation; the EOI
  family; edge/level triggering; the MCS-80/85 CALL and the 8086 vector byte. **Special
  fully-nested mode** (ICW4 D4) is accepted but treated as ordinary fully-nested — it only
  changes how a master handles two interrupts from one slave, which the stock single-slave
  cascade does not exercise. **Automatic EOI** (ICW4 D1) and **poll mode** (OCW3 P) are not
  modeled; Intel advises against auto-EOI in a cascade and the System Support 1's own sample
  program uses neither.
