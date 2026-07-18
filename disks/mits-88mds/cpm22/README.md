# CP/M 2.2 for the Altair Minidisk — 88-MDS, 5.25″

CP/M 2.2b on a **5.25″ hard-sector minidisk**, in an **88-MDS**, booted by the MDBL PROM.

```
altairsim disks/mits-88mds/cpm22/cpm22-mini.toml

56K CP/M 2.2b v2.3
For Altair Mini Disk

A>DIR
A: AFORMAT  COM : LS       COM : ASM      COM : SYSGEN   COM
A: LOAD     COM : STAT     COM : DDT      COM : DUMP     COM
A: MOVCPM5  COM : ACOPY    COM : PIP      COM : IOBYTE   TXT
A: PCPUT    COM : PCGET    COM
A>DIR B:
B: NSWP     COM : MBASIC   COM : LUNAR    BAS : WM       COM
B: WM       HLP
```

Run it **from anywhere** — the paths inside the `.toml` resolve against **that file**, not
against the directory you launched from, so this folder boots wherever it is copied to. (A
path you *type* at the prompt is still relative to your shell, which is the other half of
the same rule.)

## This is not the 88-DCDD with a smaller disk

It is a **different controller**, and until 2026-07-13 this simulator did not have one: the minidisk
was a row in the 8″ card's format table, which meant a minidisk mounted here turned at **360 RPM
instead of 300** and clocked a byte every **32 µs instead of 64**. Neither error can fail loudly.
See `docs/boards/mits-88mds.md`.

| | 88-DCDD (8″) | **88-MDS (this)** |
|---|---|---|
| Rotation | 360 RPM | **300 RPM**, 200 ms/rev |
| Data rate | 250,000 bit/s | **125,000 bit/s** — 64 µs a byte |
| Geometry | 77 × 32 | **35 × 16**, 137-byte slot |
| Capacity | ~330 KB | **71,680 bytes** formatted |
| Head | a solenoid, and a bit to drive it | **no head-load bit at all** — the head is loaded whenever the drive is enabled |
| Bit 2 (04h) | load head | **TIMER RESET** — the motor stops after 6.4 s without it |

**A minidisk holds a fourteenth of an 8″ floppy, so CP/M for it ships as TWO disks** — the system on
A:, the tools on B:. That is why `cpm22-mini.toml` mounts both.

## The images

| File | Tracked? | What it is |
|---|---|---|
| `CPM56K-1.DSK` | **yes** | The bootable system disk, built for **56K**. **`cpm22-mini.toml` mounts this as A:.** |
| `CPM56K-2.DSK` | **yes** | Its second disk — MBASIC, `WM`, `NSWP`. Mounted as B:. |
| `CPM24K-1.DSK` / `CPM24K-2.DSK` | no | The same CP/M built for a **24K** machine. It boots in the stock 56K `minidisk` machine as-is — the constraint is one-way, and more RAM than the image needs is fine. Shrink the memory card only if you want the authentic small machine; `cpm22-mini.toml` shows how in a comment. |
| `MOVCPM5.COM`, `SYSGEN.COM` | Relocate and write a new system. On the disks already. |
| `ReadMe.pdf` | Mike Douglas's notes. Not tracked (vendor documentation). |

The two 56K disks are **in git** — 75 KB each, and `.gitignore` names them one at a time. They are
here so a fresh clone can boot the minidisk and so `acceptance-minidisk` runs without anyone
downloading anything first. The 24K pair is not tracked; get it from

> <https://deramp.com/downloads/altair/software/minidisk/CPM%202.2/>

They are **76,800 bytes**, not the 76,720 the geometry implies (35 × 16 × 137). XMODEM padded them up
to a 128-byte boundary — and note 76,720 is *not* a multiple of 128 to begin with (it is 599.375
blocks), so **every** real minidisk image is padded. `sizeMatches()` tolerates it.

**There is no undo.** CP/M writes to A: for anything you create, and the config mounts both drives
read/write, because that is what a real machine is. Git cannot restore what it never tracked. **Copy
an image before testing writes.**

## The motor is optional, and it is off

The real card takes **one second** to spin a stopped drive up, and turns the motor off after **6.4
seconds** with no access — which is why the period BIOS pokes `TIMER RESET` before every single read.
The card models both exactly. It just does not make you live through them by default:

```toml
[[board]]
id    = "mds0"
motor = "real"     # 1 s spin-up, and it stops after 6.4 s
```

CP/M boots and runs under either setting. Flat out is the default here for the same reason it is on
the CPU (`clock_hz = 0`).

## The sources here are tracked, and they are why the board is right

`BIOS.ASM` and `BOOT.ASM` are **in git** — unlike the images — because they are Mike Douglas's own
listings, written against real hardware, and the 88-MDS was modelled from them alongside the manual.
The `MINIDSK` equates (`NUMTRK 35`, `NUMSEC 16`, `DATATRK 4`, `BIOSLEN 1000h`) and the period
software's own account of the card — *"5.25″ drives (which don't support 'ready' the same as 8″
drives)"* — come straight out of these files. They are first-hand sources under `DESIGN.md` §0.1, so
they stay in the tree.

`BOOT.ASM`'s header is also the answer to a question that cost me an hour, and it is worth reading:

> *"This code is loaded from sectors 0 and 2 into RAM by the disk boot loader PROM **(DBL)**."*

**DBL — the 8″ PROM — boots this disk.** It went into the acceptance test as a negative control that
was supposed to hang, and it did not. The two controllers are register-compatible by design, so a
bootstrap written for one runs on the other. `docs/roms.md` has the whole story; it is the same
compatibility that let the minidisk hide inside the wrong card for months.
