# Serial ports, sockets and telnet

The Altair had no screen. It had a serial board, and you connected a device to it: a Teletype, a
video terminal, a modem or a paper-tape reader. The board did not know which device it was. It
moved characters.

`altairsim` works in the same way. **Every board that moves characters has one or more UNITS,
and you can CONNECT each unit to an ENDPOINT.** The unit is the connector on the back of the
board. The endpoint is what you connect to it.

```
altairsim> CONNECT sio0:a socket:2323
altairsim> DISCONNECT sio0:a
```

## The same in a machine file

`CONNECT` connects a unit now. A machine file makes the same connection each time that the
machine starts. `CONNECT <id>:<unit> <endpoint>` becomes `connect = "<endpoint>"` in the
`[board.unit.<unit>]` table of the board that has that `id`.

```
altairsim> CONNECT sio0:a telnet:2323
```

In a machine file:

```toml
[machine]
name = "cpm-telnet"
base = "default"

[[board]]
id = "sio0"                    # no type: change the board that is there

  [board.unit.a]
  connect = "telnet:2323"
```

This is a complete machine file. The `default` machine already has the board `sio0`. The file
names that board by its `id`, and gives only the key that changes. All the other settings of the
board stay.

**Do not write a `type` on that `[[board]]`.** With a `type`, the file replaces the board, and
the settings that the base made on it are lost. The configuring chapter gives the rule.

The endpoint is always one string in quotes, with its options and its tap.

You can get the same result in two other ways:

- Type the `CONNECT` command at the monitor, and then type `CONFIG SAVE mine.toml`. The saved
  file is a complete machine with no `base`. It lists every board and every setting.
- Put the command in the `startup` list: `startup = ["CONNECT sio0:a telnet:2323", "RUN FF00"]`.

In this chapter, the words "In a machine file:" introduce the machine-file form of the command
above them. Each sample shows only the tables that change.

## The endpoints

This is the complete list.

| Endpoint | What it is |
|---|---|
| `console` | the terminal where you started the program: your keyboard and your screen |
| `null` | nothing. Writes go nowhere, and a read never gets a byte |
| `loopback` | the unit itself. What the guest writes comes back as a read |
| `socket:PORT` | **listens** on that TCP port, and passes the bytes unchanged. Add `?banner` to greet each caller |
| `socket:HOST:PORT` | **calls** that host and port, and passes the bytes unchanged |
| `telnet:PORT` | **listens** like `socket:PORT`, and uses the **Telnet protocol**, so that a `telnet` client works well. Use it when a **person** connects. It greets each caller, and `?banner=off` stops that |
| `telnet:HOST:PORT` | **calls** like `socket:HOST:PORT`, as a telnet client |
| `serial:DEVICE` | a real serial port on your computer |
| `pty` | a pseudo-terminal that a terminal program opens as a serial port. Only on macOS and Linux. `pty:LINK` selects the name |
| `in:PATH` | a file that the board reads: a **paper-tape reader** |
| `out:PATH` | a file that the board writes: a **paper-tape punch** |
| `in:PATH,out:PATH` | a reader and a punch on one line, with two files |
| `terminal` | a terminal in its own window: a VT100, ADM-3A, VT52 or H19 |
| `printer:QUEUE` | a print queue on your computer. Only on a build that has host printing |
| `scripted` | a terminal that a program types into, for the MCP tools and the tests |

You can add two things to any endpoint. `|FILE` writes every byte on the line to a log file, and
`|socket:PORT` lets a second person watch the line and type on it (`|pty` does the same for a
terminal program, on macOS and Linux, and `|serial:DEVICE` does the same for a terminal on a
serial port). See "Tap a line to a log
file" and "Mirror a line", below.

### `null` is not an error

A unit that is not connected is `null`. **This is a correct state, not a fault.** A 6850 with no
cable is always ready to send. A program that writes to it runs correctly, and nobody receives
the output. A read never completes, because nothing sends. A real board with no cable does the
same.

### The colon makes the difference

`socket:2323` **listens**. `socket:localhost:2323` **calls out**. A port alone is a port that
you own. A host with a port is a place that you go to.

## Only one unit can hold the console

The console is **your keyboard**, and you have one. When you connect a second unit to `console`,
**it takes the console, and says which unit it took it from**:

```
altairsim> CONNECT sio1:a console
console taken from sio0:a
sio1:a: connected to console
```

