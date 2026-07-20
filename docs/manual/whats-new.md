# What's new in 0.2.0

This is the short version of what changed since `0.1.0`: the things you can do here that you
could not do there. Each one is documented properly in its own chapter, and this one mostly
says where to look.

## Three more monitors that boot from a bare command line

`amon`, `acuter` and `cdbl` are machines now, so Martin Eberhard's Altair ROMs boot by name —
nothing to fetch, nothing to mount:

```
$ altairsim amon          AMON 3.1 in a 4K EPROM at F000 -- a full-featured Altair monitor
$ altairsim acuter        ACUTER at F000 -- CUTER on a plain Altair, driving a terminal
$ altairsim cdbl          the `default` machine, with the Combo Disk Boot Loader in the socket
```

The ROM *images* were in the binary at `0.1.0` already. What is new is that each has a machine
built around it — which is the whole distance between shipping an image and being able to run
it. `SHOW ROMS` still lists every image the binary carries, and `hdbl` is deliberately **not**
among the machines: it boots an 88-HDSK hard disk, and there is no 88-HDSK board here for it
to boot.

Sixteen machines are built in now. `altairsim --list` is the authority, not this paragraph.

## The video window behaves like a window

At `0.1.0` the VDM-1 and Sol-20 opened a window and that was about all you could say for it.
It now does the things a window is expected to do.

- **It does not steal your keyboard when it opens.** The terminal keeps the keys, which is
  what you want while you are still typing at the monitor prompt. `SET DISPLAY focus=on` hands
  the keyboard to the window when you actually want to type at the guest, and stopping the
  guest hands it back.
- **It is named after the machine**, so `sol20` and `vdm1` are two windows you can tell apart.
- **It is sized to fit the screen it opened on**, rather than to a number chosen on somebody
  else's monitor.
- **Arrows and HOME reach the guest** from the video window. The boards chapter's `Press`
  column is about those keys, and it works there.
- **Closing the window stops the guest**, instead of leaving a machine running with nothing to
  draw on.

Two of those were bugs rather than features, and are worth naming because the symptom was
confusing: typed input could lag by a whole frame, and the VDM-1 could repaint the screen
hundreds of times per emulated millisecond. Both are gone. The VDM-1's cursor also blinks on
the board's own oscillator now — wall-clock time, as the hardware did — so it blinks at the
same rate whether the CPU is running flat out or strapped to a 2 MHz crystal.

`SHOW DISPLAY` says which of the two has the keyboard. `[display]` in a machine file sets it
at startup; the configuring chapter has the table.

## Disk BASIC, as a worked example

`examples/diskbasic` boots **Altair Disk BASIC 4.1** off a floppy, media included — the fourth
worked example, alongside CP/M, cassette BASIC and the Sol-20. The examples all live in one
tree now (`examples/`), each with its own `README.md` saying what it is and what it needs.

## Which build is this?

```
altairsim> SHOW VERSION
  altairsim  0.2.0
  commit     v0.2.0
  tree       clean
```

`--version` says the same thing on the way past. This exists because *"`0.1.0`"* named every
build anyone had — a nightly, a CI artifact, a binary somebody was handed — and a bug report
against one of them could not be traced to the code that produced it. A binary now names the
commit it was built from, and says so plainly when the tree was dirty at build time.

## Smaller things

- **`writeprotect` is accepted wherever `readonly` is**, in machine files and at `SET`. They
  are the same property; `readonly` is what `SHOW` prints back.
- **The manual and the program say "board", not "card".** The bus takes boards, the command is
  `BOARDS`, and the two words no longer alternate mid-page.

## What has not changed

The holes named in the introduction are still holes. There is no snapshot and no replay, the
six reserved monitor verbs are still reserved, and there is still no audio. `SET BUS
UNCLAIMED` is documented in the design notes and is still not built.
