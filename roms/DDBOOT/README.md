# DDBOOT — Jade Double D boot PROM (`builtin:ddboot`)

The host boot PROM for the **Jade Double D** floppy controller, CP/M 2.2 Release 2. The PROM
is in host memory, not on the board. It starts the board's own Z80 and loads CP/M from drive 0.

- **Run address:** `F000h`. Jade's header says to put the PROM at `F000`.
- **Decoded image:** `F000`–`F24D`, 590 bytes, CRC32 `631C3AD5`. The image has 581 bytes.
  `F1E3`–`F1EB` are not programmed and read `FF`. An `ORG` puts the board program's NMI
  handler at its offset `0066h`, which is the Z80's NMI address, and that makes the gap.
- **RAM:** `0040`–`0043` for the PROM's variables, and a stack below `0080`.

## Entry points

| Address | Name | What it does |
|---|---|---|
| `F000` | INIT | Cold start. INIT is a patch area of `NOP`s, and then the PROM boots. |
| `F003` | BOOT | Boot the disk system again. |
| `F006` | CNS$CK | Console status: `A` = `FF` when a key is ready, else `00`. |
| `F009` | CNS$IN | Console input, in `A`, seven bits. |
| `F00C` | CNS$OT | Console output of the byte in `C`. |
| `F00F` | MSG$OT | Print the string at `HL`, which ends with `$`. |

The CP/M BIOS on Jade's disks (DDBIOS) calls `F006`, `F009` and `F00C` for the console. The
PROM must therefore stay in memory while CP/M runs.

## What it does

1. It reads the board's status port `43h`. Bits D1–D3 give the address of the board's 1 K
   memory window, from `E000` to `FC00`.
2. It asks for bank 0 of the board's RAM, and copies a 200-byte program (the "injection
   module") into it.
3. It starts the board Z80 with `OUT 43h, 80h`, and waits for the Z80 to halt.
4. The program on the board Z80 does these steps:
   1. It selects drive 0.
   2. It steps the head to track 0.
   3. It reads 1 K of track 0, from sector 13, into bank 1. This is DCM, Jade's disk
      program for the board Z80.
   4. It starts DCM, which loads the CP/M system.
5. The host takes the RAM back. A status byte at offset `0377h` tells the PROM what happened:
   - Bit 7 set means that the drive is not ready. The PROM prints `INSERT SYSTEM DISKETTE`
     one time, and tries again until a disk is in the drive.
   - Any other bit set means that the load failed. The PROM prints `DDBOOT LOAD ERROR -`
     and the code in hex, and stops on a `HLT`.
6. A load address and a length at offsets `0378h`–`037Bh` describe the loaded block. The PROM
   copies that block from bank 1 to host memory, and jumps to the load address.

## The console

The PROM's console is a serial port at I/O ports `00` and `01`. Jade marked these ports as
patch points for the user's own console board.

| Port | Read | Write |
|---|---|---|
| `00` | Status. Bit 1 (`02h`) = a byte is ready to read. Bit 2 (`04h`) = ready to send. Both are active high. | — |
| `01` | The received byte. | The byte to send. |

The PROM sets up no UART: `INIT` is all `NOP`s. These ports and bits are the port map of the
SD Systems VDB-8024, a video board with fixed ports `00`/`01` that needs no set-up.

The `jadedd` machine uses [`ddboot-2sio`](../DDBOOT-2SIO/README.md) instead. That PROM is
this one with the console patched for an 88-2SIO at ports `10`/`11`.

## Use it

```toml
  [[board.region]]
  type  = "rom"
  at    = F000
  mount = "builtin:ddboot"
```

Then type `RUN F000`. The board's strap settings must agree with the PROM: the board's port is
`43h`, and the board is a Rev C board. See
[`docs/boards/jade-doubled.md`](../../docs/boards/jade-doubled.md).

## Files here

| File | What it is |
|---|---|
| `DDBOOT.HEX` | The image. The build embeds this file. |
| `DDBOOT.ASM` | Jade's source, unchanged. It assembles with Digital Research `ASM`. |
| `DDBOOT.PRN` | The assembler listing. `SYMBOLS LOAD` reads it. |

**Source:** `DDBOOT.ASM` from the Jade CP/M 2.2 master disk, serial number 2-187-01961
(`JADEDD-CPM22-MASTER-SSSD.DSK`, in the maintainer's `jadedd-cpm22` repository). DRI `ASM` 2.0,
run inside altairsim on 2026-10-28, makes `DDBOOT.HEX` and `DDBOOT.PRN` from the source
byte for byte. The CRC32 test is in [`docs/roms.md`](../../docs/roms.md).