This is not an error. If two boards read one keyboard, each one would get about half the
characters. `SHOW CONSOLE` shows which unit has the console, and the console settings.

## Use telnet to connect to the guest

Connect a unit to a `telnet:` port, and the guest has a serial port with a terminal on it:

```
altairsim> CONNECT sio0:a telnet:2323
altairsim> RUN
```

In a machine file:

```toml
[[board]]
id = "sio0"

  [board.unit.a]
  connect = "telnet:2323"
```

In another terminal on your computer, type:

```
$ telnet localhost 2323
Connected to AltairSim X.Y.Z (sio0:a) on port 2323
```

The console of the guest is now that window. Your first terminal still has the monitor and
`Ctrl-E`.

Unit `a` is the console port of the `default` machine. To keep the console where it is and give
the guest a second terminal, connect unit `b`: write `sio0:b` at the monitor, or
`[board.unit.b]` in the machine file.

### `socket:` or `telnet:`

Both raise carrier when a caller connects, and drop it when the caller leaves. The guest cannot
tell them apart. They differ in what they send to the far end:

| | `socket:` | `telnet:` |
|---|---|---|
| Use it for | another program or another machine | a **person** with `telnet` or `nc` |
| What goes on the line | the bytes of the guest, and nothing else | the bytes of the guest, and the Telnet settings for echo and single keys |
| What the person sees | each key two times, and a whole line sent at Enter | each key one time, sent when it is pressed |
| Greeting | none, unless you add `?banner` | one line with the machine, the unit and the port |

Use `telnet:` when a person types to the guest. Use `socket:` when a program is at the far end:
one `altairsim` connected to another, a file transfer, or a tool that uses raw bytes.

A `telnet` client on a raw `socket:` shows each key two times, because the client and the guest
both echo it. Enter sends a whole line, with a line feed in place of the carriage return. You
cannot fix this from the keyboard. The Telnet protocol sets up the two ends to agree.

### The connect banner

A `telnet:PORT` line greets each person who connects, before the guest sends anything. This is
the `Connected to ...` line above. It names the build, the unit and the port, so that you know
that you reached the correct machine. Only the caller sees it, and nothing goes to the board. To
turn it off, add `?banner=off`:

```
altairsim> CONNECT sio0:a telnet:2323?banner=off
```

A `socket:PORT` line has no banner, because a program at the far end would read the banner as
data. To greet a person on a raw socket, add `?banner`: `CONNECT sio0:a socket:2323?banner`. A
banner is only for a port that listens. On a line that calls out, the program refuses `?banner`.

## A terminal in its own window

A telnet client must be installed and pointed at the correct port. It changes any byte that
looks like a telnet command, which can break cursor sequences and file transfers. A modern
terminal also emulates a modern terminal, not the ADM-3A or VT52 that the software of the period
expects.

`terminal` is a terminal that the program draws itself, in its own window:

```
altairsim> CONNECT sio0:a terminal
altairsim> RUN
```

The console of the guest is now that window. The monitor and `Ctrl-E` stay in the terminal that
you started from. The window opens when you connect the line. It answers the reports that a
program of the period asks for, such as `ESC[6n`.

`terminal` alone is a VT100 in an 80×24 window. Give options after a `?`, joined by `&`:

```
altairsim> CONNECT sio0:a terminal?emulation=adm3a
altairsim> CONNECT sio0:a terminal?emulation=vt52&size=80x24
altairsim> CONNECT sio0:a "terminal?emulation=h19&size=132x24"
altairsim> CONNECT sio0:a terminal?emulation=vt100&phosphor=amber&width=1100
```

In a machine file:

```toml
[[board]]
id = "sio0"

  [board.unit.a]
  connect = "terminal?emulation=vt52&size=80x24"
```

The options are part of the string. A machine file always has quotes around the endpoint, so
the `&` needs no special quotes there.

- `emulation` is `vt100` (the default, and `ansi` is the same), `adm3a` (the Lear Siegler
  ADM-3A, the usual CP/M terminal), `vt52`, or `h19` (the Heath/Zenith H19, a VT52 with an ANSI
  mode).
- `size` is *columns*×*rows*. The default is `80x24`.
- `phosphor` is the color of the tube: `green` (the default) or `amber`.
- `width` is the width of the window when it opens, in pixels. Without it, the window is about
  half as wide as the screen.

The text uses the **DEC VT220** character set. It goes well with `[display] crt = true`, which
the configuring chapter describes. With `crt = true`, a `width` opens the window at that size.

