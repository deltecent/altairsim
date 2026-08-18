# SD Systems ExpandoRAM II

Source: [SD Sales ExpandoRAM-II.pdf](#) (© SD Systems, 1979; this printing "Revision C, March 1981")

The ExpandoRAM II is an S-100 dynamic-RAM board (SD Systems / SD Sales, Dallas TX) built around
either 16K (4116) or 64K (4164) DRAMs, organized as **4 banks of 8 chips each**, with a
port-addressed **page/partition-select** scheme controlled by an on-board bipolar PROM (82S130,
U8) and an 8-position DIP switch (S3). This file is a distilled emulation reference written from a
**scanned** manual with no text layer (read as page images). It omits assembly/checkout steps,
the parts list, and the schematic/timing appendices. The payload is the page-select model
(§1–§3), which the manual scatters across Sections 2 and 4 and one out-of-order figure.

⚠ **This is the ExpandoRAM *II*.** The original ExpandoRAM (I) is an earlier, unrelated SD Systems
board with a completely different mechanism — see [`SD Systems ExpandoRAM.md`](SD%20Systems%20ExpandoRAM.md).
Do not assume the I shares this PROM/DIP page-select scheme; the I in fact has **no I/O port at
all**.

---

## 1. Page-select port (the payload)

| Port | Direction | Function |
|:---:|---|---|
| **FF** | Output | Selects the memory **page** (a 32K or 48K partition, per PROM type) presented to the CPU. The byte written is the page number. (p. 12) |

- Verbatim (p. 12): *"Port FF is used to select the Memory page (32K or 48K) to be accessed by the
  CPU… The pages are accessed by outputting the page number to port FF."*
- Up to **10 pages (0–9)** can coexist: either **6 boards with a 32K PROM** or **8 boards with a
  48K PROM** (p. 12).
- The port-FF logic **latches** the output byte on the board (p. 5 block diagram) — a persistent
  page-select latch, not a per-cycle address qualifier.
- ⚠ Section 4 points the reader to "Figure 4-2 for page mapping"; the only such figure present is
  **"Figure 2-4. PROM Program and Page Mapping"** (printed page "2-9"), bound **out of order**
  after Appendix E near the end of the PDF. Treat "Figure 4-2" and "Figure 2-4" as the same
  figure — the numbering mismatch is in the source.

---

## 2. PROM decode model (82S130, U8)

The 82S130 bipolar PROM performs the decode: it compares address lines, the board-select switch
value, and the latched port-FF page value, and asserts active-low bank-enable (RAS) outputs into
the array (p. 4–5). Two PROM variants exist for the same socket:

| PROM variant | Partition size | Boards for full page range |
|---|---|---|
| 32K boundary-select PROM | 32K window | 6 boards (pages 0–5) |
| 48K boundary-select PROM | 48K window | 8 boards (pages 0–7, + extra 16K half-pages) |

Figure 2-4 draws both variants as a grid of bank cells against a board-select value **S** (0–7),
producing a page number **P**. The cleanly legible part is the per-bank PROM output nibble:

| Bank | PROM output nibble |
|:---:|:---:|
| 0 | E (1110) |
| 1 | D (1101) |
| 2 | B (1011) |
| 3 | 7 (0111) |

— one active-low bit per bank, consistent with four RAS lines (RAS0–RAS3). The figure legend:
**S** = board-select number (0–7); **P** = page number output to port FF (0–9; "P=B,C,D,E,F IS
RESERVED"); **A** = top address bits A15,A14 selecting which bank within the page's window.

⚠ **The per-cell S/A→P mapping inside Figure 2-4 could not be reliably transcribed** — it is a
hand-annotated drawing, heavily compressed in the photocopy. Do not hand-encode a page-number
table from this scan; if the exact function is needed, take it from the schematic (Appendix D,
drawing 0100161) or a real PROM dump.

---

## 3. DIP switch S3 (8-position)

Function lines, top-to-bottom per Figure 4-1 (p. 12):

| Function | Meaning |
|---|---|
| PROM ENABLE | must be ON for the board to work |
| D0 | board-select bit 0 |
| D1 | board-select bit 1 |
| D2 | board-select bit 2 |
| Bank Enable 0 | ON = bank populated/enabled |
| Bank Enable 1 | ″ |
| Bank Enable 2 | ″ |
| Bank Enable 3 | ″ |

- **D0/D1/D2 are the 3-bit board-select number, read in octal, 0–7, ON = 0 / OFF = 1** (verbatim:
  "Board may be designated 0-7 by setting switches to octal address (ON is 0, OFF is 1)", p. 12).
- **4 Bank Enable switches** mark which physical banks (0–3) are populated.
- ⚠ Which physical switch (1–8) maps to which function is inferred from the diagram's fan-out
  layout, not printed numerals — verify against the schematic if a specific position matters.

---

## 4. Bank/memory geometry

| Item | Value | Page |
|---|---|---|
| RAM chips per board | up to 32, in 4 banks of 8 | p. 4 |
| Chip options | MK4116 (16K×1) or MK4164 (64K×1) | p. 1, 4 |
| Bank size / board (16K chips) | 16K/bank → 64K/board | p. 1–2, 4 |
| Bank size / board (64K chips) | 64K/bank → 256K/board | p. 1–2, 4 |
| Partial population | supported; Bank Enable switches mark populated banks | p. 9 |
| 16K vs 64K chip select | etch cuts/jumpers E1–E8 (a rework, not a switch) | p. 11 |

---

## 5. Reset / bus notes

⚠ **Reset/power-on behavior is not described in prose anywhere in the scan.** Connector J1 lists a
`POC` (Power-On-Clear) input at pin 99 and `PHANTOM` at pin 67 (Table 1-2, p. 3), but the manual
never says what the board does on POC — e.g. whether the page-select latch clears to page 0. Do
not invent a reset-to-page-0 behavior. Other context: optimized for the SD SBC-100/200, uses the
Z-80 RFSH signal, 4 MHz with fast enough RAMs; a phantom-disable jumper (E9–E10) and M1-wait
jumper (E14–E15) exist for non-SD CPU boards (pp. 12–13).
