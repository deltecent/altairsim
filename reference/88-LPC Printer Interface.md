# MITS 88-LPC — Line Printer Controller

Source: [88-LPC Printer Interface.pdf](#)

MITS, Inc. "88-LPC Board Documentation" (LINE PRINTER CONTROLLER), © 1975. The 88-LPC is the
S-100 interface board that couples an Altair 8800-series computer to the **88-LP line printer**
(an Okidata line-printer mechanism, 64-character uppercase set, 80 columns). Like the
[[88-C700 Centronics Printer Controller]] it occupies **two consecutive I/O ports** — a
control/status port and a data port — but where the C700 is a transparent byte pipe, the 88-LPC
speaks a **line-oriented command protocol**: the guest loads a 6-bit character code at a time
into the printer's 80-character buffer, and a separate PRINT command (or the buffer filling)
commits a line. It shares the even/odd two-port shape with the [[MITS 88-PIO]] and the C700.

This file captures everything needed to *emulate* the board and the printer-visible behavior of
the 88-LP. Assembly, parts lists, the schematic, and the cable/connector wiring from the manual
are omitted.

---

## 1. Quick reference for emulation

| Item | Value |
|------|-------|
| I/O ports | **Two consecutive** — an even Control port and the next odd Data port |
| Base select | 128 bases via jumpers E1–E7 → address lines A1–A7 |
| Address bit | **A0 = 0 → Control channel; A0 = 1 → Data channel** |
| MITS software default | **Base 002** → `IN 2`=status, `OUT 2`=control, `OUT 3`=data (MITS software *requires* it) |
| `IN` even | Read **status byte** (§4): D0 buffer-empty, D1 not-printing, D2 paper-ok, D3 line-feed-ok |
| `OUT` even | Write **control byte** (§3): D0 PRINT, D1 LINE FEED, D2 CLEAR, D3 interrupt enable |
| `OUT` odd | Load one **6-bit character code** into the printer's 80-char buffer |
| Print trigger | A PRINT command **or** the buffer filling to **80 characters** (auto-print) |
| Interrupt | Full hardware interrupt (after each line printed): 88-VI vectored (levels 0–7) or single-level via `PINT` |
| Printer | 88-LP line printer (Okidata), **64-char** uppercase USASCII subset, **80 columns** |

Unlike the C700, the control bits are **active-HIGH command strobes** (a set bit *does the
thing*) rather than a static control word — and the data channel carries **6-bit codes**, not
finished ASCII bytes.

---

## 2. Address selection

The board decodes an 8-bit device address. Seven jumpers **E1–E7** strap it to any of **128**
bases by connecting to address lines **A1–A7** (true or complemented); the manual's I/O Address
Selection Chart gives the octal value for each combination. Address bit **A0** is *not* strapped —
it picks one of the two consecutive ports the board needs:

- **Control channel** — the even base (A0 = 0). `OUT` writes a command; `IN` reads status.
- **Data channel** — the odd address above it (A0 = 1). `OUT` loads a character code into the
  printer buffer.

> **Note (manual):** "Address 2 should be selected if MITS software compatibility is required."
> With base 002: `INPUT 2 = STATUS`, `OUTPUT 2 = CONTROL`, `OUTPUT 3 = DATA`. The manual's test
> program (§6) uses exactly ports 2 and 3.

Device-select decode (Theory of Operation §I): the eight low address bits feed comparators; when
they match the strapped value the board-select line `N-8` goes low, enabling the port. `A0` then
routes the cycle to the control channel (A0 = low) or the data channel (A0 = high).

---

## 3. Control channel (`OUT`, even address)

An `OUTPUT` to the even address drives the control byte. The manual (§II) defines data lines
**DO0–DO5**; each is an **active-high command** — set the bit to perform the action. More than one
may be set in a single write.

| Bit | Name | HIGH (set) does | LOW does |
|-----|------|-----------------|----------|
| 0 | PRINT | Print the buffer (commit the current line) | nothing |
| 1 | LINE FEED | Advance the paper one line | nothing |
| 2 | CLEAR | Clear the printer's character buffer | nothing |
| 3 | INTERRUPT | **HIGH = enable** interrupts; **LOW = disable** | disable |
| 4 | — | Not used | |
| 5 | — | Not used | |

- **PRINT (D0).** Commits whatever is in the 80-character buffer as a printed line. Used to print
  a line of **fewer than 80 characters**; an 80-character line prints automatically (§6). On the
  hardware this drops `RUN` low and printing begins; the buffer clears and the head returns HOME.
- **LINE FEED (D1).** Advances the paper one line with no printing. (Accepted only when the print
  head is not in motion — see status bit 3.)
- **CLEAR (D2).** Discards the buffer contents without printing.
- **INTERRUPT (D3).** HIGH arms the interrupt structure, LOW disarms it. With interrupts enabled,
  an interrupt occurs after each line is printed and is reset each time a PRINT command is sent
  (§II.A). See §7.

---

## 4. Status channel (`IN`, even address)

An `INPUT` from the even address returns the status byte (§II). It reads **true-sense** (active
high): a set bit means the named condition *is* so. The manual defines **bits 0–3**; bits 4–7 are
undefined.

| Bit | Name | HIGH (1) means | LOW (0) means |
|-----|------|----------------|---------------|
| 0 | BUFFER EMPTY | Printer buffer is empty — ready for a character | Buffer full |
| 1 | PRINTING | **Not** printing (idle) | Print head in motion |
| 2 | PAPER | Paper feeding normally | Paper has jammed |
| 3 | LINE FEED OK | Line feed will be accepted (head not in motion) | Line feed is taking place |

> **Note on bit 0 (manual):** the BUFFER EMPTY signal "goes active *after* the first character has
> been output to the printer and inactive after the 80th character has been sent." It is the
> "buffer has room / last character taken" flag a polling driver watches while loading the line.

A polled driver is complete: load a character to the data channel, poll BUFFER EMPTY, repeat; on
a short line issue PRINT; watch PRINTING (bit 1) go idle to know the line is done.

---

## 5. Data channel (`OUT`, odd address) — the 6-bit character code

An `OUTPUT` to the odd address strobes a character into the printer's 80-character buffer (§III).
Only **DO0–DO5** reach the buffer — a **6-bit code**. The manual: "When a character consisting of
a 6-bit ASCII word is output to the data channel, it is transmitted to the printer… The line
printer manual contains a chart showing the bit pattern for each character."

The printer's 64-character set is the ASCII columns **0x20–0x5F** (space through `_`, i.e. space,
punctuation, digits, and uppercase A–Z). Packed into six bits, the standard line-printer encoding
makes **bit 6 the complement of bit 5**:

