# Boards

An Altair is a **backplane**, and everything in it is a board. The memory, the serial ports, the
disk controller, the front panel and the processor are all boards. Without the boards, only the
bus is left.

`altairsim` works in the same way. For this reason, the crystal is a property of the processor
*board*, and the sense switches are a property of the *front panel*. You can remove a board, add
a different one, and see what the software does.

This chapter starts with the commands that add, remove and show boards. After that, it tells you
what each board **is**: what the real hardware was, what it is for, and what can go wrong. **It
does not list the properties of each board.** The board reference at the back of this manual
lists every property of every board, from the program's own tables.

## Add, remove and look at boards

You can do everything by hand that a machine file does to a board.

| Command | |
|---|---|
| `BOARDS` | what is in the backplane |
| `SHOW BOARDS` | the board types that you can add, one line each |
| `SHOW BOARD <type>` | the description and settings of one type (add `UNITS` for only the units) |
| `BOARDS ADD <type> <id>` | add a board |
| `BOARDS REMOVE <id>` | remove a board |
| `SHOW <id>` | the settings of one board in the machine, with the values that it accepts |
| `SET <id> <key>=<value>` | change one setting |

```
altairsim> BOARDS ADD virtc vi0
altairsim> SET vi0 rtc_source=line
altairsim> SHOW vi0
```

**The keys at the prompt are the keys in a machine file.** `SET cpu0 clock_hz=2000000` at the
prompt and `clock_hz = 2000000` in a machine file set the same property.

When you have the machine that you want, type:

```
altairsim> CONFIG SAVE mine.toml
```

This writes the machine to a file, and the file loads back as the same machine.

### Look at a board type before you add it

`SHOW BOARD <type>` reads the *catalog*. It makes one board of that type, describes it, lists
its settings, and removes it again. Nothing is added to the backplane. For a disk controller,
the last lines name its units:

```
altairsim> SHOW BOARD dcdd
  dcdd  MITS 88-DCDD: 8" hard-sector floppy, up to 16 drives...
  ...
  This board has units: drive
  SHOW BOARD dcdd UNITS for their properties.
```

Add `UNITS` to see only the units. Each unit has a heading with its **kind**, and after the
kind, the **command that fills it**:

```
altairsim> SHOW BOARD sol units

  Unit 'serial'  (serial, CONNECT)

  PROPERTY   HELP
  ---------  -----------------------------------------------------------------
  connect    The endpoint on the other end of this line (CONNECT sets this)
  ...

  Unit 'tape1'  (tape, MOUNT)
  mode       Which way the bytes go: play loads from the file, record saves to it
  ...
```

- A **serial** unit is an endpoint, so you `CONNECT` it, to `console`, a socket or a real port.
- A **disk**, **rom** or **tape** unit holds an image, so you `MOUNT` a file in it.
- A **cpu** unit is fixed on the board. It takes neither command, and the heading shows only its
  kind.

The heading tells you which command a unit takes, before you add the board.

When a *machine file* fills a unit, `UNITS` shows the TOML table and its keys instead. For a
disk controller, this is `[[board.drive]]` with its `mount`, `readonly` and `media` keys. You
can find the file form as easily as the prompt form.

## Contention

When two boards decode the same port, that is **contention**, and it happened on real backplanes
too. Both boards answer and both drive the data bus, so the processor reads the wrong value.

The simulator does not stop you from doing this. **It tells you about it.** For example, add an
`mds` to a machine that already has a `dcdd`:

```
altairsim> BOARDS ADD mds mds0
mds0: mds added
altairsim> SHOW BUS CONTENTION
  port 08 OUT driven by dsk0 mds0
  port 09 OUT driven by dsk0 mds0
  port 0A OUT driven by dsk0 mds0
```

The output names each port and both boards. You can build a machine that does not work, on
purpose, because a wrong setup is part of developing hardware. The program shows you what is
wrong.

## The boards

The boards are in groups, in the same order as the sections below.

**The whole machine**

| Type | What it is |
|---|---|
| `fp` | the front panel |
| `turnkey` | MITS 8800b Turnkey Module: the Altair with no front panel, on one board |

**Memory**

| Type | What it is |
|---|---|
| `memory` | RAM and ROM, as a list of regions |
| `bankmem` | bank-switched RAM: Vector Graphic, Cromemco 64KZ, North Star HRAM, ExpandoRAM II |
| `v2z80rom` | the onboard MASTER monitor ROM of the S100Computers V2 Z80 processor board. A paged EEPROM, not a processor |

**Processors**

| Type | What it is |
|---|---|
| `8080` | the MITS 88-CPU |
| `8085` | an 8085 processor board. It runs all 8080 code, and adds RIM, SIM and the TRAP and RST interrupts |
| `z80` | a Z80 processor board. The same bus, with a different instruction set |

**Serial ports and consoles**

| Type | What it is |
|---|---|
| `2sio` | MITS 88-2SIO: two serial ports. The usual console |
| `sio` | MITS 88-SIO: one serial port. The first MITS serial board |
| `sbc` | SD Systems SBC-100/200: the serial console, timer and parallel port of a Z80 single-board computer |
| `gsio` | a generic serial board with two channels, set up with straps |
| `io4` | SSM IO-4: two serial ports and four parallel ports |
| `propio` | S100Computers Console I/O: a serial console built on a Propeller chip |
| `pmmi` | PMMI MM-103: a Bell 103 telephone modem on one board |

**Cassette**

| Type | What it is |
|---|---|
| `acr` | MITS 88-ACR: the cassette interface |
| `uio` | MITS 88-UIO: a serial port and a cassette interface on one board |

**Printers**

| Type | What it is |
|---|---|
| `c700` | MITS 88-C700: the line-printer controller. Capture to a file |
| `lpc` | MITS 88-LPC: the other line-printer controller, with a line buffer |

**Parallel and analog I/O**

| Type | What it is |
|---|---|
| `pio` | MITS 88-PIO: an 8-bit parallel port, in and out |
| `4pio` | MITS 88-4PIO: up to four programmable parallel ports |
| `d7a` | Cromemco D+7A: analog and parallel I/O. Reads joysticks |

**Floppy and disk controllers**

| Type | What it is |
|---|---|
| `dcdd` | MITS 88-DCDD: the 8″ floppy controller |
| `mds` | MITS 88-MDS: the 5¼″ minidisk controller |
| `fdcplus` | FarmTek FDC+: the 1.5 MB floppy, or the serial drive, where the disk images are on a drive server on a serial line |
| `hdsk` | MITS 88-HDSK: the Datakeeper hard-disk controller |
| `versafloppy` | SD Systems VersaFloppy I/II: a soft-sector floppy controller. Boots SDOS |
| `tarbell` | Tarbell #1011: a single-density floppy controller with its own boot PROM. Boots CP/M by itself |
| `tarbelldd` | Tarbell #2022: the double-density version, for mixed-density disks |
| `icom` | iCOM FD3712/FD3812: an 8″ floppy controller with its own boot PROM. Boots CP/M and FDOS |
| `16fdc` | Cromemco 16FDC: a floppy controller with a console UART and the RDOS 2.52 boot PROM. Boots CDOS |
| `64fdc` | Cromemco 64FDC: a floppy controller with a console UART and the RDOS 3.12 boot PROM. Boots CDOS |
| `dualsd` | S100Computers Dual SD: two microSD cards as CP/M drives. Boots CP/M 3 |
| `dualide` | S100Computers IDE-AB: two CompactFlash cards as CP/M drives. Boots CP/M 3 |

**Video and display**

| Type | What it is |
|---|---|
| `vdm1` | Processor Technology VDM-1: memory-mapped video. Needs a display |
| `dazzler` | Cromemco Dazzler: color graphics. Needs a display |
| `vdb8024` | SD Systems VDB-8024: an 80×24 video terminal on one board. Needs a display |
| `sol` | Processor Technology Sol-PC: the onboard I/O of the Sol-20, on one board |

**Interrupts and the clock**

| Type | What it is |
|---|---|
| `virtc` | MITS 88-VI/RTC: vectored interrupts and a clock |
| `ss1` | CompuPro System Support 1: a real-time clock, a serial channel, an interval timer and two interrupt controllers |
| `rtc100` | SciTronics RTC-100: a battery-backed real-time clock, with an optional interrupt each second |

**Host integration**

| Type | What it is |
|---|---|
| `hostbridge` | file transfer to your computer. **Our own board, not a period board** |

**PROM programmers**

| Type | What it is |
|---|---|
| `pb1` | SSM PB1: program a 2708 or 2716 EPROM, and save it as a hex file |

## The whole machine

The front panel, and the turnkey board that replaces it.

### `fp`: the front panel

The front panel is **a board**, because it was one on a real Altair. It plugged into the bus
like the other boards. On a real Altair, the panel was how you toggled a bootstrap into memory.
Here, the monitor's `DEPOSIT` does that, and the board gives the machine its sense switches.

#### The SENSE switches, at port `FF`

The sense switches are the eight switches on the left of the address switches, `SA8` to `SA15`.
**`IN FFH` reads them.** They are **read-only**. An `OUT FF` does nothing on this board.

`SA15` is the **top** bit of the byte, and `SA8` is the bottom bit. The bits are in the same
order, left to right, as the switches on the panel:

```
switch   SA15  SA14  SA13  SA12  SA11  SA10   SA9   SA8
bit         7     6     5     4     3     2     1     0
value    0x80  0x40  0x20  0x10  0x08  0x04  0x02  0x01
```

This makes a period boot procedure easy to follow. "Raise A15 and A11" is `0x80` plus `0x08`, or
`0b10001000` when you write it to look like the panel.

**The switches are important.** Period bootstraps read the sense switches to decide **what to
boot from**: which device, at which port, and at which speed. For this reason, every tape
machine in this package sets them:

```
sense = 0x80        # basic4k: load from the cassette
sense = 0b10000000  #          the same eight switches, drawn
sense = 0x8E        # ps2:     the 2SIO, and interrupts off
sense = 0b10001110  #          again, one digit per switch
```

`0b` is not special to this board. It works for any number, at the prompt and in a machine file.
*The Monitor* gives the full list of prefixes. Binary is easier to read for eight switches.
However you write the value, `SHOW fp0` shows it in hex.

If you set the switches wrong, the loader reads a device that is not there, and it gives no
error.

`sense` is a **board property**, because the switches are on the panel. There is no `sense` for
the machine, and the program gives an error if you try to set one.

### `turnkey`: the MITS 8800bt, on one board

The 8800b "turnkey" system had **no front panel**. One board, the Systems Turnkey Module, did
the work of the panel and more. It had the boot PROM, the serial port for the terminal, the
sense switches, and a circuit that booted the machine when you turned it on. This board is that
board, so a `turnkey` machine has **no `fp` and no separate `2sio`**. All three are on this
board. `altairsim turnkey` is the plain machine. Give it a disk and one of the two boot loaders
below (`start = "FF00"` for the floppy disk, `FC00` for the hard disk), and CP/M starts when the
machine turns on.

#### It boots itself

There is no front panel to toggle in a bootstrap, so the board has an **Auto-Start** circuit.
`RUN 0000` starts the processor at address 0, and the board puts a `JMP` on the bus, so that the
boot PROM runs first. This is what the START switch on the panel did. The `start` property is
the START ADDR switches: `FF00` runs the floppy loader, and `FC00` runs the hard-disk loader.
With `startup = ["RUN 0000"]` in the machine file, this happens when the program starts.

#### The boot PROM gets out of the way

The PROM is at `FC00`–`FFFF`. **A read there gets the PROM, not the RAM under it,** until the
first `IN` from port `FE` or `FF`. The PROM then switches itself off, and the machine has the
**full 64 KB** of RAM. For this reason, this machine has 64K, and a front-panel Altair has 56K.
The PROM does not use the top of memory all the time. It is a boot device that gets out of the
way. The same `IN FF` that reads the sense switches switches it off. Period software, such as
Altair BASIC and the CP/M loaders, gets the full 64K in this way without knowing about it.

#### The console and the sense switches

The serial console is the `tty` unit, at port `10h`. It works like channel A of a `2sio`, so the
same software drives it. The sense switches are at port `FF`, as on the front panel. This board
uses `FF`, so **do not put an `fp` in a `turnkey` machine**. Both boards would answer the port.

The PROM sockets are a list, like the regions of a memory board:

```
[[board.socket]]
at    = 0xFC00      # socket L1: the hard-disk loader
mount = "builtin:hdbl"
```

## Memory

RAM, ROM, and the boards that switch banks of RAM.

### `memory`: RAM and ROM

A memory board is **a list of regions**. A real S-100 memory board was the same. It had banks of
chips, and each bank decoded the addresses that its jumpers set. A board with 56K of RAM at the
bottom and a 256-byte boot PROM at `FF00` was an ordinary board.

For this reason, `default` has one memory board, and that board holds both the 56K and the PROM.

#### `PHANTOM*`: how a boot PROM gets out of the way

The bus has a line called `PHANTOM*`. **A board pulls this line to switch another board off.**
When the processor reads the PROM at `FF00`, the PROM asserts `PHANTOM*`. The RAM board under it
stops answering, if its jumper tells it to obey `PHANTOM*`. Two boards decode `FF00`, and only
one answers.

This is how an Altair with disks boots. The PROM covers the RAM at the top of memory, and the
loader runs from the PROM. After that, **the loader switches the PROM off**, and the RAM under
it can be used again. CP/M needs that memory.

Whether a board obeys `PHANTOM*`, and whether it asserts it, are **jumpers** on the board. You
set them. If you set them wrong, the machine does not boot, and it does not tell you why. The
bus view in the monitor shows you both boards on the same page.

A plain `memory` board has no bank switching. Bank switching is a different board, `bankmem`.

### `bankmem`: bank-switched RAM

When 64K was not enough, S-100 makers put several planes of RAM at the same addresses. A
write-only **select port** chose which plane the processor saw. Each maker did this in a
different way, so `bankmem` is **one board with four decoders**. Its `card` property selects the
decoder:

| `card` | Real board | Select port | What a write does |
|---|---|---|---|
| `vector` | Vector Graphic 64K | `40` | **one bit for each bank**: `01`→bank 0, `02`→1, `04`→2 … `80`→7 |
| `cromemco64kz` | Cromemco 64KZ / 64KZ-II | `40` | **8-bit mask**: bit *N* turns bank *N* on. **Several banks can be on** (`28`→banks 3 and 5) |
| `northstar` | North Star HRAM | `C0` | bit 0 = on or off, bits 1–7 = which bank. Banks change **one at a time** |
| `expandoram2` | SD Systems ExpandoRAM II | `FF` | the byte is a **page number** (see the note below) |

`banks` sets the number of planes on the board: up to 8 for `vector` and `cromemco64kz`, 6 for
`northstar`, and 10 for `expandoram2`. `fill` and `seed` work as they do on `memory`. The guest
writes the select port. At the monitor, you can write it yourself with `OUT`, and `SHOW` lists
every plane and which plane is on.

The package has no operating system that uses banks. The quickest way to see the board work is
from the monitor:

```
$ altairsim bankmem
altairsim> OUT 40 01
port 40 <- 01
altairsim> DEPOSIT 1000 A0
altairsim> OUT 40 08
port 40 <- 08
altairsim> DEPOSIT 1000 B3
altairsim> OUT 40 01
port 40 <- 01
altairsim> DUMP 1000-1000
1000  A0                                                .
altairsim> OUT 40 08
port 40 <- 08
altairsim> DUMP 1000-1000
1000  B3                                                .
```

`OUT 40 08` selects bank 3, because bit 3 is on. It does not select bank 8. The two `DUMP`
commands read different bytes at the same address, because the plane changed.

> **The `expandoram2` page decode is an approximation.** The real board sends the page number
> through a PROM on the board, and that PROM map is not published in a form that we can copy.
> For this reason, `bankmem` selects a plain page. The two stock partitions of the PROM are
> real: `partition = "ex48"` gives 48K of banked RAM and 16K of common RAM at `C000`, and
> `partition = "ex32"` gives 32K of banked RAM and 32K of common RAM at `8000`. `ram` sets the
> RAM on the board, and the program calculates the number of banks from it. The other three
> cards are exact.

### `v2z80rom`: the S100Computers V2 Z80 processor board's onboard monitor ROM

**This board is not a processor.** It is the **monitor ROM** on a real S100Computers V2 Z80
processor board. That ROM is an **8K EEPROM** that holds John Monahan's MASTER V6.6 ROM monitor.
A machine that uses this board also needs a `z80` board (below) for the processor. The real
board has the Z80 and its own firmware on one board, and here they are two boards.

The EEPROM has **two 4K pages**, and both pages use the addresses `F000`–`FFFF`. A write to port
`D3` selects the page. The same write can also switch the EEPROM off, so that the RAM under it
can be used. CP/M does this after it boots, to get a flat 64K of RAM. While the EEPROM is on, a
read in its window gets the EEPROM, not the RAM.

There is no `BOOT` command. **The monitor boots the machine.** `startup = ["RUN F000"]` starts
the monitor. At its `->` prompt, the **`I` command** boots CP/M 3 from a Dual SD card (below).
The `dualsd` machine uses this board.

## The processor boards

`altairsim` has three S-100 processor boards: `8080`, `8085` and `z80`. This section applies to
all three. The sections after it describe each one.

**The processor is a board like the other boards.** It plugs into the backplane, and you can
remove it. With `-n`, you can build a machine with no processor. The processor board decodes no
ports and no addresses. It runs the program, and it drives the bus to do that. You can put a
`z80` where an `8080` was, and the bus, the other boards and the debugger all work the same.

Every processor board has the same three properties:

- **`clock_hz`**, the crystal
- **`idle`**, which lets the processor rest at a prompt
- **`achieved_hz`**, a read-only value: the speed that the processor reached

### The crystal is on the board: `clock_hz`

The crystal is on the processor board, so `clock_hz` is a property of the board, not of the
machine. **`clock_hz = 0` is the default, and it means "run as fast as possible".** On a modern
computer, that is more than a hundred times as fast as a real Altair.

To run at the speed of a real 2 MHz Altair, type:

```
SET cpu0 clock_hz=2000000
```

**The guest sees the same result at either speed.** Each instruction takes the same number of
T-states. The crystal changes how the machine feels to you, not what it does. A cassette has its
own speed setting, `rate`, which the tapes chapter describes.