The window has its own settings for changing bytes, under `[terminal]`. See "The built-in
terminal has these too", below.

## Call another computer

```
altairsim> CONNECT sio0:b socket:bbs.example.com:23
```

The program opens the connection. The guest sees a serial line with carrier, and it can run a
terminal program of the period over it.

## A real serial port

```
altairsim> CONNECT sio0:a serial:/dev/tty.usbserial-A600K1XY
altairsim> CONNECT sio0:a serial:COM3
```

The second form is for Windows. The bytes go out of a real UART, down a real cable, to the
device at the other end. The modem control lines (DCD, CTS and RTS) are connected too.

**If you give a wrong device name, the program lists the serial ports on your computer.** A
cable can appear under a name that is one character different from what you expected, and the
list shows you the correct name.

### The board sets the frame on the line

The program opens the port at 9600 8N1. **The board then sets the port again at once**, because
only the board knows what frame it sends. To send 300 baud, 7 bits and even parity, set it on
the **board**, where an operator in 1975 set it:

- On an **88-SIO** or an **88-ACR**, jumpers set the word format. The board's `baud`,
  `data_bits`, `stop_bits` and `parity` properties are the frame on the line.
- On an **88-2SIO**, the baud rate is a jumper (`baud`), but the **guest writes the word format
  to a register** of the 6850. For this reason, the 2SIO has no `data_bits` property. The frame
  on the line is what the guest last set. A guest that selects 7E1 sets the cable to 7E1.

**`data_bits` is a frame, not a mask.** `data_bits=7` puts seven data bits in each character on
the line. It does not clear bit 7 of the byte that the guest wrote. Do not use it to fix a
prompt on your screen. Use the console settings (below).

### Give the machine its real crystal before you transfer a file

```
altairsim> SET cpu0 clock_hz=2000000
```

A guest has no clock. It counts instructions. At full speed, its "three-second" timeout passes
in a few milliseconds, and it decides that your sender is dead. Full speed is correct for a
machine that talks only to itself. **A machine that talks to the outside world needs the real
crystal.** The troubleshooting chapter tells you more.

### The port, the baud rate and the crystal in a machine file

The three commands are:

```
altairsim> SET cpu0 clock_hz=2000000
altairsim> SET sio0:a baud=9600
altairsim> CONNECT sio0:a serial:/dev/tty.usbserial-A600K1XY
```

In a machine file:

```toml
[[board]]
id       = "cpu0"
clock_hz = 2000000             # SET cpu0 clock_hz=2000000

[[board]]
id = "sio0"

  [board.unit.a]
  baud    = 9600               # SET sio0:a baud=9600
  connect = "serial:/dev/tty.usbserial-A600K1XY"
```

A `SET` on a board is a key in the `[[board]]` of that board. A `SET` on a unit is a key in the
table of that unit.

## A pseudo-terminal for a terminal program

On macOS and Linux, a line can go to a **pseudo-terminal**. A terminal program, such as
`screen`, `minicom` or `cu`, opens a pseudo-terminal as it opens a serial port. No cable and no
port number are necessary. A file transfer program can use the line also, because each byte
goes through unchanged.

```
altairsim> CONNECT sio0:b pty
sio0:b: connected to pty
pty: /tmp/altairsim0 (/dev/ttys004)
```

The program prints the name of the line. The first line is `/tmp/altairsim0`, and the next one
is `/tmp/altairsim1`. Type `screen /tmp/altairsim0` in a second window to use the line. The
name is a link to the device in the brackets. The program removes the link when the line
disconnects, and when the program stops.

To select the name, give a path after `pty:`. The name is then the same each time. A path that
is not absolute is relative to the folder of the machine file.

```
altairsim> CONNECT sio0:b pty:/tmp/altair-line
sio0:b: connected to pty:/tmp/altair-line
pty: /tmp/altair-line (/dev/ttys004)
```

In a machine file:

```toml
[[board]]
id = "sio0"

  [board.unit.b]
  connect = "pty:/tmp/altair-line"
```

When the machine file starts the machine, the program prints the same `pty:` line, with the
name of the unit before it.

- **When no program has the name open, the line discards what the guest sends.** The guest
  does not wait.
- **A program that has the name open is the carrier.** The DCD and DSR pins of the line are on
  while a program has the name open, and off when it closes the name. On a 2SIO, the guest
  sees DCD only after `SET sio0:b dcd=wired`.
