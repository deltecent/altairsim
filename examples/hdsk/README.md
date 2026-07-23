# CP/M 2.2 on an Altair 88-HDSK hard disk

```
altairsim hdsk.toml

HDBL 2.00
LOADING FROM 0

48K CP/M 2.2b v1.6
For MITS 88-HDSK

A0>DIR
```

Mike Douglas's **CP/M 2.2 for the 88-HDSK** (the `HDCPM22v16` build for a 48K machine),
booted by the **HDBL** PROM at `FC00` — `RUN FC00` is the machine file's whole startup,
because on a real machine that was EXAMINE `FC00` and RUN. HDBL reads the disk's Pack
Descriptor Page, loads the boot pages, and jumps into CP/M.

The **88-HDSK "Datakeeper"** is not a floppy card. It is an outboard controller (its own
8X300 processor and Pertec drive) that plugs into the Altair through an 88-4PIO at ports
`A0h`–`A7h` and moves whole 256-byte sectors for you over a command/handshake protocol.
The image is one platter: 406 cylinders × 2 sides × 24 sectors × 256 bytes = 4,988,928
bytes. See `docs/boards/mits-88hdsk.md` and `reference/88-HDSK.md`.

`^E` (ATTN) takes the keyboard back to the monitor at any point; `RUN` resumes. `^C`
belongs to CP/M (it is warm boot) and CP/M gets it.

## The files

| File | What it is |
|---|---|
| `hdsk.toml` | The machine: `base = "default"` plus the 88-HDSK controller, the HDBL PROM at `FC00`, and the platter in drive 0. |
| `HDCPM22v16-48K.DSK` | The bootable system platter, built for a 48K machine. |

**There is no undo.** Drive 0 is mounted read/write because that is what a real machine is,
and CP/M writes to `A:` for anything you create. In a clone `git checkout` puts the image
back; in the package you were handed, nothing does. Copy it first if you are about to test
writes in anger, or add `readonly = true` to the drive in `hdsk.toml`.

`BOOT.ASM`, `BIOS.ASM` and the original `HDBL.ASM` — the authoritative sources this was
built and verified against — live at deramp.com (Altair hard-disk software and ROMs). They
are source rather than product, so they are not shipped here.
