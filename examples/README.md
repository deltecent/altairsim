# examples

**Machines that boot.** Each directory here is self-contained: a `.toml` that describes the
machine, the media it needs lying beside it, and a note saying what you will see. Copy any one of
them anywhere and it still runs — a path inside a machine file resolves against **that file**, not
against the directory you launched from.

```
altairsim examples/cpm/cpm22-buffered.toml     # CP/M 2.2b on an 8" floppy
altairsim examples/basic/basic4k.toml          # Altair 4K BASIC, off a 1975 cassette
altairsim examples/sol/trek80.toml             # a Sol-20 at SOLOS, with TREK80 in the deck
```

| | What it is |
|---|---|
| [`cpm/`](cpm/) | Mike Douglas's track-buffered **CP/M 2.2b v2.3**, 56K, booted by the DBL PROM from an 8" floppy. `A>` in one command. |
| [`basic/`](basic/) | **Altair 4K BASIC 3.1** read off a period `.tap` by the bootstrap MITS shipped, unmodified. `MEMORY SIZE?` |
| [`sol/`](sol/) | A **Processor Technology Sol-20** running SOLOS 1.3, with the 1977 game **TREK80** on cassette. Type `XE TRK80`. |

**This tree is the product**, which is the reason it exists as one directory rather than as media
scattered through `disks/` and `tapes/`. It is what `tools/build-package.sh` assembles, it is what
`docs/manual/quick-start.md` promises, and `acceptance-examples` boots every one of these out of a
scratch directory with no repository in sight — because "does it work here" and "does it work where
we hand it to people" turned out to be different questions.

Nothing is duplicated. `disks/` and `tapes/` keep only what does **not** ship: the period `.ASM`
listings the boards were built from, download stubs for optional images, and vendor documentation.