- **A program that reads slowly makes the guest wait.** The guest loses no bytes.
- **`pty:` replaces a link that is at the path.** If the path is a file that is not a link,
  the program refuses, and does not replace the file.
- **Windows has no pseudo-terminal, and refuses `pty`.** Use `socket:PORT` or `telnet:PORT`.

To let a second person watch a line that goes to a different endpoint, use `|pty`. See "A
mirror on a pseudo-terminal", below.

## A paper-tape reader and punch: `in:` and `out:`

A **paper-tape station** was connected to a serial or parallel line. The program makes one from
two files on your computer:

```
altairsim> CONNECT lpt0:prn out:printout.txt              # a punch: save what the board sends
altairsim> CONNECT 4pio0:ja in:reader.tap                 # a reader: give a file to the board
altairsim> CONNECT 4pio0:ja in:reader.tap,out:punch.tap   # both, on one line
```

In a machine file, for a machine that has the board `4pio0`:

```toml
[[board]]
id = "4pio0"

  [board.unit.ja]
  connect = "in:reader.tap,out:punch.tap"
```

The name of the table is the name of the unit: `ja` here, `a` on a 2SIO.

`in:` is a **reader**. It gives the bytes of the file to the board one at a time, from the
start. When the file ends, the line goes **quiet**, with no error and no end-of-file byte, as a
reader with no more tape does. If the file does not exist, `CONNECT` refuses, and names the
path.

`out:` is a **punch**. The board's output goes to the file. If the file does not exist, the
punch makes it. **The punch does not truncate the file.** It writes from the start, and the end
of a longer old file stays in the file.

The reader and the punch are **separate files, each with its own position**. Both are 8-bit
clean: the bytes on the line are the bytes in the file. A relative path starts from the
machine's folder.

### The 88-HSR: a reader with a speed

A program that times its input needs a reader with a real speed. Add `?cps=N` (characters per
second) or `?baud=N` (a line rate, at 10 bits for each character):

```
altairsim> CONNECT 4pio0:ja in:tape.tap?cps=300       # the 88-HSR high-speed reader
altairsim> CONNECT 4pio0:ja in:tape.tap?cps=30        # a slow reader
```

`in:tape.tap?cps=300` **is** the MITS 88-HSR. Give only one of `cps` or `baud`. Without either,
the reader runs as fast as possible. The punch takes no options.

## Print on a real printer

On macOS and Linux, a line can go to a print queue on your computer:

```
altairsim> CONNECT lpt0:prn printer:linewriter
```

The **Windows** builds do not have host printing yet. On Windows, send the output to a file
(`out:`) or a socket (`socket:`). To see what your build can do, connect to `printer:` with no
name. The program lists the queues that it can reach, or tells you that the build has no host
printing.

Any line can use `printer:`, not only the 88-C700 of the Boards chapter. The bytes go into a
buffer, and then to the print system as one **job**.

**A printer has no "done" signal, so the program decides where a job ends.** Set it on the
endpoint:

| Option | Ends the job | Default |
|---|---|---|
| `?idle=N` | after **N seconds** with nothing more printed. `0` means never | `5` |
| `?onff` | also on a **form feed** (the page-eject character) | off |
| `?max=N` | at **N bytes**, so that a program that does not stop cannot fill memory | a large number |

```
altairsim> CONNECT lpt0:prn printer:linewriter?idle=15
altairsim> CONNECT lpt0:prn printer:linewriter?onff
altairsim> CONNECT lpt0:prn "printer:Generic / Text Only?idle=0&onff"
```

`?onff` with no value turns the option on. The first option that applies ends the job. **An
empty buffer never prints**, so a form feed and then silence does not print a blank page. A job
also prints when you `DISCONNECT` the line, load another machine or quit.

**Use a *raw* queue**, which passes the bytes to the printer unchanged. A normal queue can
change a printer control language that it does not recognize. Make the raw queue one time, in
the printer settings of your operating system. If a queue name has spaces, put quotes around the
whole endpoint, as above.

## Tap a line to a log file

Add **`|FILE`** to any endpoint, and the program writes every byte that crosses the line, in
both directions, to a text file. It writes hex and ASCII, with times. The guest cannot tell that
the tap is there, and the tap never changes a byte. For this reason, you can use it on a binary
transfer.

```
altairsim> CONNECT sio0:a socket:2323|bbs.hex
```

