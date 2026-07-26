# MITS 88-LPC — Line Printer Controller

**Status:** implemented, `type = "lpc"` — **polled** (see *Limitations*)

## The real hardware

The S-100 interface card that drove the **88-LP line printer** (MITS, © 1975) — a line printer
built on an **Okidata** mechanism, an 80-column, 64-character uppercase USASCII subset. The board
is jumperable to any of **128** base addresses (jumpers E1–E7 → address lines A1–A7); the MITS
default, which MITS software requires, is **002 octal**.

Like the [88-C700](mits-88c700.md) it is a plain output card with **no receive path** — a printer
sends nothing back — and it uses the same even/odd two-port shape: **Control at the even base**,
**Data at the odd address above it**, with address bit A0 picking the channel (so the base is
always even).

But it is **not** a transparent byte pipe like the C700. The 88-LPC speaks a **line-oriented
command protocol**:

- The **data channel** (odd) loads a **6-bit character code** at a time into the printer's
  80-character line buffer. The code is not finished ASCII — the 64-char set (0x20–0x5F) is packed
  into six bits with **bit 6 = the complement of bit 5**.
- The **control channel** (even) is **active-high command strobes** (not a static control word):
  **D0 PRINT**, **D1 LINE FEED**, **D2 CLEAR**, **D3 interrupt enable**. A line commits on PRINT or
  when the buffer fills to 80 characters.

## Sources

| Source | Path | Authority |
|---|---|---|
| *88-LPC Board Documentation* (LINE PRINTER CONTROLLER), MITS, © 1975 | `reference/88-LPC Printer Interface.md` (distilled from `88-LPC Printer Interface.pdf`) | **Authoritative.** The two-port model and the A0 split, the control-command bits (§II), the status bits (§II), the 6-bit data channel (§III), and the print/line-buffer mechanics (§II, and the test program §6). |
| I/O Address Selection Chart | same, §2 | **Authoritative** for the 128 jumpered bases (E1–E7 → A1–A7); MITS software requires **002**. |
| Okidata printer glyph chart | *(not in the manual)* | **Absent.** The manual references "a chart showing the bit pattern for each character" in the *Okidata printer* manual, which is not included. The 6-bit → glyph packing is **inferred** from the standard 64-char subset and is consistent with the manual's own test program (`space = 100000 octal = 0x20`). See `docs/sources.md`. |

## Register reference

Two ports. Control at an **even** base, Data at base+1.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| BASE+0 | **Control** (active-high commands). D0 = PRINT, D1 = LINE FEED, D2 = CLEAR, D3 = interrupt enable. D4–D7 ignored. | Status word (below) |
| BASE+1 | Data — a **6-bit character code** into the printer buffer | *(write-only; reads float to `0xFF`)* |

### The control word (reference §3) — **active-high command strobes**

Unlike the C700's static, active-low control word, each bit here *performs* an action when set,
and a single write may set more than one.

| bit | name | set (HIGH) does |
|---|---|---|
| 0 | PRINT | print (commit) the buffered line |
| 1 | LINE FEED | advance the paper one line, printing nothing |
| 2 | CLEAR | discard the buffer |
| 3 | INTERRUPT | HIGH enables the interrupt structure, LOW disables it |

### The status word (reference §4) — **active high**

The manual defines bits 0–3; 4–7 are undefined.

| bit | name | HIGH means |
|---|---|---|
| 0 | BUFFER EMPTY | the buffer has room — ready for a character |
| 1 | PRINTING | **not** printing (idle); LOW = print head in motion |
| 2 | PAPER | paper feeding normally; LOW = jammed |
| 3 | LINE FEED OK | a line feed would be accepted (head not in motion) |

### The data code (reference §5)

A 6-bit code on DO0–DO5, decoded to its glyph: `ascii = (code & 0x20) ? code : (code | 0x40)`. So
`0x20`→space, `0x00`→`@`, `0x01`→`A`, `0x1F`→`_`. **This packing is inferred** — see *Sources*.

## How it is simulated

**A line mechanism, not a byte latch** (`src/boards/mits-88lpc.{h,cpp}`). The board owns a single
`ByteStream`, the port decode around it, and an 80-character line buffer that mirrors the printer's.

