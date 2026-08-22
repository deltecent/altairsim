# What altairsim is

`altairsim` simulates the **MITS Altair 8800** and the **S-100 bus** it was built around.

It boots real software — not software written to work with it, but the actual artifacts,
byte for byte, as they were sold: Altair BASIC off a cassette, CP/M 2.2 off an 8″ floppy,
MITS Programming System II off paper tape. None of it has been patched, and none of it knows
it is not running on a real machine.

The S-100 bus is a real object in the program, not a wiring diagram implied by the code.
Boards plug into it, contend for addresses, pull the interrupt line, and float the data bus
when nobody is driving it — getting the answer wrong in exactly the ways real boards did. That
makes the machine a **bench**: a board you have not built yet can be fitted here, and the
driver you have not finished can be run against it, before either exists in copper. And it
makes bugs findable — you can stop the machine mid-instruction, ask which board answered and
which stayed silent, and run the same thing again and get the same answer, because nothing
here is intermittent and nothing is hidden.

## What it does

- **An 8080 and its binary-compatible successor the 8085**, with a Z80 alongside — each faithful
  down to the flags, the carries and the undocumented behaviours, and each checked against the
  standard processor exercisers before any board is built on it. The Altair 680b's Motorola 6800
  is here too.
- **A board for most of the machine**, all but one modelled from its own manual: CPU boards,
  RAM/ROM, serial boards, cassette interfaces, floppy and disk controllers, a line-printer
  controller, video displays, the Sol-PC's integrated I/O, a vectored-interrupt/real-time-clock
  board, the front panel, and one board of our own for moving files in and out. The boards
  chapter has the whole list.
- **A monitor** — the prompt you get when the machine is not running — with breakpoints (plain
  or conditional), single-stepping, disassembly, memory examine and deposit, a bus-cycle trace
  and a history ring, and a view of the bus itself: who decodes what, who is pulling which
  interrupt line, and where two boards are fighting.
- **Real I/O.** A serial board can be wired to your terminal, to a TCP socket (so you can
  telnet into the guest), or to an actual serial port on your machine, with the modem
  control lines wired through. A line-printer board can be wired to a **real print queue**,
  so a listing from 1977 comes out of the printer on your desk.
- **File transfer** between the host and CP/M, sandboxed. A board in the machine does the
  moving; the things you actually *type* — `HDIR`, `R` and `W` — are ordinary CP/M programs
  that run at the `A>` prompt, like `PIP` or `STAT`.

## What it does not do

This section is here because a manual that only lists strengths is an advertisement.

- **The 8085's on-chip serial and interrupt *pins* connect to nothing.** Every documented
  8085 instruction runs, and so do the undocumented opcodes — but no card on the S-100 bus
  carries the `SID`/`SOD` serial lines or the `TRAP`/`RST 5.5`/`6.5`/`7.5` inputs, so code
  that bit-bangs a console on those pins has nothing on the other end. Ordinary interrupts,
  over the shared `INTR` line, work exactly as for the 8080.
- **You can save state, but not replay.** `SNAPSHOT` writes the machine's whole state to a
  file and `RESTORE` reads it back. What you cannot do is *record* a session and step
  backwards through it — there is no rewind.
- **The Altair itself had no video and no audio.** A terminal on a serial port is its display,
  as it was — but **video cards existed for the S-100 bus, and several are here** (the VDM-1,
  the Dazzler, the SD Systems VDB-8024, the Sol-PC). Each opens a real window when the program
  was built with SDL3, and falls back to a headless display when it was not. **There is no
  audio output.** Cassette `.WAV` files are read and written as *files*, which the tapes
  chapter covers.
- **Not every S-100 board is here**, and timing, while honest, is not a circuit simulation:
  instructions cost the right number of T-states and a cassette takes the right number of them
  to load, but propagation delays and analogue behaviour are not modelled — and no software
  from the period could tell.

## What is in the box

You have the `altairsim` program, this manual, and a folder of worked examples with their
media — complete machines that boot, each with a README of its own. More machines are built
into the program itself. It boots something the moment you unzip it; the next chapter says
what, and where further disks and tapes come from.

There is no installer and nothing to set up. Unzip it and run it.
