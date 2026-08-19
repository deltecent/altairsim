# What altairsim is

`altairsim` simulates the **MITS Altair 8800** and the **S-100 bus** it was built around.

It boots real software. Not software written to work with it — the actual artifacts, byte
for byte, as they were sold: Altair BASIC off a cassette, CP/M 2.2 off an 8″ floppy, MITS
Programming System II off paper tape's successor. None of it has been patched, and none of
it knows it is not running on a real machine.

The point of a simulator is usually to run the old software. This one is built the other way
round. It is a **bench for new hardware, and for the software that has to drive it** — which
are not two jobs, they are one job, and it is very good at running the old software besides.

The S-100 bus is a real object in the program, not a wiring diagram implied by the code.
Boards plug into it. They contend for addresses, pull the interrupt line, float the data bus
when nobody is driving it, and get the answer wrong in exactly the ways real boards did. So a
board you have not built yet can be *fitted* here, and the driver you have not finished can be
*run* against it, months before either exists in copper.

That is the whole of the argument, and it is an argument about **where you find your bugs.**
On real hardware a bug is a scope probe, a stubborn intermittent, and a machine that will not
tell you what it just did. Here it is a breakpoint on a bus cycle. You can stop the machine
mid-instruction, ask which board answered and which stayed silent, watch a driver poll a status
bit that will never come true, and run the same thing again and get the same answer — because
nothing here is intermittent, and nothing is hidden.

Every bug you kill on the bench is a bug you are not chasing at 2 MHz with a logic analyser.
And when you do finally power up the real board, the software has already run — so the faults
you are left with are the ones that are genuinely the *hardware's*: a timing margin, a noisy
line, a pin on the wrong side of a buffer. That is a bring-up you can finish. The one where
you cannot tell whether the board is wrong or the driver is wrong is the one that eats a month.

## What it does

- **An 8080 that is validated, not merely plausible — and a Z80 alongside it.** TST8080,
  8080PRE, CPUTEST and the full 8080EXM exerciser all pass — every one of the exerciser's CRC
  groups. Flags, carries, the undocumented behaviours, the lot. The Z80 clears the same bar,
  against ZEXDOC and ZEXALL.
- **A board for most of the machine**, all but one modelled from its own manual: CPU boards (an
  8080 and a Z80), RAM/ROM, serial boards, cassette interfaces, floppy controllers, a line-printer
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
  where your build found a print system, so a listing from 1977 comes out of the printer on
  your desk.
- **File transfer** between the host and CP/M, sandboxed. A board in the machine does the
  moving; the things you actually *type* — `HDIR`, `R` and `W` — are ordinary CP/M programs
  that live on a disk and run at the `A>` prompt, like `PIP` or `STAT`.

## What it does not do

This section is here because a manual that only lists strengths is an advertisement.

- **It runs an 8080, a Z80, or an 8085 — but the 8085's *undocumented* opcodes are not
  modeled.** The documented 8085 is here and validated against real silicon (RIM/SIM, the
  TRAP/RST 5.5/6.5/7.5 interrupts, and the whole documented set). What it does not run is the
  ten undocumented 8085 instructions (`DSUB`, `ARHL`, `LDHI`…) or the V/K flag bits some of
  them set — code that leans on those will not behave.
- **You can save state, but not replay.** `SNAPSHOT` writes the machine's whole state to a
  file and `RESTORE` reads it back, so you can save a machine and return to it. What you cannot
  do is *record* a session and step backwards through it, or replay a run from the start —
  there is no rewind.
- **The Altair itself had no video and no audio**, and a terminal on a serial port is its
  display, exactly as it was. But **video cards existed for the S-100 bus, and two of them are
  here**: the Processor Technology VDM-1, and the Sol-PC's integrated video. Both open a real
  window when the program was built with SDL3, and fall back to a headless display when it was
  not. **There is still no audio output** — nothing is ever sent to a speaker. Cassette `.WAV`
  files are read and written as *files*, which is a different thing, and the tapes chapter
  covers it.
- **Not every S-100 board is here.** The ones that are, are in the boards chapter. A PMMI
  modem is designed but not built.
- **Timing is honest, but it is not a circuit simulation.** Instructions cost the right
  number of T-states and a cassette takes the right number of them to load. Propagation
  delays and analogue behaviour are not modelled, and no software from the period could tell.

## What is in the box

You have the `altairsim` program, this manual, and a folder of worked examples with their
media — complete machines that boot, each with a README of its own saying what it is. More
machines are built into the program itself. It boots something the moment you unzip it; the
next chapter says what, and where further disks and tapes will come from.

There is no installer, no configuration, and nothing to set up. Unzip it and run it.
