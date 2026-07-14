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
card you have not built yet can be *fitted* here, and the driver you have not finished can be
*run* against it, months before either exists in copper.

That is the whole of the argument, and it is an argument about **where you find your bugs.**
On real hardware a bug is a scope probe, a stubborn intermittent, and a machine that will not
tell you what it just did. Here it is a breakpoint on a bus cycle. You can stop the machine
mid-instruction, ask which card answered and which stayed silent, watch a driver poll a status
bit that will never come true, and run the same thing again and get the same answer — because
nothing here is intermittent, and nothing is hidden.

Every bug you kill on the bench is a bug you are not chasing at 2 MHz with a logic analyser.
And when you do finally power up the real board, the software has already run — so the faults
you are left with are the ones that are genuinely the *hardware's*: a timing margin, a noisy
line, a pin on the wrong side of a buffer. That is a bring-up you can finish. The one where
you cannot tell whether the board is wrong or the driver is wrong is the one that eats a month.

## What it does

- **An 8080 that is validated, not merely plausible.** TST8080, 8080PRE, CPUTEST and the
  full 8080EXM exerciser all pass — every one of the exerciser's CRC groups. Flags, carries,
  the undocumented behaviours, the lot.
- **Ten board types**, each modelled from its own manual: the CPU card, RAM/ROM, two serial
  cards, a cassette interface, two floppy controllers, a vectored-interrupt/real-time-clock
  card, the front panel, and one card of our own for moving files in and out.
- **A monitor** — the prompt you get when the machine is not running — with breakpoints,
  single-stepping, disassembly, memory examine and deposit, and a view of the bus itself:
  who decodes what, who is pulling which interrupt line, and where two cards are fighting.
- **Real I/O.** A serial card can be wired to your terminal, to a TCP socket (so you can
  telnet into the guest), or to an actual serial port on your machine, with the modem
  control lines wired through.
- **File transfer** between the host and CP/M, sandboxed. A card in the machine does the
  moving; the things you actually *type* — `HDIR`, `R` and `W` — are ordinary CP/M programs
  that live on a disk and run at the `A>` prompt, like `PIP` or `STAT`.

## What it does not do

This section is here because a manual that only lists strengths is an advertisement.

- **It is an 8080.** No Z80, no 8085. Software that needs them will not run.
- **Eight monitor commands are reserved but not built** — `TRACE`, `SNAPSHOT`, `RESTORE`,
  `RECORD`, `REPLAY`, `HISTORY`, `EDIT` and `STOP`. They **resolve**: type `T` and you are
  told that `TRACE` is waiting on the debugger, rather than being told nothing at all. That
  is deliberate — their abbreviations are claimed *now*, so that the day they land, `T` does
  not silently stop meaning what your fingers think it means.
- **There is no snapshot, no replay, and no execution trace.** When you stop the machine,
  you stop it; you cannot rewind it.
- **There is no video and no audio.** The Altair had neither. A terminal on a serial port is
  the display, exactly as it was.
- **Not every S-100 card is here.** The ones that are, are in the boards chapter. A Tarbell
  disk controller and a PMMI modem are designed but not built.
- **Timing is honest, but it is not a circuit simulation.** Instructions cost the right
  number of T-states and a cassette takes the right number of them to load. Propagation
  delays and analogue behaviour are not modelled, and no software from the period could tell.

## What is in the box

You have the `altairsim` program, this manual, and some disks and tapes. The next chapter
says what they are.

There is no installer, no configuration, and nothing to set up. Unzip it and run it.