There is one exception, at the edge of the machine. A guest counts instructions to measure time.
At full speed, a guest's "three-second" timeout passes in a few milliseconds of your time. For
this reason, a guest that times something outside the machine, such as XMODEM through a serial
port, needs the real crystal. The troubleshooting chapter tells you more.

To see how much time has passed for the guest, type `SHOW CLOCK`:

```
altairsim> SHOW CLOCK
clock  (emulated time -- T-states since POWER, and what they are worth)

  elapsed    0.102953 s   (205905 T-states)
  crystal    2000000 Hz   SET cpu0 clock_hz=N
  pacing     paced -- emulated seconds keep step with real ones
```

The program counts the T-states since power on, and divides them by the crystal. The result is
the guest's time, not your time. A CP/M boot takes the same guest time at full speed and at 2
MHz. Use this number to measure a guest's own timeout.

### `idle`: the processor rests at a prompt

At a prompt, a guest only reads the serial status register again and again, and waits for a key.
At full speed, that keeps one core of your computer busy. **`idle` lets the processor rest while
the guest reads an empty keyboard.** It is on by default. The busy core then uses a few percent.
**The guest cannot see the difference**, because the processor starts again as soon as a byte
arrives, before the next read. An XMODEM transfer is correct with `idle` on.

### `8080`: the MITS 88-CPU

The original processor board. The Altair shipped with it, and the other two processor boards
replace it. It has all the properties in the section above, and nothing more.

### `8085`: an 8085 processor

**The 8085 came after the 8080, and it runs every 8080 program** without a change. Of the three
processors, it is the nearest to the 88-CPU. Everything in *The processor boards* above applies.
This section gives only what the 8085 adds.

The 8085 adds `RIM` and `SIM`, which read and set the interrupt mask and the SID and SOD serial
pins. It also adds its own interrupts, `TRAP`, `RST 5.5`, `RST 6.5` and `RST 7.5`, as well as
the 8080's `INTR` line. The documented instructions are correct. This includes the one
instruction that is different from the 8080: `ANA` and `ANI` always set the auxiliary carry. The
undocumented instructions run too, and set the extra V and K flag bits. `DISASM` marks each
undocumented byte, as `DDT` does. The built-in `8085` machine has an `8085`, 64K of RAM and a
2SIO console.

No board drives the 8085's own *pins*: the `SID` and `SOD` serial lines, and the `TRAP`,
`RST 5.5`, `RST 6.5` and `RST 7.5` interrupt inputs. The S-100 bus does not carry them. Ordinary
interrupts on the `INTR` line work as they do on the 8080.

### `z80`: a Z80 processor

**The other processor that you can put in the machine.** It uses the same backplane, with a
different instruction set. Everything in *The processor boards* above applies. The built-in
`z80` machine has a `z80`, 64K of RAM and a 2SIO console, so that you can try the processor.

## Serial ports and consoles

The boards that have a serial port, for a console, a modem or both.

### `2sio`: MITS 88-2SIO

Two **6850 ACIAs**, units `a` and `b`, with four ports at BASE+0 to BASE+3. The base is `10` hex
by default, which is where every listing of the period expects it.

This is **the usual console board**, and `default` has one. CP/M and Microsoft BASIC use this
board.

#### The two halves are independent

**The two units are two separate chips on the same board.** Each unit has its own baud rate, its
own connection and its own interrupt setting. For example, unit `a` can be a console at 9600
baud, and unit `b` a modem at 1200 baud. One can use interrupts while the other is polled.

The serial chapter tells you what you can connect a unit to: your terminal, a TCP socket, or a
real serial port on your computer, with the modem control lines.

### `sio`: MITS 88-SIO

One **COM2502 UART**, unit `tty`, with two ports. This was **the first MITS serial board**. It
came before the 2SIO, and the earliest Altair software uses it. The `basic4k` machine uses it.

The port must be **even**. Control is at BASE, and data is at BASE+1.

#### On the default Rev 1 board, the status bits are inverted

**A clear bit means "ready".** This is correct for the board, and it is not a bug in the
simulator. MITS changed the board at the factory, and on the changed board, "ready" is bit 7 for
output and bit 0 for input, both inverted. Every program for the 88-SIO expects this. The I/O
routine of `basic4k` tests for zero, and that is correct.

The `rev` property selects the board. `rev = 1` is the changed board, and it is the default.
`rev = 0` is the board as it first shipped, which also shows the two signals, not inverted, on
bits 5 and 1.

`in_int` and `out_int` set where the receive and transmit interrupts go, as the pads on the real
board did.

### `sbc`: SD Systems SBC-100/200

SD Systems made S-100 boards for people who wanted a whole computer on as few boards as
possible. The **SBC-100** and **SBC-200** are **Z80 single-board computers**, with the
processor, memory, a serial console and more on one board. The `sbc` board models the parts that
the software uses. They are all in one block of eight ports, `78`–`7F`:

- an **Intel 8251 USART** console, unit `tty`, with data at `7C` and status and command at `7D`
- a **Z80-CTC** at `78`–`7B`, which gives a keyboard interrupt when a byte arrives
- a **parallel port** at `7E`–`7F`. A write to `7F` with bit 1 set switches the onboard PROM off
- a socket for an **onboard boot PROM**

The 8251 is not the 6850 that the MITS boards use. Software for a 2SIO cannot use it. The SBC's
own **SD monitor** can. The processor is a separate `z80` board, and its `clock_hz` sets the
speed. `variant` selects the board: `sbc200` or `sbc100`.

#### It measures the speed of your terminal

The board has **auto-baud**. Run `altairsim sbc200`, and the SD monitor waits for you to **press
Return**. It times the bits of that one character, and sets its own baud rate to match. A
terminal at any common speed works. Nothing happens until you press Return. The program is not
stopped. It is waiting for that key.

The `sbc200` machine boots the **SD monitor**. With the **DDBIOS** disk BIOS in a PROM socket
and a `versafloppy` controller, the monitor's `C` command boots **SDOS**. See the VersaFloppy
section below.

### `gsio`: the generic serial board, set up with straps

Most serial boards in this chapter model **one specific chip**, such as the 2SIO's 6850, the
SBC's 8251 or the 88-SIO's COM2502. Software must be written for that chip. **`gsio`** is
different. It models no specific chip. You **describe** the port with straps:

- which port is status and control, and which port is data
- which status bit means that a byte is waiting (`dav`)
- which status bit means that the transmitter is ready (`tbmt`)
- whether the shared **`inverter_gate`** inverts both bits

Use it for any polled interface that reads a status bit and then a data byte.

The board has **two independent serial channels**, units **`a`** and **`b`**. Each channel has
its own straps, `baud` and `connect`, in its own `[board.unit.a]` or `[board.unit.b]` table. By
default, `a` uses ports `0` and `1`, and `b` uses ports `2` and `3`.

You seldom set the straps one at a time. A **profile** sets them to match a known board:

- **`sior1`**: MITS SIO Rev 1. This is the default, and the SSM 8080 monitor expects it
- `sior0`: MITS SIO Rev 0
- `tuart`: Cromemco TU-ART
- `imsai-sio2`: IMSAI SIO-2
- `compupro-if2`: CompuPro Interfacer II
- `compupro-ss1`: CompuPro System Support 1

Select a profile for each channel, and then change any strap, as you would move a jumper. The
board is polled, with no interrupts. It **only sends and receives bytes**. It does not model
word length, parity or stop bits. A board that needs those is a separate board with a full
model.

### `io4`: SSM IO-4 (2P + 2S)

`gsio` describes a serial port with straps. **`io4`** is a specific board, the **SSM IO-4**. It
has **two full-duplex serial channels *and* four latched parallel ports**. Each serial channel
is a real UART with **programmable word length (5 to 8 bits), parity, stop bits and baud**. The
status byte can be strapped in many ways, as on the real board.

The **two serial channels** are units **`a`** (Serial A) and **`b`** (Serial B). Each has its
own `[board.unit.a]` or `[board.unit.b]` table, its own `baud` and its own `connect`. The serial
half uses a block of four ports, which must start at a multiple of four (default `0`–`3`). `a`
uses ports `0` and `1`, and `b` uses ports `2` and `3`.

#### Set the straps, or select a profile

The IO-4 had many jumpers, and each jumper is a property here:

- which data bit means that a byte is waiting (`stat_dav`)
- which data bit means that the transmitter is ready (`stat_tbmt`)
- four more status signals: `stat_teoc`, `stat_ror`, `stat_rpe` and `stat_rfe`
- whether the whole status byte is inverted (`invert_status`)
- whether the status port and the data port change places (`port_reversal`)

You seldom set these one at a time. A **`profile`** sets them to match a known host:
**`altair-rev1`** (the default, the MITS SIO Rev 1 console that the SSM 8080 monitor expects),
`altair-rev0`, `i8251`, `proctech`, `imsai`, and `custom` (all straps free for you to set).
Select a profile, and then change any strap, as you would move a jumper.

You can strap the three UART **error flags** (parity, framing and overrun) to the status byte,
but they always read as inactive. The serial line carries exact bytes and has no line noise. The
current-loop and RS-232 options of the real board are electrical, not programmable, so they are
not modeled.

#### The parallel half