In a machine file:

```toml
[[board]]
id = "sio0"

  [board.unit.a]
  connect = "socket:2323|bbs.hex"
```

Connect with telnet, and use the guest. `bbs.hex` fills with the conversation:

```
# altairsim capture  socket:2323  2026-07-29 14:03:11  fmt=dump
+0.001000  TX 0000  41 54 5A 0D                                       ATZ.
+0.048213  RX 0000  0D 0A 4F 4B 0D 0A                                 ..OK..
+9.100000  [DCD^]
+45.30000  [DTR_]
```

- `TX` is what the guest sent, and `RX` is what came back.
- The offset counts the bytes in a burst.
- The right column is the ASCII, with a `.` for a character that cannot be printed.
- A line in `[...]` is a change on a **modem control line**, such as carrier, DTR or RTS. `^` is
  a rising edge, and `_` is a falling edge.

The program empties the file each time that you connect. Add the `append` option to keep what
the file holds, and to add to the end of it. `SHOW` prints the tap, and `CONFIG SAVE` saves it,
so a machine file can have a line that is always traced.

### The layouts and the options

The tap takes options after the file name, in the `?key=value` form:

```
altairsim> CONNECT sio0:a socket:2323|bbs.hex?fmt=cols
altairsim> CONNECT sio0:a in:reader.tap?cps=300|trace.log?fmt=jsonl
```

- **`fmt=dump`** (the default) is the layout above: one hex row on each line, in time order.
- **`fmt=cols`** puts what the guest sent on the **left**, and what it received on the
  **right**. A request and its reply read down the page.
- **`fmt=jsonl`** writes one JSON record for each transfer, for another program to read.
- `ts=elapsed` (the default, seconds from the first byte), `ts=wall` (the clock of your
  computer), or `ts=none`
- `width=N`: the bytes in each hex row. The default is 16
- `gap=MS`: how long a quiet line waits before it writes a part row. The default is 200 ms
- `pins=off`: leave out the changes on the modem control lines
- `append`: add to the end of the file. Without `append`, the program empties the file first

### A log of plain text: `fmt=text`

**`fmt=text`** writes no hex. The file holds only the characters that the guest prints, with
nothing added. Use it to keep a record of a session that you can read:

```
altairsim> CONNECT sio0:a console|session.log?fmt=text
```

In a machine file:

```toml
[[board]]
id = "sio0"

  [board.unit.a]
  connect = "console|session.log?fmt=text"
```

- The program writes each character to the file at the time that the guest prints it. The file
  is an ordinary text file, and you can read it, search it or copy it while the line is in use.
  For example, `tail -f session.log` in a second window follows the session. In PowerShell on
  Windows, the command is `Get-Content -Wait session.log`.
- The file does not hold what you type. If the guest echoes a character, the echo is in the
  file, as it is on a terminal.
- The file holds each byte as the guest sent it. The `[console]` settings do not change the
  bytes on a line (see "The console settings change bytes, and a line does not", below).
- `append` is the only option that `fmt=text` accepts. The options `ts`, `width`, `gap` and
  `pins` are for the hex layouts, and `fmt=text` refuses them.

### A tap on a mirror

A line can have a mirror and a tap together. Put the tap last:

```
altairsim> CONNECT sio0:a console|socket:2323|session.log?fmt=text
```

The next section describes the mirror.

## Mirror a line so a person can watch and take over

A **mirror** sends the line to a *socket*, in both directions. Add `|socket:PORT` to any
endpoint. A second person can then type `nc localhost PORT` (or `telnet localhost PORT`; a new
Mac has `nc` but no `telnet`) to see every character that the guest prints, and can also **type
on the line**:

```
altairsim> CONNECT sio0:a console|socket:2323
```

The guest talks to your terminal as before. The usual use is a console that a program drives. A
person connects, watches the program work, and types when they want to.

The mirror adds no echo. The watcher sees what the guest sends, and what the watcher types goes
to the guest. A password that the guest does not echo stays hidden. One watcher can connect at a
time.

Add `?ro` to make the mirror **watch-only**:

```
altairsim> CONNECT sio0:a console|socket:2323?ro
```

A slow watcher never slows the guest. The watcher loses some output, and the guest loses no
bytes. `SHOW` prints the mirror, and `CONFIG SAVE` saves it.

