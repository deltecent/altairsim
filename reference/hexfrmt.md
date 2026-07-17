# Intel Hexadecimal Object File Format

Source: [Hexfrmt.pdf](#) — *Intel Hexadecimal Object File Format Specification*, Revision A,
6 January 1988. 10pp, real text layer. (The PDF credits the conversion to Spehro Pefhany.)

The format `LOAD`, `SAVE`, the built-in ROM registry and the TOML `mount` of a ROM region
all speak. One implementation, four front ends: `src/core/hex.{h,cpp}`.

**What matters here for an 8-bit machine:** records **00** (data) and **01** (end of file),
and the checksum rule. Records 02–05 exist for the 16- and 32-bit Intel processors; the
loader accepts 02/03/04/05 and folds them into a 16-bit address, refusing anything that
lands past 64K — an Altair has sixteen address lines.

## Why the format exists

An absolute binary object file, written in ASCII, so it survives a medium that cannot carry
binary: paper tape, punched cards, a CRT terminal, a line printer. Each byte becomes two
ASCII hex digits (the 8-bit value `0011-1111` is `3F`, sent as the characters `3` and `F`),
high-order digit first — so the file is twice the size of the binary and can be read aloud,
listed, and emailed.

## General record format

Every record is one line:

```
: RECLEN  LOAD OFFSET  RECTYP  INFO/DATA  CHKSUM
  1 byte    2 bytes    1 byte   n bytes   1 byte
```

| Field | Width | What it is |
|---|---|---|
| **RECORD MARK** | 1 char | `':'` — ASCII 3AH. Every record starts with it. |
| **RECLEN** | 2 chars | Number of bytes in INFO/DATA, *after* RECTYP. Max `FF` (255). |
| **LOAD OFFSET** | 4 chars | 16-bit load address. **Only meaningful on a data record**; every other record type codes it `0000`. |
| **RECTYP** | 2 chars | Which record this is — see below. |
| **INFO/DATA** | 2n chars | Zero or more bytes, as digit pairs. Meaning depends on RECTYP. |
| **CHKSUM** | 2 chars | See below. |

### Record types

| Type | Name | Formats | Notes |
|---|---|---|---|
| `00` | Data | 8/16/32 | The only one that carries a load address. |
| `01` | End of File | 8/16/32 | Always exactly `:00000001FF`. |
| `02` | Extended Segment Address | 16/32 | USBA — bits 4–19 of a segment base. |
| `03` | Start Segment Address | 16/32 | CS:IP. **We take the IP and ignore CS** — an 8080 has no CS. |
| `04` | Extended Linear Address | 32 | ULBA — bits 16–31 of a linear base. |
| `05` | Start Linear Address | 32 | EIP, 32-bit. |

### The checksum — the one thing the format gives you for free

CHKSUM is the **two's complement** of the 8-bit sum of every byte from RECLEN through the
last INFO/DATA byte. Which gives the property worth actually implementing:

> **The sum of all the byte pairs in a record, from RECLEN through CHKSUM inclusive, is
> zero (mod 256).**

So verification is: decode the whole record to bytes, add them up, and the low byte must be
`00`. `hex.cpp` does exactly that, on **every record, every time** — a bad one **fails the
load and names the record number**. A silently truncated load is a miserable bug to chase,
and not having it is the entire reason the checksum is in the file.

The EOF record's fields are all fixed, so its checksum is a constant: `FF`. That is why
every Intel HEX file on earth ends with the same eleven characters.

## Data record (type 00)

```
:10010000C300F8AF32004D3E0132014D76C9AA5509
 ^^ ^^^^ ^^ ^^^^^^^^ ... ^^^^^^^^^^^^^^^^ ^^
 |  |    |  16 data bytes                 checksum: the record sums to zero
 |  |    type 00 = data
 |  load address: these bytes go at 0100
 10 = sixteen data bytes follow
```

*(That record is genuine — `SAVE`'s own output, verified. It is the example in the LOAD
reference for the same reason.)*

- **RECLEN** — how many data bytes. Not the line length.
- **LOAD OFFSET** — where the **first** data byte goes. Byte *k* goes at offset + *k*.
- **DATA** — one digit pair per byte.

## End of File record (type 01)

```
:00000001FF
```

RECLEN `00` (it carries nothing), LOAD OFFSET `0000` (unused), RECTYP `01`, CHKSUM `FF`.

## Addressing, and the wrap that matters

For the 8-bit case the address is simply the LOAD OFFSET, and the spec's segment/linear
machinery does not apply. But the spec's **arithmetic** does, and it is the authority for
one thing altairsim relies on. §4 defines a byte's address as:

> `SBA + ([DRLO + DRI] MOD 64K)`
>
> …where DRLO is the LOAD OFFSET of a data record and DRI is the byte's index within it.
> The offset addition is done **modulo 64K**, ignoring any carry, "so that offset
> wraparound loading (from 0FFFFH to 00000H) results in wrapping around from the end to the
> beginning of the 64K segment."

That is why **`LOAD <file> AT <addr>` wraps**. A file whose first record is `F000`, loaded
`AT 0`, moves by −F000: its `F800` record lands at `0800`, and an `E000` record lands back
up at `F000`. Not a liberty we took — the format's own rule, and the hardware's: an 8080
has sixteen address lines and no seventeenth to carry into.

## Traps for an implementer

- **RECLEN is data bytes, not characters, and not the record length.** A record holding *n*
  data bytes decodes to *n* + 5 bytes. Checking one against the other catches a truncated
  line, which is what `hex.cpp`'s "length byte says N but record holds M" is for.
- **LOAD OFFSET is `0000` on every non-data record**, and it means nothing there. Reading it
  as an address on a type 01 would place a byte at zero.
- **A file is SPARSE and you must not fill the gaps.** Records may place 16 bytes at `FF00`
  and 16 more at `2C00` and say nothing about the space between. `Image` is a `std::map` for
  this reason: zero-filling would invent bytes the file never contained.
- **The file's records need not ascend.** Nothing in the spec orders them, so "the first
  record" and "the lowest address" are different questions. `LOAD ... AT` anchors on the
  first *record*, which only the parser can know — hence `Image::first`.
- **Type 03 carries CS:IP, both 16-bit.** The IP is the low pair. An 8080 has no CS, so we
  keep the IP and drop the CS rather than pretend.
- **`start` is not `first`.** Type 03/05 give an *execution* address — a fact about the
  program. `Image::first` is where the file began placing bytes — a fact about the file.