The other half is **four 8212 latched ports**, two for input and two for output, as units
**`pa`** and **`pb`** (Parallel A and B). They use their own block of two ports (default `4` and
`5`), which must start at an even port. An input port latches a byte on its strobe and sets a
service-request flip-flop. A read gets the byte and clears the request. `CONNECT io40:pa …`
connects a port to a source or a sink of bytes, like a serial line. The `dav_bit`, `dav_source`
and `dav_active_low` straps set up a console with its status byte on one port and its data byte
on the other.

**If the two blocks of ports overlap, neither half answers** on the shared ports. The real board
was designed this way. It is not two boards on the same address.

#### Interrupts, if you strap them

The **W4 header** of the board sets the interrupts, and `io4` follows it. Each serial channel's
receive (`rx_int`) and transmit (`tx_int`), and each parallel input (`int`), can go to a
vectored interrupt line, to the plain interrupt pin, or to `none`. **There is no software
enable.** The strap is the enable, as on the real board. With no straps, the default, the board
is polled. A strapped transmit interrupt stays on while the transmitter is idle. A parallel
input sets its interrupt on the strobe, also when nothing reads the port. Connect these to a
`virtc`, and a received character or a parallel strobe goes to the RST that you strapped.

`SHOW io40` shows every unit, its ports and the live state of its lines.

### `propio`: S100Computers Console I/O

A **serial console** from the modern reproduction boards: the S100Computers Console I/O board.
It uses a Parallax Propeller chip, not a 6850 or an 8251, with status at `00` and data at `01`.
It is a polled console, unit `serial`. You `CONNECT` it to a terminal, a file, a socket or a
serial port, like other serial boards. The Dual SD machine uses it as its console.

`propio` is a **preset** of the serial model that `gsio` uses (above). The preset sets the ports
of this board, and which status bits mean "receive ready" and "transmit ready". The real board
has jumpers, so you can still change each strap. A Console I/O board with different jumpers
needs only a property or two, not a new board type.

### `pmmi`: PMMI MM-103 modem

A **Bell 103 telephone modem on one S-100 board**. It was the first S-100 modem that was
approved for a direct connection to the telephone line. In a real machine, it dialed, answered
and carried a serial link at up to 600 baud. It has unit `line`, and four ports from a base that
must be a multiple of four. The default base is `C0`. The DIP switch on the real board set it,
and the North Star software of PMMI used `E0`.

**A read and a write at the same port are different registers:**

| Port | Write | Read |
|---|---|---|
| BASE+0 | character format and modem control | UART status |
| BASE+1 | transmit | receive |
| BASE+2 | baud-rate divisor | modem status |
| BASE+3 | the modem chip's control word | nothing that the board drives |

The three control registers are **write-only**. The program must keep its own copy of what it
wrote, as it did on the real board.

#### The telephone line

The board does not model the tones of a Bell 103. The line carries exact bytes. You choose what
is at the other end of the line:

- **`dial = "host:port"`** is the number that the modem calls. When the guest takes the line off
  the hook and raises DTR, the board opens a TCP connection to `host:port`.
- **`answer = port`** makes the board answer calls. A TCP connection to that port rings the
  guest, and the guest answers it as it would answer a telephone call. When the guest drops DTR,
  the call ends, and the board waits for the next caller.
- On a `dial` or `answer` line, **`telnet`** (on by default) uses the Telnet protocol. A person
  with a `telnet` client then gets no double echo, and sends one key at a time. Set
  `telnet = off` for a raw connection, for example to another simulator.
- **`CONNECT`** connects the line to something else, as for other serial boards. With a real
  serial port, the guest's DTR goes to the port, and the port's CTS, DCD and RI come back in the
  modem status. `rtsdtr = on` makes RTS follow DTR, for a cable that needs it. With a file or a
  socket, the modem status always reads "connected and ready".

The board does not decode the dial pulses of the guest into a telephone number. The `dial`
property gives the number. The board also raises no interrupts.

`SHOW pmmi0` shows the live character format, baud rate, UART flags and modem lines, with the
base address. To try the board, add it to `default`. `BOARDS ADD pmmi pmmi0` adds it at `C0`.

## Cassette

Boards that read and write cassette tape.

### `acr`: MITS 88-ACR

The **cassette interface**. It is an 88-SIO channel B with an FSK modem, so a byte on the bus
becomes a tone on a tape, and a tone becomes a byte again. It has unit `tape` and default port
`06`. It runs at 300 baud, because that is what an audio cassette could carry.

With this board, an Altair needs no disk and no PROM. You enter a bootstrap by hand and load the
program from a cassette. **The tapes chapter describes this board**: how to load Altair 4K BASIC
3.1, and the `WIND`, `REWIND` and `EXTRACT` commands.

### `uio`: MITS 88-UIO

**A serial port and a cassette interface on one board.** The Universal I/O board is almost a
2SIO channel and an 88-ACR on the same board, with some parts shared. It has the same addresses
as the two boards that it replaces: a **6850 serial port** at `10` (unit `serial`) and a
**cassette section** at `06` (unit `tape`). Software that expects a 2SIO console and an ACR tape
finds both.

It adds **motor control** and a **modulation switch**. `SW-1` selects one of the two encodings
of the period: the MITS 300-baud format or the Kansas City standard. The tape section has the
same `WIND`, `REWIND` and `EXTRACT` commands and the same tape counter as the 88-ACR.

## Printers

Line-printer controllers. Each one sends its output to a file or to a print queue.

### `c700`: MITS 88-C700

The **line-printer controller**, an output-only board for an Altair C700 printer. It has unit
`prn` and default port `02`, which is the MITS default: control and status at `02`, data at
`03`.

The package has no printer, so **`CONNECT` the `prn` line to where you want the output**:

- a file: `CONNECT lpt0:prn out:printout.txt`
- the `console`, to see it print while it runs
- a `socket:`
- a real `serial:` printer
- a **real print queue on your computer**: `CONNECT lpt0:prn printer:linewriter`. This works
  when your build found a print system. The serial chapter gives the options.

The output is the exact bytes that the program sent, with the control codes. It is not a
formatted page.

You can use the board **polled or with interrupts**:

- **Polled:** write a character to the data port (`03`). Before the next character, read the
  status port (`02`) until bit 0, ACKNOWLEDGE, is set.
- **Interrupts:** set control bit 1 to turn on its **single-level interrupt**. The printer then
  interrupts the processor each time it takes a byte, and the handler sends the next byte.

The `interrupt` strap selects the line: pin 73 (`int`, the default), a vectored interrupt level
(`vi0` to `vi7`), or `none`. `interrupt_after` is switch SW2 #4 of the board. It sets whether
the interrupt comes after every character, or only after a CR or LF.

The **`lineprinter`** machine is `default` with one of these boards added. Its output starts on
`null`. To capture it, type `CONNECT lpt0:prn out:printout.txt`. To see it, connect it to
`console`, which takes the console from the terminal.

### `lpc`: MITS 88-LPC

The **other** line-printer controller, for the 88-LP printer. The `c700` drives the C700
printer. The `lpc` has the same two ports: control at an even base and data above it, with the
MITS default `02`. It drives the printer as the real one worked, and the output shows the
difference.

The C700 **passes every byte through**. The LPC **prints a line at a time**. The guest loads a
**6-bit character code** at a time into an **80-character line buffer**. Nothing prints until
the guest sends a **PRINT** command, or the buffer is full. **LINE FEED** and **CLEAR** are also
commands. For this reason, the output is the printed *page*: the codes changed to characters,
one text line for each printed line. On this board, the line breaks are commands, not data.

`CONNECT` its `prn` line to an `out:` file, the `console`, a `socket:` or a real `printer:`
queue. The **`lineprinter-lpc`** machine has one at `02`, with its output on `null` until you
`CONNECT` it. The board is polled. The interrupt of the real board is not modeled.

## Parallel and analog I/O

Parallel ports, and one board that also reads analog inputs.

### `pio`: MITS 88-PIO

An **8-bit parallel port**, in and out. It is the simplest way to move a byte that is not a
serial character. It has **two lines that you connect separately**: `out` for an output device
(a printer or a socket), and `in` for an input device (a keyboard or another socket). For
example, one board can write to an `out:` file and read a keyboard from the `console` at the
same time. The default ports are `04` and `05`. The board is **polled**. A byte moves when a
driver reads the status port for it.

### `4pio`: MITS 88-4PIO

The programmable version of the `pio`. It has up to **four Motorola 6820 PIAs** on one board,
and **the guest sets the direction of each port in software**. Each port is a section (`ja`,
`jb` and so on), and each section is a line that you can connect. It uses sixteen ports from a
default base of `20`. It is polled, like the `pio`.

The `pio` has a fixed eight bits each way. The `4pio` can be set up as the software needs. The
**`parallel`** machine has both boards, and the `pio` sends its output to a file.

### `d7a`: Cromemco D+7A

An **analog and parallel I/O board**. It has one parallel port and **seven analog channels**, in
a block of eight ports (default base `18`). Each analog channel is an **A/D converter when you
read it and a D/A converter when you write it**, in 8-bit two's complement: `00` is 0 V, `7F` is
about +2.5 V, and `80` is about −2.5 V. On a real bench, it read sensors and drove instruments.

Here, it is the input of a **game console**. It reads **one or two JS-1 joysticks**. The X and Y
controls go to analog channels. The four buttons of each stick are **active-low** bits in the
parallel byte: the low four bits for one stick, and the high four bits for the other. The sticks
come from a **USB gamepad** on your computer, or from the **keyboard** (the arrow keys and a few
other keys) when there is no gamepad.