`nc` and `telnet` stop when the simulator stops, and Windows installs neither. The scripts
`tools/mirror-watch.sh` and `tools/mirror-watch.ps1` in the package watch a mirror, and connect
again when the simulator starts again. They only watch. The chapter *The MCP server* describes
them.

### A mirror on a pseudo-terminal

On macOS and Linux, a mirror can go to a **pseudo-terminal** as an alternative to a socket. A
terminal program, such as `screen`, `minicom` or `cu`, opens a pseudo-terminal as it opens a
serial port. Add `|pty` to any endpoint:

```
altairsim> CONNECT sio0:a console|pty
sio0:a: connected to console|pty
mirror: /tmp/altairsim0 (/dev/ttys004)
```

The first mirror is `/tmp/altairsim0`, and the next one is `/tmp/altairsim1`. A second person
types `screen /tmp/altairsim0` to watch the line and to type on it. The name is a link to the
device in the brackets, and the program removes the link when the line disconnects.

To select the name, give a path after `pty:`. A path that is not absolute is relative to the
folder of the machine file. Use this form in a machine file, because the name stays the same:

```
altairsim> CONNECT sio0:a console|pty:/tmp/console
```

In a machine file:

```toml
[[board]]
id = "sio0"
a = "console|pty:/tmp/console"
```

`?ro` makes this mirror watch-only also. When no program has the name open, the mirror discards
what the guest prints. If `pty:` names a file that is not a link, the program refuses, and does
not replace the file. Windows has no pseudo-terminal, and refuses `|pty`.

### A mirror on a serial port

A mirror can go to a **serial port** on your computer. A hardware terminal, or a second
computer with a terminal program, then watches the line and types on it. Add `|serial:DEVICE`
to any endpoint:

```
altairsim> CONNECT sio0:a console|serial:/dev/cu.usbserial-AL009KFH
sio0:a: connected to console|serial:/dev/cu.usbserial-AL009KFH
mirror: /dev/cu.usbserial-AL009KFH at 9600 baud
```

The port is 8 data bits, no parity and 1 stop bit, with no flow control. The rate is 9600 baud.
`?baud=N` selects a different rate, and the terminal must use the same one. No board programs
this port, as it does for `serial:` as an endpoint, so the rate is part of the mirror:

```
altairsim> CONNECT sio0:a console|serial:/dev/cu.usbserial-AL009KFH?baud=19200
```

In a machine file:

```toml
[[board]]
id = "sio0"
a = "console|serial:/dev/cu.usbserial-AL009KFH?baud=19200"
```

`?ro` makes this mirror watch-only also. Use `&` to give two options:
`serial:COM3?baud=19200&ro`.

Three things are different from a socket or a pseudo-terminal:

- **The program cannot tell when a terminal is there.** The mirror sends the output to the port
  at all times. A terminal that you connect later shows the output that is still waiting to go,
  which is at most 256 KB, and the oldest output is discarded first.
- **The port sets the speed.** At 9600 baud a long listing takes time to print, and the guest
  does not wait for it. Only a terminal that falls behind loses the oldest text.
- **A key that you type reaches the guest only while the guest runs.** This is the same as for
  a socket.

If the device does not open, the program refuses, and lists the serial ports that it can see.
The port cannot be the same one that the line uses.

## An endpoint that `CONNECT` does not understand is an error

If the program cannot read your endpoint, it **refuses, and lists the forms that it accepts**:

```
altairsim> CONNECT sio0:a sockit:2323
no endpoint 'sockit:2323'. Try: console | null | loopback | scripted | socket:PORT[?banner] | socket:HOST:PORT |
telnet:PORT[?banner=off] | telnet:HOST:PORT | serial:DEVICE | pty[:LINK] |
in:PATH | out:PATH | terminal[?emulation=vt100&size=80x24] | printer:QUEUE |
<endpoint>|FILE | <endpoint>|socket:PORT | <endpoint>|pty[:LINK] |
<endpoint>|serial:DEVICE[?baud=N]
```

It never uses `null` in its place. A machine that boots and prints nothing sends you to look for
the fault in the wrong place.

## The console settings change bytes, and a line does not

**The console settings are the only part of the program that changes a byte. Every serial line
is 8-bit clean.** No board has a setting that clears a bit.

