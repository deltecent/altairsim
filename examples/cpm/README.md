# CP/M 2.2b on an Altair 8" floppy

**To launch it**, name the machine file from inside this folder:

```
$ altairsim cpm22-buffered.toml
```

On **Windows** the program is `altairsim.exe`:

```
> altairsim.exe cpm22-buffered.toml
```

The machine boots itself — you type no `BOOT` command — and comes up at the CP/M prompt.
Type `DIR` to see the disk:

```
56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>DIR
```

Mike Douglas's track-buffered **CP/M 2.2b v2.3**, on an 8" Pertec FD-400 behind an 88-DCDD, booted
by the DBL PROM at `FF00` — `RUN FF00` is the machine file's whole startup, because on a real disk
Altair that was EXAMINE `FF00` and RUN.

`^E` (ATTN) takes the keyboard back to the monitor at any point; `RUN` resumes. `^C` belongs to
CP/M (it is warm boot) and CP/M gets it.

## The files

| File | What it is |
|---|---|
| `cpm22-buffered.toml` | The machine: `base = "default"` plus the floppy in drive 0. Read it — it explains the memory arithmetic and what `readonly` really does on this controller. |
| `cpm22-terminal.toml` | The same machine and the same disk, but the console is a **built-in VT100 window** the simulator draws itself instead of stdio. The monitor stays in the terminal you launched from; CP/M comes up in its own window. No telnet client, no external emulator. Try `emulation=adm3a` for a period CP/M terminal. |
| `cpm22b23-56k.dsk` | The bootable system disk, built for a 56K machine. Carries `DDT.COM`, `M80`/`L80`, `MBASIC` and the host-bridge utilities (`R`, `W`, `HDIR`), with 18K free. Shared by both machine files above. |

**There is no undo.** Drive 0 is mounted read/write because that is what a real machine is, and CP/M
writes to `A:` for anything you create. In a clone `git checkout` puts the image back; in the
package you were handed, nothing does. Copy it first if you are about to test writes in anger.

Three drives are empty. `MOUNT dsk0:drive1 "my-scratch.dsk"` fills one, or `FORMAT` from inside
CP/M will make you a disk.

`BOOT.ASM` and `BIOS.ASM` — this CP/M's own, and the authoritative source the 88-DCDD was built
from — are in `disks/mits-88dcdd/cpm22/buffered/`. They are source rather than product, so they
stay in the repository and are not in the package.
