# TARPROM — Tarbell floppy boot PROM (`builtin:tarbell-sd`)

The 32-byte cold-start bootstrap that shipped socketed on the **Tarbell
Electronics** S-100 floppy interface — the single-density #1011 (1977) and,
unchanged, the double-density #2022 (1979–80). It is the whole PROM: 32 bytes,
reproduced in full below.

- **Load address:** `0000h` — the PROM shadows the bottom of memory over the
  S-100 **PHANTOM\*** line while the machine boots, and releases it
  combinationally the moment the CPU fetches from an address with **A5 set**.
- **Decoded image:** `0000`–`001F`, 32 bytes, CRC32 `55E3446D`.

## What it does

```
BOOT:   IN   0FCH        ; wait for the FD1771 to finish its power-on restore (home)
        XRA  A
        MOV  L,A          ; HL = 0000 -- load pointer
        MOV  H,A
        INR  A
        OUT  0FAH         ; sector register = 1
        MVI  A,8CH        ; FD1771 Read Sector command
        OUT  0F8H
RLOOP:  IN   0FCH         ; poll the wait port: bit7=1 DRQ, bit7=0 INTRQ
        ORA  A
        JP   RDONE        ; INTRQ (sign clear) -> the sector is in
        IN   0FBH         ; DRQ  -> read a data byte
        MOV  M,A          ; ...store it (A5 crosses high here WITHOUT releasing PHANTOM*:
        INX  H            ;    the release triggers on a READ, never a write, so the loop
        JMP  RLOOP        ;    keeps fetching itself from the PROM while it fills RAM)
RDONE:  IN   0F8H         ; read the FD1771 status
        ORA  A
        JZ   07DH         ; no error -> jump into the loaded sector. 07D has A5 SET, so this
        HLT               ;    very fetch releases the shadow and drops into the cold loader.
```

The read command is `8Ch` (a Type II Read Sector on the FD1771), **not** `0Ch`
— `0Ch` is a Type I Restore and would transfer nothing. An early transcription
of this listing labeled the byte `0CH READ SECTOR`; the shipped `TARPROM.HEX`
carries the correct `8Ch`, and the `.ASM` has been corrected to match it.

## Use it

The Tarbell boards load this PROM automatically — it is not a mountable socket.
`altairsim tarbell` and `altairsim tarbelldd` boot CP/M 2.2 through it. See
[`docs/boards/tarbell-sd.md`](../../docs/boards/tarbell-sd.md) and
[`docs/boards/tarbelldd.md`](../../docs/boards/tarbelldd.md).

## Files here

| File | What it is |
|---|---|
| `TARPROM.HEX` | The 32-byte image, embedded verbatim and decoded by the simulator's Intel HEX loader. |
| `TARPROM.ASM` | The bootstrap source, matching the shipped bytes. |

**Source:** the *Tarbell Floppy Disk Interface Manual* boot-PROM listing;
hardware detail is distilled in
`reference/Tarbell_Floppy_Disk_Interface_Manual.md`. Provenance and the CRC32
test are in [`docs/roms.md`](../../docs/roms.md).
