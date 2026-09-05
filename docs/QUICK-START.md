# Quick start

**Boot CP/M with one command.** From the folder you unzipped this into:

```
$ altairsim examples/cpm/cpm22-buffered.toml
```

On **Windows**, the path is spelled with backslashes and the program is `altairsim.exe`:

```
> altairsim.exe examples\cpm\cpm22-buffered.toml
```

That is the whole of it. The machine file boots itself — you do not type a `BOOT` command,
because an Altair had none — and a few seconds later you are in CP/M:

```
56K CP/M 2.2b v2.3
For Altair 8" Floppy

A>
```

It is 1977. Type `DIR` to see what is on the disk:

```
A>DIR
```

An assembler, a debugger, Microsoft BASIC, a text editor, and a game or two are on there.
Run one — `A>MBASIC` starts Microsoft BASIC.

## The three keys to remember

You are talking to CP/M inside the machine. Two prompts share the window: `A>` is the guest,
and `altairsim>` is the simulator's own monitor. You move between them with these:

| | |
|---|---|
| **`^E`** (Control-E) | Leave the guest, back to the `altairsim>` monitor. The machine stops exactly where it stood and loses nothing. |
| **`RUN`** | Go back into the guest. It picks up where it left off. |
| **`QUIT`** | Done. (`Q` will do. There is no `EXIT`.)|

`^C` is *not* how you get out — that belongs to CP/M, which warm-boots on it. Use `^E`.

## The disk is real — there is no undo

The disk is mounted read/write, because that is what a machine with a disk in it is. Anything
you do in CP/M happens to the file on your host, and nothing keeps a spare copy. Two ways to
stay safe:

- **Look around read-only.** Add `RO` when you mount, and every write is refused at the
  controller so the file cannot change.
- **Copy the folder first** when you mean to write. It is self-contained and boots from
  anywhere:

  ```
  $ cp -R examples/cpm my-cpm
  $ altairsim my-cpm/cpm22-buffered.toml
  ```

One trap worth knowing: this CP/M **buffers a whole track** and only flushes it the next time
it reads the console, so **get back to the `A>` prompt before you quit or copy the image**, or
the last write never lands.

## No disk? Boot a tape instead

Every folder under `examples/` is a complete machine with its media already inside it, so any
of them comes up the moment you name it:

```
$ ls examples/
$ altairsim examples/basic/basic4k.toml
```

`examples/basic/` toggles in the MITS bootstrap by hand and loads **Altair 4K BASIC** off a
cassette — the machine that shows you what an Altair actually was. Each folder carries its own
`README.pdf` describing what it is and what to type.

## Where to go next

`altairsim-manual.pdf` is the full User Manual — the same quick start above at more length,
then the machines, the disks and tapes, the S-100 boards, and the file-transfer utilities in
`hostbridge/`. `altairsim-monitor.pdf` and `altairsim-debugger.pdf` cover the `altairsim>`
prompt and the built-in debugger. If you are coming from AltairZ80 (SIMH) or z80pack, read
`migrating.pdf` first.
