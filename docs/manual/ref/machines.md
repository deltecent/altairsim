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
| `8085` | A minimal 8085 machine: an `8085` CPU, 64K of RAM, and a 2SIO console. |
| `acuter` | ACUTER at F000 -- CUTER on a plain Altair, with a terminal instead of a VDM. |
| `altmon` | An Altair with a monitor in ROM and a terminal on it. |
| `amon` | AMON 3.1 in a 4K EPROM at F000 -- Martin Eberhard's full-featured Altair monitor. |
| `basic4k` | The machine Altair 4K BASIC was sold to run on: an 88-SIO Teletype, a cassette in the ACR. |
| `basic8k` | The machine Altair 8K BASIC was sold to run on: an 88-2SIO terminal, a cassette in the ACR. |
| `cadzilla` | An HD63484 ACRTC graphics board with a Bt453 RAMDAC in an Altair -- the bench for CADzilla. |
| `default` | The machine you get when you name none: 56K, and the DBL boot PROM at FF00. |
| `icom` | iCOM FD3712 8" floppy machine -- boots CP/M 2.2 off a single-density iCOM disk. |
| `minidisk` | The Altair Minidisk: an 88-MDS at 08 and the MDBL boot PROM. You supply the 5.25" disk. |
| `northstar` | North Star MDS-A single-density floppy: the controller at E800 and its boot PROM. You supply the disk. |
| `northstardd` | North Star MDS-A-D double-density floppy: the controller at E800 and its boot PROM. You supply the disk. |
| `original` | The Altair as it actually left Albuquerque. |
| `ps2` | The machine MITS Programming System II ran on: basic8k's cards, but not basic8k's bootstrap. |
| `ps2int` | MITS Programming System II, WITH INTERRUPTS. `ps2` with A9 down and an 88-VI/RTC in it. |
| `rombasic` | MITS Extended ROM BASIC 16K -- Extended BASIC that runs directly out of ROM. |
| `sbc200` | SD Systems SBC-200 -- a 4 MHz Z80 single-board computer running the MSMONR21 monitor. |
| `sol20` | The Processor Technology Sol-20 -- an integrated 8080 machine, running SOLOS. |
| `tarbell` | Tarbell #1011 single-density floppy machine -- boots CP/M 2.2 the moment a disk is in it. |
| `tarbelldd` | Tarbell #2022 double-density floppy machine -- boots CP/M 2.2 the moment a disk is in it. |
| `turnkey` | The MITS 8800bt -- an Altair with a Turnkey Module where the front panel used to be. |
| `vg3sb` | A Vector System B: Z-80, 56K of RAM, Flashwriter II, Bitstreamer II, two Micropolis drives. |
| `vg3vip` | A Vector 3 VIP: ZCB processor, 56K of RAM, Flashwriter II console and one Micropolis drive. |
| `z80` | A minimal Z80 machine: a `z80` CPU, 64K of RAM, and a 2SIO console. |

