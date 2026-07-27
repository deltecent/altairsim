# ALTAIR Extended ROM BASIC 16K (`builtin:rombasic`)

MITS **Extended BASIC that executes directly from ROM** — the interpreter lives in
eight 2K PROMs at `C000`–`FFFF` and runs in place, so it needs **no RAM to hold
itself**. That is the whole point of the ROM build: it leaves the full **48K** below
it (`0000`–`BFFF`) free for the user's program, where the tape BASICs had to load the
interpreter into that same RAM first.

- **ALTAIR ROM BASIC Version 4.1**, © 1977 MITS Inc.
- **Load address / span:** `C000`–`FFFF` (16 KB, contiguous).
- **Decoded image:** 16384 bytes, CRC32 `643FE88B`.
- The **last 2K** is the disk bootloader, reachable with `RUN FF00` to boot Altair
  DOS / CP/M (a 48K-or-smaller CP/M, since BASIC occupies the top 16K).

## Running it

```
altairsim rombasic
```

BASIC cold-starts by executing at `C000` (the machine's `startup = ["RUN C000"]`), then
asks its start-up questions on the 88-2SIO console:

```
MEMORY SIZE?            <- Enter (use all of it)
LINEPRINTER? C          <- 'C' selects the console; anything else re-asks
ALTAIR ROM BASIC VER  4.1
COPYRIGHT 1977 BY MITS INC.
48101 BYTES FREE
OK
```

## Hardware it expects

- **8080 CPU.**
- **Front-panel sense switches** (port `FF`) select the terminal type. From the ReadMe's
  table (high byte, A15–A8): `0x00` = 88-2SIO / 2 stop bits, `0x10` = 88-2SIO / 1 stop
  bit, `0x20` = 88-SIO. `machines/rombasic.toml` uses **`sense = 0x00`**.
- **88-2SIO** console at port `0x10`, channel A.
- **48K RAM** at `0000`–`BFFF`; **the ROM at `C000`–`FFFF`.**
- An **88-ACR** at port `06` for Extended BASIC's `CSAVE`/`CLOAD`.

The machine that satisfies all of this is [`machines/rombasic.toml`](../../machines/rombasic.toml).

## Provenance

The MITS distribution is retained in [`dist/`](dist/): the eight individual 2K PROM
images `EBROM1.HEX`–`EBROM8.HEX` (addressed `0000`–`1FFF` within each part) and their
address-placed siblings `EBROM-C0.HEX`–`EBROM-F8.HEX` (`C000`–`FFFF`), plus the combined
`EBROM-ALL.hex`. The embedded `ROMBASIC.HEX` is that combined `C000`–`FFFF` image (its
trailing blank lines after the Intel-HEX end record removed). See `ROM BASIC ReadMe.pdf`
for the original load map and sense-switch table.
