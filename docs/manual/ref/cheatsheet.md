<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# Quick reference

## Getting out, and back in

| Key | Does |
|---|---|
| `^E` | **ATTN** — stop the machine and take the keyboard back. Nothing is lost. |
| `RUN` | Resume, at the exact instruction it stopped on. |
| `QUIT` | Leave. (There is no `EXIT`.) |

## Command line

```
altairsim [options] [machine]

  machine            a built-in name, or a config file (has a '/' or ends .toml).
                     Omitted: ./altairsim.toml if there is one, else `default`.
  -m, --machine <n>  ALWAYS a built-in name -- never a file.
  -f, --file <path>  ALWAYS a file -- never a built-in name.
  -n, --none         empty backplane: no boards, no memory, nothing.
  -l, --list         list the built-in machines and exit.
  -s, --script <f>   run a command script, then exit with its status.
  -x, --exec <cmd>   run one monitor command (repeatable), then exit.
  -i, --interactive  after --script/--exec, stay in the monitor.
      --mcp          MCP server on stdio.
  -v, --version      -h, --help
```

## Monitor commands

Type the part before the bracket.

| Command | Usage |
|---|---|
| `D[UMP]` | `DUMP [<addr>\|<range>] [WIDTH=16]` |
| `S[TEP]` | `STEP [n]` |
| `N[EXT]` | `NEXT` |
| `R[UN]` | `RUN [addr]` |
| `H[ISTORY]` | `HISTORY [BUS\|CPU] [n]` |
| `M[OUNT]` | `MOUNT <id>[:<u>] <file> [WP]` |
| `B[REAK]` | `BREAK [<addr> [IF <expr>] \| MEM R\|W <addr> \| IO R\|W <port>] [TRACE ON\|OFF]` |
| `E[DIT]` | `EDIT <addr> [ROM]` |
| `C[ONFIG]` | `CONFIG LOAD <f.toml> \| CONFIG SAVE <f.toml>` |
| `SE[T]` | `SET <id>\|CONSOLE\|DISPLAY <k>=<v>` |
| `SH[OW]` | `SHOW <id>\|BOARDS\|BOARD <type>\|MACHINES\|MACHINE [<name>]\|BUS [MAP\|IO\|IRQ\|CONTENTION]\|ROMS\|MOUNTS\|PATHS\|CONSOLE\|DISPLAY\|SYMBOLS\|VERSION` |
| `DE[POSIT]` | `DEPOSIT <addr> <bytes...>` |
| `EX[AMINE]` | `EXAMINE [<addr>]` |
| `I[N]` | `IN <port>` |
| `O[UT]` | `OUT <port> <byte>` |
| `L[OAD]` | `LOAD <file> [AT <addr>] [FORMAT=BIN\|HEX] [ROM]` |
| `SA[VE]` | `SAVE <file> <range> [FORMAT=BIN\|HEX]` |
| `F[ILL]` | `FILL <range> <byte>` |
| `SEA[RCH]` | `SEARCH <range> <bytes...>\|"str"` |
| `COM[PARE]` | `COMPARE <range> <addr>` |
| `MOV[E]` | `MOVE <range> <dest> [ROM]` |
| `W[HO]` | `WHO <addr> \| WHO IO <port>` |
| `BO[ARDS]` | `BOARDS [LIST]\|ADD <type> <id> [k=v...]\|REMOVE <id>` |
| `RE[GS]` | `REGS \| SET REG <r>=<v>` |
| `REGI[ON]` | `REGION ADD <id> type=ram\|rom at=<addr> [size=\|mount=]` |
| `DI[SASM]` | `DISASM [<addr>\|<range>] [n] [CPU=8080]` |
| `SY[MBOLS]` | `SYMBOLS LOAD <file> [REPLACE] \| SYMBOLS CLEAR` |
| `U[NMOUNT]` | `UNMOUNT <id>:<u>` |
| `DISC[ONNECT]` | `DISCONNECT <id>:<u>` |
| `CONS[OLE]` | `CONSOLE [<k>=<v>...]` |
| `CONN[ECT]` | `CONNECT <id>:<u> <endpoint>` |
| `RES[ET]` | `RESET [CPU]` |
| `P[OWER]` | `POWER` |
| `T[RACE]` | `TRACE ON\|OFF [file] [MASK=IN,OUT,IRQ,DMA,CONTENTION]` |
| `TY[PE]` | `TYPE "text"` |
| `SN[APSHOT]` | `SNAPSHOT <file>` |
| `REST[ORE]` | `RESTORE <file>` |
| `NO[BREAK]` | `NOBREAK [id]` |
| `HE[LP]` | `HELP [<command>]` |
| `Q[UIT]` | `QUIT` |

