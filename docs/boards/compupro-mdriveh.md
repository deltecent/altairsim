# CompuPro M-Drive/H

**Status:** done. The two ports, the 22-bit address counter, up to eight boards on one port
pair, and the snapshot. CompuPro CP/M 2.2N boots with it and uses it as drive `M:`.

## The real hardware

The **M-Drive/H** (CompuPro board 197B, 1982–83) is a RAM disk. It has 512K of dynamic RAM
and an Intel 8203 DRAM controller. The board decodes no memory addresses. A guest reaches the
RAM through two I/O ports only.

Switch S1 sets the board:

- S1-1 to S1-7 set address lines A7–A1 of the port pair. The standard base is `C6`.
- S1-8 to S1-10 set the board's cascade number, 0–7. Up to eight boards decode the same two
  ports. Together they make one address space of 4 MB.

## Sources

| Source | Path | Authority |
|---|---|---|
| *M-DRIVE/H Technical Manual*, Document #11978 (1982, 1983) | `reference/CompuPro M-Drive Manual.md` | The ports, switch S1, the address counter, the sample driver |
| CompuPro CP/M 2.2N system disk | `tests/media/compupro/CPM22N.DSK` | The BIOS that counts the boards, and `MFORM` |
| CompuPro CP/M-86 BIOS source on `TMXBIOS.IMD` (maintainer's collection) | — | The same driver as the manual: byte order, parity seed, board count |

The manual's sample driver and the CP/M-86 BIOS agree on each fact that the model uses. The
PAL equations (197P-1, 197P-2) are not printed in the manual.

## Register reference

| Addr | OUT (write) | IN (read) |
|---|---|---|
| base (`C6`) | Write the byte at the address. Then add 1 to the address. | Read the byte at the address. Then add 1 to the address. |
| base+1 (`C7`) | Shift the address up 8 bits and put the byte in bits 0–7. | Not decoded. The bus floats to `FF`. |

The address is 22 bits. Three OUTs to base+1 load it, **high byte first**. Only bits 0–5 of
the first byte are kept. Bits 19–21 of the address select the board by its cascade number.
After `3FFFFF` the address goes to `000000`.

CompuPro's driver puts the 128-byte record for track *t*, sector *s* (both from 0) at
`s<<16 | t<<7`. The parity byte of that record is at `s<<16 | t`, and its value is `80H` plus
the sum of the 128 bytes. Each board holds 8 sectors of 512 tracks. Tracks 0–3 hold the parity
bytes, and the disk parameter block reserves them.

## How it is simulated

- **Bus cycles:** `IoWrite` at base and base+1, and `IoRead` at base only. The base must be
  even: A0 picks the data port or the address port, and S1 sets A7–A1.
- **One board entry is the whole set.** The `boards` property, 1–8, gives boards 0 to
  `boards`-1. Two entries on one port pair would be bus contention here, because `decodes()`
  cannot look at the counter.
- **An address on a board that is not there** reads `FF`, and a write to it changes nothing.
  The counter still counts. CompuPro's BIOS counts the boards with this behavior.
- **The RAM** is one vector of `boards` × 512K. A change of `boards` keeps the bytes of the
  boards that stay. A new board is filled per `fill` and `seed`. Each board has its own random
  stream, so a new board does not change the others.
- **No tick, no events, no interrupt, no DMA.** The board does nothing between bus cycles.
- **Properties:** `port` (even, default `C6`), `boards`, `fill` (`zero` or `random`), `seed`,
  and `address`, which shows the counter and cannot be set.
- **Snapshot:** the counter, `boards` and every byte of the RAM.

### Reset

- `Reset::PowerOn` (POC*): nothing changes. The counter's CLR inputs are tied high on the
  board, and the DRAM keeps its contents.
- `Reset::Bus` (RESET*): nothing changes, for the same reason.
- `POWER`: the RAM is filled per `fill` and `seed`, and the counter is set to 0. The real
  counter has no defined value at power-on.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The address loads high byte first, by a shift. | The driver writes each record at a wrong address. The parity check fails, and the BIOS counts 0 boards. |
| A board that is not there reads `FF`. | The BIOS sizing loop reads, writes back and reads the first record of each board. If a missing board answers, the BIOS counts boards that are not there. |
| A reset keeps the RAM and the counter. | `MFORM` at cold boot formats only a drive that has no format. If a reset clears the RAM, each boot loses the files on `M:`. |
| The address counts up after each data access, read or write. | The driver reads or writes 128 bytes with one address load. A counter that does not count gives the same byte 128 times. |

## Limitations and deliberate departures

- **An IN from the address port is not decoded.** The schematic shows no read path from the
  counters, and the PAL equations are not printed. The manual's driver and CompuPro's BIOS
  do not read the port.
- **No wait states.** The real board holds RDY until the 8203 answers. The manual gives no
  count, and this machine has no slow memory.
- **No PWRFAIL\*.** The signal goes through J1 into the PAL, and the PAL equations are not
  printed.
- **No gaps in the cascade numbers.** A real set can use numbers 0, 1 and 3. Here the boards
  are always 0 to `boards`-1. CompuPro's BIOS stops counting at the first missing board, so a
  set with a gap is a smaller drive on real hardware too.

## Verification

- `tests/test_mdriveh.cpp` (suite `mdriveh`) uses real bus cycles. It tests:
  - the address shift, high byte first, and the bits of the first byte that are lost
  - the count after each access, the step from board 0 to board 1, and the wrap at `3FFFFF`
  - `FF` from a missing board, and the count that continues
  - a C++ copy of the manual's driver as the oracle: a record and its parity byte
  - the BIOS sizing loop, which counts 1, 3 and 8 boards
  - the base: an odd value is refused, and a moved base moves both ports
  - both resets, `POWER`, `fill`, `seed`, `boards` (0 and 9 refused) and the snapshot
- `tests/acceptance/disk1.cmake`, step 6, boots CompuPro CP/M 2.2N with
  `tests/media/compupro/cpm22n-mdrive.toml`. The BIOS prints `1 M-Drive/H` and formats `M:`.
  `PIP` copies `STAT.COM` to `M:`, and it runs from there. After a bus `RESET`, `DIR M:` still
  lists the file. With `boards = 8` and `POWER`, the BIOS prints `8 M-Drive/H`, and `STAT`
  reports a 2016K drive.

## References

- `reference/CompuPro M-Drive Manual.md`
- `docs/boards/compupro-disk1.md`, the floppy controller that boots CP/M 2.2N
