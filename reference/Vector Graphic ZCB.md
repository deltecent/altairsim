# Vector Graphic ZCB — board revisions, PROM sockets and jumpers

Source: [ZCB CPU - Misc.pdf](#) and [ZCB Mod for 19.2K baud.pdf](#)

Vector Graphic bulletins on the ZCB, the Z-80 single board computer, 10 PDF pages, 1980–1982, and a
one-page note on a 19,200 baud change. The ZCB holds the processor, the Monitor PROMs, 1K of RAM at
FC00, an 8251 and an 8255. This file holds what the bulletins give. The
[ZCB CPU Manual](ZCB%20CPU%20Manual.md) file has the reset logic, the decode, the ports and the
jumpers.

**Emulated** as `vgzcb` (`src/boards/vector-zcb.h`, `docs/boards/vector-zcb.md`). The `vector` machine does not use it yet: `machines/vector.toml` has a generic Z-80 and a stand-in memory board. The ports
are in [Vector Graphic Port Map](Vector%20Graphic%20Port%20Map.md), and the addresses are in
[Vector Graphic Memory Maps](Vector%20Graphic%20Memory%20Maps.md).

---

## 1. Quick reference for emulation

| Item | Value | Source |
|---|---|---|
| Processor | Z-80A or Z-80-4 at 4 MHz, Z-80-B at 6 MHz | V3-0022 |
| Monitor PROM on later boards | One 2732 at U20, monitor, boot and printer together | V3-0022 |
| Serial | An 8251 at IC6, called U6 in PIB 0056 | 19.2K note, PIB 0056 |

---

## 2. Board identification (V3-0022, 1 Dec 1981)

| Revision | Part number | U20 | U21 | U22 | How to identify it |
|---|---|---|---|---|---|
| 1 type A, 4 MHz original | #3501-0000 | Monitor, 2708 | Monitor, 2708 | Printer, 2708 | U26 vacant, no wires in area F, standard traces only |
| 1 type B, 4 MHz | #3501-0000 | Printer, 2708 | Boot/Graphic, 2708 | Monitor, 2716 | Wire on the back from U22 pin 21 to pin 24, U26 vacant |
| 2 type A, 4 MHz | #3501-0000 | Monitor, 2716 | Boot/Graphic, 2708 | Printer, 2708 | No wires, U26 vacant |
| 3 type A, 4 MHz with read delay | #3501-0000 | Monitor, 2716 | Boot/Graphic, 2708 | Printer, 2708 | U26 has a socket. The wire from U33 pin 3 must go to U33 pin 13, not pin 12 |
| 3 type B, 4 MHz upgradable | #3501-0000 | Monitor-boot-printer, 2732 | No PROM | No PROM | U26 has an IC soldered in, U33 a socket. Trace or wire from pad 1 to pad 2 of area A. Z-80-A or Z-80-4 |
| 3 type C, 6 MHz with piggy-back board | #3501-0002 | Monitor-boot-printer, 2732 | No PROM | No PROM | Piggy-back board in the U33 socket, U26 soldered in, area A trace 1 to 2 cut. Z-80-4 as printed. See ⚠ |
| 4, 6 MHz with no piggy-back board | #3501-0002 | Monitor-boot-printer, 2732 | Only U20 has a socket | | U26 and U33 soldered in, 12.000 MHz crystal at Y-1 |

The printer manual has a note that puts a printer PROM in U22. The advisory tells the reader of
revisions 3 B, 3 C and 4 to ignore that note.

**Conversions.**

- **3 type B to 6 MHz:** remove U33 and insert the piggy-back board #3524-0000. Cut or remove the
  link from pad 1 to pad 2 of area A. The processor must be a Z-80-B.
- **3 type C to 4 MHz:** remove the piggy-back board and fit a 74LS04 (part 2501-0003) at U33. Add a
  wire from pad 1 to pad 2 of area A. A Z-80-B works at 4 MHz, so the processor can stay.
- **4 to 4 MHz:** remove the link from pad 1 to pad 2 of area A. Add a wire from pad 2 to pad 4. This
  version has part number 3501-0004.

---

## 3. PROM jumpers (V3-0022, ZCB reference chart)

Revisions 1 A and 1 B cannot upgrade to later revisions with this chart.

| Revision | Area F | Area J | Area K | Area L | Area M |
|---|---|---|---|---|---|
| 1 A | 14–1, 13–6, 11–2, 7–3 | 2–4, 5–7 | 3–4–1, 6–2–5 | 3–4–1, 6–2–5 | 3–4–1, 6–2–5 |
| 1 B | 14–2–13, 11–1, 7–3, 6–12 | 2–4, 5–7 | 3–4–1, 6–2–5 | 3–4–1, 6–2–5 | 3–4–1, 6–2–5 |
| 2 A | 13–14–1, 11–2, 7–3, 12–6 | 6–1, 7–5 | | 7–4, 6–3 | 7–4, 6–3 |
| 3 A | 13–14–1, 11–2, 7–3, 12–6 | 6–1, 7–5 | | 7–4, 6–3 | 7–4, 6–3 |
| 3 B | 11–12–13–14–1, 7–3 | 6–1, 7–2 | | 7–4, 6–3 | 7–4, 6–3 |
| 3 C | 11–12–13–14–1, 7–3 | 6–1, 7–2 | | 7–4, 6–3 | 7–4, 6–3 |
| 4 | 11–12–13–14–1, 7–3 | 6–1, 7–2 | | 7–4, 6–3 | 7–4, 6–3 |

Pads joined by a dash are wired together. Revision 2 dropped area K.

**The revision 1 B change (PIB 0066, 5/8/80).** It lets an 8316 Monitor ROM go in U22, on Rev 1
boards only. After the change U21 is at E800 and U20 is at EC00. Area F becomes 1 to 11, 2 to 13 and
14, 6 to 12, 3 to 7. Under the U22 socket, cut the traces to pins 19 and 21. On the back, wire U22
pin 19 to pad J-3, and U22 pin 21 to U22 pin 24. Footnote `**` of V3-0022 gives the same four steps.

---

## 4. Other bulletins

| Reference | Date | Change |
|---|---|---|
| PIB 0056 | 2/15/80 | Jumper from J2 pin 10 to U6 pin 4, for signal ground at the serial connector |
| VTA 81-1, also V3-005 | Jan 15, 1981 | Rev 3 delays `RD`, because a race caused a data glitch. The glitch looked like a fault on the 64K board. The fix is factory only. See [Vector Graphic Technical Advisories](Vector%20Graphic%20Technical%20Advisories.md) |
| MZ-0034 | 30 Mar 1982 | Units 3100 and 3105. Older boards leave Carrier Detect open, and some serial printers fail. If R10 is absent, solder a 1K resistor from J2 pin 9 to U15 pin 14 |

**The delay of `RD` (VTA 81-1, sheet 2).** Half of the LS74 at U26 clocks on `PCLK` and delays the
signal into pin 4 of the LS32 at U4, which gates the data-in buffers. Notes in a person's hand on
the first page give the wires: U26-1 to U26-14, U26-2 to U33-9, U26-3 to U33-13, U26-4 to U33-8,
U26-5 to U4-4, U4-1 to U4-10, then cut U4-4 from U4-10, and cut U4-4 from the feed-through and U4-1.
The scan holds two sheets: *Revision 3, 4/7/81* with the change printed, and *Revision 3, 7/16/80*
with the change drawn by hand.

---

## 5. 19,200 baud change (one page, not a Vector Graphic document)

The change doubles each baud switch, so 9600 becomes 19200.

1. Make sure that the USART at IC6 is an 8251A or a 9551.
2. Cut the trace from U8 pin 13 toward U10. This takes the 2 MHz clock off the first counter of the
   baud rate generator.
3. Wire the 4 MHz clock in area A to U8 pin 13. It is pad 2 if area A has three pads, and pad 3 if
   area A has five pads.

---

## Quirks worth carrying

- **⚠ Z-80-4 or Z-80-B on revision 3 C.** V3-0022 prints "Processor chip must be a Z-80-4" for the
  6 MHz board. The same page says that a 6 MHz processor is a Z-80-B, and the conversion text says
  "Z-80-B (6Mhz)". The Z-80-4 here is most likely a slip. Treat Z-80-B as the 6 MHz part.
- **⚠ The PROM sockets moved.** U20, U21 and U22 hold different PROMs on each revision, so a socket
  number does not name a PROM. Rev 1 B is the odd one, with the Monitor at U22.
- **⚠ A PROM dump needs its revision.** One 2732 at U20 fills E000–EFFF, as
  [Monitor 4.3](Vector%20Graphic%20Memory%20Maps.md) does. The older boards split the same range
  across three sockets.