`joystick1` and `joystick2` select the device on your computer for each stick. Both are `auto`
by default, which gives each stick a **different** gamepad: stick 1 gets gamepad 0, and stick 2
gets gamepad 1. Two controllers work with no setup, and each stick uses the keyboard when its
gamepad is not there. `SHOW <id>` shows what each stick uses now (a named controller, the
keyboard, or nothing). `SHOW JOYSTICKS` lists the controllers that your computer has.

The Dazzler example in `examples/` uses this board with color graphics. It sets the video window
to be a **display, not a keyboard** (`[display] keyboard = none`), so your keys move the stick
and do not go to a prompt. The sound output of the JS-1, which is a D/A that the processor
writes a waveform to, is not modeled.

## Floppy and disk controllers

The disk controllers, from 8-inch floppy disks to CompactFlash. Several of them boot an
operating system by themselves.

For most of these controllers, the package has a built-in machine but no disk image. The machine
starts with empty drives, and you supply the image. The disks chapter tells you how to mount
one.

### `dcdd`: MITS 88-DCDD

The **8″ hard-sector floppy controller**. It has up to sixteen drives, and three ports at `08`,
`09` and `0A`. **CP/M booted from this board**, and `default` has one.

It can also use the **8 MB medium**, a large format that the same controller can address. You
supply the image. The package has no 8 MB disk.

Its status bits are **inverted**, as on the 88-SIO. A clear bit means "ready". The `interrupt`
strap sets where the board's interrupt goes.

**The machine must run at 2 MHz, or at full speed.** The software for 8″ disks (Disk BASIC,
Altair DOS, CP/M) reads and writes two bytes each time through its loop, and it times the second
byte for a 2 MHz processor. At full speed, the board keeps the same 2 MHz time as the processor,
so the disk works. With a faster `clock_hz`, the software reads each byte before it arrives, as on
a real Altair with a fast processor, and the disk does not work. The boot PROM then prints `C`
again and again.

The disks chapter describes this board: the formats, how to mount a disk, write protection, and
the track buffer, which is why you go back to the `A>` prompt before you stop the machine.

### `mds`: MITS 88-MDS

The **5¼″ minidisk** controller, with four drives. **It has the same registers as the DCDD**, so
a program for one board drives the other. The drives are different:

| | `dcdd` | `mds` |
|---|---|---|
| Spindle | 360 RPM | **300 RPM** |
| Byte time | 32 µs | **64 µs** |
| Motor | always turning | **stops after 6.4 seconds** |

The minidisk motor does not turn all the time. It starts, and if nothing uses the drive, it
stops again. The software must handle this. To see it do that, set `motor` to `real`. The
default is `free`: the motor is always at speed, with no wait. The DCDD has no such setting,
because its motor never stopped.

**You supply the minidisk image.** The `minidisk` machine boots its PROM, but the package has no
5¼″ image, so the drives start empty.

#### It cannot be in the same machine as a `dcdd`

**The two boards use the same three ports.** This is the MITS address map. A real Altair with
both boards would have them both on the data bus at the same time. If you add both here, the bus
view names the conflict. Use one or the other.

### `fdcplus`: FarmTek FDC+ serial drive and 1.5 MB floppy