```
code  = DO0..DO5                     (6 bits, 0x00–0x3F)
ascii = (code & 0x20) ? code : (code | 0x40)
   0x20 -> 0x20 ' '     0x00 -> 0x40 '@'     0x01 -> 0x41 'A'
   0x1F -> 0x5F '_'     0x3F -> 0x3F '?'     0x1A -> 0x5A 'Z'
```

> **⚠ Inference — flagged.** The exact 6-bit → glyph chart lives in the *Okidata printer* manual,
> which the 88-LPC document *references but does not contain*. The mapping above is the standard
> 64-character-subset packing and is **consistent with this manual's own test program**, which
> emits `space = 100000 octal = 0x20` and expects a blank line (§6). It is recorded as an
> inference in `docs/sources.md`; if the Okidata chart ever surfaces, verify it against this.

---

## 6. Print and line-buffer mechanics

The 80-character buffer lives **in the printer**; the board strobes codes into it and issues
commands. Two things commit a line:

- **Automatic** — when the buffer fills to **80 characters**, printing starts on its own ("buffer
  full, printing starts", §II.B and the test program §6).
- **Explicit PRINT** — control bit 0, used for a line **shorter than 80 characters**.

Printing a line **advances the paper**: the manual's test program (§6) prints one line for each of
the 64 characters and never issues a LINE FEED between them, so the print operation itself carries
the line to the next row. A separate **LINE FEED** (control bit 1) advances the paper *without*
printing — for blank lines and extra spacing. **CLEAR** (control bit 2) empties the buffer with no
output. `space = 100000` produces a blank printed line (§6).

**Test program (§6), for reference** — fills the buffer with 80 identical characters (auto-print),
loops over all 64 characters, using ports **2** (control) and **3** (data). It is the ready-made
emulation smoke test: each of the 64 characters should print as one line, one line blank (space).

---

## 7. Interrupt structure

The board has **full hardware interrupt capability** (§II.A). With INTERRUPT enabled (control bit
3 high), an interrupt occurs **after each line is printed** and is reset each time a PRINT command
is sent. The priority level is a hardware option:

- **Vectored** via the [[88-VI-RTC]] — 8 levels, 0 (lowest) … 7 (highest) — strapping pad `E8` to
  one of the `VI0…VI7` pads. Level *n* → `RST n`.
- **Single-level** without an 88-VI: strap `E8` to the `PINT` pad (one I/O board in the system
  only). `PINT` → `RST 7` → `0x38`.

Manual controls on the printer itself (not visible to software): a **LINE** switch (online/offline
with an indicator lamp — the computer has the printer only when lit) and a **FEED** switch
(manual paper advance, works only offline).

---

## 8. 88-LP printer characteristics (context)

- Line printer built on an **Okidata** mechanism (parts list: "Okadata Printer with Controller").
- **80 columns**; an 80-character line buffer that prints on command or when full.
- **64-character** uppercase USASCII subset (ASCII 0x20–0x5F).
- Manual **LINE** (online/offline) and **FEED** (paper advance) switches on the printer.
- MITS recommends 20 lb., 4-part paper, 9-inch hole spacing.

For emulation, a faithful model keeps an 80-character line buffer, decodes each 6-bit data code to
its ASCII glyph (§5), emits the buffer as a line on PRINT or at 80 characters, treats LINE FEED as
a bare line advance and CLEAR as a buffer discard, and exposes the §4 status bits (BUFFER EMPTY
tracking whether the sink will take a byte; the rest reported OK, since a file cannot jam or run
out of paper). The interrupt structure is optional and typically left unmodeled for a polled
driver.
