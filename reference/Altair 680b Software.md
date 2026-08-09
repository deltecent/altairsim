# Altair 680b software — the deramp software archive

Source: **deramp.com** Altair 680b software directory,
`https://deramp.com/downloads/altair/software/altair_680/` (Mike Douglas's vintage-computer
archive, an authorized source — see [[altair-hardware-sources]]). Fetched 2026-08-08.

Unlike the other files in `reference/`, this is **not a distilled hardware manual**. It is the
provenance anchor for the **680b software that ships in this tree as built-in ROMs** — the
Motorola **S-record (`.S19`)** images the 6800 line of the simulator was built to run. The
hardware those images run on is documented in the sibling notes ([Theory of
Operation](Altair%20680b%20Theory%20of%20Operation.md), [Programming
Manual](Altair%20680b%20Programming%20Manual.md), [KCACR](Altair%20680b%20KCACR.md),
[Universal I/O Board](Altair%20680b%20Universal%20IO%20Board.md)); this note records where the
*code* came from, so a fresh checkout can see the origin of every committed `.S19` without a
scan. Per-image size, address range and CRC32 are in [`docs/roms.md`](../docs/roms.md), and a
unit test (`tests/test_roms.cpp`) checks each CRC at build time — a mangled byte fails the
build, not a user chasing a phantom.

The images are small and are ours to keep beside the code, so — unlike the large hardware
scans, which are **not** redistributed — the actual `.S19` **plus** the `.ASM` and `.LST` that
document them are tracked verbatim under `roms/<NAME>/` (each with its own `README.md`).

## The directories

The parent `altair_680/` directory holds four software sub-directories relevant here. Each PROM
folder carries, for every image, the S-record (`.S19`), the assembler source (`.ASM`), and the
listing (`.LST` or `.PRN`) so the bytes can be reconciled against the source.

| Sub-directory (under `.../altair_680/`) | Holds | In this tree |
|---|---|---|
| `PROM Monitor/` | The 680b **System Monitor** PROM (PROM 1, `FF00`–`FFFF`, holding the monitor and the reset/interrupt vectors) in two builds: **MON680** (the shipped ACIA console monitor) and **SWIMON** (the same monitor with `SWI` vectored through `$0010` for breakpoints). | `builtin:mon680` (`roms/MON680/`), `builtin:swimon` (`roms/SWIMON/`). Boots: `altairsim altair680` → `.` prompt. |
| `PROM for KCACR/` | The optional **KCACR cassette loader/punch** PROM (socket V, `FD00`–`FDFF`): `.J FD00` loads an S-record tape, `.J FD74` punches memory. Calls the MON680 console routines, so it runs one PROM below the monitor. Mike Douglas's July-2022 disassembly. | `builtin:kcacr` (`roms/KCACR/`), used by `examples/altair680/altair680-kcacr.toml` with the `680kcacr` board. |
| `Cassette Binary Loader/` | The 680b **binary cassette loader** — the small bootstrap that reads a binary (non-monitor) tape image over the cassette interface. The authoritative source for loading period binary tapes on the emulated 680b. | Not embedded as a built-in; loaded at runtime under the monitor. Reference only. |
| `BASIC/` | **Altair 680 BASIC** — the MITS-shipped BASIC for the 680b, as cassette images. Real 680b BASIC loads on the emulated machine via the KCACR loader (`.J FD00`) once mounted as a tape. | Not embedded (large, and licensed differently from a PROM); loaded at runtime. Reference only — see `examples/altair680/README.md`. |

## What ships as a built-in, and what does not

Only the **PROM images** are embedded as built-ins: they are a few hundred bytes each, they are
*part of the machine* (a 680b is not a 680b without its monitor PROM), and they are on the same
footing as the other MITS ROMs already committed (`docs/roms.md`). The **cassette software** —
the binary loader and BASIC — is **not** embedded: it is application software the operator
loads at run time, exactly as on real hardware, and it belongs on a mounted tape rather than in
the machine's ROM. Recording all four directories here keeps the whole 680b software story in
one place while being honest about which images the build actually carries.

## Related

- [`docs/roms.md`](../docs/roms.md) — the per-image manifest: size, address range, CRC32 and
  provenance for `mon680`, `swimon` and `kcacr` (each also has a `roms/<NAME>/README.md`).
- [`docs/sources.md`](../docs/sources.md) — the source manifest this note is listed in.
- `machines/altair680.toml`, `examples/altair680/` — the machine and the board examples that
  run this software. See also [[altair680-machine-build]].