The FDC+ is a modern board that replaces the 88-DCDD and the 88-MDS. Its drive type switches
select what it does. This board does three drive types: **5**, the 1.5 MB floppy (see
[The 1.5 MB floppy](#the-15-mb-floppy-drive-type-5) below), and **6** and **7**, the serial drive.

In its **serial drive**
mode it has no disk drive. A **drive server** on another computer keeps the disk images, and the
board gets a whole track at a time over a serial line. To the software, the board is an 88-DCDD
or an 88-MDS, so CP/M and the boot PROM work as they are.

Use it to use the images on a drive server, to share one set of images between the simulator
and a real FDC+ Altair, or to test a drive server.

| Property | What it sets |
|---|---|
| `drivetype` | `7`: the server's disks are 8″ disks, including the 8 MB disk. `6`: they are minidisks. `5`: the 1.5 MB floppy, below. The board reads this at power-on, as the real switches are read. |
| `connect` | The drive server. `CONNECT fdc0:line` sets it too. |
| `baud` | The speed of the serial line: 9600, 19200, 38400, 57600, 76800, 230400, 403200 or 460800. Set the same speed on the server. The default is 403200, the fastest reliable speed. |
| `port` | `08`, or `80`. The board uses four ports. |

**The server mounts the images, not the simulator.** `MOUNT` on `fdc0:line` gives an error. A
drive that has no image on the server is not ready, as an empty drive is.

**Do not run the machine at full speed.** A guest counts instructions to measure time, and CP/M
stops looking for a sector after 1.4 seconds of its time at 2 MHz. At full speed, that is a few
milliseconds, and a track takes longer than that to arrive. A faster crystal is possible if the
line is fast too: 10 MHz with 230400 baud works, and 38400 baud needs 2 MHz.

To use a server on `/dev/cu.usbserial-AL009KFH` at 38400 baud in the `default` machine:

```
altairsim> BOARDS REMOVE dsk0
altairsim> BOARDS ADD fdcplus fdc0
altairsim> SET cpu0 clock_hz=2000000
altairsim> SET fdc0 baud=38400
altairsim> CONNECT fdc0:line serial:/dev/cu.usbserial-AL009KFH
altairsim> RUN FF00
```

The `default` machine's `dcdd` uses the same ports, so remove it first. On Windows, the device is
a name such as `serial:COM3`. `CONFIG SAVE` writes the machine to a file, so that you do the
setup only once.

The package has this machine as a file: `examples/cpm/cpm22-fdcplus.toml`. Set your serial port
and speed in it, and run it.

A slower line makes a slower disk. At 38400 baud, a track takes about one second. When the machine
is not using the disk, the board writes changed tracks back to the server after about one second.
It also writes them back before a `DISCONNECT`, a `POWER`, or when you quit.

#### The 1.5 MB floppy (drive type 5)

With `drivetype = 5`, the FDC+ runs a high-density floppy drive in a format of its own: each
track is **one sector of 10,240 bytes**, on both sides of the disk. A disk holds **1,525,760
bytes**, five times an Altair floppy. The board has four drives, `drive0` to `drive3`, and you
put a disk image in one with `MOUNT`. The serial line is not used.

**A stock Altair boot PROM boots this disk.** The PROM cannot read a 1.5 MB disk. But when the
PROM asks for a sector, the FDC+ gives it a sector of its own. That sector holds a short loader,
and the loader reads the real disk. So `RUN FF00` works with the DBL PROM of the `default`
machine.

**The machine must run at 2 MHz, or at full speed.** The CP/M for this disk moves a track with no
handshake: it reads a byte every 17 µs, slightly slower than the disk, and writes one every 14
µs, slightly faster. Those are 2 MHz loops. At full speed, the board keeps the same 2 MHz time as
the processor, so the disk works. With a faster `clock_hz`, CP/M reads bytes before the disk has
sent them, as on a real Altair with a fast processor, and the disk does not work.

The package has CP/M for this drive: `examples/cpm/cpm22-fdcplus-hdf.toml` and the disk
`CPM22-48K-HDF.dsk`. To do the same by hand in the `default` machine:

```
altairsim> BOARDS REMOVE dsk0
altairsim> BOARDS ADD fdcplus fdc0
altairsim> SET fdc0 drivetype=5
altairsim> POWER
altairsim> MOUNT fdc0:drive0 CPM22-48K-HDF.dsk
altairsim> RUN FF00
```

The board reads `drivetype` at power-on, so `POWER` comes after it. A disk that is mounted
read-only is write-protected. The CP/M for this disk refuses to write to it, and CP/M then
shows `Bdos Err On A: Bad Sector`.

### `hdsk`: MITS 88-HDSK Datakeeper

A **hard-disk controller**, the "Datakeeper". CP/M boots from it when it does not boot from a
floppy disk. It is a separate controller with a **command and handshake protocol** and four
**256-byte page buffers**. Unlike the floppy disk boards, **it moves whole sectors for you**. It
does not shift bits in real time. It has eight ports, default `A0`–`A7`.

The **HDBL** boot PROM at `FC00` reads the descriptor page of the disk and starts the system.
The package has an example in `examples/` with the image. Run it, and you get an `A>` prompt on
a CP/M 2.2 disk of several megabytes. The disk is **read/write**, and CP/M saves to it. The
README of the example tells you how to start it.

### `versafloppy`: SD Systems VersaFloppy I & II

The **soft-sector floppy controller** of SD Systems, built on a **Western Digital FD177x**. It
is the board that boots **SDOS**, a CP/M-compatible system, on an SBC-200.

**One board type covers both versions.** `variant` selects the version:

- **`vfii`** (the default): the double-density VersaFloppy II, with an **FD1791**
- **`vfi`**: the single-density VersaFloppy I, with an **FD1771**

The two versions differ only in the controller chip and a few control bits. They have the same
eight ports (default `60`) and the same drivers. Neither version has a boot PROM. The boot BIOS
is in a separate PROM, which is the onboard socket on an SBC-100/200.

Add a `vfii`, mount an 8″ double-density SDOS disk, and use the **DDBIOS** of the SBC-200. The
disk commands of the monitor then work: **`C`** boots SDOS, and **`R`** and **`W`** read and
write sectors.

#### It cannot format a blank disk

The `Z` command **formats** a disk. On this board, it cannot make a disk from nothing. A disk
image holds only the data. It has none of the gaps and address marks that a real format writes
between the sectors, so a low-level format has nothing to write. The controller reports a WRITE
FAULT. Mount a disk that already has a format, and reads and writes work. For a new SDOS disk,
copy a disk that is already formatted. The iCOM board has the same limit. The Cromemco boards
(below) can format a blank disk.

### `tarbell`: Tarbell #1011 single-density floppy

The **Tarbell Electronics #1011** (July 1977) was the S-100 floppy interface that booted CP/M on
many Altair and IMSAI machines. It is a **Western Digital FD1771** soft-sector controller with
eight ports, default `F8`. Unlike the VersaFloppy, it has **its own 32-byte boot PROM**, so you
do not type a boot command.

Turn the machine on with a disk in drive 0, and the machine boots itself. RESET puts the PROM at
address `0000`, over the bottom of memory. The PROM reads the first sector from the disk. When
the loaded code runs, the PROM switches off, and CP/M starts. The `tarbell` machine has this
board. Mount a CP/M disk in drive 0, and you get the `A>` prompt with no monitor first. With no
disk in the drive, the PROM has nothing to load, and it stops. Put a disk in and reset.

The `bootstrap` switch turns the PROM off. The board is then a plain disk controller, for a
machine that boots in a different way. It has four drives, which the software selects. The disks
are 8″ single density, with 128-byte sectors. The board recognizes a disk by the size of its
image.

### `tarbelldd`: Tarbell #2022 double-density floppy

The **#2022** (1979–80) is the #1011 with a **Western Digital FD1791**, which reads **double
density** as well as single density. It boots in the same way, with the same boot PROM and the
same `F8` ports. It boots from a **mixed-density** disk: the first track is single density, so
that the boot PROM can read it, and the other tracks are double density. The `tarbelldd` machine
boots CP/M 2.2 from a disk of this kind.

This board does everything that the `tarbell` does. It adds double density, and a **DMA
register** at port `FD`. With DMA, the board moves data to memory itself, without the processor.
Use this board for a double-density Tarbell image, and the `tarbell` for a single-density image.
The board recognizes the two kinds by the size of the image.

### `icom`: iCOM FD3712 / FD3812 8″ floppy

An **8″ floppy controller** of a different kind. Like the Datakeeper, it uses **commands and a
handshake**. It does not shift bits. It holds a whole sector, and the processor moves the bytes
through two ports (default `C0`–`C1`). The disk driver of the operating system is in a **boot
PROM** in high memory. One board covers both iCOM versions: the single-density **FD3712**, and
the double-density **FD3812**. The FD3812 disk is mixed density, with a single-density track 0
and double-density tracks after it. The board recognizes the two by the size of the image.

The `rom` property selects the PROM, and the PROM selects the system that boots:

- **`builtin:icom-fd3712-cpm`** (the default) boots **CP/M 2.2**, single density, from the PROM
  at `F000`.
- **`builtin:icom-fd3812-cpm`** boots **CP/M 2.23**, double density, also at `F000`.
- **`builtin:icom-fd3712-fdos`** boots iCOM's own **FDOS** from the PROM at `C000`. FDOS is not
  CP/M. It has a `!` prompt, and its own `LIST`, `EDIT`, `ASMB` and other commands.

The `icom` machine boots CP/M 2.2 as soon as you mount a disk. Reads and writes of a formatted
disk work. Like the other controllers here, it cannot format a **blank** disk from nothing.

### `16fdc` and `64fdc`: Cromemco 16FDC and 64FDC

Cromemco's double-density floppy controllers. Each board has three things on it:

- a **Western Digital FD1793** floppy controller, for single and double density, with up to four
  drives. Its registers are at `30`–`34`.
- a **TMS 5501** console UART, unit `tty`, at `00`–`09`
- an **RDOS** boot PROM at `C000`. The 16FDC has 4K of RDOS 2.52. The 64FDC, from 1983, has 8K
  of RDOS 3.12, at `C000`–`DFFF`.

`OUT 40H` switches the PROM off, and RESET switches it on again. With the `bootstrap` strap on
(the default), RDOS boots the disk. With it off, RDOS gives its monitor prompt.

Both boards boot Cromemco **CDOS**. The package has no CDOS disk and no built-in machine for
these boards, so you add the board and supply the disk. **These boards can format a blank
disk.** Mount a blank image, and the guest's own format program writes the tracks.

The boards are polled. They deliver no interrupts. The front-panel switches of the 64FDC (baud
rate, boot drive and self-test) are not modeled.

### `dualsd`: S100Computers Dual SD

A **modern** disk controller among the period boards. The S100Computers Dual SD board puts **two
microSD cards** on the S-100 bus as drives with 512-byte sectors, and a Z80 machine runs **CP/M
3** from them. Like the iCOM and the Datakeeper, it uses **commands and a handshake**. It has
two ports (default `80`–`81`), and a microcontroller on the board does the card I/O. Each SD
card is one CP/M drive. The **socket** of the card gives the drive letter: socket 1 is A:, and
socket 2 is B:.

**It has no boot PROM.** The monitor on the processor board boots it. For this reason, the
`dualsd` machine has three boards: a `z80` for the processor, the `v2z80rom` monitor EEPROM,
whose `I` command reads CP/M 3 from the card, and this controller. Start the machine, type `I`
at the `->` monitor prompt, and CP/M 3 starts at `A>`.

**Mount a card in both sockets.** The CP/M 3 boot ROM checks that each socket has a card before
it starts. With a card in socket 1 only, the loader boots and then stops. Put the bootable
system card in socket 1, and a blank card in socket 2.

A card is a raw **`.img` file, with a `.geo` file beside it** that gives the true size of the
card. The `.img` can be **shorter** than the card, with only the live file system in it. Every
sector after its end reads as an erased card does, as all `FF`. For this reason, a card that is
hundreds of megabytes on real flash can be a few megabytes here. `MOUNT … CREATE` makes a
**blank** data card, an empty `.img` and its `.geo`, for the guest to format. A *bootable* card
must come from a real image, because nothing can make its system tracks. You supply the images.

### `dualide`: S100Computers IDE-AB (CompactFlash)

The other half of the same S100Computers board. The Dual SD part drives microSD cards. The
**IDE-AB** part drives **two CompactFlash cards**, through an 8255 parallel port that connects
to the IDE bus of a CF card (default ports `30`–`34`). Its two cards are CP/M drives A: and B:,
and it boots the same **CP/M 3**. It stores data on a card in the same layout as the SD part, so
**a card image works on both boards**.

**It has no boot PROM** either. The `dualide` machine has the `z80`, the `v2z80rom` monitor
EEPROM and this controller. At the `->` monitor prompt, the **`P` command** (not the `I` of the
Dual SD) reads CP/M 3 from CompactFlash drive 0, and CP/M starts at `A>`.

The cards are the same as on the Dual SD board: a raw **`.img` with a `.geo` file beside it**.
The image can stop after the live file system, with `FF` after its end. `MOUNT … CREATE` makes a
blank data card, and a bootable card must come from a real image. The **`dualidesd`** machine
has the **whole board** at once: CompactFlash as A: and B:, and microSD as C: and D:, with one
CP/M 3 system on all four drives.

## Video and display

Boards that show a picture in a window.

Each video board opens its own window. `[display]` in the configuring chapter sets which window
gets the keyboard, and how the picture looks.

**Closing a video window stops the machine. It does not quit the program.** The close box does
what `STOP` does. The guest stops at an instruction boundary, and you get the monitor prompt,
with the machine where it was. `RUN` continues in the same window. `QUIT` leaves the program.

### The window size: the `width` property

Every video board (the VDM-1, the Dazzler and the VDB-8024) has a **`width`** property. It sets
the width of the window in pixels. `auto`, the default, makes the window about **half as wide as
the screen**. A number such as `width = 1024` asks for that many pixels. **The height follows
from the shape of the board's picture.** With the default look, the program draws each pixel of
the board as a whole number of screen pixels, so that the picture stays sharp. A thin dark
border fills the rest. With the period look (`[display] crt = true`), the picture fills the
width that you asked for, because a soft picture has no square pixels to keep sharp. A width
that is too big for the screen is made smaller.

The built-in `terminal` window (see the serial chapter) sets its size in the same way, with a
`width=` option in its connect string, not a board property.

`width` is a property of the board, not of `[display]`. On real hardware, each board had its own
video output and could drive its own monitor. The same is true here. Add a VDM-1 and a Dazzler
to one machine, and you get two pictures in two windows. The keyboard is still shared. Whichever
window you type in, the keys go to the machine.

### A stopped machine's window does not redraw

The window stays live when the machine is stopped. You can move it, and you can close it. If you
close it at the monitor prompt, `RUN` opens a new one the next time a program draws. A stopped
machine does **not** redraw the picture, because the running machine draws it. This has two
results:

- **A change such as `SET vdm0 video=reverse` does not show** until you type `RUN` again. The
  program keeps the setting, but nothing redraws the screen until the machine runs.
- **The cursor does not blink** while the machine is stopped. To see it blink, use `sol20`,
  where SOLOS runs in a loop and does not halt.

The `vdm1` demo halts after it draws its banner, so its window belongs to a stopped machine.

### `vdm1`: Processor Technology VDM-1

**Memory-mapped video.** A 1K screen of **16 rows by 64 columns** is in the machine's own
address space, at `CC00` by default. A program puts a character on the screen when it *stores a
byte*. It needs no port and no driver. For this reason, the board is fast, and it needs no
`CONNECT`, because the screen is memory.

One port (default `CC`) does the rest. A write to it sets which row is at the top of the screen.
This is how the VDM-1 scrolls: the text does not move, but the window on it does. A read of the
port gives two timing bits. The software uses them to avoid writing while the beam draws.

Bit 7 of each byte is the **cursor and blink** flag, not part of the character. For this reason,
the board draws 128 characters from a real character ROM, not 256.

Two machines use it:

- **`vdm1`**: an Altair with a VDM-1, and a demo that draws on it
- **`cuter`**: the period CUTER monitor, with its own VDM-1 driver

### `dazzler`: Cromemco Dazzler

The **first color graphics board for the S-100 bus**. The VDM-1 shows text, and the Dazzler
shows a **picture**. It does this in the same way, from a **frame buffer in the machine's own
RAM**. You point the board at a block of 512 bytes or 2 KB, at any 512-byte boundary, and it
shows that memory on the screen. A program draws when it *stores bytes*, with no port in the
inner loop.

Two ports (default `0E` and `0F`) set the rest. The first turns the board on or off, and sets
where the frame buffer is. The second sets the format: the resolution, the size and the color.
This gives four modes: **32×32 or 64×64** elements in color or grey, and **64×64 or 128×128**
on/off elements, in **16 colors** or 16 greys.

The Dazzler example in `examples/` runs **Li-Chen Wang's Kaleidoscope**, a pattern that turns
and is mirrored four ways. `STOP` gives you the monitor. The `dazzler` machine is the plain
board, for you to build on. A 64×64 picture is very small, so the board's `width` property
(above) makes the window about the size of a VDM-1 window.

### `vdb8024`: SD Systems VDB-8024

An **80-column by 24-line video terminal on one board**. The `sbc` board gives an SBC-100/200 a
serial port for a terminal. The VDB-8024 gives it *the terminal itself*: a complete display with
a keyboard, on the backplane.

**It is not memory-mapped.** None of its screen is in the machine's address space. To the
computer, it is **two I/O ports**, a status port and a data port at `00` and `01`, that work
like a terminal on a line. The program reads the status to see whether a key is waiting or the
display is ready. It writes a character or a control code to the data port, and it reads a typed
key from the same port. The screen, its memory and its own processor are all behind those two
ports, as on the real board. The real board had its ports fixed at `00`. The board here has a
`port` property so that you can move them, as the `sol` does below.

The board is polled by default. The `interrupt` strap (`vi0` to `vi7`) sends a keyboard
interrupt to a vectored interrupt line, for the SBC-200's CTC. The SD video CBIOS needs this.

It runs **`sdmonv21`**, the video version of the SD monitor. It is the same monitor as in the
serial `sbc200` machine, with the same commands and the same `.` prompt. You **do not press
Return first**, because the VDB has no serial speed to measure. The prompt is on the screen as
soon as the machine starts:

```
$ altairsim sbc200v
```

This gives the monitor's `.` prompt **in the video window**.

Like a Sol-20, **the window is the console.** The machine sets `focus=on`, so the window keeps
the keyboard while the guest runs. Keys typed in the window and at the terminal go to the
monitor together. The characters come from the board's own character font, with true descenders
on `g`, `j`, `p`, `q` and `y`, as the font PROM of the hardware drew them.

The board obeys the same control codes as its firmware: carriage return, line feed, backspace,
tab, cursor up and right, clear screen and home, and the `ESC` sequences that move the cursor
and erase to the end of a line or of the screen. A full line wraps, and a line feed on the
bottom line scrolls the page up.

### `sol`: Processor Technology Sol-PC

The **onboard I/O of the Sol-20, as one board**, because it was one board on a real Sol-20. The
Sol was not an Altair with boards in it. It was one machine, with the serial port, keyboard,
parallel port and cassette interface all on the processor board, at `F8`–`FE`. On the real
hardware, those addresses were fixed. Here, the board has a `base` property so that you can move
it. This is the one change from the hardware, and the reference records it.

You reach the parts of the board as units. `serial`, `printer` and `keyboard` are lines that you
`CONNECT`. `tape1` and `tape2` are cassette decks that you `MOUNT`. The keyboard is connected to
the console by default. It takes what you type, from the video window when there is one, or from
your terminal.

#### The special keys of the keyboard

The Sol keyboard has eight keys that send codes with no ASCII equal. You use them to control
SOLOS and the screen:

| Key | Sends | What it does | Press | Or type |
|---|---|---|---|---|
| `←` | `81` | Cursor left one | ← | Ctrl-A |
| `→` | `93` | Cursor right one | → | Ctrl-S |
| `↑` | `97` | Cursor up one | ↑ | Ctrl-W |
| `↓` | `9A` | Cursor down one | ↓ | Ctrl-Z |
| `HOME CURSOR` | `8E` | Cursor to the top left, screen untouched | Home | Ctrl-N |
| `MODE SELECT` | `80` | Return to the command mode, restarting the command line | F1 | Ctrl-@ |
| `CLEAR` | `8B` | Erase the screen, cursor home | F2 | Ctrl-K |
| `LOAD` | `8C` | Nothing. Neither SOLOS nor CONSOL uses it | F3 | none |

**The `Press` column works only in the video window.** When the window has focus, your arrow
keys, Home, and F1 to F3 send these codes. They cannot work from a terminal. There, an arrow key
or a function key sends an escape sequence, not one byte, and the guest needs `ESC` as a
character, so the program cannot safely read those sequences. From a terminal, use the last
column.

A PC keyboard has no `MODE SELECT`, `CLEAR` or `LOAD` key, so **F1** is `MODE SELECT`, **F2** is
`CLEAR`, and **F3** is `LOAD`. On a Mac, the function keys reach the window only if the system
sends F1, F2 and so on as plain function keys. If not, hold `fn`.

**The last column is how the hardware worked.** The code of each special key is `80` plus the
control code for the same action, because the display driver of SOLOS clears the top bit of each
character before it uses it. For example, `CLEAR` and Ctrl-K go to the same routine. The command
reader clears the top bit too. For this reason, a NUL byte is `MODE SELECT`. Type Ctrl-@
(Ctrl-Space on many keyboards), and SOLOS drops the line that you were typing and gives a new
prompt.

The console is 8-bit clean. If you can send the byte itself, for example with a paste, a script
or a terminal macro, the real code works too.

One register (`FA`) gives the state of *all* of these parts at once, with **mixed polarity**.
The keyboard and parallel bits are active-low, and the tape bits are active-high. This is what
the hardware did, and the period software inverts the bits that it needs.

#### The cassette decks

The Sol's cassette interface is on the main board, and it has **two decks**, `tape1` and
`tape2`. The 88-ACR has one. Everything in the tapes chapter applies to them, with a different
unit name. Always give the name of the deck. The program refuses a plain `WIND sol0`, because it
does not know which tape to move.

```
altairsim> MOUNT sol0:tape1 "mytape.tap"
altairsim> SET sol0:tape1 mode=record
altairsim> REW sol0:tape1
```

There are three differences from the ACR, and each one comes from the hardware:

- **The Sol controls the motors.** `OUT 0FAh` starts and stops each deck, and SOLOS does this
  for you. `SAVE` starts the deck, writes, and stops it. For this reason, a Sol tape plays only
  while the guest runs it, and a deck with its motor off gives nothing. When the motor stops,
  the board writes the recording out. The 88-ACR cannot see the deck, so it cannot do this. The
  Sol still cannot **wind** the tape by itself, because a motor line says only "turn", not which
  way. You use `WIND` and `REWIND` for that.
- **The guest sets the speed.** The ACR's 300 baud is fixed. The Sol's cassette runs at 300 or
  1200 baud, and bit D5 of `OUT 0FAh` selects the speed while the machine runs. The SOLOS
  command `SE TA` sets that bit.
- **The modulation is CUTS, not the ACR's FSK.** A Sol tape is `cuts1200` (1200 and 600 Hz) at
  1200 baud, one octave below the ACR's 2400 and 1850 Hz. It can also read Kansas City
  (`kcs300`, 2400 and 1200 Hz). For this reason, the 88-ACR refuses a Sol tape. The tapes
  chapter tells you more.

When a tape is mounted, the SOLOS commands work:

```
>SA MYPROG 0100 01FF        (save memory to the tape)
>GE MYPROG                  (find it again and load it)
>CA                         (catalog what is on the tape)
```

**To write audio that a real Sol can load**, you need the right tones, and also the right
*shape* and level. A real Sol CUTS modem is a flip-flop that divides a master clock into a
square wave. An RC network rounds the wave, and the tape records it at a moderate level. Three
properties copy this when the board writes a `.WAV` file. Their defaults come from measurements
of a real archived tape:

| Property | Default | What it does |
|---|---|---|
| `leader` | `3` | Seconds of steady tone before the data. The real tape has 3.05 s. The ACR's leader is longer, 15 s |
| `trailer` | `2` | Seconds of tone after the data. The real tape has 1.93 s |
| `rc` | `4000` | The corner of the low-pass filter that rounds the edges, in Hz. It rounds the edges of the square wave as the modem's RC network and the cassette do. CUTS only |

`level` (percent of full scale, default `36`) applies to both boards, and the tapes chapter
describes it. With these defaults, a CUTS tape from the simulator has its tones on the same
clock grid as a real Sol expects, at the level of a real tape. It is made to load on the
hardware, not only to read back here.

Add a `vdm1` to this board, and you have the **`sol20`** machine, which starts the real SOLOS
operating system.

## Interrupts and the clock

Vectored interrupts and real-time clocks.

### `virtc`: MITS 88-VI/RTC

Two things on one board.

**Vectored interrupts.** The board has eight lines, **VI0 to VI7**. You strap a device to a
line. When the device interrupts, **level *n* becomes `RST n`**. The processor jumps to `8×n`,
and the correct handler runs, with no polling. **VI0 has the highest priority**, and the board
enforces it: a lower level cannot interrupt a higher level while the higher level is being
handled.

With interrupts, the machine can do other work and does not have to wait in a loop. Every board
with an interrupt strap in its properties, such as the serial boards and the floppy controllers,
is strapped to one of these lines, to `int`, or to nothing.

**A real-time clock**, on the same board. It gives a periodic interrupt from the 60 Hz power
line, or from the system clock divided down. The `rtc_source` jumper selects which.

The board has one port, at `FE`, and it is **write-only**. You cannot read anything back.
Interrupt boards are easy to set up wrong, so read the reference for this board before you strap
anything to it.

The `ps2int` machine shows the board in use, with a MITS Programming System II tape **that you
supply**. The package has no such tape, so its cassette deck starts empty.

### `ss1`: CompuPro System Support 1

A **multifunction** board. On the real board, one block of 16 ports holds a serial channel, an
interval timer, two interrupt controllers, a battery-backed **real-time clock and calendar**,
and a socket for a math coprocessor. This board models the **clock**, the **serial channel**,
the **interval timer** and the **two interrupt controllers**. The math socket is empty.

The clock is an OKI MSM5832. It starts with **the date and time of your computer**, so a guest
program that reads it gets the real time. A guest can also **set** it. After that, the clock
keeps the setting, and the time is kept through a RESET, as the battery on the real chip did.
The block is at base **`50H`** by default, the CompuPro convention. The `base` property moves
it.

The serial channel is a **2651 UART** at `5C`–`5F`. It is a spare serial port. A guest sets its
rate, its frame and its RS-232 handshake through the mode and command registers. You connect it
with `CONNECT ss1:serial <endpoint>`. Its `baud`, `interrupt` and `connect` are unit settings,
which `SHOW ss1` shows.

The interval timer is an **Intel 8253** at `54`–`57`. It has three independent 16-bit counters,
which a guest can use as timers or as square-wave and rate generators. The counters count at 2
MHz, and their outputs go to the interrupt controllers. `SHOW ss1` prints a live `timer` line
with the mode, count and output of each counter.

The interrupt system is two **Intel 8259A** controllers at `50`–`53`, cascaded as master and
slave:

- The master watches the eight vectored interrupt lines of the bus, and drives the processor's
  interrupt pin.
- The slave collects the sources on the board, which are the three timer outputs and the UART's
  transmit-ready and receive-ready signals. It sends them to the master.

A guest sets them up in the usual 8259A way, with the ICW and OCW words, and unmasks the sources
that it wants. On an interrupt, the controller gives the processor a `CALL` to the vector of the
source with the highest priority. `SHOW ss1` prints a live `pic` line with the request,
in-service and mask registers of each controller. The master is the interrupt priority encoder
of the machine, so do not also add a `virtc`. The two boards would both answer the interrupt
acknowledge.

The `compupro` machine has one: a stock Altair with the System Support 1 added, and its console
still on the 2SIO. The clock is at `5A` (command) and `5B` (data). The board reference gives the
digit map, the read and set sequences and the register layouts.

### `rtc100`: SciTronics RTC-100

A **battery-backed calendar clock**, and nothing else. SciTronics sold it in 1980 for machines
that did not know the date. It has an OKI MSM5832 clock chip, the same chip as on the System
Support 1, behind a 6821 parallel interface, on four ports in a row.

It starts with **the date and time of your computer**, so a guest gets the real time. When a
guest sets it, the clock keeps that setting through a RESET and a power cycle, as the lithium
cell of the real board did. `SHOW <id>` prints a live `time` line with the time on the clock and
how far it is from the time of your computer.

The base address is the PORT switch of the board, and it **must be a multiple of 4**. The switch
decodes only the top six address bits, and the bottom two bits select one of the four ports. The
default is `F0`. A guest reads the clock one BCD digit at a time. It writes the number of the
digit to the first port, and reads the digit back **in the top half of the byte**. For this
reason, the period drivers for this board all mask and rotate the byte at the end.

It can also **interrupt once a second**, to drive a clock display in the background. Set
`interrupt` to `int`, and the board pulls the interrupt line of the machine. `restart` selects
the `RST` that it gives the processor (0 to 7, the INT switch of the board). The manual of the
board says that 0 and 7 are often used by other things. With `interrupt` set to `none`, the
board never interrupts, and it still keeps the correct time.

## Host integration

A board that connects the machine to your computer. It is not period hardware.

### `hostbridge`: our own board, not a period board

**This board never existed.** It is the one part of the machine that is not history, and this
manual says so.

It moves **files between the guest and your computer**, in both directions. It is **sandboxed**.
The guest sees one folder that you choose, and **it cannot leave that folder**. It cannot use
`..` or an absolute path to leave it. This is a fixed rule, not a setting.

The default port is `B0`. **The utilities are not in the board.** `R`, `W` and `HDIR` are
ordinary CP/M `.COM` programs. They are on a disk, they run at the `A>` prompt, and they use the
two ports of the board, as any other CP/M program could. They were assembled *in the machine*,
with the machine's own assembler, from 8080 source that ships with the package. You can read
their code.

The file-transfer chapter describes this board. It is the fastest way to get your own code into
CP/M. XMODEM also works, over an ordinary serial line, as it did on real machines.

## PROM programmers

A board that does not run software, but *programs* it into a chip. You load a period EPROM
programmer routine and run it. The bytes that it programs go into a socket, and you can save the
socket to a file.

### `pb1`: SSM PB1

The **SSM PB1** (Solid State Music) is a 2708 and 2716 EPROM programmer. It has a **4K window**
in memory (default `D000`) for its two sockets: **U22** holds a 2708 (1K), and **U23** holds a
2716 (2K). It has one **control port** (default `10`). **The default machine has its 2SIO at
`10` too**, so move one of the two boards when you add a `pb1`.

Software programs the chip:

1. An `OUT` to the control port arms the board and selects the chip. Write `01` for the 2708, or
   `02` for the 2716.
2. After that, every byte that the guest **writes into the window** is programmed into the chip.
3. A **read** of the window disarms the board again. The period routines end in this way, and it
   turns the LED off.

"Programming is a write, and disarming is a read" has two results:

- **A socket starts erased**, with every byte `FF`. **Programming can only clear bits to 0.** It
  cannot set them, as on a real EPROM. Program a *blank* chip. A second program over the first
  gives the AND of the two.
- The program is in the board, not on your computer. To keep it, **`SAVE` it to a file**
  (below). A power cycle reads the sockets again from their mounts, so a program that you did
  not save is lost.

#### Programming an EPROM, and making a hex file

You need this board, not only a write to memory, because you want to run the period *software*.
The PB1 manual has SSM's programmer routines, one for the 2708 and one for the 2716. Each
routine copies bytes from `4000` into the socket. The 2708 routine starts at `0100`. The package
does not include these routines. Enter one from the manual, or `LOAD` it from a hex file of your
own. After that, put the data at `4000`, run the routine, and save the result:

```
altairsim> FILL 4000-43FF E5
altairsim> LOAD PROG2708.HEX
altairsim> BREAK F021
altairsim> RUN 100
altairsim> SAVE eprom.hex D000-D3FF
```

- `FILL` puts the 1K that you want to program at `4000`. You can also `LOAD` your own data
  there.
- `LOAD PROG2708.HEX` loads the 2708 routine.
- `BREAK F021` is needed because SSM's routine ends with a jump to SSM's own system monitor, at
  `F021`. This machine does not have that monitor. The breakpoint stops the machine at that
  jump, after the chip is programmed.
- `RUN 100` runs the routine. It arms the board and programs the 2708.
- `SAVE` reads the socket window from the bus and writes it to a file. After it is programmed,
  the socket reads like other memory. `eprom.hex` is an ordinary Intel HEX image of the chip.
  You can give it to another tool, or `LOAD` it again later.

Your own routine works in the same way. It writes `01` to port `10` (or `02` for a 2716), and
then writes the data into the window. After it runs, `SAVE` the window.

The board also has an optional **EPROM area on the board** (U11 to U14 on the real board). These
are read-only chips that you mount above `8000` with `[[board.prom]]` (`at` and `mount`). Use
them as the *source* when you copy one EPROM to another.

