<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# The built-in machines

A built-in is a machine file that lives **inside the binary** — the same format you
would write yourself, with nothing special about it. Name one and you get it in any
directory on earth:

```
$ altairsim basic4k
```

`altairsim --list` prints this table, and `altairsim -x 'SHOW MACHINE' <name>` shows
what is actually in one.

| Machine | What it is |
|---|---|
| `acuter` | ACUTER at F000 -- CUTER on a plain Altair, with a terminal instead of a VDM. |
| `altair680` | The Altair 680b -- MITS's second machine, and a different animal from the 8800. |
| `altmon` | An Altair with a monitor in ROM and a terminal on it. |
| `amon` | AMON 3.1 in a 4K EPROM at F000 -- Martin Eberhard's full-featured Altair monitor. |
| `basic4k` | The machine Altair 4K BASIC was sold to run on: an 88-SIO Teletype, a cassette in the ACR. |
| `basic8k` | The machine Altair 8K BASIC was sold to run on: an 88-2SIO terminal, a cassette in the ACR. |
| `cdbl` | The `default` machine with the Combo Disk Boot Loader in the PROM socket. |
| `cuter` | CUTER 1.3 driving a Processor Technology VDM-1 -- the real Sol/CUTS monitor. |
| `dazzler` | A Cromemco Dazzler in an Altair -- the bench for the S-100's first color graphics card. |
| `default` | The machine you get when you name none: 56K, and the DBL boot PROM at FF00. |
| `lineprinter-lpc` | The `default` machine with an 88-LPC line printer at port 02, captured to a file. |
| `lineprinter` | The `default` machine with an 88-C700 line printer at port 02, captured to a file. |
| `minidisk` | The Altair Minidisk: an 88-MDS at 08 and the MDBL boot PROM. You supply the 5.25" disk. |
| `original` | The Altair as it actually left Albuquerque. |
| `parallel` | The `default` machine with two MITS parallel boards: an 88-PIO and an 88-4PIO. |
| `ps2` | The machine MITS Programming System II ran on: basic8k's cards, but not basic8k's bootstrap. |
| `ps2int` | MITS Programming System II, WITH INTERRUPTS. `ps2` with A9 down and an 88-VI/RTC in it. |
| `rombasic` | MITS Extended ROM BASIC 16K -- Extended BASIC that runs directly out of ROM. |
| `sbc200` | SD Systems SBC-200 -- a 4 MHz Z80 single-board computer running the MSMONR21 monitor. |
| `sbc200v` | The SD Systems SBC-200 with a VDB-8024 VIDEO console: SDMONV21 on an 80x24 screen. |
| `sol20` | The Processor Technology Sol-20 -- an integrated 8080 machine, running SOLOS. |
| `tarbell` | Tarbell #1011 single-density floppy machine -- boots CP/M 2.2 the moment a disk is in it. |
| `tarbelldd` | Tarbell #2022 double-density floppy machine -- boots CP/M 2.2 the moment a disk is in it. |
| `turnkey` | The MITS 8800bt -- an Altair with a Turnkey Module where the front panel used to be. |
| `vdm1` | A Processor Technology VDM-1 in an Altair, and a demo that draws on it. |
| `z80` | A minimal Z80 machine: a `z80` CPU, 64K of RAM, and a 2SIO console. |

