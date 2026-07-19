# Altair Disk Extended BASIC 4.1 on an 8" floppy

```
altairsim diskbasic.toml

MEMORY SIZE? 
LINEPRINTER? C
HIGHEST DISK NUMBER? 0
HOW MANY FILES? 
HOW MANY RANDOM FILES? 

37033 BYTES FREE
ALTAIR BASIC REV. 4.1
[DISK EXTENDED VERSION]
COPYRIGHT 1977 BY MITS INC.
OK
```

**Altair BASIC Rev 4.1, Disk Extended Version** (MITS, 1977) on an 8" Pertec FD-400 behind an
88-DCDD, booted by the DBL PROM at `FF00` — `RUN FF00` is the machine file's whole startup,
because on a real disk Altair that was EXAMINE `FF00` and RUN.

Unlike the cassette BASIC in `../basic`, this one has a filesystem: files, a directory, `SAVE`
by name, and the `DSKINI` command.

`^E` (ATTN) takes the keyboard back to the monitor at any point; `RUN` resumes.

## The startup dialogue, because one question has no guessable answer

| Question | Answer |
|---|---|
| `MEMORY SIZE?` | Return — take all of it |
| `LINEPRINTER?` | **`C`** — and only `C`, `O` or `Q` are accepted |
| `HIGHEST DISK NUMBER?` | `0` — one drive, numbered from zero |
| `HOW MANY FILES?` | Return |
| `HOW MANY RANDOM FILES?` | Return |

**`LINEPRINTER?` re-asks in silence on anything it does not like** — no error, no hint. Answer
it with a blank line or an `N` and the prompt comes straight back, which looks like a hung
machine and is not one. `C` is the 88-C700 line printer, and it is legal here even though this
machine has no printer board: the answer only tells BASIC where `LPRINT` should go, and nothing
is written until you use it. Fit an 88-C700 if you want it to land somewhere —
`altairsim -x 'SHOW MACHINE' lineprinter` has one already wired up.

The answer set is Rev 4.1's own; MITS's *Basic Versions* table, kept with the sources under
`disks/mits-88dcdd/diskbasic/`, is where it is written down.

## The files

| File | What it is |
|---|---|
| `diskbasic.toml` | The machine: `base = "default"` plus the floppy in drive 0. |
| `Disk BASIC 4.1.dsk` | The bootable system disk. |

**There is no undo.** Drive 0 is mounted read/write because that is what a real machine is, and
`SAVE` and `DSKINI` both write. In a clone `git checkout` puts the image back; in the package
you were handed, nothing does. Copy it first if you are about to experiment in anger.

**The filename has spaces in it**, which is authentic and occasionally inconvenient: quote it
anywhere you type it, or `MOUNT` reads `BASIC` as an option and refuses.

Three drives are empty. `MOUNT dsk0:drive1 "my-data.dsk"` fills one — and raise the answer to
`HIGHEST DISK NUMBER?` to let BASIC see it.