## Boards

| Type | What it is |
|---|---|
| `memory` | RAM/ROM board: a list of regions, PHANTOM*, and five banking schemes |
| `8080` | MITS 88-CPU: an 8080A at 2 MHz. Decodes nothing -- it drives the bus |
| `z80` | Generic Z80 CPU board. Decodes nothing -- it drives the bus. The 88-CPU's twin, with a Z80 core |
| `2sio` | MITS 88-2SIO: two 6850 ACIAs, units 'a' and 'b'. Four ports at BASE+0..3 |
| `sio` | MITS 88-SIO: one COM2502 UART, unit 'tty'. Two ports at BASE+0..1. INVERTED status bits |
| `sbc` | SD Systems SBC-100/200: Z80 single-board computer. One 8-port block (78-7F): Intel 8251 console (unit 'tty', data 7C / status 7D, RxD->/DSR auto-baud for MSMONR21), Z80-CTC (78-7B) whose ch1 raises a mode-2 keyboard interrupt (vector 0x82) off the 8251 RxRDY, and a parallel port (7E/7F) whose OUT 7F bit 1 switches the onboard PROM out. Optional onboard boot PROM via [[board.socket]] (at+mount). variant=sbc100\|sbc200 |
| `dcdd` | MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives. Three ports at BASE+0..2. INVERTED status bits |
| `mds` | MITS 88-MDS: 5.25" minidisk, 4 drives. Same three ports as the dcdd -- but 300 RPM, 64 us/byte, and a motor that stops after 6.4 s |
| `hdsk` | MITS 88-HDSK Datakeeper: Pertec hard disk, 256-byte sectors from a linear .DSK. Eight ports at BASE+0..7 (default A0). Command/handshake protocol, four page buffers |
| `versafloppy` | SD Systems VersaFloppy I/II: WD FD177x soft-sector floppy, up to 4 drives. Eight ports at BASE+0..7 (default 60). variant=vfi (FD1771, single density) \| vfii (FD1791, single+double). Boots SDOS with the SBC-200 + DDBIOS |
| `acr` | MITS 88-ACR: cassette. An 88-SIO B + an FSK modem, unit 'tape'. Brings the WIND/REWIND/EXTRACT verbs and a tape counter |
| `uio` | MITS 88-UIO: serial + cassette on one board. A 6850 (unit 'serial', default 0x10) and an 88-ACR cassette section (unit 'tape', default 0x06) with motor control and a SW-1 MITS/Kansas-City modulation switch. Defaults reproduce the standard 0x10 + 0x06 layout |
| `c700` | MITS 88-C700: Centronics line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Output-only; CONNECT it to a file |
| `lpc` | MITS 88-LPC: 88-LP line-printer controller, unit 'prn'. Two ports at BASE+0..1 (default 02). Line-buffered: 6-bit codes + PRINT/LINE FEED/CLEAR. CONNECT it to a file |
| `pio` | MITS 88-PIO: 8-bit parallel port, units 'out'/'in'. Two ports at BASE+0..1 (default 04). CONNECT a printer, a keyboard, a socket |
| `4pio` | MITS 88-4PIO: up to four 6820 PIAs, sections ja/jb.. per port. 16 ports from BASE (default 20). Software-set direction; CONNECT each section |
| `vdm1` | Processor Technology VDM-1: memory-mapped 16x64 video, screen RAM at BASE (default CC00), scroll/status port (default CC). Needs a Display |
| `dazzler` | Cromemco Dazzler: color graphics from a framebuffer in main RAM. Two ports at BASE+0..1 (default 0E): control/status and format. 32x32 to 128x128, 16 colors/greys. Needs a Display |
| `vdb8024` | SD Systems VDB-8024: an 80x24 video terminal on one board -- the video console for an SBC-100/200 (the alternative to the 8251). Two I/O ports at BASE+0..1 (default 00): status/keyboard/display. Unit 'keyboard' (CONNECT). Boots sdmonv21. Needs a Display |
| `d7a` | Cromemco D+7A: analog + parallel I/O. Eight ports from BASE (default 18): one parallel port + seven two's-complement A/D-in/D/A-out channels. Reads 1-2 JS-1 joysticks from the host |
| `sol` | Processor Technology Sol-PC I/O: serial, keyboard, parallel, CUTS tape as one board. Seven ports F8..FE. Units serial/printer/keyboard (CONNECT) and tape1/tape2 (MOUNT). Brings the WIND/REWIND/EXTRACT verbs and a tape counter |
| `fp` | Altair front panel: the SENSE switches at port FF (read-only), and the lamps |
| `turnkey` | MITS 8800b Turnkey Module: phantom boot PROM (FC00-FFFF), integrated 6850 SIO (unit 'tty', default 0x10), sense switches at FF, and the Auto-Start JMP jam. Sockets via [[board.socket]] |
| `virtc` | MITS 88-VI/RTC: vectored interrupts (VI0-VI7 -> RST n) and a real-time clock. One port at FE |
| `hostbridge` | Host Bridge: guest <-> host file transfer, sandboxed. OUR OWN BOARD, not a period one. Two ports at BASE+0..1. R.COM/W.COM/HDIR.COM |