| Setting | What it does |
|---|---|
| `upper` | changes what you type to upper case |
| `strip7in` | clears bit 7 of every character that you type |
| `strip7out` | clears bit 7 of every character that the guest prints |
| `crlf` | changes line endings |
| `echo` | shows your keys locally |
| `bell` | rings the bell of your terminal on `Ctrl-G` |
| `bsdel` | makes Backspace and Delete send the same code: `off` (the default), `bs`, or `del`. The `default` machine sets `bs`. `SHOW CONSOLE` shows the value in use |
| `stop` | the STOP key, a control character. The default is `Ctrl-E`. `attn` is the same setting |
| `base` | `hex` or `octal`: how the **monitor** prints numbers. It does not change a byte |

Set them with `CONSOLE k=v`, or `SET CONSOLE k=v`. You can give several at one time:

```
altairsim> CONSOLE strip7out=on
altairsim> CONSOLE upper=on crlf=off
altairsim> CONSOLE stop=1D
```

In a machine file:

```toml
[console]
strip7out = true
upper     = true
crlf      = false
stop      = 0x1D
```

`[console]` is a table of its own, not a `[[board]]`. A setting that is `on` or `off` at the
monitor is `true` or `false` in the file.

`stop=1D` moves the STOP key from `Ctrl-E` to `Ctrl-]`. **The STOP key must be a control
character**, so that you cannot press it by accident in the middle of a word.

### Why: `MEMORY SIZ?`

Boot MITS BASIC with the settings off, and it asks:

```
MEMORY SIZ?
```

**MITS BASIC sets bit 7 of the last character of every message**, to mark the end of the string.
`E` is `45`, and with bit 7 set, it is `C5`. Your terminal prints whatever `C5` means to it.

On the real machine, the board sent all eight bits. The Teletype at the other end **did not read
bit 7**, because on a Model 33 bit 7 is the parity position. It printed `E`. **`strip7out` is
the terminal that does not read bit 7**, so it belongs on the console, not on the board.

A 7-bit mask on the board would fix the prompt, and it would also **damage every XMODEM transfer
through that port**. XMODEM sends binary data, and bit 7 is a real bit in half of its bytes. A
line can carry binary. Only the terminal knows that it shows text.

### Where the console settings stop

The settings belong to the console, so they stop where the console stops:

```
altairsim> CONSOLE strip7out=on
altairsim> CONNECT sio0:a socket:2323
sio0:a: connected to socket:2323
```

Connect with telnet, and `MEMORY SIZ?` is back. The same is true of `upper`, `crlf`, `echo`,
`bell` and `bsdel`, and of a `serial:` port. The console settings are still on, but the byte no
longer goes through the console. It goes from the 2SIO to the socket to your telnet client, and
every step on that path is 8-bit clean.

Set the same thing on the terminal that shows the text. Every terminal program and telnet client
has these settings, with its own names: strip parity or 7-bit display for `strip7out`, local
echo for `echo`, and newline handling for `crlf`.

The STOP key also belongs to **your keyboard**. The program never looks for it on a socket or a
serial line. A `05` that comes down a cable is part of somebody's data.

### The built-in terminal has these too

The built-in `terminal` window draws the text itself, so it has the same kind of settings, under
`[terminal]` in place of `[console]`:

| Setting | What it does |
|---|---|
| `upper` | changes what you type to upper case |
| `strip7in` | clears bit 7 of every character that you type |
| `strip7out` | clears bit 7 of every character that the guest prints |
| `cr` | `cr` (the default: pass the guest's CR unchanged) or `crlf` (add an LF after every CR) |
| `echo` | shows your keys locally, for half-duplex software |
| `bell` | passes `Ctrl-G` to the terminal. On by default |
| `bsdel` | makes Backspace and Delete send the same code: `off` (the default), `bs`, or `del` |

Set them with `SET TERMINAL k=v`, read them with `SHOW TERMINAL`, or put a `[terminal]` table in
a machine file. The settings apply to the `terminal` line that is open.

```
altairsim> CONNECT sio0:a terminal
altairsim> SET TERMINAL strip7out=on
```

In a machine file:

```toml
[[board]]
id = "sio0"

  [board.unit.a]
  connect = "terminal"

[terminal]
strip7out = true
```

`strip7out` is useful for an **even-parity monitor**. MITS Programming System II puts parity in
bit 7 of every character, so it sends a carriage return as `8D`. In the terminal window, `8D` is
not a carriage return, and every line feeds without going back to the left. `strip7out=on` fixes
this. `cr=crlf` is for a guest that sends a CR alone and expects the terminal to add the LF.
