# PCGET / PCPUT — moving files in and out of a running CP/M

Mike Douglas's XMODEM file-transfer programs for Altair CP/M. **There is no disk image here** — these
are CP/M-side `.COM` programs you put *on* a disk, so this directory has no machine file of its own.
Boot any of the CP/M configs in the sibling directories and run them there.

They are the answer to "how do I get a file into the simulated machine without editing the `.dsk` on
the host?" — they talk XMODEM over the 88-2SIO, which is the same port the console is on.

## Not in this repository

Nothing in this directory is tracked. **Download it from:**

> <https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/PCGET%20and%20PCPUT/>

| File | What it is |
|---|---|
| `PCGET.COM`, `PCPUT.COM` | The programs. Copy them onto a CP/M disk to use them. |
| `PCGET.ASM`, `PCPUT.ASM`, `PCGET.HEX` | Sources. |
| `-ReadMe.pdf` | Mike Douglas's own instructions. |

The `.COM` files are already on the `cpm22b23-56k.dsk` and `CPM22-8MB-56K.DSK` images (`DIR` shows
`PCGET COM` and `PCPUT COM`), so for most purposes you do not need to download anything here at all —
just boot one of those and the tools are on `A:`.

## Why the line is 8-bit clean

XMODEM is **binary**, and it goes through the same 88-2SIO the console does. That is precisely why no
serial card in this simulator masks the eighth bit, and why `strip7out` is a property of the
**console** and not of the line (`DESIGN.md` §7.2). A `data_bits = 7` strap or a mask on the UART
would fix MITS BASIC's prompt and silently corrupt every byte of this transfer.

None of the CP/M machine files set any `[console]` transform, for the same reason.