## Machines

| Machine | What it is |
|---|---|
| `4k` | The Altair as it actually left Albuquerque. |
| `acuter` | ACUTER at F000 -- CUTER on a plain Altair, with a terminal instead of a VDM. |
| `altmon` | An Altair with a monitor in ROM and a terminal on it. |
| `amon` | AMON 3.1 in a 4K EPROM at F000 -- Martin Eberhard's full-featured Altair monitor. |
| `basic4k` | The machine Altair 4K BASIC was sold to run on: a cassette in the ACR, a Teletype |
| `basic8k` | The machine Altair 8K BASIC was sold to run on: a cassette in the ACR, and a terminal |
| `cdbl` | The `default` machine with the Combo Disk Boot Loader in the PROM socket. |
| `cuter` | CUTER 1.3 driving a Processor Technology VDM-1 -- the real Sol/CUTS monitor. |
| `d7a` | A Cromemco Dazzler + D+7A + JS-1 joysticks -- the S-100 game console of 1976. |
| `dazzler` | A Cromemco Dazzler in an Altair -- the bench for the S-100's first color graphics card. |
| `default` | The machine you get when you name none: 56K, and the DBL boot PROM at FF00. |
| `lineprinter-lpc` | The `default` machine with an 88-LPC line printer at port 02, captured to a file. |
| `lineprinter` | The `default` machine with an 88-C700 line printer at port 02, captured to a file. |
| `minidisk` | The Altair Minidisk: an 88-MDS at 08, the MDBL boot PROM, and CP/M 2.2b on a 5.25" disk. |
| `parallel` | The `default` machine with two MITS parallel boards: an 88-PIO and an 88-4PIO. |
| `ps2` | The machine MITS Programming System II ran on. It is `basic8k`'s CARDS -- same 2SIO, same |
| `ps2int` | MITS Programming System II, WITH INTERRUPTS. `ps2` with A9 down and an 88-VI/RTC in it. |
| `sbc200` | SD Systems SBC-200 -- a 4 MHz Z80 single-board computer running the MSMONR21 monitor. |
| `sbc200v` | The SD Systems SBC-200 with a VDB-8024 VIDEO console -- a 4 MHz Z80 single-board |
| `sol20` | The Processor Technology Sol-20 -- an integrated 8080 machine, running SOLOS. |
| `turnkey` | The MITS 8800bt -- an Altair with a Turnkey Module where the front panel used to be. |
| `vdm1` | A Processor Technology VDM-1 in an Altair, and a demo that draws on it. |
| `z80` | A minimal Z80 machine: a `z80` CPU, 64K of RAM, and a 2SIO console. |

## A machine file, in one look

```toml
[machine]
name    = "mine"
base    = "default"        # start from a machine, and say what is DIFFERENT
startup = ["RUN FF00"]     # the operator's own keystrokes. There is no BOOT verb.

[[board]]                  # type + a NEW id      -> ADD the card
type = "2sio"              # type + an id from the base -> REPLACE it outright
id   = "sio0"              # NO type + an id      -> MODIFY the one already there
port = 10                  # remove = true        -> PULL THE CARD OUT

  [board.unit.a]           # a unit's own settings
  connect = "console"

  [[board.region]]         # a list the card owns (memory)
  type = "ram"
  at   = 0000              # hex: it is an address
  size = "56K"             # decimal: it is a size

  [[board.drive]]          # a list the card owns (disk controllers)
  unit  = 0
  mount = "cpm.dsk"        # relative to THIS FILE

[console]                  # the HOST's terminal -- not a board
strip7out = true
base      = octal          # read/print the wire class in split octal (MITS style)
```

**Paths:** a path *inside* a machine file is relative to **that file**. A path you
*type* is relative to **your shell**.

## Endpoints — `CONNECT <id>:<unit> <endpoint>`

| Endpoint | Is |
|---|---|
| `console` | the host terminal. Exactly one unit may hold it. |
| `null` | nowhere. Writes vanish, reads never come. |
| `loopback` | itself — what you write comes back. |
| `socket:PORT` | **listens** — this is telnet-in. |
| `socket:HOST:PORT` | **calls out**. |
| `serial:DEVICE` | a real serial port on this host. |

