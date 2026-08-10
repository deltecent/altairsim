# examples

**Machines that boot.** Each directory here is self-contained: a `.toml` that describes the
machine, the media it needs lying beside it, and a note saying what you will see. Copy any one of
them anywhere and it still runs — a path inside a machine file resolves against **that file**, not
against the directory you launched from.

```
altairsim examples/cpm/cpm22-buffered.toml     # CP/M 2.2b on an 8" floppy
altairsim examples/cpm/cpm22-terminal.toml     # ...the same, console in a built-in VT100 window
altairsim examples/basic/basic4k.toml          # Altair 4K BASIC, off a 1975 cassette
altairsim examples/diskbasic/diskbasic.toml    # Altair Disk Extended BASIC 4.1 on an 8" floppy
altairsim examples/uio/uio.toml                # Altair 8K BASIC over ONE 88-UIO (serial + cassette)
altairsim examples/tarbell/tarbell.toml        # CP/M that boots itself from a Tarbell PROM
altairsim examples/hdsk/hdsk.toml              # CP/M off an 88-HDSK hard disk
altairsim examples/turnkey/floppy.toml         # an 8800bt Turnkey Module, no front panel
altairsim examples/cdos/cdos.toml              # Cromemco CDOS 2.58 off a 16FDC
altairsim examples/sdsys/sbc200.toml           # the SD Systems SBC-200 Z80 single-board computer
altairsim examples/sol/trek80.toml             # a Sol-20 at SOLOS, with TREK80 in the deck
altairsim examples/debugger/debugger.toml      # a bench for learning the symbolic debugger
altairsim examples/dazzler/kscope.toml         # a Cromemco Dazzler, drawing a kaleidoscope
altairsim examples/printing/printer.toml       # an 88-C700 printer, wired to a real host printer
altairsim examples/frontpanel/fp.toml          # the base machine, wired to the graphical front panel
```

| | What it is |
|---|---|
| [`cpm/`](cpm/) | Mike Douglas's track-buffered **CP/M 2.2b v2.3**, 56K, booted by the DBL PROM from an 8" floppy. `A>` in one command. `cpm22-terminal.toml` boots the same disk with its console in a **built-in VT100 window** the simulator draws itself — no telnet client, no external emulator. |
| [`basic/`](basic/) | **Altair 4K BASIC 3.1** (`basic4k.toml`) read off a period `.tap` by the bootstrap MITS shipped, unmodified: `MEMORY SIZE?`. Beside it, **8080 BASIC VER 1.0** (`basic1.toml`) — the first Altair BASIC, 1975 — booted the raw two-step way its primitive bootstrap forced: `RUN 1800`, `^E`, `RUN 0` → `MEMSIZ?`. |
| [`diskbasic/`](diskbasic/) | **Altair Disk Extended BASIC 4.1** (MITS, 1977) on an 8" floppy behind an 88-DCDD, booted by the DBL PROM. Unlike the cassette BASIC, this one has a filesystem — `SAVE` by name, a directory, `DSKINI`. `MEMORY SIZE?` |
| [`uio/`](uio/) | **Altair 8K BASIC 3.2** over a single **88-UIO** — the board that is a 6850 serial port (at 0x10) *and* an 88-ACR cassette (at 0x06) in one. The period bootstrap runs unmodified. `MEMORY SIZE?` |
| [`tarbell/`](tarbell/) | The **Tarbell** single- and double-density floppy interfaces, which carry their own 32-byte boot PROM: power on with a disk in the drive and 48K **CP/M 2.2** comes up — no monitor, nothing to type. |
| [`hdsk/`](hdsk/) | **CP/M 2.2** booting off an Altair **88-HDSK "Datakeeper"** hard disk — an outboard controller moving whole sectors over an 88-4PIO — loaded by the HDBL PROM. `A0>` |
| [`turnkey/`](turnkey/) | The **MITS 8800bt Turnkey Module**: an Altair with no front panel that boots itself the moment it powers on. Two files boot CP/M two ways — off an 88-DCDD floppy and off an 88-HDSK hard disk. |
| [`cdos/`](cdos/) | **Cromemco CDOS 2.58**, Cromemco's CP/M work-alike, booted from an 8" diskette through a **Cromemco 16FDC** — the board carrying the disk controller, console UART, and boot PROM at once. |
| [`sdsys/`](sdsys/) | The **SD Systems SBC-200**, a whole Z80 computer on one S-100 board (8251 console, Z80-CTC, boot PROMs) running the **MSMONR21** monitor; with a VersaFloppy it boots SDOS or CP/M. |
| [`sol/`](sol/) | A **Processor Technology Sol-20** running SOLOS 1.3, with the 1977 game **TREK80** on cassette. Type `XE TRK80`. |
| [`debugger/`](debugger/) | A 46-byte program with its **symbols** and a guided walk through the monitor's debugger: `SYMBOLS LOAD`, symbolic `DISASM`, single-step, break on a label, run. |
| [`dazzler/`](dazzler/) | A **Cromemco Dazzler**, the S-100's first color graphics card, running Li-Chen Wang's **Kaleidoscope**. Comes up drawing a four-way-mirrored pattern in a window; ATTN (Ctrl-E) breaks back to the monitor. |
| [`pmmi/`](pmmi/) | A **PMMI MM-103 modem** and a small 8080 **terminal** program that bridges the console to the phone line. Four control keys work the modem — `^D` DTR, `^S` the 6860 Self Test loopback, `^I` modem status, `^C` quit. `^D` then `^S` makes the card echo your keystrokes with no phone line attached; set `dial`/`answer` to place a real call over TCP. |
| [`printing/`](printing/) | An **88-C700 line printer**, and a banner program that prints through it. The README sets up a real printer on your host — a network printer over `socket:`, or a CUPS queue over `printer:` — and a page comes out. Per-OS host setup (macOS, Linux; Windows pending). |
| [`frontpanel/`](frontpanel/) | The base Altair with its **front panel connected to altairsim-fp** — a *separate* program (its own repo) that draws the panel's lamps and switches in a window and listens on **TCP 8800**. Loads to the `altairsim>` prompt; the window mirrors the running machine, and the on-screen sense switches feed a guest `IN 0FFH`. |
| [`ai-mcp/`](ai-mcp/) | A working directory for an **AI assistant driving altairsim over MCP**: a CP/M machine and a tiny `HELLO.ASM` with one deliberate bug the assistant assembles, runs, single-steps to find, and fixes — all through the simulator's MCP tools. See `DRIVING-WITH-AI.md`. |

**This tree is the product**, which is the reason it exists as one directory rather than as media
scattered through `disks/` and `tapes/`. It is what `tools/build-package.sh` assembles, it is what
`docs/manual/quick-start.md` promises, and `acceptance-examples` boots every one of these out of a
scratch directory with no repository in sight — because "does it work here" and "does it work where
we hand it to people" turned out to be different questions.

Nothing is duplicated. `disks/` and `tapes/` keep only what does **not** ship: the period `.ASM`
listings the boards were built from, download stubs for optional images, and vendor documentation.
