# Built-in ROMs

The common ROMs are **compiled into the simulator**. There is no portable place to keep ROM images across macOS, Linux, and Windows — `/usr/share`, `~/Library`, `%APPDATA%`, and "next to the binary" are four different answers and every one of them becomes a support question. ROMs are small, so they are embedded and the question disappears: a fresh checkout boots on any OS with nothing to download.

Use one from a `memory` board region:

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  mount = "builtin:dbl"
```

`SHOW ROMS` lists what is compiled in. A bare path (`mount = "roms/mine.bin"`) loads a host file instead — **built-ins are a convenience, never a lock-in.**

## The provenance rule

`DESIGN.md` §0.1 says hardware facts come from period manuals and first-hand artifacts, never from another emulator. **A ROM image is a hardware fact**, and an embedded blob with no recorded source is exactly the second-hand fact that rule exists to prevent — worse than a wrong bit in a document, because every piece of software above it would then be debugged against the wrong ground truth, and it would look like a software bug for a very long time.

So **every built-in ROM has a row in the table below**, and no ROM is embedded without one:

| Field | Why |
|---|---|
| **Source** | A specific dump, part, or listing. "From the internet" is not a source. |
| **Size** | Exact bytes. A ROM padded to the next power of two is a *different artifact*. |
| **CRC32** | Verified by a unit test at build time, so a corrupted embed **fails the build**, not a user. |
| **Verified against** | The listing, manual, or hardware the dump was checked against — if it was. Say so honestly when it wasn't. |

## The ROMs

| `builtin:` name | Part | Size | CRC32 | Source | Verified against |
|---|---|---|---|---|---|
| `dbl` | Altair Disk Boot Loader 4.1 | **256 B** (`FF00`–`FFFF`) | **`8E658905`** | `roms/DBL/` — `DBL.ASM` / `DBL.HEX` / `DBL.PRN`. Disassembled by **Martin Eberhard, 4 March 2012**, from an EPROM labeled "DBL 4.1" found socketed in a **MITS Turnkey board**. | `DBL.PRN` listing, **all 256 bytes**, byte-for-byte. |
| `mdbl` | Altair **Mini**disk Boot Loader | **256 B** (`FF00`–`FFFF`) | **`3BC20ADD`** | `roms/MDBL/` — `MDBL.ASM` / `MDBL.HEX` / `MDBL.PRN`. Disassembled by **M. Eberhard, 5 May 2014**, from MITS EPROMs. Fetched 2026-07-13 from deramp.com (`.../altair/software/roms/orginal_roms/`). | Its own `MDBL.PRN` listing. **And it boots**: `altairsim minidisk` loads CP/M 2.2b off a real period `.DSK` through this PROM and reaches `A>`. |
| `altmon` | ALTMON 1.3 — 1K Altair monitor | **1013 B** (`F800`–`FBF4`) | **`705203BE`** | `roms/ALTMON/` — `ALTMON.ASM` / `ALTMON.HEX` / `ALTMON.PRN`. **Mike Douglas**, 2016–2024, based on the Vector Graphic 2.0C monitor, reworked to drive an **88-2SIO**. | Its own `ALTMON.ASM` source, which is in the repository. **And it runs**: `altairsim altmon` prints its banner, takes commands, and dumps memory. |
| `cdbl` | CDBL 3.00 — Combo Disk Boot Loader | **245 B** (`FF00`–`FFF4`) | **`0558293E`** | `roms/CDBL/` — `CDBL.ASM` / `CDBL.HEX` / `CDBL.PRN` + manual. **Martin Eberhard & Mike Douglas**, v3.00, 16 Jan 2016. Boots an 88-DCDD 8″ floppy *or* an 88-MDS minidisk, auto-detecting the drive; the single PROM that supersedes `dbl` and `mdbl`. Fetched from deramp.com (*M. Eberhard Improved ROMs*). | Its own `CDBL.PRN` listing, byte-for-byte, and the `CDBL Manual.pdf` retained beside it. |
| `hdbl` | HDBL 2.00 — Hard Disk Boot Loader | **255 B** (`FC00`–`FCFE`) | **`796FCA9B`** | `roms/HDBL/` — `HDBL.ASM` / `HDBL.HEX` / `HDBL.PRN` + manual. **Martin Eberhard**, v2.00, 13 Aug 2014. Boots an 88-HDSK Pertec hard disk; boot platter chosen by Sense Switch 3. Fetched from deramp.com (*M. Eberhard Improved ROMs*). | Its own `HDBL.PRN` listing and `HDBL Manual.pdf`. |
| `amon` | AMON 3.0 — full-featured ROM monitor | **F000–FFFE** (4 KB span; **3807 B** programmed, gaps read `FF`) | **`C00DC413`** (over the `FF`-filled span) | `roms/AMON/` — `AMON.ASM` / `AMON.HEX` / `AMON.PRN` + manual. **Martin Eberhard**, v3.0, 12 Feb 2023 (later fixes by **D. Hansel**). Memory/transfer/EPROM-programming monitor with built-in floppy/minidisk/HDSK/tape boot entry points; console on an 88-2SIO port 0, cold start `F800h`. Fetched from deramp.com (Eberhard firmware archive). | Its own `AMON.PRN` listing and `AMON Users Manual.pdf`. `AMON.HEX` ends in Ctrl-Z (CP/M soft-EOF) padding, which `loadHex` treats as end-of-file. |
| `acuter` | ACUTER 1.0 — CUTER for the Altair | **2048 B** (`F000`–`F7FF`) | **`4A4E608D`** | `roms/ACUTER/` — `ACUTER.ASM` / `ACUTER.HEX` / `ACUTER.PRN` + manual. **Mike Douglas**, 25 Apr 2015, from Processor Technology's **CUTER 1.3** (© 1977 Software Technology Corp.); console on the 1st 2SIO port, tape via the ACR, `HGET`/`HSAVE` Intel-HEX commands added. Fetched from deramp.com (*CUTER for Altair*). | Its own `ACUTER.PRN` listing and the retained `CUTER Manual.pdf`. |

> ### `dbl` and `mdbl` are more interchangeable than they look, and finding that out was the point
>
> DBL is the 8″ floppy's bootstrap and MDBL is the minidisk's. Both live at `FF00`, both are 256
> bytes, and both open by copying themselves into RAM — a 1702A EPROM was too slow to execute
> from — differing in where they land and how much they move:
>
> ```
> DBL   FF00  21 13 FF   LXI H,FF13     MDBL  FF00  21 13 FF   LXI H,FF13
>       FF03  11 00 2C   LXI D,2C00           FF03  11 00 4C   LXI D,4C00
>       FF06  0E EB      MVI C,0EBH           FF06  0E E3      MVI C,0E3H
> ```
>
> MDBL relocates to `4C00` rather than `2C00` because **Minidisk BASIC loads low** and would land
> on top of it.
>
> **I expected DBL to be unable to read a minidisk. It reads one perfectly well.** It went into
> `machines/minidisk.toml` as a negative control — an 8″ PROM on a 5.25″ machine, which surely
> hangs — and CP/M booted and listed both drives. Mike Douglas's own `BOOT.ASM`, the loader on
> that very disk, says why in its header: *"This code is loaded from sectors 0 and 2 into RAM by
> the disk boot loader PROM (DBL)."* DBL interleaves 2:1, so it reads sector 0 and then sector 2 —
> exactly where the loader sits — and DBL's `cHDLOAD` arrives at an 88-MDS as `TIMER RESET`, which
> is harmless.
>
> A bootstrap written for one controller runs on the other **because MITS built the 88-MDS to
> present the 88-DCDD's programming model** (`docs/boards/mits-88mds.md`). That is precisely the
> compatibility that let a minidisk hide inside the 8″ controller in this tree for months. So the
> minidisk acceptance test's negative control swaps the **card**, not the PROM — that is the
> variable that actually matters.
>
> Use `mdbl` on a minidisk machine anyway: it is the PROM MITS shipped for that subsystem
> (88-MDS manual §1-3.B), and it is the one that will boot Minidisk BASIC when that lands.

**`altmon` is the one ROM here that is verified by *execution*, not just by listing.** It is not a data structure we assert things about — it is a program, written for real hardware by someone who owns one, and it either works on our 8080 and our 2SIO or it does not. It does:

```
$ altairsim altmon
ALTMON 1.3
*DUMP F800 F80F
F800 3E 03 D3 10 D3 12 3E 11 D3 10 D3 12 31 00 C0 CD  >.....>.....1...
```

Those sixteen bytes are ALTMON's own first sixteen — `MVI A,3 / OUT 10 / OUT 12 / MVI A,11 / OUT 10 / OUT 12` — so this is the ROM reading its own 2SIO initialization back to us *through the card it initializes*, and it agrees with `ALTMON.HEX` byte for byte.

It expects **itself at F800**, **RAM below C000** (it sets its stack there and pushes down), and an **88-2SIO at port 10** with the console on channel A. Those are not choices; they are what the listing requires, and `machines/altmon.toml` is the machine that satisfies it.

> **A trap, from ALTMON's own listing.** Its `ahex` routine reads exactly four hex digits and **aborts to the prompt on any byte below `'0'`** — so a space is not a separator, it is a *cancel*. Type `DF800F80F`, not `D F800 F80F`. The spaces in ALTMON's own printed command summary are typography, not grammar.

**How `dbl` was verified.** All 16 Intel HEX records checksum clean and cover `FF00`–`FFFF` contiguously with no gaps and no overlap. Every one of the 256 bytes in `DBL.HEX` matches the address/opcode columns of the `DBL.PRN` assembler listing exactly. The image ends `... C9 00 00` — the trailing zeros are the listing's own `DW 00H` ("FILLS THE EPROM OUT WITH 00'S"), so this is a **complete 256-byte part, not a truncated one**.

**One caveat, stated plainly.** The chain is *EPROM → Eberhard's disassembly → reassembly*, not a raw dump. The bytes are therefore only as faithful as the disassembly, and I cannot close that last link without the physical part. This is still far better than a bare `.bin`, because the listing makes every byte auditable — but it is not the same as a dump, and the table should not pretend it is.

**What DBL actually does, since it keeps getting mis-cited.** DBL copies *itself* from `FF13` into RAM at `2C00` (`EB` bytes) and jumps there, because — its own header says so — "BECAUSE OF THE SLOW EPROM ACCESS TIME." **It never writes to `FFxx`.** It therefore has *no* opinion about PHANTOM\* write-through, and any claim that DBL "needs shadow RAM" is false. Earlier drafts of `DESIGN.md` §4.2, `docs/config.md`, and `docs/boards/s100-memory.md` all said exactly that; it was fabricated and has been removed. It reads the sense switches at `FF22` (`IN 0FFH`, bit 4) to pick the 2SIO stop-bit setting — *that* part is real and is why `[machine] sense` exists.

## Licensing

Most of these parts are from companies that no longer exist (MITS folded in 1979), and vintage ROM images circulate freely — but "freely circulating" is not the same as "licensed to redistribute," and **embedding a ROM in the binary is redistribution** in a way that asking a user to supply a file is not.

This is Patrick's call, not the simulator's, and it is recorded here so it is a decision rather than an accident. If any ROM turns out to be one we should not ship, the fallback costs nothing: it stays a path (`mount = "roms/x.bin"`), the user supplies it once, and `docs/roms.md` says where to get it.

`cdbl`, `hdbl`, and `amon` are Martin Eberhard's and Mike Douglas's own work, published on deramp.com for exactly this kind of use. `acuter` is more delicate: the code is Mike Douglas's 2015 Altair port, but it derives from Processor Technology's **CUTER 1.3**, which carries a 1977 Software Technology Corp. copyright. It circulates as freely as the rest of that vanished catalog, and it ships here on the same basis as the MITS parts above — a decision, made here, reversible to a path if it should not have been.

**Each ROM directory carries its manual and a `README.md`.** The vendor manual PDFs are retained in `roms/<NAME>/` as downloaded, and each `README.md` is distilled from that manual (plus the ROM's own `.ASM` header) — what the ROM is, its load address and entry points, its I/O and RAM needs, and the `mount =` line to use it. These are the one exception to the repo's "no vendor PDFs" rule (`.gitignore`): they are small, they are the primary-source manuals for parts we embed, and keeping them beside the image is the same provenance discipline the table above exists to enforce.

## How it works

At build time, CMake turns each file in `roms/` into a byte array in a generated translation unit, and a registry maps `builtin:<name>` → `{span<const uint8_t>, crc32, description}`. Consequences worth stating:

- **No filesystem access at runtime** for a built-in. It is `.rodata`. Nothing to find, nothing to permission, nothing to ship alongside.
- **The board does not care.** A region takes a `span<const uint8_t>`; whether that came from `.rodata` or a file the host service read is not its business (§7). The `builtin:` scheme is resolved by the config loader, above the board.
- **`CONFIG SAVE` round-trips the name, not the bytes** — `mount = "builtin:dbl"` in, `mount = "builtin:dbl"` out.
- **A CRC test per ROM.** Cheap, and it turns "someone's editor mangled a binary" from a mystery into a build failure.

## References

- `DESIGN.md` §0.1 (where hardware facts come from), §7 (host services), §10.2 (`RAW` — how you get bytes into a ROM at runtime).
- `docs/boards/s100-memory.md` — the board that holds them.