- **Decodes** `IoWrite` at `BASE` and `BASE+1`, but `IoRead` **at `BASE` only**. No memory. The
  direction is part of the decode: there is no `IN` at the odd data address, a printer sends
  nothing back, so on `IN BASE+1` nothing turns on and the card leaves the bus alone — the read
  floats to `0xFF` **from the bus**, the only thing allowed to say it (DESIGN.md §4.6.1, issue
  #26). `port` **must be even** — A0 selects the channel, so an odd base is refused with a reason.
- **The data channel** decodes each 6-bit code to its glyph and appends it to the line buffer. At
  80 characters the line auto-prints, exactly as the real printer starts on its own when the
  buffer fills.
- **The control channel** runs the active-high commands: CLEAR discards the buffer, PRINT commits
  it, LINE FEED emits a bare paper advance; the interrupt-enable bit (D3) is stored.
- **A committed line is the decoded characters followed by `\n`.** Printing a line advances the
  paper on the real printer — its own test program prints 64 lines with no explicit LINE FEED
  between them — so a printed line becomes a text line. This is the one deliberate departure from
  the C700's raw-byte model (see *Limitations*).
- **The status** is derived from the stream: BUFFER EMPTY (ready) tracks `writable()`; PRINTING,
  PAPER and LINE FEED OK are always "OK" because a byte-sink is never mid-stroke and cannot jam or
  run out of paper. The bits exist so the day a real parallel port reports one, it lands where the
  manual says.
- **`pump()`** forwards to the stream, so a `socket:` endpoint accepts and drains and an `out:`
  capture is flushed while the machine runs, not only at `DISCONNECT`.
- **Where the printed text goes is the operator's `CONNECT`**, not the card's business (DESIGN.md
  §7.7): an `out:` file, the `console`, a `socket:`, a real `printer:` queue, or `null`.
- Does **not** master the bus, and (see below) asserts no interrupt.

`SNAPSHOT`/`RESTORE` serialize the stored interrupt-enable **and the pending line buffer** (the
printer's not-yet-printed line is software-visible state); the port is a strap and the line is a
host handle, so neither is serialized.

## Limitations and deliberate departures

**The card renders the printed page — unlike the C700, which is byte-raw.** The C700 can pass its
line through untouched because its wire carries finished bytes whose line breaks are *data*. The
88-LPC's wire carries **6-bit codes whose line breaks are *commands***; there is no
byte-transparent reading of it. Decoding codes to glyphs and commands to lines is therefore the
*only* faithful model of this card + printer, not a transform on a transparent line (which the
design forbids, DESIGN.md §7.2). The capture is the printed page — decoded characters, one text
line per printed line, `\n`-terminated — not the raw 6-bit codes.

**The 6-bit → glyph mapping is inferred.** The Okidata printer's glyph chart is not in the LPC
manual (§5, *Sources*). The standard 64-char-subset packing is used and matches the manual's own
test program; if the chart surfaces, verify against it.

**The interrupt structure is not modeled — the card is polled.** The real 88-LPC has full hardware
interrupt capability (after each line, via the [88-VI/RTC](mits-88virtc.md) or single-level
`PINT`). Here the INTERRUPT-enable bit (control D3) is stored, but **no request is raised and no
wire is pulled**. A polled driver — load the buffer, poll BUFFER EMPTY, PRINT — is complete and is
how print routines of the era typically worked. Interrupt support is a deliberately separate
addition, deferred pending a use for it (issue #26), the same as the C700.

**PAPER and LINE FEED OK are always "OK".** A `ByteStream` sink cannot jam or be mid-stroke.
Synthesizing those states would invent a hardware event the transport does not have.

## Verification

`tests/test_lpc.cpp`, with `Bus::setVerify(true)` on:

1. **The card** — one unit `prn`, two write ports and a status read at base only, no memory, an
   **odd base refused with a reason**; the decode follows the base when `port` moves.
2. **Status is active-high** — an idle byte-sink reads ready (BUFFER EMPTY set), and the
   write-only data channel reads `0xFF` — checked for **provenance**: the card must not claim the
   read, so the `0xFF` is the bus's (issue #26).
3. **The 6-bit decode** — codes `0x01`/`0x20`/`0x00`/`0x1F`/`0x1A` land as `A`/space/`@`/`_`/`Z`,
   and bits above 6 are ignored.
4. **Line buffering** — characters accumulate silently; **PRINT** flushes `buffer + '\n'`;
   **auto-print at 80** fires without a PRINT and clears the buffer; **CLEAR** drops a pending
   line; **LINE FEED** emits a lone `'\n'` while leaving the buffer intact.
5. **Interrupt is polled** — enabling it (D3) raises no request and pulls no wire, and a printed
   line raises none.
6. **Disconnect** leaves a dead line (not a dangling pointer) and names the one real unit;
   **`connect` round-trips** the endpoint spec through the property (for `CONFIG SAVE`).

End-to-end: `machines/lineprinter-lpc.toml` loads the card at port 02; `CONNECT lpt0:prn
out:printout.txt`, then `OUT 3 <code>` to buffer characters and `OUT 2 01` to print, produces a
readable page in the host file.

## References

- `reference/88-LPC Printer Interface.md` — the distilled *88-LPC Board Documentation* (MITS, ©
  1975).
- `docs/boards/mits-88c700.md` — the Centronics printer card this one shares its two-port,
  even/odd shape with (but which is a byte pipe, where this one is a line mechanism).
- `docs/devguide/adding-a-board.md` — the guide the C700 and 88-LPC are worked examples of.
