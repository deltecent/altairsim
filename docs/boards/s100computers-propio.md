# S100Computers Console IO Board — the Propeller console

**Status:** done — the board's whole bus interface: a polled status port and a data port, with
its documented status bits as presets you can override. Board type `propio`. It is the console
of the Dual SD and IDE-AB machine files in the *User Manual*.

**Not modelled:** the Propeller's own terminal (the VGA display and the PS/2 keyboard) — the
host's terminal takes its place — and the output-busy delay. See *Limitations*.

## The real hardware

A modern S-100 console board by **John Monahan** (S100Computers), built around a **Parallax
Propeller** (P8X32A). The Propeller takes a PS/2 keyboard in and drives a VGA display out, and
presents both to the bus as one ordinary **two-port polled console**: a status port and a data
port. There is no UART and no chip register map on the bus side — the Propeller *is* the
terminal. It is the console the S100Computers Z80 CP/M 3 systems use, the Dual SD machines
among them.

The board was designed to "splice into almost any S-100 system", so nearly everything is a
switch or a jumper:

- **SW2** sets the status port address and **SW3** the data port address. The board's monitor
  uses **`00`/`01`**; its own test software uses **`14`/`15`**.
- Jumpers **P74–P77** choose which status bit carries keyboard-ready and which carries
  output-ready, and each one's polarity.

The handshake is two flip-flops. When the Propeller latches a key it loads U47 (74LS374) and
sets *keyboard ready*; a CPU read of the data port clears it. A CPU write to the data port
latches the character into U46 and sets *output busy*; the Propeller clears it once it has taken
the character.

## Sources

| Source | Path | Authority |
|---|---|---|
| S100Computers *Console IO Board* project page (HTML, fetched 2026-08-16) | `reference/Console IO Board.md` | **Authoritative** for the port switches, the P74–P77 status jumpers, the worked-example driver and the handshake circuit |
| The page's example driver (the SD Systems 8024 convention its shipped monitor assumes) | same | **Authoritative** for the default bits: `AND 02H` for keyboard ready, `AND 04H` for output ready, both active high |

The page gives no licence. Nothing from it is redistributed; the reference is a text
distillation that cites it.

## Register reference

The defaults below are the board's worked example. Every one is a jumper on the real board and
a property here.

| Addr | OUT (write) | IN (read) |
|---|---|---|
| `status_port` (default `00`) | ignored — there is no control register | status byte: bit 1 = keyboard ready, bit 2 = output ready, both active high; every other bit 0 |
| `data_port` (default `01`) | display character (sets output busy) | keyboard character (clears keyboard ready) |

The period driver:

```
INPUT:  IN   A,(00H)     ; keyboard status
        AND  02H         ; bit 1 = a key is waiting
        JP   Z,INPUT
        IN   A,(01H)     ; the key (clears the flag)

OUTPUT: IN   A,(00H)     ; console status
        AND  04H         ; bit 2 = ready (0 = busy)
        JP   Z,OUTPUT
        LD   A,C
        OUT  (01H),A     ; the character to display
```

## How it is simulated

- **It is a preset, not a new engine.** `PropIoBoard` (`src/boards/propio.{h,cpp}`) is a
  one-channel `StrapSerialBoard` (`src/boards/strapserial.h`), the same polled "read a status
  bit, read/write a data byte" engine behind `gsio`. `propioProfile()` holds the board's
  defaults: status `00`, data `01`, `dav` = 1, `tbmt` = 2, `inverter_gate` off.
- **Bus cycles:** `IoRead` and `IoWrite` at the two strapped ports, nothing else.
- **The status byte** is built from the line: keyboard ready is `readable()` and output ready
  is `writable()` of the connected `ByteStream`, each at its strapped bit. The one
  `inverter_gate` flips both, because on the strap engine both bits share one buffer.
- **The data port:** a read takes one byte off the line (a quiet line reads `00`); a write puts
  one on it at once.
- **The line** is one serial unit named `serial`. `CONNECT con0:serial …` (or `connect=` in the
  machine file) attaches the console, a file, a socket, a real serial port, `in:`/`out:`,
  `null` or `loopback`. An unconnected board holds a `NullStream`: always ready to send, never
  a key.
- **No interrupts, no bus mastering, no media, no `Clock` use.**
- **Properties** are board level, because there is only one channel: `profile`,
  `status_port`, `data_port`, `dav`, `tbmt`, `inverter_gate`, `baud`, `connect`. `profile`
  reads `custom`, because the board's preset is not one of the strap engine's named profiles.
  Choosing one of those replaces the board's straps with that profile's. `baud` sets the rate
  on a connected real serial port only; it does not pace the emulated line.

### Reset

- `Reset::PowerOn` (POC*) and `Reset::Bus` (RESET*): nothing changes. The straps are config,
  the line stays connected, and a key waiting on the line is still waiting after a reset. The
  reference does not say whether a reset clears the board's two flip-flops; here they are the
  line's own state, which a reset does not touch.

SNAPSHOT carries only the receive counter. The straps are re-applied from the machine file, and
the line is re-opened from `connect`.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| Keyboard ready is **bit 1**, output ready is **bit 2**, both **active high** | The MASTER monitor and CP/M 3's CHARIO are written for this layout. Any other layout hangs the console at the first character |
| Reading the **data** port clears keyboard ready; reading status does not | A driver that polls status twice would see a key vanish, or the same key twice |
| Writing the status port does nothing | There is no register there. Letting the write change a strap would give the board a feature it never had |
| Ports and bits are **overridable** | The real board is jumpered. A machine whose owner set SW2/SW3 to `14`/`15`, or rewired P77 for an active-low ready, is one property away, not a new board type |

## Limitations and deliberate departures

- **The Propeller's terminal is not modelled.** On the real board the Propeller draws the VGA
  screen and reads the PS/2 keyboard itself, with its own escape sequences. Here the bytes go
  straight to whatever the line is connected to — usually the host terminal — so the screen
  behaves like that terminal, not like the Propeller's firmware. A program that relies on
  Propeller-specific escape sequences will show them raw.
- **Output is never busy.** A write goes out at once, so output ready drops only if the
  connected line cannot take a byte. Software that times the Propeller's display speed by
  counting busy polls sees none.
- **No interrupts.** The strap engine has none. The reference describes no interrupt wiring on
  the board's bus side.
- **The status bits share one polarity.** P77 sets keyboard-ready polarity on its own, and
  P75/P76 set the output bit and its polarity, so the real board can make the two bits
  opposite. The strap engine has one `inverter_gate` for both, so a board jumpered that way
  cannot be described.

## Verification

- `test_propio` (unit, `./build/altair_tests propio`): the board is one `serial` unit; the
  preset matches `propioProfile()`; a new board decodes `00`/`01` and nothing else with no
  configuration; the status byte follows the 8024 convention on an idle line and with a key
  waiting; a byte goes both ways through `00`/`01`, and reading it clears keyboard ready;
  overriding the ports to `14`/`15` moves the decode and the data path.
- `test_cli`: `SHOW BOARD propio` lists the board-level straps.
- `acceptance-dualsd`, `acceptance-dualidesd`: each boots the MASTER
  monitor and CP/M 3 on a `propio` console and types commands at it through the real CLI.

## References

- `reference/Console IO Board.md` — the distilled project page.
- `docs/manual/boards.md` §`propio` — the user's view.
- `docs/devguide/serial-io.md` — *Two shapes of serial board*: the strap engine.
- `docs/boards/s100computers-v2z80rom.md` — the monitor EEPROM it is paired with.
